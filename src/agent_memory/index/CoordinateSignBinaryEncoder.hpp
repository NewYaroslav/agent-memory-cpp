#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_COORDINATE_SIGN_BINARY_ENCODER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_COORDINATE_SIGN_BINARY_ENCODER_HPP_INCLUDED

/// \file CoordinateSignBinaryEncoder.hpp
/// \brief Coordinate-wise sign baseline binary signature encoder.

#include "IBinarySignatureEncoder.hpp"

#include <cstddef>

namespace agent_memory {

    /// \brief Options for the coordinate-wise sign baseline encoder.
    struct CoordinateSignBinaryEncoderOptions final {
        /// \brief Expected dense-vector dimension and output bit count.
        std::size_t input_dimension = 0;
    };

    /// \brief Zero-training baseline that stores one sign bit per input coordinate.
    ///
    /// This encoder naturally emits exactly `input_dimension` bits. It is a
    /// diagnostic baseline for comparing projected encoders against the cheapest
    /// possible sign quantisation. Bits are set only for strictly positive input
    /// values; zero and negative coordinates encode as zero.
    class CoordinateSignBinaryEncoder final : public IBinarySignatureEncoder {
    public:
        explicit CoordinateSignBinaryEncoder(CoordinateSignBinaryEncoderOptions options);

        [[nodiscard]] const BinarySignatureEncoderInfo& info() const noexcept override;

        [[nodiscard]] BinarySignature encode(const Embedding& vector) const override;

    private:
        void validate_input(const Embedding& vector) const;

        CoordinateSignBinaryEncoderOptions m_options;
        BinarySignatureEncoderInfo m_info;
    };

} // namespace agent_memory

#endif
