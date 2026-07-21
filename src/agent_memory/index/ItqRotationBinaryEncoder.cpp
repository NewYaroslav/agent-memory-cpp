#include "ItqRotationBinaryEncoder.hpp"

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

        constexpr double kMinimumAxisNorm = 1.0e-8;

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
                throw std::logic_error("failed to format ITQ encoder fingerprint");
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
                throw std::logic_error("failed to format ITQ artifact hash");
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
            const ItqRotationBinaryEncoderOptions& options
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
            hash ^= static_cast<std::uint64_t>(options.pca_power_iterations);
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint64_t>(options.rotation_iterations);
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
            const ItqRotationBinaryEncoderOptions& options
        ) {
            std::string output = "itq_rotation_projection_v1:dim=";
            append_integer(output, options.input_dimension);
            output += ":bits=";
            append_integer(output, options.bit_count);
            output += ":seed=";
            append_integer(output, options.seed);
            output += ":train=";
            append_integer(output, options.training_vector_count);
            output += ":pca_iters=";
            append_integer(output, options.pca_power_iterations);
            output += ":itq_iters=";
            append_integer(output, options.rotation_iterations);
            output += ":artifact=";
            append_hex64(output, artifact_hash(options));
            return output;
        }

        [[nodiscard]] std::size_t checked_matrix_value_count(
            std::size_t rows,
            std::size_t columns
        ) {
            if(rows == 0 || columns == 0) {
                return 0;
            }
            if(rows > std::numeric_limits<std::size_t>::max() / columns) {
                throw std::length_error("ITQ matrix size overflows size_t");
            }
            return rows * columns;
        }

        void validate_training_input(
            const std::vector<Embedding>& training_vectors,
            const ItqRotationTrainingOptions& options
        ) {
            if(options.input_dimension == 0) {
                throw std::invalid_argument("ITQ training input dimension must be positive");
            }
            if(options.bit_count == 0) {
                throw std::invalid_argument("ITQ training bit count must be positive");
            }
            if(options.bit_count > options.input_dimension) {
                throw std::invalid_argument(
                    "ITQ bit count must not exceed input dimension"
                );
            }
            if(options.pca_power_iterations == 0) {
                throw std::invalid_argument(
                    "ITQ PCA power iterations must be positive"
                );
            }
            if(options.rotation_iterations == 0) {
                throw std::invalid_argument(
                    "ITQ rotation iterations must be positive"
                );
            }
            if(options.max_training_vectors == 1) {
                throw std::invalid_argument(
                    "ITQ training requires max_training_vectors to be zero or at least two"
                );
            }
            if(training_vectors.size() < 2) {
                throw std::invalid_argument("ITQ training requires at least two vectors");
            }
            for(const auto& vector : training_vectors) {
                if(vector.dimension() != options.input_dimension) {
                    throw std::invalid_argument("ITQ training vector dimension mismatch");
                }
                for(const auto value : vector.values) {
                    if(!std::isfinite(value)) {
                        throw std::invalid_argument("ITQ training input must be finite");
                    }
                }
            }
        }

        void validate_artifact(const ItqRotationBinaryEncoderOptions& options) {
            if(options.input_dimension == 0) {
                throw std::invalid_argument("ITQ encoder input dimension must be positive");
            }
            if(options.bit_count == 0) {
                throw std::invalid_argument("ITQ encoder bit count must be positive");
            }
            if(options.bit_count > options.input_dimension) {
                throw std::invalid_argument(
                    "ITQ encoder bit count must not exceed input dimension"
                );
            }
            if(options.training_vector_count < 2) {
                throw std::invalid_argument(
                    "ITQ encoder requires at least two training vectors"
                );
            }
            if(options.pca_power_iterations == 0 || options.rotation_iterations == 0) {
                throw std::invalid_argument("ITQ encoder iteration counts must be positive");
            }
            if(options.mean.size() != options.input_dimension) {
                throw std::invalid_argument("ITQ encoder mean size mismatch");
            }
            if(options.projection_rows.size() !=
               checked_matrix_value_count(options.bit_count, options.input_dimension)) {
                throw std::invalid_argument("ITQ encoder projection matrix size mismatch");
            }
            if(options.thresholds.size() != options.bit_count) {
                throw std::invalid_argument("ITQ encoder threshold count mismatch");
            }
            for(const auto value : options.mean) {
                if(!std::isfinite(value)) {
                    throw std::invalid_argument("ITQ encoder mean values must be finite");
                }
            }
            for(const auto value : options.projection_rows) {
                if(!std::isfinite(value)) {
                    throw std::invalid_argument(
                        "ITQ encoder projection values must be finite"
                    );
                }
            }
            for(const auto value : options.thresholds) {
                if(!std::isfinite(value)) {
                    throw std::invalid_argument("ITQ encoder thresholds must be finite");
                }
            }
        }

        [[nodiscard]] std::vector<const Embedding*> sample_training_vectors(
            const std::vector<Embedding>& training_vectors,
            const ItqRotationTrainingOptions& options
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

        [[nodiscard]] std::vector<double> centered_pca_projection_matrix(
            const std::vector<const Embedding*>& sampled,
            const PcaProjectionBinaryEncoderOptions& pca
        ) {
            std::vector<double> projections(
                checked_matrix_value_count(sampled.size(), pca.bit_count),
                0.0
            );
            for(std::size_t sample = 0; sample < sampled.size(); ++sample) {
                const auto& vector = *sampled[sample];
                for(std::size_t bit = 0; bit < pca.bit_count; ++bit) {
                    const auto* row =
                        pca.projection_rows.data() + bit * pca.input_dimension;
                    double value = 0.0;
                    for(std::size_t dimension = 0; dimension < pca.input_dimension;
                        ++dimension) {
                        value += static_cast<double>(row[dimension])
                            * (static_cast<double>(vector.values[dimension])
                               - static_cast<double>(pca.mean[dimension]));
                    }
                    projections[sample * pca.bit_count + bit] = value;
                }
            }
            return projections;
        }

        [[nodiscard]] std::vector<double> identity_matrix(std::size_t dimension) {
            std::vector<double> matrix(
                checked_matrix_value_count(dimension, dimension),
                0.0
            );
            for(std::size_t index = 0; index < dimension; ++index) {
                matrix[index * dimension + index] = 1.0;
            }
            return matrix;
        }

        void jacobi_rotate(
            std::vector<double>& matrix,
            std::vector<double>& eigenvectors,
            std::size_t dimension,
            std::size_t p,
            std::size_t q
        ) {
            const auto app = matrix[p * dimension + p];
            const auto aqq = matrix[q * dimension + q];
            const auto apq = matrix[p * dimension + q];
            if(std::fabs(apq) <= 1.0e-12 * (std::fabs(app) + std::fabs(aqq) + 1.0)) {
                return;
            }
            const auto tau = (aqq - app) / (2.0 * apq);
            const auto sign = tau >= 0.0 ? 1.0 : -1.0;
            const auto t = sign / (std::fabs(tau) + std::sqrt(1.0 + tau * tau));
            const auto c = 1.0 / std::sqrt(1.0 + t * t);
            const auto s = t * c;

            for(std::size_t k = 0; k < dimension; ++k) {
                if(k == p || k == q) {
                    continue;
                }
                const auto akp = matrix[k * dimension + p];
                const auto akq = matrix[k * dimension + q];
                const auto new_kp = c * akp - s * akq;
                const auto new_kq = s * akp + c * akq;
                matrix[k * dimension + p] = new_kp;
                matrix[p * dimension + k] = new_kp;
                matrix[k * dimension + q] = new_kq;
                matrix[q * dimension + k] = new_kq;
            }

            matrix[p * dimension + p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
            matrix[q * dimension + q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
            matrix[p * dimension + q] = 0.0;
            matrix[q * dimension + p] = 0.0;

            for(std::size_t k = 0; k < dimension; ++k) {
                const auto vkp = eigenvectors[k * dimension + p];
                const auto vkq = eigenvectors[k * dimension + q];
                eigenvectors[k * dimension + p] = c * vkp - s * vkq;
                eigenvectors[k * dimension + q] = s * vkp + c * vkq;
            }
        }

        [[nodiscard]] std::vector<double> symmetric_eigenvectors(
            std::vector<double> matrix,
            std::size_t dimension
        ) {
            auto eigenvectors = identity_matrix(dimension);
            for(std::size_t sweep = 0; sweep < 32; ++sweep) {
                double max_offdiag = 0.0;
                for(std::size_t p = 0; p < dimension; ++p) {
                    for(std::size_t q = p + 1; q < dimension; ++q) {
                        max_offdiag = std::max(
                            max_offdiag,
                            std::fabs(matrix[p * dimension + q])
                        );
                        jacobi_rotate(matrix, eigenvectors, dimension, p, q);
                    }
                }
                if(max_offdiag <= 1.0e-10) {
                    break;
                }
            }
            return eigenvectors;
        }

        [[nodiscard]] double column_norm(
            const std::vector<double>& matrix,
            std::size_t dimension,
            std::size_t column
        ) noexcept {
            double sum = 0.0;
            for(std::size_t row = 0; row < dimension; ++row) {
                const auto value = matrix[row * dimension + column];
                sum += value * value;
            }
            return std::sqrt(sum);
        }

        void orthogonalize_column(
            std::vector<double>& matrix,
            std::size_t dimension,
            std::size_t column,
            std::size_t completed_columns
        ) {
            for(std::size_t previous = 0; previous < completed_columns; ++previous) {
                double projection = 0.0;
                for(std::size_t row = 0; row < dimension; ++row) {
                    projection += matrix[row * dimension + column]
                        * matrix[row * dimension + previous];
                }
                for(std::size_t row = 0; row < dimension; ++row) {
                    matrix[row * dimension + column] -=
                        projection * matrix[row * dimension + previous];
                }
            }
        }

        [[nodiscard]] bool normalize_column(
            std::vector<double>& matrix,
            std::size_t dimension,
            std::size_t column
        ) noexcept {
            const auto norm = column_norm(matrix, dimension, column);
            if(norm <= kMinimumAxisNorm || !std::isfinite(norm)) {
                return false;
            }
            for(std::size_t row = 0; row < dimension; ++row) {
                matrix[row * dimension + column] /= norm;
            }
            return true;
        }

        void fill_orthonormal_complement_column(
            std::vector<double>& matrix,
            std::size_t dimension,
            std::size_t column
        ) {
            for(std::size_t offset = 0; offset < dimension; ++offset) {
                for(std::size_t row = 0; row < dimension; ++row) {
                    matrix[row * dimension + column] = 0.0;
                }
                matrix[((column + offset) % dimension) * dimension + column] = 1.0;
                orthogonalize_column(matrix, dimension, column, column);
                orthogonalize_column(matrix, dimension, column, column);
                if(normalize_column(matrix, dimension, column)) {
                    return;
                }
            }
            throw std::logic_error("failed to construct ITQ orthogonal complement");
        }

        void orthonormalize_columns(
            std::vector<double>& matrix,
            std::size_t dimension
        ) {
            for(std::size_t column = 0; column < dimension; ++column) {
                orthogonalize_column(matrix, dimension, column, column);
                orthogonalize_column(matrix, dimension, column, column);
                if(!normalize_column(matrix, dimension, column)) {
                    fill_orthonormal_complement_column(matrix, dimension, column);
                }
            }
        }

        [[nodiscard]] std::vector<double> procrustes_rotation(
            const std::vector<double>& cross_covariance,
            std::size_t dimension
        ) {
            std::vector<double> gram(
                checked_matrix_value_count(dimension, dimension),
                0.0
            );
            for(std::size_t row = 0; row < dimension; ++row) {
                for(std::size_t column = 0; column < dimension; ++column) {
                    double value = 0.0;
                    for(std::size_t k = 0; k < dimension; ++k) {
                        value += cross_covariance[k * dimension + row]
                            * cross_covariance[k * dimension + column];
                    }
                    gram[row * dimension + column] = value;
                }
            }

            auto right = symmetric_eigenvectors(std::move(gram), dimension);
            std::vector<double> left(
                checked_matrix_value_count(dimension, dimension),
                0.0
            );
            for(std::size_t column = 0; column < dimension; ++column) {
                for(std::size_t row = 0; row < dimension; ++row) {
                    double value = 0.0;
                    for(std::size_t k = 0; k < dimension; ++k) {
                        value += cross_covariance[row * dimension + k]
                            * right[k * dimension + column];
                    }
                    left[row * dimension + column] = value;
                }
            }
            orthonormalize_columns(left, dimension);

            std::vector<double> rotation(
                checked_matrix_value_count(dimension, dimension),
                0.0
            );
            for(std::size_t row = 0; row < dimension; ++row) {
                for(std::size_t column = 0; column < dimension; ++column) {
                    double value = 0.0;
                    for(std::size_t k = 0; k < dimension; ++k) {
                        value += left[row * dimension + k]
                            * right[column * dimension + k];
                    }
                    rotation[row * dimension + column] = value;
                }
            }
            return rotation;
        }

        [[nodiscard]] std::vector<double> train_rotation(
            const std::vector<double>& projections,
            std::size_t sample_count,
            std::size_t bit_count,
            std::size_t iterations
        ) {
            auto rotation = identity_matrix(bit_count);
            std::vector<double> cross_covariance(
                checked_matrix_value_count(bit_count, bit_count),
                0.0
            );
            for(std::size_t iteration = 0; iteration < iterations; ++iteration) {
                std::fill(cross_covariance.begin(), cross_covariance.end(), 0.0);
                for(std::size_t sample = 0; sample < sample_count; ++sample) {
                    const auto* projection = projections.data() + sample * bit_count;
                    for(std::size_t bit = 0; bit < bit_count; ++bit) {
                        double rotated = 0.0;
                        for(std::size_t source = 0; source < bit_count; ++source) {
                            rotated += projection[source]
                                * rotation[source * bit_count + bit];
                        }
                        const auto binary_value = rotated > 0.0 ? 1.0 : -1.0;
                        for(std::size_t source = 0; source < bit_count; ++source) {
                            cross_covariance[source * bit_count + bit] +=
                                projection[source] * binary_value;
                        }
                    }
                }
                rotation = procrustes_rotation(cross_covariance, bit_count);
            }
            return rotation;
        }

        [[nodiscard]] std::vector<float> rotate_pca_rows(
            const PcaProjectionBinaryEncoderOptions& pca,
            const std::vector<double>& rotation
        ) {
            std::vector<float> rows(
                checked_matrix_value_count(pca.bit_count, pca.input_dimension),
                0.0F
            );
            for(std::size_t bit = 0; bit < pca.bit_count; ++bit) {
                for(std::size_t dimension = 0; dimension < pca.input_dimension;
                    ++dimension) {
                    double value = 0.0;
                    for(std::size_t source = 0; source < pca.bit_count; ++source) {
                        value += rotation[source * pca.bit_count + bit]
                            * static_cast<double>(
                                pca.projection_rows[source * pca.input_dimension
                                                    + dimension]
                            );
                    }
                    rows[bit * pca.input_dimension + dimension] =
                        static_cast<float>(value);
                }
            }
            return rows;
        }

    } // namespace

    ItqRotationBinaryEncoderOptions train_itq_rotation_encoder(
        const std::vector<Embedding>& training_vectors,
        ItqRotationTrainingOptions options
    ) {
        validate_training_input(training_vectors, options);

        PcaProjectionTrainingOptions pca_options;
        pca_options.input_dimension = options.input_dimension;
        pca_options.bit_count = options.bit_count;
        pca_options.seed = options.seed;
        pca_options.power_iterations = options.pca_power_iterations;
        pca_options.max_training_vectors = options.max_training_vectors;
        const auto pca = train_pca_projection_encoder(training_vectors, pca_options);

        const auto sampled = sample_training_vectors(training_vectors, options);
        if(sampled.size() != pca.training_vector_count) {
            throw std::logic_error("ITQ and PCA training samples diverged");
        }
        const auto projections = centered_pca_projection_matrix(sampled, pca);
        const auto rotation = train_rotation(
            projections,
            sampled.size(),
            options.bit_count,
            options.rotation_iterations
        );

        ItqRotationBinaryEncoderOptions artifact;
        artifact.input_dimension = options.input_dimension;
        artifact.bit_count = options.bit_count;
        artifact.seed = options.seed;
        artifact.training_vector_count = sampled.size();
        artifact.pca_power_iterations = options.pca_power_iterations;
        artifact.rotation_iterations = options.rotation_iterations;
        artifact.mean = pca.mean;
        artifact.projection_rows = rotate_pca_rows(pca, rotation);
        artifact.thresholds.assign(options.bit_count, 0.0F);
        return artifact;
    }

    ItqRotationBinaryEncoder::ItqRotationBinaryEncoder(
        ItqRotationBinaryEncoderOptions options
    )
        : m_options(std::move(options)) {
        validate_artifact(m_options);
        m_info.encoder_id = "itq_rotation_projection";
        m_info.encoder_version = "v1";
        m_info.input_dimension = m_options.input_dimension;
        m_info.bit_count = m_options.bit_count;
        m_info.seed = m_options.seed;
        m_info.config_fingerprint = make_config_fingerprint(m_options);
    }

    const BinarySignatureEncoderInfo& ItqRotationBinaryEncoder::info() const noexcept {
        return m_info;
    }

    BinarySignature ItqRotationBinaryEncoder::encode(const Embedding& vector) const {
        validate_input(vector);
        return encode_validated(vector);
    }

    std::vector<BinarySignature> ItqRotationBinaryEncoder::encode_batch(
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

    VectorSimilarityBackend ItqRotationBinaryEncoder::similarity_backend()
        const noexcept {
        return m_similarity.backend();
    }

    const std::vector<float>& ItqRotationBinaryEncoder::mean() const noexcept {
        return m_options.mean;
    }

    const std::vector<float>& ItqRotationBinaryEncoder::projection_rows()
        const noexcept {
        return m_options.projection_rows;
    }

    const std::vector<float>& ItqRotationBinaryEncoder::thresholds() const noexcept {
        return m_options.thresholds;
    }

    void ItqRotationBinaryEncoder::validate_input(const Embedding& vector) const {
        if(vector.dimension() != m_options.input_dimension) {
            throw std::invalid_argument("ITQ encoder input dimension mismatch");
        }
        for(const auto value : vector.values) {
            if(!std::isfinite(value)) {
                throw std::invalid_argument("ITQ encoder input must be finite");
            }
        }
    }

    BinarySignature ItqRotationBinaryEncoder::encode_validated(
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
