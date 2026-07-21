#include <agent_memory/eval/PrecomputedEmbeddingDataset.hpp>

#include <stdexcept>
#include <string>

namespace {

    [[nodiscard]] const char* valid_dataset_json() noexcept {
        return R"json(
{
  "schema_version": 1,
  "name": "precomputed-loader-test",
  "embedding_model": {
    "model_id": "fixture-embedding-v1",
    "dimension": 3,
    "similarity_metric": "cosine",
    "pooling_mode": "mean",
    "normalized": true
  },
  "corpus": [
    {"id": "doc:a", "title": "A", "text": "alpha"},
    {"id": "doc:b", "title": "B", "text": "beta"}
  ],
  "queries": [
    {"id": "q:a", "text": "alpha query", "query_type": "semantic", "limit": 2}
  ],
  "judgments": [
    {"query_id": "q:a", "item_id": "doc:a", "relevance_grade": 3}
  ],
  "document_embeddings": [
    {"id": "doc:a", "vector": [1.0, 0.0, 0.0]},
    {"id": "doc:b", "vector": [0.0, 1.0, 0.0]}
  ],
  "query_embeddings": [
    {"id": "q:a", "vector": [1.0, 0.0, 0.0]}
  ]
}
)json";
    }

    [[noreturn]] void fail(const char* message) {
        throw std::runtime_error(message);
    }

    template <typename Fn>
    void expect_runtime_error(Fn&& fn, const char* message) {
        try {
            fn();
        } catch(const std::runtime_error&) {
            return;
        }
        fail(message);
    }

} // namespace

int main() {
    const auto dataset =
        agent_memory::load_precomputed_embedding_dataset_from_json_string(
            valid_dataset_json()
        );
    if(dataset.retrieval.name != "precomputed-loader-test") {
        fail("dataset name was not loaded");
    }
    if(dataset.embedding_model.model_id != "fixture-embedding-v1") {
        fail("embedding model id was not loaded");
    }
    if(dataset.embedding_model.dimension != 3) {
        fail("embedding model dimension was not loaded");
    }
    if(!dataset.embedding_model.normalized) {
        fail("embedding model normalized flag was not loaded");
    }
    if(dataset.document_embeddings.size() != 2 || dataset.query_embeddings.size() != 1) {
        fail("embedding record counts are wrong");
    }

    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.document_embeddings.pop_back();
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "missing document embedding must be rejected"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.query_embeddings.clear();
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "missing query embedding must be rejected"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.document_embeddings.push_back(dataset.document_embeddings.front());
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "duplicate document embedding must be rejected"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.query_embeddings.push_back(dataset.query_embeddings.front());
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "duplicate query embedding must be rejected"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.query_embeddings.front().embedding.values.push_back(0.0F);
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "query embedding dimension mismatch must be rejected"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.document_embeddings.front().id = "doc:unknown";
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "unknown embedding id must be rejected"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.document_embeddings.front().embedding.values = {2.0F, 0.0F, 0.0F};
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "normalized document embedding norm mismatch must be rejected"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.query_embeddings.front().embedding.values = {0.5F, 0.0F, 0.0F};
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "normalized query embedding norm mismatch must be rejected"
    );
    {
        auto dataset =
            agent_memory::load_precomputed_embedding_dataset_from_json_string(
                valid_dataset_json()
            );
        dataset.embedding_model.normalized = false;
        dataset.document_embeddings.front().embedding.values = {2.0F, 0.0F, 0.0F};
        dataset.query_embeddings.front().embedding.values = {0.5F, 0.0F, 0.0F};
        agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
    }
}
