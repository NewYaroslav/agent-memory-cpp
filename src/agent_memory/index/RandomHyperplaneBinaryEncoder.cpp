#include "RandomHyperplaneBinaryEncoder.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace agent_memory {
    namespace {

        [[nodiscard]] std::uint64_t mix64(std::uint64_t value) noexcept {
            value ^= value >> 30U;
            value *= 0xbf58476d1ce4e5b9ULL;
            value ^= value >> 27U;
            value *= 0x94d049bb133111ebULL;
            value ^= value >> 31U;
            return value;
        }

        template <typename Integer>
        void append_integer(std::string& output, Integer value) {
            std::array<char, 32> buffer{};
            const auto result =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            if(result.ec != std::errc{}) {
                throw std::logic_error("failed to format binary encoder config fingerprint");
            }
            output.append(buffer.data(), result.ptr);
        }

        [[nodiscard]] std::string make_config_fingerprint(
            const RandomHyperplaneBinaryEncoderOptions& options
        ) {
            std::string output = "random_hyperplane_rademacher_v2:dim=";
            append_integer(output, options.input_dimension);
            output += ":bits=";
            append_integer(output, options.bit_count);
            output += ":seed=";
            append_integer(output, options.seed);
            return output;
        }

        [[nodiscard]] std::size_t checked_projection_value_count(
            const RandomHyperplaneBinaryEncoderOptions& options
        ) {
            if(options.input_dimension >
               std::numeric_limits<std::size_t>::max() / options.bit_count) {
                throw std::length_error("random hyperplane projection size overflows size_t");
            }
            return options.input_dimension * options.bit_count;
        }

        [[nodiscard]] float sparse_dot_product(
            const float* hyperplane,
            std::size_t dimension,
            const std::vector<SparseEmbeddingValue>& values
        ) noexcept {
            constexpr std::size_t lane_count = 8;
            const auto vectorized_dimension = dimension - dimension % lane_count;
            std::array<float, 8> lanes{};
            for(const auto& entry : values) {
                if(entry.index < vectorized_dimension) {
                    lanes[entry.index % lane_count] += entry.value * hyperplane[entry.index];
                }
            }
            float dot = 0.0F;
            for(std::size_t lane = 0; lane < lane_count; ++lane) {
                dot += lanes[lane];
            }
            for(const auto& entry : values) {
                if(entry.index >= vectorized_dimension) {
                    dot += entry.value * hyperplane[entry.index];
                }
            }
            return dot;
        }

    } // namespace

    RandomHyperplaneBinaryEncoder::RandomHyperplaneBinaryEncoder(
        RandomHyperplaneBinaryEncoderOptions options
    )
        : m_options(options) {
        if(m_options.input_dimension == 0) {
            throw std::invalid_argument("random hyperplane encoder input dimension must be positive");
        }
        if(m_options.bit_count == 0) {
            throw std::invalid_argument("random hyperplane encoder bit count must be positive");
        }

        m_info.encoder_id = "random_hyperplane_rademacher";
        m_info.encoder_version = "v2";
        m_info.input_dimension = m_options.input_dimension;
        m_info.bit_count = m_options.bit_count;
        m_info.seed = m_options.seed;
        m_info.config_fingerprint = make_config_fingerprint(m_options);
    }

    const BinarySignatureEncoderInfo& RandomHyperplaneBinaryEncoder::info() const noexcept {
        return m_info;
    }

    BinarySignature RandomHyperplaneBinaryEncoder::encode(const Embedding& vector) const {
        validate_dense_input(vector);
        ensure_hyperplanes();
        return encode_dense_validated(vector);
    }

    std::vector<BinarySignature> RandomHyperplaneBinaryEncoder::encode_batch(
        const std::vector<Embedding>& vectors
    ) const {
        for(const auto& vector : vectors) {
            validate_dense_input(vector);
        }
        if(vectors.empty()) {
            return {};
        }
        ensure_hyperplanes();

        std::vector<BinarySignature> signatures;
        signatures.reserve(vectors.size());
        for(const auto& vector : vectors) {
            signatures.push_back(encode_dense_validated(vector));
        }
        return signatures;
    }

    BinarySignature RandomHyperplaneBinaryEncoder::encode_sparse(
        std::size_t dimension,
        const std::vector<SparseEmbeddingValue>& values
    ) const {
        if(dimension != m_options.input_dimension) {
            throw std::invalid_argument("binary signature encoder sparse input dimension mismatch");
        }
        std::size_t previous_index = 0;
        bool has_previous = false;
        for(const auto& entry : values) {
            if(entry.index >= dimension) {
                throw std::invalid_argument("binary signature encoder sparse index is out of range");
            }
            if(has_previous && entry.index <= previous_index) {
                throw std::invalid_argument(
                    "binary signature encoder sparse indices must be unique and increasing"
                );
            }
            if(!std::isfinite(entry.value) || entry.value == 0.0F) {
                throw std::invalid_argument(
                    "binary signature encoder sparse values must be finite and non-zero"
                );
            }
            previous_index = entry.index;
            has_previous = true;
        }

        ensure_hyperplanes();
        BinarySignature signature(m_options.bit_count);
        for(std::size_t bit = 0; bit < m_options.bit_count; ++bit) {
            const auto* hyperplane =
                m_hyperplanes.data() + bit * m_options.input_dimension;
            const auto dot = sparse_dot_product(
                hyperplane,
                m_options.input_dimension,
                values
            );
            signature.set_bit(bit, dot > 0.0);
        }
        return signature;
    }

    VectorSimilarityBackend RandomHyperplaneBinaryEncoder::similarity_backend() const noexcept {
        return m_similarity.backend();
    }

    void RandomHyperplaneBinaryEncoder::ensure_hyperplanes() const {
        std::call_once(m_hyperplanes_once, [this] {
            m_hyperplanes.resize(checked_projection_value_count(m_options));
            for(std::size_t bit = 0; bit < m_options.bit_count; ++bit) {
                for(std::size_t dimension = 0; dimension < m_options.input_dimension;
                    ++dimension) {
                    m_hyperplanes[bit * m_options.input_dimension + dimension] =
                        hyperplane_sign(m_options.seed, bit, dimension) ? 1.0F : -1.0F;
                }
            }
        });
    }

    void RandomHyperplaneBinaryEncoder::validate_dense_input(const Embedding& vector) const {
        if(vector.dimension() != m_options.input_dimension) {
            throw std::invalid_argument("binary signature encoder input dimension mismatch");
        }

        for(const auto value : vector.values) {
            if(!std::isfinite(value)) {
                throw std::invalid_argument("binary signature encoder input must be finite");
            }
        }
    }

    BinarySignature RandomHyperplaneBinaryEncoder::encode_dense_validated(
        const Embedding& vector
    ) const {
        BinarySignature signature(m_options.bit_count);
        for(std::size_t bit = 0; bit < m_options.bit_count; ++bit) {
            const auto dot = m_similarity.dot_product_values(
                vector.values.data(),
                m_hyperplanes.data() + bit * m_options.input_dimension,
                m_options.input_dimension
            );
            signature.set_bit(bit, dot > 0.0);
        }
        return signature;
    }

    bool RandomHyperplaneBinaryEncoder::hyperplane_sign(
        std::uint64_t seed,
        std::size_t bit_index,
        std::size_t dimension_index
    ) noexcept {
        auto value = seed;
        value ^= (static_cast<std::uint64_t>(bit_index) + 0x9e3779b97f4a7c15ULL);
        value = mix64(value);
        value ^= (static_cast<std::uint64_t>(dimension_index) + 0xbf58476d1ce4e5b9ULL);
        value = mix64(value);
        return (value & std::uint64_t{1}) != 0;
    }

} // namespace agent_memory
