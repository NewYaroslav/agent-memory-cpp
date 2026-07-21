#include "LearnedProjectionBinaryEncoder.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

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
        void append_integer(std::string& output, Integer value, int base = 10) {
            std::array<char, 32> buffer{};
            const auto result =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, base);
            if(result.ec != std::errc{}) {
                throw std::logic_error(
                    "failed to format learned projection encoder fingerprint"
                );
            }
            output.append(buffer.data(), result.ptr);
        }

        void append_hex64(std::string& output, std::uint64_t value) {
            std::array<char, 16> digits{};
            auto result = std::to_chars(
                digits.data(),
                digits.data() + digits.size(),
                value,
                16
            );
            if(result.ec != std::errc{}) {
                throw std::logic_error(
                    "failed to format learned projection artifact hash"
                );
            }
            const auto written = static_cast<std::size_t>(result.ptr - digits.data());
            output.append(16U - written, '0');
            output.append(digits.data(), result.ptr);
        }

        [[nodiscard]] std::uint64_t hash_float(
            std::uint64_t hash,
            float value
        ) noexcept {
            static_assert(sizeof(float) == sizeof(std::uint32_t), "unexpected float size");
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            hash ^= bits;
            hash *= 1099511628211ULL;
            return hash;
        }

        [[nodiscard]] std::uint64_t artifact_hash(
            const LearnedProjectionBinaryEncoderOptions& options
        ) noexcept {
            auto hash = 1469598103934665603ULL;
            hash ^= static_cast<std::uint64_t>(options.input_dimension);
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint64_t>(options.bit_count);
            hash *= 1099511628211ULL;
            hash ^= options.seed;
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint64_t>(options.training_vector_count);
            hash *= 1099511628211ULL;
            for(const auto value : options.projection_rows) {
                hash = hash_float(hash, value);
            }
            for(const auto value : options.thresholds) {
                hash = hash_float(hash, value);
            }
            return hash;
        }

        [[nodiscard]] std::string make_config_fingerprint(
            const LearnedProjectionBinaryEncoderOptions& options
        ) {
            std::string output = "learned_pair_difference_projection_v1:dim=";
            append_integer(output, options.input_dimension);
            output += ":bits=";
            append_integer(output, options.bit_count);
            output += ":seed=";
            append_integer(output, options.seed);
            output += ":train=";
            append_integer(output, options.training_vector_count);
            output += ":artifact=";
            append_hex64(output, artifact_hash(options));
            return output;
        }

        [[nodiscard]] std::size_t checked_projection_value_count(
            std::size_t input_dimension,
            std::size_t bit_count
        ) {
            if(input_dimension == 0 || bit_count == 0) {
                return 0;
            }
            if(input_dimension > std::numeric_limits<std::size_t>::max() / bit_count) {
                throw std::length_error("learned projection matrix size overflows size_t");
            }
            return input_dimension * bit_count;
        }

        void validate_training_input(
            const std::vector<Embedding>& training_vectors,
            const LearnedProjectionTrainingOptions& options
        ) {
            if(options.input_dimension == 0) {
                throw std::invalid_argument(
                    "learned projection training input dimension must be positive"
                );
            }
            if(options.bit_count == 0) {
                throw std::invalid_argument(
                    "learned projection training bit count must be positive"
                );
            }
            if(options.max_training_vectors == 1) {
                throw std::invalid_argument(
                    "learned projection training requires max_training_vectors to be zero or at least two"
                );
            }
            if(training_vectors.size() < 2) {
                throw std::invalid_argument(
                    "learned projection training requires at least two vectors"
                );
            }
            for(const auto& vector : training_vectors) {
                if(vector.dimension() != options.input_dimension) {
                    throw std::invalid_argument(
                        "learned projection training vector dimension mismatch"
                    );
                }
                for(const auto value : vector.values) {
                    if(!std::isfinite(value)) {
                        throw std::invalid_argument(
                            "learned projection training input must be finite"
                        );
                    }
                }
            }
        }

        void validate_artifact(const LearnedProjectionBinaryEncoderOptions& options) {
            if(options.input_dimension == 0) {
                throw std::invalid_argument(
                    "learned projection encoder input dimension must be positive"
                );
            }
            if(options.bit_count == 0) {
                throw std::invalid_argument(
                    "learned projection encoder bit count must be positive"
                );
            }
            if(options.training_vector_count < 2) {
                throw std::invalid_argument(
                    "learned projection encoder requires at least two training vectors"
                );
            }
            if(options.projection_rows.size() !=
               checked_projection_value_count(options.input_dimension, options.bit_count)) {
                throw std::invalid_argument(
                    "learned projection encoder projection matrix size mismatch"
                );
            }
            if(options.thresholds.size() != options.bit_count) {
                throw std::invalid_argument(
                    "learned projection encoder threshold count mismatch"
                );
            }
            for(const auto value : options.projection_rows) {
                if(!std::isfinite(value)) {
                    throw std::invalid_argument(
                        "learned projection encoder projection values must be finite"
                    );
                }
            }
            for(const auto value : options.thresholds) {
                if(!std::isfinite(value)) {
                    throw std::invalid_argument(
                        "learned projection encoder thresholds must be finite"
                    );
                }
            }
        }

        [[nodiscard]] std::vector<const Embedding*> sample_training_vectors(
            const std::vector<Embedding>& training_vectors,
            const LearnedProjectionTrainingOptions& options
        ) {
            const auto sample_limit =
                options.max_training_vectors == 0
                    ? training_vectors.size()
                    : std::min(options.max_training_vectors, training_vectors.size());
            std::vector<const Embedding*> sampled;
            sampled.reserve(sample_limit);
            if(sample_limit == training_vectors.size()) {
                for(const auto& vector : training_vectors) {
                    sampled.push_back(&vector);
                }
                return sampled;
            }

            std::vector<std::size_t> ordinals(training_vectors.size());
            for(std::size_t index = 0; index < ordinals.size(); ++index) {
                ordinals[index] = index;
            }
            std::sort(ordinals.begin(), ordinals.end(), [&](auto lhs, auto rhs) {
                const auto lhs_key =
                    mix64(options.seed ^ (static_cast<std::uint64_t>(lhs) + 0x51ed2705ULL));
                const auto rhs_key =
                    mix64(options.seed ^ (static_cast<std::uint64_t>(rhs) + 0x51ed2705ULL));
                if(lhs_key != rhs_key) {
                    return lhs_key < rhs_key;
                }
                return lhs < rhs;
            });

            for(std::size_t index = 0; index < sample_limit; ++index) {
                sampled.push_back(&training_vectors[ordinals[index]]);
            }
            return sampled;
        }

        [[nodiscard]] std::size_t deterministic_index(
            std::uint64_t seed,
            std::size_t bit,
            std::size_t candidate,
            std::size_t size
        ) noexcept {
            auto value = seed;
            value ^= static_cast<std::uint64_t>(bit) + 0x9e3779b97f4a7c15ULL;
            value = mix64(value);
            value ^= static_cast<std::uint64_t>(candidate) + 0xbf58476d1ce4e5b9ULL;
            value = mix64(value);
            return static_cast<std::size_t>(value % size);
        }

        [[nodiscard]] double squared_distance(
            const Embedding& lhs,
            const Embedding& rhs
        ) noexcept {
            double sum = 0.0;
            for(std::size_t index = 0; index < lhs.values.size(); ++index) {
                const auto delta =
                    static_cast<double>(lhs.values[index]) - static_cast<double>(rhs.values[index]);
                sum += delta * delta;
            }
            return sum;
        }

        [[nodiscard]] float fallback_sign(
            std::uint64_t seed,
            std::size_t bit,
            std::size_t dimension
        ) noexcept {
            auto value = seed;
            value ^= static_cast<std::uint64_t>(bit) + 0xd6e8feb86659fd93ULL;
            value = mix64(value);
            value ^= static_cast<std::uint64_t>(dimension) + 0xa5a3564e27f8862bULL;
            value = mix64(value);
            return (value & std::uint64_t{1}) != 0 ? 1.0F : -1.0F;
        }

        void learn_direction(
            const std::vector<const Embedding*>& sampled,
            const LearnedProjectionTrainingOptions& options,
            std::size_t bit,
            float* row
        ) {
            constexpr std::size_t candidate_count = 16;
            const auto anchor_index =
                deterministic_index(options.seed, bit, 0, sampled.size());
            const auto& anchor = *sampled[anchor_index];
            std::size_t best_index = anchor_index;
            double best_distance = -1.0;
            for(std::size_t candidate = 1; candidate <= candidate_count; ++candidate) {
                auto index = deterministic_index(options.seed, bit, candidate, sampled.size());
                if(index == anchor_index) {
                    index = (index + 1U) % sampled.size();
                }
                const auto distance = squared_distance(anchor, *sampled[index]);
                if(distance > best_distance) {
                    best_distance = distance;
                    best_index = index;
                }
            }

            double norm_squared = 0.0;
            const auto& other = *sampled[best_index];
            for(std::size_t dimension = 0; dimension < options.input_dimension; ++dimension) {
                const auto value = static_cast<double>(anchor.values[dimension])
                    - static_cast<double>(other.values[dimension]);
                row[dimension] = static_cast<float>(value);
                norm_squared += value * value;
            }

            if(norm_squared <= 0.0) {
                const auto scale = 1.0F / std::sqrt(static_cast<float>(options.input_dimension));
                for(std::size_t dimension = 0; dimension < options.input_dimension; ++dimension) {
                    row[dimension] = fallback_sign(options.seed, bit, dimension) * scale;
                }
                return;
            }

            const auto inv_norm = 1.0 / std::sqrt(norm_squared);
            for(std::size_t dimension = 0; dimension < options.input_dimension; ++dimension) {
                row[dimension] = static_cast<float>(
                    static_cast<double>(row[dimension]) * inv_norm
                );
            }
        }

        [[nodiscard]] float dot_product(const float* row, const Embedding& vector) noexcept {
            double dot = 0.0;
            for(std::size_t dimension = 0; dimension < vector.values.size(); ++dimension) {
                dot += static_cast<double>(row[dimension])
                    * static_cast<double>(vector.values[dimension]);
            }
            return static_cast<float>(dot);
        }

        [[nodiscard]] float lower_median(std::vector<float>& values) {
            if(values.empty()) {
                throw std::logic_error("cannot compute median of empty values");
            }
            const auto middle = (values.size() - 1U) / 2U;
            std::nth_element(values.begin(), values.begin() + middle, values.end());
            return values[middle];
        }

    } // namespace

    LearnedProjectionBinaryEncoderOptions train_learned_projection_encoder(
        const std::vector<Embedding>& training_vectors,
        LearnedProjectionTrainingOptions options
    ) {
        validate_training_input(training_vectors, options);
        const auto sampled = sample_training_vectors(training_vectors, options);
        if(sampled.size() < 2) {
            throw std::invalid_argument(
                "learned projection training sample must contain at least two vectors"
            );
        }

        LearnedProjectionBinaryEncoderOptions artifact;
        artifact.input_dimension = options.input_dimension;
        artifact.bit_count = options.bit_count;
        artifact.seed = options.seed;
        artifact.training_vector_count = sampled.size();
        artifact.projection_rows.resize(
            checked_projection_value_count(options.input_dimension, options.bit_count)
        );
        artifact.thresholds.resize(options.bit_count);

        std::vector<float> projections;
        projections.reserve(sampled.size());
        for(std::size_t bit = 0; bit < options.bit_count; ++bit) {
            auto* row = artifact.projection_rows.data() + bit * options.input_dimension;
            learn_direction(sampled, options, bit, row);

            projections.clear();
            for(const auto* vector : sampled) {
                projections.push_back(dot_product(row, *vector));
            }
            artifact.thresholds[bit] = lower_median(projections);
        }
        return artifact;
    }

    LearnedProjectionBinaryEncoder::LearnedProjectionBinaryEncoder(
        LearnedProjectionBinaryEncoderOptions options
    )
        : m_options(std::move(options)) {
        validate_artifact(m_options);
        m_info.encoder_id = "learned_pair_difference_projection";
        m_info.encoder_version = "v1";
        m_info.input_dimension = m_options.input_dimension;
        m_info.bit_count = m_options.bit_count;
        m_info.seed = m_options.seed;
        m_info.config_fingerprint = make_config_fingerprint(m_options);
    }

    const BinarySignatureEncoderInfo& LearnedProjectionBinaryEncoder::info()
        const noexcept {
        return m_info;
    }

    BinarySignature LearnedProjectionBinaryEncoder::encode(
        const Embedding& vector
    ) const {
        validate_input(vector);
        return encode_validated(vector);
    }

    std::vector<BinarySignature> LearnedProjectionBinaryEncoder::encode_batch(
        const std::vector<Embedding>& vectors
    ) const {
        for(const auto& vector : vectors) {
            validate_input(vector);
        }
        std::vector<BinarySignature> signatures;
        signatures.reserve(vectors.size());
        for(const auto& vector : vectors) {
            signatures.push_back(encode_validated(vector));
        }
        return signatures;
    }

    VectorSimilarityBackend LearnedProjectionBinaryEncoder::similarity_backend()
        const noexcept {
        return m_similarity.backend();
    }

    const std::vector<float>& LearnedProjectionBinaryEncoder::projection_rows()
        const noexcept {
        return m_options.projection_rows;
    }

    const std::vector<float>& LearnedProjectionBinaryEncoder::thresholds()
        const noexcept {
        return m_options.thresholds;
    }

    void LearnedProjectionBinaryEncoder::validate_input(const Embedding& vector) const {
        if(vector.dimension() != m_options.input_dimension) {
            throw std::invalid_argument("learned projection encoder input dimension mismatch");
        }
        for(const auto value : vector.values) {
            if(!std::isfinite(value)) {
                throw std::invalid_argument(
                    "learned projection encoder input must be finite"
                );
            }
        }
    }

    BinarySignature LearnedProjectionBinaryEncoder::encode_validated(
        const Embedding& vector
    ) const {
        BinarySignature signature(m_options.bit_count);
        for(std::size_t bit = 0; bit < m_options.bit_count; ++bit) {
            const auto dot = m_similarity.dot_product_values(
                vector.values.data(),
                m_options.projection_rows.data() + bit * m_options.input_dimension,
                m_options.input_dimension
            );
            signature.set_bit(bit, dot > m_options.thresholds[bit]);
        }
        return signature;
    }

} // namespace agent_memory
