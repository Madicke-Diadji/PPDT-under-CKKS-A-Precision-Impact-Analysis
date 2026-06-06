#include "SoftTree.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <queue>
#include <iomanip>

SoftTree::SoftTree(std::shared_ptr<TreeNode> root, int nb_classes, int nb_features)
    : root_(root), nb_classes_(nb_classes), nb_features_(nb_features) {}

// ─── Configuration globale ────────────────────────────────────────────────────
void SoftTree::configureGlobal(int degree, double window, double norm_factor) {
    global_degree_ = degree;
    global_window_ = window;
    norm_factor_   = norm_factor;
    global_phi_    = SoftStepApprox(degree, window);
    std::cout << "[SoftTree] Global : degre=" << degree
              << "  window=" << window
              << "  norm_factor=" << norm_factor << "\n";
}

// ─── Configuration adaptive ───────────────────────────────────────────────────
void SoftTree::configureAdaptive(double window) {
    node_approx_.clear();
    std::queue<std::shared_ptr<TreeNode>> q;
    q.push(root_);
    while (!q.empty()) {
        auto node = q.front(); q.pop();
        if (node->isLeaf()) continue;
        int deg = SoftStepApprox::chooseAdaptiveDegree(node->min_gap);
        node->poly_degree = deg;
        node_approx_.emplace(node->node_id, SoftStepApprox(deg, window));
        q.push(node->left);
        q.push(node->right);
    }
    std::cout << "[SoftTree] Adaptatif : " << node_approx_.size()
              << " noeuds configures\n";
}

// ─── Evaluation de phi selon le mode ─────────────────────────────────────────
double SoftTree::evalPhi(const TreeNode* node, double t, SoftMode mode) const {
    if (mode == SoftMode::GLOBAL) {
        // Normalisation : ramener t dans [-2, 2] via norm_factor
        double t_norm = t / (norm_factor_ + 1e-12);
        t_norm = std::max(-2.0, std::min(2.0, t_norm));
        return global_phi_.eval(t_norm);
    } else {
        // Adaptatif : utilise le SoftStepApprox propre a ce noeud
        auto it = node_approx_.find(node->node_id);
        if (it == node_approx_.end()) {
            // Fallback sur global si non configure
            return global_phi_.eval(std::max(-2.0, std::min(2.0,
                   t / (norm_factor_ + 1e-12))));
        }
        return it->second.eval(std::max(-2.0, std::min(2.0, t)));
    }
}

// ─── Traversée récursive (style Akavia) ───────────────────────────────────────
// Retourne un vecteur de taille nb_classes representant les probabilites.
// Pour chaque noeud interne v :
//   result = phi(x[feat]-theta)   * traverse(right, x)   <- prob. branche droite
//          + phi(theta-x[feat])   * traverse(left,  x)   <- prob. branche gauche
// Pour une feuille :
//   result = leaf_value  (one-hot du label)
std::vector<double> SoftTree::traverse(const TreeNode* node,
                                        const std::vector<double>& x,
                                        SoftMode mode) const {
    if (node->isLeaf()) {
        // Feuille : retourne directement le vecteur de valeurs (one-hot ou distribution)
        if (!node->leaf_value.empty()) return node->leaf_value;
        // Fallback si leaf_value non initialisé
        std::vector<double> onehot(nb_classes_, 0.0);
        if (node->class_label >= 0 && node->class_label < nb_classes_)
            onehot[node->class_label] = 1.0;
        return onehot;
    }

    double x_feat = x[node->feature_index];
    double theta  = node->threshold;

    // phi_right = phi(x - theta)  ≈ 1 si x > theta (va a droite)
    // phi_left  = phi(theta - x)  ≈ 1 si x ≤ theta (va a gauche)
    double phi_right = evalPhi(node, x_feat - theta, mode);
    double phi_left  = evalPhi(node, theta - x_feat, mode);

    auto v_right = traverse(node->right.get(), x, mode);
    auto v_left  = traverse(node->left.get(),  x, mode);

    std::vector<double> result(nb_classes_);
    for (int c = 0; c < nb_classes_; ++c)
        result[c] = phi_right * v_right[c] + phi_left * v_left[c];
    return result;
}

// ─── Prédiction ──────────────────────────────────────────────────────────────
std::vector<double> SoftTree::predictProba(const std::vector<double>& x,
                                             SoftMode mode) const {
    return traverse(root_.get(), x, mode);
}

int SoftTree::predict(const std::vector<double>& x, SoftMode mode) const {
    auto proba = predictProba(x, mode);
    return (int)(std::max_element(proba.begin(), proba.end()) - proba.begin());
}

std::vector<int> SoftTree::predictBatch(const std::vector<std::vector<double>>& X,
                                          SoftMode mode) const {
    std::vector<int> preds(X.size());
    for (size_t i = 0; i < X.size(); ++i) preds[i] = predict(X[i], mode);
    return preds;
}

// ─── Affichage des degrés ────────────────────────────────────────────────────
void SoftTree::printDegrees() const {
    std::cout << "Polynomial degrees by node:\n";
    std::queue<std::shared_ptr<TreeNode>> q;
    q.push(root_);
    while (!q.empty()) {
        auto node = q.front(); q.pop();
        if (node->isLeaf()) continue;
        auto it = node_approx_.find(node->node_id);
        int deg = (it != node_approx_.end()) ? it->second.getDegree() : global_degree_;
        std::cout << "  Noeud #" << node->node_id
                  << "  X[" << node->feature_index << "]<=" << std::fixed
                  << std::setprecision(3) << node->threshold
                  << "  gap=" << node->min_gap
                  << "  degre=" << deg << "\n";
        q.push(node->left);
        q.push(node->right);
    }
}
