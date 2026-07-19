#include "RandomHyperplaneBinaryEncoder.hpp"

#include <array>
#include <charconv>
#include <cmath>
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
            std::string output = "random_hyperplane_rademacher_v1:dim=";
            append_integer(output, options.input_dimension);
            output += ":bits=";
            append_integer(output, options.bit_count);
            output += ":seed=";
            append_integer(output, options.seed);
            return output;
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
        m_info.encoder_version = "v1";
        m_info.input_dimension = m_options.input_dimension;
        m_info.bit_count = m_options.bit_count;
        m_info.seed = m_options.seed;
        m_info.config_fingerprint = make_config_fingerprint(m_options);
    }

    const BinarySignatureEncoderInfo& RandomHyperplaneBinaryEncoder::info() const noexcept {
        return m_info;
    }

    BinarySignature RandomHyperplaneBinaryEncoder::encode(const Embedding& vector) const {
        if(vector.dimension() != m_options.input_dimension) {
            throw std::invalid_argument("binary signature encoder input dimension mismatch");
        }

        for(const auto value : vector.values) {
            if(!std::isfinite(value)) {
                throw std::invalid_argument("binary signature encoder input must be finite");
            }
        }

        BinarySignature signature(m_options.bit_count);
        for(std::size_t bit = 0; bit < m_options.bit_count; ++bit) {
            double dot = 0.0;
            for(std::size_t dimension = 0; dimension < vector.values.size(); ++dimension) {
                const auto weight = hyperplane_sign(m_options.seed, bit, dimension) ? 1.0 : -1.0;
                dot += static_cast<double>(vector.values[dimension]) * weight;
            }
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
