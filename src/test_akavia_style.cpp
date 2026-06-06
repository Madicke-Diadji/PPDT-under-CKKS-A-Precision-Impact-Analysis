#include "ClearInference.h"
#include "DataLayout.h"
#include "HEInference.h"
#include "HardTree.h"
#include "TreeExporter.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int g_passed = 0;
int g_total = 0;

#define TEST(name, expr) do { \
    g_total++; \
    bool _ok = (expr); \
    if (_ok) { g_passed++; std::cout << "  [OK] " << name << "\n"; } \
    else     { std::cout << "  [FAIL] " << name << "\n"; } \
} while(0)

std::shared_ptr<TreeNode> makeTestTree() {
    return TreeExporter::makeSimpleTestTree(2);
}

std::shared_ptr<DataLayout> makeLayout(std::shared_ptr<TreeNode> root) {
    auto layout = std::make_shared<DataLayout>(8, 2);
    layout->buildFromTree(root);
    layout->generateRandomMasks();
    return layout;
}

void setupEngine(HEInference& he, int packed_samples) {
    he.setupCKKS(
        /*degree_global=*/8,
        /*multDepth=*/12,
        /*scaleModSize=*/50,
        /*batchSize=*/0,
        /*packedSamplesHint=*/packed_samples);
    he.precomputeModel();
}

void suite_he_vs_clear_single() {
    std::cout << "\n[Suite A] HE vs Clair (sample unique)\n";

    auto root = makeTestTree();
    auto tree = std::make_shared<HardTree>(root, 2, 2);
    auto layout = makeLayout(root);
    tree->collectGapStats({
        {0.1, 0.1}, {0.1, 0.9}, {0.9, 0.1}, {0.9, 0.9}
    });

    ClearInference clear(tree, layout, 2);
    clear.configureSoftAdaptive(0.05);

    const std::vector<std::vector<double>> X = {
        {0.1, 0.1},
        {0.1, 0.9},
        {0.9, 0.1},
        {0.9, 0.9}
    };

    std::vector<int> expected_clear;
    expected_clear.reserve(X.size());
    for (const auto& x : X) {
        expected_clear.push_back(clear.predict(x, InferenceMode::SOFT_ADAPTIVE));
    }

    HEInference he(tree, layout, 2);
    setupEngine(he, 4);

    for (size_t i = 0; i < X.size(); ++i) {
        const int pred_clear = expected_clear[i];
        auto ct_result = he.inferenceEncrypted(he.encryptInput(X[i]), true);
        const int pred_he = he.decryptAndRetrieve(ct_result);
        TEST("HE==clair sample " + std::to_string(i), pred_he == pred_clear);
    }
}

void suite_packing_consistency() {
    std::cout << "\n[Suite B] Cohérence packing\n";

    auto root = makeTestTree();
    auto tree = std::make_shared<HardTree>(root, 2, 2);
    auto layout = makeLayout(root);
    tree->collectGapStats({
        {0.1, 0.1}, {0.1, 0.9}, {0.9, 0.1}, {0.9, 0.9}
    });

    HEInference he(tree, layout, 2);
    setupEngine(he, 4);

    const std::vector<std::vector<double>> X = {
        {0.1, 0.1},
        {0.1, 0.9},
        {0.9, 0.1},
        {0.9, 0.9}
    };

    std::vector<int> preds_unitary;
    preds_unitary.reserve(X.size());
    for (const auto& x : X) {
        preds_unitary.push_back(he.predictEncrypted(x, true));
    }

    auto ct_batch = he.encryptInputBatch(X);
    auto ct_result = he.inferenceEncryptedBatch(
        ct_batch,
        static_cast<int>(X.size()),
        true);
    auto preds_packed = he.decryptAndRetrieveBatch(
        ct_result,
        static_cast<int>(X.size()));

    TEST("taille batch = 4", preds_packed.size() == X.size());
    for (size_t i = 0; i < X.size(); ++i) {
        TEST("packing==unitaire sample " + std::to_string(i),
             preds_packed[i] == preds_unitary[i]);
    }
}

void suite_demo_timing_sanity() {
    std::cout << "\n[Suite C] Sanité demo HE\n";

    auto root = makeTestTree();
    auto tree = std::make_shared<HardTree>(root, 2, 2);
    auto layout = makeLayout(root);
    tree->collectGapStats({
        {0.1, 0.1}, {0.1, 0.9}, {0.9, 0.1}, {0.9, 0.9}
    });

    HEInference he(tree, layout, 2);
    setupEngine(he, 4);

    auto demo = he.runClientServerDemo({0.1, 0.1}, true, false);
    TEST("temps chiffrement >= 0", demo.client_encrypt_ms >= 0.0);
    TEST("temps inference >= 0", demo.server_inference_ms >= 0.0);
    TEST("temps dechiffrement >= 0", demo.client_decrypt_ms >= 0.0);
    TEST("temps total >= inference", demo.total_ms >= demo.server_inference_ms);
}
}

int main() {
    std::cout << "==========================================================\n"
              << "  POC HBDT-SumPath - Tests style Akavia\n"
              << "==========================================================\n";

    suite_he_vs_clear_single();
    suite_packing_consistency();
    suite_demo_timing_sanity();

    std::cout << "\n==========================================================\n";
    std::cout << "  Resultat : " << g_passed << " / " << g_total << " tests passes";
    if (g_passed == g_total) {
        std::cout << "  OK\n";
    } else {
        std::cout << "  ECHECS=" << (g_total - g_passed) << "\n";
    }
    std::cout << "==========================================================\n";

    return (g_passed == g_total) ? 0 : 1;
}
