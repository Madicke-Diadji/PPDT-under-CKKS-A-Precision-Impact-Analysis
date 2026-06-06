#include "ClearInference.h"

#include "Metrics.h"

#include <iomanip>
#include <iostream>
#include <queue>

ClearInference::ClearInference(std::shared_ptr<HardTree> tree,
                               std::shared_ptr<DataLayout> layout,
                               int nb_classes)
    : tree_(std::move(tree)),
      layout_(std::move(layout)),
      nb_classes_(nb_classes) {
    refreshAdaptiveDegrees(nullptr);
}

void ClearInference::configureSoftGlobal(int degree, double window) {
    global_degree_ = degree;
    global_window_ = window;
}

void ClearInference::configureSoftAdaptive(double window) {
    adaptive_window_ = window;
    refreshAdaptiveDegrees(nullptr);
}

void ClearInference::configureSoftAdaptive(
    double window,
    const std::vector<std::vector<double>>& X_calib) {
    adaptive_window_ = window;
    refreshAdaptiveDegrees(&X_calib);
}

int ClearInference::predict(const std::vector<double>& x, InferenceMode mode) const {
    switch (mode) {
    case InferenceMode::HARD:
        return tree_->predict(x);
    case InferenceMode::SOFT_GLOBAL: {
        std::vector<int> degrees(layout_->getNodeBlocks().size(), global_degree_);
        return layout_->predict(x, true, degrees, {}, {}, global_window_);
    }
    case InferenceMode::SOFT_ADAPTIVE:
        return layout_->predict(
            x,
            true,
            adaptive_degrees_,
            adaptive_windows_,
            adaptive_norm_factors_,
            adaptive_window_);
    }
    return tree_->predict(x);
}

ClearInference::Results ClearInference::evaluateAll(
    const std::vector<std::vector<double>>& X,
    const std::vector<int>& y_true) const {
    std::vector<int> pred_hard;
    std::vector<int> pred_soft_global;
    std::vector<int> pred_soft_adaptive;
    pred_hard.reserve(X.size());
    pred_soft_global.reserve(X.size());
    pred_soft_adaptive.reserve(X.size());

    for (const auto& x : X) {
        pred_hard.push_back(predict(x, InferenceMode::HARD));
        pred_soft_global.push_back(predict(x, InferenceMode::SOFT_GLOBAL));
        pred_soft_adaptive.push_back(predict(x, InferenceMode::SOFT_ADAPTIVE));
    }

    Results r;
    r.nb_samples = static_cast<int>(X.size());
    for (size_t i = 0; i < y_true.size(); ++i) {
        if (pred_hard[i] == y_true[i]) {
            ++r.correct_hard;
        }
        if (pred_soft_global[i] == y_true[i]) {
            ++r.correct_soft_global;
        }
        if (pred_soft_adaptive[i] == y_true[i]) {
            ++r.correct_soft_adaptive;
        }
    }
    r.accuracy_hard = computeAccuracy(y_true, pred_hard);
    r.accuracy_soft_global = computeAccuracy(y_true, pred_soft_global);
    r.accuracy_soft_adaptive = computeAccuracy(y_true, pred_soft_adaptive);
    r.global_degree = global_degree_;
    r.adaptive_degrees = adaptive_degrees_;
    return r;
}

void ClearInference::printResults(const Results& r) const {
    std::cout << "\n=== Results - clear inference ===\n"
              << "Samples                  : " << r.nb_samples << "\n"
              << "Hard (clear)             : "
              << r.correct_hard << "/" << r.nb_samples << " - "
              << std::fixed << std::setprecision(2)
              << r.accuracy_hard << "%\n"
              << "Soft global (clear)      : "
              << r.correct_soft_global << "/" << r.nb_samples << " - "
              << r.accuracy_soft_global << "%\n"
              << "Soft adaptive (clear)    : "
              << r.correct_soft_adaptive << "/" << r.nb_samples << " - "
              << r.accuracy_soft_adaptive << "%\n";
}

void ClearInference::refreshAdaptiveDegrees(
    const std::vector<std::vector<double>>* X_calib) {
    adaptive_degrees_.clear();
    adaptive_windows_.clear();
    adaptive_norm_factors_.clear();
    std::queue<std::shared_ptr<TreeNode>> q;
    q.push(tree_->getRoot());
    while (!q.empty()) {
        auto node = q.front();
        q.pop();
        if (!node || node->isLeaf()) continue;
        double norm_factor = 1.0;
        if (X_calib && !X_calib->empty()) {
            norm_factor = 0.0;
            for (const auto& x : *X_calib) {
                if (node->feature_index >= 0
                    && node->feature_index < static_cast<int>(x.size())) {
                    norm_factor = std::max(
                        norm_factor,
                        std::abs(x[node->feature_index] - node->threshold));
                }
            }
            norm_factor = std::max(norm_factor, 1e-6);
        }
        const double normalized_gap = node->min_gap / norm_factor;
        int degree = SoftStepApprox::chooseAdaptiveDegree(normalized_gap, 0.01, 64);
        adaptive_degrees_.push_back(degree);
        adaptive_windows_.push_back(adaptive_window_);
        adaptive_norm_factors_.push_back(norm_factor);
        q.push(node->left);
        q.push(node->right);
    }
}
