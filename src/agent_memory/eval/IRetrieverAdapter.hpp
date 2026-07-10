#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_I_RETRIEVER_ADAPTER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_I_RETRIEVER_ADAPTER_HPP_INCLUDED

/// \file IRetrieverAdapter.hpp
/// \brief Adapter that drives `IRetriever` queries and emits a `RetrievalRun`.

#include <agent_memory/eval/Evaluation.hpp>
#include <agent_memory/retrieval/IRetriever.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace agent_memory {

    /// \brief Drives `IRetriever::retrieve()` for every query in the dataset
    ///        and converts the returned `RetrievedChunk`s into `RetrievalRunHit`s.
    ///
    /// Latency is measured strictly around each `retrieve()` call. Chunk order
    /// in the retriever result becomes the implicit rank in the run hit.
    inline RetrievalRun run_retriever(
        const IRetriever& retriever,
        const RetrievalEvalDataset& dataset,
        std::string_view baseline_name
    ) {
        const std::string baseline{baseline_name};

        RetrievalRun run;
        run.name = baseline;
        run.queries.reserve(dataset.queries.size());

        for(const auto& query : dataset.queries) {
            RetrievalQueryRun query_run;
            query_run.query_id = query.id;

            RetrievalQuery retrieval_query;
            retrieval_query.text = query.text;
            retrieval_query.limit = query.limit;
            retrieval_query.metadata_filters = query.metadata_filters;

            const auto start = std::chrono::steady_clock::now();
            const RetrievalResult result = retriever.retrieve(retrieval_query);
            const auto end = std::chrono::steady_clock::now();

            const auto elapsed_ms = std::chrono::duration<double, std::milli>(
                end - start
            ).count();
            query_run.latency_ms = elapsed_ms;

            query_run.hits.reserve(result.chunks.size());
            for(const auto& chunk : result.chunks) {
                RetrievalRunHit hit;
                hit.item_id = chunk.chunk.id.value();
                hit.score = chunk.score;
                // Implicit rank (0) lets evaluate_retrieval() use vector position.
                hit.rank = 0;
                hit.retriever_name = baseline;
                query_run.hits.push_back(std::move(hit));
            }

            run.queries.push_back(std::move(query_run));
        }

        return run;
    }

} // namespace agent_memory

#endif