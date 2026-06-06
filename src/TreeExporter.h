#pragma once

#include "TreeNode.h"

#include <memory>
#include <string>

class TreeExporter {
public:
    static std::shared_ptr<TreeNode> loadFromCSV(const std::string& path);
    static std::shared_ptr<TreeNode> loadFromModelJSON(
        const std::string& path,
        int* nb_classes = nullptr,
        int* nb_features = nullptr);
    static std::shared_ptr<TreeNode> loadFromAkaviaPlainTree(
        const std::string& path,
        int* nb_classes = nullptr,
        int* nb_features = nullptr);
    static std::shared_ptr<TreeNode> loadFromAkaviaPlainTreeJSON(
        const std::string& path,
        int* nb_classes = nullptr,
        int* nb_features = nullptr);
    static std::shared_ptr<TreeNode> makeSimpleTestTree(int nb_classes);
    static void printTree(const std::shared_ptr<TreeNode>& root);
};
