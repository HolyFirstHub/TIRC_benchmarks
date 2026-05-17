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

Matrix extract_block(const Matrix& large_matrix, size_t start_row, size_t start_col,
                     size_t rows, size_t cols) {
    Matrix block(rows, std::vector<double>(cols));

    for (size_t i = 0; i < rows; ++i)
        for (size_t j = 0; j < cols; ++j)
            block[i][j] = large_matrix[start_row + i][start_col + j];

    return block;
}

void add_block_inplace(Matrix& target_matrix, const Matrix& block,
                       size_t start_row, size_t start_col) {
    for (size_t i = 0; i < block.size(); ++i)
        for (size_t j = 0; j < block[0].size(); ++j)
            target_matrix[start_row + i][start_col + j] += block[i][j];
}

int main() {
    try {
        const size_t M_large = 128, K_large = 64, N_large = 128;

        double online_time_ms = 0.0;
        double max_error = 0.0;

        {
            CoutSilencer silencer;

            HEMatrixMultiplier he_system(16384, 8);

            Matrix A_large = generateRandomMatrix(M_large, K_large);
            Matrix B_large = generateRandomMatrix(K_large, N_large);
            Matrix C_ref = traditionalMatrixMultiply(A_large, B_large);
            Matrix C_he(M_large, std::vector<double>(N_large, 0.0));

            auto start = std::chrono::high_resolution_clock::now();

            for (size_t bi = 0; bi < 2; ++bi) {
                for (size_t bj = 0; bj < 2; ++bj) {
                    Matrix A_sub = extract_block(A_large, bi * 64, 0, 64, 64);
                    Matrix B_sub = extract_block(B_large, 0, bj * 64, 64, 64);

                    Matrix A_pad(64, std::vector<double>(128, 0.0));
                    for (size_t r = 0; r < 64; ++r)
                        for (size_t c = 0; c < 64; ++c)
                            A_pad[r][c] = A_sub[r][c];

                    Matrix B_pad(128, std::vector<double>(64, 0.0));
                    for (size_t r = 0; r < 64; ++r)
                        for (size_t c = 0; c < 64; ++c)
                            B_pad[r][c] = B_sub[r][c];

                    auto encA = he_system.encrypt_data(A_pad);
                    auto encB_variants = he_system.precompute_encrypted_B_variants(B_pad);
                    auto encC = he_system.compute_on_server_encrypted(encA, encB_variants);
                    Matrix C_block = he_system.decrypt_result(encC);

                    add_block_inplace(C_he, C_block, bi * 64, bj * 64);
                }
            }

            auto end = std::chrono::high_resolution_clock::now();
            online_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

            max_error = calculateMatrixError(C_ref, C_he);
        }

        std::cout << std::fixed << std::setprecision(4);

        std::cout << "=== QK: 128x64 Matrix by 64x128 Matrix ===\n";

        std::cout << "\n=== Online Time ===\n";
        std::cout << "Online End-to-End Time: " << online_time_ms << " ms\n";

        std::cout << "\n=== Error Analysis ===\n";
        std::cout << "Maximum Error: " << std::setprecision(8) << max_error << "\n";
        std::cout << "Verification Result: " << (max_error < 0.5 ? "Passed" : "Failed") << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Runtime Error: " << e.what() << "\n";
    }

    return 0;
}
