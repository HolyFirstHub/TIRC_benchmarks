#include "HEMatrixMultiplier.h"


using Matrix = std::vector<std::vector<double>>;

class CoutSilencer {
private:
    std::streambuf* old_buf;
    std::ostringstream null_stream;

public:
    CoutSilencer() {
        old_buf = std::cout.rdbuf(null_stream.rdbuf());
    }

    ~CoutSilencer() {
        std::cout.rdbuf(old_buf);
    }
};

Matrix generateRandomMatrix(size_t rows, size_t cols, double min_val = -1.0, double max_val = 1.0) {
    Matrix matrix(rows, std::vector<double>(cols));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min_val, max_val);

    for (size_t i = 0; i < rows; ++i)
        for (size_t j = 0; j < cols; ++j)
            matrix[i][j] = dis(gen);

    return matrix;
}

Matrix traditionalMatrixMultiply(const Matrix& A, const Matrix& B) {
    if (A.empty() || B.empty() || A[0].size() != B.size()) {
        throw std::runtime_error("Invalid matrix dimensions for multiplication.");
    }

    size_t m = A.size(), n = A[0].size(), p = B[0].size();
    Matrix C(m, std::vector<double>(p, 0.0));

    for (size_t i = 0; i < m; ++i)
        for (size_t j = 0; j < p; ++j)
            for (size_t k = 0; k < n; ++k)
                C[i][j] += A[i][k] * B[k][j];

    return C;
}

double calculateMatrixError(const Matrix& A, const Matrix& B) {
    if (A.size() != B.size() || A.empty() || B.empty() || A[0].size() != B[0].size())
        return -1.0;

    double max_error = 0.0;

    for (size_t i = 0; i < A.size(); ++i)
        for (size_t j = 0; j < A[0].size(); ++j)
            max_error = std::max(max_error, std::abs(A[i][j] - B[i][j]));

    return max_error;
}

Matrix extract_block(const Matrix& large_matrix, size_t start_row, size_t start_col, size_t rows, size_t cols) {
    Matrix block(rows, std::vector<double>(cols));

    for (size_t i = 0; i < rows; ++i)
        for (size_t j = 0; j < cols; ++j)
            block[i][j] = large_matrix[start_row + i][start_col + j];

    return block;
}

void add_block_inplace(Matrix& target_matrix, const Matrix& block, size_t start_row, size_t start_col) {
    for (size_t i = 0; i < block.size(); ++i)
        for (size_t j = 0; j < block[0].size(); ++j)
            target_matrix[start_row + i][start_col + j] += block[i][j];
}

void run_blocked_multiplication_test() {
    try {
        const size_t M_kernel = 64, N_kernel = 128;
        const size_t M_large = 128, K_large = 768, N_large = 64;
        const size_t row_blocks_A = M_large / M_kernel;
        const size_t col_blocks_A = K_large / N_kernel;
        const size_t col_blocks_B = N_large / M_kernel;

        Matrix C_he_large;
        Matrix C_traditional;

        double total_online_duration_ms = 0.0;
        double total_upload_mb = 0.0;
        double max_error = 0.0;

        {
            CoutSilencer silencer;

            HEMatrixMultiplier he_system(16384, 8);

            Matrix A_large_secret = generateRandomMatrix(M_large, K_large);
            Matrix B_large_plain = generateRandomMatrix(K_large, N_large);

            C_traditional = traditionalMatrixMultiply(A_large_secret, B_large_plain);

            std::vector<std::vector<seal::Plaintext>> precomputed_B_blocks(col_blocks_A);
            for (size_t j = 0; j < col_blocks_A; ++j) {
                for (size_t k = 0; k < col_blocks_B; ++k) {
                    Matrix B_block_plain = extract_block(
                        B_large_plain,
                        j * N_kernel,
                        k * M_kernel,
                        N_kernel,
                        M_kernel
                    );
                    precomputed_B_blocks[j] = he_system.precompute_B_variants(B_block_plain);
                }
            }

            auto total_start_time = std::chrono::high_resolution_clock::now();

            C_he_large = Matrix(M_large, std::vector<double>(N_large, 0.0));

            for (size_t i = 0; i < row_blocks_A; ++i) {
                for (size_t k = 0; k < col_blocks_B; ++k) {
                    for (size_t j = 0; j < col_blocks_A; ++j) {
                        Matrix A_block_secret = extract_block(
                            A_large_secret,
                            i * M_kernel,
                            j * N_kernel,
                            M_kernel,
                            N_kernel
                        );

                        seal::Ciphertext encrypted_A_block = he_system.encrypt_data(A_block_secret);

                        const auto& precomputed_B = precomputed_B_blocks[j];
                        std::vector<seal::Ciphertext> encrypted_result =
                            he_system.compute_on_server(encrypted_A_block, precomputed_B);

                        Matrix partial_result = he_system.decrypt_result(encrypted_result);
                        add_block_inplace(C_he_large, partial_result, i * M_kernel, k * M_kernel);

                        auto stats = he_system.getLastPerformanceStats();
                        total_upload_mb += stats.communication_upload_mb;
                    }
                }
            }

            auto total_end_time = std::chrono::high_resolution_clock::now();
            total_online_duration_ms =
                std::chrono::duration<double, std::milli>(total_end_time - total_start_time).count();

            max_error = calculateMatrixError(C_he_large, C_traditional);
        }

        std::cout << std::fixed << std::setprecision(4);

        std::cout << "=== Online Time ===\n";
        std::cout << "Online End-to-End Time: " << total_online_duration_ms << " ms\n";

        std::cout << "\n=== Communication ===\n";
        std::cout << "Total Communication: " << total_upload_mb << " MB\n";

        std::cout << "\n=== Error Analysis ===\n";
        std::cout << "Maximum Error: " << std::setprecision(8) << max_error << "\n";
        std::cout << "Verification Result: " << (max_error < 1e-2 ? "Passed" : "Failed") << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Runtime Error: " << e.what() << "\n";
    }
}

int main() {
    run_blocked_multiplication_test();
    return 0;
}
