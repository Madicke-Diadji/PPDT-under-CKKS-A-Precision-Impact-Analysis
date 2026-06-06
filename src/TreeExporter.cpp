#include "TreeExporter.h"
#include "SoftStepApprox.h"

#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <set>
#include <unordered_map>
#include <vector>

namespace {
struct CsvNode {
    int node_id = -1;
    bool is_leaf = false;
    int feature = -1;
    double threshold = 0.0;
    int left_id = -1;
    int right_id = -1;
    int class_label = 0;
    double min_gap = 0.5;
};

struct AkaviaJsonNode {
    int index = -1;
    bool is_leaf = false;
    int feature = -1;
    double theta = 0.0;
    int left_id = -1;
    int right_id = -1;
    std::vector<double> values;
};

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) parts.push_back(item);
    return parts;
}

std::shared_ptr<TreeNode> makeLeaf(int node_id, int label) {
    auto node = std::make_shared<TreeNode>();
    node->node_id = node_id;
    node->class_label = label;
    return node;
}

std::shared_ptr<TreeNode> makeInternal(
    int node_id,
    int feature,
    double threshold,
    std::shared_ptr<TreeNode> left,
    std::shared_ptr<TreeNode> right) {
    auto node = std::make_shared<TreeNode>();
    node->node_id = node_id;
    node->feature_index = feature;
    node->threshold = threshold;
    node->left = std::move(left);
    node->right = std::move(right);
    return node;
}

class SimpleJsonParser {
public:
    explicit SimpleJsonParser(std::string text)
        : text_(std::move(text)) {}

    std::shared_ptr<TreeNode> parseModel(int* nb_classes, int* nb_features) {
        skipWhitespace();
        auto root = parseNode();
        skipWhitespace();
        if (pos_ != text_.size()) {
            throw std::runtime_error("JSON arbre: contenu inattendu apres la fin");
        }

        assignNodeIds(root);

        if (nb_features) {
            *nb_features = max_feature_index_ + 1;
        }
        if (nb_classes) {
            *nb_classes = leaf_labels_.empty() ? 0 : static_cast<int>(*leaf_labels_.rbegin()) + 1;
        }
        return root;
    }

private:
    std::string text_;
    size_t pos_ = 0;
    int max_feature_index_ = -1;
    std::set<int> leaf_labels_;

    void skipWhitespace() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
            ++pos_;
        }
    }

    void expect(char c) {
        skipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != c) {
            throw std::runtime_error(std::string("JSON arbre: caractere attendu '") + c + "'");
        }
        ++pos_;
    }

    bool consume(char c) {
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::string parseString() {
        skipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            throw std::runtime_error("JSON arbre: chaine attendue");
        }
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    throw std::runtime_error("JSON arbre: sequence d'echappement incomplete");
                }
                char escaped = text_[pos_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escaped);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    throw std::runtime_error("JSON arbre: sequence d'echappement non supportee");
                }
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("JSON arbre: fin inattendue dans une chaine");
    }

    double parseNumber() {
        skipWhitespace();
        const size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) {
            ++pos_;
        }
        bool seen_digit = false;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
            seen_digit = true;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
                seen_digit = true;
            }
        }
        if (!seen_digit) {
            throw std::runtime_error("JSON arbre: nombre attendu");
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) {
                ++pos_;
            }
            bool exp_digit = false;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
                exp_digit = true;
            }
            if (!exp_digit) {
                throw std::runtime_error("JSON arbre: exposant invalide");
            }
        }
        return std::stod(text_.substr(start, pos_ - start));
    }

    void skipValue() {
        skipWhitespace();
        if (pos_ >= text_.size()) {
            throw std::runtime_error("JSON arbre: valeur attendue");
        }
        if (text_[pos_] == '{') {
            expect('{');
            if (consume('}')) {
                return;
            }
            do {
                parseString();
                expect(':');
                skipValue();
            } while (consume(','));
            expect('}');
            return;
        }
        if (text_[pos_] == '[') {
            expect('[');
            if (consume(']')) {
                return;
            }
            do {
                skipValue();
            } while (consume(','));
            expect(']');
            return;
        }
        if (text_[pos_] == '"') {
            parseString();
            return;
        }
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return;
        }
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return;
        }
        (void)parseNumber();
    }

    std::shared_ptr<TreeNode> parseNodeWrapper(const std::string& expected_key) {
        expect('{');
        std::string key = parseString();
        if (key != expected_key) {
            throw std::runtime_error("JSON arbre: cle inattendue '" + key + "'");
        }
        expect(':');
        std::shared_ptr<TreeNode> node =
            (expected_key == "internal") ? parseInternalBody() : parseLeafBody();
        expect('}');
        return node;
    }

    std::shared_ptr<TreeNode> parseNode() {
        skipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != '{') {
            throw std::runtime_error("JSON arbre: objet noeud attendu");
        }

        const size_t lookahead = pos_;
        expect('{');
        std::string key = parseString();
        pos_ = lookahead;

        if (key == "internal") {
            return parseNodeWrapper("internal");
        }
        if (key == "leaf") {
            return parseNodeWrapper("leaf");
        }
        throw std::runtime_error("JSON arbre: noeud ni internal ni leaf");
    }

    std::shared_ptr<TreeNode> parseInternalBody() {
        auto node = std::make_shared<TreeNode>();
        expect('{');
        bool has_threshold = false;
        bool has_feature = false;
        bool has_left = false;
        bool has_right = false;

        if (!consume('}')) {
            do {
                const std::string key = parseString();
                expect(':');
                if (key == "threshold") {
                    node->threshold = parseNumber();
                    has_threshold = true;
                } else if (key == "feature") {
                    node->feature_index = static_cast<int>(parseNumber());
                    max_feature_index_ = std::max(max_feature_index_, node->feature_index);
                    has_feature = true;
                } else if (key == "left") {
                    node->left = parseNode();
                    has_left = true;
                } else if (key == "right") {
                    node->right = parseNode();
                    has_right = true;
                } else {
                    skipValue();
                }
            } while (consume(','));
            expect('}');
        }

        if (!has_threshold || !has_feature || !has_left || !has_right) {
            throw std::runtime_error("JSON arbre: noeud interne incomplet");
        }
        return node;
    }

    std::shared_ptr<TreeNode> parseLeafBody() {
        auto node = std::make_shared<TreeNode>();
        node->class_label = static_cast<int>(parseNumber());
        leaf_labels_.insert(node->class_label);
        return node;
    }
};

void printTreeRec(const std::shared_ptr<TreeNode>& node,
                  const std::string& prefix,
                  bool is_left) {
    if (!node) {
        return;
    }

    std::cout << prefix;
    if (!prefix.empty()) {
        std::cout << (is_left ? "|-- " : "\\-- ");
    }

    if (node->isLeaf()) {
        std::cout << "Leaf #" << node->node_id
                  << " -> class " << node->class_label << "\n";
        return;
    }

    std::cout << "Node #" << node->node_id
              << " : X[" << node->feature_index << "] <= " << node->threshold
              << " ?\n";

    std::string child_prefix = prefix;
    if (!prefix.empty()) {
        child_prefix += (is_left ? "|   " : "    ");
    }

    printTreeRec(node->left, child_prefix, true);
    printTreeRec(node->right, child_prefix, false);
}
}

std::shared_ptr<TreeNode> TreeExporter::loadFromCSV(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("TreeExporter::loadFromCSV: impossible d'ouvrir " + path);

    std::string line;
    std::getline(in, line); // header

    std::unordered_map<int, CsvNode> rows;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto p = splitCsvLine(line);
        if (p.size() < 8) throw std::runtime_error("CSV arbre invalide: " + line);

        CsvNode row;
        row.node_id = std::stoi(p[0]);
        row.is_leaf = std::stoi(p[1]) != 0;
        row.feature = std::stoi(p[2]);
        row.threshold = std::stod(p[3]);
        row.left_id = std::stoi(p[4]);
        row.right_id = std::stoi(p[5]);
        row.class_label = std::stoi(p[6]);
        row.min_gap = std::stod(p[7]);
        rows[row.node_id] = row;
    }

    std::unordered_map<int, std::shared_ptr<TreeNode>> nodes;
    for (const auto& kv : rows) {
        const auto& row = kv.second;
        auto node = std::make_shared<TreeNode>();
        node->node_id = row.node_id;
        node->feature_index = row.feature;
        node->threshold = row.threshold;
        node->class_label = row.class_label;
        node->min_gap = row.min_gap;
        node->poly_degree = SoftStepApprox::chooseAdaptiveDegree(row.min_gap);
        nodes[row.node_id] = node;
    }

    for (const auto& kv : rows) {
        const auto& row = kv.second;
        if (row.is_leaf) continue;
        auto node = nodes[row.node_id];
        node->left = nodes.at(row.left_id);
        node->right = nodes.at(row.right_id);
    }

    auto root = nodes.at(0);
    assignNodeIds(root);
    return root;
}

std::shared_ptr<TreeNode> TreeExporter::loadFromModelJSON(
    const std::string& path,
    int* nb_classes,
    int* nb_features) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("TreeExporter::loadFromModelJSON: impossible d'ouvrir " + path);
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    SimpleJsonParser parser(buffer.str());
    return parser.parseModel(nb_classes, nb_features);
}

std::shared_ptr<TreeNode> TreeExporter::loadFromAkaviaPlainTree(
    const std::string& path,
    int* nb_classes,
    int* nb_features) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(
            "TreeExporter::loadFromAkaviaPlainTree: impossible d'ouvrir " + path);
    }

    int tree_height = 0;
    int labels_count = 0;
    int features_count_hint = 0;
    in >> tree_height;
    in >> labels_count;
    in >> features_count_hint;

    if (!in || tree_height < 0 || labels_count <= 0) {
        throw std::runtime_error(
            "TreeExporter::loadFromAkaviaPlainTree: header invalide dans " + path);
    }

    const int nodes_count = static_cast<int>(std::pow(2, tree_height + 1)) - 1;
    const int non_leaf_count = nodes_count - static_cast<int>(std::pow(2, tree_height));
    std::vector<std::shared_ptr<TreeNode>> nodes(nodes_count);

    int max_feature_index = -1;
    for (int index = 0; index < non_leaf_count; ++index) {
        int feature_index = -1;
        double theta = 0.0;
        in >> feature_index;
        in >> theta;
        if (!in) {
            throw std::runtime_error(
                "TreeExporter::loadFromAkaviaPlainTree: lecture noeud interne invalide dans " + path);
        }

        auto node = std::make_shared<TreeNode>();
        node->node_id = index;
        node->feature_index = feature_index;
        node->threshold = theta;
        nodes[index] = node;
        max_feature_index = std::max(max_feature_index, feature_index);
    }

    for (int index = non_leaf_count; index < nodes_count; ++index) {
        std::vector<double> values(labels_count, 0.0);
        int best_label = 0;
        double best_value = -std::numeric_limits<double>::infinity();
        for (int label = 0; label < labels_count; ++label) {
            in >> values[label];
            if (!in) {
                throw std::runtime_error(
                    "TreeExporter::loadFromAkaviaPlainTree: lecture feuille invalide dans " + path);
            }
            if (values[label] > best_value) {
                best_value = values[label];
                best_label = label;
            }
        }

        auto node = std::make_shared<TreeNode>();
        node->node_id = index;
        node->class_label = best_label;
        node->leaf_value = values;
        nodes[index] = node;
    }

    for (int index = 0; index < non_leaf_count; ++index) {
        nodes[index]->left = nodes.at((2 * index) + 1);
        nodes[index]->right = nodes.at((2 * index) + 2);
    }

    if (nb_classes) {
        *nb_classes = labels_count;
    }
    if (nb_features) {
        *nb_features = std::max(features_count_hint, max_feature_index + 1);
    }

    auto root = nodes.at(0);
    assignNodeIds(root);
    return root;
}

std::shared_ptr<TreeNode> TreeExporter::loadFromAkaviaPlainTreeJSON(
    const std::string& path,
    int* nb_classes,
    int* nb_features) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(
            "TreeExporter::loadFromAkaviaPlainTreeJSON: impossible d'ouvrir " + path);
    }

    static const std::regex re_tree_height(R"("tree_height"\s*:\s*(-?\d+))");
    static const std::regex re_labels_count(R"("labels_count"\s*:\s*(-?\d+))");
    static const std::regex re_features_count(R"("features_count"\s*:\s*(-?\d+))");
    static const std::regex re_index(R"("index"\s*:\s*(-?\d+))");
    static const std::regex re_type(R"re("type"\s*:\s*"([^"]+)")re");
    static const std::regex re_feature(R"("feature"\s*:\s*(-?\d+))");
    static const std::regex re_theta(R"("theta"\s*:\s*([-+0-9.eE]+))");
    static const std::regex re_left(R"("left"\s*:\s*(-?\d+))");
    static const std::regex re_right(R"("right"\s*:\s*(-?\d+))");
    static const std::regex re_number(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)");

    int tree_height = -1;
    int labels_count = -1;
    int features_count_hint = -1;
    std::vector<AkaviaJsonNode> parsed_nodes;
    AkaviaJsonNode current;
    bool in_values = false;

    auto finalize_current = [&]() {
        if (current.index >= 0) {
            parsed_nodes.push_back(current);
            current = AkaviaJsonNode{};
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty()) {
            continue;
        }

        std::smatch match;
        if (std::regex_search(t, match, re_tree_height)) {
            tree_height = std::stoi(match[1].str());
            continue;
        }
        if (std::regex_search(t, match, re_labels_count)) {
            labels_count = std::stoi(match[1].str());
            continue;
        }
        if (std::regex_search(t, match, re_features_count)) {
            features_count_hint = std::stoi(match[1].str());
            continue;
        }

        if (std::regex_search(t, match, re_index)) {
            finalize_current();
            current.index = std::stoi(match[1].str());
            continue;
        }
        if (std::regex_search(t, match, re_type)) {
            current.is_leaf = (match[1].str() == "leaf");
            continue;
        }
        if (std::regex_search(t, match, re_feature)) {
            current.feature = std::stoi(match[1].str());
            continue;
        }
        if (std::regex_search(t, match, re_theta)) {
            current.theta = std::stod(match[1].str());
            continue;
        }
        if (std::regex_search(t, match, re_left)) {
            current.left_id = std::stoi(match[1].str());
            continue;
        }
        if (std::regex_search(t, match, re_right)) {
            current.right_id = std::stoi(match[1].str());
            continue;
        }

        if (t.find("\"values\"") != std::string::npos) {
            in_values = true;
        }

        if (in_values) {
            for (std::sregex_iterator it(t.begin(), t.end(), re_number), end; it != end; ++it) {
                current.values.push_back(std::stod((*it).str()));
            }
            if (t.find(']') != std::string::npos) {
                in_values = false;
            }
            continue;
        }

        if ((t == "}," || t == "}") && current.index >= 0) {
            finalize_current();
        }
    }
    finalize_current();

    if (tree_height < 0 || labels_count <= 0 || parsed_nodes.empty()) {
        throw std::runtime_error(
            "TreeExporter::loadFromAkaviaPlainTreeJSON: contenu invalide dans " + path);
    }

    const int nodes_count = static_cast<int>(std::pow(2, tree_height + 1)) - 1;
    std::vector<std::shared_ptr<TreeNode>> nodes(nodes_count);
    int max_feature_index = -1;

    for (const auto& row : parsed_nodes) {
        if (row.index < 0 || row.index >= nodes_count) {
            throw std::runtime_error(
                "TreeExporter::loadFromAkaviaPlainTreeJSON: index de noeud invalide dans " + path);
        }

        auto node = std::make_shared<TreeNode>();
        node->node_id = row.index;

        if (row.is_leaf) {
            if (static_cast<int>(row.values.size()) < labels_count) {
                throw std::runtime_error(
                    "TreeExporter::loadFromAkaviaPlainTreeJSON: feuille incomplete dans " + path);
            }
            node->leaf_value = row.values;
            int best_label = 0;
            double best_value = -std::numeric_limits<double>::infinity();
            for (int label = 0; label < labels_count; ++label) {
                if (row.values[label] > best_value) {
                    best_value = row.values[label];
                    best_label = label;
                }
            }
            node->class_label = best_label;
        } else {
            node->feature_index = row.feature;
            node->threshold = row.theta;
            max_feature_index = std::max(max_feature_index, row.feature);
        }

        nodes[row.index] = node;
    }

    for (const auto& row : parsed_nodes) {
        if (!row.is_leaf) {
            nodes[row.index]->left = nodes.at(row.left_id);
            nodes[row.index]->right = nodes.at(row.right_id);
        }
    }

    if (nb_classes) {
        *nb_classes = labels_count;
    }
    if (nb_features) {
        *nb_features = std::max(features_count_hint, max_feature_index + 1);
    }

    auto root = nodes.at(0);
    assignNodeIds(root);
    return root;
}

std::shared_ptr<TreeNode> TreeExporter::makeSimpleTestTree(int nb_classes) {
    (void)nb_classes;
    auto l0 = makeLeaf(3, 0);
    auto l1 = makeLeaf(4, 1);
    auto l2 = makeLeaf(5, 1);
    auto l3 = makeLeaf(6, 0);
    auto n1 = makeInternal(1, 1, 0.3, l0, l1);
    auto n2 = makeInternal(2, 1, 0.7, l2, l3);
    auto root = makeInternal(0, 0, 0.5, n1, n2);
    assignNodeIds(root);
    return root;
}

void TreeExporter::printTree(const std::shared_ptr<TreeNode>& root) {
    if (!root) {
        std::cout << "(arbre vide)\n";
        return;
    }

    std::cout << "\nStructure de l'arbre :\n";
    printTreeRec(root, "", true);
}
