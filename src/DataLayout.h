#pragma once
#include "TreeNode.h"
#include <vector>
#include <memory>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// DataLayout — Disposition des données pour l'inférence HBDT-SumPath
//
// Implémente la Fig. 6 de Shin et al. (HBDT) : organisation des nœuds de
// l'arbre dans les slots d'un chiffretexte CKKS pour que l'inférence se
// ramène à une addition de blocs + un seul produit (O(1) mult. depth).
//
// Structure d'un chiffretexte (M slots au total) :
//
//   [ Bloc N₀ | Bloc N₁ | Bloc N₂ | ... | Bloc Nₘ ]
//    ←n_max×d→ ←n_max×d→
//
//   Chaque bloc encode les valeurs de feature du nœud correspondant
//   (encodage one-hot sur n_max valeurs × d features).
//
// Principe de l'inférence SumPath :
//   Pour chaque nœud Nᵢ : indicateur Iᵢ ∈ {0,1} (0 = condition satisfaite)
//   Les deux ciphertexts "upper" (Iᵢ) et "lower" (Ī_i) sont construits.
//   Addition → seul le slot de la feuille correcte vaut 0.
//   Multiplication par masque aléatoire → isolation du résultat.
//   Rotation aléatoire → obfuscation de la position.
// ─────────────────────────────────────────────────────────────────────────────

struct NodeBlock {
    int  node_id;
    int  feature_index;
    double threshold;
    int  slot_start;   // premier slot de ce bloc dans le vecteur plat
    int  block_size;   // = n_max × n_features dans l'encodage one-hot
    int  depth;
    // Valeurs pré-calculées pour l'inférence en clair (répliquées sur le bloc)
    std::vector<double> model_values;   // valeurs du modèle pré-chiffrées
};

struct LeafBlock {
    int node_id;
    int class_label;
    int slot_position;  // position dans le vecteur résultat
    std::vector<double> leaf_value;   // one-hot du label
};

class DataLayout {
public:
    // Une etape du chemin racine -> feuille.
    // went_left=true signifie que le chemin attend la condition x <= threshold.
    struct PathStep { int node_id; bool went_left; };

    // n_max     : nombre de valeurs possibles par feature (pour one-hot)
    // n_features: dimension de l'input
    // M         : taille totale du vecteur de slots (puissance de 2)
    DataLayout(int n_max, int n_features, int M = 0);

    // ─── Construction depuis l'arbre ────────────────────────────────────────
    // Traverse l'arbre en BFS et construit le mapping nœuds → blocs de slots.
    // Attribue node->block_slot pour chaque nœud interne.
    void buildFromTree(std::shared_ptr<TreeNode>& root);

    // ─── Encodage de l'input ─────────────────────────────────────────────────
    // Encode x ∈ R^d en vecteur de slots one-hot (taille M).
    // La réplication nécessaire pour couvrir tous les blocs est effectuée ici.
    std::vector<double> encodeInput(const std::vector<double>& x) const;

    // ─── Calcul de l'indicateur de chemin (en clair) ─────────────────────────
    // Pour chaque nœud Nᵢ : calcule Iᵢ = φ(threshold - x[feature])
    // (soft) ou Iᵢ = int(x[feature] ≤ threshold) (hard).
    // Retourne le vecteur plat d'indicateurs (taille = nb_internal_nodes).
    std::vector<double> computeNodeIndicators(
        const std::vector<double>& x,
        bool use_soft = false,
        const std::vector<int>& poly_degrees = {},
        const std::vector<double>& soft_windows = {},
        const std::vector<double>& soft_norm_factors = {},
        double soft_window = 0.05) const;

    // ─── SumPath : accumulation des indicateurs ───────────────────────────────
    // Somme les indicateurs sur chaque chemin racine→feuille.
    // Retourne un vecteur de taille nb_leaves : seul le bon chemin vaut 0.
    std::vector<double> sumPath(
        const std::vector<double>& node_indicators) const;

    // ─── Récupération de la feuille prédite ──────────────────────────────────
    // À partir du vecteur sumPath, retourne le label associé à la feuille
    // dont le score est le plus proche de 0.
    int retrieveLeafLabel(const std::vector<double>& path_scores) const;

    // Version vectorisée : retourne directement le label prédit
    int predict(const std::vector<double>& x,
                bool use_soft = false,
                const std::vector<int>& poly_degrees = {},
                const std::vector<double>& soft_windows = {},
                const std::vector<double>& soft_norm_factors = {},
                double soft_window = 0.05) const;

    // ─── Pré-computation des masques aléatoires (pour HE) ────────────────────
    // Génère les masques aléatoires à multiplier avec les résultats
    // pour isoler la bonne feuille (comme dans HBDT Fig. 6-(2)-(B)).
    void generateRandomMasks(unsigned int seed = 42);

    // ─── Accesseurs ─────────────────────────────────────────────────────────
    const std::vector<NodeBlock>& getNodeBlocks() const { return node_blocks_; }
    const std::vector<LeafBlock>& getLeafBlocks() const { return leaf_blocks_; }
    const std::vector<std::vector<PathStep>>& getPaths() const { return paths_; }
    int  getNMax()      const { return n_max_; }
    int  getNFeatures() const { return n_features_; }
    int  getM()         const { return M_; }
    int  getSlotSize()  const { return n_max_ * n_features_; }

    void concealThresholds();

    void printLayout() const;

private:
    int n_max_;
    int n_features_;
    int M_;  // taille du chiffretexte (slots)

    std::vector<NodeBlock> node_blocks_;   // nœuds internes (ordre BFS)
    std::vector<LeafBlock> leaf_blocks_;   // feuilles (ordre BFS)

    // Mapping node_id → index dans node_blocks_
    std::unordered_map<int, int> node_id_to_block_idx_;
    // Mapping node_id → index dans leaf_blocks_
    std::unordered_map<int, int> leaf_id_to_block_idx_;

    // Masques aléatoires (taille = nb_leaves)
    std::vector<double> random_masks_;

    // Chemins racine→feuille pré-calculés
    // paths_[i] = liste des (node_id, direction) pour atteindre la feuille i
    std::vector<std::vector<PathStep>> paths_;

    void buildPaths(const std::shared_ptr<TreeNode>& node,
                    std::vector<PathStep> current_path);
};
