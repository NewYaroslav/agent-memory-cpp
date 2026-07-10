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

} // namespace

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

    if(!almost_equal(
           static_cast<double>(agent_memory::kBaselineNameStub.size()),
           13.0
       )) {
        return fail("kBaselineNameStub must equal \"stub_exact_id\"");
    }

    return 0;
}