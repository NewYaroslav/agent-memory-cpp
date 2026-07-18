#include <agent_memory/eval/BenchmarkReport.hpp>
#include <agent_memory/eval/DatasetLoader.hpp>
#include <agent_memory/eval/Evaluation.hpp>
#include <agent_memory/eval/IRetrieverAdapter.hpp>
#include <agent_memory/retrieval/BowVectorRetriever.hpp>
#include <agent_memory/retrieval/ExactLexicalRetriever.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

    int fail(const std::string& message) {
        std::cerr << "synthetic_sweep_test: " << message << '\n';
        return 1;
    }

    std::vector<std::string> corpus_ids(
        const agent_memory::RetrievalEvalDataset& dataset
    ) {
        std::vector<std::string> ids;
        ids.reserve(dataset.corpus.size());
        for(const auto& item : dataset.corpus) {
            ids.push_back(item.id);
        }
        return ids;
    }

    std::vector<std::string> corpus_texts(
        const agent_memory::RetrievalEvalDataset& dataset
    ) {
        std::vector<std::string> texts;
        texts.reserve(dataset.corpus.size());
        for(const auto& item : dataset.corpus) {
            texts.push_back(item.text);
        }
        return texts;
    }

    std::vector<agent_memory::Metadata> corpus_metadata(
        const agent_memory::RetrievalEvalDataset& dataset
    ) {
        std::vector<agent_memory::Metadata> metadata;
        metadata.reserve(dataset.corpus.size());
        for(const auto& item : dataset.corpus) {
            metadata.push_back(item.metadata);
        }
        return metadata;
    }

    std::vector<std::string> tokenize_simple(std::string_view text) {
        std::vector<std::string> out;
        std::string current;
        for(const char raw : text) {
            char ch = raw;
            if(ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch + ('a' - 'A'));
            }
            const bool alnum =
                (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
            if(alnum) {
                current.push_back(ch);
            } else if(!current.empty()) {
                if(current.size() >= 2) {
                    out.push_back(std::move(current));
                }
                current.clear();
            }
        }
        if(!current.empty() && current.size() >= 2) {
            out.push_back(std::move(current));
        }
        return out;
    }

    std::size_t vocabulary_size(
        const agent_memory::RetrievalEvalDataset& dataset
    ) {
        std::unordered_set<std::string> vocabulary;
        for(const auto& item : dataset.corpus) {
            for(const auto& token : tokenize_simple(item.text)) {
                vocabulary.insert(token);
            }
        }
        return vocabulary.size();
    }

    agent_memory::BenchmarkReport make_report(
        const agent_memory::IRetriever& retriever,
        const agent_memory::RetrievalEvalDataset& dataset,
        const std::string& baseline_name
    ) {
        auto run = agent_memory::run_retriever(retriever, dataset, baseline_name);
        agent_memory::RetrievalEvalReport eval_report;
        eval_report.baseline_name = baseline_name;
        eval_report.dataset_name = dataset.name;
        eval_report.corpus_size = dataset.corpus.size();
        eval_report.query_count = dataset.queries.size();
        eval_report.run = std::move(run);
        eval_report.metrics = agent_memory::evaluate_retrieval(dataset, eval_report.run);
        eval_report.latency = eval_report.metrics.latency_ms;

        agent_memory::BenchmarkMeasurements measurements;
        measurements.total_benchmark_time_ms = 100.0;
        measurements.index.document_count = dataset.corpus.size();
        measurements.index.vocabulary_size = vocabulary_size(dataset);
        measurements.index.mean_document_length = 7.0;
        return agent_memory::make_benchmark_report(
            eval_report,
            "synthetic_sweep_v1_tiny",
            measurements
        );
    }

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path fixture_path = fs::path("fixtures") / "tiny_synthetic_v1.json";
    std::error_code ec;
    const auto absolute_fixture = fs::absolute(fixture_path, ec);
    if(ec) {
        return fail("cannot resolve fixture path: " + ec.message());
    }
    if(!fs::exists(absolute_fixture)) {
        return fail("missing fixture file: " + absolute_fixture.string());
    }

    agent_memory::RetrievalEvalDataset dataset;
    try {
        dataset = agent_memory::load_dataset_from_json_file(absolute_fixture);
    } catch(const std::exception& err) {
        return fail(std::string("loading fixture threw: ") + err.what());
    }

    if(dataset.corpus.size() != 50) {
        return fail("tiny fixture must contain 50 corpus items");
    }
    if(dataset.queries.size() != 40) {
        return fail("tiny fixture must contain 40 queries");
    }

    std::set<std::string> query_types;
    std::size_t no_answer_count = 0;
    for(const auto& query : dataset.queries) {
        query_types.insert(query.query_type);
        if(query.answer_mode == agent_memory::EvalQueryAnswerMode::NoAnswer) {
            ++no_answer_count;
        }
    }
    const std::set<std::string> expected_query_types = {
        "Synthetic/exact",
        "Synthetic/partial",
        "Synthetic/graded",
        "Synthetic/distractor_heavy",
        "Synthetic/oov",
        "Synthetic/no_answer",
        "Synthetic/length_bias",
        "Synthetic/repeated_term"
    };
    if(query_types != expected_query_types) {
        return fail("tiny fixture must cover all 8 synthetic query classes");
    }
    if(no_answer_count == 0) {
        return fail("tiny fixture must include NoAnswer queries");
    }

    const auto ids = corpus_ids(dataset);
    const auto texts = corpus_texts(dataset);
    const auto metadata = corpus_metadata(dataset);

    agent_memory::BowVectorRetriever bow(ids, texts, 0);
    agent_memory::ExactLexicalRetriever bm25(ids, texts, metadata);

    const auto bow_report = make_report(
        bow,
        dataset,
        std::string{agent_memory::kBaselineNameBowVector}
    );
    const auto bm25_report = make_report(
        bm25,
        dataset,
        "bm25_exact"
    );

    if(bow_report.speed.measured_query_count != 40) {
        return fail("Bow report must measure every non-ignored query");
    }
    if(bm25_report.speed.measured_query_count != 40) {
        return fail("BM25 report must measure every non-ignored query");
    }
    if(bow_report.quality.recall_at_10 < 0.5) {
        return fail("Bow Recall@10 must be at least 0.5 on tiny synthetic fixture");
    }
    if(bm25_report.quality.recall_at_10 < 0.5) {
        return fail("BM25 Recall@10 must be at least 0.5 on tiny synthetic fixture");
    }
    if(bow_report.quality.no_answer_accuracy != 1.0) {
        return fail("Bow no-answer accuracy must be 1.0 for synthetic OOV no-answer queries");
    }
    if(bm25_report.quality.no_answer_accuracy != 1.0) {
        return fail("BM25 no-answer accuracy must be 1.0 for synthetic OOV no-answer queries");
    }
    if(bm25_report.index.vocabulary_size == 0) {
        return fail("Benchmark report must carry synthetic vocabulary size");
    }

    return 0;
}
