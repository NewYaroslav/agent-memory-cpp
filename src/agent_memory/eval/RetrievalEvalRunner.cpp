#include "RetrievalEvalRunner.hpp"

#include "IRetrieverAdapter.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace agent_memory {

    namespace {

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
        // Defensive contract validation. In-memory callers can construct
        // datasets without going through DatasetLoader; this catches them
        // before any retrieval or metric work begins.
        validate_retrieval_eval_dataset(dataset);

        RetrievalEvalReport report;
        report.baseline_name.assign(baseline_name.begin(), baseline_name.end());
        report.dataset_name = dataset.name;
        report.corpus_size = dataset.corpus.size();
        report.query_count = dataset.queries.size();
        report.run = run_retriever(retriever, dataset, baseline_name);
        report.metrics = evaluate_retrieval(dataset, report.run, options);
        // Single source of truth for latency: delegate to the eval layer.
        report.latency = report.metrics.latency_ms;
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