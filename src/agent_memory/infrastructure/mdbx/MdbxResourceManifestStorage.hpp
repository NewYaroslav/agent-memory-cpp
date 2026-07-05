#pragma once
#ifndef AGENT_MEMORY_HEADER_INFRASTRUCTURE_MDBX_MDBX_RESOURCE_MANIFEST_STORAGE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INFRASTRUCTURE_MDBX_MDBX_RESOURCE_MANIFEST_STORAGE_HPP_INCLUDED

/// \file MdbxResourceManifestStorage.hpp
/// \brief MDBX-backed resource manifest storage adapter.

#include "../../storage/IResourceManifestStorage.hpp"

#include <memory>
#include <optional>
#include <string>

#if AGENT_MEMORY_HAS_MDBX

namespace agent_memory {

    /// \brief Configuration for MDBX-backed resource manifest storage.
    struct MdbxResourceManifestStorageOptions final {
        std::string path;
        /// \brief Table name prefix.
        /// \note Non-alphanumeric characters are normalized to underscores, so
        ///       distinct input prefixes can map to the same table prefix.
        std::string table_prefix = "agent_memory";
        bool relative_to_exe = false;
    };

    /// \brief MDBX-backed implementation of `IResourceManifestStorage`.
    class MdbxResourceManifestStorage final : public IResourceManifestStorage {
    public:
        explicit MdbxResourceManifestStorage(MdbxResourceManifestStorageOptions options);
        ~MdbxResourceManifestStorage() override;

        MdbxResourceManifestStorage(const MdbxResourceManifestStorage&) = delete;
        MdbxResourceManifestStorage& operator=(
            const MdbxResourceManifestStorage&
        ) = delete;

        MdbxResourceManifestStorage(MdbxResourceManifestStorage&& other) noexcept;
        MdbxResourceManifestStorage& operator=(
            MdbxResourceManifestStorage&& other
        ) noexcept;

        void upsert_manifest(ResourceManifest manifest) override;

        [[nodiscard]] std::optional<ResourceManifest> find_manifest(
            const ResourceId& resource_id
        ) const override;

        [[nodiscard]] bool erase_manifest(const ResourceId& resource_id) override;

    private:
        class Impl;

        std::unique_ptr<Impl> m_impl;
    };

} // namespace agent_memory

#endif // AGENT_MEMORY_HAS_MDBX

#endif
