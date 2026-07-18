#include "BenchmarkReport.hpp"

#if defined(AGENT_MEMORY_ENABLE_JSON) && AGENT_MEMORY_ENABLE_JSON
#include <nlohmann/json.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace agent_memory {

    namespace {

        [[nodiscard]] double metric_or_zero(
            const std::vector<MetricAtK>& metrics,
            std::size_t k
        ) noexcept {
            return metric_value_at(metrics, k).value_or(0.0);
        }

        [[nodiscard]] std::string format_double(double value) {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(3) << value;
            return stream.str();
        }

        void require_finite_non_negative(double value, const char* field) {
            if(!std::isfinite(value) || value < 0.0) {
                throw std::invalid_argument(
                    std::string{"benchmark report field '"} + field
                    + "' must be finite and non-negative"
                );
            }
        }

        void require_fraction(double value, const char* field) {
            require_finite_non_negative(value, field);
            if(value > 1.0) {
                throw std::invalid_argument(
                    std::string{"benchmark report field '"} + field
                    + "' must be in [0, 1]"
                );
            }
        }

        [[nodiscard]] double empty_result_fraction(
            const RetrievalEvalReport& report
        ) noexcept {
            const std::size_t evaluated_query_count =
                report.metrics.judged_query_count
                + report.metrics.no_answer_query_count;
            if(evaluated_query_count == 0) {
                return 0.0;
            }

            const auto non_empty_count = static_cast<std::size_t>(std::count_if(
                report.run.queries.begin(),
                report.run.queries.end(),
                [](const RetrievalQueryRun& query_run) {
                    return !query_run.hits.empty();
                }
            ));
            const std::size_t bounded_non_empty_count =
                std::min(non_empty_count, evaluated_query_count);
            return static_cast<double>(evaluated_query_count - bounded_non_empty_count)
                / static_cast<double>(evaluated_query_count);
        }

    } // namespace

    BenchmarkReport make_benchmark_report(
        const RetrievalEvalReport& eval_report,
        std::string_view benchmark_name,
        const BenchmarkMeasurements& measurements
    ) {
        BenchmarkReport report;
        report.benchmark_name.assign(benchmark_name.begin(), benchmark_name.end());
        report.baseline_name = eval_report.baseline_name;
        report.dataset_name = eval_report.dataset_name;

        report.quality.recall_at_1 = metric_or_zero(eval_report.metrics.recall_at, 1);
        report.quality.recall_at_5 = metric_or_zero(eval_report.metrics.recall_at, 5);
        report.quality.recall_at_10 = metric_or_zero(eval_report.metrics.recall_at, 10);
        report.quality.recall_at_50 = metric_or_zero(eval_report.metrics.recall_at, 50);
        report.quality.ndcg_at_10 = metric_or_zero(eval_report.metrics.ndcg_at, 10);
        report.quality.mrr = eval_report.metrics.mrr;
        report.quality.no_answer_accuracy = eval_report.metrics.no_answer_accuracy;
        report.quality.oov_fraction = measurements.oov_fraction;
        report.quality.empty_result_fraction = empty_result_fraction(eval_report);

        report.speed.measured_query_count = eval_report.latency.sample_count;
        report.speed.mean_latency_ms = eval_report.latency.mean;
        report.speed.p50_latency_ms = eval_report.latency.p50;
        report.speed.p95_latency_ms = eval_report.latency.p95;
        report.speed.p99_latency_ms = eval_report.latency.p99;
        report.speed.total_benchmark_time_ms = measurements.total_benchmark_time_ms;
        if(report.speed.total_benchmark_time_ms > 0.0) {
            report.speed.queries_per_second =
                static_cast<double>(report.speed.measured_query_count) * 1000.0
                / report.speed.total_benchmark_time_ms;
        }

        report.index = measurements.index;
        report.pr29_hooks = measurements.pr29_hooks;
        validate_benchmark_report(report);
        return report;
    }

    void validate_benchmark_report(const BenchmarkReport& report) {
        if(report.schema_version != BenchmarkReport::kSchemaVersion) {
            throw std::invalid_argument("unsupported benchmark report schema_version");
        }
        if(report.benchmark_name.empty()) {
            throw std::invalid_argument("benchmark report benchmark_name must not be empty");
        }
        if(report.baseline_name.empty()) {
            throw std::invalid_argument("benchmark report baseline_name must not be empty");
        }
        if(report.dataset_name.empty()) {
            throw std::invalid_argument("benchmark report dataset_name must not be empty");
        }

        require_fraction(report.quality.recall_at_1, "quality.recall_at_1");
        require_fraction(report.quality.recall_at_5, "quality.recall_at_5");
        require_fraction(report.quality.recall_at_10, "quality.recall_at_10");
        require_fraction(report.quality.recall_at_50, "quality.recall_at_50");
        require_fraction(report.quality.ndcg_at_10, "quality.ndcg_at_10");
        require_fraction(report.quality.mrr, "quality.mrr");
        require_fraction(
            report.quality.no_answer_accuracy,
            "quality.no_answer_accuracy"
        );
        require_fraction(report.quality.oov_fraction, "quality.oov_fraction");
        require_fraction(
            report.quality.empty_result_fraction,
            "quality.empty_result_fraction"
        );

        require_finite_non_negative(
            report.speed.mean_latency_ms,
            "speed.mean_latency_ms"
        );
        require_finite_non_negative(
            report.speed.p50_latency_ms,
            "speed.p50_latency_ms"
        );
        require_finite_non_negative(
            report.speed.p95_latency_ms,
            "speed.p95_latency_ms"
        );
        require_finite_non_negative(
            report.speed.p99_latency_ms,
            "speed.p99_latency_ms"
        );
        require_finite_non_negative(
            report.speed.queries_per_second,
            "speed.queries_per_second"
        );
        require_finite_non_negative(
            report.speed.total_benchmark_time_ms,
            "speed.total_benchmark_time_ms"
        );
        if(report.speed.p95_latency_ms < report.speed.p50_latency_ms
            || report.speed.p99_latency_ms < report.speed.p95_latency_ms) {
            throw std::invalid_argument(
                "benchmark report latency percentiles must be monotonic"
            );
        }

        require_finite_non_negative(
            report.index.corpus_ingest_time_ms,
            "index.corpus_ingest_time_ms"
        );
        require_finite_non_negative(
            report.index.embedding_time_ms,
            "index.embedding_time_ms"
        );
        require_finite_non_negative(
            report.index.index_build_time_ms,
            "index.index_build_time_ms"
        );
        require_finite_non_negative(
            report.index.mean_document_length,
            "index.mean_document_length"
        );

        require_finite_non_negative(
            report.pr29_hooks.candidate_count_before_filter,
            "pr29_hooks.candidate_count_before_filter"
        );
        require_finite_non_negative(
            report.pr29_hooks.candidate_count_after_filter,
            "pr29_hooks.candidate_count_after_filter"
        );
        require_fraction(
            report.pr29_hooks.candidate_reduction_ratio,
            "pr29_hooks.candidate_reduction_ratio"
        );
        require_finite_non_negative(
            report.pr29_hooks.filter_latency_ms,
            "pr29_hooks.filter_latency_ms"
        );
        require_finite_non_negative(
            report.pr29_hooks.rerank_latency_ms,
            "pr29_hooks.rerank_latency_ms"
        );
        require_fraction(
            report.pr29_hooks.candidate_set_recall,
            "pr29_hooks.candidate_set_recall"
        );
    }

    void print_benchmark_report(std::ostream& out, const BenchmarkReport& report) {
        validate_benchmark_report(report);
        out << "=== Benchmark Report v" << report.schema_version << " ===\n"
            << "benchmark: " << report.benchmark_name << '\n'
            << "baseline:  " << report.baseline_name << '\n'
            << "dataset:   " << report.dataset_name << '\n'
            << "Quality:\n"
            << "  Recall@1:              " << format_double(report.quality.recall_at_1) << '\n'
            << "  Recall@5:              " << format_double(report.quality.recall_at_5) << '\n'
            << "  Recall@10:             " << format_double(report.quality.recall_at_10) << '\n'
            << "  Recall@50:             " << format_double(report.quality.recall_at_50) << '\n'
            << "  nDCG@10:               " << format_double(report.quality.ndcg_at_10) << '\n'
            << "  MRR:                   " << format_double(report.quality.mrr) << '\n'
            << "  no-answer accuracy:    "
            << format_double(report.quality.no_answer_accuracy) << '\n'
            << "  OOV fraction:          " << format_double(report.quality.oov_fraction) << '\n'
            << "  empty-result fraction: "
            << format_double(report.quality.empty_result_fraction) << '\n'
            << "Speed:\n"
            << "  measured queries:      " << report.speed.measured_query_count << '\n'
            << "  mean latency (ms):     " << format_double(report.speed.mean_latency_ms) << '\n'
            << "  p50 latency (ms):      " << format_double(report.speed.p50_latency_ms) << '\n'
            << "  p95 latency (ms):      " << format_double(report.speed.p95_latency_ms) << '\n'
            << "  p99 latency (ms):      " << format_double(report.speed.p99_latency_ms) << '\n'
            << "  queries/second:        " << format_double(report.speed.queries_per_second) << '\n'
            << "  total time (ms):       "
            << format_double(report.speed.total_benchmark_time_ms) << '\n'
            << "Index:\n"
            << "  documents:             " << report.index.document_count << '\n'
            << "  vocabulary size:       " << report.index.vocabulary_size << '\n'
            << "  mean document length:  "
            << format_double(report.index.mean_document_length) << '\n'
            << "  corpus ingest (ms):    "
            << format_double(report.index.corpus_ingest_time_ms) << '\n'
            << "  embedding (ms):        " << format_double(report.index.embedding_time_ms) << '\n'
            << "  index build (ms):      " << format_double(report.index.index_build_time_ms) << '\n'
            << "  peak RSS (bytes):      " << report.index.peak_resident_set_bytes << '\n'
            << "Roadmap-label PR #29 hooks:\n"
            << "  candidates before:     "
            << format_double(report.pr29_hooks.candidate_count_before_filter) << '\n'
            << "  candidates after:      "
            << format_double(report.pr29_hooks.candidate_count_after_filter) << '\n'
            << "  reduction ratio:       "
            << format_double(report.pr29_hooks.candidate_reduction_ratio) << '\n'
            << "  filter latency (ms):   "
            << format_double(report.pr29_hooks.filter_latency_ms) << '\n'
            << "  rerank latency (ms):   "
            << format_double(report.pr29_hooks.rerank_latency_ms) << '\n'
            << "  candidate-set recall:  "
            << format_double(report.pr29_hooks.candidate_set_recall) << '\n';
    }

#if defined(AGENT_MEMORY_ENABLE_JSON) && AGENT_MEMORY_ENABLE_JSON
    std::string serialize_benchmark_report_json(
        const BenchmarkReport& report,
        int indent
    ) {
        validate_benchmark_report(report);
        if(indent < -1) {
            throw std::invalid_argument("benchmark report JSON indent must be -1 or greater");
        }

        const nlohmann::json document = {
            {"schema_version", report.schema_version},
            {"benchmark_name", report.benchmark_name},
            {"baseline_name", report.baseline_name},
            {"dataset_name", report.dataset_name},
            {"quality", {
                {"recall_at_1", report.quality.recall_at_1},
                {"recall_at_5", report.quality.recall_at_5},
                {"recall_at_10", report.quality.recall_at_10},
                {"recall_at_50", report.quality.recall_at_50},
                {"ndcg_at_10", report.quality.ndcg_at_10},
                {"mrr", report.quality.mrr},
                {"no_answer_accuracy", report.quality.no_answer_accuracy},
                {"oov_fraction", report.quality.oov_fraction},
                {"empty_result_fraction", report.quality.empty_result_fraction}
            }},
            {"speed", {
                {"measured_query_count", report.speed.measured_query_count},
                {"mean_latency_ms", report.speed.mean_latency_ms},
                {"p50_latency_ms", report.speed.p50_latency_ms},
                {"p95_latency_ms", report.speed.p95_latency_ms},
                {"p99_latency_ms", report.speed.p99_latency_ms},
                {"queries_per_second", report.speed.queries_per_second},
                {"total_benchmark_time_ms", report.speed.total_benchmark_time_ms}
            }},
            {"index", {
                {"vocabulary_size", report.index.vocabulary_size},
                {"corpus_ingest_time_ms", report.index.corpus_ingest_time_ms},
                {"embedding_time_ms", report.index.embedding_time_ms},
                {"index_build_time_ms", report.index.index_build_time_ms},
                {"peak_resident_set_bytes", report.index.peak_resident_set_bytes},
                {"document_count", report.index.document_count},
                {"mean_document_length", report.index.mean_document_length}
            }},
            {"pr29_hooks", {
                {"candidate_count_before_filter",
                    report.pr29_hooks.candidate_count_before_filter},
                {"candidate_count_after_filter",
                    report.pr29_hooks.candidate_count_after_filter},
                {"candidate_reduction_ratio",
                    report.pr29_hooks.candidate_reduction_ratio},
                {"filter_latency_ms", report.pr29_hooks.filter_latency_ms},
                {"rerank_latency_ms", report.pr29_hooks.rerank_latency_ms},
                {"candidate_set_recall", report.pr29_hooks.candidate_set_recall}
            }}
        };
        return document.dump(indent);
    }
#endif

} // namespace agent_memory
