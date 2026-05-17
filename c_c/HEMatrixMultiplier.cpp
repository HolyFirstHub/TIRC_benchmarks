#include "HEMatrixMultiplier.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>
#include <numeric>
#include <atomic>
#include <future>

using Matrix = std::vector<std::vector<double>>;

HEMatrixMultiplier::HEMatrixMultiplier(size_t poly_modulus_degree, int thread_count)
    : thread_count_(thread_count) {
    logInfo("--- Precomputation Phase (Not Timed) ---");
    initializeSEAL(poly_modulus_degree);
    generateKeys();
    logInfo("--- Precomputation Completed ---");
}

HEMatrixMultiplier::~HEMatrixMultiplier() {}

void HEMatrixMultiplier::initializeSEAL(size_t poly_modulus_degree) {
    seal::EncryptionParameters parms(seal::scheme_type::ckks);
    parms.set_poly_modulus_degree(poly_modulus_degree);
    parms.set_coeff_modulus(seal::CoeffModulus::Create(poly_modulus_degree, {60, 40, 40, 60}));

    context_ = std::make_unique<seal::SEALContext>(parms);

    if (!context_->parameters_set()) {
        throw HEMatrixMultiplierException("Invalid SEAL parameter settings.");
    }

    encoder_ = std::make_unique<seal::CKKSEncoder>(*context_);
    evaluator_ = std::make_unique<seal::Evaluator>(*context_);

    logInfo("SEAL initialized successfully. Slot count: " + std::to_string(encoder_->slot_count()));
}

void HEMatrixMultiplier::generateKeys() {
    logInfo("Generating keys...");

    keygen_ = std::make_unique<seal::KeyGenerator>(*context_);
    auto secret_key = keygen_->secret_key();

    public_key_ = std::make_unique<seal::PublicKey>();
    keygen_->create_public_key(*public_key_);

    encryptor_ = std::make_unique<seal::Encryptor>(*context_, *public_key_);
    decryptor_ = std::make_unique<seal::Decryptor>(*context_, secret_key);

    relin_keys_ = std::make_unique<seal::RelinKeys>();
    keygen_->create_relin_keys(*relin_keys_);

    std::vector<int> rotation_steps;

    for (size_t j = 1; j < BSGS_BABY_STEP; ++j) {
        rotation_steps.push_back(j * M);
    }

    for (size_t i = 1; i < BSGS_GIANT_STEP; ++i) {
        rotation_steps.push_back(i * BSGS_BABY_STEP * M);
    }

    galois_keys_ = std::make_unique<seal::GaloisKeys>();
    keygen_->create_galois_keys(rotation_steps, *galois_keys_);

    logInfo("Key generation completed.");
}

seal::Ciphertext HEMatrixMultiplier::encrypt_data(const Matrix& A) {
    auto start_time = std::chrono::high_resolution_clock::now();

    validateMatrixDimensions_A(A);

    auto A_packed = packMatrixByColumns(A);

    seal::Plaintext pt_A;
    double scale = pow(2.0, 40);

    encoder_->encode(A_packed, scale, pt_A);

    seal::Ciphertext ct_A;
    encryptor_->encrypt(pt_A, ct_A);

    auto end_time = std::chrono::high_resolution_clock::now();

    last_stats_.data_party_encryption_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::stringstream ss;
    ct_A.save(ss);
    last_stats_.communication_upload_mb = static_cast<double>(ss.tellp()) / (1024 * 1024);

    return ct_A;
}

std::vector<seal::Ciphertext> HEMatrixMultiplier::precompute_encrypted_B_variants(
    const Matrix& B_secret) {
    auto start_time = std::chrono::high_resolution_clock::now();

    validateMatrixDimensions_B(B_secret);

    std::vector<seal::Ciphertext> encrypted_B_variants(M);
    double scale = pow(2.0, 40);

    parallelFor(0, M, [this, &encrypted_B_variants, &B_secret, scale](int d) {
        auto B_d_packed = packAndShiftMatrixB(B_secret, d);

        seal::Plaintext pt_B_d;
        encoder_->encode(B_d_packed, scale, pt_B_d);
        encryptor_->encrypt(pt_B_d, encrypted_B_variants[d]);
    });

    auto end_time = std::chrono::high_resolution_clock::now();

    last_stats_.model_party_encryption_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return encrypted_B_variants;
}

std::vector<seal::Ciphertext> HEMatrixMultiplier::compute_on_server_encrypted(
    const seal::Ciphertext& ct_A,
    const std::vector<seal::Ciphertext>& precomputed_encrypted_B_variants) {

    auto start_time = std::chrono::high_resolution_clock::now();

    last_stats_.rotation_count = 0;
    std::vector<seal::Ciphertext> diagonals(M);

    parallelFor(0, M, [this, &diagonals, &ct_A, &precomputed_encrypted_B_variants](int d) {
        const auto& ct_B_d = precomputed_encrypted_B_variants[d];

        seal::Ciphertext ct_C_prime_d;

        evaluator_->multiply(ct_A, ct_B_d, ct_C_prime_d);
        evaluator_->relinearize_inplace(ct_C_prime_d, *relin_keys_);
        evaluator_->rescale_to_next_inplace(ct_C_prime_d);

        diagonals[d] = sumBlocks(ct_C_prime_d);
    });

    auto end_time = std::chrono::high_resolution_clock::now();

    last_stats_.model_party_computation_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::stringstream ss;

    for (const auto& diag : diagonals) {
        diag.save(ss);
    }

    last_stats_.communication_download_mb = static_cast<double>(ss.tellp()) / (1024 * 1024);

    return diagonals;
}

Matrix HEMatrixMultiplier::decrypt_result(const std::vector<seal::Ciphertext>& encrypted_diagonals) {
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::vector<double>> decrypted_diagonals_vectors(M);

    parallelFor(0, M, [this, &encrypted_diagonals, &decrypted_diagonals_vectors](int i) {
        seal::Plaintext plain;
        decryptor_->decrypt(encrypted_diagonals[i], plain);
        encoder_->decode(plain, decrypted_diagonals_vectors[i]);
    });

    Matrix result(M, std::vector<double>(M, 0.0));

    for (size_t i = 0; i < M; ++i) {
        for (size_t j = 0; j < M; ++j) {
            int d = (j - i + M) % M;
            result[i][j] = decrypted_diagonals_vectors[d][i];
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();

    last_stats_.data_party_decryption_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return result;
}

HEMatrixMultiplier::PerformanceStats HEMatrixMultiplier::getLastPerformanceStats() const {
    return last_stats_;
}

void HEMatrixMultiplier::validateMatrixDimensions_A(const Matrix& A) const {
    if (A.empty() || A[0].empty()) {
        throw HEMatrixMultiplierException("Input matrix must not be empty.");
    }

    if (A.size() != M || A[0].size() != N) {
        throw HEMatrixMultiplierException(
            "Matrix A must have dimensions " + std::to_string(M) + "x" + std::to_string(N) + "."
        );
    }
}

void HEMatrixMultiplier::validateMatrixDimensions_B(const Matrix& B) const {
    if (B.empty() || B[0].empty()) {
        throw HEMatrixMultiplierException("Input matrix must not be empty.");
    }

    if (B.size() != N || B[0].size() != M) {
        throw HEMatrixMultiplierException(
            "Matrix B must have dimensions " + std::to_string(N) + "x" + std::to_string(M) + "."
        );
    }
}

std::vector<double> HEMatrixMultiplier::packMatrixByColumns(const Matrix& matrix) {
    std::vector<double> packed(SLOT_COUNT, 0.0);

    for (size_t k = 0; k < N; ++k) {
        for (size_t i = 0; i < M; ++i) {
            packed[k * M + i] = matrix[i][k];
        }
    }

    return packed;
}

std::vector<double> HEMatrixMultiplier::packAndShiftMatrixB(const Matrix& matrix, int shift) {
    std::vector<double> packed(SLOT_COUNT, 0.0);

    for (size_t k = 0; k < N; ++k) {
        for (size_t j = 0; j < M; ++j) {
            packed[k * M + j] = matrix[k][(j + shift) % M];
        }
    }

    return packed;
}

seal::Ciphertext HEMatrixMultiplier::sumBlocks(const seal::Ciphertext& ciphertext) {
    seal::Ciphertext running_sum = ciphertext;

    for (size_t j = 1; j < BSGS_BABY_STEP; ++j) {
        seal::Ciphertext temp;
        evaluator_->rotate_vector(ciphertext, j * M, *galois_keys_, temp);
        evaluator_->add_inplace(running_sum, temp);
    }

    seal::Ciphertext giant_sum = running_sum;

    for (size_t i = 1; i < BSGS_GIANT_STEP; ++i) {
        seal::Ciphertext temp;
        evaluator_->rotate_vector(running_sum, i * BSGS_BABY_STEP * M, *galois_keys_, temp);
        evaluator_->add_inplace(giant_sum, temp);
    }

    reinterpret_cast<std::atomic_size_t*>(&last_stats_.rotation_count)->fetch_add(
        (BSGS_BABY_STEP - 1) + (BSGS_GIANT_STEP - 1),
        std::memory_order_relaxed
    );

    return giant_sum;
}

void HEMatrixMultiplier::parallelFor(int start, int end, std::function<void(int)> func) {
    int range = end - start;

    if (range <= 0) {
        return;
    }

    int num_threads = std::min(thread_count_, range);

    if (num_threads <= 1) {
        for (int i = start; i < end; ++i) {
            func(i);
        }
        return;
    }

    std::vector<std::future<void>> futures;

    for (int i = 0; i < num_threads; ++i) {
        futures.push_back(std::async(std::launch::async, [=, &func] {
            for (int j = start + i; j < end; j += num_threads) {
                func(j);
            }
        }));
    }

    for (auto& f : futures) {
        f.get();
    }
}

void HEMatrixMultiplier::printPerformanceStats() const {
    std::lock_guard<std::mutex> lock(sync_mutex_);

    std::cout << "\n--- Single Online Computation Performance Report ---\n"
              << "[Data Party] Encryption Time: " << last_stats_.data_party_encryption_time_ms << " ms\n"
              << "[Communication] Upload Size: " << last_stats_.communication_upload_mb << " MB\n"
              << "-------------------------------------\n"
              << "[Model Party] Online Computation Time: " << last_stats_.model_party_computation_time_ms << " ms\n"
              << "              Rotation Count: " << last_stats_.rotation_count << "\n"
              << "-------------------------------------\n"
              << "[Communication] Download Size: " << last_stats_.communication_download_mb << " MB\n"
              << "[Data Party] Decryption and Reconstruction Time: "
              << last_stats_.data_party_decryption_time_ms << " ms\n"
              << "--- Report End ---\n";
}

void HEMatrixMultiplier::logInfo(const std::string& message) const {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    std::cout << "[INFO] " << message << std::endl;
}

HEMatrixMultiplierException::HEMatrixMultiplierException(const std::string& msg)
    : std::runtime_error(msg) {}
