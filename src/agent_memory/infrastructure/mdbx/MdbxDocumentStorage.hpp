#pragma once
#ifndef AGENT_MEMORY_HEADER_INFRASTRUCTURE_MDBX_MDBX_DOCUMENT_STORAGE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INFRASTRUCTURE_MDBX_MDBX_DOCUMENT_STORAGE_HPP_INCLUDED

/// \file MdbxDocumentStorage.hpp
/// \brief MDBX-backed document storage adapter.

#include "../../storage/IDocumentStorage.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#if AGENT_MEMORY_HAS_MDBX

namespace agent_memory {

    /// \brief Configuration for MDBX-backed document storage.
    struct MdbxDocumentStorageOptions final {
        std::string path;
        /// \brief Table name prefix.
        /// \note Non-alphanumeric characters are normalized to underscores, so
        ///       distinct input prefixes can map to the same table prefix.
        std::string table_prefix = "agent_memory";
        bool relative_to_exe = false;
    };

    /// \brief MDBX-backed implementation of `IDocumentStorage`.
    class MdbxDocumentStorage final : public IDocumentStorage {
    public:
        explicit MdbxDocumentStorage(MdbxDocumentStorageOptions options);
        ~MdbxDocumentStorage() override;

        MdbxDocumentStorage(const MdbxDocumentStorage&) = delete;
        MdbxDocumentStorage& operator=(const MdbxDocumentStorage&) = delete;

        MdbxDocumentStorage(MdbxDocumentStorage&& other) noexcept;
        MdbxDocumentStorage& operator=(MdbxDocumentStorage&& other) noexcept;

        void upsert_document(DocumentSnapshot snapshot) override;

        [[nodiscard]] std::optional<Document> find_document(
            const DocumentId& id
        ) const override;

        [[nodiscard]] std::optional<DocumentChunk> find_chunk(
            const ChunkId& id
        ) const override;

        [[nodiscard]] std::vector<DocumentChunk> list_chunks(
            const DocumentId& document_id
        ) const override;

        [[nodiscard]] bool erase_document(const DocumentId& id) override;

    private:
        class Impl;

        std::unique_ptr<Impl> m_impl;
    };

} // namespace agent_memory

#endif // AGENT_MEMORY_HAS_MDBX

#endif
