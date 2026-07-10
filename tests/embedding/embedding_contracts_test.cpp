#include <agent_memory.hpp>

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

    const auto parsed_purpose =
        agent_memory::to_enum<agent_memory::EmbeddingPurpose>("Document");
    if(!parsed_purpose.success) {
        return fail("embedding purpose parser must accept mixed-case input");
    }

    if(parsed_purpose.value != agent_memory::EmbeddingPurpose::Document) {
        return fail("embedding purpose parser returned unexpected value");
    }

    if(agent_memory::to_string(parsed_purpose.value) != "document") {
        return fail("embedding purpose names must be stable lowercase strings");
    }

    const auto parsed_metric =
        agent_memory::to_enum<agent_memory::SimilarityMetric>("dot_product");
    if(!parsed_metric.success) {
        return fail("similarity metric parser must accept stable lowercase input");
    }

    if(parsed_metric.value != agent_memory::SimilarityMetric::DotProduct) {
        return fail("similarity metric parser returned unexpected value");
    }

    const auto parsed_pooling =
        agent_memory::to_enum<agent_memory::PoolingMode>("Last_Token");
    if(!parsed_pooling.success) {
        return fail("pooling mode parser must accept mixed-case input");
    }

    if(parsed_pooling.value != agent_memory::PoolingMode::LastToken) {
        return fail("pooling mode parser returned unexpected value");
    }

    if(agent_memory::to_enum<agent_memory::EmbeddingPurpose>("unknown_purpose")) {
        return fail("unknown embedding purpose must not parse");
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
