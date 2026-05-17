#ifndef HE_MATRIX_MULTIPLIER_H
#define HE_MATRIX_MULTIPLIER_H

#include "seal/seal.h"
#include <vector>
#include <memory>
#include <mutex>
#include <future>
#include <chrono>
#include <functional>
#include <utility>
#include <string>
#include <stdexcept>

using Matrix = std::vector<std::vector<double>>;

class HEMatrixMultiplier {
public:
    HEMatrixMultiplier(size_t poly_modulus_degree = 16384, int thread_count = 8);
    ~HEMatrixMultiplier();

    seal::Ciphertext encrypt_data(const Matrix& A);

    std::vector<seal::Ciphertext> precompute_encrypted_B_variants(const Matrix& B_secret);

    std::vector<seal::Ciphertext> compute_on_server_encrypted(
        const seal::Ciphertext& ct_A,
        const std::vector<seal::Ciphertext>& precomputed_encrypted_B_variants
    );

    Matrix decrypt_result(const std::vector<seal::Ciphertext>& encrypted_diagonals);

    struct PerformanceStats {
        double data_party_encryption_time_ms = 0.0;
        double model_party_encryption_time_ms = 0.0;
        double model_party_computation_time_ms = 0.0;
        double data_party_decryption_time_ms = 0.0;
        double communication_upload_mb = 0.0;
        double communication_download_mb = 0.0;
        size_t rotation_count = 0;
    };

    PerformanceStats getLastPerformanceStats() const;
    void printPerformanceStats() const;

private:
    std::unique_ptr<seal::SEALContext> context_;
    std::unique_ptr<seal::KeyGenerator> keygen_;
    std::unique_ptr<seal::Encryptor> encryptor_;
    std::unique_ptr<seal::Decryptor> decryptor_;
    std::unique_ptr<seal::Evaluator> evaluator_;
    std::unique_ptr<seal::CKKSEncoder> encoder_;
    std::unique_ptr<seal::RelinKeys> relin_keys_;
    std::unique_ptr<seal::GaloisKeys> galois_keys_;
    std::unique_ptr<seal::PublicKey> public_key_;

    static constexpr size_t M = 64;
    static constexpr size_t N = 128;
    static constexpr size_t SLOT_COUNT = M * N;
    static constexpr size_t BSGS_BABY_STEP = 16;
    static constexpr size_t BSGS_GIANT_STEP = N / BSGS_BABY_STEP;

    int thread_count_;
    mutable std::mutex sync_mutex_;
    mutable PerformanceStats last_stats_;

    void initializeSEAL(size_t poly_modulus_degree);
    void generateKeys();

    std::vector<double> packMatrixByColumns(const Matrix& matrix);
    std::vector<double> packAndShiftMatrixB(const Matrix& matrix, int shift);
    seal::Ciphertext sumBlocks(const seal::Ciphertext& ciphertext);

    void parallelFor(int start, int end, std::function<void(int)> func);
    void validateMatrixDimensions_A(const Matrix& A) const;
    void validateMatrixDimensions_B(const Matrix& B) const;
    void logInfo(const std::string& message) const;
};

class HEMatrixMultiplierException : public std::runtime_error {
public:
    explicit HEMatrixMultiplierException(const std::string& msg);
};

#endif
