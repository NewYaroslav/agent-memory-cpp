#include "ResourceIndexer.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace agent_memory {

    namespace {

        void validate_resource_index_snapshot(const ResourceIndexSnapshot& snapshot) {
            if(snapshot.revision.resource_id.empty()) {
                throw std::invalid_argument("ResourceIndexSnapshot resource id must not be empty");
            }

            if(snapshot.document_snapshot.document.id.empty()) {
                throw std::invalid_argument("ResourceIndexSnapshot document id must not be empty");
            }

            for(const auto& chunk : snapshot.document_snapshot.chunks) {
                if(chunk.document_id != snapshot.document_snapshot.document.id) {
                    throw std::invalid_argument(
                        "ResourceIndexSnapshot chunks must belong to snapshot document"
                    );
                }
            }
        }

        ResourceManifest make_manifest(const ResourceIndexSnapshot& snapshot) {
            ResourceManifest manifest;
            manifest.revision = snapshot.revision;
            manifest.records.push_back(DerivedRecordRef{
                DerivedRecordKind::Document,
                {},
                snapshot.document_snapshot.document.id.value(),
                0
            });

            std::uint32_t ordinal = 0;
            for(const auto& chunk : snapshot.document_snapshot.chunks) {
                manifest.records.push_back(DerivedRecordRef{
                    DerivedRecordKind::Chunk,
                    chunk.id,
                    {},
                    ordinal
                });
                manifest.records.push_back(DerivedRecordRef{
                    DerivedRecordKind::Embedding,
                    chunk.id,
                    {},
                    ordinal
                });
                manifest.records.push_back(DerivedRecordRef{
                    DerivedRecordKind::VectorRecord,
                    chunk.id,
                    {},
                    ordinal
                });
                ++ordinal;
            }
            return manifest;
        }

        std::vector<VectorRecord> make_vector_records(
            const DocumentSnapshot& snapshot,
            IEmbedder& embedder
        ) {
            std::vector<VectorRecord> records;
            records.reserve(snapshot.chunks.size());
            for(const auto& chunk : snapshot.chunks) {
                auto embedding = embedder.embed(EmbeddingRequest{
                    chunk.text,
                    EmbeddingPurpose::Document
                });
                records.push_back(VectorRecord{
                    chunk.id,
                    std::move(embedding),
                    chunk.metadata
                });
            }
            return records;
        }

    } // namespace

    IResourceIndexer::~IResourceIndexer() = default;

    ResourceIndexer::ResourceIndexer(
        IDocumentStorage& document_storage,
        IResourceManifestStorage& manifest_storage,
        IEmbedder& embedder,
        IVectorIndex& vector_index
    )
        : m_document_storage(&document_storage)
        , m_manifest_storage(&manifest_storage)
        , m_embedder(&embedder)
        , m_vector_index(&vector_index) {}

    void ResourceIndexer::reindex_resource(ResourceIndexSnapshot snapshot) {
        validate_resource_index_snapshot(snapshot);

        auto manifest = make_manifest(snapshot);
        if(!is_valid_resource_manifest(manifest)) {
            throw std::invalid_argument("ResourceIndexSnapshot produced invalid manifest");
        }

        auto vector_records = make_vector_records(snapshot.document_snapshot, *m_embedder);

        const auto old_manifest = m_manifest_storage->find_manifest(
            snapshot.revision.resource_id
        );
        if(old_manifest) {
            erase_derived_records(*old_manifest);
        }

        m_document_storage->upsert_document(std::move(snapshot.document_snapshot));
        for(auto& record : vector_records) {
            m_vector_index->upsert(std::move(record));
        }
        m_manifest_storage->upsert_manifest(std::move(manifest));
    }

    bool ResourceIndexer::erase_resource(const ResourceId& resource_id) {
        const auto manifest = m_manifest_storage->find_manifest(resource_id);
        if(!manifest) {
            return false;
        }

        erase_derived_records(*manifest);
        m_manifest_storage->erase_manifest(resource_id);
        return true;
    }

    void ResourceIndexer::erase_derived_records(const ResourceManifest& manifest) {
        for(const auto& record : manifest.records) {
            if(record.kind == DerivedRecordKind::Document && !record.key.empty()) {
                m_document_storage->erase_document(DocumentId{record.key});
            }

            if(record.kind == DerivedRecordKind::VectorRecord && !record.chunk_id.empty()) {
                m_vector_index->erase(record.chunk_id);
            }
        }
    }

} // namespace agent_memory
