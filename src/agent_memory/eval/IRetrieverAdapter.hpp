#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_I_RETRIEVER_ADAPTER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_I_RETRIEVER_ADAPTER_HPP_INCLUDED

/// \file IRetrieverAdapter.hpp
/// \brief Adapters that drive retriever/engine queries and emit a `RetrievalRun`.

#include <agent_memory/eval/Evaluation.hpp>
#include <agent_memory/retrieval/IRetrievalEngine.hpp>
#include <agent_memory/retrieval/IRetriever.hpp>

#include <string>
#include <string_view>

namespace agent_memory {

    /// \brief Drives `IRetriever::retrieve()` for every query in the dataset
    ///        and converts the returned `RetrievedChunk`s into `RetrievalRunHit`s.
    ///
    /// Latency is measured strictly around each `retrieve()` call. Chunk order
    /// in the retriever result becomes the implicit rank in the run hit.
    /// Queries whose `answer_mode` is `EvalQueryAnswerMode::Ignore` are
    /// skipped before any retrieval call so they contribute neither hits nor
    /// latency samples to the run.
    [[nodiscard]] RetrievalRun run_retriever(
        const IRetriever& retriever,
        const RetrievalEvalDataset& dataset,
        std::string_view baseline_name
    );

    /// \brief Drives `IRetrievalEngine::retrieve()` for every non-ignored query.
    ///
    /// Request construction mirrors `run_retriever()`. Result identity comes
    /// from `RetrievalResponseItem::lexical.chunk_id`, with `object.id` as the
    /// fallback for non-chunk engines. The `response.items` order is treated
    /// as the final ranking order. `RetrievalResponseItem::lexical.score` is
    /// copied as diagnostic payload only; evaluation metrics use item order
    /// unless explicit ranks are introduced by a future adapter.
    /// Latency is measured strictly around the engine call, excluding
    /// request/result conversion.
    /// \throws std::invalid_argument when an engine result has no usable id.
    [[nodiscard]] RetrievalRun run_retrieval_engine(
        const IRetrievalEngine& engine,
        const RetrievalEvalDataset& dataset,
        std::string_view baseline_name
    );

} // namespace agent_memory

#endif
