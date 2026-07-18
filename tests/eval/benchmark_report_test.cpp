#include <agent_memory/eval/BenchmarkReport.hpp>

#if defined(AGENT_MEMORY_ENABLE_JSON) && AGENT_MEMORY_ENABLE_JSON
#include <nlohmann/json.hpp>
#endif

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

    int fail(const std::string& message) {
        std::cerr << message << '\n';
        return 1;
    }

    [[nodiscard]] bool almost_equal(double lhs, double rhs, double epsilon = 1e-9) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    template <typename Function>
    [[nodiscard]] bool throws_invalid_argument(Function&& function) {
        try {
            function();
        } catch(const std::invalid_argument&) {
            return true;
        }
        return false;
    }

    [[nodiscard]] agent_memory::RetrievalEvalReport make_eval_report() {
        agent_memory::RetrievalEvalReport report;
        report.baseline_name = "exact_test";
        report.dataset_name = "fixture_v1";
        report.corpus_size = 12;
        report.query_count = 4;
        report.metrics.query_count = 4;
        report.metrics.judged_query_count = 2;
        report.metrics.no_answer_query_count = 1;
        report.metrics.ignored_query_count = 1;
        report.metrics.recall_at = {
            {1, 0.25},
            {5, 0.50},
            {10, 0.75},
            {50, 1.0}
        };
        report.metrics.ndcg_at = {{10, 0.625}};
        report.metrics.mrr = 0.375;
        report.metrics.no_answer_accuracy = 1.0;
        report.metrics.empty_result_count = 1;
        report.metrics.empty_result_fraction = 1.0 / 3.0;
        report.metrics.latency_ms.sample_count = 3;
        report.metrics.latency_ms.mean = 2.0;
        report.metrics.latency_ms.min = 1.0;
        report.metrics.latency_ms.max = 3.0;
        report.metrics.latency_ms.p50 = 2.0;
        report.metrics.latency_ms.p95 = 3.0;
        report.metrics.latency_ms.p99 = 3.0;
        report.latency = report.metrics.latency_ms;

        agent_memory::RetrievalQueryRun first;
        first.query_id = "query:1";
        first.hits.push_back(agent_memory::RetrievalRunHit{"doc:1", 1.0F, 0, "exact_test"});
        report.run.queries.push_back(std::move(first));

        agent_memory::RetrievalQueryRun second;
        second.query_id = "query:2";
        report.run.queries.push_back(std::move(second));

        agent_memory::RetrievalQueryRun third;
        third.query_id = "query:3";
        third.hits.push_back(agent_memory::RetrievalRunHit{"doc:3", 0.5F, 0, "exact_test"});
        report.run.queries.push_back(std::move(third));
        return report;
    }

    [[nodiscard]] agent_memory::BenchmarkMeasurements make_measurements() {
        agent_memory::BenchmarkMeasurements measurements;
        measurements.total_benchmark_time_ms = 20.0;
        measurements.oov_fraction = 0.125;
        measurements.index.vocabulary_size = 42;
        measurements.index.corpus_ingest_time_ms = 1.25;
        measurements.index.embedding_time_ms = 2.5;
        measurements.index.index_build_time_ms = 3.75;
        measurements.index.peak_resident_set_bytes = 4096;
        measurements.index.document_count = 12;
        measurements.index.mean_document_length = 8.5;
        return measurements;
    }

} // namespace

int main() {
    const auto report = agent_memory::make_benchmark_report(
        make_eval_report(),
        "benchmark_fixture",
        make_measurements()
    );

    if(report.schema_version != agent_memory::BenchmarkReport::kSchemaVersion) {
        return fail("schema version must use the current BenchmarkReport version");
    }
    if(!almost_equal(report.quality.recall_at_10, 0.75)) {
        return fail("Recall@10 must map from RetrievalMetrics");
    }
    if(!almost_equal(report.quality.ndcg_at_10, 0.625)) {
        return fail("nDCG@10 must map from RetrievalMetrics");
    }
    if(!almost_equal(report.quality.empty_result_fraction, 1.0 / 3.0)) {
        return fail("one empty result among three evaluated queries must equal 1/3");
    }
    if(report.speed.measured_query_count != 3) {
        return fail("measured query count must map from evaluated query count");
    }
    if(!almost_equal(report.speed.queries_per_second, 150.0)) {
        return fail("3 queries over 20ms must equal 150 queries/second");
    }
    if(report.index.peak_resident_set_bytes != 4096) {
        return fail("peak RSS must round-trip through benchmark measurements");
    }
    if(report.pr29_hooks.candidate_count_before_filter != 0.0
        || report.pr29_hooks.candidate_set_recall != 0.0) {
        return fail("roadmap-label PR #29 hooks must default to zero");
    }

    std::ostringstream summary;
    agent_memory::print_benchmark_report(summary, report);
    const std::string summary_text = summary.str();
    for(const std::string expected : {
        "Quality:",
        "Speed:",
        "Index:",
        "Roadmap-label PR #29 hooks:",
        "Recall@10:"
    }) {
        if(summary_text.find(expected) == std::string::npos) {
            return fail("human summary is missing section/value: " + expected);
        }
    }

#if defined(AGENT_MEMORY_ENABLE_JSON) && AGENT_MEMORY_ENABLE_JSON
    const auto json = nlohmann::json::parse(
        agent_memory::serialize_benchmark_report_json(report)
    );
    if(json.size() != 8 || json.at("schema_version") != 1) {
        return fail("JSON root must contain the versioned report fields");
    }
    if(json.at("quality").size() != 9
        || json.at("speed").size() != 7
        || json.at("index").size() != 7
        || json.at("pr29_hooks").size() != 6) {
        return fail("JSON sections must contain every BenchmarkReport metric");
    }
    if(!almost_equal(json.at("quality").at("recall_at_10").get<double>(), 0.75)) {
        return fail("JSON must serialize Recall@10");
    }
    if(json.at("index").at("peak_resident_set_bytes").get<std::uint64_t>() != 4096) {
        return fail("JSON must serialize peak RSS as bytes");
    }
    if(json.at("pr29_hooks").at("candidate_count_before_filter").get<double>() != 0.0) {
        return fail("JSON must keep exact-retriever hook values at zero");
    }
#endif

    {
        auto invalid = report;
        invalid.quality.oov_fraction = std::numeric_limits<double>::quiet_NaN();
        bool threw = false;
        try {
            agent_memory::validate_benchmark_report(invalid);
        } catch(const std::invalid_argument&) {
            threw = true;
        }
        if(!threw) {
            return fail("validation must reject NaN metric values");
        }
    }

    {
        auto invalid = report;
        invalid.speed.p99_latency_ms = invalid.speed.p50_latency_ms - 0.5;
        bool threw = false;
        try {
            agent_memory::validate_benchmark_report(invalid);
        } catch(const std::invalid_argument&) {
            threw = true;
        }
        if(!threw) {
            return fail("validation must reject non-monotonic latency percentiles");
        }
    }

    {
        auto eval_report = make_eval_report();
        eval_report.metrics.recall_at.pop_back();
        if(!throws_invalid_argument([&] {
               (void)agent_memory::make_benchmark_report(
                   eval_report,
                   "benchmark_fixture",
                   make_measurements()
               );
           })) {
            return fail("missing required Recall cutoffs must be rejected");
        }
    }

    {
        auto eval_report = make_eval_report();
        eval_report.run.queries.pop_back();
        if(!throws_invalid_argument([&] {
               (void)agent_memory::make_benchmark_report(
                   eval_report,
                   "benchmark_fixture",
                   make_measurements()
               );
           })) {
            return fail("benchmark reports require one run per evaluated query");
        }
    }

    {
        auto eval_report = make_eval_report();
        eval_report.metrics.latency_ms.sample_count = 2;
        if(!throws_invalid_argument([&] {
               (void)agent_memory::make_benchmark_report(
                   eval_report,
                   "benchmark_fixture",
                   make_measurements()
               );
           })) {
            return fail("benchmark reports require latency for every evaluated query");
        }
    }

    {
        auto invalid = report;
        invalid.speed.queries_per_second += 1.0;
        if(!throws_invalid_argument([&] {
               agent_memory::validate_benchmark_report(invalid);
           })) {
            return fail("validation must reject inconsistent throughput");
        }
    }

    {
        auto invalid = report;
        invalid.pr29_hooks.candidate_count_before_filter = 10.0;
        invalid.pr29_hooks.candidate_count_after_filter = 20.0;
        invalid.pr29_hooks.candidate_reduction_ratio = 0.0;
        if(!throws_invalid_argument([&] {
               agent_memory::validate_benchmark_report(invalid);
           })) {
            return fail("PR29 hooks must reject after-count above before-count");
        }
    }

    {
        auto invalid = report;
        invalid.pr29_hooks.candidate_count_before_filter = 10.0;
        invalid.pr29_hooks.candidate_count_after_filter = 4.0;
        invalid.pr29_hooks.candidate_reduction_ratio = 0.9;
        if(!throws_invalid_argument([&] {
               agent_memory::validate_benchmark_report(invalid);
           })) {
            return fail("PR29 hooks must reject inconsistent reduction ratio");
        }
    }

    return 0;
}
