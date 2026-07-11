#include <agent_memory/eval/Evaluation.hpp>
#include <agent_memory/eval/IRetrieverAdapter.hpp>
#include <agent_memory/eval/RetrievalEvalRunner.hpp>
#include <agent_memory/eval/StubDataset.hpp>
#include <agent_memory/eval/StubRetriever.hpp>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

    int fail(const std::string& message) {
        std::cerr << message << '\n';
        return 1;
    }

    bool almost_equal(double lhs, double rhs, double epsilon = 0.0001) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    double recall_at_k(const agent_memory::RetrievalMetrics& m, std::size_t k) {
        return agent_memory::metric_value_at(m.recall_at, k).value_or(-1.0);
    }

    std::vector<std::string> corpus_ids(std::size_t n) {
        std::vector<std::string> ids;
        ids.reserve(n);
        for(std::size_t i = 0; i < n; ++i) {
            ids.push_back("doc:" + std::to_string(i));
        }
        return ids;
    }

    void seed_corpus(agent_memory::RetrievalEvalDataset& dataset, std::size_t n) {
        for(std::size_t i = 0; i < n; ++i) {
            agent_memory::EvalCorpusItem item;
            item.id = "doc:" + std::to_string(i);
            item.title = "title-" + std::to_string(i);
            item.text = "text " + std::to_string(i);
            dataset.corpus.push_back(std::move(item));
        }
    }

    bool latency_stats_equal(
        const agent_memory::LatencyStats& a,
        const agent_memory::LatencyStats& b
    ) {
        if(a.sample_count != b.sample_count) return false;
        if(!almost_equal(a.mean, b.mean)) return false;
        if(!almost_equal(a.min, b.min)) return false;
        if(!almost_equal(a.max, b.max)) return false;
        if(!almost_equal(a.p50, b.p50)) return false;
        if(!almost_equal(a.p95, b.p95)) return false;
        if(!almost_equal(a.p99, b.p99)) return false;
        return true;
    }

} // namespace
agent_memory::RetrievalEvalDataset exact_id_dataset(std::size_t corpus_size) {
    agent_memory::RetrievalEvalDataset dataset;
    dataset.name = "exact_id_dataset";
    seed_corpus(dataset, corpus_size);
    for(std::size_t i = 0; i < corpus_size; ++i) {
        const std::string qid = "q:" + std::to_string(i);
        const std::string target = "doc:" + std::to_string(i);
        agent_memory::EvalQuery query;
        query.id = qid;
        query.text = "id:" + target;
        query.query_type = "StubLookup";
        query.limit = 10;
        dataset.queries.push_back(std::move(query));
        agent_memory::RelevanceJudgment j;
        j.query_id = qid;
        j.item_id = target;
        j.relevance_grade = 1;
        dataset.judgments.push_back(std::move(j));
    }
    return dataset;
}

agent_memory::RetrievalEvalDataset noise_dataset(
    std::size_t corpus_size,
    std::size_t query_count
) {
    agent_memory::RetrievalEvalDataset dataset;
    dataset.name = "noise_dataset";
    seed_corpus(dataset, corpus_size);
    for(std::size_t q = 0; q < query_count; ++q) {
        const std::string qid = "q:" + std::to_string(q);
        const std::string target = "doc:" + std::to_string(q % corpus_size);
        agent_memory::EvalQuery query;
        query.id = qid;
        query.text = "noise:" + std::to_string(q);
        query.query_type = "StubLookup";
        query.limit = 10;
        dataset.queries.push_back(std::move(query));
        agent_memory::RelevanceJudgment j;
        j.query_id = qid;
        j.item_id = target;
        j.relevance_grade = 1;
        dataset.judgments.push_back(std::move(j));
    }
    return dataset;
}

// Mixed dataset: judged queries interleaved with Ignore-mode queries so we
// can prove the runner skips Ignore queries entirely (no retrieve() call, no
// latency sample, no hit entry).
agent_memory::RetrievalEvalDataset mixed_ignore_dataset(std::size_t corpus_size) {
    auto dataset = exact_id_dataset(corpus_size);
    for(std::size_t i = 0; i < 3; ++i) {
        agent_memory::EvalQuery q;
        q.id = "q:ignore:" + std::to_string(i);
        q.text = "noise-ignore:" + std::to_string(i);
        q.query_type = "StubLookup";
        q.limit = 10;
        q.answer_mode = agent_memory::EvalQueryAnswerMode::Ignore;
        dataset.queries.push_back(std::move(q));
    }
    dataset.name = "mixed_ignore_dataset";
    return dataset;
}

int main() {
    const std::string baseline{agent_memory::kBaselineNameStub};

    // Exact-id happy path: every query is answered at rank one.
    {
        const auto dataset = exact_id_dataset(32);
        agent_memory::StubRetriever retriever(corpus_ids(32), 42);
        const auto report = agent_memory::run_retrieval_eval(
            retriever,
            dataset,
            baseline
        );
        if(report.baseline_name != baseline) {
            return fail("baseline name must round-trip through the report");
        }
        if(report.metrics.judged_query_count != 32) {
            return fail("exact-id dataset must count all queries as judged");
        }
        if(recall_at_k(report.metrics, 10) < 0.99) {
            return fail("exact-id retriever must produce Recall@10 ~= 1.0");
        }
        if(report.metrics.mrr < 0.99) {
            return fail("exact-id retriever must produce MRR ~= 1.0");
        }
        if(report.latency.sample_count == 0) {
            return fail("latency stats must record at least one sample");
        }
        if(report.latency.max < report.latency.min) {
            return fail("latency max must be at least min");
        }
        if(report.latency.p99 < report.latency.p95) {
            return fail("latency p99 must be at least p95");
        }
    }

    // Random path: every query falls through to the shuffle, so Recall@K
    // must be bounded and latency must still produce samples.
    {
        const auto dataset = noise_dataset(100, 100);
        agent_memory::StubRetriever retriever(corpus_ids(100), 7);
        const auto report = agent_memory::run_retrieval_eval(
            retriever,
            dataset,
            baseline
        );
        const auto recall_10 = recall_at_k(report.metrics, 10);
        if(recall_10 < 0.0 || recall_10 > 0.5) {
            return fail("random-rank retriever must produce Recall@10 in [0, 0.5]");
        }
        if(report.latency.sample_count != 100) {
            return fail("latency stats must record one sample per query");
        }
    }

    // Stub fixture path: the canonical seeded dataset must produce a
    // non-empty run and a non-trivial metric table.
    {
        const auto dataset = agent_memory::make_stub_dataset();
        agent_memory::StubRetriever retriever(
            corpus_ids(dataset.corpus.size()),
            0xC0FFEEu
        );
        const auto report = agent_memory::run_retrieval_eval(
            retriever,
            dataset,
            baseline
        );
        if(report.metrics.judged_query_count != dataset.queries.size()) {
            return fail("StubDataset must yield one judged query per generated query");
        }
        if(report.latency.sample_count != dataset.queries.size()) {
            return fail("runner must measure latency once per query");
        }
        if(recall_at_k(report.metrics, 10) < 0.5) {
            return fail("StubDataset must yield a high Recall@10 baseline");
        }
    }

    // Single-source-of-truth latency: report.latency must mirror
    // report.metrics.latency_ms field-by-field for non-Ignore datasets.
    {
        const auto dataset = noise_dataset(20, 20);
        agent_memory::StubRetriever retriever(corpus_ids(20), 11);
        const auto report = agent_memory::run_retrieval_eval(
            retriever,
            dataset,
            baseline
        );
        if(!latency_stats_equal(report.latency, report.metrics.latency_ms)) {
            return fail(
                "report.latency must mirror report.metrics.latency_ms "
                "(single source of truth for latency)"
            );
        }
    }

    // Ignore-mode queries must be skipped entirely: no retrieve() call,
    // no latency sample, no run entry. The latency sample count must
    // equal the number of non-Ignore queries.
    {
        const auto dataset = mixed_ignore_dataset(8);
        const std::size_t non_ignore_queries =
            dataset.queries.size() - 3; // three Ignore queries appended
        agent_memory::StubRetriever retriever(corpus_ids(8), 13);
        const auto report = agent_memory::run_retrieval_eval(
            retriever,
            dataset,
            baseline
        );
        if(report.metrics.ignored_query_count != 3) {
            return fail("metrics must count 3 ignored queries");
        }
        if(report.metrics.judged_query_count != non_ignore_queries) {
            return fail("metrics must count non-ignore queries as judged");
        }
        if(report.latency.sample_count != non_ignore_queries) {
            return fail(
                "runner must not measure latency for Ignore queries; "
                "expected samples == non-ignore queries"
            );
        }
        if(report.metrics.latency_ms.sample_count != non_ignore_queries) {
            return fail(
                "metrics.latency_ms must skip Ignore queries too"
            );
        }
        for(const auto& query_run : report.run.queries) {
            if(query_run.query_id.rfind("q:ignore:", 0) == 0) {
                return fail(
                    "run must not include Ignore-mode query entries"
                );
            }
        }
        if(!latency_stats_equal(report.latency, report.metrics.latency_ms)) {
            return fail(
                "report.latency and report.metrics.latency_ms must agree "
                "even when Ignore queries are present"
            );
        }
    }

    if(!almost_equal(
           static_cast<double>(agent_memory::kBaselineNameStub.size()),
           13.0
       )) {
        return fail("kBaselineNameStub must equal \"stub_exact_id\"");
    }

    return 0;
}
