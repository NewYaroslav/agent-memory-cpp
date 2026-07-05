#include <agent_memory/AgentMemory.hpp>

#include <iostream>
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

    template <typename Fn>
    bool throws_invalid_argument(Fn&& fn) {
        try {
            fn();
        } catch(const std::invalid_argument&) {
            return true;
        }
        return false;
    }

} // namespace

int main() {
    agent_memory::ExactVectorIndex index(agent_memory::ExactVectorIndexOptions{
        3,
        agent_memory::SimilarityMetric::Cosine
    });

    if(index.dimension() != 3 || index.similarity_metric() != agent_memory::SimilarityMetric::Cosine) {
        return fail("exact vector index must expose configured dimension and metric");
    }

    index.upsert(make_record("chunk:alpha", {1.0F, 0.0F, 0.0F}, "documents"));
    index.upsert(make_record("chunk:beta", {0.0F, 1.0F, 0.0F}, "chat"));
    index.upsert(make_record("chunk:gamma", {0.5F, 0.5F, 0.0F}, "documents"));

    if(index.size() != 3) {
        return fail("exact vector index must count inserted records");
    }

    const auto stored = index.find(agent_memory::ChunkId{"chunk:alpha"});
    if(!stored || stored->embedding.dimension() != 3) {
        return fail("exact vector index must find inserted records");
    }

    const auto cosine_results = index.search(agent_memory::VectorSearchQuery{
        agent_memory::Embedding{{1.0F, 0.0F, 0.0F}},
        2,
        {agent_memory::MetadataFilter{"scope", "documents"}}
    });

    if(cosine_results.size() != 2) {
        return fail("exact vector search must apply limit and metadata filters");
    }

    if(
        cosine_results[0].chunk_id.value() != "chunk:alpha" ||
        cosine_results[1].chunk_id.value() != "chunk:gamma"
    ) {
        return fail("cosine search must rank exact neighbours first");
    }

    if(cosine_results[0].score <= cosine_results[1].score) {
        return fail("cosine search scores must be ordered descending");
    }

    index.upsert(make_record("chunk:alpha", {0.0F, 1.0F, 0.0F}, "documents"));
    if(index.size() != 3) {
        return fail("upsert with existing chunk id must replace without growing");
    }

    if(!index.erase(agent_memory::ChunkId{"chunk:beta"})) {
        return fail("erase must report removed records");
    }

    if(index.find(agent_memory::ChunkId{"chunk:beta"})) {
        return fail("erase must remove records");
    }

    agent_memory::ExactVectorIndex dynamic_index;
    const auto empty_dynamic_results = dynamic_index.search(agent_memory::VectorSearchQuery{
        agent_memory::Embedding{{1.0F, 0.0F}},
        10,
        {}
    });
    if(dynamic_index.dimension() != 0 || !empty_dynamic_results.empty()) {
        return fail("empty dynamic exact vector index must accept queries and return no results");
    }

    dynamic_index.upsert(make_record("chunk:dynamic", {1.0F, 0.0F}, "dynamic"));
    if(dynamic_index.dimension() != 2) {
        return fail("dynamic exact vector index must adopt first record dimension");
    }

    if(!throws_invalid_argument([&dynamic_index] {
        dynamic_index.upsert(make_record("chunk:bad", {1.0F, 0.0F, 0.0F}, "dynamic"));
    })) {
        return fail("exact vector index must reject record dimension mismatches");
    }

    if(!throws_invalid_argument([&dynamic_index] {
        (void)dynamic_index.search(agent_memory::VectorSearchQuery{
            agent_memory::Embedding{{1.0F, 0.0F, 0.0F}},
            1,
            {}
        });
    })) {
        return fail("exact vector index must reject query dimension mismatches");
    }

    agent_memory::ExactVectorIndex dot_index(agent_memory::ExactVectorIndexOptions{
        2,
        agent_memory::SimilarityMetric::DotProduct
    });
    dot_index.upsert(make_record("chunk:dot", {2.0F, 0.0F}, "metric"));
    const auto dot_results = dot_index.search(agent_memory::VectorSearchQuery{
        agent_memory::Embedding{{1.0F, 0.0F}},
        1,
        {}
    });
    if(dot_results.empty() || dot_results.front().score != 2.0F) {
        return fail("dot product search must expose dot product scores");
    }

    agent_memory::ExactVectorIndex tie_index(agent_memory::ExactVectorIndexOptions{
        2,
        agent_memory::SimilarityMetric::DotProduct
    });
    tie_index.upsert(make_record("chunk:b", {1.0F, 0.0F}, "tie"));
    tie_index.upsert(make_record("chunk:a", {1.0F, 0.0F}, "tie"));
    const auto tie_results = tie_index.search(agent_memory::VectorSearchQuery{
        agent_memory::Embedding{{1.0F, 0.0F}},
        2,
        {}
    });
    if(
        tie_results.size() != 2 ||
        tie_results[0].chunk_id.value() != "chunk:a" ||
        tie_results[1].chunk_id.value() != "chunk:b"
    ) {
        return fail("exact vector index must break score ties by chunk id");
    }

    agent_memory::ExactVectorIndex euclidean_index(agent_memory::ExactVectorIndexOptions{
        2,
        agent_memory::SimilarityMetric::Euclidean
    });
    euclidean_index.upsert(make_record("chunk:near", {1.0F, 0.0F}, "metric"));
    euclidean_index.upsert(make_record("chunk:far", {2.0F, 0.0F}, "metric"));
    const auto euclidean_results = euclidean_index.search(agent_memory::VectorSearchQuery{
        agent_memory::Embedding{{0.0F, 0.0F}},
        2,
        {}
    });
    if(
        euclidean_results.size() != 2 ||
        euclidean_results[0].chunk_id.value() != "chunk:near" ||
        euclidean_results[1].chunk_id.value() != "chunk:far"
    ) {
        return fail("euclidean search must rank lower distances first");
    }

    index.clear();
    if(index.size() != 0 || index.dimension() != 3) {
        return fail("clear must remove all exact vector index records without changing dimension");
    }

    return 0;
}
