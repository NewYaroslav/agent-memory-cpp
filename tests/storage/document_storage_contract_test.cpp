#include <agent_memory/AgentMemory.hpp>

#include <iostream>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    class InMemoryDocumentStorage final : public agent_memory::IDocumentStorage {
    public:
        void upsert_document(agent_memory::DocumentSnapshot snapshot) override {
            erase_chunks_for(snapshot.document.id);

            const agent_memory::DocumentId document_id = snapshot.document.id;
            m_documents[document_id] = std::move(snapshot.document);

            for(auto& chunk : snapshot.chunks) {
                const agent_memory::ChunkId chunk_id = chunk.id;
                m_chunk_ids_by_document[document_id].push_back(chunk_id);
                m_chunks[chunk_id] = std::move(chunk);
            }
        }

        [[nodiscard]] std::optional<agent_memory::Document> find_document(
            const agent_memory::DocumentId& id
        ) const override {
            const auto it = m_documents.find(id);
            if(it == m_documents.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        [[nodiscard]] std::optional<agent_memory::DocumentChunk> find_chunk(
            const agent_memory::ChunkId& id
        ) const override {
            const auto it = m_chunks.find(id);
            if(it == m_chunks.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        [[nodiscard]] std::vector<agent_memory::DocumentChunk> list_chunks(
            const agent_memory::DocumentId& document_id
        ) const override {
            std::vector<agent_memory::DocumentChunk> result;
            const auto ids_it = m_chunk_ids_by_document.find(document_id);
            if(ids_it == m_chunk_ids_by_document.end()) {
                return result;
            }

            result.reserve(ids_it->second.size());
            for(const auto& chunk_id : ids_it->second) {
                const auto chunk_it = m_chunks.find(chunk_id);
                if(chunk_it != m_chunks.end()) {
                    result.push_back(chunk_it->second);
                }
            }
            return result;
        }

        [[nodiscard]] bool erase_document(const agent_memory::DocumentId& id) override {
            const bool removed_document = m_documents.erase(id) > 0;
            const bool removed_chunks = erase_chunks_for(id) > 0;
            return removed_document || removed_chunks;
        }

    private:
        std::size_t erase_chunks_for(const agent_memory::DocumentId& document_id) {
            const auto ids_it = m_chunk_ids_by_document.find(document_id);
            if(ids_it == m_chunk_ids_by_document.end()) {
                return 0;
            }

            std::size_t removed_count = 0;
            for(const auto& chunk_id : ids_it->second) {
                removed_count += m_chunks.erase(chunk_id);
            }
            m_chunk_ids_by_document.erase(ids_it);
            return removed_count;
        }

        std::map<agent_memory::DocumentId, agent_memory::Document> m_documents;
        std::map<agent_memory::ChunkId, agent_memory::DocumentChunk> m_chunks;
        std::map<
            agent_memory::DocumentId,
            std::vector<agent_memory::ChunkId>
        > m_chunk_ids_by_document;
    };

    agent_memory::Document make_document(
        agent_memory::DocumentId id,
        std::string text
    ) {
        return agent_memory::Document{
            std::move(id),
            agent_memory::SourceKind::Markdown,
            "notes/project.md",
            std::move(text),
            {}
        };
    }

    agent_memory::DocumentChunk make_chunk(
        agent_memory::ChunkId id,
        const agent_memory::DocumentId& document_id,
        std::size_t offset,
        std::string text
    ) {
        return agent_memory::DocumentChunk{
            std::move(id),
            document_id,
            agent_memory::TextRange{offset, text.size()},
            std::move(text),
            {}
        };
    }

} // namespace

int main() {
    InMemoryDocumentStorage storage;
    const agent_memory::DocumentId document_id{"doc:storage"};

    storage.upsert_document(agent_memory::DocumentSnapshot{
        make_document(document_id, "# Storage\nInitial content."),
        {
            make_chunk(
                agent_memory::ChunkId{"chunk:storage:0"},
                document_id,
                0,
                "# Storage"
            ),
            make_chunk(
                agent_memory::ChunkId{"chunk:storage:1"},
                document_id,
                10,
                "Initial content."
            )
        }
    });

    const auto stored_document = storage.find_document(document_id);
    if(!stored_document || stored_document->text != "# Storage\nInitial content.") {
        return fail("storage must return the inserted document");
    }

    const auto stored_chunks = storage.list_chunks(document_id);
    if(stored_chunks.size() != 2) {
        return fail("storage must return all chunks for a document");
    }

    const auto first_chunk = storage.find_chunk(agent_memory::ChunkId{"chunk:storage:0"});
    if(!first_chunk || first_chunk->text != "# Storage") {
        return fail("storage must find chunks by id");
    }

    storage.upsert_document(agent_memory::DocumentSnapshot{
        make_document(document_id, "# Storage\nUpdated content."),
        {
            make_chunk(
                agent_memory::ChunkId{"chunk:storage:new"},
                document_id,
                0,
                "Updated content."
            )
        }
    });

    if(storage.find_chunk(agent_memory::ChunkId{"chunk:storage:0"})) {
        return fail("upsert must replace old chunks for the same document");
    }

    const auto replacement_chunks = storage.list_chunks(document_id);
    if(
        replacement_chunks.size() != 1 ||
        replacement_chunks.front().id.value() != "chunk:storage:new"
    ) {
        return fail("upsert must expose replacement chunks");
    }

    if(!storage.erase_document(document_id)) {
        return fail("erase must report removed document state");
    }

    if(storage.find_document(document_id) || !storage.list_chunks(document_id).empty()) {
        return fail("erase must remove document and chunks");
    }

    if(storage.erase_document(document_id)) {
        return fail("erase of missing document must report false");
    }

    return 0;
}
