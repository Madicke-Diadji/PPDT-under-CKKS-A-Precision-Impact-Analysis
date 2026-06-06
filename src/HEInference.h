#pragma once

#include <seal/seal.h>

#include "DataLayout.h"
#include "HardTree.h"
#include "SoftStepApprox.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class HEInference {
public:
    using HECiphertext = seal::Ciphertext;

    HEInference(std::shared_ptr<HardTree> tree,
                std::shared_ptr<DataLayout> layout,
                int nb_classes);

    void setupCKKS(int degree_global = 8,
                   int multDepth = 12,
                   int scaleModSize = 50,
                   int batchSize = 0,
                   int packedSamplesHint = 1,
                   double softWindow = 0.05);

    void precomputeModel();

    HECiphertext encryptInput(const std::vector<double>& x) const;

    HECiphertext inferenceEncrypted(
        const HECiphertext& ct_input,
        bool use_adaptive = false) const;

    int decryptAndRetrieve(const HECiphertext& ct_result) const;

    std::vector<double> decryptValues(const HECiphertext& ct_result,
                                      int max_values = -1) const;

    int predictEncrypted(const std::vector<double>& x,
                         bool use_adaptive = false) const;

    struct DemoTimings {
        int pred_label = 0;
        double client_encrypt_ms = 0.0;
        double server_inference_ms = 0.0;
        double client_decrypt_ms = 0.0;
        double total_ms = 0.0;
    };

    DemoTimings runClientServerDemo(const std::vector<double>& x,
                                    bool use_adaptive = false,
                                    bool verbose = true) const;

    void printClientServerDemo(const std::vector<double>& x,
                               bool use_adaptive = false) const;

    HECiphertext encryptInputBatch(
        const std::vector<std::vector<double>>& X) const;

    HECiphertext inferenceEncryptedBatch(
        const HECiphertext& ct_input,
        int packed_samples,
        bool use_adaptive = false) const;

    std::vector<int> decryptAndRetrieveBatch(
        const HECiphertext& ct_result,
        int packed_samples) const;

    std::vector<int> predictEncryptedBatch(
        const std::vector<std::vector<double>>& X,
        bool use_adaptive = false) const;

    struct HEResults {
        double accuracy_he_global;
        double accuracy_he_adaptive;
        double avg_time_ms_global;
        double avg_time_ms_adaptive;
        int correct_he_global;
        int correct_he_adaptive;
        int nb_samples;
        int multDepth_used;
    };

    HEResults evaluateEncrypted(
        const std::vector<std::vector<double>>& X,
        const std::vector<int>& y_true) const;

    void printHEResults(const HEResults& r) const;

private:
    std::shared_ptr<HardTree> tree_;
    std::shared_ptr<DataLayout> layout_;
    int nb_classes_;

    std::shared_ptr<seal::SEALContext> context_;
    seal::PublicKey public_key_;
    seal::SecretKey secret_key_;
    seal::RelinKeys relin_keys_;
    seal::GaloisKeys galois_keys_;
    std::unique_ptr<seal::Encryptor> encryptor_;
    std::unique_ptr<seal::Evaluator> evaluator_;
    std::unique_ptr<seal::Decryptor> decryptor_;
    std::unique_ptr<seal::CKKSEncoder> encoder_;

    int multDepth_ = 12;
    int batchSize_ = 0;
    int packedSamplesHint_ = 1;
    int globalDeg_ = 8;
    int scaleModSize_ = 40;
    double softWindow_ = 0.05;
    bool setupDone_ = false;
    double scale_ = static_cast<double>(1ULL << 40);
    size_t polyModulusDegree_ = 0;
    size_t slotCapacity_ = 0;

    std::vector<seal::Plaintext> pt_node_masks_;
    std::vector<HECiphertext> ct_thresholds_;
    std::vector<HECiphertext> ct_thresholds_packed_;
    std::vector<int> node_degrees_;
    std::vector<double> node_norm_factors_;

    void concealTreeThresholds() const;

    seal::Plaintext encodeVector(const std::vector<double>& values) const;
    seal::Plaintext encodeConstant(double value) const;
    HECiphertext encryptPlain(const seal::Plaintext& pt) const;
    HECiphertext encryptVector(const std::vector<double>& values) const;
    HECiphertext encryptZero() const;
    HECiphertext encryptZeroAt(const seal::parms_id_type& parms_id,
                               double scale) const;

    int chainIndex(const seal::parms_id_type& parms_id) const;
    seal::parms_id_type nextParmsId(const seal::parms_id_type& parms_id) const;
    void alignCiphertexts(HECiphertext& lhs, HECiphertext& rhs) const;
    void alignCipherAndPlain(HECiphertext& ct, seal::Plaintext& pt) const;

    HECiphertext addCiphertexts(const HECiphertext& lhs,
                                const HECiphertext& rhs) const;
    HECiphertext addPlain(const HECiphertext& ct,
                          const seal::Plaintext& pt) const;
    HECiphertext multiplyCiphertexts(const HECiphertext& lhs,
                                     const HECiphertext& rhs) const;
    HECiphertext multiplyPlain(const HECiphertext& ct,
                               const seal::Plaintext& pt) const;
    HECiphertext negateCiphertext(const HECiphertext& ct) const;
    HECiphertext rotateCiphertext(const HECiphertext& ct, int steps) const;

    HECiphertext evalSoftStep(
        const HECiphertext& ct_t, int degree) const;

    HECiphertext evalPolyBSGS(
        const HECiphertext& ct_x,
        const std::vector<double>& coeffs) const;

    HECiphertext getPowerBSGS(
        const HECiphertext& ct_x,
        int exponent,
        std::unordered_map<int, HECiphertext>& power_cache) const;

    HECiphertext evalNodeIndicator(
        const HECiphertext& ct_input,
        int node_block_idx,
        int degree,
        bool normalize_logits = false) const;

    HECiphertext evalNodeIndicatorPacked(
        const HECiphertext& ct_input,
        int node_block_idx,
        int degree,
        int packed_samples,
        bool normalize_logits = false) const;

    std::vector<HECiphertext> sumPathEncrypted(
        const std::vector<HECiphertext>& ct_indicators) const;

    HECiphertext sumAllSlots(
        const HECiphertext& ct) const;

    HECiphertext sumFirstSlots(
        const HECiphertext& ct,
        int active_slots) const;
};
