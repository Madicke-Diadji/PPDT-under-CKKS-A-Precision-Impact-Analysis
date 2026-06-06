#include "Metrics.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────────────
// Metrics.cpp — Fonctions de mesure de performance
//
// Compléments a Metrics.h (qui contient les définitions inline de base).
// Ce fichier fournit les fonctions plus elaborees :
//   - Rapport de classification complet (precision, recall, F1 par classe)
//   - Courbe de dégradation : précision en fonction du degré polynomial
//   - Comparaison clair / chiffré
// ─────────────────────────────────────────────────────────────────────────────

// ─── Rapport de classification complet ───────────────────────────────────────
void printClassificationReport(const std::vector<int>& y_true,
                                 const std::vector<int>& y_pred,
                                 int nb_classes) {
    ConfusionMatrix cm(nb_classes);
    for (size_t i = 0; i < y_true.size(); ++i)
        cm.update(y_true[i], y_pred[i]);
    cm.print();

    // Precision, recall, F1 par classe
    std::cout << "\n  Class   Precision  Recall   F1\n";
    std::cout << "  ------  ---------  ------  ------\n";
    for (int c = 0; c < nb_classes; ++c) {
        int tp = cm.matrix[c][c];
        int fp = 0, fn = 0;
        for (int j = 0; j < nb_classes; ++j) {
            if (j != c) fp += cm.matrix[j][c];
            if (j != c) fn += cm.matrix[c][j];
        }
        double prec = (tp + fp > 0) ? 100.0 * tp / (tp + fp) : 0.0;
        double rec  = (tp + fn > 0) ? 100.0 * tp / (tp + fn) : 0.0;
        double f1   = (prec + rec > 0) ? 2.0 * prec * rec / (prec + rec) : 0.0;
        std::cout << "  " << std::setw(6) << c
                  << "  " << std::setw(7) << std::fixed << std::setprecision(1) << prec << "%"
                  << "  " << std::setw(5) << rec  << "%"
                  << "  " << std::setw(5) << f1   << "%\n";
    }
}

// ─── Courbe precision vs degré polynomial ────────────────────────────────────
// Pour chaque degré dans degrees[], calcule la précision d'un predictor
// basé sur le polynôme de ce degré.
// predict_fn : fonction (x, degree) -> label
void printAccuracyVsDegree(
    const std::vector<std::vector<double>>& X_test,
    const std::vector<int>& y_true,
    const std::vector<int>& degrees,
    std::function<int(const std::vector<double>&, int)> predict_fn) {

    std::cout << "\n  Degree  Accuracy    Mult.Depth\n";
    std::cout << "  -----   ---------   ----------\n";

    for (int deg : degrees) {
        int correct = 0;
        for (size_t i = 0; i < X_test.size(); ++i)
            if (predict_fn(X_test[i], deg) == y_true[i]) correct++;
        double acc = 100.0 * correct / (int)X_test.size();

        // Profondeur multiplicative theorique (BSGS)
        int depth = (int)std::ceil(std::log2((double)deg)) + 1;

        std::cout << "  " << std::setw(5) << deg
                  << "  " << std::setw(7) << std::fixed << std::setprecision(2) << acc << "%"
                  << "  " << std::setw(10) << depth << "\n";
    }
}

// ─── Comparaison clair / chiffré ─────────────────────────────────────────────
void printClearVsHEComparison(
    double acc_hard,
    double acc_soft_global,
    double acc_soft_adaptive,
    double acc_he_global,
    double acc_he_adaptive,
    double time_he_global_ms,
    double time_he_adaptive_ms,
    int    he_mult_depth) {

    std::cout << "\n";
    std::cout << "  ┌──────────────────────────────────────────────────────────┐\n";
    std::cout << "  │           FINAL COMPARISON: CLEAR vs ENCRYPTED           │\n";
    std::cout << "  ├──────────────────────┬───────────┬───────────┬───────────┤\n";
    std::cout << "  │ Mode                 │ Accuracy  │ Time      │ HE Depth  │\n";
    std::cout << "  ├──────────────────────┼───────────┼───────────┼───────────┤\n";

    auto row = [](const std::string& name, double acc,
                  const std::string& time, const std::string& depth) {
        std::cout << "  │ " << std::left  << std::setw(20) << name
                  << " │ " << std::right << std::setw(7) << std::fixed
                  << std::setprecision(2) << acc << "%"
                  << " │ " << std::setw(9) << time
                  << " │ " << std::setw(9) << depth << " │\n";
    };

    row("Hard (baseline)",     acc_hard,           "N/A",  "0 (clear)");
    row("Soft global clear",   acc_soft_global,    "N/A",  "0 (clear)");
    row("Soft adaptive clear", acc_soft_adaptive,  "N/A",  "0 (clear)");
    row("HE soft global",     acc_he_global,
        std::to_string((int)time_he_global_ms) + " ms",
        std::to_string(he_mult_depth));
    row("HE soft adaptive",   acc_he_adaptive,
        std::to_string((int)time_he_adaptive_ms) + " ms",
        std::to_string(he_mult_depth));

    std::cout << "  └──────────────────────┴───────────┴───────────┴───────────┘\n";

    // Perte de precision clair -> HE
    double loss_global   = acc_soft_global   - acc_he_global;
    double loss_adaptive = acc_soft_adaptive - acc_he_adaptive;
    std::cout << "\n  Loss due to encryption:\n"
              << "    Global   : " << std::setprecision(2) << loss_global   << " pts\n"
              << "    Adaptive : " << loss_adaptive << " pts\n";
}
