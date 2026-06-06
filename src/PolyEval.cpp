#include "PolyEval.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ─── 1. Schema de Horner ──────────────────────────────────────────────────────
double PolyEval::horner(const std::vector<double>& c, double x) {
    if (c.empty()) return 0.0;
    double result = c.back();
    for (int i = (int)c.size() - 2; i >= 0; --i)
        result = result * x + c[i];
    return result;
}

double PolyEval::hornerClamped(const std::vector<double>& c, double x,
                                double lo, double hi) {
    double v = horner(c, x);
    return std::max(lo, std::min(hi, v));
}

// ─── 2. Baby-Step Giant-Step ──────────────────────────────────────────────────
// Pour p(x) = sum_{j=0}^{n} c_j * x^j  avec n = deg(p)
// Choisit k = ceil(sqrt(n+1)).
// Ecrit p(x) = sum_{i=0}^{B-1} Q_i(x) * (x^k)^i
// ou Q_i(x) = sum_{j=0}^{k-1} c_{i*k+j} * x^j  (polynome de degre < k)
//
// Etape 1 (baby steps)  : calcule x, x^2, ..., x^{k-1}   -> k-1 mult
// Etape 2 (giant step)  : calcule X = x^k                 -> log2(k) mult (expo rapide)
// Etape 3               : calcule X^0, X^1, ..., X^{B-1}  -> B-1 mult
// Etape 4               : evalue chaque Q_i par Horner sur les baby steps, multiplie par X^i
// Profondeur totale ~ ceil(log2(k)) + ceil(log2(B))
double PolyEval::bsgs(const std::vector<double>& c, double x) {
    int n = (int)c.size() - 1;
    if (n < 0) return 0.0;
    if (n == 0) return c[0];

    int k = (int)std::ceil(std::sqrt((double)(n + 1)));
    int B = (n / k) + 1;  // nombre de blocs

    // Baby steps : puissances de x jusqu'a x^{k-1}
    std::vector<double> baby(k, 1.0);
    for (int j = 1; j < k; ++j) baby[j] = baby[j-1] * x;

    // Giant step : X = x^k
    double X = baby[k > 1 ? k-1 : 0] * x;  // x^k

    // Puissances de X : X^0, X^1, ..., X^{B-1}
    std::vector<double> giant(B, 1.0);
    for (int i = 1; i < B; ++i) giant[i] = giant[i-1] * X;

    // Evaluation : pour chaque bloc i, calcule Q_i(x) par Horner sur baby steps
    double result = 0.0;
    for (int i = 0; i < B; ++i) {
        // Q_i(x) = c_{i*k} + c_{i*k+1}*x + ... + c_{i*k+k-1}*x^{k-1}
        double qi = 0.0;
        for (int j = k - 1; j >= 0; --j) {
            int idx = i * k + j;
            double coeff = (idx < (int)c.size()) ? c[idx] : 0.0;
            qi = qi * x + coeff;
        }
        result += qi * giant[i];
    }
    return result;
}

// ─── 3. Clenshaw (base Chebyshev) ────────────────────────────────────────────
// p(x) = sum_{k=0}^{n} a_k * T_k(x)
// Algorithme de Clenshaw : b_n=0, b_{n+1}=0,
// b_k = a_k + 2x*b_{k+1} - b_{k+2}  pour k=n,...,1
// resultat = a_0 + x*b_1 - b_2
double PolyEval::clenshaw(const std::vector<double>& a, double x) {
    int n = (int)a.size() - 1;
    if (n < 0) return 0.0;
    if (n == 0) return a[0];
    double b2 = 0.0, b1 = 0.0;
    for (int k = n; k >= 1; --k) {
        double b0 = a[k] + 2.0 * x * b1 - b2;
        b2 = b1; b1 = b0;
    }
    return a[0] + x * b1 - b2;
}

// ─── Conversions ──────────────────────────────────────────────────────────────
// Monomiale -> Chebyshev : resolution du systeme par regression sur les zeros de T_n
std::vector<double> PolyEval::chebyshevToMonomial(const std::vector<double>& cheb) {
    int n = (int)cheb.size();
    // T_0 = [1], T_1 = [0, 1], T_k = 2x*T_{k-1} - T_{k-2}
    std::vector<std::vector<double>> T(n, std::vector<double>(n, 0.0));
    if (n > 0) T[0][0] = 1.0;
    if (n > 1) T[1][1] = 1.0;
    for (int k = 2; k < n; ++k) {
        // T_k = 2x*T_{k-1} - T_{k-2}
        for (int j = 1; j < n; ++j) T[k][j] += 2.0 * T[k-1][j-1];
        for (int j = 0; j < n; ++j) T[k][j] -= T[k-2][j];
    }
    std::vector<double> mono(n, 0.0);
    for (int k = 0; k < n; ++k)
        for (int j = 0; j < n; ++j)
            mono[j] += cheb[k] * T[k][j];
    return mono;
}

std::vector<double> PolyEval::monomialToChebyshev(const std::vector<double>& mono) {
    // Projection par evaluation aux n+1 points de Chebyshev x_j = cos(pi*(2j+1)/(2n+2))
    int n = (int)mono.size() - 1;
    int N = n + 1;
    std::vector<double> cheb(N, 0.0);
    for (int k = 0; k < N; ++k) {
        for (int j = 0; j < N; ++j) {
            double x_j = std::cos(kPi * (2.0*j + 1.0) / (2.0*N));
            double Tk_xj = std::cos(k * std::acos(x_j));
            cheb[k] += horner(mono, x_j) * Tk_xj;
        }
        cheb[k] *= (k == 0) ? (1.0 / N) : (2.0 / N);
    }
    return cheb;
}

// ─── Profondeur multiplicative BSGS ──────────────────────────────────────────
int PolyEval::bsgsMultDepth(int degree) {
    if (degree <= 1) return 0;
    int k = (int)std::ceil(std::sqrt((double)(degree + 1)));
    int B = degree / k + 1;
    // Depth pour baby steps + giant step + combinaison
    int d_baby  = (int)std::ceil(std::log2((double)k));       // x^2, x^4, ..., x^k
    int d_giant = (int)std::ceil(std::log2((double)B));       // X^2, ..., X^B
    return d_baby + d_giant + 1;                               // +1 pour la combinaison finale
}

// ─── Metriques ────────────────────────────────────────────────────────────────
double PolyEval::maxError(const std::vector<double>& c1,
                           const std::vector<double>& c2,
                           double xmin, double xmax, int n_pts) {
    double maxErr = 0.0;
    for (int i = 0; i < n_pts; ++i) {
        double x = xmin + (xmax - xmin) * i / (n_pts - 1);
        double err = std::abs(horner(c1, x) - horner(c2, x));
        maxErr = std::max(maxErr, err);
    }
    return maxErr;
}

double PolyEval::stepApproxMSE(const std::vector<double>& coeffs,
                                 double window, int n_pts) {
    double mse = 0.0;
    int count = 0;
    for (int i = 0; i < n_pts; ++i) {
        double x = -2.0 + 4.0 * i / (n_pts - 1);
        if (std::abs(x) < window) continue;
        double I0 = (x >= 0.0) ? 1.0 : 0.0;
        double phi = hornerClamped(coeffs, x);
        mse += (I0 - phi) * (I0 - phi);
        count++;
    }
    return count > 0 ? mse / count : 0.0;
}

void PolyEval::printEvalGrid(const std::vector<double>& coeffs,
                               double xmin, double xmax, int n_pts) {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  x          phi(x)   I0(x)   err\n";
    std::cout << "  ---------  -------  ------  ------\n";
    for (int i = 0; i <= n_pts; ++i) {
        double x   = xmin + (xmax - xmin) * i / n_pts;
        double phi = hornerClamped(coeffs, x);
        double I0  = (x >= 0.0) ? 1.0 : 0.0;
        std::cout << "  " << std::setw(9) << x
                  << "  " << std::setw(7) << phi
                  << "  " << std::setw(6) << I0
                  << "  " << std::setw(6) << std::abs(I0 - phi) << "\n";
    }
}
