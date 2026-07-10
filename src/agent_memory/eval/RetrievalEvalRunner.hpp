#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_RETRIEVAL_EVAL_RUNNER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_RETRIEVAL_EVAL_RUNNER_HPP_INCLUDED

/// \file RetrievalEvalRunner.hpp
/// \brief Orchestrator that wires a retriever through the eval layer.

#include <agent_memory/eval/Evaluation.hpp>
#include <agent_memory/retrieval/IRetriever.hpp>

#include <iosfwd>
#include <string>
#include <string_view>

namespace agent_memory {

    /// \brief Latency summary in milliseconds with min/max on top of the
    ///        mean/p50/p95/p99 sample stored on `RetrievalMetrics::latency_ms`.
    struct RetrievalLatencyStats final {
        std::size_t sample_count = 0;
        double mean = 0.0;
        double min = 0.0;
        double max = 0.0;
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
    };

    /// \brief End-to-end evaluation report for one retriever over one dataset.
    struct RetrievalEvalReport final {
        std::string baseline_name;
        std::string dataset_name;
        std::size_t corpus_size = 0;
        std::size_t query_count = 0;
        RetrievalRun run;
        RetrievalMetrics metrics;
        RetrievalLatencyStats latency;
    };

    /// \brief Runs the eval pipeline for the supplied retriever and dataset.
    ///
    /// The runner produces a `RetrievalRun` by calling `run_retriever`, then
    /// feeds it through `evaluate_retrieval`. The returned report carries the
    /// full run, the metric table, and the latency summary.
    [[nodiscard]] RetrievalEvalReport run_retrieval_eval(
        const IRetriever& retriever,
        const RetrievalEvalDataset& dataset,
        std::string_view baseline_name,
        const RetrievalEvaluationOptions& options = {}
    );

    /// \brief Writes a human-readable summary of the report to `out`.
    void print_report(std::ostream& out, const RetrievalEvalReport& report);

} // namespace agent_memory

#endif