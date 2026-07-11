#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_DATASET_LOADER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_DATASET_LOADER_HPP_INCLUDED

/// \file DatasetLoader.hpp
/// \brief File/string JSON loader for `agent_memory::RetrievalEvalDataset`.
///
/// Mirrors the in-memory shape documented in
/// `docs/eval/dataset-schema.md` (frozen by PR #26). Snake-case field names
/// on disk map directly to the C++ types; see `DatasetLoader.cpp` for the
/// field-level contract.

#include <agent_memory/eval/Evaluation.hpp>

#include <filesystem>
#include <string>
#include <string_view>

#if !defined(AGENT_MEMORY_ENABLE_JSON) || !AGENT_MEMORY_ENABLE_JSON
#error "DatasetLoader is unavailable: rebuild with -DAGENT_MEMORY_ENABLE_JSON=ON \
(nlohmann_json is required). See docs/eval/json-library-choice.md."
#endif

namespace agent_memory {

    /// \brief Loads a `RetrievalEvalDataset` from a JSON file on disk.
    ///
    /// The file format matches the schema pinned by PR #26; see
    /// `DatasetLoader.cpp` for the exact field names and types. Malformed
    /// JSON, missing required fields, or wrong types surface as
    /// `std::runtime_error` with a message that includes the path and field
    /// that failed.
    /// \throws std::runtime_error on any file/parse/schema failure.
    [[nodiscard]] RetrievalEvalDataset load_dataset_from_json_file(
        const std::filesystem::path& path
    );

    /// \brief Loads a `RetrievalEvalDataset` from an in-memory JSON string.
    ///
    /// Same schema as `load_dataset_from_json_file`; error messages include a
    /// synthetic `<string>` path placeholder.
    /// \throws std::runtime_error on any parse/schema failure.
    [[nodiscard]] RetrievalEvalDataset load_dataset_from_json_string(
        std::string_view json_text
    );

} // namespace agent_memory

#endif
