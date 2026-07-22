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
    ///
    /// `config_hash` is the lowercase hexadecimal `hash_algorithm` digest of
    /// the canonical generator configuration: model id/revision, document and
    /// query prompt identities, normalization, dtype, pooling/truncation,
    /// projection transform, random seed, and other behavior-affecting
    /// settings. `dataset_hash` and `qrels_hash` cover the canonical retrieval
    /// corpus/query and judgment payloads, respectively. `artifact_hash` is the
    /// lowercase hexadecimal digest of the canonical embedding payload before
    /// any self-referential provenance wrapper is added. For JSON fixtures this
    /// payload is the ordered document/query embedding records with ids and
    /// float values.
    struct PrecomputedEmbeddingArtifactInfo final {
        std::string generator_id;
        std::string generator_version;
        std::string dataset_revision;
        std::string generator_revision;
        std::string model_revision;
        std::string tokenizer_revision;
        std::string generator_source_hash;
        std::string generator_contract_source_hash;
        std::string generator_command;
        std::string generator_requirements_lock;
        std::string qrels_revision;
        std::string document_prompt_id;
        std::string query_prompt_id;
        std::string projection_kind;
        std::string normalization;
        std::string dtype;
        std::string hash_algorithm;
        std::string config_hash;
        std::string dataset_hash;
        std::string qrels_hash;
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
