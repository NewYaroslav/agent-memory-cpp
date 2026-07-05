#include "MdbxDocumentStorage.hpp"

#if AGENT_MEMORY_HAS_MDBX

#include <mdbx_containers/KeyValueTable.hpp>

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace agent_memory {

    namespace {

        constexpr std::string_view DOCUMENT_PAYLOAD_VERSION = "agent_memory.document.v1";
        constexpr std::string_view CHUNK_PAYLOAD_VERSION = "agent_memory.chunk.v1";
        constexpr std::string_view STRING_LIST_PAYLOAD_VERSION = "agent_memory.string_list.v1";

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

        void append_metadata(std::string& payload, const Metadata& metadata) {
            append_size(payload, metadata.size());
            for(const auto& item : metadata.values()) {
                append_string(payload, item.first);
                append_string(payload, item.second);
            }
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
                    const auto digit = static_cast<std::size_t>(c - static_cast<unsigned char>('0'));
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

            void require_end() const {
                if(m_position != m_payload.size()) {
                    throw std::runtime_error("Unexpected trailing payload data");
                }
            }

        private:
            std::string_view m_payload;
            std::size_t m_position = 0;
        };

        Metadata read_metadata(PayloadReader& reader) {
            Metadata metadata;
            const auto count = reader.read_size();
            for(std::size_t i = 0; i < count; ++i) {
                auto key = reader.read_string();
                auto value = reader.read_string();
                metadata.set(std::move(key), std::move(value));
            }
            return metadata;
        }

        std::string serialize_document(const Document& document) {
            std::string payload;
            append_string(payload, DOCUMENT_PAYLOAD_VERSION);
            append_string(payload, document.id.value());
            append_string(payload, to_string(document.kind));
            append_string(payload, document.source_uri);
            append_string(payload, document.text);
            append_metadata(payload, document.metadata);
            return payload;
        }

        Document deserialize_document(std::string_view payload) {
            PayloadReader reader{payload};
            if(reader.read_string() != DOCUMENT_PAYLOAD_VERSION) {
                throw std::runtime_error("Unsupported document payload version");
            }

            const auto id = reader.read_string();
            SourceKind kind = SourceKind::Unknown;
            if(!parse_source_kind(reader.read_string(), kind)) {
                throw std::runtime_error("Invalid document source kind");
            }

            auto source_uri = reader.read_string();
            auto text = reader.read_string();
            auto metadata = read_metadata(reader);
            reader.require_end();

            Document document{
                DocumentId{id},
                kind,
                std::move(source_uri),
                std::move(text),
                std::move(metadata)
            };
            return document;
        }

        std::string serialize_chunk(const DocumentChunk& chunk) {
            std::string payload;
            append_string(payload, CHUNK_PAYLOAD_VERSION);
            append_string(payload, chunk.id.value());
            append_string(payload, chunk.document_id.value());
            append_size(payload, chunk.source_range.offset);
            append_size(payload, chunk.source_range.length);
            append_string(payload, chunk.text);
            append_metadata(payload, chunk.metadata);
            return payload;
        }

        DocumentChunk deserialize_chunk(std::string_view payload) {
            PayloadReader reader{payload};
            if(reader.read_string() != CHUNK_PAYLOAD_VERSION) {
                throw std::runtime_error("Unsupported chunk payload version");
            }

            auto chunk_id = reader.read_string();
            auto document_id = reader.read_string();
            const auto offset = reader.read_size();
            const auto length = reader.read_size();
            auto text = reader.read_string();
            auto metadata = read_metadata(reader);
            reader.require_end();

            DocumentChunk chunk{
                ChunkId{std::move(chunk_id)},
                DocumentId{std::move(document_id)},
                TextRange{offset, length},
                std::move(text),
                std::move(metadata)
            };
            return chunk;
        }

        std::string serialize_string_list(const std::vector<std::string>& values) {
            std::string payload;
            append_string(payload, STRING_LIST_PAYLOAD_VERSION);
            append_size(payload, values.size());
            for(const auto& value : values) {
                append_string(payload, value);
            }
            return payload;
        }

        std::vector<std::string> deserialize_string_list(std::string_view payload) {
            PayloadReader reader{payload};
            if(reader.read_string() != STRING_LIST_PAYLOAD_VERSION) {
                throw std::runtime_error("Unsupported string list payload version");
            }

            std::vector<std::string> values;
            const auto count = reader.read_size();
            values.reserve(count);
            for(std::size_t i = 0; i < count; ++i) {
                values.push_back(reader.read_string());
            }
            reader.require_end();
            return values;
        }

        void validate_snapshot(const DocumentSnapshot& snapshot) {
            for(const auto& chunk : snapshot.chunks) {
                if(chunk.document_id != snapshot.document.id) {
                    throw std::invalid_argument(
                        "DocumentSnapshot chunks must belong to snapshot.document"
                    );
                }
            }
        }

        mdbxc::Config make_config(const MdbxDocumentStorageOptions& options) {
            if(options.path.empty()) {
                throw std::invalid_argument("MdbxDocumentStorageOptions::path must not be empty");
            }

            mdbxc::Config config;
            config.pathname = options.path;
            config.max_dbs = 16;
            config.no_subdir = true;
            config.relative_to_exe = options.relative_to_exe;
            return config;
        }

    } // namespace

    class MdbxDocumentStorage::Impl final {
    public:
        explicit Impl(MdbxDocumentStorageOptions options)
            : m_table_prefix(sanitize_table_part(std::move(options.table_prefix)))
            , m_connection(mdbxc::Connection::create(make_config(options)))
            , m_documents(m_connection, table_name(m_table_prefix, "documents"))
            , m_chunks(m_connection, table_name(m_table_prefix, "chunks"))
            , m_document_chunks(m_connection, table_name(m_table_prefix, "document_chunks")) {}

        void upsert_document(DocumentSnapshot snapshot) {
            validate_snapshot(snapshot);

            auto txn = m_connection->transaction(mdbxc::TransactionMode::WRITABLE);
            erase_chunks_for(snapshot.document.id, txn);

            std::vector<std::string> chunk_ids;
            chunk_ids.reserve(snapshot.chunks.size());
            for(const auto& chunk : snapshot.chunks) {
                chunk_ids.push_back(chunk.id.value());
                m_chunks.insert_or_assign(chunk.id.value(), serialize_chunk(chunk), txn);
            }

            m_documents.insert_or_assign(
                snapshot.document.id.value(),
                serialize_document(snapshot.document),
                txn
            );
            m_document_chunks.insert_or_assign(
                snapshot.document.id.value(),
                serialize_string_list(chunk_ids),
                txn
            );
            txn.commit();
        }

        [[nodiscard]] std::optional<Document> find_document(const DocumentId& id) const {
            const auto payload = m_documents.find(id.value());
            if(!payload) {
                return std::nullopt;
            }
            return deserialize_document(*payload);
        }

        [[nodiscard]] std::optional<DocumentChunk> find_chunk(const ChunkId& id) const {
            const auto payload = m_chunks.find(id.value());
            if(!payload) {
                return std::nullopt;
            }
            return deserialize_chunk(*payload);
        }

        [[nodiscard]] std::vector<DocumentChunk> list_chunks(const DocumentId& document_id) const {
            auto txn = m_connection->transaction(mdbxc::TransactionMode::READ_ONLY);
            const auto payload = m_document_chunks.find(document_id.value(), txn);
            if(!payload) {
                return {};
            }

            const auto chunk_ids = deserialize_string_list(*payload);
            std::vector<DocumentChunk> chunks;
            chunks.reserve(chunk_ids.size());
            for(const auto& chunk_id : chunk_ids) {
                const auto chunk_payload = m_chunks.find(chunk_id, txn);
                if(!chunk_payload) {
                    throw std::runtime_error(
                        "MdbxDocumentStorage chunk list references missing chunk payload"
                    );
                }

                auto chunk = deserialize_chunk(*chunk_payload);
                if(chunk.document_id != document_id) {
                    throw std::runtime_error(
                        "MdbxDocumentStorage chunk list references chunk from another document"
                    );
                }
                chunks.push_back(std::move(chunk));
            }
            return chunks;
        }

        [[nodiscard]] bool erase_document(const DocumentId& id) {
            auto txn = m_connection->transaction(mdbxc::TransactionMode::WRITABLE);
            const bool removed_chunks = erase_chunks_for(id, txn) > 0;
            const bool removed_document = m_documents.erase(id.value(), txn);
            const bool removed_chunk_list = m_document_chunks.erase(id.value(), txn);
            txn.commit();
            return removed_document || removed_chunk_list || removed_chunks;
        }

    private:
        std::size_t erase_chunks_for(const DocumentId& id, const mdbxc::Transaction& txn) {
            const auto payload = m_document_chunks.find(id.value(), txn);
            if(!payload) {
                return 0;
            }

            std::size_t removed_count = 0;
            for(const auto& chunk_id : deserialize_string_list(*payload)) {
                if(m_chunks.erase(chunk_id, txn)) {
                    ++removed_count;
                }
            }
            m_document_chunks.erase(id.value(), txn);
            return removed_count;
        }

        std::string m_table_prefix;
        std::shared_ptr<mdbxc::Connection> m_connection;
        mdbxc::KeyValueTable<std::string, std::string> m_documents;
        mdbxc::KeyValueTable<std::string, std::string> m_chunks;
        mdbxc::KeyValueTable<std::string, std::string> m_document_chunks;
    };

    MdbxDocumentStorage::MdbxDocumentStorage(MdbxDocumentStorageOptions options)
        : m_impl(std::make_unique<Impl>(std::move(options))) {}

    MdbxDocumentStorage::~MdbxDocumentStorage() = default;

    MdbxDocumentStorage::MdbxDocumentStorage(MdbxDocumentStorage&& other) noexcept = default;

    MdbxDocumentStorage& MdbxDocumentStorage::operator=(
        MdbxDocumentStorage&& other
    ) noexcept = default;

    void MdbxDocumentStorage::upsert_document(DocumentSnapshot snapshot) {
        m_impl->upsert_document(std::move(snapshot));
    }

    std::optional<Document> MdbxDocumentStorage::find_document(const DocumentId& id) const {
        return m_impl->find_document(id);
    }

    std::optional<DocumentChunk> MdbxDocumentStorage::find_chunk(const ChunkId& id) const {
        return m_impl->find_chunk(id);
    }

    std::vector<DocumentChunk> MdbxDocumentStorage::list_chunks(
        const DocumentId& document_id
    ) const {
        return m_impl->list_chunks(document_id);
    }

    bool MdbxDocumentStorage::erase_document(const DocumentId& id) {
        return m_impl->erase_document(id);
    }

} // namespace agent_memory

#endif // AGENT_MEMORY_HAS_MDBX
