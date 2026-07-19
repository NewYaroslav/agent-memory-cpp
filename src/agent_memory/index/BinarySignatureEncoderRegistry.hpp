#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_ENCODER_REGISTRY_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_ENCODER_REGISTRY_HPP_INCLUDED

/// \file BinarySignatureEncoderRegistry.hpp
/// \brief In-memory registry for binary encoders.

#include "BinarySignatureInfo.hpp"
#include "IBinarySignatureEncoder.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace agent_memory {

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
