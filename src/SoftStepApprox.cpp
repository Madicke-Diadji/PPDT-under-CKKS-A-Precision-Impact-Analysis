#include "SoftStepApprox.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>

// ─── Résolution des moindres carrés pondérés ──────────────────────────────────
// On utilise une décomposition naïve (Gram-Schmidt / Vandermonde) sur n_pts points.
// Pour un vrai POC, on peut substituer Eigen::BDCSVD pour plus de stabilité.
//
// Problème : min_{p ∈ Pn}  ∫_{-2}^{2} (I₀(x) − p(x))² · w(x) dx
//
// w(x) = 0  si |x| < window
//       = 1  sinon
//
// I₀(x) = 1 si x ≥ 0, = 0 sinon  (seuil centré à 0)

static double I0(double x) { return (x >= 0.0) ? 1.0 : 0.0; }

void SoftStepApprox::fitMSE(int degree, double window, int n_pts) {
    // Points d'échantillonnage uniformes sur [-2, 2]
    std::vector<double> pts(n_pts), weights(n_pts), targets(n_pts);
    for (int i = 0; i < n_pts; ++i) {
        double x = -2.0 + 4.0 * i / (n_pts - 1);
        pts[i]     = x;
        weights[i] = (std::abs(x) < window) ? 0.0 : 1.0;
        targets[i] = I0(x);
    }

    // Matrice de Vandermonde V (n_pts × (degree+1))
    int n = degree + 1;
    std::vector<std::vector<double>> V(n_pts, std::vector<double>(n, 0.0));
    for (int i = 0; i < n_pts; ++i) {
        double xp = 1.0;
        for (int j = 0; j < n; ++j) {
            V[i][j] = weights[i] * xp;
            xp *= pts[i];
        }
    }
    // Wy = diag(weights) · targets
    std::vector<double> Wy(n_pts);
    for (int i = 0; i < n_pts; ++i) Wy[i] = weights[i] * targets[i];

    // Système normal : (Vᵀ·V)·c = Vᵀ·Wy  (dimensions n×n)
    std::vector<std::vector<double>> A(n, std::vector<double>(n, 0.0));
    std::vector<double> b(n, 0.0);
    for (int j = 0; j < n; ++j) {
        for (int k = 0; k < n; ++k)
            for (int i = 0; i < n_pts; ++i)
                A[j][k] += V[i][j] * V[i][k] / weights[i == 0 ? 1e-9 : 1];
        // Correction : utiliser les colonnes originales de V sans pondération double
    }
    // Recalcul propre
    for (int j = 0; j < n; ++j) {
        b[j] = 0.0;
        for (int i = 0; i < n_pts; ++i) {
            double vij = (std::abs(pts[i]) < window) ? 0.0 : 1.0;
            double xp = std::pow(pts[i], j);
            b[j] += vij * xp * targets[i];
        }
        for (int k = 0; k < n; ++k) {
            A[j][k] = 0.0;
            for (int i = 0; i < n_pts; ++i) {
                double w_i = (std::abs(pts[i]) < window) ? 0.0 : 1.0;
                A[j][k] += w_i * std::pow(pts[i], j) * std::pow(pts[i], k);
            }
        }
    }

    // Élimination de Gauss avec pivot partiel
    poly_.coeffs.assign(n, 0.0);
    std::vector<std::vector<double>> Ab(n, std::vector<double>(n + 1));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) Ab[i][j] = A[i][j];
        Ab[i][n] = b[i];
    }
    for (int col = 0; col < n; ++col) {
        // Pivot
        int maxRow = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs(Ab[row][col]) > std::abs(Ab[maxRow][col])) maxRow = row;
        std::swap(Ab[col], Ab[maxRow]);
        if (std::abs(Ab[col][col]) < 1e-12) continue;  // dégénéré
        for (int row = 0; row < n; ++row) {
            if (row == col) continue;
            double f = Ab[row][col] / Ab[col][col];
            for (int j = col; j <= n; ++j) Ab[row][j] -= f * Ab[col][j];
        }
    }
    for (int i = 0; i < n; ++i)
        if (std::abs(Ab[i][i]) > 1e-12)
            poly_.coeffs[i] = Ab[i][n] / Ab[i][i];
}

SoftStepApprox::SoftStepApprox(int degree, double window, int n_pts)
    : window_(window) {
    fitMSE(degree, window, n_pts);
}

// ─── Évaluation par schéma de Horner ─────────────────────────────────────────
double SoftStepApprox::horner(double t) const {
    const auto& c = poly_.coeffs;
    double result = c.back();
    for (int i = static_cast<int>(c.size()) - 2; i >= 0; --i)
        result = result * t + c[i];
    return result;
}

double SoftStepApprox::eval(double t) const {
    return std::max(0.0, std::min(1.0, horner(t)));  // clamp [0,1] pour stabilité
}

double SoftStepApprox::evalComparison(double x_feat, double threshold) const {
    return eval(x_feat - threshold);  // ≈ 1 si x > θ
}

double SoftStepApprox::evalLeftIndicator(double x_feat, double threshold) const {
    return eval(threshold - x_feat);  // ≈ 1 si x ≤ θ (condition HBDT)
}

double SoftStepApprox::evalNormalized(double x_feat, double threshold) const {
    double t = (x_feat - threshold) / (norm_factor_ + 1e-12);
    t = std::max(-2.0, std::min(2.0, t));  // projection dans [-2, 2]
    return eval(t);
}

// ─── Choix adaptatif du degré ─────────────────────────────────────────────────
// Règle : plus le gap est petit, plus le polynôme doit être de degré élevé
// pour discriminer correctement autour du seuil.
// Degrés autorisés : {4, 8, 16, 32} (compatibles Baby-Step-Giant-Step HE)
int SoftStepApprox::chooseAdaptiveDegree(double min_gap, double tol, int max_degree) {
    (void)tol;
    if (min_gap <= 0.0) return max_degree;
    // Heuristique volontairement simple et interpretable pour le POC.
    // Plus le gap local est petit, plus on augmente le degre.
    // Les seuils ci-dessous visent a rendre l'adaptativite visible sur des
    // arbres peu profonds comme Iris, au lieu de saturer tous les noeuds a 32.
    if (min_gap >= 0.08 && max_degree >= 4) {
        return 4;
    }
    if (min_gap >= 0.04 && max_degree >= 8) {
        return 8;
    }
    if (min_gap >= 0.015 && max_degree >= 16) {
        return 16;
    }
    return max_degree;
}

SoftStepApprox SoftStepApprox::makeAdaptive(double min_gap, double window, int n_pts) {
    int deg = chooseAdaptiveDegree(min_gap);
    return SoftStepApprox(deg, window, n_pts);
}

void SoftStepApprox::printCoeffs() const {
    std::cout << "Polynôme degré " << getDegree() << " [window=" << window_ << "] :\n";
    for (int i = 0; i <= getDegree(); ++i)
        std::cout << "  c[" << i << "] = " << std::setprecision(8) << poly_.coeffs[i] << "\n";
}
