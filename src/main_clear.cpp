// ─────────────────────────────────────────────────────────────────────────────
// poc_hbdt_sumpath — Inférence en CLAIR
//
// Pipeline :
//   1. Charger / créer un arbre "hard" (sklearn export ou arbre test)
//   2. Collecter les statistiques de gap (pour soft adaptatif)
//   3. Construire le DataLayout HBDT-SumPath
//   4. Configurer soft_global (degré uniforme + norme des logits)
//   5. Configurer soft_adaptatif (degré par nœud)
//   6. Évaluer les trois modes (hard / soft_global / soft_adaptatif) sur le dataset
//   7. Afficher les résultats comparatifs
// ─────────────────────────────────────────────────────────────────────────────

#include "HardTree.h"
#include "DataLayout.h"
#include "TreeExporter.h"
#include "Metrics.h"
#include "ClearInference.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

class NullBuffer : public std::streambuf {
public:
    int overflow(int c) override { return c; }
};

class ScopedCoutSilencer {
public:
    explicit ScopedCoutSilencer(bool enabled)
        : enabled_(enabled), previous_(nullptr) {
        if (enabled_) {
            previous_ = std::cout.rdbuf(&buffer_);
        }
    }

    ~ScopedCoutSilencer() {
        if (enabled_) {
            std::cout.rdbuf(previous_);
        }
    }

private:
    bool enabled_;
    NullBuffer buffer_;
    std::streambuf* previous_;
};

std::string resolveDatasetName(const std::string& tree_path);

int chooseLayoutResolution(int nb_features) {
    return (nb_features <= 4) ? 4 : 2;
}

bool containsInsensitive(std::string value, const std::string& needle) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string lowered_needle = needle;
    std::transform(lowered_needle.begin(), lowered_needle.end(), lowered_needle.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find(lowered_needle) != std::string::npos;
}

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
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

bool loadTestData(const std::string& path,
                  int nb_features,
                  std::vector<std::vector<double>>& X,
                  std::vector<int>& y) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    if (!std::getline(in, line)) return false;
    auto header_parts = splitCsvLine(line);
    const bool label_first =
        !header_parts.empty() && header_parts[0] == "label";
    std::vector<std::vector<double>> X_loaded;
    std::vector<int> y_loaded;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto parts = splitCsvLine(line);
        if (static_cast<int>(parts.size()) < nb_features + 1) return false;

        std::vector<double> row(nb_features);
        if (label_first) {
            for (int j = 0; j < nb_features; ++j) row[j] = std::stod(parts[j + 1]);
            y_loaded.push_back(std::stoi(parts[0]));
        } else {
            for (int j = 0; j < nb_features; ++j) row[j] = std::stod(parts[j]);
            y_loaded.push_back(std::stoi(parts[nb_features]));
        }
        X_loaded.push_back(row);
    }

    if (X_loaded.empty()) return false;
    X = std::move(X_loaded);
    y = std::move(y_loaded);
    return true;
}

bool loadFeatureMatrix(const std::string& path,
                       int nb_features,
                       std::vector<std::vector<double>>& X) {
    std::ifstream in(path);
    if (!in) return false;

    std::vector<std::vector<double>> X_loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto parts = splitCsvLine(line);
        if (static_cast<int>(parts.size()) < nb_features) return false;

        std::vector<double> row(nb_features);
        for (int j = 0; j < nb_features; ++j) {
            row[j] = std::stod(parts[j]);
        }
        X_loaded.push_back(std::move(row));
    }

    if (X_loaded.empty()) return false;
    X = std::move(X_loaded);
    return true;
}

bool loadLabels(const std::string& path, std::vector<int>& y) {
    std::ifstream in(path);
    if (!in) return false;

    std::vector<int> y_loaded;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        y_loaded.push_back(std::stoi(line));
    }

    if (y_loaded.empty()) return false;
    y = std::move(y_loaded);
    return true;
}

bool loadSplitTestData(const std::string& x_path,
                       const std::string& y_path,
                       int nb_features,
                       std::vector<std::vector<double>>& X,
                       std::vector<int>& y) {
    std::vector<std::vector<double>> X_loaded;
    std::vector<int> y_loaded;
    if (!loadFeatureMatrix(x_path, nb_features, X_loaded)) return false;
    if (!loadLabels(y_path, y_loaded)) return false;
    if (X_loaded.size() != y_loaded.size()) return false;
    X = std::move(X_loaded);
    y = std::move(y_loaded);
    return true;
}

std::string directoryOf(const std::string& path) {
    const fs::path p(path);
    const fs::path parent = p.parent_path();
    return parent.empty() ? "." : parent.string();
}

[[maybe_unused]] std::string siblingPath(const std::string& path, const std::string& filename) {
    return (fs::path(directoryOf(path)) / filename).string();
}

std::string defaultLabelPath(const std::string& tree_path) {
    const fs::path parent(directoryOf(tree_path));
    const fs::path y_path = parent / "y_test.csv";
    return y_path.string();
}

std::string defaultTestDataPath(const std::string& tree_path) {
    const fs::path parent(directoryOf(tree_path));
    const fs::path split_x = parent / "x_test.csv";
    if (fs::exists(split_x)) {
        return split_x.string();
    }

    std::string path = tree_path;
    const std::string plain_prefix = "plain_tree_";
    const auto plain_pos = path.rfind(plain_prefix);
    if (plain_pos != std::string::npos) {
        const auto dataset_start = plain_pos + plain_prefix.size();
        const auto dot_pos = path.find('.', dataset_start);
        if (dot_pos != std::string::npos) {
            const std::string dataset = path.substr(dataset_start, dot_pos - dataset_start);
            return "data/" + dataset + "_test.csv";
        }
    }
    const std::string needle = "tree.csv";
    const auto pos = path.rfind(needle);
    if (pos != std::string::npos) {
        path.replace(pos, needle.size(), "test_data.csv");
        return path;
    }
    return "data/test_data.csv";
}

std::string sanitizeFileComponent(std::string value) {
    for (char& ch : value) {
        const bool safe =
            (ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '-' || ch == '_';
        if (!safe) {
            ch = '_';
        }
    }
    return value.empty() ? "default" : value;
}

std::string getEnvValue(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buffer, &len, name) == 0 && buffer != nullptr) {
        std::string value(buffer);
        free(buffer);
        return value;
    }
    return "";
#else
    const char* value = std::getenv(name);
    return (value != nullptr) ? std::string(value) : "";
#endif
}

std::string inferDatasetName(const std::string& tree_path) {
    const fs::path path(tree_path);
    const std::string stem = path.stem().string();

    if (stem.rfind("plain_tree_", 0) == 0) {
        return sanitizeFileComponent(stem.substr(std::string("plain_tree_").size()));
    }
    if (path.filename() == "model.json") {
        return sanitizeFileComponent(path.parent_path().filename().string());
    }
    if (path.filename() == "tree.csv" || path.filename() == "tree.json") {
        return "tree";
    }
    return sanitizeFileComponent(stem);
}

std::string resolveResultsDir() {
    const std::string env_value = getEnvValue("POC_RESULTS_DIR");
    if (!env_value.empty()) {
        return env_value;
    }
    return "results";
}

std::string resolveClearResultsFile(const std::string& tree_path) {
    const std::string env_value = getEnvValue("POC_CLEAR_RESULTS_FILE");
    if (!env_value.empty()) {
        return env_value;
    }
    const fs::path results_dir(resolveResultsDir());
    return (results_dir / ("clear_predictions_" + resolveDatasetName(tree_path) + ".csv")).string();
}

std::string resolveDatasetName(const std::string& tree_path) {
    const std::string env_value = getEnvValue("POC_DATASET_NAME");
    if (!env_value.empty()) {
        return sanitizeFileComponent(env_value);
    }
    return inferDatasetName(tree_path);
}

double chooseSoftWindow(const std::string& tree_path) {
    (void)tree_path;
    return 0.05;
}

void writeClearPredictionsCsv(const std::string& path,
                              const std::vector<int>& y_true,
                              const std::vector<int>& hard_preds,
                              const std::vector<int>& soft_global_preds,
                              const std::vector<int>& soft_adaptive_preds) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Unable to write the clear results file: " + path);
    }

    out << "sample_idx,y_true,pred_hard,pred_soft_global,pred_soft_adaptive\n";
    for (size_t i = 0; i < y_true.size(); ++i) {
        out << i << ","
            << y_true[i] << ","
            << hard_preds[i] << ","
            << soft_global_preds[i] << ","
            << soft_adaptive_preds[i] << "\n";
    }
}

int parseSampleCount(int argc, char* argv[]) {
    if (argc <= 4) {
        return -1;
    }
    return std::stoi(argv[4]);
}

void keepFirstSamples(std::vector<std::vector<double>>& X,
                      std::vector<int>& y,
                      int sample_count) {
    if (sample_count <= 0 || static_cast<size_t>(sample_count) >= X.size()) {
        return;
    }
    X.resize(static_cast<size_t>(sample_count));
    y.resize(static_cast<size_t>(sample_count));
}

[[maybe_unused]] void printHardPath(const TreeNode* root, const std::vector<double>& x) {
    const TreeNode* node = root;
    std::cout << "\nSelected hard path in the tree:\n";
    while (node && !node->isLeaf()) {
        const int feat = node->feature_index;
        const double value = x[feat];
        const bool go_left = value <= node->threshold;
        std::cout << "  Node " << node->node_id
                  << " : X[" << feat << "]=" << value
                  << " <= " << node->threshold
                  << " ? " << (go_left ? "yes" : "no")
                  << " -> " << (go_left ? "left" : "right") << "\n";
        node = go_left ? node->left.get() : node->right.get();
    }

    if (node) {
        std::cout << "  Leaf " << node->node_id
                  << " : predicted class = " << node->class_label << "\n";
    }
}
}

int main(int argc, char* argv[]) {
    if (false) {

    std::cout << "╔══════════════════════════════════════════════════════╗\n"
              << "║  POC HBDT-SumPath — Inférence en CLAIR               ║\n"
              << "╚══════════════════════════════════════════════════════╝\n\n";

    // ─── 1. Chargement de l'arbre ─────────────────────────────────────────────
    }
    std::shared_ptr<TreeNode> root;
    int nb_features = 2;
    int nb_classes  = 2;
    const std::string tree_path = (argc > 1) ? argv[1] : "";

    if (argc > 1) {
        if (tree_path.rfind(".json") != std::string::npos
            && tree_path.find("plain_tree") != std::string::npos) {
            root = TreeExporter::loadFromAkaviaPlainTreeJSON(
                tree_path,
                &nb_classes,
                &nb_features);
        } else if (tree_path.rfind(".json") != std::string::npos) {
            root = TreeExporter::loadFromModelJSON(
                tree_path,
                &nb_classes,
                &nb_features);
        } else if (tree_path.find("plain_tree") != std::string::npos
            && tree_path.rfind(".txt") != std::string::npos) {
            root = TreeExporter::loadFromAkaviaPlainTree(
                tree_path,
                &nb_classes,
                &nb_features);
        } else {
            // Chargement depuis fichier CSV exporté par train_and_export.py
            root = TreeExporter::loadFromCSV(tree_path);
        }
        if (argc > 3) {
            nb_features = std::stoi(argv[2]);
            nb_classes  = std::stoi(argv[3]);
        }
    } else {
        // Arbre de test minimal (profondeur 2)
        root = TreeExporter::makeSimpleTestTree(nb_classes);
    }

    auto hard_tree = std::make_shared<HardTree>(root, nb_classes, nb_features);

    // ─── 2. Dataset de test ───────────────────────────────────────────────────
    std::vector<std::vector<double>> X_test;
    std::vector<int> y_test;

    bool loaded_test_data = false;
    if (argc > 1) {
        const std::string test_path = defaultTestDataPath(argv[1]);
        if (fs::path(test_path).filename() == "x_test.csv") {
            loaded_test_data = loadSplitTestData(
                test_path,
                defaultLabelPath(argv[1]),
                nb_features,
                X_test,
                y_test);
        } else {
            loaded_test_data = loadTestData(test_path, nb_features, X_test, y_test);
        }
    }

    if (!loaded_test_data) {
        std::mt19937 rng(42);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        int N_test = 200;
        X_test.assign(N_test, std::vector<double>(nb_features));
        y_test.assign(N_test, 0);
        for (int i = 0; i < N_test; ++i) {
            for (int j = 0; j < nb_features; ++j)
                X_test[i][j] = dist(rng);
            y_test[i] = hard_tree->predict(X_test[i]);
        }
    }
    // ─── 3. Collecte des gaps (pour soft adaptatif) ───────────────────────────
    const int requested_sample_count = parseSampleCount(argc, argv);
    keepFirstSamples(X_test, y_test, requested_sample_count);
    std::cout << "[main_clear] Evaluating "
              << X_test.size() << " sample(s).\n";

    hard_tree->collectGapStats(X_test);

    // ─── 4. Construction du DataLayout HBDT-SumPath ───────────────────────────
    int n_max = chooseLayoutResolution(nb_features);
    auto layout = std::make_shared<DataLayout>(n_max, nb_features);
    {
        ScopedCoutSilencer silencer(true);
        layout->buildFromTree(root);
        layout->generateRandomMasks();
        layout->printLayout();
    }

    // ─── 5. Pipeline d'inférence en clair ────────────────────────────────────
    ClearInference engine(hard_tree, layout, nb_classes);
    const double soft_window = chooseSoftWindow(tree_path);

    // Soft global : degré 8, fenêtre 0.05
    engine.configureSoftGlobal(/*degree=*/8, /*window=*/soft_window);

    // Soft adaptatif : degré calculé automatiquement par nœud
    engine.configureSoftAdaptive(/*window=*/soft_window, X_test);
    std::cout << "[main_clear] Soft window used: "
              << soft_window << "\n";

    // ─── 6. Évaluation comparative ────────────────────────────────────────────
    auto results = engine.evaluateAll(X_test, y_test);
    engine.printResults(results);

    std::vector<int> hard_preds;
    std::vector<int> soft_global_preds;
    std::vector<int> soft_adaptive_preds;
    hard_preds.reserve(X_test.size());
    soft_global_preds.reserve(X_test.size());
    soft_adaptive_preds.reserve(X_test.size());
    for (const auto& x : X_test) {
        hard_preds.push_back(hard_tree->predict(x));
        soft_global_preds.push_back(engine.predict(x, InferenceMode::SOFT_GLOBAL));
        soft_adaptive_preds.push_back(engine.predict(x, InferenceMode::SOFT_ADAPTIVE));
    }

    std::cout << "\n=== Detailed metrics - hard baseline ===\n";
    printClassificationReport(y_test, hard_preds, nb_classes);

    try {
        const fs::path output_path(resolveClearResultsFile(tree_path));
        if (!output_path.parent_path().empty()) {
            fs::create_directories(output_path.parent_path());
        }
        writeClearPredictionsCsv(
            output_path.string(),
            y_test,
            hard_preds,
            soft_global_preds,
            soft_adaptive_preds);
        std::cout << "[main_clear] Clear results saved to: "
                  << output_path.string() << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "[main_clear] Warning: " << ex.what() << "\n";
    }

    if (false) {

    // ─── 7. Exemple d'inférence individuelle ─────────────────────────────────
    std::cout << "\nExemple d'inférence individuelle :\n";
    }
    /*
    std::random_device rd;
    std::mt19937 demo_rng(rd());
    std::uniform_int_distribution<int> sample_pick(0, static_cast<int>(X_test.size()) - 1);
    int sample_idx = sample_pick(demo_rng);
    const std::vector<double>& x_demo = X_test[sample_idx];
    int y_demo = y_test[sample_idx];

    std::cout << "  Sample choisi : #" << sample_idx << "\n";
    std::cout << "  Label attendu : " << y_demo << "\n";

    std::cout << "  Input    : [";
    for (int j = 0; j < nb_features; ++j)
        std::cout << x_demo[j] << (j < nb_features-1 ? ", " : "");
    std::cout << "]\n";
    std::cout << "  Hard     : label = " << engine.predict(x_demo, InferenceMode::HARD)           << "\n";
    std::cout << "  Soft_glob: label = " << engine.predict(x_demo, InferenceMode::SOFT_GLOBAL)    << "\n";
    std::cout << "  Soft_adap: label = " << engine.predict(x_demo, InferenceMode::SOFT_ADAPTIVE)  << "\n";
    const TreeNode* predicted_leaf_node = hard_tree->predictLeaf(x_demo);
    if (predicted_leaf_node) {
        std::cout << "  Feuille predite (hard) : node_id=" << predicted_leaf_node->node_id
                  << ", label=" << predicted_leaf_node->class_label << "\n";
        std::cout << "  Image feuille          : [Leaf #" << predicted_leaf_node->node_id
                  << " -> classe " << predicted_leaf_node->class_label << "]\n";
    }
    printHardPath(hard_tree->getRoot().get(), x_demo);

    // ─── 8. Vérification SumPath manuelle ────────────────────────────────────
    std::cout << "\nVérification SumPath pour x_demo :\n";
    auto indicators  = layout->computeNodeIndicators(x_demo, false);  // hard
    auto path_scores = layout->sumPath(indicators);
    for (size_t i = 0; i < path_scores.size(); ++i)
        std::cout << "  Chemin " << i << " : score=" << path_scores[i] << "\n";
    int predicted_leaf = layout->retrieveLeafLabel(path_scores);
    std::cout << "  → Label prédit (SumPath hard) : " << predicted_leaf << "\n";

    */

    return 0;
}
