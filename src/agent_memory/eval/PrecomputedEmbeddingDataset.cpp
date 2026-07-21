#include "PrecomputedEmbeddingDataset.hpp"

#include "DatasetLoader.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace agent_memory {
    namespace {

        constexpr double kNormalizedEmbeddingSquaredNormTolerance = 1.0e-3;

        struct SourceLocation final {
            std::string label;
        };

        [[noreturn]] void throw_field_error(
            const SourceLocation& loc,
            std::string_view field,
            const std::string& reason
        ) {
            throw std::runtime_error(
                loc.label + "." + std::string{field} + ": " + reason
            );
        }

        [[noreturn]] void throw_parse_error(
            const SourceLocation& loc,
            const std::string& reason
        ) {
            throw std::runtime_error(loc.label + ": " + reason);
        }

        [[nodiscard]] const nlohmann::json& require_field(
            const nlohmann::json& object,
            std::string_view field,
            const SourceLocation& loc
        ) {
            if(!object.is_object()) {
                throw_parse_error(loc, "expected object");
            }
            const auto it = object.find(std::string{field});
            if(it == object.end()) {
                throw_field_error(loc, field, "required field is missing");
            }
            return *it;
        }

        [[nodiscard]] const nlohmann::json* find_optional(
            const nlohmann::json& object,
            std::string_view field
        ) {
            if(!object.is_object()) {
                return nullptr;
            }
            const auto it = object.find(std::string{field});
            return it == object.end() ? nullptr : &*it;
        }

        [[nodiscard]] const nlohmann::json& require_array(
            const nlohmann::json& object,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto& value = require_field(object, field, loc);
            if(!value.is_array()) {
                throw_field_error(loc, field, "expected array");
            }
            return value;
        }

        [[nodiscard]] std::string read_string(
            const nlohmann::json& object,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto& value = require_field(object, field, loc);
            if(!value.is_string()) {
                throw_field_error(loc, field, "expected string");
            }
            return value.get<std::string>();
        }

        [[nodiscard]] std::size_t read_size(
            const nlohmann::json& object,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto& value = require_field(object, field, loc);
            if(!value.is_number_unsigned()) {
                throw_field_error(loc, field, "expected unsigned integer");
            }
            return value.get<std::size_t>();
        }

        [[nodiscard]] std::size_t read_optional_size(
            const nlohmann::json& object,
            std::string_view field,
            std::size_t fallback,
            const SourceLocation& loc
        ) {
            const auto* value = find_optional(object, field);
            if(value == nullptr) {
                return fallback;
            }
            if(!value->is_number_unsigned()) {
                throw_field_error(loc, field, "expected unsigned integer");
            }
            return value->get<std::size_t>();
        }

        [[nodiscard]] bool read_optional_bool(
            const nlohmann::json& object,
            std::string_view field,
            bool fallback,
            const SourceLocation& loc
        ) {
            const auto* value = find_optional(object, field);
            if(value == nullptr) {
                return fallback;
            }
            if(!value->is_boolean()) {
                throw_field_error(loc, field, "expected boolean");
            }
            return value->get<bool>();
        }

        [[nodiscard]] std::string read_optional_string(
            const nlohmann::json& object,
            std::string_view field,
            std::string fallback,
            const SourceLocation& loc
        ) {
            const auto* value = find_optional(object, field);
            if(value == nullptr) {
                return fallback;
            }
            if(!value->is_string()) {
                throw_field_error(loc, field, "expected string");
            }
            return value->get<std::string>();
        }

        [[nodiscard]] float read_float_value(
            const nlohmann::json& value,
            const SourceLocation& loc
        ) {
            if(value.is_boolean() || !value.is_number()) {
                throw_parse_error(loc, "expected numeric vector value");
            }
            const auto as_double = value.get<double>();
            if(!std::isfinite(as_double)) {
                throw_parse_error(loc, "vector value must be finite");
            }
            return static_cast<float>(as_double);
        }

        [[nodiscard]] Embedding read_embedding_vector(
            const nlohmann::json& object,
            const SourceLocation& loc
        ) {
            const auto& vector_json = require_array(object, "vector", loc);
            Embedding embedding;
            embedding.values.reserve(vector_json.size());
            for(std::size_t index = 0; index < vector_json.size(); ++index) {
                SourceLocation value_loc = loc;
                value_loc.label += ".vector[" + std::to_string(index) + "]";
                embedding.values.push_back(read_float_value(vector_json[index], value_loc));
            }
            return embedding;
        }

        [[nodiscard]] SimilarityMetric read_similarity_metric(
            const nlohmann::json& object,
            const SourceLocation& loc
        ) {
            const auto text = read_optional_string(object, "similarity_metric", "cosine", loc);
            const auto parsed = to_enum<SimilarityMetric>(text);
            if(!parsed) {
                throw_field_error(loc, "similarity_metric", "unsupported value: " + text);
            }
            return parsed.value;
        }

        [[nodiscard]] PoolingMode read_pooling_mode(
            const nlohmann::json& object,
            const SourceLocation& loc
        ) {
            const auto text =
                read_optional_string(object, "pooling_mode", "model_default", loc);
            const auto parsed = to_enum<PoolingMode>(text);
            if(!parsed) {
                throw_field_error(loc, "pooling_mode", "unsupported value: " + text);
            }
            return parsed.value;
        }

        [[nodiscard]] EmbeddingModelInfo read_embedding_model(
            const nlohmann::json& root,
            const SourceLocation& loc
        ) {
            const auto& model_json = require_field(root, "embedding_model", loc);
            SourceLocation model_loc = loc;
            model_loc.label += ".embedding_model";
            if(!model_json.is_object()) {
                throw_parse_error(model_loc, "expected object");
            }
            EmbeddingModelInfo model;
            model.model_id = read_string(model_json, "model_id", model_loc);
            model.dimension = read_size(model_json, "dimension", model_loc);
            model.max_tokens = read_optional_size(model_json, "max_tokens", 0, model_loc);
            model.similarity_metric = read_similarity_metric(model_json, model_loc);
            model.pooling_mode = read_pooling_mode(model_json, model_loc);
            model.normalized =
                read_optional_bool(model_json, "normalized", false, model_loc);
            return model;
        }

        [[nodiscard]] bool is_lowercase_sha256_hex(std::string_view value) noexcept {
            if(value.size() != 64) {
                return false;
            }
            for(const auto ch : value) {
                if((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')) {
                    continue;
                }
                return false;
            }
            return true;
        }

        void require_non_empty_artifact_field(
            std::string_view value,
            std::string_view field
        ) {
            if(value.empty()) {
                throw std::runtime_error(
                    "embedding_artifact." + std::string{field} + " must not be empty"
                );
            }
        }

        [[nodiscard]] std::optional<PrecomputedEmbeddingArtifactInfo>
        read_embedding_artifact(
            const nlohmann::json& root,
            const SourceLocation& loc
        ) {
            const auto* artifact_json = find_optional(root, "embedding_artifact");
            if(artifact_json == nullptr) {
                return std::nullopt;
            }
            SourceLocation artifact_loc = loc;
            artifact_loc.label += ".embedding_artifact";
            if(!artifact_json->is_object()) {
                throw_parse_error(artifact_loc, "expected object");
            }
            PrecomputedEmbeddingArtifactInfo artifact;
            artifact.generator_id =
                read_string(*artifact_json, "generator_id", artifact_loc);
            artifact.generator_version =
                read_string(*artifact_json, "generator_version", artifact_loc);
            artifact.dataset_revision =
                read_string(*artifact_json, "dataset_revision", artifact_loc);
            artifact.generator_revision =
                read_string(*artifact_json, "generator_revision", artifact_loc);
            artifact.model_revision =
                read_string(*artifact_json, "model_revision", artifact_loc);
            artifact.qrels_revision =
                read_string(*artifact_json, "qrels_revision", artifact_loc);
            artifact.document_prompt_id =
                read_string(*artifact_json, "document_prompt_id", artifact_loc);
            artifact.query_prompt_id =
                read_string(*artifact_json, "query_prompt_id", artifact_loc);
            artifact.projection_kind =
                read_string(*artifact_json, "projection_kind", artifact_loc);
            artifact.normalization =
                read_string(*artifact_json, "normalization", artifact_loc);
            artifact.dtype =
                read_string(*artifact_json, "dtype", artifact_loc);
            artifact.hash_algorithm =
                read_string(*artifact_json, "hash_algorithm", artifact_loc);
            artifact.config_hash =
                read_string(*artifact_json, "config_hash", artifact_loc);
            artifact.artifact_hash =
                read_string(*artifact_json, "artifact_hash", artifact_loc);
            return artifact;
        }

        [[nodiscard]] std::vector<PrecomputedEmbeddingRecord> read_embedding_records(
            const nlohmann::json& root,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto& array = require_array(root, field, loc);
            std::vector<PrecomputedEmbeddingRecord> records;
            records.reserve(array.size());
            for(std::size_t index = 0; index < array.size(); ++index) {
                SourceLocation child_loc = loc;
                child_loc.label += "." + std::string{field} + "["
                    + std::to_string(index) + "]";
                if(!array[index].is_object()) {
                    throw_parse_error(child_loc, "expected object");
                }
                PrecomputedEmbeddingRecord record;
                record.id = read_string(array[index], "id", child_loc);
                record.embedding = read_embedding_vector(array[index], child_loc);
                records.push_back(std::move(record));
            }
            return records;
        }

        [[nodiscard]] RetrievalEvalDataset read_retrieval_dataset(
            const nlohmann::json& root,
            const SourceLocation& loc
        ) {
            nlohmann::json retrieval_json;
            retrieval_json["name"] = require_field(root, "name", loc);
            retrieval_json["corpus"] = require_field(root, "corpus", loc);
            retrieval_json["queries"] = require_field(root, "queries", loc);
            retrieval_json["judgments"] = require_field(root, "judgments", loc);
            return load_dataset_from_json_string(retrieval_json.dump());
        }

        [[nodiscard]] std::unordered_set<std::string> corpus_ids(
            const RetrievalEvalDataset& dataset
        ) {
            std::unordered_set<std::string> ids;
            ids.reserve(dataset.corpus.size());
            for(const auto& item : dataset.corpus) {
                ids.insert(item.id);
            }
            return ids;
        }

        [[nodiscard]] std::unordered_set<std::string> query_ids(
            const RetrievalEvalDataset& dataset
        ) {
            std::unordered_set<std::string> ids;
            ids.reserve(dataset.queries.size());
            for(const auto& query : dataset.queries) {
                ids.insert(query.id);
            }
            return ids;
        }

        void validate_embedding_records(
            const std::vector<PrecomputedEmbeddingRecord>& records,
            const std::unordered_set<std::string>& expected_ids,
            std::size_t expected_dimension,
            bool require_unit_norm,
            std::string_view label
        ) {
            std::unordered_set<std::string> seen;
            seen.reserve(records.size());
            for(const auto& record : records) {
                if(record.id.empty()) {
                    throw std::runtime_error(
                        std::string(label) + ": embedding id must not be empty"
                    );
                }
                if(!seen.insert(record.id).second) {
                    throw std::runtime_error(
                        std::string(label) + ": duplicate embedding id: " + record.id
                    );
                }
                if(expected_ids.find(record.id) == expected_ids.end()) {
                    throw std::runtime_error(
                        std::string(label) + ": embedding id is not in retrieval dataset: "
                        + record.id
                    );
                }
                if(record.embedding.dimension() != expected_dimension) {
                    throw std::runtime_error(
                        std::string(label) + ": embedding dimension mismatch for id "
                        + record.id
                    );
                }
                for(const auto value : record.embedding.values) {
                    if(!std::isfinite(value)) {
                        throw std::runtime_error(
                            std::string(label) + ": embedding value must be finite for id "
                            + record.id
                        );
                    }
                }
                if(require_unit_norm) {
                    double squared_norm = 0.0;
                    for(const auto value : record.embedding.values) {
                        squared_norm += static_cast<double>(value) * value;
                    }
                    if(std::abs(squared_norm - 1.0)
                       > kNormalizedEmbeddingSquaredNormTolerance) {
                        throw std::runtime_error(
                            std::string(label)
                            + ": normalized embedding squared norm mismatch for id "
                            + record.id
                        );
                    }
                }
            }
            for(const auto& expected_id : expected_ids) {
                if(seen.find(expected_id) == seen.end()) {
                    throw std::runtime_error(
                        std::string(label) + ": missing embedding for id " + expected_id
                    );
                }
            }
        }

        [[nodiscard]] std::string read_file_text(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            if(!input) {
                throw std::runtime_error("cannot open precomputed embedding dataset: "
                                         + path.string());
            }
            std::string text(
                (std::istreambuf_iterator<char>(input)),
                std::istreambuf_iterator<char>()
            );
            return text;
        }

        [[nodiscard]] PrecomputedEmbeddingEvalDataset parse_dataset(
            const nlohmann::json& root,
            const SourceLocation& loc
        ) {
            if(!root.is_object()) {
                throw_parse_error(loc, "top-level value must be a JSON object");
            }
            const auto schema_version = read_size(root, "schema_version", loc);
            if(schema_version != 1) {
                throw_field_error(loc, "schema_version", "expected 1");
            }
            PrecomputedEmbeddingEvalDataset dataset;
            dataset.retrieval = read_retrieval_dataset(root, loc);
            dataset.embedding_model = read_embedding_model(root, loc);
            dataset.embedding_artifact = read_embedding_artifact(root, loc);
            dataset.document_embeddings =
                read_embedding_records(root, "document_embeddings", loc);
            dataset.query_embeddings =
                read_embedding_records(root, "query_embeddings", loc);
            validate_precomputed_embedding_eval_dataset(dataset);
            return dataset;
        }

    } // namespace

    void validate_precomputed_embedding_eval_dataset(
        const PrecomputedEmbeddingEvalDataset& dataset
    ) {
        validate_retrieval_eval_dataset(dataset.retrieval);
        if(dataset.embedding_model.model_id.empty()) {
            throw std::runtime_error("embedding_model.model_id must not be empty");
        }
        if(dataset.embedding_model.dimension == 0) {
            throw std::runtime_error("embedding_model.dimension must be positive");
        }
        if(dataset.embedding_artifact) {
            const auto& artifact = *dataset.embedding_artifact;
            require_non_empty_artifact_field(artifact.generator_id, "generator_id");
            require_non_empty_artifact_field(
                artifact.generator_version,
                "generator_version"
            );
            require_non_empty_artifact_field(
                artifact.dataset_revision,
                "dataset_revision"
            );
            require_non_empty_artifact_field(
                artifact.generator_revision,
                "generator_revision"
            );
            require_non_empty_artifact_field(artifact.model_revision, "model_revision");
            require_non_empty_artifact_field(artifact.qrels_revision, "qrels_revision");
            require_non_empty_artifact_field(
                artifact.document_prompt_id,
                "document_prompt_id"
            );
            require_non_empty_artifact_field(
                artifact.query_prompt_id,
                "query_prompt_id"
            );
            require_non_empty_artifact_field(artifact.projection_kind, "projection_kind");
            require_non_empty_artifact_field(artifact.normalization, "normalization");
            require_non_empty_artifact_field(artifact.dtype, "dtype");
            require_non_empty_artifact_field(artifact.hash_algorithm, "hash_algorithm");
            if(artifact.hash_algorithm != "sha256") {
                throw std::runtime_error(
                    "embedding_artifact.hash_algorithm must be sha256"
                );
            }
            require_non_empty_artifact_field(artifact.config_hash, "config_hash");
            require_non_empty_artifact_field(artifact.artifact_hash, "artifact_hash");
            if(!is_lowercase_sha256_hex(artifact.config_hash)) {
                throw std::runtime_error(
                    "embedding_artifact.config_hash must be 64 lowercase SHA-256 hex characters"
                );
            }
            if(!is_lowercase_sha256_hex(artifact.artifact_hash)) {
                throw std::runtime_error(
                    "embedding_artifact.artifact_hash must be 64 lowercase SHA-256 hex characters"
                );
            }
        }
        validate_embedding_records(
            dataset.document_embeddings,
            corpus_ids(dataset.retrieval),
            dataset.embedding_model.dimension,
            dataset.embedding_model.normalized,
            "document_embeddings"
        );
        validate_embedding_records(
            dataset.query_embeddings,
            query_ids(dataset.retrieval),
            dataset.embedding_model.dimension,
            dataset.embedding_model.normalized,
            "query_embeddings"
        );
    }

    PrecomputedEmbeddingEvalDataset load_precomputed_embedding_dataset_from_json_file(
        const std::filesystem::path& path
    ) {
        return load_precomputed_embedding_dataset_from_json_string(read_file_text(path));
    }

    PrecomputedEmbeddingEvalDataset load_precomputed_embedding_dataset_from_json_string(
        std::string_view json_text
    ) {
        nlohmann::json root;
        try {
            root = nlohmann::json::parse(json_text.begin(), json_text.end());
        } catch(const nlohmann::json::parse_error& error) {
            throw std::runtime_error(
                std::string{"<precomputed-embedding-json>: parse error: "}
                + error.what()
            );
        }
        return parse_dataset(root, SourceLocation{"<precomputed-embedding-json>"});
    }

} // namespace agent_memory
