#pragma once
#include <vector>
#include <cmath>
#include <stdexcept>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// PolyEval — Evaluation de polynomes en clair
//
// Fournit plusieurs algorithmes d'evaluation :
//
//   1. Horner            — O(n) multiplications, stable, reference
//   2. Baby-Step Giant-Step (BSGS) — structure reutilisee pour EvalPoly sous CKKS,
//      decompose p(x) = sum_i q_i(x) * x^{k*i} avec k=ceil(sqrt(n))
//   3. Clenshaw           — evaluation stable en base de Chebyshev
//
// Usage : verifier la coherence entre evaluation en clair et chiffree,
// et calculer les profondeurs multiplicatives theoriques.
// ─────────────────────────────────────────────────────────────────────────────

class PolyEval {
public:
    // 1. Schema de Horner : coeffs[k] = coeff de x^k
    static double horner(const std::vector<double>& coeffs, double x);

    // Horner avec clamp [lo, hi] (utile pour phi dans [0,1])
    static double hornerClamped(const std::vector<double>& coeffs, double x,
                                double lo = 0.0, double hi = 1.0);

    // 2. Baby-Step Giant-Step : meme resultat que Horner,
    // structure BSGS avec k = ceil(sqrt(n)) baby steps.
    // Profondeur multiplicative = ceil(log2(k)) + ceil(log2(ceil(n/k))) = O(log n)
    static double bsgs(const std::vector<double>& coeffs, double x);

    // 3. Algorithme de Clenshaw pour base de Chebyshev
    // T_0=1, T_1=x, T_k = 2x*T_{k-1} - T_{k-2}
    static double clenshaw(const std::vector<double>& cheb_coeffs, double x);

    // Conversion monomiale <-> Chebyshev
    static std::vector<double> monomialToChebyshev(const std::vector<double>& mono);
    static std::vector<double> chebyshevToMonomial(const std::vector<double>& cheb);

    // Profondeur multiplicative theorique BSGS pour degree n
    static int bsgsMultDepth(int degree);

    // Erreur max entre deux evaluations sur une grille
    static double maxError(const std::vector<double>& coeffs1,
                           const std::vector<double>& coeffs2,
                           double xmin = -2.0, double xmax = 2.0,
                           int n_pts = 1000);

    // Erreur MSE de l'approximation vs I0 (fonction echelon)
    static double stepApproxMSE(const std::vector<double>& coeffs,
                                 double window = 0.0,
                                 int n_pts = 2000);

    // Affichage de l'evaluation sur une grille
    static void printEvalGrid(const std::vector<double>& coeffs,
                               double xmin = -2.0, double xmax = 2.0,
                               int n_pts = 20);
};
