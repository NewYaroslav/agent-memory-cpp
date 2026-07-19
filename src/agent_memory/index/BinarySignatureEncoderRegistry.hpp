#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_ENCODER_REGISTRY_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_ENCODER_REGISTRY_HPP_INCLUDED

/// \file BinarySignatureEncoderRegistry.hpp
/// \brief In-memory registry and persisted identity metadata for binary encoders.

#include "IBinarySignatureEncoder.hpp"

#include <agent_memory/embedding/enums.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

    struct EmbeddingModelInfo;

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

    /// \brief Dependency-free in-memory registry keyed by encoder config fingerprint.
    class BinarySignatureEncoderRegistry final {
    public:
        using EncoderPtr = std::shared_ptr<const IBinarySignatureEncoder>;

        /// \brief Number of registered encoder configurations.
        [[nodiscard]] std::size_t size() const noexcept;

        /// \brief Returns true when no encoders are registered.
        [[nodiscard]] bool empty() const noexcept;

        /// \brief Registers one encoder configuration.
        ///
        /// Registration is idempotent for the same fingerprint and same encoder
        /// metadata. Reusing a fingerprint for different metadata is rejected.
        void register_encoder(EncoderPtr encoder);

        /// \brief Returns true when an encoder fingerprint has a registration snapshot.
        /// \note This does not validate the live encoder object. A later
        ///       `find()` or `require()` call may still throw std::logic_error
        ///       if a custom adapter changed `info()` after registration.
        [[nodiscard]] bool contains(std::string_view config_fingerprint) const;

        /// \brief Finds an encoder by config fingerprint, or returns nullptr.
        /// \throws std::logic_error when a registered encoder's live metadata
        ///         no longer matches its registration-time snapshot.
        [[nodiscard]] EncoderPtr find(std::string_view config_fingerprint) const;

        /// \brief Finds an encoder by config fingerprint, or throws std::out_of_range.
        /// \throws std::logic_error when a registered encoder's live metadata
        ///         no longer matches its registration-time snapshot.
        [[nodiscard]] EncoderPtr require(std::string_view config_fingerprint) const;

        /// \brief Returns registered encoder metadata sorted by config fingerprint.
        [[nodiscard]] std::vector<BinarySignatureEncoderInfo> entries() const;

    private:
        /// \brief Registration-time metadata snapshot plus the live encoder.
        ///
        /// The snapshot keeps registry lookup keys stable even if a custom
        /// adapter mutates its returned `info()` after registration.
        struct Entry final {
            BinarySignatureEncoderInfo info;
            EncoderPtr encoder;
        };

        [[nodiscard]] std::vector<Entry>::const_iterator find_entry(
            std::string_view config_fingerprint
        ) const;

        void require_consistent_entry(const Entry& entry) const;

        std::vector<Entry> m_entries;
    };

} // namespace agent_memory

#endif
