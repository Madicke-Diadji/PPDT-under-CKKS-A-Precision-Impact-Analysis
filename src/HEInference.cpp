#include "HEInference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace {
constexpr double kHeCoeffEpsilon = 1e-10;

template <typename Clock = std::chrono::high_resolution_clock>
double elapsedMs(const typename Clock::time_point& start,
                 const typename Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void concealTreeThresholdsRec(const std::shared_ptr<TreeNode>& node) {
    if (!node) {
        return;
    }
    if (!node->isLeaf()) {
        node->threshold = std::numeric_limits<double>::quiet_NaN();
        concealTreeThresholdsRec(node->left);
        concealTreeThresholdsRec(node->right);
    }
}

std::vector<int> buildCoeffModulusBits(int mult_depth, int scale_mod_size) {
    std::vector<int> bits;
    bits.reserve(static_cast<size_t>(mult_depth) + 2);
    bits.push_back(60);
    for (int i = 0; i < std::max(1, mult_depth); ++i) {
        bits.push_back(scale_mod_size);
    }
    bits.push_back(60);
    return bits;
}

size_t choosePolyModulusDegree(size_t min_slots_needed,
                               const std::vector<int>& coeff_modulus_bits) {
    const size_t min_poly_degree = std::max<size_t>(8192, min_slots_needed * 2);
    const std::vector<size_t> candidates = {8192, 16384, 32768};
    const int required_bits = std::accumulate(
        coeff_modulus_bits.begin(),
        coeff_modulus_bits.end(),
        0);

    for (size_t degree : candidates) {
        if (degree < min_poly_degree) {
            continue;
        }
        if (seal::CoeffModulus::MaxBitCount(degree) >= required_bits) {
            return degree;
        }
    }

    throw std::runtime_error(
        "Impossible de choisir un poly_modulus_degree compatible avec la profondeur CKKS demandee.");
}
}

HEInference::HEInference(std::shared_ptr<HardTree> tree,
                         std::shared_ptr<DataLayout> layout,
                         int nb_classes)
    : tree_(std::move(tree)),
      layout_(std::move(layout)),
      nb_classes_(nb_classes) {}

void HEInference::setupCKKS(int degree_global,
                            int multDepth,
                            int scaleModSize,
                            int batchSize,
                            int packedSamplesHint,
                            double softWindow) {
    globalDeg_ = degree_global;
    multDepth_ = multDepth;
    scaleModSize_ = scaleModSize;
    batchSize_ = (batchSize > 0) ? batchSize : layout_->getM();
    packedSamplesHint_ = std::max(1, packedSamplesHint);
    softWindow_ = softWindow;
    scale_ = std::pow(2.0, static_cast<double>(scaleModSize_));

    std::cout << "[HEInference] setupCKKS(SEAL): preparation des parametres\n"
              << "  Batch size demande : " << batchSize_ << "\n"
              << "  Block size max     : " << layout_->getSlotSize() << "\n"
              << "  Noeuds internes    : " << layout_->getNodeBlocks().size() << "\n"
              << "  Feuilles           : " << layout_->getLeafBlocks().size() << "\n"
              << "  Soft window        : " << softWindow_ << "\n";

    const auto coeff_modulus_bits = buildCoeffModulusBits(multDepth_, scaleModSize_);
    polyModulusDegree_ = choosePolyModulusDegree(
        static_cast<size_t>(batchSize_),
        coeff_modulus_bits);

    seal::EncryptionParameters parms(seal::scheme_type::ckks);
    parms.set_poly_modulus_degree(polyModulusDegree_);
    parms.set_coeff_modulus(
        seal::CoeffModulus::Create(polyModulusDegree_, coeff_modulus_bits));

    auto t_ctx_0 = std::chrono::high_resolution_clock::now();
    context_ = std::make_shared<seal::SEALContext>(parms);
    auto t_ctx_1 = std::chrono::high_resolution_clock::now();
    std::cout << "[HEInference] setupCKKS(SEAL): contexte cree en "
              << elapsedMs(t_ctx_0, t_ctx_1) << " ms\n";

    auto first_context_data = context_->first_context_data();
    if (!first_context_data || !first_context_data->qualifiers().parameters_set()) {
        throw std::runtime_error("Parametres SEAL invalides pour CKKS.");
    }

    encoder_ = std::make_unique<seal::CKKSEncoder>(*context_);
    slotCapacity_ = encoder_->slot_count();
    if (static_cast<size_t>(batchSize_) > slotCapacity_) {
        throw std::runtime_error(
            "Le batch size demande depasse la capacite de slots du contexte SEAL.");
    }

    auto t_keygen_0 = std::chrono::high_resolution_clock::now();
    seal::KeyGenerator keygen(*context_);
    secret_key_ = keygen.secret_key();
    keygen.create_public_key(public_key_);
    keygen.create_relin_keys(relin_keys_);
    auto t_keygen_1 = std::chrono::high_resolution_clock::now();
    std::cout << "[HEInference] setupCKKS(SEAL): KeyGen/ReLin termine en "
              << elapsedMs(t_keygen_0, t_keygen_1) << " ms\n";

    std::vector<int> rot_indices;
    const int block_size = layout_->getSlotSize();
    for (int i = 1; i < block_size; i <<= 1) {
        rot_indices.push_back(i);
    }
    for (const auto& block : layout_->getNodeBlocks()) {
        if (block.slot_start != 0) {
            rot_indices.push_back(block.slot_start);
            rot_indices.push_back(-block.slot_start);
        }
    }
    for (const auto& leaf : layout_->getLeafBlocks()) {
        const int packed_rotation = leaf.slot_position * packedSamplesHint_;
        if (packed_rotation != 0) {
            rot_indices.push_back(packed_rotation);
            rot_indices.push_back(-packed_rotation);
        }
    }
    std::sort(rot_indices.begin(), rot_indices.end());
    rot_indices.erase(std::unique(rot_indices.begin(), rot_indices.end()), rot_indices.end());

    auto t_rotkey_0 = std::chrono::high_resolution_clock::now();
    keygen.create_galois_keys(rot_indices, galois_keys_);
    auto t_rotkey_1 = std::chrono::high_resolution_clock::now();
    std::cout << "[HEInference] setupCKKS(SEAL): Galois keys terminees en "
              << elapsedMs(t_rotkey_0, t_rotkey_1) << " ms\n";

    encryptor_ = std::make_unique<seal::Encryptor>(*context_, public_key_);
    evaluator_ = std::make_unique<seal::Evaluator>(*context_);
    decryptor_ = std::make_unique<seal::Decryptor>(*context_, secret_key_);

    node_degrees_.resize(layout_->getNodeBlocks().size(), globalDeg_);
    node_norm_factors_.resize(layout_->getNodeBlocks().size(), 1.0);
    {
        std::queue<std::shared_ptr<TreeNode>> q;
        q.push(tree_->getRoot());
        size_t idx = 0;
        while (!q.empty() && idx < node_degrees_.size()) {
            auto node = q.front();
            q.pop();
            if (!node || node->isLeaf()) {
                continue;
            }
            node_degrees_[idx++] =
                (node->poly_degree > 0) ? node->poly_degree : globalDeg_;
            node_norm_factors_[idx - 1] =
                (node->norm_factor > 0.0) ? node->norm_factor : 1.0;
            q.push(node->left);
            q.push(node->right);
        }
    }

    setupDone_ = true;

    std::cout << "[HEInference] Contexte CKKS/SEAL initialise\n"
              << "  Poly modulus degree : " << polyModulusDegree_ << "\n"
              << "  Capacite slots      : " << slotCapacity_ << "\n"
              << "  Batch size utilise  : " << batchSize_ << "\n"
              << "  Mult depth cible    : " << multDepth_ << "\n"
              << "  Degree global       : " << globalDeg_ << "\n";
}

void HEInference::precomputeModel() {
    if (!setupDone_) {
        throw std::runtime_error("Appelez setupCKKS() d'abord.");
    }

    const auto& blocks = layout_->getNodeBlocks();

    pt_node_masks_.clear();
    ct_thresholds_.clear();
    ct_thresholds_packed_.clear();

    for (const auto& block : blocks) {
        std::vector<double> mask(batchSize_, 0.0);
        std::vector<double> thresh_vec(batchSize_, block.threshold);
        std::vector<double> thresh_vec_packed(batchSize_, 0.0);
        const int slot_end = std::min(batchSize_, block.slot_start + block.block_size);
        for (int slot = block.slot_start; slot < slot_end; ++slot) {
            mask[slot] = 1.0;
        }
        for (int slot = 0; slot < std::min(batchSize_, packedSamplesHint_); ++slot) {
            thresh_vec_packed[slot] = block.threshold;
        }
        pt_node_masks_.push_back(encodeVector(mask));
        ct_thresholds_.push_back(encryptVector(thresh_vec));
        ct_thresholds_packed_.push_back(encryptVector(thresh_vec_packed));
    }

    std::cout << "[HEInference] Modele pre-calcule : "
              << blocks.size() << " noeuds, "
              << layout_->getLeafBlocks().size() << " feuilles\n";

    concealTreeThresholds();
    layout_->concealThresholds();
    std::cout << "[HEInference] Seuils du modele masques en clair apres pre-calcul.\n";
}

seal::Plaintext HEInference::encodeVector(const std::vector<double>& values) const {
    seal::Plaintext pt;
    encoder_->encode(values, scale_, pt);
    return pt;
}

seal::Plaintext HEInference::encodeConstant(double value) const {
    std::vector<double> constant_vec(batchSize_, value);
    return encodeVector(constant_vec);
}

HEInference::HECiphertext HEInference::encryptPlain(const seal::Plaintext& pt) const {
    HECiphertext ct;
    encryptor_->encrypt(pt, ct);
    return ct;
}

HEInference::HECiphertext HEInference::encryptVector(const std::vector<double>& values) const {
    return encryptPlain(encodeVector(values));
}

HEInference::HECiphertext HEInference::encryptZero() const {
    std::vector<double> zeros(batchSize_, 0.0);
    return encryptVector(zeros);
}

HEInference::HECiphertext HEInference::encryptZeroAt(
    const seal::parms_id_type& parms_id,
    double scale) const {
    std::vector<double> zeros(batchSize_, 0.0);
    seal::Plaintext pt_zero;
    encoder_->encode(zeros, scale, pt_zero);
    if (pt_zero.parms_id() != parms_id) {
        evaluator_->mod_switch_to_inplace(pt_zero, parms_id);
    }

    HECiphertext ct_zero;
    encryptor_->encrypt(pt_zero, ct_zero);
    ct_zero.scale() = scale;
    return ct_zero;
}

int HEInference::chainIndex(const seal::parms_id_type& parms_id) const {
    auto data = context_->get_context_data(parms_id);
    if (!data) {
        throw std::runtime_error("Niveau CKKS introuvable dans le contexte SEAL.");
    }
    return static_cast<int>(data->chain_index());
}

seal::parms_id_type HEInference::nextParmsId(
    const seal::parms_id_type& parms_id) const {
    auto data = context_->get_context_data(parms_id);
    if (!data || !data->next_context_data()) {
        throw std::runtime_error("Aucun niveau CKKS suivant disponible.");
    }
    return data->next_context_data()->parms_id();
}

void HEInference::alignCiphertexts(HECiphertext& lhs, HECiphertext& rhs) const {
    const int lhs_chain = chainIndex(lhs.parms_id());
    const int rhs_chain = chainIndex(rhs.parms_id());

    if (lhs_chain > rhs_chain) {
        evaluator_->mod_switch_to_inplace(lhs, rhs.parms_id());
    } else if (rhs_chain > lhs_chain) {
        evaluator_->mod_switch_to_inplace(rhs, lhs.parms_id());
    }

    const double common_scale = std::min(lhs.scale(), rhs.scale());
    lhs.scale() = common_scale;
    rhs.scale() = common_scale;
}

void HEInference::alignCipherAndPlain(HECiphertext& ct, seal::Plaintext& pt) const {
    const int ct_chain = chainIndex(ct.parms_id());
    const int pt_chain = chainIndex(pt.parms_id());

    if (pt_chain > ct_chain) {
        evaluator_->mod_switch_to_inplace(pt, ct.parms_id());
    } else if (ct_chain > pt_chain) {
        evaluator_->mod_switch_to_inplace(ct, pt.parms_id());
    }

    pt.scale() = ct.scale();
}

HEInference::HECiphertext HEInference::addCiphertexts(
    const HECiphertext& lhs,
    const HECiphertext& rhs) const {
    try {
        auto left = lhs;
        auto right = rhs;
        alignCiphertexts(left, right);

        HECiphertext result;
        evaluator_->add(left, right, result);
        if (result.is_transparent()) {
            return encryptZeroAt(left.parms_id(), left.scale());
        }
        result.scale() = left.scale();
        return result;
    } catch (const std::exception& ex) {
        if (std::string(ex.what()).find("transparent") != std::string::npos) {
            auto left = lhs;
            auto right = rhs;
            alignCiphertexts(left, right);
            return encryptZeroAt(left.parms_id(), left.scale());
        }
        std::ostringstream oss;
        oss << "addCiphertexts(lhs_chain=" << chainIndex(lhs.parms_id())
            << ", rhs_chain=" << chainIndex(rhs.parms_id())
            << ", lhs_scale=" << lhs.scale()
            << ", rhs_scale=" << rhs.scale()
            << ") : " << ex.what();
        throw std::runtime_error(oss.str());
    }
}

HEInference::HECiphertext HEInference::addPlain(
    const HECiphertext& ct,
    const seal::Plaintext& pt) const {
    try {
        auto aligned_ct = ct;
        auto aligned_pt = pt;
        alignCipherAndPlain(aligned_ct, aligned_pt);

        HECiphertext result;
        evaluator_->add_plain(aligned_ct, aligned_pt, result);
        if (result.is_transparent()) {
            return encryptZeroAt(aligned_ct.parms_id(), aligned_ct.scale());
        }
        result.scale() = aligned_ct.scale();
        return result;
    } catch (const std::exception& ex) {
        if (std::string(ex.what()).find("transparent") != std::string::npos) {
            auto aligned_ct = ct;
            auto aligned_pt = pt;
            alignCipherAndPlain(aligned_ct, aligned_pt);
            return encryptZeroAt(aligned_ct.parms_id(), aligned_ct.scale());
        }
        std::ostringstream oss;
        oss << "addPlain(ct_chain=" << chainIndex(ct.parms_id())
            << ", pt_chain=" << chainIndex(pt.parms_id())
            << ", ct_scale=" << ct.scale()
            << ", pt_scale=" << pt.scale()
            << ") : " << ex.what();
        throw std::runtime_error(oss.str());
    }
}

HEInference::HECiphertext HEInference::multiplyCiphertexts(
    const HECiphertext& lhs,
    const HECiphertext& rhs) const {
    try {
        auto left = lhs;
        auto right = rhs;
        alignCiphertexts(left, right);

        HECiphertext product;
        evaluator_->multiply(left, right, product);
        if (product.is_transparent()) {
            return encryptZeroAt(nextParmsId(left.parms_id()), scale_);
        }
        evaluator_->relinearize_inplace(product, relin_keys_);
        evaluator_->rescale_to_next_inplace(product);
        if (product.is_transparent()) {
            return encryptZeroAt(product.parms_id(), scale_);
        }
        product.scale() = scale_;
        return product;
    } catch (const std::exception& ex) {
        if (std::string(ex.what()).find("transparent") != std::string::npos) {
            auto left = lhs;
            auto right = rhs;
            alignCiphertexts(left, right);
            return encryptZeroAt(nextParmsId(left.parms_id()), scale_);
        }
        std::ostringstream oss;
        oss << "multiplyCiphertexts(lhs_chain=" << chainIndex(lhs.parms_id())
            << ", rhs_chain=" << chainIndex(rhs.parms_id())
            << ", lhs_scale=" << lhs.scale()
            << ", rhs_scale=" << rhs.scale()
            << ") : " << ex.what();
        throw std::runtime_error(oss.str());
    }
}

HEInference::HECiphertext HEInference::multiplyPlain(
    const HECiphertext& ct,
    const seal::Plaintext& pt) const {
    try {
        auto aligned_ct = ct;
        auto aligned_pt = pt;
        alignCipherAndPlain(aligned_ct, aligned_pt);

        HECiphertext product;
        evaluator_->multiply_plain(aligned_ct, aligned_pt, product);
        if (product.is_transparent()) {
            return encryptZeroAt(nextParmsId(aligned_ct.parms_id()), scale_);
        }
        evaluator_->rescale_to_next_inplace(product);
        if (product.is_transparent()) {
            return encryptZeroAt(product.parms_id(), scale_);
        }
        product.scale() = scale_;
        return product;
    } catch (const std::exception& ex) {
        if (std::string(ex.what()).find("transparent") != std::string::npos) {
            auto aligned_ct = ct;
            auto aligned_pt = pt;
            alignCipherAndPlain(aligned_ct, aligned_pt);
            return encryptZeroAt(nextParmsId(aligned_ct.parms_id()), scale_);
        }
        std::ostringstream oss;
        oss << "multiplyPlain(ct_chain=" << chainIndex(ct.parms_id())
            << ", pt_chain=" << chainIndex(pt.parms_id())
            << ", ct_scale=" << ct.scale()
            << ", pt_scale=" << pt.scale()
            << ") : " << ex.what();
        throw std::runtime_error(oss.str());
    }
}

HEInference::HECiphertext HEInference::negateCiphertext(const HECiphertext& ct) const {
    HECiphertext result;
    evaluator_->negate(ct, result);
    result.scale() = ct.scale();
    return result;
}

HEInference::HECiphertext HEInference::rotateCiphertext(
    const HECiphertext& ct,
    int steps) const {
    if (steps == 0) {
        return ct;
    }
    try {
        HECiphertext rotated;
        evaluator_->rotate_vector(ct, steps, galois_keys_, rotated);
        if (rotated.is_transparent()) {
            return encryptZeroAt(ct.parms_id(), ct.scale());
        }
        rotated.scale() = ct.scale();
        return rotated;
    } catch (const std::exception& ex) {
        if (std::string(ex.what()).find("transparent") != std::string::npos) {
            return encryptZeroAt(ct.parms_id(), ct.scale());
        }
        std::ostringstream oss;
        oss << "rotateCiphertext(steps=" << steps
            << ", chain=" << chainIndex(ct.parms_id())
            << ", scale=" << ct.scale()
            << ") : " << ex.what();
        throw std::runtime_error(oss.str());
    }
}

HEInference::HECiphertext HEInference::encryptInput(const std::vector<double>& x) const {
    const auto& blocks = layout_->getNodeBlocks();
    std::vector<double> encoded(batchSize_, 0.0);

    for (const auto& block : blocks) {
        const double value = x[block.feature_index];
        const int slot_end = std::min(batchSize_, block.slot_start + block.block_size);
        for (int slot = block.slot_start; slot < slot_end; ++slot) {
            encoded[slot] = value;
        }
    }

    return encryptVector(encoded);
}

HEInference::HECiphertext HEInference::sumAllSlots(const HECiphertext& ct) const {
    auto acc = ct;
    for (int shift = 1; shift < batchSize_; shift <<= 1) {
        acc = addCiphertexts(acc, rotateCiphertext(acc, shift));
    }
    return acc;
}

HEInference::HECiphertext HEInference::sumFirstSlots(
    const HECiphertext& ct,
    int active_slots) const {
    auto acc = ct;
    for (int shift = 1; shift < active_slots; shift <<= 1) {
        acc = addCiphertexts(acc, rotateCiphertext(acc, shift));
    }
    return acc;
}

HEInference::HECiphertext HEInference::evalSoftStep(
    const HECiphertext& ct_t,
    int degree) const {
    SoftStepApprox phi(degree, softWindow_);
    return evalPolyBSGS(ct_t, phi.getCoeffs().coeffs);
}

HEInference::HECiphertext HEInference::getPowerBSGS(
    const HECiphertext& ct_x,
    int exponent,
    std::unordered_map<int, HECiphertext>& power_cache) const {
    if (exponent < 1) {
        throw std::runtime_error("getPowerBSGS: exponent doit etre >= 1.");
    }

    const auto it = power_cache.find(exponent);
    if (it != power_cache.end()) {
        return it->second;
    }

    HECiphertext power;
    if (exponent == 1) {
        power = ct_x;
    } else if (exponent % 2 == 0) {
        auto half = getPowerBSGS(ct_x, exponent / 2, power_cache);
        power = multiplyCiphertexts(half, half);
    } else {
        const int left_exp = exponent / 2;
        const int right_exp = exponent - left_exp;
        auto left = getPowerBSGS(ct_x, left_exp, power_cache);
        auto right = getPowerBSGS(ct_x, right_exp, power_cache);
        power = multiplyCiphertexts(left, right);
    }

    power_cache.emplace(exponent, power);
    return power;
}

HEInference::HECiphertext HEInference::evalPolyBSGS(
    const HECiphertext& ct_x,
    const std::vector<double>& coeffs) const {
    if (coeffs.empty()) {
        return encryptZero();
    }

    const int degree = static_cast<int>(coeffs.size()) - 1;
    auto result = encryptZero();

    if (degree == 0) {
        return addPlain(result, encodeConstant(coeffs[0]));
    }

    const int baby_step = static_cast<int>(
        std::ceil(std::sqrt(static_cast<double>(degree + 1))));
    const int giant_step_count = (degree / baby_step) + 1;

    std::unordered_map<int, HECiphertext> power_cache;
    power_cache.emplace(1, ct_x);

    std::vector<HECiphertext> baby_powers;
    baby_powers.reserve(static_cast<size_t>(std::max(0, baby_step - 1)));
    for (int exp = 1; exp < baby_step; ++exp) {
        baby_powers.push_back(getPowerBSGS(ct_x, exp, power_cache));
    }

    std::vector<HECiphertext> giant_powers(static_cast<size_t>(giant_step_count));
    for (int giant_idx = 1; giant_idx < giant_step_count; ++giant_idx) {
        const int exponent = giant_idx * baby_step;
        giant_powers[static_cast<size_t>(giant_idx)] =
            getPowerBSGS(ct_x, exponent, power_cache);
    }

    for (int giant_idx = 0; giant_idx < giant_step_count; ++giant_idx) {
        HECiphertext block_poly;
        bool block_has_term = false;
        double block_constant = 0.0;

        for (int baby_idx = 0; baby_idx < baby_step; ++baby_idx) {
            const int exponent = giant_idx * baby_step + baby_idx;
            if (exponent > degree) {
                break;
            }

            const double coeff = coeffs[static_cast<size_t>(exponent)];
            if (std::abs(coeff) < kHeCoeffEpsilon) {
                continue;
            }

            if (baby_idx == 0) {
                block_constant += coeff;
                continue;
            }

            HECiphertext term;
            try {
                term = multiplyPlain(
                    baby_powers[static_cast<size_t>(baby_idx - 1)],
                    encodeConstant(coeff));
            } catch (const std::exception& ex) {
                std::ostringstream oss;
                oss << "evalPolyBSGS(term exponent=" << exponent
                    << ", coeff=" << coeff
                    << ", giant_idx=" << giant_idx
                    << ", baby_idx=" << baby_idx
                    << ") : " << ex.what();
                throw std::runtime_error(oss.str());
            }

            if (!block_has_term) {
                block_poly = term;
                block_has_term = true;
            } else {
                block_poly = addCiphertexts(block_poly, term);
            }
        }

        if (std::abs(block_constant) > kHeCoeffEpsilon) {
            if (!block_has_term) {
                block_poly = encryptPlain(encodeConstant(block_constant));
                block_has_term = true;
            } else {
                block_poly = addPlain(block_poly, encodeConstant(block_constant));
            }
        }

        if (!block_has_term) {
            continue;
        }

        if (giant_idx == 0) {
            result = addCiphertexts(result, block_poly);
        } else {
            auto block_scaled = multiplyCiphertexts(
                block_poly,
                giant_powers[static_cast<size_t>(giant_idx)]);
            result = addCiphertexts(result, block_scaled);
        }
    }

    return result;
}

HEInference::HECiphertext HEInference::evalNodeIndicator(
    const HECiphertext& ct_input,
    int node_block_idx,
    int degree,
    bool normalize_logits) const {
    const auto& block = layout_->getNodeBlocks()[static_cast<size_t>(node_block_idx)];
    try {
        // Chaque bloc encode deja la meme feature sur toute sa fenetre.
        // On amene donc simplement le bloc courant en tete des slots utiles,
        // sans remultiplier par un masque clair ni recalculer une moyenne.
        auto ct_feature = (block.slot_start == 0)
            ? ct_input
            : rotateCiphertext(ct_input, block.slot_start);

        auto ct_t = addCiphertexts(
            negateCiphertext(ct_feature),
            ct_thresholds_[static_cast<size_t>(node_block_idx)]);
        if (normalize_logits && static_cast<size_t>(node_block_idx) < node_norm_factors_.size()) {
            const double norm_factor = std::max(
                node_norm_factors_[static_cast<size_t>(node_block_idx)],
                1e-6);
            ct_t = multiplyPlain(ct_t, encodeConstant(1.0 / norm_factor));
        }

        auto ct_left_indicator = evalSoftStep(ct_t, degree);
        return addPlain(negateCiphertext(ct_left_indicator), encodeConstant(1.0));
    } catch (const std::exception& ex) {
        std::ostringstream oss;
        oss << "evalNodeIndicator(node_id=" << block.node_id
            << ", feature=" << block.feature_index
            << ", slot_start=" << block.slot_start
            << ", block_size=" << block.block_size
            << ", degree=" << degree
            << ") : " << ex.what();
        throw std::runtime_error(oss.str());
    }
}

HEInference::HECiphertext HEInference::evalNodeIndicatorPacked(
    const HECiphertext& ct_input,
    int node_block_idx,
    int degree,
    int packed_samples,
    bool normalize_logits) const {
    const auto& block = layout_->getNodeBlocks()[static_cast<size_t>(node_block_idx)];
    try {
        auto ct_feature = (block.slot_start == 0)
            ? ct_input
            : rotateCiphertext(ct_input, block.slot_start);

        auto ct_t = addCiphertexts(
            negateCiphertext(ct_feature),
            ct_thresholds_packed_[static_cast<size_t>(node_block_idx)]);
        if (normalize_logits && static_cast<size_t>(node_block_idx) < node_norm_factors_.size()) {
            const double norm_factor = std::max(
                node_norm_factors_[static_cast<size_t>(node_block_idx)],
                1e-6);
            ct_t = multiplyPlain(ct_t, encodeConstant(1.0 / norm_factor));
        }
        auto ct_left_indicator = evalSoftStep(ct_t, degree);

        std::vector<double> ones(batchSize_, 0.0);
        for (int s = 0; s < packed_samples; ++s) {
            ones[s] = 1.0;
        }
        return addPlain(negateCiphertext(ct_left_indicator), encodeVector(ones));
    } catch (const std::exception& ex) {
        std::ostringstream oss;
        oss << "evalNodeIndicatorPacked(node_id=" << block.node_id
            << ", feature=" << block.feature_index
            << ", slot_start=" << block.slot_start
            << ", packed_samples=" << packed_samples
            << ", degree=" << degree
            << ") : " << ex.what();
        throw std::runtime_error(oss.str());
    }
}

std::vector<HEInference::HECiphertext> HEInference::sumPathEncrypted(
    const std::vector<HECiphertext>& ct_indicators) const {
    const auto& leaves = layout_->getLeafBlocks();
    const auto& paths = layout_->getPaths();
    const auto& blocks = layout_->getNodeBlocks();

    auto ct_zero = encryptZero();
    const auto pt_one = encodeConstant(1.0);

    std::vector<HECiphertext> ct_path_scores(leaves.size(), ct_zero);

    for (size_t leaf_idx = 0; leaf_idx < leaves.size() && leaf_idx < paths.size(); ++leaf_idx) {
        auto score = ct_zero;
        for (const auto& step : paths[leaf_idx]) {
            int block_idx = -1;
            for (size_t i = 0; i < blocks.size(); ++i) {
                if (blocks[i].node_id == step.node_id) {
                    block_idx = static_cast<int>(i);
                    break;
                }
            }
            if (block_idx < 0 || block_idx >= static_cast<int>(ct_indicators.size())) {
                continue;
            }

            if (step.went_left) {
                score = addCiphertexts(score, ct_indicators[static_cast<size_t>(block_idx)]);
            } else {
                score = addCiphertexts(
                    score,
                    addPlain(
                        negateCiphertext(ct_indicators[static_cast<size_t>(block_idx)]),
                        pt_one));
            }
        }
        ct_path_scores[leaf_idx] = score;
    }

    return ct_path_scores;
}

HEInference::HECiphertext HEInference::inferenceEncrypted(
    const HECiphertext& ct_input,
    bool use_adaptive) const {
    if (!setupDone_) {
        throw std::runtime_error("Appelez setupCKKS() puis precomputeModel().");
    }

    const auto& blocks = layout_->getNodeBlocks();
    const auto& leaves = layout_->getLeafBlocks();

    auto result = encryptZero();

    std::vector<HECiphertext> ct_indicators(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const int degree = use_adaptive ? node_degrees_[i] : globalDeg_;
        ct_indicators[i] = evalNodeIndicator(
            ct_input,
            static_cast<int>(i),
            degree,
            use_adaptive);
    }

    auto ct_path_scores = sumPathEncrypted(ct_indicators);

    for (size_t i = 0; i < leaves.size(); ++i) {
        std::vector<double> slot_mask(batchSize_, 0.0);
        if (leaves[i].slot_position < batchSize_) {
            slot_mask[leaves[i].slot_position] = 1.0;
        }
        auto ct_contrib = multiplyPlain(ct_path_scores[i], encodeVector(slot_mask));
        result = addCiphertexts(result, ct_contrib);
    }

    return result;
}

HEInference::HECiphertext HEInference::encryptInputBatch(
    const std::vector<std::vector<double>>& X) const {
    if (X.empty()) {
        throw std::runtime_error("encryptInputBatch: batch vide.");
    }

    const int packed_samples = static_cast<int>(X.size());
    const auto& blocks = layout_->getNodeBlocks();
    std::vector<double> encoded(batchSize_, 0.0);

    for (const auto& block : blocks) {
        for (int s = 0; s < packed_samples; ++s) {
            const int slot = block.slot_start + s;
            if (slot < batchSize_) {
                encoded[slot] = X[static_cast<size_t>(s)][block.feature_index];
            }
        }
    }

    return encryptVector(encoded);
}

HEInference::HECiphertext HEInference::inferenceEncryptedBatch(
    const HECiphertext& ct_input,
    int packed_samples,
    bool use_adaptive) const {
    if (!setupDone_) {
        throw std::runtime_error("Appelez setupCKKS() puis precomputeModel().");
    }
    if (packed_samples <= 0 || packed_samples > packedSamplesHint_) {
        throw std::runtime_error("inferenceEncryptedBatch: packed_samples invalide.");
    }

    const auto& blocks = layout_->getNodeBlocks();
    const auto& leaves = layout_->getLeafBlocks();

    std::vector<HECiphertext> ct_indicators(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const int degree = use_adaptive ? node_degrees_[i] : globalDeg_;
        ct_indicators[i] = evalNodeIndicatorPacked(
            ct_input,
            static_cast<int>(i),
            degree,
            packed_samples,
            use_adaptive);
    }

    auto ct_path_scores = sumPathEncrypted(ct_indicators);
    auto result = encryptZero();

    for (size_t i = 0; i < leaves.size(); ++i) {
        std::vector<double> base_mask(batchSize_, 0.0);
        for (int s = 0; s < packed_samples; ++s) {
            base_mask[s] = 1.0;
        }
        auto ct_contrib = multiplyPlain(ct_path_scores[i], encodeVector(base_mask));
        if (leaves[i].slot_position != 0) {
            ct_contrib = rotateCiphertext(
                ct_contrib,
                -leaves[i].slot_position * packed_samples);
        }
        result = addCiphertexts(result, ct_contrib);
    }

    return result;
}

int HEInference::decryptAndRetrieve(const HECiphertext& ct_result) const {
    const auto& leaves = layout_->getLeafBlocks();

    const auto values = decryptValues(ct_result);

    double best = std::numeric_limits<double>::max();
    int pred = 0;
    for (const auto& leaf : leaves) {
        if (leaf.slot_position >= static_cast<int>(values.size())) {
            continue;
        }
        const double score = values[static_cast<size_t>(leaf.slot_position)];
        if (score < best) {
            best = score;
            pred = leaf.class_label;
        }
    }

    return pred;
}

std::vector<double> HEInference::decryptValues(const HECiphertext& ct_result,
                                               int max_values) const {
    if (ct_result.is_transparent()) {
        std::vector<double> values(batchSize_, 0.0);
        if (max_values > 0 && max_values < batchSize_) {
            values.resize(static_cast<size_t>(max_values));
        }
        return values;
    }

    seal::Plaintext pt_result;
    decryptor_->decrypt(ct_result, pt_result);
    std::vector<double> values;
    encoder_->decode(pt_result, values);
    if (max_values > 0 && static_cast<size_t>(max_values) < values.size()) {
        values.resize(static_cast<size_t>(max_values));
    }
    return values;
}

std::vector<int> HEInference::decryptAndRetrieveBatch(
    const HECiphertext& ct_result,
    int packed_samples) const {
    const auto& leaves = layout_->getLeafBlocks();

    const auto values = decryptValues(ct_result);

    std::vector<int> preds(static_cast<size_t>(packed_samples), 0);
    for (int sample_idx = 0; sample_idx < packed_samples; ++sample_idx) {
        double best = std::numeric_limits<double>::max();
        int pred = 0;
        for (const auto& leaf : leaves) {
            const int slot = leaf.slot_position * packed_samples + sample_idx;
            if (slot >= static_cast<int>(values.size())) {
                continue;
            }
            const double score = values[static_cast<size_t>(slot)];
            if (score < best) {
                best = score;
                pred = leaf.class_label;
            }
        }
        preds[static_cast<size_t>(sample_idx)] = pred;
    }
    return preds;
}

int HEInference::predictEncrypted(const std::vector<double>& x,
                                  bool use_adaptive) const {
    auto ct_input = encryptInput(x);
    auto ct_result = inferenceEncrypted(ct_input, use_adaptive);
    return decryptAndRetrieve(ct_result);
}

void HEInference::concealTreeThresholds() const {
    concealTreeThresholdsRec(tree_->getRoot());
}

HEInference::DemoTimings HEInference::runClientServerDemo(
    const std::vector<double>& x,
    bool use_adaptive,
    bool verbose) const {
    if (!setupDone_) {
        throw std::runtime_error("Appelez setupCKKS() puis precomputeModel().");
    }

    const auto& blocks = layout_->getNodeBlocks();
    const auto& leaves = layout_->getLeafBlocks();
    const auto& paths = layout_->getPaths();

    DemoTimings timings;

    if (verbose) {
        std::cout << "\n========================================================\n"
                  << " DEMO CLIENT / SERVEUR - INFERENCE HE CKKS SUR 1 SAMPLE \n"
                  << "========================================================\n";
    }

    if (verbose) {
        std::cout << "\n[Client] Etape 1 - Preparation des donnees\n";
        std::cout << "  x = [";
        for (size_t i = 0; i < x.size(); ++i) {
            std::cout << x[i] << (i + 1 < x.size() ? ", " : "");
        }
        std::cout << "]\n";
        std::cout << "  Le client chiffre son vecteur dans un ciphertext CKKS/SEAL.\n";
    }

    auto t_client_encrypt_0 = std::chrono::high_resolution_clock::now();
    auto ct_input = encryptInput(x);
    auto t_client_encrypt_1 = std::chrono::high_resolution_clock::now();
    timings.client_encrypt_ms = elapsedMs(t_client_encrypt_0, t_client_encrypt_1);

    if (verbose) {
        std::cout << "  Ciphertext cree avec batch_size=" << batchSize_
                  << " slots utiles.\n";
        std::cout << "  Temps client chiffrement : "
                  << timings.client_encrypt_ms << " ms\n";
        std::cout << "\n[Serveur] Etape 2 - Traitement homomorphe du modele\n";
        std::cout << "  Le serveur ne voit jamais x en clair.\n";
    }

    auto t_server_0 = std::chrono::high_resolution_clock::now();
    std::vector<HECiphertext> ct_indicators(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const int degree = use_adaptive ? node_degrees_[i] : globalDeg_;
        const auto& block = blocks[i];
        if (verbose) {
            std::cout << "  Noeud " << block.node_id
                      << " : feature X[" << block.feature_index << "]"
                      << ", seuil=<masque>"
                      << ", degre_poly=" << degree << "\n";
        }
        ct_indicators[i] = evalNodeIndicator(
            ct_input,
            static_cast<int>(i),
            degree,
            use_adaptive);
    }

    if (verbose) {
        std::cout << "  SumPath agrege les scores sur les feuilles candidates.\n";
    }
    auto ct_path_scores = sumPathEncrypted(ct_indicators);

    auto ct_result = encryptZero();
    for (size_t i = 0; i < leaves.size(); ++i) {
        std::vector<double> slot_mask(batchSize_, 0.0);
        if (leaves[i].slot_position < batchSize_) {
            slot_mask[leaves[i].slot_position] = 1.0;
        }
        auto ct_contrib = multiplyPlain(ct_path_scores[i], encodeVector(slot_mask));
        ct_result = addCiphertexts(ct_result, ct_contrib);
    }
    auto t_server_1 = std::chrono::high_resolution_clock::now();
    timings.server_inference_ms = elapsedMs(t_server_0, t_server_1);

    for (size_t leaf_idx = 0; leaf_idx < leaves.size() && leaf_idx < paths.size(); ++leaf_idx) {
        if (verbose) {
            std::cout << "  Feuille " << leaves[leaf_idx].node_id
                      << " : classe=" << leaves[leaf_idx].class_label
                      << ", longueur_chemin=" << paths[leaf_idx].size() << "\n";
        }
    }

    if (verbose) {
        std::cout << "  Le serveur renvoie un ciphertext resultat au client.\n";
        std::cout << "  Temps serveur inference : "
                  << timings.server_inference_ms << " ms\n";
        std::cout << "\n[Client] Etape 3 - Dechiffrement final\n";
    }

    auto t_client_decrypt_0 = std::chrono::high_resolution_clock::now();
    timings.pred_label = decryptAndRetrieve(ct_result);
    auto t_client_decrypt_1 = std::chrono::high_resolution_clock::now();
    timings.client_decrypt_ms = elapsedMs(t_client_decrypt_0, t_client_decrypt_1);
    timings.total_ms = timings.client_encrypt_ms
        + timings.server_inference_ms
        + timings.client_decrypt_ms;

    if (verbose) {
        std::cout << "  Label predit apres dechiffrement : "
                  << timings.pred_label << "\n";
        std::cout << "  Temps client dechiffrement : "
                  << timings.client_decrypt_ms << " ms\n";
        std::cout << "  Temps total client->serveur->client : "
                  << timings.total_ms << " ms\n";
        std::cout << "  La decision finale reste cote client.\n";
    }

    return timings;
}

void HEInference::printClientServerDemo(const std::vector<double>& x,
                                        bool use_adaptive) const {
    (void)runClientServerDemo(x, use_adaptive, true);
}

std::vector<int> HEInference::predictEncryptedBatch(
    const std::vector<std::vector<double>>& X,
    bool use_adaptive) const {
    if (X.size() <= static_cast<size_t>(packedSamplesHint_)) {
        auto ct_input = encryptInputBatch(X);
        auto ct_result = inferenceEncryptedBatch(
            ct_input,
            static_cast<int>(X.size()),
            use_adaptive);
        return decryptAndRetrieveBatch(ct_result, static_cast<int>(X.size()));
    }

    std::vector<int> preds;
    preds.reserve(X.size());
    for (size_t offset = 0; offset < X.size(); offset += static_cast<size_t>(packedSamplesHint_)) {
        const size_t count = std::min<size_t>(
            static_cast<size_t>(packedSamplesHint_),
            X.size() - offset);
        std::vector<std::vector<double>> chunk(
            X.begin() + static_cast<std::ptrdiff_t>(offset),
            X.begin() + static_cast<std::ptrdiff_t>(offset + count));
        auto chunk_preds = predictEncryptedBatch(chunk, use_adaptive);
        preds.insert(preds.end(), chunk_preds.begin(), chunk_preds.end());
    }
    return preds;
}

HEInference::HEResults HEInference::evaluateEncrypted(
    const std::vector<std::vector<double>>& X,
    const std::vector<int>& y_true) const {
    const int n = static_cast<int>(X.size());
    int corr_global = 0;
    int corr_adaptive = 0;
    double time_global = 0.0;
    double time_adaptive = 0.0;

    for (int i = 0; i < n; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        const int pred_global = predictEncrypted(X[static_cast<size_t>(i)], false);
        auto end = std::chrono::high_resolution_clock::now();
        time_global += elapsedMs(start, end);
        if (pred_global == y_true[static_cast<size_t>(i)]) {
            ++corr_global;
        }

        start = std::chrono::high_resolution_clock::now();
        const int pred_adaptive = predictEncrypted(X[static_cast<size_t>(i)], true);
        end = std::chrono::high_resolution_clock::now();
        time_adaptive += elapsedMs(start, end);
        if (pred_adaptive == y_true[static_cast<size_t>(i)]) {
            ++corr_adaptive;
        }
    }

    HEResults r{};
    r.nb_samples = n;
    r.correct_he_global = corr_global;
    r.correct_he_adaptive = corr_adaptive;
    r.accuracy_he_global = 100.0 * static_cast<double>(corr_global) / static_cast<double>(n);
    r.accuracy_he_adaptive = 100.0 * static_cast<double>(corr_adaptive) / static_cast<double>(n);
    r.avg_time_ms_global = time_global / static_cast<double>(n);
    r.avg_time_ms_adaptive = time_adaptive / static_cast<double>(n);
    r.multDepth_used = multDepth_;
    return r;
}

void HEInference::printHEResults(const HEResults& r) const {
    std::cout << "\n==============================================\n"
              << "  Resultats - inference chiffree (CKKS/SEAL)\n"
              << "==============================================\n"
              << "  Samples           : " << r.nb_samples << "\n"
              << "  HE Soft global    : "
              << r.correct_he_global << "/" << r.nb_samples << " - "
              << std::fixed << std::setprecision(2) << r.accuracy_he_global
              << "%   " << r.avg_time_ms_global << " ms/inf\n"
              << "  Soft adaptatif (chiffre) : "
              << r.correct_he_adaptive << "/" << r.nb_samples << " - "
              << r.accuracy_he_adaptive
              << "%   " << r.avg_time_ms_adaptive << " ms/inf\n"
              << "  Mult. depth       : " << r.multDepth_used << "\n";
}
