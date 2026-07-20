#include "CoordinateSignBinaryEncoder.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>

namespace agent_memory {
    namespace {

        template <typename Integer>
        void append_integer(std::string& output, Integer value) {
            std::array<char, 32> buffer{};
            const auto result =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            if(result.ec != std::errc{}) {
                throw std::logic_error("failed to format coordinate-sign encoder fingerprint");
            }
            output.append(buffer.data(), result.ptr);
        }

        [[nodiscard]] std::string make_config_fingerprint(std::size_t dimension) {
            std::string output = "coordinate_sign_v1:dim=";
            append_integer(output, dimension);
            return output;
        }

    } // namespace

    CoordinateSignBinaryEncoder::CoordinateSignBinaryEncoder(
        CoordinateSignBinaryEncoderOptions options
    )
        : m_options(options) {
        if(m_options.input_dimension == 0) {
            throw std::invalid_argument("coordinate-sign encoder input dimension must be positive");
        }

        m_info.encoder_id = "coordinate_sign";
        m_info.encoder_version = "v1";
        m_info.input_dimension = m_options.input_dimension;
        m_info.bit_count = m_options.input_dimension;
        m_info.seed = 0;
        m_info.config_fingerprint = make_config_fingerprint(m_options.input_dimension);
    }

    const BinarySignatureEncoderInfo& CoordinateSignBinaryEncoder::info() const noexcept {
        return m_info;
    }

    BinarySignature CoordinateSignBinaryEncoder::encode(const Embedding& vector) const {
        validate_input(vector);

        BinarySignature signature(m_options.input_dimension);
        for(std::size_t index = 0; index < vector.values.size(); ++index) {
            signature.set_bit(index, vector.values[index] > 0.0F);
        }
        return signature;
    }

    void CoordinateSignBinaryEncoder::validate_input(const Embedding& vector) const {
        if(vector.dimension() != m_options.input_dimension) {
            throw std::invalid_argument("coordinate-sign encoder input dimension mismatch");
        }

        for(const auto value : vector.values) {
            if(!std::isfinite(value)) {
                throw std::invalid_argument("coordinate-sign encoder input must be finite");
            }
        }
    }

} // namespace agent_memory
