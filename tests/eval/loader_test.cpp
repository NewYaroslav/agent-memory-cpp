#include <agent_memory/eval.hpp>

#include <agent_memory/eval/DatasetLoader.hpp>
#include <agent_memory/eval/IRetrieverAdapter.hpp>
#include <agent_memory/eval/RetrievalEvalRunner.hpp>
#include <agent_memory/eval/StubDataset.hpp>
#include <agent_memory/eval/StubRetriever.hpp>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

    int fail(const std::string& message) {
        std::cerr << "loader_test: " << message << '\n';
        return 1;
    }

    // Defined below; declared here so main() can call it.
    int main_validation_tests();

    std::vector<std::string> corpus_ids_from(const agent_memory::RetrievalEvalDataset& ds) {
        std::vector<std::string> ids;
        ids.reserve(ds.corpus.size());
        for(const auto& item : ds.corpus) {
            ids.push_back(item.id);
        }
        return ids;
    }

    bool retrieval_metrics_equal(
        const agent_memory::RetrievalMetrics& a,
        const agent_memory::RetrievalMetrics& b
    ) {
        if(a.query_count != b.query_count) return false;
        if(a.judged_query_count != b.judged_query_count) return false;
        if(a.no_answer_query_count != b.no_answer_query_count) return false;
        if(a.ignored_query_count != b.ignored_query_count) return false;
        const auto almost = [](double lhs, double rhs) {
            return std::fabs(lhs - rhs) <= 1e-9;
        };
        if(!almost(a.mrr, b.mrr)) return false;
        if(!almost(a.no_answer_accuracy, b.no_answer_accuracy)) return false;
        if(a.recall_at.size() != b.recall_at.size()) return false;
        for(std::size_t i = 0; i < a.recall_at.size(); ++i) {
            if(a.recall_at[i].k != b.recall_at[i].k) return false;
            if(!almost(a.recall_at[i].value, b.recall_at[i].value)) return false;
        }
        if(a.ndcg_at.size() != b.ndcg_at.size()) return false;
        for(std::size_t i = 0; i < a.ndcg_at.size(); ++i) {
            if(a.ndcg_at[i].k != b.ndcg_at[i].k) return false;
            if(!almost(a.ndcg_at[i].value, b.ndcg_at[i].value)) return false;
        }
        // Latency stats depend on steady_clock and are intentionally skipped:
        // the runner captures wall-clock per query, which differs across runs.
        return true;
    }

    // Mirror `tests/eval/fixtures/sample_v1.json` exactly. Any drift between
    // this dataset and the on-disk fixture breaks the parity test below.
    agent_memory::RetrievalEvalDataset mirrored_fixture_dataset() {
        agent_memory::RetrievalEvalDataset ds;
        ds.name = "sample_v1";

        const std::vector<std::pair<std::string, std::string>> corpus = {
            {"doc:0", "en?"}, // doc:0 = no lang metadata
            {"doc:1", "en"},
            {"doc:2", "en?"},
            {"doc:3", "en"},
            {"doc:4", "en?"}
        };
        // The fixture only sets metadata for some items; we mirror that.
        ds.corpus.reserve(corpus.size());
        for(std::size_t i = 0; i < corpus.size(); ++i) {
            agent_memory::EvalCorpusItem item;
            item.id = "doc:" + std::to_string(i);
            item.title = "title-" + std::to_string(i);
            item.text = "text payload #" + std::to_string(i);
            if(corpus[i].second == "en") {
                item.metadata.set("lang", "en");
            }
            ds.corpus.push_back(std::move(item));
        }

        struct Q { const char* text; };
        const std::vector<Q> queries = {
            {"id:doc:1"},
            {"id:doc:3"},
            {"noise:2"},
            {"id:doc:0"}
        };
        ds.queries.reserve(queries.size());
        for(std::size_t i = 0; i < queries.size(); ++i) {
            agent_memory::EvalQuery q;
            q.id = "q:" + std::to_string(i);
            q.text = queries[i].text;
            q.query_type = "StubLookup";
            q.limit = 10;
            ds.queries.push_back(std::move(q));
        }

        const std::vector<std::int32_t> grades = {3, 2, 1, 2};
        ds.judgments.reserve(grades.size());
        for(std::size_t i = 0; i < grades.size(); ++i) {
            agent_memory::RelevanceJudgment j;
            j.query_id = "q:" + std::to_string(i);
            j.item_id = "doc:" + std::to_string(queries[i].text == std::string("noise:2") ? 2 : (i == 0 ? 1 : (i == 1 ? 3 : 0)));
            j.relevance_grade = grades[i];
            ds.judgments.push_back(std::move(j));
        }

        return ds;
    }

} // namespace

int main() {
    namespace fs = std::filesystem;

    const fs::path fixture_path = fs::path("fixtures") / "sample_v1.json";
    std::error_code ec;
    const auto absolute_fixture = fs::absolute(fixture_path, ec);
    if(ec) {
        return fail("cannot resolve fixture path: " + ec.message());
    }
    if(!fs::exists(absolute_fixture)) {
        return fail("missing fixture file: " + absolute_fixture.string());
    }

    agent_memory::RetrievalEvalDataset json_dataset;
    try {
        json_dataset = agent_memory::load_dataset_from_json_file(
            absolute_fixture
        );
    } catch(const std::exception& err) {
        return fail(
            std::string("loading fixture threw: ") + err.what()
        );
    }
    if(json_dataset.name != "sample_v1") {
        return fail("dataset name mismatch");
    }
    if(json_dataset.corpus.size() != 5) {
        return fail("corpus size mismatch");
    }
    if(json_dataset.queries.size() != 4) {
        return fail("queries size mismatch");
    }
    if(json_dataset.judgments.size() != 4) {
        return fail("judgments size mismatch");
    }

    // Case 1: end-to-end run through the loaded dataset must produce a
    // non-trivial Recall@10 because three of four queries have an id:doc:N
    // prefix that StubRetriever answers at rank 1.
    {
        const std::string baseline{agent_memory::kBaselineNameStub};
        agent_memory::StubRetriever retriever(
            corpus_ids_from(json_dataset),
            0xC0FFEEu
        );
        const auto report = agent_memory::run_retrieval_eval(
            retriever,
            json_dataset,
            baseline
        );
        const auto recall_10 = agent_memory::metric_value_at(
            report.metrics.recall_at, 10
        );
        if(!recall_10 || *recall_10 <= 0.0) {
            return fail("expected non-zero Recall@10 from loaded fixture");
        }
    }

    // Case 2: round-trip parity. Build an in-memory dataset that mirrors the
    // fixture byte-for-byte and confirm the runner produces identical metrics.
    {
        const auto mirror = mirrored_fixture_dataset();
        // Confirm the mirrored payload actually mirrors before running it.
        if(mirror.queries.size() != json_dataset.queries.size()) {
            return fail("mirror drift: query count differs from fixture");
        }
        for(std::size_t i = 0; i < mirror.queries.size(); ++i) {
            if(mirror.queries[i].id != json_dataset.queries[i].id) {
                return fail("mirror drift: query id differs from fixture");
            }
            if(mirror.queries[i].text != json_dataset.queries[i].text) {
                return fail("mirror drift: query text differs from fixture");
            }
        }

        const std::string baseline{agent_memory::kBaselineNameStub};
        agent_memory::StubRetriever retriever_from_json(
            corpus_ids_from(json_dataset),
            0xC0FFEEu
        );
        agent_memory::StubRetriever retriever_from_mirror(
            corpus_ids_from(mirror),
            0xC0FFEEu
        );
        const auto json_report = agent_memory::run_retrieval_eval(
            retriever_from_json,
            json_dataset,
            baseline
        );
        const auto mirror_report = agent_memory::run_retrieval_eval(
            retriever_from_mirror,
            mirror,
            baseline
        );
        if(!retrieval_metrics_equal(
               json_report.metrics,
               mirror_report.metrics
           )) {
            return fail(
                "metrics from JSON-loaded dataset differ from in-memory mirror"
            );
        }
    }

    // Case 3: malformed JSON must surface as std::runtime_error with a
    // descriptive message that includes "malformed JSON".
    {
        const auto malformed_path = fs::temp_directory_path(ec) /
            "agent_memory_dataset_loader_malformed.json";
        {
            std::ofstream out(malformed_path, std::ios::binary);
            if(!out) {
                return fail("cannot write malformed fixture");
            }
            out << "{not valid json";
        }
        bool threw = false;
        std::string message;
        try {
            (void)agent_memory::load_dataset_from_json_file(malformed_path);
        } catch(const std::runtime_error& err) {
            threw = true;
            message = err.what();
        } catch(...) {
            // Wrong exception type — fail below with a clearer message.
        }
        std::error_code remove_ec;
        fs::remove(malformed_path, remove_ec);
        if(!threw) {
            return fail("malformed JSON did not throw std::runtime_error");
        }
        if(message.find("malformed JSON") == std::string::npos) {
            return fail(
                "malformed JSON message must contain 'malformed JSON'; got: "
                    + message
            );
        }
    }

    // Case 4: the in-memory string variant must agree with the file variant
    // when given the same JSON text.
    {
        std::ifstream in(absolute_fixture, std::ios::binary);
        if(!in) {
            return fail("cannot reopen fixture for string test");
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();

        agent_memory::RetrievalEvalDataset a;
        agent_memory::RetrievalEvalDataset b;
        try {
            a = agent_memory::load_dataset_from_json_file(absolute_fixture);
            b = agent_memory::load_dataset_from_json_string(text);
        } catch(const std::exception& err) {
            return fail(
                std::string("string loader threw: ") + err.what()
            );
        }
        if(a.queries.size() != b.queries.size()) {
            return fail("file vs string loader: query count mismatch");
        }
        for(std::size_t i = 0; i < a.queries.size(); ++i) {
            if(a.queries[i].text != b.queries[i].text) {
                return fail("file vs string loader: query text mismatch");
            }
        }
        if(a.judgments.size() != b.judgments.size()) {
            return fail("file vs string loader: judgment count mismatch");
        }
    }

    if(const int rc = main_validation_tests(); rc != 0) {
        return rc;
    }

    return 0;
}

namespace {

// Helper for testing validate_retrieval_eval_dataset messages.
bool throws_runtime_message(
    std::string_view needle,
    const std::function<void()>& fn
) {
    try {
        fn();
    } catch(const std::runtime_error& err) {
        return std::string(err.what()).find(std::string{needle}) != std::string::npos;
    } catch(...) {
        return false;
    }
    return false;
}
int main_validation_tests() {
    using agent_memory::EvalCorpusItem;
    using agent_memory::EvalQuery;
    using agent_memory::RelevanceJudgment;
    using agent_memory::EvalQueryAnswerMode;
    using agent_memory::RetrievalEvalDataset;
    using agent_memory::validate_retrieval_eval_dataset;

    // Baseline: a known-good dataset that mirrors the fixture must
    // validate without throwing.
    {
        const auto good = mirrored_fixture_dataset();
        try {
            validate_retrieval_eval_dataset(good);
        } catch(const std::exception& err) {
            return fail(
                std::string("valid fixture dataset must validate: ")
                + err.what()
            );
        }
    }

    // Empty query id is rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.queries.front().id.clear();
        if(!throws_runtime_message("id must not be empty", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail("empty query id must throw std::runtime_error");
        }
    }

    // Empty corpus id is rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.corpus.front().id.clear();
        if(!throws_runtime_message("id must not be empty", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail("empty corpus id must throw std::runtime_error");
        }
    }

    // Empty judgment query_id/item_id is rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.judgments.front().query_id.clear();
        if(!throws_runtime_message("non-empty query_id/item_id", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail("empty judgment query_id must throw std::runtime_error");
        }
    }

    // Duplicate corpus ids are rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.corpus[1].id = bad.corpus[0].id;
        if(!throws_runtime_message("duplicate corpus id", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail("duplicate corpus id must throw std::runtime_error");
        }
    }

    // Duplicate query ids are rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.queries[1].id = bad.queries[0].id;
        if(!throws_runtime_message("duplicate query id", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail("duplicate query id must throw std::runtime_error");
        }
    }

    // Duplicate (query_id, item_id) judgment pairs are rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.judgments.push_back(bad.judgments.front());
        if(!throws_runtime_message("duplicate judgment", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail("duplicate judgment pair must throw std::runtime_error");
        }
    }

    // Query referencing an unknown corpus item is rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.judgments.front().item_id = "doc:does-not-exist";
        if(!throws_runtime_message("unknown corpus item id", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail(
                "judgment referencing unknown corpus item "
                "must throw std::runtime_error"
            );
        }
    }

    // Query referencing an unknown query id is rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.judgments.front().query_id = "q:unknown";
        if(!throws_runtime_message("unknown query id", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail(
                "judgment referencing unknown query id "
                "must throw std::runtime_error"
            );
        }
    }

    // limit == 0 is rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.queries.front().limit = 0;
        if(!throws_runtime_message("limit must be greater than zero", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail("limit == 0 must throw std::runtime_error");
        }
    }

    // Empty query text is rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.queries.front().text.clear();
        if(!throws_runtime_message("text must not be empty", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail("empty query text must throw std::runtime_error");
        }
    }

    // JudgedRetrieval query without any judgments is rejected.
    {
        auto bad = mirrored_fixture_dataset();
        bad.judgments.clear();
        if(!throws_runtime_message("JudgedRetrieval", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail(
                "JudgedRetrieval with no judgments "
                "must throw std::runtime_error"
            );
        }
    }

    // NoAnswer query with a positive relevance judgment is rejected.
    {
        RetrievalEvalDataset bad;
        bad.name = "no_answer_dataset";
        EvalCorpusItem c;
        c.id = "doc:a";
        c.text = "alpha";
        bad.corpus.push_back(c);
        EvalQuery q;
        q.id = "q:n";
        q.text = "anything";
        q.limit = 5;
        q.answer_mode = EvalQueryAnswerMode::NoAnswer;
        bad.queries.push_back(q);
        RelevanceJudgment j;
        j.query_id = "q:n";
        j.item_id = "doc:a";
        j.relevance_grade = 1;
        bad.judgments.push_back(j);
        if(!throws_runtime_message("NoAnswer", [&] {
            validate_retrieval_eval_dataset(bad);
        })) {
            return fail(
                "NoAnswer with positive qrel must throw std::runtime_error"
            );
        }
    }

    // NoAnswer query with only zero-grade judgments is allowed (the
    // explicit zero-grade marks the items as non-relevant, which is
    // consistent with the no-answer intent).
    {
        RetrievalEvalDataset ds;
        ds.name = "no_answer_zero_grade_allowed";
        EvalCorpusItem c;
        c.id = "doc:a";
        c.text = "alpha";
        ds.corpus.push_back(c);
        EvalQuery q;
        q.id = "q:n";
        q.text = "anything";
        q.limit = 5;
        q.answer_mode = EvalQueryAnswerMode::NoAnswer;
        ds.queries.push_back(q);
        RelevanceJudgment j;
        j.query_id = "q:n";
        j.item_id = "doc:a";
        j.relevance_grade = 0;
        ds.judgments.push_back(j);
        try {
            validate_retrieval_eval_dataset(ds);
        } catch(const std::exception& err) {
            return fail(
                std::string(
                    "NoAnswer with only zero-grade qrels must validate; got: "
                )
                + err.what()
            );
        }
    }

    // NoAnswer query with a mix of zero-grade and positive judgments is
    // rejected because the positive grade contradicts the no-answer intent.
    {
        RetrievalEvalDataset ds;
        ds.name = "no_answer_mixed_grades_rejected";
        EvalCorpusItem c0;
        c0.id = "doc:a";
        c0.text = "alpha";
        ds.corpus.push_back(c0);
        EvalCorpusItem c1;
        c1.id = "doc:b";
        c1.text = "bravo";
        ds.corpus.push_back(c1);
        EvalQuery q;
        q.id = "q:n";
        q.text = "anything";
        q.limit = 5;
        q.answer_mode = EvalQueryAnswerMode::NoAnswer;
        ds.queries.push_back(q);
        RelevanceJudgment j0;
        j0.query_id = "q:n";
        j0.item_id = "doc:a";
        j0.relevance_grade = 0;
        ds.judgments.push_back(j0);
        RelevanceJudgment j1;
        j1.query_id = "q:n";
        j1.item_id = "doc:b";
        j1.relevance_grade = 2;
        ds.judgments.push_back(j1);
        if(!throws_runtime_message("NoAnswer", [&] {
            validate_retrieval_eval_dataset(ds);
        })) {
            return fail(
                "NoAnswer with mixed zero/positive qrels "
                "must throw std::runtime_error"
            );
        }
    }

    return 0;
}

} // namespace
