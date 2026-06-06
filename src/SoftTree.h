#pragma once
#include "TreeNode.h"
#include "SoftStepApprox.h"
#include <vector>
#include <memory>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// SoftTree — Inférence avec approximation polynomiale de la fonction echelon
//
// Deux variantes de traversée, les deux basées sur Akavia et al. (récursif) :
//
//   GLOBAL   : un seul polynome phi de degre fixe appliqué a tous les noeuds.
//              Les logits (x[feat]-theta) sont normalisés par norm_factor avant
//              d'etre passés a phi.
//
//   ADAPTIVE : degré adapté par noeud, stocké dans node->poly_degree.
//              Chaque noeud utilise un approximant calibré selon son min_gap.
//
// La traversée est récursive (style Akavia) :
//   predict(v, x) = phi(x[feat]-theta) * predict(right, x)
//                 + phi(theta-x[feat]) * predict(left, x)
// avec la valeur de retour = vecteur one-hot de probabilités (taille nb_classes).
// Le label prédit est l'argmax de ce vecteur.
//
// Note : contrairement a DataLayout/SumPath, cette traversée visite tous les
// chemins — son cout est O(2^depth).  Elle sert de baseline / cross-check.
// ─────────────────────────────────────────────────────────────────────────────

enum class SoftMode { GLOBAL, ADAPTIVE };

class SoftTree {
public:
    SoftTree(std::shared_ptr<TreeNode> root,
             int nb_classes,
             int nb_features);

    // --- Configuration -------------------------------------------------------

    // Mode global : meme polynome pour tous les noeuds
    void configureGlobal(int degree = 8, double window = 0.05,
                         double norm_factor = 2.0);

    // Mode adaptatif : un SoftStepApprox par noeud interne
    // Appelle SoftStepApprox::makeAdaptive(node->min_gap) pour chaque noeud.
    void configureAdaptive(double window = 0.05);

    // --- Prédiction ----------------------------------------------------------

    // Retourne le label prédit (argmax du vecteur de probabilites)
    int predict(const std::vector<double>& x, SoftMode mode = SoftMode::GLOBAL) const;

    // Retourne le vecteur de probabilites brutes (taille nb_classes)
    std::vector<double> predictProba(const std::vector<double>& x,
                                      SoftMode mode = SoftMode::GLOBAL) const;

    // Batch
    std::vector<int> predictBatch(const std::vector<std::vector<double>>& X,
                                   SoftMode mode = SoftMode::GLOBAL) const;

    // --- Informations --------------------------------------------------------
    void printDegrees() const;  // affiche le degré assigné a chaque noeud
    int  getGlobalDegree() const { return global_degree_; }

private:
    std::shared_ptr<TreeNode> root_;
    int nb_classes_;
    int nb_features_;

    // Mode global
    int    global_degree_  = 8;
    double global_window_  = 0.05;
    double norm_factor_    = 2.0;
    SoftStepApprox global_phi_{8, 0.05};

    // Mode adaptatif : mapping node_id -> SoftStepApprox
    std::unordered_map<int, SoftStepApprox> node_approx_;

    // Traversée récursive (retourne vecteur de probabilites one-hot)
    std::vector<double> traverse(const TreeNode* node,
                                  const std::vector<double>& x,
                                  SoftMode mode) const;

    // Evalue phi(t) pour un noeud selon le mode
    double evalPhi(const TreeNode* node, double t, SoftMode mode) const;
};
