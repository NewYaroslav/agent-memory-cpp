#include <agent_memory/AgentMemory.hpp>

#include <iostream>
#include <string_view>
#include <vector>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    class FakeEmbedder final : public agent_memory::IEmbedder {
    public:
        FakeEmbedder() {
            m_info.model_id = "fake-embedding-model";
            m_info.dimension = 3;
            m_info.max_tokens = 128;
            m_info.similarity_metric = agent_memory::SimilarityMetric::Cosine;
            m_info.pooling_mode = agent_memory::PoolingMode::Mean;
            m_info.normalized = true;
        }

        [[nodiscard]] const agent_memory::EmbeddingModelInfo& info() const noexcept override {
            return m_info;
        }

        [[nodiscard]] agent_memory::Embedding embed(
            const agent_memory::EmbeddingRequest& request
        ) override {
            return agent_memory::Embedding{{
                purpose_bias(request.purpose),
                static_cast<float>(request.text.size()),
                m_info.normalized ? 1.0F : 0.0F
            }};
        }

    private:
        static float purpose_bias(agent_memory::EmbeddingPurpose purpose) noexcept {
            switch(purpose) {
                case agent_memory::EmbeddingPurpose::Query:
                    return 1.0F;
                case agent_memory::EmbeddingPurpose::Document:
                    return 2.0F;
                case agent_memory::EmbeddingPurpose::Symmetric:
                    return 3.0F;
                case agent_memory::EmbeddingPurpose::Classification:
                    return 4.0F;
                case agent_memory::EmbeddingPurpose::Custom:
                    return 5.0F;
            }
            return 0.0F;
        }

        agent_memory::EmbeddingModelInfo m_info;
    };

} // namespace

int main() {
    agent_memory::Embedding embedding{{0.5F, 0.25F, 0.125F}};
    if(embedding.empty()) {
        return fail("embedding with values must not be empty");
    }

    if(embedding.dimension() != 3) {
        return fail("embedding dimension must match value count");
    }

    agent_memory::EmbeddingPurpose parsed_purpose = agent_memory::EmbeddingPurpose::Custom;
    if(!agent_memory::parse_embedding_purpose("Document", parsed_purpose)) {
        return fail("embedding purpose parser must accept mixed-case input");
    }

    if(parsed_purpose != agent_memory::EmbeddingPurpose::Document) {
        return fail("embedding purpose parser returned unexpected value");
    }

    if(agent_memory::to_string(parsed_purpose) != "document") {
        return fail("embedding purpose names must be stable lowercase strings");
    }

    agent_memory::SimilarityMetric parsed_metric = agent_memory::SimilarityMetric::Euclidean;
    if(!agent_memory::parse_similarity_metric("dot_product", parsed_metric)) {
        return fail("similarity metric parser must accept stable lowercase input");
    }

    if(parsed_metric != agent_memory::SimilarityMetric::DotProduct) {
        return fail("similarity metric parser returned unexpected value");
    }

    agent_memory::PoolingMode parsed_pooling = agent_memory::PoolingMode::Custom;
    if(!agent_memory::parse_pooling_mode("Last_Token", parsed_pooling)) {
        return fail("pooling mode parser must accept mixed-case input");
    }

    if(parsed_pooling != agent_memory::PoolingMode::LastToken) {
        return fail("pooling mode parser returned unexpected value");
    }

    FakeEmbedder embedder;
    const auto& info = embedder.info();
    if(
        info.model_id != "fake-embedding-model" ||
        info.dimension != 3 ||
        info.similarity_metric != agent_memory::SimilarityMetric::Cosine ||
        info.pooling_mode != agent_memory::PoolingMode::Mean ||
        !info.normalized
    ) {
        return fail("embedder info must expose stable model metadata");
    }

    const auto query_embedding = embedder.embed(agent_memory::EmbeddingRequest{
        "memory query",
        agent_memory::EmbeddingPurpose::Query
    });

    if(query_embedding.dimension() != info.dimension || query_embedding.values.front() != 1.0F) {
        return fail("single embed must honor request purpose and model dimension");
    }

    const auto batch = embedder.embed_batch({
        {"first document", agent_memory::EmbeddingPurpose::Document},
        {"classification item", agent_memory::EmbeddingPurpose::Classification}
    });

    if(batch.size() != 2) {
        return fail("batch embed must return one embedding per request");
    }

    if(batch[0].values.front() != 2.0F || batch[1].values.front() != 4.0F) {
        return fail("batch embed must preserve request order and purpose");
    }

    return 0;
}
