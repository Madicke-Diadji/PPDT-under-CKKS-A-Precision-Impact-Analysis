#include "TreeNode.h"

#include <algorithm>
#include <queue>

void assignNodeIds(const std::shared_ptr<TreeNode>& root) {
    if (!root) return;

    std::queue<std::shared_ptr<TreeNode>> q;
    q.push(root);
    int next_id = 0;
    while (!q.empty()) {
        auto node = q.front();
        q.pop();
        node->node_id = next_id++;
        if (node->left) {
            node->left->depth = node->depth + 1;
            q.push(node->left);
        }
        if (node->right) {
            node->right->depth = node->depth + 1;
            q.push(node->right);
        }
    }
}

int countNodes(const std::shared_ptr<TreeNode>& root) {
    if (!root) return 0;
    return 1 + countNodes(root->left) + countNodes(root->right);
}

int countLeaves(const std::shared_ptr<TreeNode>& root) {
    if (!root) return 0;
    if (root->isLeaf()) return 1;
    return countLeaves(root->left) + countLeaves(root->right);
}

int computeDepth(const std::shared_ptr<TreeNode>& root) {
    if (!root || root->isLeaf()) return 0;
    return 1 + std::max(computeDepth(root->left), computeDepth(root->right));
}
