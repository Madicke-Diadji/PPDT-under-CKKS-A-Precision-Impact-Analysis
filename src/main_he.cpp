// -----------------------------------------------------------------------------
// poc_hbdt_sumpath - Inference CHIFFREE (CKKS / Microsoft SEAL)
//
// Pipeline :
//   1. Chargement de l'arbre
//   2. Configuration du contexte CKKS
//   3. Pre-calcul du modele cote serveur
//   4. Demonstration client / serveur sur un sample aleatoire
// -----------------------------------------------------------------------------

#include "ClearInference.h"
#include "DataLayout.h"
#include "HEInference.h"
#include "HardTree.h"
#include "TreeExporter.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <stdexcept>
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

bool shouldUseNodeWiseNormalization(const std::string& tree_path) {
    const fs::path path(tree_path);
    return path.filename() == "model.json";
}

bool containsInsensitive(std::string value, const std::string& needle) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string lowered_needle = needle;
    std::transform(lowered_needle.begin(), lowered_needle.end(), lowered_needle.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find(lowered_needle) != std::string::npos;
}

int choosePackedSampleCount(size_t dataset_size, int slot_size) {
    return std::max(1, std::min({static_cast<int>(dataset_size), slot_size, 32}));
}

struct HEProfile {
    const char* name;
    int layout_resolution;
    int global_degree;
    int max_degree;
    int scale_mod_size;
};

HEProfile chooseHEProfile(int nb_features, const std::string& dataset_name) {
    if (containsInsensitive(dataset_name, "heart")) {
        return {"heart_degree64", 2, 8, 64, 50};
    }
    if (nb_features <= 4) {
        return {"small", 4, 8, 32, 50};
    }
    if (nb_features <= 16) {
        return {"medium", 2, 4, 16, 45};
    }
    return {"large", 2, 4, 8, 40};
}

void capAdaptiveDegrees(const std::shared_ptr<TreeNode>& node, int max_degree) {
    if (!node || node->isLeaf()) {
        return;
    }
    node->poly_degree = std::min(node->poly_degree, max_degree);
    capAdaptiveDegrees(node->left, max_degree);
    capAdaptiveDegrees(node->right, max_degree);
}

void forceAdaptiveDegrees(const std::shared_ptr<TreeNode>& node, int degree) {
    if (!node || node->isLeaf()) {
        return;
    }
    node->poly_degree = degree;
    forceAdaptiveDegrees(node->left, degree);
    forceAdaptiveDegrees(node->right, degree);
}

void assignNodeNormFactors(
    const std::shared_ptr<TreeNode>& node,
    const std::vector<std::vector<double>>& X_calib,
    int max_degree) {
    if (!node || node->isLeaf()) {
        return;
    }
    double norm_factor = 0.0;
    for (const auto& x : X_calib) {
        if (node->feature_index >= 0
            && node->feature_index < static_cast<int>(x.size())) {
            norm_factor = std::max(
                norm_factor,
                std::abs(x[node->feature_index] - node->threshold));
        }
    }
    node->norm_factor = std::max(norm_factor, 1e-6);
    const double normalized_gap = node->min_gap / node->norm_factor;
    node->poly_degree = SoftStepApprox::chooseAdaptiveDegree(
        normalized_gap, 0.01, max_degree);
    assignNodeNormFactors(node->left, X_calib, max_degree);
    assignNodeNormFactors(node->right, X_calib, max_degree);
}

int getMaxAdaptiveDegree(const std::shared_ptr<TreeNode>& node) {
    if (!node || node->isLeaf()) {
        return 0;
    }
    return std::max({
        node->poly_degree,
        getMaxAdaptiveDegree(node->left),
        getMaxAdaptiveDegree(node->right)
    });
}

int chooseTargetMultDepth(int max_adaptive_degree) {
    const int effective_degree = std::max(2, max_adaptive_degree);
    const int poly_depth = static_cast<int>(
        std::ceil(std::log2(static_cast<double>(effective_degree))));
    return std::max(10, poly_depth + 8);
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        parts.push_back(item);
    }
    return parts;
}

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool loadTestData(const std::string& path,
                  int nb_features,
                  std::vector<std::vector<double>>& X,
                  std::vector<int>& y) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    std::string line;
    if (!std::getline(in, line)) {
        return false;
    }
    auto header_parts = splitCsvLine(line);
    const bool label_first =
        !header_parts.empty() && header_parts[0] == "label";
    std::vector<std::vector<double>> X_loaded;
    std::vector<int> y_loaded;

    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto parts = splitCsvLine(line);
        if (static_cast<int>(parts.size()) < nb_features + 1) {
            return false;
        }

        std::vector<double> row(nb_features);
        if (label_first) {
            for (int j = 0; j < nb_features; ++j) {
                row[j] = std::stod(parts[j + 1]);
            }
            y_loaded.push_back(std::stoi(parts[0]));
        } else {
            for (int j = 0; j < nb_features; ++j) {
                row[j] = std::stod(parts[j]);
            }
            y_loaded.push_back(std::stoi(parts[nb_features]));
        }
        X_loaded.push_back(row);
    }

    if (X_loaded.empty()) {
        return false;
    }

    X = std::move(X_loaded);
    y = std::move(y_loaded);
    return true;
}

bool loadFeatureMatrix(const std::string& path,
                       int nb_features,
                       std::vector<std::vector<double>>& X) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    std::vector<std::vector<double>> X_loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto parts = splitCsvLine(line);
        if (static_cast<int>(parts.size()) < nb_features) {
            return false;
        }

        std::vector<double> row(nb_features);
        for (int j = 0; j < nb_features; ++j) {
            row[j] = std::stod(parts[j]);
        }
        X_loaded.push_back(std::move(row));
    }

    if (X_loaded.empty()) {
        return false;
    }

    X = std::move(X_loaded);
    return true;
}

bool loadLabels(const std::string& path, std::vector<int>& y) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    std::vector<int> y_loaded;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        y_loaded.push_back(std::stoi(line));
    }

    if (y_loaded.empty()) {
        return false;
    }

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
            const std::string dataset = path.substr(
                dataset_start,
                dot_pos - dataset_start);
            return "data/" + dataset + "_test.csv";
        }
    }
    const fs::path filename = fs::path(tree_path).filename();
    const std::string stem = filename.stem().string();
    if (stem.rfind("tree_", 0) == 0) {
        const std::string dataset = stem.substr(std::string("tree_").size());
        return (parent / ("test_data_" + dataset + ".csv")).string();
    }
    throw std::runtime_error(
        "Unable to infer a dataset-specific test file from tree path: " + tree_path);
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
    if (stem.rfind("tree_", 0) == 0) {
        return sanitizeFileComponent(stem.substr(std::string("tree_").size()));
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

std::string resolveHeResultsFile(const std::string& tree_path) {
    const std::string env_value = getEnvValue("POC_HE_RESULTS_FILE");
    if (!env_value.empty()) {
        return env_value;
    }
    const fs::path results_dir(resolveResultsDir());
    return (results_dir / ("he_predictions_" + resolveDatasetName(tree_path) + ".csv")).string();
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

double computeAccuracyPct(const std::vector<int>& y_true,
                          const std::vector<int>& y_pred) {
    if (y_true.empty() || y_true.size() != y_pred.size()) {
        return 0.0;
    }
    int correct = 0;
    for (size_t i = 0; i < y_true.size(); ++i) {
        if (y_true[i] == y_pred[i]) {
            ++correct;
        }
    }
    return 100.0 * static_cast<double>(correct) / static_cast<double>(y_true.size());
}

int computeCorrectCount(const std::vector<int>& y_true,
                        const std::vector<int>& y_pred) {
    int correct = 0;
    for (size_t i = 0; i < y_true.size() && i < y_pred.size(); ++i) {
        if (y_true[i] == y_pred[i]) {
            ++correct;
        }
    }
    return correct;
}

void writeHePredictionsCsv(const std::string& path,
                           const std::vector<int>& y_true,
                           const std::vector<int>& soft_global_preds,
                           const std::vector<int>& soft_adaptive_preds) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Unable to write the HE results file: " + path);
    }

    out << "sample_idx,y_true,pred_soft_global_ckks,pred_soft_adaptive_ckks\n";
    for (size_t i = 0; i < y_true.size(); ++i) {
        out << i << ","
            << y_true[i] << ","
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

[[maybe_unused]] void printAdaptiveDegrees(const std::shared_ptr<TreeNode>& root) {
    std::cout << "\nAdaptive degrees by node:\n";
    std::queue<std::shared_ptr<TreeNode>> q;
    q.push(root);
    while (!q.empty()) {
        auto node = q.front();
        q.pop();
        if (!node || node->isLeaf()) {
            continue;
        }
        std::cout << "  Noeud #" << node->node_id
                  << " : X[" << node->feature_index << "] <= "
                  << std::fixed << std::setprecision(6) << node->threshold
                  << "  gap=" << node->min_gap
                  << "  -> degre=" << node->poly_degree << "\n";
        q.push(node->left);
        q.push(node->right);
    }
}

bool parseSampleString(const std::string& raw,
                       int nb_features,
                       std::vector<double>& sample) {
    std::string cleaned = raw;
    for (char& ch : cleaned) {
        if (ch == '[' || ch == ']' || ch == ';') {
            ch = ' ';
        }
    }
    std::replace(cleaned.begin(), cleaned.end(), ',', ' ');

    std::stringstream ss(cleaned);
    std::vector<double> values;
    double value = 0.0;
    while (ss >> value) {
        values.push_back(value);
    }

    if (static_cast<int>(values.size()) != nb_features) {
        return false;
    }

    sample = std::move(values);
    return true;
}
}

int main(int argc, char* argv[]) {
    std::cout.setf(std::ios::unitbuf);

    int nb_features = 2;
    int nb_classes = 2;
    std::shared_ptr<TreeNode> root;
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
            root = TreeExporter::loadFromCSV(tree_path);
        }
        if (argc > 3) {
            nb_features = std::stoi(argv[2]);
            nb_classes = std::stoi(argv[3]);
        }
    } else {
        root = TreeExporter::makeSimpleTestTree(nb_classes);
    }

    auto hard_tree = std::make_shared<HardTree>(root, nb_classes, nb_features);

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
        std::mt19937 rng_fallback(42);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        const int fallback_samples = 32;
        X_test.assign(fallback_samples, std::vector<double>(nb_features));
        y_test.assign(fallback_samples, 0);
        for (int i = 0; i < fallback_samples; ++i) {
            for (int j = 0; j < nb_features; ++j) {
                X_test[i][j] = dist(rng_fallback);
            }
            y_test[i] = hard_tree->predict(X_test[i]);
        }
    }
    const int requested_sample_count = parseSampleCount(argc, argv);
    keepFirstSamples(X_test, y_test, requested_sample_count);
    std::cout << "[main_he] Evaluation sur "
              << X_test.size() << " sample(s).\n";

    hard_tree->collectGapStats(X_test);
    const std::string dataset_name = resolveDatasetName(tree_path);
    const HEProfile he_profile = chooseHEProfile(nb_features, dataset_name);
    if (shouldUseNodeWiseNormalization(tree_path)) {
        assignNodeNormFactors(root, X_test, he_profile.max_degree);
    } else {
        capAdaptiveDegrees(root, he_profile.max_degree);
    }
    const int max_adaptive_degree = getMaxAdaptiveDegree(root);

    int n_max = he_profile.layout_resolution;
    auto layout = std::make_shared<DataLayout>(n_max, nb_features);
    {
        ScopedCoutSilencer silencer(true);
        layout->buildFromTree(root);
        layout->generateRandomMasks();
    }
    const int packed_samples = choosePackedSampleCount(X_test.size(), layout->getSlotSize());
    const bool skip_packed_batch =
        nb_features <= 4
        || layout->getM() > 4096;
    const int compact_samples_hint = skip_packed_batch
        ? std::min<int>(10, static_cast<int>(X_test.size()))
        : packed_samples;

    int mult_depth = chooseTargetMultDepth(max_adaptive_degree);
    const double soft_window = chooseSoftWindow(tree_path);

    HEInference he_engine(hard_tree, layout, nb_classes);
    {
        ScopedCoutSilencer silencer(true);
        he_engine.setupCKKS(
            /*degree_global=*/he_profile.global_degree,
            /*multDepth=*/mult_depth,
            /*scaleModSize=*/he_profile.scale_mod_size,
            /*batchSize=*/0,
            /*packedSamplesHint=*/compact_samples_hint,
            /*softWindow=*/soft_window);
        he_engine.precomputeModel();
    }

    // Demo unitaire conservee pour reference, mais non executee pour alleger la sortie console.
    /*
    std::uniform_int_distribution<int> sample_pick(0, static_cast<int>(X_test.size()) - 1);
    int sample_idx = sample_pick(rng);
    std::vector<double> x_demo = X_test[sample_idx];
    int truth = y_test[sample_idx];
    bool manual_sample = false;

    if (const char* env_sample = std::getenv("HE_SAMPLE"); env_sample && env_sample[0] != '\0') {
        std::vector<double> parsed_sample;
        if (parseSampleString(env_sample, nb_features, parsed_sample)) {
            x_demo = std::move(parsed_sample);
            truth = hard_tree->predict(x_demo);
            manual_sample = true;
        } else {
            std::cout << "[main_he] HE_SAMPLE ignore : dimension invalide, attendu "
                      << nb_features << " valeurs.\n";
        }
    }
    int pred_clear = hard_tree->predict(x_demo);

    auto run_demo = [&](const std::string& title, bool use_adaptive) {
        try {
            auto demo = he_engine.runClientServerDemo(x_demo, use_adaptive, true);
            printSortingHatStyleTimingLine(
                demo.client_encrypt_ms,
                demo.server_inference_ms,
                demo.server_inference_ms);
        } catch (const std::exception& ex) {
            std::cout << "\nFailure for " << title << ":\n";
            std::cout << "  " << ex.what() << "\n";
        }
    };

    run_demo("DEMO HE ADAPTATIF", true);
    */

    std::vector<int> preds_he_global;
    std::vector<int> preds_he_adaptive;
    bool has_global_results = false;
    bool has_adaptive_results = false;
    double avg_global_ms = 0.0;
    double avg_adaptive_ms = 0.0;
    int correct_global = 0;
    int correct_adaptive = 0;
    double accuracy_global = 0.0;
    double accuracy_adaptive = 0.0;
    std::string global_error;
    std::string adaptive_error;

    std::cout << "\n==============================================\n";
    std::cout << "  Results - encrypted inference (CKKS/SEAL)\n";
    std::cout << "==============================================\n";
    std::cout << "  Samples               : " << X_test.size() << "\n";
    std::cout << "  Soft window           : " << soft_window << "\n";

    try {
        std::cout << "[main_he] Running HE soft global...\n";
        auto t_global_0 = std::chrono::high_resolution_clock::now();
        preds_he_global = he_engine.predictEncryptedBatch(X_test, false);
        auto t_global_1 = std::chrono::high_resolution_clock::now();

        const double total_global_ms =
            std::chrono::duration<double, std::milli>(t_global_1 - t_global_0).count();
        correct_global = computeCorrectCount(y_test, preds_he_global);
        accuracy_global = computeAccuracyPct(y_test, preds_he_global);
        avg_global_ms =
            X_test.empty() ? 0.0 : total_global_ms / static_cast<double>(X_test.size());
        has_global_results = true;
    } catch (const std::exception& ex) {
        global_error = ex.what();
    }

    try {
        std::cout << "[main_he] Running HE soft adaptive...\n";
        auto t_adaptive_0 = std::chrono::high_resolution_clock::now();
        preds_he_adaptive = he_engine.predictEncryptedBatch(X_test, true);
        auto t_adaptive_1 = std::chrono::high_resolution_clock::now();

        const double total_adaptive_ms =
            std::chrono::duration<double, std::milli>(t_adaptive_1 - t_adaptive_0).count();
        correct_adaptive = computeCorrectCount(y_test, preds_he_adaptive);
        accuracy_adaptive = computeAccuracyPct(y_test, preds_he_adaptive);
        avg_adaptive_ms =
            X_test.empty() ? 0.0 : total_adaptive_ms / static_cast<double>(X_test.size());
        has_adaptive_results = true;
    } catch (const std::exception& ex) {
        adaptive_error = ex.what();
    }

    if (has_global_results) {
        std::cout << "  HE Soft global        : "
                  << correct_global << "/" << X_test.size() << " - "
                  << std::fixed << std::setprecision(2)
                  << accuracy_global << "%   " << avg_global_ms << " ms/inf\n";
    } else {
        std::cout << "  HE Soft global        : failed\n";
        std::cout << "    Reason              : " << global_error << "\n";
    }

    if (has_adaptive_results) {
        std::cout << "  HE Soft adaptive      : "
                  << correct_adaptive << "/" << X_test.size() << " - "
                  << std::fixed << std::setprecision(2)
                  << accuracy_adaptive << "%   " << avg_adaptive_ms << " ms/inf\n";
    } else {
        std::cout << "  HE Soft adaptive      : failed\n";
        std::cout << "    Reason              : " << adaptive_error << "\n";
    }

    std::cout << "  Mult. depth           : " << mult_depth << "\n";
    std::cout << "  Mode batch            : "
              << (skip_packed_batch ? "chunked" : "packed") << "\n";
    std::cout << "  Packed samples hint   : " << compact_samples_hint << "\n";

    if (has_global_results && has_adaptive_results) {
        try {
            const fs::path output_path(resolveHeResultsFile(tree_path));
            if (!output_path.parent_path().empty()) {
                fs::create_directories(output_path.parent_path());
            }
            writeHePredictionsCsv(
                output_path.string(),
                y_test,
                preds_he_global,
                preds_he_adaptive);
            std::cout << "[main_he] CKKS results saved to: "
                      << output_path.string() << "\n";
        } catch (const std::exception& ex) {
            std::cout << "[main_he] Failed to write CKKS results: "
                      << ex.what() << "\n";
        }
    }

    return 0;
}
