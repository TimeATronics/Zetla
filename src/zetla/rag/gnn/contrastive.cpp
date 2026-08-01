#include "contrastive.hpp"
#include "../core/lorentz.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

namespace hgnfs::training {

std::vector<float> train_projection(
    const float* E, const int* labels, int N,
    int euc_dim, int hyp_dim,
    float* W,
    const ContrastiveConfig& cfg) {

    std::vector<float> history;
    if (N < 2) return history;

    // Adam state for W
    std::vector<float> m(hyp_dim * euc_dim, 0.0f);
    std::vector<float> v(hyp_dim * euc_dim, 0.0f);
    float beta1 = 0.9f, beta2 = 0.999f;
    int t_step = 0;

    // Temporary buffers
    std::vector<float> Z_batch(cfg.batch_size * hyp_dim);
    std::vector<float> X_batch(cfg.batch_size * (hyp_dim + 1));
    std::vector<float> scores(cfg.batch_size * cfg.batch_size);

    std::mt19937 rng(42);

    for (int epoch = 0; epoch < cfg.epochs; ++epoch) {
        float total_loss = 0.0f;
        int batches = 0;

        // Shuffle indices
        std::vector<int> idx(N);
        for (int i = 0; i < N; ++i) idx[i] = i;
        std::shuffle(idx.begin(), idx.end(), rng);

        for (int start = 0; start < N; start += cfg.batch_size) {
            int batch_n = std::min(cfg.batch_size, N - start);
            if (batch_n < 2) continue;

            // Project: Z[i] = W @ E[idx[start+i]]  (batch_n × hyp_dim)
            for (int i = 0; i < batch_n; ++i) {
                const float* e = E + idx[start + i] * euc_dim;
                float* z = Z_batch.data() + i * hyp_dim;
                for (int j = 0; j < hyp_dim; ++j) {
                    float val = 0.0f;
                    for (int k = 0; k < euc_dim; ++k)
                        val += W[j * euc_dim + k] * e[k];
                    z[j] = val;
                }
            }

            // Clamp norms, project to Lorentz
            for (int i = 0; i < batch_n; ++i) {
                float* z = Z_batch.data() + i * hyp_dim;
                float n2 = 0.0f;
                for (int j = 0; j < hyp_dim; ++j) n2 += z[j] * z[j];
                float n = std::sqrt(n2);
                if (n > NORM_CLAMP) {
                    float s = NORM_CLAMP / n;
                    for (int j = 0; j < hyp_dim; ++j) z[j] *= s;
                }
            }
            lorentz::batch_exp_o(Z_batch.data(), batch_n, hyp_dim, X_batch.data());

            // Lorentz inner product pairwise: scores[a][b] = ⟨x_a, x_b⟩
            // Negate time of each row, then matrix multiply
            for (int a = 0; a < batch_n; ++a) {
                float* xa = X_batch.data() + a * (hyp_dim + 1);
                for (int b = 0; b < batch_n; ++b) {
                    float* xb = X_batch.data() + b * (hyp_dim + 1);
                    float s = -xa[0] * xb[0];
                    for (int j = 0; j < hyp_dim; ++j)
                        s += xa[1 + j] * xb[1 + j];
                    scores[a * batch_n + b] = s;
                }
            }

            // InfoNCE loss + gradient
            float batch_loss = 0.0f;
            // Zero gradient accumulator
            std::vector<float> grad_W(hyp_dim * euc_dim, 0.0f);

            for (int a = 0; a < batch_n; ++a) {
                // Positive pairs: all chunks with same label as a
                float pos_sum = 0.0f;
                float neg_sum = 0.0f;
                int n_pos = 0;

                // Log-sum-exp denominator (numerically stable)
                float max_score = -1e20f;
                for (int b = 0; b < batch_n; ++b)
                    max_score = std::max(max_score, scores[a * batch_n + b] / cfg.temperature);

                float denom = 0.0f;
                for (int b = 0; b < batch_n; ++b)
                    denom += std::exp(scores[a * batch_n + b] / cfg.temperature - max_score);

                int a_label = labels[idx[start + a]];

                for (int b = 0; b < batch_n; ++b) {
                    int b_label = labels[idx[start + b]];
                    if (a == b) continue;  // skip self
                    float sim = scores[a * batch_n + b];
                    float prob = std::exp(sim / cfg.temperature - max_score) / denom;

                    if (a_label == b_label) {
                        // Positive: gradient toward higher sim
                        n_pos++;
                        pos_sum += (1.0f - prob);
                    } else {
                        // Negative: gradient toward lower sim
                        neg_sum += prob;
                    }

                    // Gradient w.r.t. z_a: ∂sim/∂z_a[j] impacts via ∂W/∂z gradient chain
                    // Simplified: accumulate outer product of E[a] and gradient direction
                    float grad_factor = (a_label == b_label) ? (prob - 1.0f) / batch_n
                                                              : prob / batch_n;
                    // Accumulate into grad_W
                    const float* ea = E + idx[start + a] * euc_dim;
                    for (int j = 0; j < hyp_dim; ++j) {
                        float dz = Z_batch[a * hyp_dim + j];
                        // z_j = Σ_k W[j][k] * e[k]  ->  ∂z_j/∂W[j][k] = e[k]
                        float g = grad_factor / cfg.temperature;
                        for (int k = 0; k < euc_dim; ++k)
                            grad_W[j * euc_dim + k] += g * ea[k];
                    }
                }
                if (n_pos > 0) batch_loss += -std::log(pos_sum / std::max(n_pos, 1) + 1e-10f);
            }
            batch_loss /= batch_n;

            // Adam update
            t_step++;
            for (int i = 0; i < hyp_dim * euc_dim; ++i) {
                float g = grad_W[i] + cfg.weight_decay * W[i];
                m[i] = beta1 * m[i] + (1.0f - beta1) * g;
                v[i] = beta2 * v[i] + (1.0f - beta2) * g * g;
                float m_hat = m[i] / (1.0f - std::pow(beta1, t_step));
                float v_hat = v[i] / (1.0f - std::pow(beta2, t_step));
                W[i] -= cfg.lr * m_hat / (std::sqrt(v_hat) + 1e-8f);
            }

            total_loss += batch_loss;
            batches++;
        }

        float avg_loss = total_loss / batches;
        history.push_back(avg_loss);
    }

    return history;
}

}  // namespace hgnfs::training
