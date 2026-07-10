#include <agent_memory.hpp>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    agent_memory::Metadata make_metadata(std::string scope) {
        agent_memory::Metadata metadata;
        metadata.set("scope", std::move(scope));
        return metadata;
    }

    agent_memory::DocumentChunk make_chunk() {
        return agent_memory::DocumentChunk{
            agent_memory::ChunkId{"chunk:alpha"},
            agent_memory::DocumentId{"doc:alpha"},
            agent_memory::TextRange{0, 12},
            "retrieved text",
            make_metadata("documents")
        };
    }

    class StaticRetriever final : public agent_memory::IRetriever {
    public:
        explicit StaticRetriever(agent_memory::RetrievalResult result)
            : m_result(std::move(result)) {}

        [[nodiscard]] agent_memory::RetrievalResult retrieve(
            const agent_memory::RetrievalQuery& query
        ) const override {
            last_query = query;
            if(query.limit == 0) {
                return {};
            }
            return m_result;
        }

        mutable std::optional<agent_memory::RetrievalQuery> last_query;

    private:
        agent_memory::RetrievalResult m_result;
    };

} // namespace

int main() {
    agent_memory::RetrievalQuery empty_query;
    if(empty_query.limit != 10) {
        return fail("retrieval query must default to ten results");
    }

    if(empty_query.has_text() || empty_query.has_embedding()) {
        return fail("empty retrieval query must expose no query signal");
    }

    empty_query.embedding = agent_memory::Embedding{};
    if(empty_query.has_embedding()) {
        return fail("retrieval query must ignore empty embeddings");
    }

    agent_memory::RetrievalQuery query;
    query.text = "memory systems";
    query.embedding = agent_memory::Embedding{{1.0F, 0.0F, 0.5F}};
    query.limit = 3;
    query.metadata_filters.push_back(agent_memory::MetadataFilter{"scope", "documents"});

    if(!query.has_text() || !query.has_embedding()) {
        return fail("retrieval query must expose text and embedding signals");
    }

    agent_memory::Metadata hit_metadata;
    hit_metadata.set("stage", "exact");

    agent_memory::RetrievalResult result;
    if(!result.empty() || result.size() != 0) {
        return fail("empty retrieval result must report zero chunks");
    }

    result.chunks.push_back(agent_memory::RetrievedChunk{
        make_chunk(),
        0.75F,
        hit_metadata
    });

    if(result.empty() || result.size() != 1) {
        return fail("retrieval result must report retrieved chunk count");
    }

    StaticRetriever retriever(result);
    const agent_memory::RetrievalQuery zero_limit_query{
        "memory systems",
        agent_memory::Embedding{{1.0F, 0.0F, 0.5F}},
        0,
        {}
    };
    const auto zero_limit_result = retriever.retrieve(zero_limit_query);
    if(!zero_limit_result.empty()) {
        return fail("retriever contract must treat zero limit as an empty result request");
    }

    const auto retrieved = retriever.retrieve(query);

    if(!retriever.last_query || retriever.last_query->limit != 3) {
        return fail("retriever contract must receive query fields");
    }

    if(retriever.last_query->metadata_filters.size() != 1) {
        return fail("retrieval query must carry metadata filters");
    }

    if(retrieved.size() != 1 || retrieved.chunks.front().score != 0.75F) {
        return fail("retriever contract must return scored chunks");
    }

    if(retrieved.chunks.front().chunk.id.value() != "chunk:alpha") {
        return fail("retrieved chunk must expose the original chunk payload");
    }

    const auto stage = retrieved.chunks.front().metadata.get("stage");
    if(!stage || *stage != "exact") {
        return fail("retrieved chunk must carry retrieval metadata");
    }

    return 0;
}
