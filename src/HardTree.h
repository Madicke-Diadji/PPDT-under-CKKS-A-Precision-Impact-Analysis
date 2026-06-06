#pragma once

#include "TreeNode.h"

#include <memory>
#include <vector>

class HardTree {
public:
    HardTree(std::shared_ptr<TreeNode> root, int nb_classes, int nb_features);

    int predict(const std::vector<double>& x) const;
    const TreeNode* predictLeaf(const std::vector<double>& x) const;
    std::vector<int> predictBatch(const std::vector<std::vector<double>>& X) const;
    void collectGapStats(const std::vector<std::vector<double>>& X);
    void printInfo() const;

    std::shared_ptr<TreeNode> getRoot() const { return root_; }
    int getDepth() const { return depth_; }
    int getNbClasses() const { return nb_classes_; }
    int getNbFeatures() const { return nb_features_; }

private:
    std::shared_ptr<TreeNode> root_;
    int nb_classes_;
    int nb_features_;
    int depth_;

    int predictNode(const TreeNode* node, const std::vector<double>& x) const;
    void initializeLeaves(const std::shared_ptr<TreeNode>& node);
    void resetGaps(const std::shared_ptr<TreeNode>& node);
    void collectOne(const std::shared_ptr<TreeNode>& node, const std::vector<double>& x);
    void finalizeGaps(const std::shared_ptr<TreeNode>& node);
};
