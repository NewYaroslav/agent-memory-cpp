#include "RandomizedHadamardBinaryEncoder.hpp"

#include <algorithm>
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
                throw std::logic_error(
                    "failed to format randomized Hadamard encoder fingerprint"
                );
            }
            output.append(buffer.data(), result.ptr);
        }

        [[nodiscard]] std::string make_config_fingerprint(
            const RandomizedHadamardBinaryEncoderOptions& options,
            std::size_t padded_dimension
        ) {
            std::string output = "randomized_hadamard_projection_v1:dim=";
            append_integer(output, options.input_dimension);
            output += ":padded_dim=";
            append_integer(output, padded_dimension);
            output += ":bits=";
            append_integer(output, options.bit_count);
            output += ":seed=";
            append_integer(output, options.seed);
            return output;
        }

        [[nodiscard]] std::size_t next_power_of_two(std::size_t value) {
            if(value == 0) {
                return 0;
            }
            if(value > (std::numeric_limits<std::size_t>::max() / 2U) + 1U) {
                throw std::length_error("randomized Hadamard padded dimension overflows");
            }
            std::size_t power = 1;
            while(power < value) {
                if(power > std::numeric_limits<std::size_t>::max() / 2U) {
                    throw std::length_error(
                        "randomized Hadamard padded dimension overflows"
                    );
                }
                power *= 2U;
            }
            return power;
        }

        [[nodiscard]] bool signed_diagonal_is_positive(
            std::uint64_t seed,
            std::size_t block,
            std::size_t dimension
        ) noexcept {
            auto value = seed ^ 0x6a09e667f3bcc909ULL;
            value ^= (static_cast<std::uint64_t>(block) + 0x9e3779b97f4a7c15ULL);
            value = mix64(value);
            value ^= (static_cast<std::uint64_t>(dimension) + 0xbf58476d1ce4e5b9ULL);
            value = mix64(value);
            return (value & std::uint64_t{1}) != 0;
        }

        [[nodiscard]] std::size_t row_offset(
            std::uint64_t seed,
            std::size_t block,
            std::size_t padded_dimension
        ) noexcept {
            const auto mask = padded_dimension - 1U;
            auto value = seed ^ 0x3c6ef372fe94f82bULL;
            value ^= static_cast<std::uint64_t>(block) + 0x94d049bb133111ebULL;
            return static_cast<std::size_t>(mix64(value)) & mask;
        }

        [[nodiscard]] std::size_t row_stride(
            std::uint64_t seed,
            std::size_t block,
            std::size_t padded_dimension
        ) noexcept {
            if(padded_dimension == 1) {
                return 1;
            }
            const auto mask = padded_dimension - 1U;
            auto value = seed ^ 0xbb67ae8584caa73bULL;
            value ^= static_cast<std::uint64_t>(block) + 0x2545f4914f6cdd1dULL;
            return (static_cast<std::size_t>(mix64(value)) & mask) | std::size_t{1};
        }

        [[nodiscard]] std::size_t permuted_row(
            std::size_t within_block,
            std::size_t offset,
            std::size_t stride,
            std::size_t padded_dimension
        ) noexcept {
            return (offset + within_block * stride) & (padded_dimension - 1U);
        }

        void walsh_hadamard_transform(std::vector<float>& values) noexcept {
            const auto size = values.size();
            for(std::size_t span = 1; span < size; span *= 2U) {
                for(std::size_t base = 0; base < size; base += span * 2U) {
                    for(std::size_t lane = 0; lane < span; ++lane) {
                        const auto lhs = values[base + lane];
                        const auto rhs = values[base + lane + span];
                        values[base + lane] = lhs + rhs;
                        values[base + lane + span] = lhs - rhs;
                    }
                }
            }
        }

        void prepare_block_input(
            const Embedding& vector,
            std::uint64_t seed,
            std::size_t block,
            std::vector<float>& work
        ) noexcept {
            for(std::size_t dimension = 0; dimension < vector.values.size(); ++dimension) {
                work[dimension] = signed_diagonal_is_positive(seed, block, dimension)
                    ? vector.values[dimension]
                    : -vector.values[dimension];
            }
            for(std::size_t dimension = vector.values.size(); dimension < work.size();
                ++dimension) {
                work[dimension] = 0.0F;
            }
        }

    } // namespace

    RandomizedHadamardBinaryEncoder::RandomizedHadamardBinaryEncoder(
        RandomizedHadamardBinaryEncoderOptions options
    )
        : m_options(options),
          m_padded_dimension(next_power_of_two(options.input_dimension)) {
        if(m_options.input_dimension == 0) {
            throw std::invalid_argument(
                "randomized Hadamard encoder input dimension must be positive"
            );
        }
        if(m_options.bit_count == 0) {
            throw std::invalid_argument(
                "randomized Hadamard encoder bit count must be positive"
            );
        }

        m_info.encoder_id = "randomized_hadamard_projection";
        m_info.encoder_version = "v1";
        m_info.input_dimension = m_options.input_dimension;
        m_info.bit_count = m_options.bit_count;
        m_info.seed = m_options.seed;
        m_info.config_fingerprint =
            make_config_fingerprint(m_options, m_padded_dimension);
    }

    const BinarySignatureEncoderInfo& RandomizedHadamardBinaryEncoder::info()
        const noexcept {
        return m_info;
    }

    BinarySignature RandomizedHadamardBinaryEncoder::encode(
        const Embedding& vector
    ) const {
        validate_input(vector);
        std::vector<float> work(m_padded_dimension);
        return encode_validated(vector, work);
    }

    std::vector<BinarySignature> RandomizedHadamardBinaryEncoder::encode_batch(
        const std::vector<Embedding>& vectors
    ) const {
        for(const auto& vector : vectors) {
            validate_input(vector);
        }
        if(vectors.empty()) {
            return {};
        }

        std::vector<BinarySignature> signatures;
        signatures.reserve(vectors.size());
        std::vector<float> work(m_padded_dimension);
        for(const auto& vector : vectors) {
            signatures.push_back(encode_validated(vector, work));
        }
        return signatures;
    }

    const char* RandomizedHadamardBinaryEncoder::compute_backend_name() noexcept {
        return "fwht_scalar";
    }

    std::size_t RandomizedHadamardBinaryEncoder::padded_dimension() const noexcept {
        return m_padded_dimension;
    }

    void RandomizedHadamardBinaryEncoder::validate_input(const Embedding& vector) const {
        if(vector.dimension() != m_options.input_dimension) {
            throw std::invalid_argument(
                "randomized Hadamard encoder input dimension mismatch"
            );
        }

        for(const auto value : vector.values) {
            if(!std::isfinite(value)) {
                throw std::invalid_argument(
                    "randomized Hadamard encoder input must be finite"
                );
            }
        }
    }

    BinarySignature RandomizedHadamardBinaryEncoder::encode_validated(
        const Embedding& vector,
        std::vector<float>& work
    ) const {
        BinarySignature signature(m_options.bit_count);

        std::size_t bit = 0;
        std::size_t block = 0;
        while(bit < m_options.bit_count) {
            prepare_block_input(vector, m_options.seed, block, work);
            walsh_hadamard_transform(work);

            const auto offset = row_offset(m_options.seed, block, m_padded_dimension);
            const auto stride = row_stride(m_options.seed, block, m_padded_dimension);
            const auto block_bit_end = std::min(
                m_options.bit_count,
                bit + m_padded_dimension
            );
            for(std::size_t within = 0; bit < block_bit_end; ++within, ++bit) {
                const auto row = permuted_row(
                    within,
                    offset,
                    stride,
                    m_padded_dimension
                );
                signature.set_bit(bit, work[row] > 0.0F);
            }
            ++block;
        }

        return signature;
    }

} // namespace agent_memory
