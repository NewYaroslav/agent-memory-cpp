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
  "embedding_artifact": {
    "generator_id": "agent-memory.fixture.loader-test",
    "generator_version": "v1",
    "dataset_revision": "unit-test-dataset",
    "generator_revision": "unit-test-generator",
    "model_revision": "unit-test-model",
    "qrels_revision": "unit-test-qrels",
    "document_prompt_id": "unit-test-document-prompt",
    "query_prompt_id": "unit-test-query-prompt",
    "projection_kind": "semantic_axes_3d",
    "normalization": "l2",
    "dtype": "float32",
    "hash_algorithm": "sha256",
    "config_hash": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "artifact_hash": "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
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

    [[nodiscard]] std::string without_line(
        std::string json,
        const std::string& line
    ) {
        const auto offset = json.find(line);
        if(offset == std::string::npos) {
            fail("test fixture line was not found");
        }
        json.erase(offset, line.size());
        return json;
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
    if(!dataset.embedding_artifact) {
        fail("embedding artifact provenance was not loaded");
    }
    if(dataset.embedding_artifact->generator_id
       != "agent-memory.fixture.loader-test") {
        fail("embedding artifact generator id was not loaded");
    }
    if(dataset.embedding_artifact->artifact_hash
       != "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789") {
        fail("embedding artifact hash was not loaded");
    }
    if(dataset.embedding_artifact->dataset_revision != "unit-test-dataset") {
        fail("embedding artifact dataset revision was not loaded");
    }
    if(dataset.embedding_artifact->document_prompt_id
       != "unit-test-document-prompt") {
        fail("embedding artifact document prompt id was not loaded");
    }
    if(dataset.embedding_artifact->hash_algorithm != "sha256") {
        fail("embedding artifact hash algorithm was not loaded");
    }
    if(dataset.document_embeddings.size() != 2 || dataset.query_embeddings.size() != 1) {
        fail("embedding record counts are wrong");
    }

    {
        auto dataset_without_artifact =
            agent_memory::load_precomputed_embedding_dataset_from_json_string(
                valid_dataset_json()
            );
        dataset_without_artifact.embedding_artifact.reset();
        agent_memory::validate_precomputed_embedding_eval_dataset(
            dataset_without_artifact
        );
    }

    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.embedding_artifact->artifact_hash.clear();
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "empty embedding artifact hash must be rejected"
    );
    expect_runtime_error(
        [] {
            const auto json = without_line(
                valid_dataset_json(),
                "    \"qrels_revision\": \"unit-test-qrels\",\n"
            );
            (void)agent_memory::load_precomputed_embedding_dataset_from_json_string(
                json
            );
        },
        "missing embedding artifact field must be rejected while loading"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.embedding_artifact->hash_algorithm = "sha1";
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "unsupported embedding artifact hash algorithm must be rejected"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.embedding_artifact->config_hash =
                "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "self-descriptive embedding artifact hash must be rejected by canonical contract"
    );
    expect_runtime_error(
        [] {
            auto dataset =
                agent_memory::load_precomputed_embedding_dataset_from_json_string(
                    valid_dataset_json()
                );
            dataset.embedding_artifact->artifact_hash =
                "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
            agent_memory::validate_precomputed_embedding_eval_dataset(dataset);
        },
        "uppercase embedding artifact hash must be rejected"
    );
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
