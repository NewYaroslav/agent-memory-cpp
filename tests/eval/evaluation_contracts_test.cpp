#include <agent_memory/eval/Evaluation.hpp>

#include <cmath>
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

    double require_metric(
        const std::vector<agent_memory::MetricAtK>& metrics,
        std::size_t k
    ) {
        const auto value = agent_memory::metric_value_at(metrics, k);
        return value.value_or(-1.0);
    }

} // namespace

int main() {
    agent_memory::RetrievalEvalDataset dataset;
    dataset.name = "unit-eval";
    dataset.corpus.push_back(agent_memory::EvalCorpusItem{
        "doc:a",
        "Alpha",
        "alpha document",
        {}
    });
    dataset.corpus.push_back(agent_memory::EvalCorpusItem{
        "doc:b",
        "Beta",
        "beta document",
        {}
    });

    dataset.queries.push_back(agent_memory::EvalQuery{
        "q:one",
        "find alpha",
        "QALookup",
        10,
        {}
    });
    dataset.queries.push_back(agent_memory::EvalQuery{
        "q:two",
        "find gamma",
        "FactLookup",
        10,
        {}
    });
    dataset.queries.push_back(agent_memory::EvalQuery{
        "q:none",
        "intentionally unanswerable",
        "NoAnswer",
        10,
        {}
    });

    dataset.judgments.push_back(agent_memory::RelevanceJudgment{
        "q:one",
        "doc:a",
        1
    });
    dataset.judgments.push_back(agent_memory::RelevanceJudgment{
        "q:one",
        "doc:b",
        1
    });
    dataset.judgments.push_back(agent_memory::RelevanceJudgment{
        "q:two",
        "doc:c",
        1
    });
    dataset.judgments.push_back(agent_memory::RelevanceJudgment{
        "q:two",
        "doc:d",
        0
    });

    if(dataset.empty() || dataset.query_count() != 3) {
        return fail("dataset query helpers must report stored queries");
    }

    agent_memory::RetrievalRun run;
    run.name = "hybrid";
    run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q:one",
        {
            agent_memory::RetrievalRunHit{"doc:a", 10.0F, 0, "bm25"},
            agent_memory::RetrievalRunHit{"doc:x", 9.0F, 0, "dense"},
            agent_memory::RetrievalRunHit{"doc:b", 8.0F, 0, "bm25"},
        },
        10.0
    });
    run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q:two",
        {
            agent_memory::RetrievalRunHit{"doc:d", 4.0F, 0, "bm25"},
            agent_memory::RetrievalRunHit{"doc:c", 3.0F, 0, "dense"},
        },
        30.0
    });
    run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q:none",
        {},
        20.0
    });

    const auto metrics = agent_memory::evaluate_retrieval(dataset, run);

    if(metrics.query_count != 3 || metrics.judged_query_count != 2) {
        return fail("metrics must count total and judged queries");
    }

    if(metrics.no_answer_query_count != 1) {
        return fail("metrics must count queries without positive relevance judgments");
    }

    if(!almost_equal(require_metric(metrics.recall_at, 1), 0.25)) {
        return fail("Recall@1 must average over judged queries");
    }

    if(!almost_equal(require_metric(metrics.recall_at, 5), 1.0)) {
        return fail("Recall@5 must include all relevant hits inside cutoff");
    }

    if(!almost_equal(metrics.mrr, 0.75)) {
        return fail("MRR must use the first relevant hit per judged query");
    }

    const auto ndcg_at_10 = require_metric(metrics.ndcg_at, 10);
    if(ndcg_at_10 <= 0.77 || ndcg_at_10 >= 0.78) {
        return fail("nDCG@10 must use graded relevance with logarithmic discount");
    }

    if(!almost_equal(metrics.no_answer_accuracy, 1.0)) {
        return fail("no-answer accuracy must reward empty result sets");
    }

    if(metrics.latency_ms.sample_count != 3 ||
       !almost_equal(metrics.latency_ms.mean, 20.0) ||
       !almost_equal(metrics.latency_ms.p50, 20.0) ||
       !almost_equal(metrics.latency_ms.p95, 30.0)) {
        return fail("latency stats must use measured query latencies");
    }

    agent_memory::RetrievalRun missing_run;
    missing_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q:none",
        {
            agent_memory::RetrievalRunHit{"doc:unexpected", 1.0F, 0, "bm25"},
        },
        std::nullopt
    });

    const auto missing_metrics = agent_memory::evaluate_retrieval(dataset, missing_run);
    if(!almost_equal(require_metric(missing_metrics.recall_at, 10), 0.0)) {
        return fail("missing query runs must behave like empty hit lists");
    }

    if(!almost_equal(missing_metrics.no_answer_accuracy, 0.0)) {
        return fail("no-answer accuracy must reject non-empty no-answer results");
    }

    return 0;
}
