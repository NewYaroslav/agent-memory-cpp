#include <agent_memory/AgentMemory.hpp>

#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    class InMemoryVectorIndex final : public agent_memory::IVectorIndex {
    public:
        [[nodiscard]] agent_memory::SimilarityMetric similarity_metric() const noexcept override {
            return agent_memory::SimilarityMetric::DotProduct;
        }

        [[nodiscard]] std::size_t dimension() const noexcept override {
            return 3;
        }

        [[nodiscard]] std::size_t size() const noexcept override {
            return m_records.size();
        }

        void upsert(agent_memory::VectorRecord record) override {
            if(record.embedding.dimension() != dimension()) {
                throw std::invalid_argument("record dimension mismatch");
            }

            const agent_memory::ChunkId chunk_id = record.chunk_id;
            m_records[chunk_id] = std::move(record);
        }

        [[nodiscard]] std::optional<agent_memory::VectorRecord> find(
            const agent_memory::ChunkId& chunk_id
        ) const override {
            const auto it = m_records.find(chunk_id);
            if(it == m_records.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        [[nodiscard]] std::vector<agent_memory::VectorSearchResult> search(
            const agent_memory::VectorSearchQuery& query
        ) const override {
            std::vector<agent_memory::VectorSearchResult> results;
            if(query.limit == 0) {
                return results;
            }

            if(query.embedding.dimension() != dimension()) {
                throw std::invalid_argument("query dimension mismatch");
            }

            for(const auto& item : m_records) {
                const auto& record = item.second;
                if(!agent_memory::matches_metadata_filters(record.metadata, query.metadata_filters)) {
                    continue;
                }

                results.push_back(agent_memory::VectorSearchResult{
                    record.chunk_id,
                    dot_product(query.embedding, record.embedding),
                    record.metadata
                });
            }

            std::sort(
                results.begin(),
                results.end(),
                [](const auto& lhs, const auto& rhs) {
                    if(lhs.score == rhs.score) {
                        return lhs.chunk_id < rhs.chunk_id;
                    }
                    return lhs.score > rhs.score;
                }
            );

            if(results.size() > query.limit) {
                results.resize(query.limit);
            }
            return results;
        }

        [[nodiscard]] bool erase(const agent_memory::ChunkId& chunk_id) override {
            return m_records.erase(chunk_id) > 0;
        }

        void clear() override {
            m_records.clear();
        }

    private:
        static float dot_product(
            const agent_memory::Embedding& lhs,
            const agent_memory::Embedding& rhs
        ) {
            float score = 0.0F;
            for(std::size_t i = 0; i < lhs.values.size(); ++i) {
                score += lhs.values[i] * rhs.values[i];
            }
            return score;
        }

        std::map<agent_memory::ChunkId, agent_memory::VectorRecord> m_records;
    };

    agent_memory::Metadata make_metadata(std::string scope) {
        agent_memory::Metadata metadata;
        metadata.set("scope", std::move(scope));
        return metadata;
    }

    agent_memory::VectorRecord make_record(
        std::string chunk_id,
        std::vector<float> values,
        std::string scope
    ) {
        return agent_memory::VectorRecord{
            agent_memory::ChunkId{std::move(chunk_id)},
            agent_memory::Embedding{std::move(values)},
            make_metadata(std::move(scope))
        };
    }

} // namespace

int main() {
    const auto documents_metadata = make_metadata("documents");
    if(!agent_memory::matches_metadata_filters(documents_metadata, {})) {
        return fail("metadata filter helper must accept empty filter lists");
    }

    if(!agent_memory::matches_metadata_filters(
        documents_metadata,
        {agent_memory::MetadataFilter{"scope", "documents"}}
    )) {
        return fail("metadata filter helper must match exact key-value pairs");
    }

    if(agent_memory::matches_metadata_filters(
        documents_metadata,
        {agent_memory::MetadataFilter{"scope", "chat"}}
    )) {
        return fail("metadata filter helper must reject non-matching values");
    }

    if(agent_memory::matches_metadata_filters(
        documents_metadata,
        {agent_memory::MetadataFilter{"missing", "documents"}}
    )) {
        return fail("metadata filter helper must reject missing keys");
    }

    agent_memory::Metadata multi_filter_metadata = make_metadata("documents");
    multi_filter_metadata.set("source", "markdown");
    if(!agent_memory::matches_metadata_filters(
        multi_filter_metadata,
        {
            agent_memory::MetadataFilter{"scope", "documents"},
            agent_memory::MetadataFilter{"source", "markdown"}
        }
    )) {
        return fail("metadata filter helper must require every filter to match");
    }

    InMemoryVectorIndex index;
    if(index.similarity_metric() != agent_memory::SimilarityMetric::DotProduct) {
        return fail("vector index must expose its similarity metric");
    }

    if(index.dimension() != 3 || index.size() != 0) {
        return fail("new vector index must expose dimension and empty size");
    }

    index.upsert(make_record("chunk:alpha", {1.0F, 0.0F, 0.0F}, "documents"));
    index.upsert(make_record("chunk:beta", {0.0F, 1.0F, 0.0F}, "chat"));
    index.upsert(make_record("chunk:gamma", {0.5F, 0.0F, 0.0F}, "documents"));

    if(index.size() != 3) {
        return fail("upsert must increase vector index size for new chunks");
    }

    const auto stored = index.find(agent_memory::ChunkId{"chunk:alpha"});
    if(!stored || stored->embedding.dimension() != 3) {
        return fail("vector index must find stored records by chunk id");
    }

    const auto results = index.search(agent_memory::VectorSearchQuery{
        agent_memory::Embedding{{1.0F, 0.0F, 0.0F}},
        2,
        {agent_memory::MetadataFilter{"scope", "documents"}}
    });

    if(results.size() != 2) {
        return fail("vector search must apply result limit and metadata filters");
    }

    if(
        results[0].chunk_id.value() != "chunk:alpha" ||
        results[1].chunk_id.value() != "chunk:gamma"
    ) {
        return fail("vector search must return best scored hits in order");
    }

    if(results[0].score != 1.0F || results[1].score != 0.5F) {
        return fail("vector search results must expose scores");
    }

    index.upsert(make_record("chunk:alpha", {0.0F, 0.0F, 1.0F}, "updated"));
    if(index.size() != 3) {
        return fail("upsert must replace existing chunks without increasing size");
    }

    const auto replaced = index.find(agent_memory::ChunkId{"chunk:alpha"});
    const auto replaced_scope = replaced ? replaced->metadata.get("scope") : std::nullopt;
    if(!replaced_scope || *replaced_scope != "updated") {
        return fail("upsert must replace stored vector records by chunk id");
    }

    try {
        index.upsert(make_record("chunk:bad", {1.0F, 0.0F}, "bad"));
        return fail("upsert must reject record dimension mismatches");
    } catch(const std::invalid_argument&) {
    }

    try {
        (void)index.search(agent_memory::VectorSearchQuery{
            agent_memory::Embedding{{1.0F, 0.0F}},
            1,
            {}
        });
        return fail("search must reject query dimension mismatches");
    } catch(const std::invalid_argument&) {
    }

    if(!index.erase(agent_memory::ChunkId{"chunk:beta"})) {
        return fail("erase must report removed vector records");
    }

    if(index.find(agent_memory::ChunkId{"chunk:beta"})) {
        return fail("erase must remove vector records");
    }

    index.clear();
    if(index.size() != 0) {
        return fail("clear must remove all vector records");
    }

    return 0;
}
