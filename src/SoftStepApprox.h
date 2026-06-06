#pragma once
#include <vector>
#include <cmath>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// SoftStepApprox — Approximation polynomiale de la fonction échelon I₀
//
// Deux modes conformes aux travaux du POC :
//
//   GLOBAL   : un polynôme de degré fixe appliqué uniformément à tous les nœuds.
//              Les coefficients sont calculés une fois via moindres carrés pondérés
//              sur [-2, 2] (Akavia et al.).
//              La norme des logits peut être appliquée pour stabiliser les entrées.
//
//   ADAPTIVE : degré adapté par nœud selon le min_gap collecté sur les données.
//              Un nœud avec un grand gap peut utiliser un degré faible (moins coûteux).
//              Un nœud avec un petit gap (décision difficile) utilise un degré élevé.
//
// Convention : φ(t) ≈ 1 si t ≥ 0  (x ≤ θ quand t = θ - x)
//              φ(t) ≈ 0 si t < 0  (x > θ quand t = θ - x)
//
// Pour la traversée HBDT-SumPath on utilise :
//   left_i  = φ(threshold - x[feature])   ≈ 1 si x ≤ θ
//   I_i     = 1 - left_i                  ≈ 0 si x ≤ θ, ≈ 1 si x > θ
// où I_i est l'indicateur effectivement propagé dans SumPath.
// ─────────────────────────────────────────────────────────────────────────────

struct PolyCoeffs {
    std::vector<double> coeffs;  // coeffs[k] = coefficient du terme x^k
    int degree() const { return static_cast<int>(coeffs.size()) - 1; }
};

class SoftStepApprox {
public:
    // ─── Construction ───────────────────────────────────────────────────────
    // degree : degré du polynôme d'approximation (typiquement 4, 8, 16, 32)
    // window : demi-largeur de la fenêtre ignorée autour du seuil (δ dans Akavia)
    //          w(x) = 0 si |x| < window, = 1 sinon
    // n_pts  : nombre de points d'échantillonnage pour les moindres carrés
    explicit SoftStepApprox(int degree = 8, double window = 0.05, int n_pts = 2000);

    // ─── Évaluation ─────────────────────────────────────────────────────────
    // Évalue φ(t) pour une valeur scalaire t ∈ [-2, 2]
    double eval(double t) const;

    // Évalue φ(x[feature] - threshold) — soft-comparateur "x > θ"
    // Retourne valeur proche de 1 si x > θ, proche de 0 si x ≤ θ
    double evalComparison(double x_feat, double threshold) const;

    // Évalue φ(threshold - x[feature]) — soft-indicator "x ≤ θ" (branche gauche)
    // Retourne valeur proche de 1 si x ≤ θ (condition HBDT satisfaite)
    double evalLeftIndicator(double x_feat, double threshold) const;

    // ─── Mode adaptatif ─────────────────────────────────────────────────────
    // Choisit le degré minimal tel que l'approximation soit précise à 'tol' près
    // compte tenu du min_gap du nœud.
    // Règle empirique issue d'Akavia : degré ~ ceil(log(1/tol) / log(gap / 2))
    // + contrainte : degré ∈ {4, 8, 16, 32} (puissances de 2 pour BSGS HE)
    static int chooseAdaptiveDegree(double min_gap,
                                    double tol = 0.01,
                                    int   max_degree = 32);

    // Construit une nouvelle SoftStepApprox avec le degré adaptatif
    static SoftStepApprox makeAdaptive(double min_gap,
                                       double window = 0.05,
                                       int    n_pts = 2000);

    // ─── Normalisation des logits ────────────────────────────────────────────
    // Pour le mode GLOBAL, on peut normaliser (x - threshold) par une norme
    // globale avant d'appliquer φ, afin de ramener l'argument dans [-2, 2].
    // norm_factor = max_{nœuds} |x[feat] - threshold| sur le dataset de calibration
    void setNormFactor(double norm_factor) { norm_factor_ = norm_factor; }
    double getNormFactor() const { return norm_factor_; }

    double evalNormalized(double x_feat, double threshold) const;

    // ─── Accesseurs ─────────────────────────────────────────────────────────
    const PolyCoeffs& getCoeffs() const { return poly_; }
    int    getDegree()     const { return poly_.degree(); }
    double getWindow()     const { return window_; }

    void printCoeffs() const;

private:
    PolyCoeffs poly_;
    double     window_;
    double     norm_factor_ = 1.0;

    // Calcule les coefficients par moindres carrés pondérés (projection sur Pn)
    void fitMSE(int degree, double window, int n_pts);

    // Évaluation du polynôme par schéma de Horner
    double horner(double t) const;
};
