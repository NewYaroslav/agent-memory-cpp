#include "PcaProjectionBinaryEncoder.hpp"

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

        constexpr double kMinimumOrthogonalAxisNorm = 1.0e-6;

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
                    "failed to format PCA projection encoder fingerprint"
                );
            }
            output.append(buffer.data(), result.ptr);
        }

        void append_hex64(std::string& output, std::uint64_t value) {
            std::array<char, 16> digits{};
            const auto result = std::to_chars(
                digits.data(),
                digits.data() + digits.size(),
                value,
                16
            );
            if(result.ec != std::errc{}) {
                throw std::logic_error(
                    "failed to format PCA projection artifact hash"
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
            const PcaProjectionBinaryEncoderOptions& options
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
            hash ^= static_cast<std::uint64_t>(options.power_iterations);
            hash *= 1099511628211ULL;
            for(const auto value : options.mean) {
                hash = hash_float(hash, value);
            }
            for(const auto value : options.projection_rows) {
                hash = hash_float(hash, value);
            }
            for(const auto value : options.thresholds) {
                hash = hash_float(hash, value);
            }
            return hash;
        }

        [[nodiscard]] std::string make_config_fingerprint(
            const PcaProjectionBinaryEncoderOptions& options
        ) {
            std::string output = "pca_projection_v1:dim=";
            append_integer(output, options.input_dimension);
            output += ":bits=";
            append_integer(output, options.bit_count);
            output += ":seed=";
            append_integer(output, options.seed);
            output += ":train=";
            append_integer(output, options.training_vector_count);
            output += ":iters=";
            append_integer(output, options.power_iterations);
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
                throw std::length_error("PCA projection matrix size overflows size_t");
            }
            return input_dimension * bit_count;
        }

        void validate_training_input(
            const std::vector<Embedding>& training_vectors,
            const PcaProjectionTrainingOptions& options
        ) {
            if(options.input_dimension == 0) {
                throw std::invalid_argument(
                    "PCA projection training input dimension must be positive"
                );
            }
            if(options.bit_count == 0) {
                throw std::invalid_argument(
                    "PCA projection training bit count must be positive"
                );
            }
            if(options.bit_count > options.input_dimension) {
                throw std::invalid_argument(
                    "PCA projection bit count must not exceed input dimension"
                );
            }
            if(options.power_iterations == 0) {
                throw std::invalid_argument(
                    "PCA projection training power iterations must be positive"
                );
            }
            if(options.max_training_vectors == 1) {
                throw std::invalid_argument(
                    "PCA projection training requires max_training_vectors to be zero or at least two"
                );
            }
            if(training_vectors.size() < 2) {
                throw std::invalid_argument(
                    "PCA projection training requires at least two vectors"
                );
            }
            for(const auto& vector : training_vectors) {
                if(vector.dimension() != options.input_dimension) {
                    throw std::invalid_argument(
                        "PCA projection training vector dimension mismatch"
                    );
                }
                for(const auto value : vector.values) {
                    if(!std::isfinite(value)) {
                        throw std::invalid_argument(
                            "PCA projection training input must be finite"
                        );
                    }
                }
            }
        }

        void validate_artifact(const PcaProjectionBinaryEncoderOptions& options) {
            if(options.input_dimension == 0) {
                throw std::invalid_argument(
                    "PCA projection encoder input dimension must be positive"
                );
            }
            if(options.bit_count == 0) {
                throw std::invalid_argument(
                    "PCA projection encoder bit count must be positive"
                );
            }
            if(options.bit_count > options.input_dimension) {
                throw std::invalid_argument(
                    "PCA projection encoder bit count must not exceed input dimension"
                );
            }
            if(options.training_vector_count < 2) {
                throw std::invalid_argument(
                    "PCA projection encoder requires at least two training vectors"
                );
            }
            if(options.power_iterations == 0) {
                throw std::invalid_argument(
                    "PCA projection encoder power iterations must be positive"
                );
            }
            if(options.mean.size() != options.input_dimension) {
                throw std::invalid_argument(
                    "PCA projection encoder mean size mismatch"
                );
            }
            if(options.projection_rows.size() !=
               checked_projection_value_count(options.input_dimension, options.bit_count)) {
                throw std::invalid_argument(
                    "PCA projection encoder projection matrix size mismatch"
                );
            }
            if(options.thresholds.size() != options.bit_count) {
                throw std::invalid_argument(
                    "PCA projection encoder threshold count mismatch"
                );
            }
            for(const auto value : options.mean) {
                if(!std::isfinite(value)) {
                    throw std::invalid_argument(
                        "PCA projection encoder mean values must be finite"
                    );
                }
            }
            for(const auto value : options.projection_rows) {
                if(!std::isfinite(value)) {
                    throw std::invalid_argument(
                        "PCA projection encoder projection values must be finite"
                    );
                }
            }
            for(const auto value : options.thresholds) {
                if(!std::isfinite(value)) {
                    throw std::invalid_argument(
                        "PCA projection encoder thresholds must be finite"
                    );
                }
            }
        }

        [[nodiscard]] std::vector<const Embedding*> sample_training_vectors(
            const std::vector<Embedding>& training_vectors,
            const PcaProjectionTrainingOptions& options
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
                    mix64(options.seed ^ (static_cast<std::uint64_t>(lhs) + 0x6c8e9cf5ULL));
                const auto rhs_key =
                    mix64(options.seed ^ (static_cast<std::uint64_t>(rhs) + 0x6c8e9cf5ULL));
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

        [[nodiscard]] float deterministic_sign(
            std::uint64_t seed,
            std::size_t bit,
            std::size_t dimension
        ) noexcept {
            auto value = seed;
            value ^= static_cast<std::uint64_t>(bit) + 0xd1b54a32d192ed03ULL;
            value = mix64(value);
            value ^= static_cast<std::uint64_t>(dimension) + 0x94d049bb133111ebULL;
            value = mix64(value);
            return (value & std::uint64_t{1}) != 0 ? 1.0F : -1.0F;
        }

        [[nodiscard]] double dot(
            const std::vector<double>& lhs,
            const std::vector<double>& rhs
        ) noexcept {
            double value = 0.0;
            for(std::size_t index = 0; index < lhs.size(); ++index) {
                value += lhs[index] * rhs[index];
            }
            return value;
        }

        [[nodiscard]] double norm(std::vector<double>& values) noexcept {
            return std::sqrt(dot(values, values));
        }

        [[nodiscard]] bool normalize(std::vector<double>& values) noexcept {
            const auto value_norm = norm(values);
            if(value_norm <= 0.0 || !std::isfinite(value_norm)) {
                return false;
            }
            for(auto& value : values) {
                value /= value_norm;
            }
            return true;
        }

        [[nodiscard]] bool normalize_orthogonal_axis(
            std::vector<double>& values
        ) noexcept {
            const auto value_norm = norm(values);
            if(value_norm <= kMinimumOrthogonalAxisNorm
               || !std::isfinite(value_norm)) {
                return false;
            }
            for(auto& value : values) {
                value /= value_norm;
            }
            return true;
        }

        void multiply_covariance(
            const std::vector<double>& covariance,
            const std::vector<double>& vector,
            std::size_t dimension,
            std::vector<double>& output
        ) {
            std::fill(output.begin(), output.end(), 0.0);
            for(std::size_t row = 0; row < dimension; ++row) {
                double sum = 0.0;
                const auto row_offset = row * dimension;
                for(std::size_t column = 0; column < dimension; ++column) {
                    sum += covariance[row_offset + column] * vector[column];
                }
                output[row] = sum;
            }
        }

        void orthogonalize_against_previous_rows(
            const std::vector<float>& rows,
            std::size_t completed_rows,
            std::size_t dimension,
            std::vector<double>& vector
        ) {
            for(std::size_t row = 0; row < completed_rows; ++row) {
                const auto* previous = rows.data() + row * dimension;
                double projection = 0.0;
                for(std::size_t column = 0; column < dimension; ++column) {
                    projection += vector[column] * static_cast<double>(previous[column]);
                }
                for(std::size_t column = 0; column < dimension; ++column) {
                    vector[column] -= projection * static_cast<double>(previous[column]);
                }
            }
        }

        void reorthogonalize_against_previous_rows(
            const std::vector<float>& rows,
            std::size_t completed_rows,
            std::size_t dimension,
            std::vector<double>& vector
        ) {
            orthogonalize_against_previous_rows(rows, completed_rows, dimension, vector);
            orthogonalize_against_previous_rows(rows, completed_rows, dimension, vector);
        }

        [[nodiscard]] bool initialize_fallback_axis(
            const std::vector<float>& rows,
            std::size_t completed_rows,
            std::size_t dimension,
            std::size_t bit,
            std::vector<double>& vector
        ) {
            for(std::size_t offset = 0; offset < dimension; ++offset) {
                std::fill(vector.begin(), vector.end(), 0.0);
                vector[(bit + offset) % dimension] = 1.0;
                reorthogonalize_against_previous_rows(
                    rows,
                    completed_rows,
                    dimension,
                    vector
                );
                if(normalize_orthogonal_axis(vector)) {
                    return true;
                }
            }
            return false;
        }

        void initialize_axis(
            const PcaProjectionTrainingOptions& options,
            std::size_t bit,
            const std::vector<float>& rows,
            std::size_t completed_rows,
            std::vector<double>& vector
        ) {
            const auto scale =
                1.0 / std::sqrt(static_cast<double>(options.input_dimension));
            for(std::size_t dimension = 0; dimension < options.input_dimension; ++dimension) {
                vector[dimension] =
                    static_cast<double>(
                        deterministic_sign(options.seed, bit, dimension)
                    ) * scale;
            }
            reorthogonalize_against_previous_rows(
                rows,
                completed_rows,
                options.input_dimension,
                vector
            );
            if(normalize_orthogonal_axis(vector)) {
                return;
            }
            if(!initialize_fallback_axis(
                   rows,
                   completed_rows,
                   options.input_dimension,
                   bit,
                   vector
               )) {
                throw std::logic_error(
                    "failed to construct an orthogonal PCA fallback axis"
                );
            }
        }

        void deflate_covariance(
            std::vector<double>& covariance,
            const std::vector<double>& axis,
            double eigenvalue,
            std::size_t dimension
        ) {
            if(eigenvalue <= 0.0 || !std::isfinite(eigenvalue)) {
                return;
            }
            for(std::size_t row = 0; row < dimension; ++row) {
                const auto row_offset = row * dimension;
                for(std::size_t column = 0; column < dimension; ++column) {
                    covariance[row_offset + column] -=
                        eigenvalue * axis[row] * axis[column];
                }
            }
        }

        [[nodiscard]] std::vector<float> compute_mean(
            const std::vector<const Embedding*>& sampled,
            std::size_t dimension
        ) {
            std::vector<double> mean(dimension, 0.0);
            for(const auto* vector : sampled) {
                for(std::size_t index = 0; index < dimension; ++index) {
                    mean[index] += static_cast<double>(vector->values[index]);
                }
            }
            const auto inv_count = 1.0 / static_cast<double>(sampled.size());
            std::vector<float> result(dimension, 0.0F);
            for(std::size_t index = 0; index < dimension; ++index) {
                result[index] = static_cast<float>(mean[index] * inv_count);
            }
            return result;
        }

        [[nodiscard]] std::vector<double> compute_covariance(
            const std::vector<const Embedding*>& sampled,
            const std::vector<float>& mean,
            std::size_t dimension
        ) {
            if(dimension > std::numeric_limits<std::size_t>::max() / dimension) {
                throw std::length_error("PCA covariance matrix size overflows size_t");
            }
            std::vector<double> covariance(dimension * dimension, 0.0);
            for(const auto* vector : sampled) {
                for(std::size_t row = 0; row < dimension; ++row) {
                    const auto row_value =
                        static_cast<double>(vector->values[row])
                        - static_cast<double>(mean[row]);
                    const auto row_offset = row * dimension;
                    for(std::size_t column = 0; column <= row; ++column) {
                        const auto column_value =
                            static_cast<double>(vector->values[column])
                            - static_cast<double>(mean[column]);
                        covariance[row_offset + column] += row_value * column_value;
                    }
                }
            }

            const auto denominator =
                sampled.size() > 1
                    ? static_cast<double>(sampled.size() - 1U)
                    : 1.0;
            for(std::size_t row = 0; row < dimension; ++row) {
                const auto row_offset = row * dimension;
                for(std::size_t column = 0; column <= row; ++column) {
                    const auto value = covariance[row_offset + column] / denominator;
                    covariance[row_offset + column] = value;
                    covariance[column * dimension + row] = value;
                }
            }
            return covariance;
        }

        [[nodiscard]] float dot_product_centered(
            const float* row,
            const Embedding& vector,
            const std::vector<float>& mean
        ) noexcept {
            double value = 0.0;
            for(std::size_t index = 0; index < vector.values.size(); ++index) {
                value += static_cast<double>(row[index])
                    * (static_cast<double>(vector.values[index])
                       - static_cast<double>(mean[index]));
            }
            return static_cast<float>(value);
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

    PcaProjectionBinaryEncoderOptions train_pca_projection_encoder(
        const std::vector<Embedding>& training_vectors,
        PcaProjectionTrainingOptions options
    ) {
        validate_training_input(training_vectors, options);
        const auto sampled = sample_training_vectors(training_vectors, options);
        if(sampled.size() < 2) {
            throw std::invalid_argument(
                "PCA projection training sample must contain at least two vectors"
            );
        }

        PcaProjectionBinaryEncoderOptions artifact;
        artifact.input_dimension = options.input_dimension;
        artifact.bit_count = options.bit_count;
        artifact.seed = options.seed;
        artifact.training_vector_count = sampled.size();
        artifact.power_iterations = options.power_iterations;
        artifact.mean = compute_mean(sampled, options.input_dimension);
        artifact.projection_rows.resize(
            checked_projection_value_count(options.input_dimension, options.bit_count)
        );
        artifact.thresholds.resize(options.bit_count);

        auto covariance = compute_covariance(
            sampled,
            artifact.mean,
            options.input_dimension
        );
        std::vector<double> axis(options.input_dimension, 0.0);
        std::vector<double> multiplied(options.input_dimension, 0.0);

        for(std::size_t bit = 0; bit < options.bit_count; ++bit) {
            initialize_axis(
                options,
                bit,
                artifact.projection_rows,
                bit,
                axis
            );
            for(std::size_t iteration = 0; iteration < options.power_iterations;
                ++iteration) {
                multiply_covariance(
                    covariance,
                    axis,
                    options.input_dimension,
                    multiplied
                );
                reorthogonalize_against_previous_rows(
                    artifact.projection_rows,
                    bit,
                    options.input_dimension,
                    multiplied
                );
                if(!normalize(multiplied)) {
                    break;
                }
                axis.swap(multiplied);
            }

            reorthogonalize_against_previous_rows(
                artifact.projection_rows,
                bit,
                options.input_dimension,
                axis
            );
            if(!normalize_orthogonal_axis(axis)) {
                initialize_axis(
                    options,
                    bit + 0x9e3779b97f4a7c15ULL,
                    artifact.projection_rows,
                    bit,
                    axis
                );
            }

            auto* row = artifact.projection_rows.data() + bit * options.input_dimension;
            for(std::size_t dimension = 0; dimension < options.input_dimension; ++dimension) {
                row[dimension] = static_cast<float>(axis[dimension]);
            }

            multiply_covariance(
                covariance,
                axis,
                options.input_dimension,
                multiplied
            );
            const auto eigenvalue = dot(axis, multiplied);
            deflate_covariance(
                covariance,
                axis,
                eigenvalue,
                options.input_dimension
            );

            std::vector<float> projections;
            projections.reserve(sampled.size());
            for(const auto* vector : sampled) {
                projections.push_back(
                    dot_product_centered(row, *vector, artifact.mean)
                );
            }
            artifact.thresholds[bit] = lower_median(projections);
        }

        return artifact;
    }

    PcaProjectionBinaryEncoder::PcaProjectionBinaryEncoder(
        PcaProjectionBinaryEncoderOptions options
    )
        : m_options(std::move(options)) {
        validate_artifact(m_options);
        m_info.encoder_id = "pca_projection";
        m_info.encoder_version = "v1";
        m_info.input_dimension = m_options.input_dimension;
        m_info.bit_count = m_options.bit_count;
        m_info.seed = m_options.seed;
        m_info.config_fingerprint = make_config_fingerprint(m_options);
    }

    const BinarySignatureEncoderInfo& PcaProjectionBinaryEncoder::info()
        const noexcept {
        return m_info;
    }

    BinarySignature PcaProjectionBinaryEncoder::encode(const Embedding& vector) const {
        validate_input(vector);
        return encode_validated(vector);
    }

    std::vector<BinarySignature> PcaProjectionBinaryEncoder::encode_batch(
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

    VectorSimilarityBackend PcaProjectionBinaryEncoder::similarity_backend()
        const noexcept {
        return m_similarity.backend();
    }

    const std::vector<float>& PcaProjectionBinaryEncoder::mean() const noexcept {
        return m_options.mean;
    }

    const std::vector<float>& PcaProjectionBinaryEncoder::projection_rows()
        const noexcept {
        return m_options.projection_rows;
    }

    const std::vector<float>& PcaProjectionBinaryEncoder::thresholds() const noexcept {
        return m_options.thresholds;
    }

    void PcaProjectionBinaryEncoder::validate_input(const Embedding& vector) const {
        if(vector.dimension() != m_options.input_dimension) {
            throw std::invalid_argument("PCA projection encoder input dimension mismatch");
        }
        for(const auto value : vector.values) {
            if(!std::isfinite(value)) {
                throw std::invalid_argument(
                    "PCA projection encoder input must be finite"
                );
            }
        }
    }

    BinarySignature PcaProjectionBinaryEncoder::encode_validated(
        const Embedding& vector
    ) const {
        std::vector<float> centered(m_options.input_dimension, 0.0F);
        for(std::size_t dimension = 0; dimension < m_options.input_dimension;
            ++dimension) {
            centered[dimension] = vector.values[dimension] - m_options.mean[dimension];
        }

        BinarySignature signature(m_options.bit_count);
        for(std::size_t bit = 0; bit < m_options.bit_count; ++bit) {
            const auto dot = m_similarity.dot_product_values(
                centered.data(),
                m_options.projection_rows.data() + bit * m_options.input_dimension,
                m_options.input_dimension
            );
            signature.set_bit(bit, dot > m_options.thresholds[bit]);
        }
        return signature;
    }

} // namespace agent_memory
