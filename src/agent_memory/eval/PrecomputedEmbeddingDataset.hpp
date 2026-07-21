#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_PRECOMPUTED_EMBEDDING_DATASET_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_PRECOMPUTED_EMBEDDING_DATASET_HPP_INCLUDED

/// \file PrecomputedEmbeddingDataset.hpp
/// \brief JSON loader for real-evaluation datasets with precomputed embeddings.

#include <agent_memory/embedding/embedding_types.hpp>
#include <agent_memory/eval/Evaluation.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if !defined(AGENT_MEMORY_ENABLE_JSON) || !AGENT_MEMORY_ENABLE_JSON
#error "PrecomputedEmbeddingDataset is unavailable: rebuild with -DAGENT_MEMORY_ENABLE_JSON=ON \
(nlohmann_json is required)."
#endif

namespace agent_memory {

    /// \brief Dense embedding attached to a corpus item or query id.
    struct PrecomputedEmbeddingRecord final {
        std::string id;
        Embedding embedding;
    };

    /// \brief Provenance for a frozen precomputed embedding artifact.
    ///
    /// The loader does not generate embeddings. This metadata records which
    /// external or fixture generator produced the vectors consumed by tests and
    /// benchmarks.
    struct PrecomputedEmbeddingArtifactInfo final {
        std::string generator_id;
        std::string generator_version;
        std::string source_revision;
        std::string projection_kind;
        std::string config_hash;
        std::string artifact_hash;
    };

    /// \brief Retrieval dataset plus locked document/query embeddings.
    ///
    /// This shape is intended for reproducible benchmark fixtures: external
    /// tooling may generate embeddings with any provider, while C++ tests and
    /// benchmarks consume a frozen artifact without network/API dependencies.
    struct PrecomputedEmbeddingEvalDataset final {
        RetrievalEvalDataset retrieval;
        EmbeddingModelInfo embedding_model;
        std::optional<PrecomputedEmbeddingArtifactInfo> embedding_artifact;
        std::vector<PrecomputedEmbeddingRecord> document_embeddings;
        std::vector<PrecomputedEmbeddingRecord> query_embeddings;
    };

    /// \brief Loads a precomputed embedding evaluation dataset from JSON file.
    /// \throws std::runtime_error on any file/parse/schema/coverage failure.
    [[nodiscard]] PrecomputedEmbeddingEvalDataset
    load_precomputed_embedding_dataset_from_json_file(
        const std::filesystem::path& path
    );

    /// \brief Loads a precomputed embedding evaluation dataset from JSON text.
    /// \throws std::runtime_error on any parse/schema/coverage failure.
    [[nodiscard]] PrecomputedEmbeddingEvalDataset
    load_precomputed_embedding_dataset_from_json_string(std::string_view json_text);

    /// \brief Validates embedding dimensionality and id coverage.
    /// \throws std::runtime_error on the first contract violation.
    void validate_precomputed_embedding_eval_dataset(
        const PrecomputedEmbeddingEvalDataset& dataset
    );

} // namespace agent_memory

#endif
