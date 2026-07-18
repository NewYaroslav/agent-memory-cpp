#include <agent_memory/eval/Evaluation.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
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

    template <typename Function>
    bool throws_invalid_argument(Function&& function) {
        try {
            function();
        } catch(const std::invalid_argument&) {
            return true;
        }
        return false;
    }

    agent_memory::RetrievalEvalDataset single_judged_dataset() {
        agent_memory::RetrievalEvalDataset dataset;
        dataset.queries.push_back(agent_memory::EvalQuery{
            "q",
            "query",
            "QALookup",
            10,
            {}
        });
        dataset.judgments.push_back(agent_memory::RelevanceJudgment{
            "q",
            "doc:a",
            1
        });
        return dataset;
    }

    agent_memory::RetrievalEvalDataset graded_two_doc_dataset() {
        agent_memory::RetrievalEvalDataset dataset;
        dataset.queries.push_back(agent_memory::EvalQuery{
            "q",
            "query",
            "QALookup",
            10,
            {}
        });
        dataset.judgments.push_back(agent_memory::RelevanceJudgment{
            "q",
            "doc:high",
            3
        });
        dataset.judgments.push_back(agent_memory::RelevanceJudgment{
            "q",
            "doc:low",
            1
        });
        return dataset;
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
        1,
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
        {},
        agent_memory::EvalQueryAnswerMode::NoAnswer
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
        return fail("metrics must count explicit no-answer queries");
    }

    if(!almost_equal(require_metric(metrics.recall_at, 1), 0.25)) {
        return fail("Recall@1 must average over judged queries");
    }

    if(!almost_equal(require_metric(metrics.recall_at, 5), 1.0)) {
        return fail("Recall@5 must use evaluation cutoffs, not EvalQuery::limit");
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
    if(metrics.empty_result_count != 1
        || !almost_equal(metrics.empty_result_fraction, 1.0 / 3.0)) {
        return fail("empty-result fraction must count evaluated queries only");
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
    if(missing_metrics.empty_result_count != 2
        || !almost_equal(missing_metrics.empty_result_fraction, 2.0 / 3.0)) {
        return fail("missing judged query runs must count as empty evaluated results");
    }

    agent_memory::RetrievalRun implicit_position_run;
    implicit_position_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q",
        {
            agent_memory::RetrievalRunHit{"doc:x", 1.0F, 0, "bm25"},
            agent_memory::RetrievalRunHit{"doc:a", 10.0F, 0, "dense"},
        },
        std::nullopt
    });

    const auto implicit_position_metrics = agent_memory::evaluate_retrieval(
        single_judged_dataset(),
        implicit_position_run,
        agent_memory::RetrievalEvaluationOptions{{1}, {1}}
    );
    if(!almost_equal(require_metric(implicit_position_metrics.recall_at, 1), 0.0)) {
        return fail("implicit-rank hits must use input vector position");
    }
    if(!almost_equal(implicit_position_metrics.mrr, 0.5)) {
        return fail("MRR must use vector position for implicit-rank hits");
    }

    agent_memory::RetrievalRun explicit_rank_run;
    explicit_rank_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q",
        {
            agent_memory::RetrievalRunHit{"doc:x", 10.0F, 2, "bm25"},
            agent_memory::RetrievalRunHit{"doc:a", 1.0F, 1, "dense"},
        },
        std::nullopt
    });

    const auto explicit_rank_metrics = agent_memory::evaluate_retrieval(
        single_judged_dataset(),
        explicit_rank_run,
        agent_memory::RetrievalEvaluationOptions{{1}, {1}}
    );
    if(!almost_equal(require_metric(explicit_rank_metrics.recall_at, 1), 1.0)) {
        return fail("explicit ranks must define metric order before vector position");
    }

    agent_memory::RetrievalRun explicit_gap_run;
    explicit_gap_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q",
        {
            agent_memory::RetrievalRunHit{"doc:a", 1.0F, 100, "dense"},
        },
        std::nullopt
    });

    const auto explicit_gap_metrics = agent_memory::evaluate_retrieval(
        single_judged_dataset(),
        explicit_gap_run,
        agent_memory::RetrievalEvaluationOptions{{10}, {10}}
    );
    if(!almost_equal(require_metric(explicit_gap_metrics.recall_at, 10), 0.0)) {
        return fail("explicit rank gaps must be preserved for Recall@K");
    }
    if(!almost_equal(explicit_gap_metrics.mrr, 0.01)) {
        return fail("explicit rank gaps must be preserved for MRR");
    }

    agent_memory::RetrievalRun duplicate_relevant_hit_run;
    duplicate_relevant_hit_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q",
        {
            agent_memory::RetrievalRunHit{"doc:high", 1.0F, 0, "dense"},
            agent_memory::RetrievalRunHit{"doc:high", 0.9F, 0, "bm25"},
        },
        std::nullopt
    });
    const auto duplicate_relevant_hit_metrics = agent_memory::evaluate_retrieval(
        graded_two_doc_dataset(),
        duplicate_relevant_hit_run,
        agent_memory::RetrievalEvaluationOptions{{2}, {2}}
    );
    if(!almost_equal(require_metric(duplicate_relevant_hit_metrics.recall_at, 2), 0.5)) {
        return fail("duplicate relevant hits must not inflate Recall@K");
    }
    const auto duplicate_ndcg =
        require_metric(duplicate_relevant_hit_metrics.ndcg_at, 2);
    if(duplicate_ndcg <= 0.9 || duplicate_ndcg >= 1.0) {
        return fail("duplicate relevant hits must not inflate nDCG@K");
    }

    agent_memory::RetrievalRun high_grade_first_run;
    high_grade_first_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q",
        {
            agent_memory::RetrievalRunHit{"doc:high", 1.0F, 0, "dense"},
            agent_memory::RetrievalRunHit{"doc:low", 0.9F, 0, "bm25"},
        },
        std::nullopt
    });
    agent_memory::RetrievalRun low_grade_first_run;
    low_grade_first_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q",
        {
            agent_memory::RetrievalRunHit{"doc:low", 1.0F, 0, "bm25"},
            agent_memory::RetrievalRunHit{"doc:high", 0.9F, 0, "dense"},
        },
        std::nullopt
    });
    const auto high_grade_first_metrics = agent_memory::evaluate_retrieval(
        graded_two_doc_dataset(),
        high_grade_first_run,
        agent_memory::RetrievalEvaluationOptions{{2}, {2}}
    );
    const auto low_grade_first_metrics = agent_memory::evaluate_retrieval(
        graded_two_doc_dataset(),
        low_grade_first_run,
        agent_memory::RetrievalEvaluationOptions{{2}, {2}}
    );
    if(!almost_equal(require_metric(high_grade_first_metrics.recall_at, 2), 1.0)
        || !almost_equal(require_metric(low_grade_first_metrics.recall_at, 2), 1.0)) {
        return fail("Recall@K must ignore ordering once all relevant docs are found");
    }
    if(!almost_equal(require_metric(high_grade_first_metrics.ndcg_at, 2), 1.0)) {
        return fail("ideal graded ranking must produce nDCG@K ~= 1.0");
    }
    if(
        require_metric(low_grade_first_metrics.ndcg_at, 2) >=
        require_metric(high_grade_first_metrics.ndcg_at, 2)
    ) {
        return fail("nDCG@K must reward higher-grade documents at earlier ranks");
    }

    agent_memory::RetrievalEvalDataset ignored_dataset = single_judged_dataset();
    ignored_dataset.queries.push_back(agent_memory::EvalQuery{
        "q:ignore",
        "ignore me",
        "Debug",
        10,
        {},
        agent_memory::EvalQueryAnswerMode::Ignore
    });
    const auto ignored_metrics = agent_memory::evaluate_retrieval(
        ignored_dataset,
        implicit_position_run
    );
    if(ignored_metrics.ignored_query_count != 1 || ignored_metrics.judged_query_count != 1) {
        return fail("ignored queries must be counted and skipped");
    }

    agent_memory::RetrievalRun ignored_non_empty_run;
    ignored_non_empty_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q",
        {},
        std::nullopt
    });
    ignored_non_empty_run.queries.push_back(agent_memory::RetrievalQueryRun{
        "q:ignore",
        {agent_memory::RetrievalRunHit{"doc:ignored", 1.0F, 0, "debug"}},
        std::nullopt
    });
    const auto ignored_non_empty_metrics = agent_memory::evaluate_retrieval(
        ignored_dataset,
        ignored_non_empty_run
    );
    if(ignored_non_empty_metrics.empty_result_count != 1
        || !almost_equal(ignored_non_empty_metrics.empty_result_fraction, 1.0)) {
        return fail("ignored non-empty runs must not mask evaluated empty results");
    }
    if(ignored_non_empty_metrics.evaluated_query_count != 1
        || ignored_non_empty_metrics.evaluated_query_run_count != 1
        || ignored_non_empty_metrics.evaluated_query_latency_count != 0
        || ignored_non_empty_metrics.ignored_query_run_count != 1) {
        return fail("coverage counters must distinguish evaluated and ignored runs");
    }

    {
        agent_memory::RetrievalEvalDataset coverage_dataset =
            single_judged_dataset();
        coverage_dataset.queries.push_back(agent_memory::EvalQuery{
            "q:ignore",
            "ignore me",
            "Debug",
            10,
            {},
            agent_memory::EvalQueryAnswerMode::Ignore
        });
        agent_memory::RetrievalRun ignored_only_run;
        ignored_only_run.queries.push_back(agent_memory::RetrievalQueryRun{
            "q:ignore",
            {agent_memory::RetrievalRunHit{"doc:ignored", 1.0F, 0, "debug"}},
            1.0
        });
        const auto coverage_metrics = agent_memory::evaluate_retrieval(
            coverage_dataset,
            ignored_only_run
        );
        if(coverage_metrics.evaluated_query_count != 1
            || coverage_metrics.evaluated_query_run_count != 0
            || coverage_metrics.evaluated_query_latency_count != 0
            || coverage_metrics.ignored_query_run_count != 1
            || coverage_metrics.latency_ms.sample_count != 0) {
            return fail("ignored-only runs must not count as evaluated coverage");
        }
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalEvalDataset invalid = single_judged_dataset();
           invalid.queries.push_back(invalid.queries.front());
           agent_memory::RetrievalRun empty_run;
           (void)agent_memory::evaluate_retrieval(invalid, empty_run);
       })) {
        return fail("duplicate query ids must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalEvalDataset invalid = single_judged_dataset();
           invalid.judgments.push_back(invalid.judgments.front());
           agent_memory::RetrievalRun empty_run;
           (void)agent_memory::evaluate_retrieval(invalid, empty_run);
       })) {
        return fail("duplicate qrels must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalEvalDataset invalid = single_judged_dataset();
           invalid.judgments.push_back(agent_memory::RelevanceJudgment{
               "q:unknown",
               "doc:x",
               1
           });
           agent_memory::RetrievalRun empty_run;
           (void)agent_memory::evaluate_retrieval(invalid, empty_run);
       })) {
        return fail("qrels for unknown query ids must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalEvalDataset invalid = single_judged_dataset();
           invalid.judgments.clear();
           agent_memory::RetrievalRun empty_run;
           (void)agent_memory::evaluate_retrieval(invalid, empty_run);
       })) {
        return fail("judged queries without positive qrels must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalRun invalid;
           invalid.queries.push_back(agent_memory::RetrievalQueryRun{
               "q",
               {agent_memory::RetrievalRunHit{"doc:a", 1.0F, 0, "bm25"}},
               std::nullopt
           });
           invalid.queries.push_back(invalid.queries.front());
           (void)agent_memory::evaluate_retrieval(single_judged_dataset(), invalid);
       })) {
        return fail("duplicate run query ids must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalRun invalid;
           invalid.queries.push_back(agent_memory::RetrievalQueryRun{
               "q",
               {
                   agent_memory::RetrievalRunHit{"doc:a", 1.0F, 1, "bm25"},
                   agent_memory::RetrievalRunHit{"doc:b", 2.0F, 1, "dense"},
               },
               std::nullopt
           });
           (void)agent_memory::evaluate_retrieval(single_judged_dataset(), invalid);
       })) {
        return fail("duplicate explicit ranks must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalRun invalid;
           invalid.queries.push_back(agent_memory::RetrievalQueryRun{
               "q",
               {
                   agent_memory::RetrievalRunHit{"doc:a", 1.0F, 1, "bm25"},
                   agent_memory::RetrievalRunHit{"doc:b", 2.0F, 0, "dense"},
               },
               std::nullopt
           });
           (void)agent_memory::evaluate_retrieval(single_judged_dataset(), invalid);
       })) {
        return fail("mixed explicit and implicit ranks must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalEvalDataset ignored = single_judged_dataset();
           ignored.queries.push_back(agent_memory::EvalQuery{
               "q:ignore",
               "ignore me",
               "Debug",
               10,
               {},
               agent_memory::EvalQueryAnswerMode::Ignore
           });
           agent_memory::RetrievalRun invalid;
           invalid.queries.push_back(agent_memory::RetrievalQueryRun{
               "q",
               {agent_memory::RetrievalRunHit{"doc:a", 1.0F, 0, "bm25"}},
               std::nullopt
           });
           invalid.queries.push_back(agent_memory::RetrievalQueryRun{
               "q:ignore",
               {agent_memory::RetrievalRunHit{"", 1.0F, 0, "bm25"}},
               std::nullopt
           });
           (void)agent_memory::evaluate_retrieval(ignored, invalid);
       })) {
        return fail("ignored query runs must still validate hit payloads");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalRun run_with_zero_cutoff;
           (void)agent_memory::evaluate_retrieval(
               single_judged_dataset(),
               run_with_zero_cutoff,
               agent_memory::RetrievalEvaluationOptions{{0}, {1}}
           );
       })) {
        return fail("zero Recall cutoff must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalRun run_with_zero_cutoff;
           (void)agent_memory::evaluate_retrieval(
               single_judged_dataset(),
               run_with_zero_cutoff,
               agent_memory::RetrievalEvaluationOptions{{1}, {0}}
           );
       })) {
        return fail("zero nDCG cutoff must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalRun invalid;
           invalid.queries.push_back(agent_memory::RetrievalQueryRun{
               "q",
               {agent_memory::RetrievalRunHit{"doc:a", 1.0F, 0, "bm25"}},
               -1.0
           });
           (void)agent_memory::evaluate_retrieval(single_judged_dataset(), invalid);
       })) {
        return fail("negative latency must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalRun invalid;
           invalid.queries.push_back(agent_memory::RetrievalQueryRun{
               "q",
               {agent_memory::RetrievalRunHit{"doc:a", 1.0F, 0, "bm25"}},
               std::numeric_limits<double>::quiet_NaN()
           });
           (void)agent_memory::evaluate_retrieval(single_judged_dataset(), invalid);
       })) {
        return fail("NaN latency must be rejected");
    }

    if(!throws_invalid_argument([] {
           agent_memory::RetrievalRun invalid;
           invalid.queries.push_back(agent_memory::RetrievalQueryRun{
               "q",
               {agent_memory::RetrievalRunHit{"doc:a", 1.0F, 0, "bm25"}},
               std::numeric_limits<double>::infinity()
           });
           (void)agent_memory::evaluate_retrieval(single_judged_dataset(), invalid);
       })) {
        return fail("infinite latency must be rejected");
    }

    return 0;
}
