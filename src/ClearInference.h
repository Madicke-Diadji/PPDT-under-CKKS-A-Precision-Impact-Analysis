#pragma once

#include "DataLayout.h"
#include "HardTree.h"
#include "SoftStepApprox.h"

#include <memory>
#include <vector>

enum class InferenceMode {
    HARD,
    SOFT_GLOBAL,
    SOFT_ADAPTIVE
};

class ClearInference {
public:
    struct Results {
        double accuracy_hard = 0.0;
        double accuracy_soft_global = 0.0;
        double accuracy_soft_adaptive = 0.0;
        int correct_hard = 0;
        int correct_soft_global = 0;
        int correct_soft_adaptive = 0;
        int nb_samples = 0;
        int global_degree = 8;
        std::vector<int> adaptive_degrees;
    };

    ClearInference(std::shared_ptr<HardTree> tree,
                   std::shared_ptr<DataLayout> layout,
                   int nb_classes);

    void configureSoftGlobal(int degree = 8, double window = 0.05);
    void configureSoftAdaptive(double window = 0.05);
    void configureSoftAdaptive(double window,
                               const std::vector<std::vector<double>>& X_calib);

    int predict(const std::vector<double>& x, InferenceMode mode) const;
    Results evaluateAll(const std::vector<std::vector<double>>& X,
                        const std::vector<int>& y_true) const;
    void printResults(const Results& r) const;

    const std::vector<int>& getAdaptiveDegrees() const { return adaptive_degrees_; }

private:
    std::shared_ptr<HardTree> tree_;
    std::shared_ptr<DataLayout> layout_;
    int nb_classes_;
    int global_degree_ = 8;
    double global_window_ = 0.05;
    double adaptive_window_ = 0.05;
    std::vector<int> adaptive_degrees_;
    std::vector<double> adaptive_windows_;
    std::vector<double> adaptive_norm_factors_;

    void refreshAdaptiveDegrees();
    void refreshAdaptiveDegrees(const std::vector<std::vector<double>>* X_calib);
};
