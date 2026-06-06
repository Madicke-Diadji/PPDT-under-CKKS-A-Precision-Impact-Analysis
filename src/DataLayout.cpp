#include "DataLayout.h"
#include "SoftStepApprox.h"
#include <queue>
#include <random>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>

DataLayout::DataLayout(int n_max, int n_features, int M)
    : n_max_(n_max), n_features_(n_features), M_(M) {}

// ─── Construction BFS ─────────────────────────────────────────────────────────
void DataLayout::buildFromTree(std::shared_ptr<TreeNode>& root) {
    if (!root) throw std::runtime_error("DataLayout::buildFromTree — arbre vide");

    node_blocks_.clear();
    leaf_blocks_.clear();
    node_id_to_block_idx_.clear();
    leaf_id_to_block_idx_.clear();
    paths_.clear();

    int slot_cursor = 0;
    int block_size  = n_max_ * n_features_;

    std::queue<std::shared_ptr<TreeNode>> q;
    q.push(root);
    int leaf_pos = 0;

    while (!q.empty()) {
        auto node = q.front(); q.pop();

        if (node->isLeaf()) {
            LeafBlock lb;
            lb.node_id      = node->node_id;
            lb.class_label  = node->class_label;
            lb.slot_position = leaf_pos++;
            lb.leaf_value   = node->leaf_value;
            leaf_id_to_block_idx_[node->node_id] =
                static_cast<int>(leaf_blocks_.size());
            leaf_blocks_.push_back(lb);
        } else {
            NodeBlock nb;
            nb.node_id       = node->node_id;
            nb.feature_index = node->feature_index;
            nb.threshold     = node->threshold;
            nb.slot_start    = slot_cursor;
            nb.block_size    = block_size;
            nb.depth         = node->depth;
            // Valeurs modèle pré-calculées (pour les opérations HE ultérieures)
            nb.model_values.assign(block_size, 0.0);
            // Pour l'instant on encode juste le threshold dans les slots correspondants
            for (int s = 0; s < n_max_; ++s)
                if (s < static_cast<int>(node->threshold * n_max_))
                    nb.model_values[node->feature_index * n_max_ + s] = 1.0;

            node->block_slot = slot_cursor;
            node_id_to_block_idx_[node->node_id] =
                static_cast<int>(node_blocks_.size());
            node_blocks_.push_back(nb);
            slot_cursor += block_size;

            q.push(node->left);
            q.push(node->right);
        }
    }

    // Si M non spécifié, on le calcule comme puissance de 2 couvrant tous les slots
    if (M_ <= 0) {
        int total = slot_cursor;
        M_ = 1;
        while (M_ < total) M_ *= 2;
    }

    // Pré-calcul des chemins racine→feuille
    buildPaths(root, {});

    std::cout << "[DataLayout] " << node_blocks_.size() << " blocs nœuds, "
              << leaf_blocks_.size() << " feuilles, M=" << M_ << " slots\n";
}

void DataLayout::buildPaths(const std::shared_ptr<TreeNode>& node,
                             std::vector<PathStep> current_path) {
    if (!node) return;
    if (node->isLeaf()) {
        auto it = leaf_id_to_block_idx_.find(node->node_id);
        if (it != leaf_id_to_block_idx_.end()) {
            if (paths_.size() < leaf_blocks_.size()) {
                paths_.resize(leaf_blocks_.size());
            }
            paths_[it->second] = std::move(current_path);
        }
        return;
    }
    PathStep step_left  = { node->node_id, true  };
    PathStep step_right = { node->node_id, false };
    auto path_left  = current_path; path_left.push_back(step_left);
    auto path_right = current_path; path_right.push_back(step_right);
    buildPaths(node->left,  path_left);
    buildPaths(node->right, path_right);
}

// ─── Encodage one-hot de l'input ──────────────────────────────────────────────
// Chaque feature x[j] ∈ [0,1] est discrétisée en n_max bins.
// Le vecteur de slots replique cet encodage sur tous les blocs de nœuds.
std::vector<double> DataLayout::encodeInput(const std::vector<double>& x) const {
    std::vector<double> slots(M_, 0.0);
    // Pour chaque bloc nœud, on copie l'encodage de la feature correspondante
    for (const auto& nb : node_blocks_) {
        int j = nb.feature_index;
        // Bin de x[j] dans [0, 1] discrétisé en n_max_ niveaux
        int bin = static_cast<int>(std::floor(x[j] * n_max_));
        bin = std::max(0, std::min(n_max_ - 1, bin));
        // Encodage one-hot dans les slots du bloc
        for (int s = 0; s < n_max_; ++s) {
            int slot = nb.slot_start + j * n_max_ + s;
            if (slot < M_)
                slots[slot] = (s == bin) ? 1.0 : 0.0;
        }
    }
    return slots;
}

// ─── Indicateurs de nœud ─────────────────────────────────────────────────────
// Pour chaque nœud Nᵢ : Iᵢ = 0 si condition satisfaite (x[feat] ≤ threshold)
//                           = 1 sinon
std::vector<double> DataLayout::computeNodeIndicators(
    const std::vector<double>& x,
    bool use_soft,
    const std::vector<int>& poly_degrees,
    const std::vector<double>& soft_windows,
    const std::vector<double>& soft_norm_factors,
    double soft_window) const {

    std::vector<double> indicators(node_blocks_.size());

    for (size_t i = 0; i < node_blocks_.size(); ++i) {
        const auto& nb = node_blocks_[i];
        double x_feat = x[nb.feature_index];
        double theta  = nb.threshold;

        if (!use_soft) {
            // Hard : 0 si condition satisfaite, 1 sinon
            indicators[i] = (x_feat <= theta) ? 0.0 : 1.0;
        } else {
            // Soft : left_i = φ(theta - x_feat) ≈ 1 si x ≤ theta, ≈ 0 sinon
            // L'indicateur propague ensuite I_i = 1 - left_i, soit :
            // I_i ≈ 0 si la branche gauche est correcte, ≈ 1 sinon.
            int deg = (!poly_degrees.empty() && i < poly_degrees.size())
                      ? poly_degrees[i] : 8;
            double node_window = (!soft_windows.empty() && i < soft_windows.size()) ? soft_windows[i] : soft_window;
            SoftStepApprox phi(deg, node_window);
            double phi_val = 0.0;
            if (!soft_norm_factors.empty() && i < soft_norm_factors.size()) {
                phi.setNormFactor(soft_norm_factors[i]);
                phi_val = phi.evalNormalized(theta, x_feat);
            } else {
                phi_val = phi.evalLeftIndicator(x_feat, theta);
            }
            indicators[i] = 1.0 - phi_val;  // ≈ 0 si condition OK
        }
    }
    return indicators;
}

// ─── SumPath ─────────────────────────────────────────────────────────────────
// Pour chaque chemin racine→feuille, somme les indicateurs des nœuds sur ce chemin.
// Le bon chemin a une somme = 0 (toutes conditions satisfaites → tous Iᵢ = 0).
std::vector<double> DataLayout::sumPath(
    const std::vector<double>& node_indicators) const {

    std::vector<double> scores(leaf_blocks_.size(), 0.0);

    for (size_t leaf_idx = 0; leaf_idx < paths_.size(); ++leaf_idx) {
        const auto& path = paths_[leaf_idx];
        double score = 0.0;
        for (const auto& step : path) {
            auto it = node_id_to_block_idx_.find(step.node_id);
            if (it == node_id_to_block_idx_.end()) continue;
            int block_idx = it->second;
            double I_i = node_indicators[block_idx];
            // On accumule Iᵢ si on va gauche, et (1 - Iᵢ) si on va droite
            // pour reproduire le mécanisme de HBDT (upper + lower blocks)
            if (step.went_left)
                score += I_i;           // Iᵢ = 0 si condition vraie (on va gauche)
            else
                score += (1.0 - I_i);  // Ī_i = 0 si condition fausse (on va droite)
        }
        scores[leaf_idx] = score;
    }
    return scores;
}

// ─── Récupération de la feuille prédite ──────────────────────────────────────
int DataLayout::retrieveLeafLabel(const std::vector<double>& path_scores) const {
    // La bonne feuille a le score le plus proche de 0
    double min_score = std::numeric_limits<double>::max();
    int best_leaf    = 0;
    for (size_t i = 0; i < path_scores.size(); ++i) {
        if (path_scores[i] < min_score) {
            min_score = path_scores[i];
            best_leaf = static_cast<int>(i);
        }
    }
    return leaf_blocks_[best_leaf].class_label;
}

// ─── Prédiction complète ─────────────────────────────────────────────────────
int DataLayout::predict(const std::vector<double>& x,
                        bool use_soft,
                        const std::vector<int>& poly_degrees,
                        const std::vector<double>& soft_windows,
                        const std::vector<double>& soft_norm_factors,
                        double soft_window) const {
    auto indicators  = computeNodeIndicators(
        x, use_soft, poly_degrees, soft_windows, soft_norm_factors, soft_window);
    auto path_scores = sumPath(indicators);
    return retrieveLeafLabel(path_scores);
}

// ─── Masques aléatoires ───────────────────────────────────────────────────────
void DataLayout::generateRandomMasks(unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(1.0, 10.0);
    random_masks_.resize(leaf_blocks_.size());
    for (auto& m : random_masks_) m = dist(rng);
}

void DataLayout::concealThresholds() {
    for (auto& nb : node_blocks_) {
        nb.threshold = std::numeric_limits<double>::quiet_NaN();
    }
}

// ─── Affichage ────────────────────────────────────────────────────────────────
void DataLayout::printLayout() const {
    std::cout << "\n=== DataLayout (HBDT-SumPath) ===\n"
              << "  n_max     = " << n_max_     << "\n"
              << "  n_features= " << n_features_ << "\n"
              << "  M (slots) = " << M_          << "\n"
              << "  block_size= " << getSlotSize() << " slots/nœud\n"
              << "  Nœuds internes : " << node_blocks_.size() << "\n"
              << "  Feuilles       : " << leaf_blocks_.size()  << "\n"
              << "  Chemins        : " << paths_.size()         << "\n\n";

    for (const auto& nb : node_blocks_) {
        std::cout << "  [Bloc " << std::setw(2) << nb.node_id << "]"
                  << "  slot_start=" << std::setw(4) << nb.slot_start
                  << "  X[" << nb.feature_index << "] ≤ ";
        if (std::isnan(nb.threshold)) {
            std::cout << "<seuil chiffre>";
        } else {
            std::cout << std::fixed << std::setprecision(3) << nb.threshold;
        }
        std::cout
                  << "  profondeur=" << nb.depth << "\n";
    }
    for (const auto& lb : leaf_blocks_) {
        std::cout << "  [Feuille " << std::setw(2) << lb.node_id << "]"
                  << "  pos=" << lb.slot_position
                  << "  label=" << lb.class_label << "\n";
    }
}
