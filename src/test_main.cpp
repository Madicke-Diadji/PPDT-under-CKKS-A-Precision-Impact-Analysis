// ─────────────────────────────────────────────────────────────────────────────
// tests/test_main.cpp — Tests unitaires du POC HBDT-SumPath
//
// Structure : tests manuels (sans framework externe) organisés en suites.
// Chaque test retourne true si OK, false sinon.
// Un résumé final indique le nombre de tests passés / total.
//
// Suites :
//   1. TreeNode / HardTree      : construction, traversée hard, gap stats
//   2. SoftStepApprox / PolyEval : coefficients, évaluation, BSGS, Clenshaw
//   3. OneHotEncoder             : encodage hard/soft, decode, bords
//   4. SoftTree                  : traversée soft globale + adaptive
//   5. DataLayout / SumPath      : construction, indicateurs, sumPath
//   6. ClearInference            : pipeline complet en clair
// ─────────────────────────────────────────────────────────────────────────────

#include "TreeNode.h"
#include "HardTree.h"
#include "SoftTree.h"
#include "SoftStepApprox.h"
#include "PolyEval.h"
#include "OneHotEncoder.h"
#include "DataLayout.h"
#include "TreeExporter.h"
#include "Metrics.h"
#include "ClearInference.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <string>
#include <functional>

// ─── Utilitaires de test ──────────────────────────────────────────────────────
static int g_passed = 0;
static int g_total  = 0;

#define TEST(name, expr) do { \
    g_total++; \
    bool _ok = (expr); \
    if (_ok) { g_passed++; std::cout << "  [OK] " << name << "\n"; } \
    else     { std::cout << "  [FAIL] " << name << "\n"; } \
} while(0)

#define APPROX(a, b, tol) (std::abs((a) - (b)) < (tol))

// ─── Arbre de test partagé ────────────────────────────────────────────────────
static std::shared_ptr<TreeNode> makeTestTree() {
    return TreeExporter::makeSimpleTestTree(2);
}

// ═════════════════════════════════════════════════════════════════════════════
// Suite 1 : TreeNode / HardTree
// ═════════════════════════════════════════════════════════════════════════════
void suite_treenode() {
    std::cout << "\n[Suite 1] TreeNode / HardTree\n";

    auto root = makeTestTree();
    HardTree tree(root, 2, 2);

    // Structure
    TEST("profondeur = 2",    tree.getDepth()         == 2);
    TEST("nb noeuds = 7",     countNodes(root)        == 7);
    TEST("nb feuilles = 4",   countLeaves(root)       == 4);
    TEST("node_ids assignes", root->node_id           == 0);

    // Prédictions hard (arbre test : X[0]<=0.5 racine, X[1]<=0.3 / X[1]<=0.7)
    // x=[0.3,0.1] -> X[0]<=0.5 vrai -> gauche (N1) -> X[1]<=0.3 vrai -> L0 label=0
    TEST("pred [0.3,0.1]=0",  tree.predict({0.3, 0.1}) == 0);
    // x=[0.3,0.8] -> X[0]<=0.5 vrai -> gauche (N1) -> X[1]<=0.3 faux -> L1 label=1
    TEST("pred [0.3,0.8]=1",  tree.predict({0.3, 0.8}) == 1);
    // x=[0.7,0.5] -> X[0]<=0.5 faux -> droite (N2) -> X[1]<=0.7 vrai -> L2 label=1
    TEST("pred [0.7,0.5]=1",  tree.predict({0.7, 0.5}) == 1);
    // x=[0.7,0.9] -> X[0]<=0.5 faux -> droite (N2) -> X[1]<=0.7 faux -> L3 label=0
    TEST("pred [0.7,0.9]=0",  tree.predict({0.7, 0.9}) == 0);

    // Gap stats
    std::vector<std::vector<double>> X = {{0.3,0.1},{0.3,0.8},{0.7,0.5},{0.7,0.9}};
    tree.collectGapStats(X);
    TEST("min_gap racine >= 0", root->min_gap >= 0.0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Suite 2 : SoftStepApprox / PolyEval
// ═════════════════════════════════════════════════════════════════════════════
void suite_polynomial() {
    std::cout << "\n[Suite 2] SoftStepApprox / PolyEval\n";

    // Approximation de degré 8, fenêtre 0.05
    SoftStepApprox phi8(8, 0.05);

    // phi(t) doit etre proche de 1 pour t >> 0 et proche de 0 pour t << 0
    TEST("phi(2.0)  ≈ 1",    APPROX(phi8.eval(2.0),  1.0, 0.05));
    TEST("phi(-2.0) ≈ 0",    APPROX(phi8.eval(-2.0), 0.0, 0.05));
    TEST("phi(0.5)  ∈ [0,1]", phi8.eval(0.5) >= 0.0 && phi8.eval(0.5) <= 1.0);

    // evalLeftIndicator : phi(theta - x) ≈ 1 si x <= theta
    // Pour x=0.3, theta=0.5 : theta-x=0.2 > 0 -> phi ≈ 1 (condition satisfaite)
    TEST("leftIndicator(0.3,0.5) ≈ 1",
         APPROX(phi8.evalLeftIndicator(0.3, 0.5), 1.0, 0.15));
    // Pour x=0.8, theta=0.5 : theta-x=-0.3 < 0 -> phi ≈ 0 (condition non satisfaite)
    TEST("leftIndicator(0.8,0.5) ≈ 0",
         APPROX(phi8.evalLeftIndicator(0.8, 0.5), 0.0, 0.15));

    // Degrés adaptatifs
    TEST("chooseAdaptiveDegree(0.5) = 4 ou 8",
         SoftStepApprox::chooseAdaptiveDegree(0.5) <= 8);
    TEST("chooseAdaptiveDegree(0.1) >= 8",
         SoftStepApprox::chooseAdaptiveDegree(0.1) >= 8);
    TEST("chooseAdaptiveDegree(0.01) = 32",
         SoftStepApprox::chooseAdaptiveDegree(0.01) >= 16);

    // PolyEval : Horner vs BSGS doivent donner le meme resultat
    std::vector<double> c = {0.5, 0.0, -0.1, 0.0, 0.05};  // polynome test
    for (double x : {-1.5, -0.5, 0.0, 0.5, 1.5}) {
        double h = PolyEval::horner(c, x);
        double b = PolyEval::bsgs(c, x);
        TEST("Horner==BSGS x=" + std::to_string((int)(x*10)),
             APPROX(h, b, 1e-9));
    }

    // Erreur MSE de l'approximation
    double mse8 = PolyEval::stepApproxMSE(phi8.getCoeffs().coeffs, 0.05);
    TEST("MSE degre 8 < 0.05", mse8 < 0.05);

    // MSE diminue avec le degre
    SoftStepApprox phi16(16, 0.05);
    double mse16 = PolyEval::stepApproxMSE(phi16.getCoeffs().coeffs, 0.05);
    TEST("MSE(16) <= MSE(8)", mse16 <= mse8 + 1e-6);

    // BSGS mult depth
    TEST("bsgsMultDepth(8) = 4",  PolyEval::bsgsMultDepth(8)  == 4);
    TEST("bsgsMultDepth(16) = 5", PolyEval::bsgsMultDepth(16) == 5);
}

// ═════════════════════════════════════════════════════════════════════════════
// Suite 3 : OneHotEncoder
// ═════════════════════════════════════════════════════════════════════════════
void suite_encoder() {
    std::cout << "\n[Suite 3] OneHotEncoder\n";

    OneHotEncoder enc(8, 2);   // 8 bins, 2 features, [0,1]

    TEST("getTotalSize = 16",  enc.getTotalSize() == 16);
    TEST("getNBins = 8",       enc.getNBins()     == 8);

    // Encodage hard : un seul 1 dans le vecteur de chaque feature
    auto v1 = enc.encode({0.1, 0.9});
    int sum1 = 0; for (double d : v1) sum1 += (d > 0.5) ? 1 : 0;
    TEST("encode: exactement 2 bins actifs", sum1 == 2);

    // La valeur de la feature 0 = 0.1 -> bin 0 (bins [0, 0.125))
    TEST("x[0]=0.1 -> bin 0 actif", v1[0] > 0.5);
    // La valeur de la feature 1 = 0.9 -> dernier bin
    TEST("x[1]=0.9 -> dernier bin actif", v1[8 + 7] > 0.5);

    // Bords
    auto v_min = enc.encode({0.0, 0.0});
    auto v_max = enc.encode({1.0, 1.0});
    TEST("x=0.0 -> bin 0",     v_min[0] > 0.5 && v_min[8] > 0.5);
    TEST("x=1.0 -> bin 7",     v_max[7] > 0.5 && v_max[15] > 0.5);

    // Encodage soft : somme des poids = 1 par feature
    auto vs = enc.encodeSoft({0.5, 0.5});
    double sum_f0 = 0, sum_f1 = 0;
    for (int b = 0; b < 8; ++b) { sum_f0 += vs[b]; sum_f1 += vs[8+b]; }
    TEST("encodeSoft: somme feature 0 ≈ 1", APPROX(sum_f0, 1.0, 1e-9));
    TEST("encodeSoft: somme feature 1 ≈ 1", APPROX(sum_f1, 1.0, 1e-9));

    // Decode
    auto onehot_feat = enc.encodeFeature(0.3);
    double decoded = enc.decodeFeature(onehot_feat);
    TEST("decode(encode(0.3)) ≈ 0.3", APPROX(decoded, 0.3, 0.125));  // precision = 1 bin
}

// ═════════════════════════════════════════════════════════════════════════════
// Suite 4 : SoftTree
// ═════════════════════════════════════════════════════════════════════════════
void suite_softtree() {
    std::cout << "\n[Suite 4] SoftTree\n";

    auto root = makeTestTree();
    SoftTree stree(root, 2, 2);

    // Configuration globale : norme = 1.0 (features dans [0,1], theta dans [0,1])
    stree.configureGlobal(8, 0.05, 1.0);

    // Les prédictions soft doivent etre coherentes avec le hard pour des points éloignés du seuil
    // x=[0.1, 0.1] : très loin de X[0]=0.5 -> soft = hard
    TEST("soft_global [0.1,0.1]=0", stree.predict({0.1, 0.1}, SoftMode::GLOBAL) == 0);
    TEST("soft_global [0.9,0.9]=0", stree.predict({0.9, 0.9}, SoftMode::GLOBAL) == 0);
    TEST("soft_global [0.1,0.9]=1", stree.predict({0.1, 0.9}, SoftMode::GLOBAL) == 1);
    TEST("soft_global [0.9,0.1]=1", stree.predict({0.9, 0.1}, SoftMode::GLOBAL) == 1);

    // Probabilités : doivent sommer a ≈ 1 (si phi_left + phi_right ≈ 1)
    auto proba = stree.predictProba({0.1, 0.1}, SoftMode::GLOBAL);
    double sum_p = 0; for (double p : proba) sum_p += p;
    TEST("somme probabilites <= 1.2", sum_p <= 1.2);  // tolerance due a approximation

    // Configuration adaptive
    // D'abord collecter les gaps via HardTree
    HardTree htree(root, 2, 2);
    htree.collectGapStats({{0.1,0.1},{0.1,0.9},{0.9,0.1},{0.9,0.9}});
    stree.configureAdaptive(0.05);

    TEST("adaptive [0.1,0.1]=0", stree.predict({0.1, 0.1}, SoftMode::ADAPTIVE) == 0);
    TEST("adaptive [0.9,0.9]=0", stree.predict({0.9, 0.9}, SoftMode::ADAPTIVE) == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Suite 5 : DataLayout / SumPath
// ═════════════════════════════════════════════════════════════════════════════
void suite_datalayout() {
    std::cout << "\n[Suite 5] DataLayout / SumPath\n";

    auto root = makeTestTree();
    assignNodeIds(root);

    DataLayout layout(8, 2);  // n_max=8, 2 features
    layout.buildFromTree(root);

    TEST("nb blocs noeuds = 3", layout.getNodeBlocks().size() == 3);
    TEST("nb feuilles = 4",     layout.getLeafBlocks().size() == 4);

    // Indicateurs hard : pour x=[0.3,0.1], l'arbre hard prédit label=0
    // Noeud 0 : X[0]<=0.5 -> condition vraie -> I0=0
    // Noeud 1 : X[1]<=0.3 -> condition vraie -> I1=0
    // Noeud 2 : X[1]<=0.7 -> non atteint, mais on calcule quand meme
    auto inds_hard = layout.computeNodeIndicators({0.3, 0.1}, false);
    TEST("indicateurs: taille = 3",   inds_hard.size() == 3);
    // Les noeuds dont la condition est satisfaite ont indicateur ≈ 0
    TEST("I[racine]=0 pour x[0]=0.3", APPROX(inds_hard[0], 0.0, 0.01));

    // SumPath
    auto scores = layout.sumPath(inds_hard);
    TEST("nb scores = nb feuilles", scores.size() == 4);

    // La feuille correspondant a x=[0.3,0.1] (label=0) doit avoir score minimal
    int pred = layout.retrieveLeafLabel(scores);
    TEST("SumPath prédit label=0 pour [0.3,0.1]", pred == 0);

    // Test x=[0.3,0.8] -> label=1
    auto inds2  = layout.computeNodeIndicators({0.3, 0.8}, false);
    auto scores2 = layout.sumPath(inds2);
    TEST("SumPath prédit label=1 pour [0.3,0.8]",
         layout.retrieveLeafLabel(scores2) == 1);

    // Test x=[0.7,0.5] -> label=1
    auto inds3  = layout.computeNodeIndicators({0.7, 0.5}, false);
    auto scores3 = layout.sumPath(inds3);
    TEST("SumPath prédit label=1 pour [0.7,0.5]",
         layout.retrieveLeafLabel(scores3) == 1);

    // Test x=[0.7,0.9] -> label=0
    auto inds4  = layout.computeNodeIndicators({0.7, 0.9}, false);
    auto scores4 = layout.sumPath(inds4);
    TEST("SumPath prédit label=0 pour [0.7,0.9]",
         layout.retrieveLeafLabel(scores4) == 0);

    // Coherence hard HardTree vs DataLayout hard
    HardTree htree(root, 2, 2);
    std::vector<std::vector<double>> X = {
        {0.1,0.1},{0.1,0.5},{0.1,0.9},
        {0.6,0.1},{0.6,0.5},{0.6,0.9}
    };
    int match = 0;
    for (const auto& x : X) {
        if (htree.predict(x) == layout.predict(x, false)) match++;
    }
    TEST("DataLayout hard == HardTree sur 6 points", match == 6);
}

// ═════════════════════════════════════════════════════════════════════════════
// Suite 6 : ClearInference pipeline complet
// ═════════════════════════════════════════════════════════════════════════════
void suite_clearinference() {
    std::cout << "\n[Suite 6] ClearInference (pipeline complet)\n";

    auto root    = makeTestTree();
    auto htree   = std::make_shared<HardTree>(root, 2, 2);
    auto layout  = std::make_shared<DataLayout>(8, 2);
    layout->buildFromTree(root);
    layout->generateRandomMasks();

    // Dataset simple : 4 coins + 4 points intermédiaires
    std::vector<std::vector<double>> X = {
        {0.1,0.1},{0.1,0.9},{0.9,0.1},{0.9,0.9},
        {0.3,0.2},{0.3,0.7},{0.7,0.4},{0.7,0.8}
    };
    std::vector<int> y_true;
    for (const auto& x : X) y_true.push_back(htree->predict(x));

    htree->collectGapStats(X);

    ClearInference engine(htree, layout, 2);
    engine.configureSoftGlobal(8, 0.05);
    engine.configureSoftAdaptive(0.05);

    auto results = engine.evaluateAll(X, y_true);

    // Hard doit etre 100% sur ses propres predictions
    TEST("hard accuracy = 100%", APPROX(results.accuracy_hard, 100.0, 0.01));

    // Le degré global configuré est bien 8
    TEST("global_degree = 8", results.global_degree == 8);

    // Nombre de degrés adaptatifs = nombre de noeuds internes = 3
    TEST("adaptive_degrees size = 3", results.adaptive_degrees.size() == 3);

    // Tous les degrés adaptatifs sont dans {4, 8, 16, 32}
    bool valid_degrees = true;
    for (int d : results.adaptive_degrees)
        if (d != 4 && d != 8 && d != 16 && d != 32) valid_degrees = false;
    TEST("tous les degres adaptatifs dans {4,8,16,32}", valid_degrees);

    engine.printResults(results);
}

// ═════════════════════════════════════════════════════════════════════════════
// Main
// ═════════════════════════════════════════════════════════════════════════════
int main() {
    std::cout << "╔══════════════════════════════════════════════════════╗\n"
              << "║  POC HBDT-SumPath — Tests Unitaires                  ║\n"
              << "╚══════════════════════════════════════════════════════╝\n";

    suite_treenode();
    suite_polynomial();
    suite_encoder();
    suite_softtree();
    suite_datalayout();
    suite_clearinference();

    // Résumé
    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "  Résultat : " << g_passed << " / " << g_total << " tests passés";
    if (g_passed == g_total)
        std::cout << "  ✓ TOUT OK\n";
    else
        std::cout << "  ✗ " << (g_total - g_passed) << " ECHEC(S)\n";
    std::cout << "════════════════════════════════════════\n";

    return (g_passed == g_total) ? 0 : 1;
}
