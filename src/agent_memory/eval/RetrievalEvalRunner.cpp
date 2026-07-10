#include "RetrievalEvalRunner.hpp"

#include "IRetrieverAdapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace agent_memory {

    namespace {

        // Linear-interpolation percentile over a sorted vector.
        double percentile(std::vector<double> sorted, double quantile) {
            if(sorted.empty()) {
                return 0.0;
            }
            if(sorted.size() == 1) {
                return sorted.front();
            }
            const double position = quantile * (sorted.size() - 1);
            const auto lower = static_cast<std::size_t>(std::floor(position));
            const auto upper = static_cast<std::size_t>(std::ceil(position));
            const double weight = position - static_cast<double>(lower);
            return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
        }

        RetrievalLatencyStats compute_latency_summary(const RetrievalRun& run) {
            std::vector<double> samples;
            samples.reserve(run.queries.size());
            for(const auto& query_run : run.queries) {
                if(query_run.latency_ms) {
                    samples.push_back(*query_run.latency_ms);
                }
            }
            RetrievalLatencyStats stats;
            stats.sample_count = samples.size();
            if(samples.empty()) {
                return stats;
            }
            std::sort(samples.begin(), samples.end());
            stats.min = samples.front();
            stats.max = samples.back();
            double total = 0.0;
            for(const auto value : samples) {
                total += value;
            }
            stats.mean = total / static_cast<double>(samples.size());
            stats.p50 = percentile(samples, 0.50);
            stats.p95 = percentile(samples, 0.95);
            stats.p99 = percentile(samples, 0.99);
            return stats;
        }

        std::string format_double(double value) {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(3) << value;
            return stream.str();
        }

    } // namespace

    RetrievalEvalReport run_retrieval_eval(
        const IRetriever& retriever,
        const RetrievalEvalDataset& dataset,
        std::string_view baseline_name,
        const RetrievalEvaluationOptions& options
    ) {
        RetrievalEvalReport report;
        report.baseline_name.assign(baseline_name.begin(), baseline_name.end());
        report.dataset_name = dataset.name;
        report.corpus_size = dataset.corpus.size();
        report.query_count = dataset.queries.size();
        report.run = run_retriever(retriever, dataset, baseline_name);
        report.metrics = evaluate_retrieval(dataset, report.run, options);
        report.latency = compute_latency_summary(report.run);
        return report;
    }

    void print_report(std::ostream& out, const RetrievalEvalReport& report) {
        out << "=== Retrieval Eval Report ===\n"
            << "baseline:       " << report.baseline_name << '\n'
            << "dataset:        " << report.dataset_name << '\n'
            << "corpus_size:    " << report.corpus_size << '\n'
            << "query_count:    " << report.query_count << '\n'
            << "judged:         " << report.metrics.judged_query_count << '\n'
            << "no-answer:      " << report.metrics.no_answer_query_count << '\n'
            << "ignored:        " << report.metrics.ignored_query_count << '\n';
        out << "Recall@K:\n";
        for(const auto& recall : report.metrics.recall_at) {
            out << "  @" << recall.k << " = " << format_double(recall.value)
                << '\n';
        }
        out << "nDCG@K:\n";
        for(const auto& ndcg : report.metrics.ndcg_at) {
            out << "  @" << ndcg.k << " = " << format_double(ndcg.value) << '\n';
        }
        out << "MRR:            " << format_double(report.metrics.mrr) << '\n'
            << "No-answer acc.: " << format_double(report.metrics.no_answer_accuracy)
            << '\n'
            << "Latency (ms):\n"
            << "  sample_count: " << report.latency.sample_count << '\n'
            << "  mean:         " << format_double(report.latency.mean) << '\n'
            << "  min:          " << format_double(report.latency.min) << '\n'
            << "  max:          " << format_double(report.latency.max) << '\n'
            << "  p50:          " << format_double(report.latency.p50) << '\n'
            << "  p95:          " << format_double(report.latency.p95) << '\n'
            << "  p99:          " << format_double(report.latency.p99) << '\n';
    }

} // namespace agent_memory