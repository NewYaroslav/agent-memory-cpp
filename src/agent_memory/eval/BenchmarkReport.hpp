#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_BENCHMARK_REPORT_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_BENCHMARK_REPORT_HPP_INCLUDED

/// \file BenchmarkReport.hpp
/// \brief Stable benchmark report sections and rendering helpers.

#include <agent_memory/eval/RetrievalEvalRunner.hpp>

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace agent_memory {

    /// \brief Retrieval-quality metrics for one benchmark run.
    struct QualityMetrics final {
        double recall_at_1 = 0.0;
        double recall_at_5 = 0.0;
        double recall_at_10 = 0.0;
        double recall_at_50 = 0.0;
        double ndcg_at_10 = 0.0;
        double mrr = 0.0;
        double no_answer_accuracy = 0.0;
        /// \brief Fraction of analyzed query tokens absent from the index vocabulary.
        /// \note Generic retrievers cannot derive this value, so benchmark drivers supply it.
        double oov_fraction = 0.0;
        /// \brief Fraction of non-ignored queries whose result set was empty.
        double empty_result_fraction = 0.0;
    };

    /// \brief Query-loop timing and throughput metrics.
    struct SpeedMetrics final {
        /// \brief Evaluated non-ignored query count with complete latency samples.
        std::size_t measured_query_count = 0;
        double mean_latency_ms = 0.0;
        double p50_latency_ms = 0.0;
        double p95_latency_ms = 0.0;
        double p99_latency_ms = 0.0;
        double queries_per_second = 0.0;
        /// \brief Wall-clock time around the complete retrieval query loop.
        double total_benchmark_time_ms = 0.0;
    };

    /// \brief Corpus and index construction metrics supplied by a benchmark driver.
    struct IndexMetrics final {
        std::size_t vocabulary_size = 0;
        double corpus_ingest_time_ms = 0.0;
        double embedding_time_ms = 0.0;
        double index_build_time_ms = 0.0;
        std::uint64_t peak_resident_set_bytes = 0;
        std::size_t document_count = 0;
        double mean_document_length = 0.0;
    };

    /// \brief Forward-compatible candidate-filter instrumentation.
    ///
    /// These hooks are intentionally present before the roadmap-label "PR #29"
    /// binary-signature work starts. Exact retrievers leave them at zero.
    struct PR29Hooks final {
        /// \brief Mean candidate count per measured query before coarse filtering.
        double candidate_count_before_filter = 0.0;
        /// \brief Mean candidate count per measured query after coarse filtering.
        double candidate_count_after_filter = 0.0;
        double candidate_reduction_ratio = 0.0;
        double filter_latency_ms = 0.0;
        double rerank_latency_ms = 0.0;
        double candidate_set_recall = 0.0;
    };

    /// \brief Driver-provided measurements that are not derivable from retrieval metrics.
    struct BenchmarkMeasurements final {
        double total_benchmark_time_ms = 0.0;
        double oov_fraction = 0.0;
        IndexMetrics index;
        PR29Hooks pr29_hooks;
    };

    /// \brief Versioned, machine-readable benchmark result.
    struct BenchmarkReport final {
        static constexpr std::uint32_t kSchemaVersion = 1;

        std::uint32_t schema_version = kSchemaVersion;
        std::string benchmark_name;
        std::string baseline_name;
        std::string dataset_name;
        QualityMetrics quality;
        SpeedMetrics speed;
        IndexMetrics index;
        PR29Hooks pr29_hooks;
    };

    /// \brief Builds a benchmark report from the existing evaluation report.
    ///
    /// Recall, nDCG, MRR, no-answer accuracy, empty-result fraction, and
    /// latency percentiles are copied from `eval_report`.
    /// Measurements that require knowledge of the concrete tokenizer, index,
    /// or process are supplied separately.
    /// \throws std::invalid_argument when the resulting report is invalid.
    [[nodiscard]] BenchmarkReport make_benchmark_report(
        const RetrievalEvalReport& eval_report,
        std::string_view benchmark_name,
        const BenchmarkMeasurements& measurements = {}
    );

    /// \brief Validates report names, schema version, ranges, and finite values.
    /// \throws std::invalid_argument on the first invalid field.
    void validate_benchmark_report(const BenchmarkReport& report);

    /// \brief Writes a human-readable report with quality, speed, index, and hook sections.
    void print_benchmark_report(std::ostream& out, const BenchmarkReport& report);

#if defined(AGENT_MEMORY_ENABLE_JSON) && AGENT_MEMORY_ENABLE_JSON
    /// \brief Serializes a validated report as structured JSON.
    /// \param report Report to serialize.
    /// \param indent Number of spaces per level, or -1 for compact JSON.
    /// \throws std::invalid_argument when the report or indent is invalid.
    [[nodiscard]] std::string serialize_benchmark_report_json(
        const BenchmarkReport& report,
        int indent = 2
    );
#endif

} // namespace agent_memory

#endif
