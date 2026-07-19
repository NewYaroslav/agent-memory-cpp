#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_INFO_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_INFO_HPP_INCLUDED

/// \file BinarySignatureInfo.hpp
/// \brief Persisted identity metadata for binary signatures.

#include <agent_memory/embedding/enums.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace agent_memory {

    struct EmbeddingModelInfo;
    struct BinarySignatureEncoderInfo;

    /// \brief Stable identity metadata attached to a stored binary signature.
    ///
    /// The encoder fingerprint identifies how the bits were produced. Source
    /// model and projection fields identify what dense vector was encoded.
    struct BinarySignatureInfo final {
        std::string encoder_id;
        std::string encoder_version;
        std::string encoder_config_fingerprint;
        std::string source_model_id;
        std::string projection_kind;
        std::size_t source_dimension = 0;
        std::size_t bit_count = 0;
        SimilarityMetric source_similarity_metric = SimilarityMetric::Cosine;
        bool source_normalized = false;
        std::uint64_t seed = 0;
    };

    [[nodiscard]] bool operator==(
        const BinarySignatureInfo& lhs,
        const BinarySignatureInfo& rhs
    ) noexcept;

    [[nodiscard]] bool operator!=(
        const BinarySignatureInfo& lhs,
        const BinarySignatureInfo& rhs
    ) noexcept;

    /// \brief Returns true when persisted binary signature identity is well-formed.
    [[nodiscard]] bool is_valid(const BinarySignatureInfo& info) noexcept;

    /// \brief Builds signature identity metadata from an encoder and source model.
    ///
    /// Throws std::invalid_argument when required identity fields are missing or
    /// when the encoder dimension does not match the source model dimension.
    [[nodiscard]] BinarySignatureInfo make_binary_signature_info(
        const BinarySignatureEncoderInfo& encoder,
        const EmbeddingModelInfo& source_model,
        std::string projection_kind
    );

    /// \brief Returns true when stored signature metadata matches the encoder.
    [[nodiscard]] bool is_compatible(
        const BinarySignatureInfo& signature_info,
        const BinarySignatureEncoderInfo& encoder
    ) noexcept;

} // namespace agent_memory

#endif
