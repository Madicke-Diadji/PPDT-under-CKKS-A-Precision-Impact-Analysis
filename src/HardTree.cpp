#include "HardTree.h"
#include "SoftStepApprox.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

HardTree::HardTree(std::shared_ptr<TreeNode> root, int nb_classes, int nb_features)
    : root_(std::move(root)),
      nb_classes_(nb_classes),
      nb_features_(nb_features),
      depth_(computeDepth(root_)) {
    if (!root_) throw std::runtime_error("HardTree: arbre vide");
    assignNodeIds(root_);
    initializeLeaves(root_);
}

int HardTree::predict(const std::vector<double>& x) const {
    if (static_cast<int>(x.size()) < nb_features_) {
        throw std::runtime_error("HardTree::predict: input dimension is too small");
    }
    return predictNode(root_.get(), x);
}

const TreeNode* HardTree::predictLeaf(const std::vector<double>& x) const {
    if (static_cast<int>(x.size()) < nb_features_) {
        throw std::runtime_error("HardTree::predictLeaf: input dimension is too small");
    }

    const TreeNode* node = root_.get();
    while (node && !node->isLeaf()) {
        const int feat = node->feature_index;
        if (feat < 0 || feat >= static_cast<int>(x.size())) {
            throw std::runtime_error("HardTree::predictLeaf: index feature invalide");
        }
        node = (x[feat] <= node->threshold) ? node->left.get() : node->right.get();
    }
    return node;
}

std::vector<int> HardTree::predictBatch(const std::vector<std::vector<double>>& X) const {
    std::vector<int> preds;
    preds.reserve(X.size());
    for (const auto& x : X) preds.push_back(predict(x));
    return preds;
}

void HardTree::collectGapStats(const std::vector<std::vector<double>>& X) {
    resetGaps(root_);
    for (const auto& x : X) collectOne(root_, x);
    finalizeGaps(root_);
}

void HardTree::printInfo() const {
    std::cout << "[HardTree] depth=" << depth_
              << " nodes=" << countNodes(root_)
              << " leaves=" << countLeaves(root_)
              << " features=" << nb_features_
              << " classes=" << nb_classes_ << "\n";
}

int HardTree::predictNode(const TreeNode* node, const std::vector<double>& x) const {
    if (!node) throw std::runtime_error("HardTree::predictNode: noeud nul");
    if (node->isLeaf()) return node->class_label;

    const int feat = node->feature_index;
    if (feat < 0 || feat >= static_cast<int>(x.size())) {
        throw std::runtime_error("HardTree::predictNode: index feature invalide");
    }

    return (x[feat] <= node->threshold)
        ? predictNode(node->left.get(), x)
        : predictNode(node->right.get(), x);
}

void HardTree::initializeLeaves(const std::shared_ptr<TreeNode>& node) {
    if (!node) return;
    if (node->isLeaf()) {
        node->leaf_value.assign(nb_classes_, 0.0);
        if (node->class_label >= 0 && node->class_label < nb_classes_) {
            node->leaf_value[node->class_label] = 1.0;
        }
        return;
    }
    initializeLeaves(node->left);
    initializeLeaves(node->right);
}

void HardTree::resetGaps(const std::shared_ptr<TreeNode>& node) {
    if (!node) return;
    if (!node->isLeaf()) {
        node->min_gap = std::numeric_limits<double>::infinity();
        resetGaps(node->left);
        resetGaps(node->right);
    }
}

void HardTree::collectOne(const std::shared_ptr<TreeNode>& node, const std::vector<double>& x) {
    if (!node || node->isLeaf()) return;
    const int feat = node->feature_index;
    if (feat < 0 || feat >= static_cast<int>(x.size())) return;

    node->min_gap = std::min(node->min_gap, std::abs(x[feat] - node->threshold));
    collectOne((x[feat] <= node->threshold) ? node->left : node->right, x);

}

void HardTree::finalizeGaps(const std::shared_ptr<TreeNode>& node) {
    if (!node || node->isLeaf()) return;
    if (!std::isfinite(node->min_gap)) node->min_gap = 0.5;
    node->poly_degree = SoftStepApprox::chooseAdaptiveDegree(node->min_gap);
    finalizeGaps(node->left);
    finalizeGaps(node->right);
}
