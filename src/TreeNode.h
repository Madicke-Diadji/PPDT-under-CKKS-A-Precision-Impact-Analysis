#pragma once

#include <memory>
#include <vector>

struct TreeNode {
    int node_id = -1;
    int feature_index = -1;
    double threshold = 0.0;
    int class_label = 0;
    int depth = 0;
    double min_gap = 0.5;
    double norm_factor = 1.0;
    int poly_degree = 8;
    int block_slot = -1;
    std::vector<double> leaf_value;
    std::shared_ptr<TreeNode> left;
    std::shared_ptr<TreeNode> right;

    bool isLeaf() const {
        return !left && !right;
    }
};

void assignNodeIds(const std::shared_ptr<TreeNode>& root);
int countNodes(const std::shared_ptr<TreeNode>& root);
int countLeaves(const std::shared_ptr<TreeNode>& root);
int computeDepth(const std::shared_ptr<TreeNode>& root);
