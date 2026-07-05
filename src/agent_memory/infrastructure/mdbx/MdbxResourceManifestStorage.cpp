#include "MdbxResourceManifestStorage.hpp"

#if AGENT_MEMORY_HAS_MDBX

#include <mdbx_containers/KeyValueTable.hpp>

#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace agent_memory {

    namespace {

        constexpr std::string_view RESOURCE_MANIFEST_PAYLOAD_VERSION =
            "agent_memory.resource_manifest.v1";

        std::string sanitize_table_part(std::string value) {
            if(value.empty()) {
                return "agent_memory";
            }

            for(char& c : value) {
                const auto unsigned_c = static_cast<unsigned char>(c);
                if(!std::isalnum(unsigned_c)) {
                    c = '_';
                }
            }
            return value;
        }

        std::string table_name(const std::string& prefix, std::string_view suffix) {
            std::string result = sanitize_table_part(prefix);
            result.push_back('_');
            result.append(suffix.data(), suffix.size());
            return result;
        }

        void append_size(std::string& payload, std::size_t value) {
            payload += std::to_string(value);
            payload.push_back(':');
        }

        void append_string(std::string& payload, std::string_view value) {
            append_size(payload, value.size());
            payload.append(value.data(), value.size());
        }

        void append_uint64(std::string& payload, std::uint64_t value) {
            append_string(payload, std::to_string(value));
        }

        class PayloadReader final {
        public:
            explicit PayloadReader(std::string_view payload)
                : m_payload(payload) {}

            [[nodiscard]] std::size_t read_size() {
                if(m_position >= m_payload.size()) {
                    throw std::runtime_error("Unexpected end of payload");
                }

                std::size_t value = 0;
                bool has_digit = false;
                while(m_position < m_payload.size() && m_payload[m_position] != ':') {
                    const unsigned char c = static_cast<unsigned char>(m_payload[m_position]);
                    if(!std::isdigit(c)) {
                        throw std::runtime_error("Invalid payload size marker");
                    }
                    const auto digit = static_cast<std::size_t>(
                        c - static_cast<unsigned char>('0')
                    );
                    if(value > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
                        throw std::runtime_error("Payload size marker overflow");
                    }
                    value = value * 10 + digit;
                    has_digit = true;
                    ++m_position;
                }

                if(!has_digit || m_position >= m_payload.size() || m_payload[m_position] != ':') {
                    throw std::runtime_error("Missing payload size delimiter");
                }

                ++m_position;
                return value;
            }

            [[nodiscard]] std::string read_string() {
                const auto size = read_size();
                if(size > m_payload.size() - m_position) {
                    throw std::runtime_error("Payload string exceeds available data");
                }

                std::string value{m_payload.data() + m_position, size};
                m_position += size;
                return value;
            }

            [[nodiscard]] std::uint64_t read_uint64() {
                const auto text = read_string();
                if(text.empty()) {
                    throw std::runtime_error("Missing uint64 payload value");
                }

                std::uint64_t value = 0;
                for(const unsigned char c : text) {
                    if(!std::isdigit(c)) {
                        throw std::runtime_error("Invalid uint64 payload value");
                    }
                    const auto digit = static_cast<std::uint64_t>(
                        c - static_cast<unsigned char>('0')
                    );
                    if(value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
                        throw std::runtime_error("Uint64 payload value overflow");
                    }
                    value = value * 10U + digit;
                }
                return value;
            }

            void require_end() const {
                if(m_position != m_payload.size()) {
                    throw std::runtime_error("Unexpected trailing payload data");
                }
            }

        private:
            std::string_view m_payload;
            std::size_t m_position = 0;
        };

        void append_record(std::string& payload, const DerivedRecordRef& record) {
            append_string(payload, to_string(record.kind));
            append_string(payload, record.chunk_id.value());
            append_string(payload, record.key);
            append_uint64(payload, record.ordinal);
        }

        DerivedRecordRef read_record(PayloadReader& reader) {
            DerivedRecordKind kind = DerivedRecordKind::Custom;
            if(!parse_derived_record_kind(reader.read_string(), kind)) {
                throw std::runtime_error("Invalid derived record kind");
            }

            const auto chunk_id = reader.read_string();
            auto key = reader.read_string();
            const auto ordinal = reader.read_uint64();
            if(ordinal > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("Derived record ordinal overflow");
            }

            return DerivedRecordRef{
                kind,
                ChunkId{chunk_id},
                std::move(key),
                static_cast<std::uint32_t>(ordinal)
            };
        }

        std::string serialize_manifest(const ResourceManifest& manifest) {
            if(!is_valid_resource_manifest(manifest)) {
                throw std::invalid_argument("ResourceManifest is invalid");
            }

            std::string payload;
            append_string(payload, RESOURCE_MANIFEST_PAYLOAD_VERSION);
            append_string(payload, manifest.revision.resource_id.value());
            append_uint64(payload, manifest.revision.generation);
            append_uint64(payload, manifest.revision.content_hash);
            append_uint64(payload, manifest.revision.pipeline_config_hash);
            append_size(payload, manifest.records.size());
            for(const auto& record : manifest.records) {
                append_record(payload, record);
            }
            return payload;
        }

        ResourceManifest deserialize_manifest(std::string_view payload) {
            PayloadReader reader{payload};
            if(reader.read_string() != RESOURCE_MANIFEST_PAYLOAD_VERSION) {
                throw std::runtime_error("Unsupported resource manifest payload version");
            }

            ResourceManifest manifest;
            manifest.revision.resource_id = ResourceId{reader.read_string()};
            manifest.revision.generation = reader.read_uint64();
            manifest.revision.content_hash = reader.read_uint64();
            manifest.revision.pipeline_config_hash = reader.read_uint64();

            const auto count = reader.read_size();
            manifest.records.reserve(count);
            for(std::size_t i = 0; i < count; ++i) {
                manifest.records.push_back(read_record(reader));
            }
            reader.require_end();

            if(!is_valid_resource_manifest(manifest)) {
                throw std::runtime_error("Invalid resource manifest payload");
            }
            return manifest;
        }

        mdbxc::Config make_config(const MdbxResourceManifestStorageOptions& options) {
            if(options.path.empty()) {
                throw std::invalid_argument(
                    "MdbxResourceManifestStorageOptions::path must not be empty"
                );
            }

            mdbxc::Config config;
            config.pathname = options.path;
            config.max_dbs = 16;
            config.no_subdir = true;
            config.relative_to_exe = options.relative_to_exe;
            return config;
        }

    } // namespace

    class MdbxResourceManifestStorage::Impl final {
    public:
        explicit Impl(MdbxResourceManifestStorageOptions options)
            : m_table_prefix(sanitize_table_part(std::move(options.table_prefix)))
            , m_connection(mdbxc::Connection::create(make_config(options)))
            , m_manifests(m_connection, table_name(m_table_prefix, "resource_manifests")) {}

        void upsert_manifest(ResourceManifest manifest) {
            m_manifests.insert_or_assign(
                manifest.revision.resource_id.value(),
                serialize_manifest(manifest)
            );
        }

        [[nodiscard]] std::optional<ResourceManifest> find_manifest(
            const ResourceId& resource_id
        ) const {
            const auto payload = m_manifests.find(resource_id.value());
            if(!payload) {
                return std::nullopt;
            }
            return deserialize_manifest(*payload);
        }

        [[nodiscard]] bool erase_manifest(const ResourceId& resource_id) {
            return m_manifests.erase(resource_id.value());
        }

    private:
        std::string m_table_prefix;
        std::shared_ptr<mdbxc::Connection> m_connection;
        mdbxc::KeyValueTable<std::string, std::string> m_manifests;
    };

    MdbxResourceManifestStorage::MdbxResourceManifestStorage(
        MdbxResourceManifestStorageOptions options
    )
        : m_impl(std::make_unique<Impl>(std::move(options))) {}

    MdbxResourceManifestStorage::~MdbxResourceManifestStorage() = default;

    MdbxResourceManifestStorage::MdbxResourceManifestStorage(
        MdbxResourceManifestStorage&& other
    ) noexcept = default;

    MdbxResourceManifestStorage& MdbxResourceManifestStorage::operator=(
        MdbxResourceManifestStorage&& other
    ) noexcept = default;

    void MdbxResourceManifestStorage::upsert_manifest(ResourceManifest manifest) {
        m_impl->upsert_manifest(std::move(manifest));
    }

    std::optional<ResourceManifest> MdbxResourceManifestStorage::find_manifest(
        const ResourceId& resource_id
    ) const {
        return m_impl->find_manifest(resource_id);
    }

    bool MdbxResourceManifestStorage::erase_manifest(const ResourceId& resource_id) {
        return m_impl->erase_manifest(resource_id);
    }

} // namespace agent_memory

#endif // AGENT_MEMORY_HAS_MDBX
