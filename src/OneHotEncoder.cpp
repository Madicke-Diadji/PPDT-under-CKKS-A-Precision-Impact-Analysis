#include "OneHotEncoder.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <stdexcept>

OneHotEncoder::OneHotEncoder(int n_bins, int n_features, double x_min, double x_max)
    : n_bins_(n_bins), n_features_(n_features),
      x_min_(x_min), x_max_(x_max) {
    if (n_bins_ < 2) throw std::invalid_argument("n_bins doit etre >= 2");
    if (n_features_ < 1) throw std::invalid_argument("n_features doit etre >= 1");
    if (x_max_ <= x_min_) throw std::invalid_argument("x_max doit etre > x_min");
    bin_width_ = (x_max_ - x_min_) / n_bins_;
}

// ─── Bin d'une valeur ──────────────────────────────────────────────────────
int OneHotEncoder::getBin(double xj) const {
    int b = (int)std::floor((xj - x_min_) / bin_width_);
    return std::max(0, std::min(n_bins_ - 1, b));
}

double OneHotEncoder::binCenter(int b) const {
    return x_min_ + (b + 0.5) * bin_width_;
}

// ─── Encodage hard ──────────────────────────────────────────────────────────
std::vector<double> OneHotEncoder::encodeFeature(double xj) const {
    std::vector<double> onehot(n_bins_, 0.0);
    onehot[getBin(xj)] = 1.0;
    return onehot;
}

std::vector<double> OneHotEncoder::encode(const std::vector<double>& x) const {
    if ((int)x.size() != n_features_)
        throw std::invalid_argument("encode: taille de x incorrecte");
    std::vector<double> result(n_bins_ * n_features_, 0.0);
    for (int j = 0; j < n_features_; ++j) {
        int b = getBin(x[j]);
        result[j * n_bins_ + b] = 1.0;
    }
    return result;
}

// ─── Encodage soft (interpolation lineaire entre deux bins voisins) ───────
// Si xj tombe a la distance alpha du centre du bin b (et 1-alpha du bin b+1),
// le bin b recoit (1-alpha) et le bin b+1 recoit alpha.
// Utile pour tester la robustesse de l'inférence aux erreurs de quantification.
std::vector<double> OneHotEncoder::encodeFeatureSoft(double xj) const {
    std::vector<double> soft(n_bins_, 0.0);
    // Position relative dans [0, n_bins]
    double pos = (xj - x_min_) / bin_width_ - 0.5;
    int b_lo = (int)std::floor(pos);
    int b_hi = b_lo + 1;
    double alpha = pos - b_lo;  // part attribuee a b_hi

    if (b_lo >= 0 && b_lo < n_bins_)  soft[b_lo] = 1.0 - alpha;
    if (b_hi >= 0 && b_hi < n_bins_)  soft[b_hi] = alpha;

    // Cas bords
    if (b_lo < 0)          { soft[0]        = 1.0; }
    if (b_hi >= n_bins_)   { soft[n_bins_-1] = 1.0; }

    return soft;
}

std::vector<double> OneHotEncoder::encodeSoft(const std::vector<double>& x) const {
    if ((int)x.size() != n_features_)
        throw std::invalid_argument("encodeSoft: taille de x incorrecte");
    std::vector<double> result(n_bins_ * n_features_, 0.0);
    for (int j = 0; j < n_features_; ++j) {
        auto feat_soft = encodeFeatureSoft(x[j]);
        for (int b = 0; b < n_bins_; ++b)
            result[j * n_bins_ + b] = feat_soft[b];
    }
    return result;
}

// ─── Decodage ────────────────────────────────────────────────────────────────
double OneHotEncoder::decodeFeature(const std::vector<double>& onehot) const {
    if ((int)onehot.size() != n_bins_)
        throw std::invalid_argument("decodeFeature: taille incorrecte");
    // Retourne le centre du bin avec la valeur maximale
    int best = (int)(std::max_element(onehot.begin(), onehot.end()) - onehot.begin());
    return binCenter(best);
}

// ─── Affichage ────────────────────────────────────────────────────────────────
void OneHotEncoder::printEncoding(const std::vector<double>& x) const {
    std::cout << "OneHotEncoder (n_bins=" << n_bins_
              << ", n_features=" << n_features_ << "):\n";
    for (int j = 0; j < n_features_; ++j) {
        int b = getBin(x[j]);
        std::cout << "  x[" << j << "]=" << std::fixed << std::setprecision(4)
                  << x[j] << " -> bin " << b
                  << " [" << binCenter(b) - bin_width_/2
                  << ", " << binCenter(b) + bin_width_/2 << ")\n";
    }
}
