#pragma once
#include <vector>
#include <string>
#include <functional>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Metrics.h — Outils de mesure de performance
// ─────────────────────────────────────────────────────────────────────────────

// ─── Matrice de confusion ────────────────────────────────────────────────────
struct ConfusionMatrix {
    int nb_classes;
    std::vector<std::vector<int>> matrix;

    explicit ConfusionMatrix(int n)
        : nb_classes(n), matrix(n, std::vector<int>(n, 0)) {}

    void update(int true_label, int pred_label) {
        if (true_label >= 0 && true_label < nb_classes &&
            pred_label  >= 0 && pred_label  < nb_classes)
            matrix[true_label][pred_label]++;
    }

    double accuracy() const {
        int total = 0, correct = 0;
        for (int i = 0; i < nb_classes; ++i) {
            for (int j = 0; j < nb_classes; ++j) total += matrix[i][j];
            correct += matrix[i][i];
        }
        return total > 0 ? 100.0 * correct / total : 0.0;
    }

    void print() const {
        std::cout << "  Matrice de confusion :\n      ";
        for (int j = 0; j < nb_classes; ++j)
            std::cout << std::setw(5) << "C" + std::to_string(j);
        std::cout << "\n";
        for (int i = 0; i < nb_classes; ++i) {
            std::cout << "  C" << i << " :";
            for (int j = 0; j < nb_classes; ++j)
                std::cout << std::setw(5) << matrix[i][j];
            std::cout << "\n";
        }
        std::cout << "  Precision globale : " << std::fixed << std::setprecision(2)
                  << accuracy() << "%\n";
    }
};

// ─── Fonctions inline ────────────────────────────────────────────────────────
inline double computeAccuracy(const std::vector<int>& y_true,
                               const std::vector<int>& y_pred) {
    if (y_true.empty()) return 0.0;
    int correct = 0;
    for (size_t i = 0; i < y_true.size(); ++i)
        if (y_true[i] == y_pred[i]) correct++;
    return 100.0 * correct / (int)y_true.size();
}

inline double computeAccuracyBool(const std::vector<bool>& correct_mask) {
    if (correct_mask.empty()) return 0.0;
    int c = 0;
    for (bool b : correct_mask) if (b) c++;
    return 100.0 * c / (int)correct_mask.size();
}

// ─── Declarations des fonctions du .cpp ──────────────────────────────────────

// Rapport complet : matrice de confusion + precision/recall/F1 par classe
void printClassificationReport(const std::vector<int>& y_true,
                                 const std::vector<int>& y_pred,
                                 int nb_classes);

// Courbe precision vs degré polynomial
// predict_fn(x, degree) -> label prédit
void printAccuracyVsDegree(
    const std::vector<std::vector<double>>& X_test,
    const std::vector<int>& y_true,
    const std::vector<int>& degrees,
    std::function<int(const std::vector<double>&, int)> predict_fn);

// Tableau comparatif clair vs HE
void printClearVsHEComparison(
    double acc_hard,
    double acc_soft_global,
    double acc_soft_adaptive,
    double acc_he_global,
    double acc_he_adaptive,
    double time_he_global_ms,
    double time_he_adaptive_ms,
    int    he_mult_depth);
