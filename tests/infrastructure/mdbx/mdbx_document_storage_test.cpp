#include <agent_memory/infrastructure/mdbx/MdbxDocumentStorage.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef AGENT_MEMORY_HAS_MDBX
#   error "AGENT_MEMORY_HAS_MDBX must be defined by the agent_memory target"
#endif

#if !AGENT_MEMORY_HAS_MDBX
#   error "MDBX document storage test requires AGENT_MEMORY_ENABLE_MDBX=ON"
#endif

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    agent_memory::Metadata make_metadata(std::string scope) {
        agent_memory::Metadata metadata;
        metadata.set("scope", std::move(scope));
        metadata.set("format", "markdown");
        return metadata;
    }

    agent_memory::Document make_document(
        agent_memory::DocumentId id,
        std::string text
    ) {
        return agent_memory::Document{
            std::move(id),
            agent_memory::SourceKind::Markdown,
            "notes/mdbx.md",
            std::move(text),
            make_metadata("documents")
        };
    }

    agent_memory::DocumentChunk make_chunk(
        agent_memory::ChunkId id,
        const agent_memory::DocumentId& document_id,
        std::size_t offset,
        std::string text
    ) {
        agent_memory::Metadata metadata;
        metadata.set("scope", "documents");
        metadata.set("chunk", id.value());
        return agent_memory::DocumentChunk{
            std::move(id),
            document_id,
            agent_memory::TextRange{offset, text.size()},
            std::move(text),
            std::move(metadata)
        };
    }

    std::filesystem::path test_database_path() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto random_suffix = std::random_device{}();
        return std::filesystem::temp_directory_path() / (
            "agent_memory_mdbx_document_storage_" +
            std::to_string(now) +
            "_" +
            std::to_string(random_suffix) +
            ".mdbx"
        );
    }

    class DatabaseFileCleanup final {
    public:
        explicit DatabaseFileCleanup(std::filesystem::path path)
            : m_path(std::move(path)) {}

        ~DatabaseFileCleanup() {
            std::error_code error;
            std::filesystem::remove(m_path, error);
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return m_path;
        }

    private:
        std::filesystem::path m_path;
    };

} // namespace

int main() {
    const DatabaseFileCleanup database_file{test_database_path()};
    const auto& database_path = database_file.path();

    const agent_memory::DocumentId document_id{"doc:mdbx"};
    const agent_memory::ChunkId old_chunk_id{"chunk:mdbx:0"};
    const agent_memory::ChunkId replacement_chunk_id{"chunk:mdbx:new"};

    {
        agent_memory::MdbxDocumentStorage storage(agent_memory::MdbxDocumentStorageOptions{
            database_path.string(),
            "agent_memory_test",
            false
        });

        const auto first_chunk = make_chunk(old_chunk_id, document_id, 0, "# MDBX");
        const auto second_chunk = make_chunk(
            agent_memory::ChunkId{"chunk:mdbx:1"},
            document_id,
            7,
            "Initial content."
        );
        if(!first_chunk.metadata.get("chunk")) {
            return fail("test fixture must create chunk metadata");
        }

        try {
            storage.upsert_document(agent_memory::DocumentSnapshot{
                make_document(document_id, "Bad snapshot."),
                {
                    make_chunk(
                        agent_memory::ChunkId{"chunk:mdbx:wrong-doc"},
                        agent_memory::DocumentId{"doc:other"},
                        0,
                        "Bad chunk."
                    )
                }
            });
            return fail("MDBX storage must reject chunks from another document");
        } catch(const std::invalid_argument&) {
        }

        storage.upsert_document(agent_memory::DocumentSnapshot{
            make_document(document_id, "# MDBX\nInitial content."),
            {first_chunk, second_chunk}
        });

        const auto stored_document = storage.find_document(document_id);
        if(!stored_document || stored_document->text != "# MDBX\nInitial content.") {
            return fail("MDBX storage must return the inserted document");
        }

        const auto stored_chunks = storage.list_chunks(document_id);
        if(stored_chunks.size() != 2) {
            return fail("MDBX storage must return all chunks for a document");
        }

        const auto old_chunk = storage.find_chunk(old_chunk_id);
        if(!old_chunk) {
            return fail("MDBX storage must find chunks by id");
        }

        const auto old_chunk_marker = old_chunk->metadata.get("chunk");
        if(!old_chunk_marker) {
            return fail("MDBX storage must restore chunk metadata");
        }

        if(*old_chunk_marker != old_chunk_id.value()) {
            std::cerr
                << "expected chunk marker " << old_chunk_id.value()
                << ", got " << *old_chunk_marker << '\n';
            return fail("MDBX storage must restore exact chunk metadata");
        }

        storage.upsert_document(agent_memory::DocumentSnapshot{
            make_document(document_id, "# MDBX\nUpdated content."),
            {
                make_chunk(replacement_chunk_id, document_id, 0, "Updated content.")
            }
        });

        if(storage.find_chunk(old_chunk_id)) {
            return fail("MDBX upsert must remove chunks replaced for the same document");
        }
    }

    {
        agent_memory::MdbxDocumentStorage storage(agent_memory::MdbxDocumentStorageOptions{
            database_path.string(),
            "agent_memory_test",
            false
        });

        const auto stored_document = storage.find_document(document_id);
        if(!stored_document || stored_document->text != "# MDBX\nUpdated content.") {
            return fail("MDBX storage must persist documents across reopen");
        }

        const auto replacement_chunks = storage.list_chunks(document_id);
        if(
            replacement_chunks.size() != 1 ||
            replacement_chunks.front().id != replacement_chunk_id
        ) {
            return fail("MDBX storage must persist replacement chunk state");
        }

        if(!storage.erase_document(document_id)) {
            return fail("MDBX erase must report removed document state");
        }

        if(storage.find_document(document_id) || !storage.list_chunks(document_id).empty()) {
            return fail("MDBX erase must remove document and chunks");
        }

        if(storage.erase_document(document_id)) {
            return fail("MDBX erase of missing document must report false");
        }
    }

    return 0;
}
