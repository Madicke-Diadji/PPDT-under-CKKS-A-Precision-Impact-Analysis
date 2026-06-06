#pragma once
#include <vector>
#include <stdexcept>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// OneHotEncoder — Encodage one-hot des features pour HBDT
//
// Dans le schema HBDT (Shin et al. Fig. 6-(1)-(A)), chaque feature x[j]
// est discretisee en n_bins niveaux uniformes sur [x_min, x_max] (apres
// normalisation dans [0,1]), puis encodee en vecteur one-hot de taille n_bins.
//
// Pour un arbre a d features, l'input encode est un vecteur de taille d*n_bins.
//   slot[j * n_bins + b] = 1  si x[j] tombe dans le bin b,  0 sinon
//
// Ce vecteur est ensuite replique dans les blocs du ciphertext CKKS selon
// la structure definie par DataLayout.
//
// Notes :
//   - Toutes les features sont supposees normalisees dans [0,1] (MinMaxScaler).
//   - n_bins est commun a toutes les features (n_max dans DataLayout).
//   - L'encodage est "hard" (1 seul bin actif) — la version "soft" est dans
//     SoftOneHot pour usage dans les evaluations polynomiales.
// ─────────────────────────────────────────────────────────────────────────────

class OneHotEncoder {
public:
    // n_bins   : nombre de niveaux de discretisation (defaut 16)
    // n_features : dimension de l'input
    // x_min/x_max : bornes de normalisation (defaut [0,1])
    explicit OneHotEncoder(int n_bins, int n_features,
                            double x_min = 0.0, double x_max = 1.0);

    // Encode x ∈ R^d en vecteur one-hot de taille d*n_bins
    std::vector<double> encode(const std::vector<double>& x) const;

    // Encode un seul feature x[j] -> vecteur one-hot de taille n_bins
    std::vector<double> encodeFeature(double xj) const;

    // Version "soft" : au lieu d'un seul 1, les bins voisins recoivent
    // une valeur fractionnaire proportionnelle a la distance (interpolation lineaire).
    // Utile pour les tests de sensibilite / analyse d'erreur.
    std::vector<double> encodeSoft(const std::vector<double>& x) const;
    std::vector<double> encodeFeatureSoft(double xj) const;

    // Decode : retourne la valeur centrale du bin active (inverse approximatif)
    double decodeFeature(const std::vector<double>& onehot) const;

    // Accesseurs
    int  getNBins()     const { return n_bins_; }
    int  getNFeatures() const { return n_features_; }
    int  getTotalSize() const { return n_bins_ * n_features_; }
    double getXMin()   const { return x_min_; }
    double getXMax()   const { return x_max_; }

    // Affiche les bins correspondant a un vecteur encode
    void printEncoding(const std::vector<double>& x) const;

private:
    int    n_bins_;
    int    n_features_;
    double x_min_;
    double x_max_;
    double bin_width_;

    int   getBin(double xj) const;
    double binCenter(int b) const;
};
