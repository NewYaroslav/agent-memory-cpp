#include <agent_memory/eval/BenchmarkReport.hpp>
#include <agent_memory/eval/DatasetLoader.hpp>
#include <agent_memory/eval/Evaluation.hpp>
#include <agent_memory/eval/IRetrieverAdapter.hpp>
#include <agent_memory/eval/RetrievalEvalRunner.hpp>
#include <agent_memory/index/BinarySignatureInfo.hpp>
#include <agent_memory/index/ExactVectorIndex.hpp>
#include <agent_memory/index/FlatBinarySignatureIndex.hpp>
#include <agent_memory/index/RandomHyperplaneBinaryEncoder.hpp>
#include <agent_memory/retrieval/ExactLexicalRetriever.hpp>
#include <agent_memory/retrieval/BowVectorRetriever.hpp>
#include <agent_memory/retrieval/IRetrievalEngine.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {

    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    constexpr std::string_view kModeRandomExact = "synthetic_random_exact";
    constexpr std::string_view kModeSyntheticSweep = "synthetic_sweep";
    constexpr std::string_view kModeSyntheticBinaryFlatVsFloat =
        "synthetic_binary_flat_vs_float";
    constexpr std::string_view kBaselineNameBm25Exact = "bm25_exact";

    struct BenchmarkConfig final {
        std::string mode{kModeRandomExact};
        std::string benchmark_name;
        std::string baseline_name;
        std::string dataset_name;
        std::size_t document_count = 0;
        std::size_t query_count = 0;
        std::size_t embedding_dimensions = 0;
        std::size_t bit_count = 0;
        std::size_t result_limit = 0;
        std::uint32_t seed = 0;
        fs::path dataset_path;
        fs::path output_path;
        std::vector<std::string> baselines;
    };

    struct ScoredDocument final {
        std::size_t index = 0;
        float score = 0.0F;
    };

    struct CorpusStats final {
        std::size_t vocabulary_size = 0;
        double mean_document_length = 0.0;
        double oov_fraction = 0.0;
    };

    struct SyntheticDenseData final {
        std::vector<agent_memory::Embedding> documents;
        std::vector<agent_memory::Embedding> queries;
    };

    struct SearchTiming final {
        double total_ms = 0.0;
        double encode_ms = 0.0;
        double search_ms = 0.0;
    };

    struct BinaryFlatVsFloatResult final {
        double data_generation_ms = 0.0;
        double exact_build_ms = 0.0;
        double binary_build_ms = 0.0;
        SearchTiming exact_query;
        SearchTiming binary_query;
        double mean_recall_at_k_vs_exact = 0.0;
        double top1_agreement = 0.0;
        std::uint64_t exact_payload_bytes = 0;
        std::uint64_t binary_payload_bytes = 0;
        std::uint64_t process_peak_resident_set_bytes = 0;
    };

    [[nodiscard]] double elapsed_ms(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    const nlohmann::json* find_optional(
        const nlohmann::json& document,
        std::string_view field
    ) {
        const auto it = document.find(std::string{field});
        return it == document.end() ? nullptr : &*it;
    }

    [[nodiscard]] std::string require_string(
        const nlohmann::json& document,
        std::string_view field
    ) {
        const std::string name{field};
        const auto it = document.find(name);
        if(it == document.end() || !it->is_string()) {
            throw std::runtime_error("config field '" + name + "' must be a string");
        }
        const std::string value = it->get<std::string>();
        if(value.empty()) {
            throw std::runtime_error("config field '" + name + "' must not be empty");
        }
        return value;
    }

    [[nodiscard]] std::string optional_string(
        const nlohmann::json& document,
        std::string_view field,
        std::string fallback
    ) {
        const auto* node = find_optional(document, field);
        if(node == nullptr) {
            return fallback;
        }
        if(!node->is_string()) {
            throw std::runtime_error(
                "config field '" + std::string{field} + "' must be a string"
            );
        }
        auto value = node->get<std::string>();
        if(value.empty()) {
            throw std::runtime_error(
                "config field '" + std::string{field} + "' must not be empty"
            );
        }
        return value;
    }

    [[nodiscard]] std::uint64_t require_positive_integer(
        const nlohmann::json& document,
        std::string_view field
    ) {
        const std::string name{field};
        const auto it = document.find(name);
        if(it == document.end() || !it->is_number_integer()) {
            throw std::runtime_error("config field '" + name + "' must be an integer");
        }
        if(it->is_number_unsigned()) {
            const auto value = it->get<std::uint64_t>();
            if(value == 0) {
                throw std::runtime_error(
                    "config field '" + name + "' must be greater than zero"
                );
            }
            return value;
        }
        const auto value = it->get<std::int64_t>();
        if(value <= 0) {
            throw std::runtime_error(
                "config field '" + name + "' must be greater than zero"
            );
        }
        return static_cast<std::uint64_t>(value);
    }

    [[nodiscard]] std::size_t checked_size(
        std::uint64_t value,
        std::string_view field
    ) {
        if(value > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
            throw std::runtime_error(
                "config field '" + std::string{field} + "' exceeds size_t"
            );
        }
        return static_cast<std::size_t>(value);
    }

    [[nodiscard]] std::vector<std::string> read_baselines(
        const nlohmann::json& document
    ) {
        const auto* node = find_optional(document, "baselines");
        if(node == nullptr) {
            return {
                std::string{agent_memory::kBaselineNameBowVector},
                std::string{kBaselineNameBm25Exact}
            };
        }
        if(!node->is_array() || node->empty()) {
            throw std::runtime_error("config field 'baselines' must be a non-empty array");
        }
        std::vector<std::string> baselines;
        baselines.reserve(node->size());
        std::set<std::string> seen;
        for(const auto& entry : *node) {
            if(!entry.is_string()) {
                throw std::runtime_error("config field 'baselines' entries must be strings");
            }
            auto value = entry.get<std::string>();
            if(value.empty()) {
                throw std::runtime_error("config field 'baselines' entries must not be empty");
            }
            if(!seen.insert(value).second) {
                throw std::runtime_error(
                    "config field 'baselines' must not contain duplicate baseline: "
                    + value
                );
            }
            baselines.push_back(std::move(value));
        }
        return baselines;
    }

    [[nodiscard]] BenchmarkConfig load_config(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if(!input) {
            throw std::runtime_error("cannot open benchmark config: " + path.string());
        }

        nlohmann::json document;
        try {
            input >> document;
        } catch(const nlohmann::json::exception& error) {
            throw std::runtime_error(
                "cannot parse benchmark config '" + path.string() + "': " + error.what()
            );
        }
        if(!document.is_object()) {
            throw std::runtime_error("benchmark config root must be an object");
        }

        BenchmarkConfig config;
        config.mode = optional_string(document, "mode", std::string{kModeRandomExact});
        config.benchmark_name = require_string(document, "benchmark_name");
        config.output_path = require_string(document, "output_path");

        if(config.mode == kModeSyntheticSweep) {
            config.dataset_path = require_string(document, "dataset_path");
            config.result_limit = checked_size(
                require_positive_integer(document, "result_limit"),
                "result_limit"
            );
            config.baselines = read_baselines(document);
            return config;
        }
        const bool binary_flat_vs_float =
            config.mode == kModeSyntheticBinaryFlatVsFloat;
        if(config.mode != kModeRandomExact && !binary_flat_vs_float) {
            throw std::runtime_error(
                "config field 'mode' must be synthetic_random_exact, synthetic_sweep, "
                "or synthetic_binary_flat_vs_float"
            );
        }

        if(!binary_flat_vs_float) {
            config.baseline_name = require_string(document, "baseline_name");
        }
        config.dataset_name = require_string(document, "dataset_name");
        config.document_count = checked_size(
            require_positive_integer(document, "document_count"),
            "document_count"
        );
        config.query_count = checked_size(
            require_positive_integer(document, "query_count"),
            "query_count"
        );
        config.embedding_dimensions = checked_size(
            require_positive_integer(document, "embedding_dimensions"),
            "embedding_dimensions"
        );
        if(binary_flat_vs_float) {
            config.bit_count = checked_size(
                require_positive_integer(document, "bit_count"),
                "bit_count"
            );
        }
        config.result_limit = checked_size(
            require_positive_integer(document, "result_limit"),
            "result_limit"
        );
        const auto seed = require_positive_integer(document, "seed");
        if(seed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("config field 'seed' exceeds uint32_t");
        }
        config.seed = static_cast<std::uint32_t>(seed);
        return config;
    }

    [[nodiscard]] agent_memory::RetrievalEvalDataset make_random_exact_dataset(
        const BenchmarkConfig& config
    ) {
        agent_memory::RetrievalEvalDataset dataset;
        dataset.name = config.dataset_name;
        dataset.corpus.reserve(config.document_count);
        dataset.queries.reserve(config.query_count);
        dataset.judgments.reserve(config.query_count);

        for(std::size_t i = 0; i < config.document_count; ++i) {
            agent_memory::EvalCorpusItem item;
            item.id = "doc:" + std::to_string(i);
            item.title = "Synthetic document " + std::to_string(i);
            item.text = "random vector document";
            dataset.corpus.push_back(std::move(item));
        }
        for(std::size_t i = 0; i < config.query_count; ++i) {
            const std::size_t target = i % config.document_count;
            agent_memory::EvalQuery query;
            query.id = "query:" + std::to_string(i);
            query.text = "query:" + std::to_string(target);
            query.query_type = "SyntheticExactVector";
            query.limit = config.result_limit;
            dataset.queries.push_back(std::move(query));

            agent_memory::RelevanceJudgment judgment;
            judgment.query_id = "query:" + std::to_string(i);
            judgment.item_id = "doc:" + std::to_string(target);
            judgment.relevance_grade = 1;
            dataset.judgments.push_back(std::move(judgment));
        }

        agent_memory::validate_retrieval_eval_dataset(dataset);
        return dataset;
    }

    [[nodiscard]] std::vector<std::vector<float>> make_embeddings(
        const BenchmarkConfig& config
    ) {
        std::mt19937 generator(config.seed);
        std::normal_distribution<float> distribution(0.0F, 1.0F);
        std::vector<std::vector<float>> embeddings(
            config.document_count,
            std::vector<float>(config.embedding_dimensions)
        );

        for(auto& embedding : embeddings) {
            double squared_norm = 0.0;
            for(float& value : embedding) {
                value = distribution(generator);
                squared_norm += static_cast<double>(value) * value;
            }
            const float inverse_norm = 1.0F / static_cast<float>(std::sqrt(squared_norm));
            for(float& value : embedding) {
                value *= inverse_norm;
            }
        }
        return embeddings;
    }

    void normalize(agent_memory::Embedding& embedding) {
        double squared_norm = 0.0;
        for(const float value : embedding.values) {
            squared_norm += static_cast<double>(value) * value;
        }
        if(squared_norm == 0.0) {
            throw std::runtime_error("synthetic embedding generator produced zero vector");
        }
        const float inverse_norm = 1.0F / static_cast<float>(std::sqrt(squared_norm));
        for(float& value : embedding.values) {
            value *= inverse_norm;
        }
    }

    [[nodiscard]] agent_memory::Embedding noisy_embedding(
        const agent_memory::Embedding& center,
        std::mt19937& generator,
        float noise_stddev
    ) {
        std::normal_distribution<float> noise(0.0F, noise_stddev);
        agent_memory::Embedding embedding;
        embedding.values.reserve(center.values.size());
        for(const float value : center.values) {
            embedding.values.push_back(value + noise(generator));
        }
        normalize(embedding);
        return embedding;
    }

    [[nodiscard]] SyntheticDenseData make_clustered_dense_data(
        const BenchmarkConfig& config
    ) {
        constexpr std::size_t kMaxClusterCount = 64;
        const std::size_t cluster_count = std::max<std::size_t>(
            1,
            std::min(kMaxClusterCount, config.document_count / 32)
        );

        std::mt19937 generator(config.seed);
        std::normal_distribution<float> distribution(0.0F, 1.0F);
        std::vector<agent_memory::Embedding> centers;
        centers.reserve(cluster_count);
        for(std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
            agent_memory::Embedding center;
            center.values.reserve(config.embedding_dimensions);
            for(std::size_t dimension = 0; dimension < config.embedding_dimensions; ++dimension) {
                center.values.push_back(distribution(generator));
            }
            normalize(center);
            centers.push_back(std::move(center));
        }

        SyntheticDenseData data;
        data.documents.reserve(config.document_count);
        for(std::size_t index = 0; index < config.document_count; ++index) {
            const auto& center = centers[index % centers.size()];
            data.documents.push_back(noisy_embedding(center, generator, 0.28F));
        }

        data.queries.reserve(config.query_count);
        for(std::size_t index = 0; index < config.query_count; ++index) {
            const auto& center = centers[(index * 7) % centers.size()];
            data.queries.push_back(noisy_embedding(center, generator, 0.18F));
        }
        return data;
    }

    [[nodiscard]] agent_memory::EmbeddingModelInfo make_synthetic_model_info(
        const BenchmarkConfig& config
    ) {
        agent_memory::EmbeddingModelInfo model;
        model.model_id = "synthetic-clustered-dense-v1";
        model.dimension = config.embedding_dimensions;
        model.similarity_metric = agent_memory::SimilarityMetric::Cosine;
        model.normalized = true;
        return model;
    }

    [[nodiscard]] agent_memory::ChunkId make_chunk_id(std::size_t index) {
        return agent_memory::ChunkId{"doc:" + std::to_string(index)};
    }

    [[nodiscard]] std::uint64_t checked_payload_bytes(
        std::size_t count,
        std::size_t width,
        std::size_t unit_bytes
    ) {
        const auto max_value = std::numeric_limits<std::uint64_t>::max();
        if(count != 0 && width > max_value / count) {
            return max_value;
        }
        const std::uint64_t elements =
            static_cast<std::uint64_t>(count) * static_cast<std::uint64_t>(width);
        if(unit_bytes != 0 && elements > max_value / unit_bytes) {
            return max_value;
        }
        return elements * static_cast<std::uint64_t>(unit_bytes);
    }

    class SyntheticExactEngine final : public agent_memory::IRetrievalEngine {
    public:
        explicit SyntheticExactEngine(std::vector<std::vector<float>> embeddings)
            : m_embeddings(std::move(embeddings)) {}

        [[nodiscard]] agent_memory::RetrievalResponse retrieve(
            const agent_memory::RetrievalRequest& request
        ) const override {
            agent_memory::RetrievalResponse response;
            if(request.limit == 0 || m_embeddings.empty()) {
                return response;
            }

            const auto query_index = parse_query_index(request.query);
            if(!query_index || *query_index >= m_embeddings.size()) {
                return response;
            }
            const auto& query = m_embeddings[*query_index];
            std::vector<ScoredDocument> scored;
            scored.reserve(m_embeddings.size());
            for(std::size_t i = 0; i < m_embeddings.size(); ++i) {
                const auto& document = m_embeddings[i];
                float score = 0.0F;
                for(std::size_t dimension = 0; dimension < document.size(); ++dimension) {
                    score += query[dimension] * document[dimension];
                }
                scored.push_back(ScoredDocument{i, score});
            }

            const std::size_t result_count = std::min(request.limit, scored.size());
            const auto compare = [](const ScoredDocument& lhs, const ScoredDocument& rhs) {
                if(lhs.score == rhs.score) {
                    return lhs.index < rhs.index;
                }
                return lhs.score > rhs.score;
            };
            std::partial_sort(
                scored.begin(),
                scored.begin() + static_cast<std::ptrdiff_t>(result_count),
                scored.end(),
                compare
            );

            response.items.reserve(result_count);
            for(std::size_t rank = 0; rank < result_count; ++rank) {
                const auto& candidate = scored[rank];
                const std::string id = "doc:" + std::to_string(candidate.index);
                agent_memory::RetrievalResponseItem item;
                item.lexical.chunk_id = agent_memory::ChunkId{id};
                item.lexical.score = candidate.score;
                item.object.id = agent_memory::MemoryObjectId{id};
                response.items.push_back(std::move(item));
            }
            return response;
        }

    private:
        [[nodiscard]] static std::optional<std::size_t> parse_query_index(
            std::string_view text
        ) noexcept {
            constexpr std::string_view prefix = "query:";
            if(text.substr(0, prefix.size()) != prefix) {
                return std::nullopt;
            }
            std::size_t value = 0;
            const char* first = text.data() + static_cast<std::ptrdiff_t>(prefix.size());
            const char* last = text.data() + static_cast<std::ptrdiff_t>(text.size());
            const auto result = std::from_chars(first, last, value);
            if(result.ec != std::errc{} || result.ptr != last) {
                return std::nullopt;
            }
            return value;
        }

        std::vector<std::vector<float>> m_embeddings;
    };

    [[nodiscard]] std::uint64_t peak_resident_set_bytes() noexcept {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        if(GetProcessMemoryInfo(
            GetCurrentProcess(),
            &counters,
            static_cast<DWORD>(sizeof(counters))
        ) == 0) {
            return 0;
        }
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#else
        rusage usage{};
        if(getrusage(RUSAGE_SELF, &usage) != 0) {
            return 0;
        }
#if defined(__APPLE__)
        return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
#endif
    }

    [[nodiscard]] std::vector<std::string> tokenize_simple(std::string_view text) {
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

    [[nodiscard]] CorpusStats compute_corpus_stats(
        const agent_memory::RetrievalEvalDataset& dataset
    ) {
        std::unordered_set<std::string> vocabulary;
        std::size_t total_document_tokens = 0;
        for(const auto& item : dataset.corpus) {
            const auto tokens = tokenize_simple(item.text);
            total_document_tokens += tokens.size();
            for(const auto& token : tokens) {
                vocabulary.insert(token);
            }
        }

        std::size_t query_tokens = 0;
        std::size_t oov_tokens = 0;
        for(const auto& query : dataset.queries) {
            if(query.answer_mode == agent_memory::EvalQueryAnswerMode::Ignore) {
                continue;
            }
            const auto tokens = tokenize_simple(query.text);
            for(const auto& token : tokens) {
                ++query_tokens;
                if(vocabulary.find(token) == vocabulary.end()) {
                    ++oov_tokens;
                }
            }
        }

        CorpusStats stats;
        stats.vocabulary_size = vocabulary.size();
        if(!dataset.corpus.empty()) {
            stats.mean_document_length =
                static_cast<double>(total_document_tokens)
                / static_cast<double>(dataset.corpus.size());
        }
        if(query_tokens != 0) {
            stats.oov_fraction =
                static_cast<double>(oov_tokens) / static_cast<double>(query_tokens);
        }
        return stats;
    }

    [[nodiscard]] std::vector<std::string> corpus_ids(
        const agent_memory::RetrievalEvalDataset& dataset
    ) {
        std::vector<std::string> ids;
        ids.reserve(dataset.corpus.size());
        for(const auto& item : dataset.corpus) {
            ids.push_back(item.id);
        }
        return ids;
    }

    [[nodiscard]] std::vector<std::string> corpus_texts(
        const agent_memory::RetrievalEvalDataset& dataset
    ) {
        std::vector<std::string> texts;
        texts.reserve(dataset.corpus.size());
        for(const auto& item : dataset.corpus) {
            texts.push_back(item.text);
        }
        return texts;
    }

    [[nodiscard]] std::vector<agent_memory::Metadata> corpus_metadata(
        const agent_memory::RetrievalEvalDataset& dataset
    ) {
        std::vector<agent_memory::Metadata> metadata;
        metadata.reserve(dataset.corpus.size());
        for(const auto& item : dataset.corpus) {
            metadata.push_back(item.metadata);
        }
        return metadata;
    }

    [[nodiscard]] agent_memory::BenchmarkReport run_retriever_benchmark(
        const agent_memory::IRetriever& retriever,
        const agent_memory::RetrievalEvalDataset& dataset,
        std::string_view benchmark_name,
        std::string_view baseline_name,
        const CorpusStats& corpus_stats,
        agent_memory::IndexMetrics index_metrics
    ) {
        const auto retrieval_start = Clock::now();
        auto run = agent_memory::run_retriever(retriever, dataset, baseline_name);
        const auto retrieval_end = Clock::now();

        agent_memory::RetrievalEvalReport eval_report;
        eval_report.baseline_name.assign(baseline_name.begin(), baseline_name.end());
        eval_report.dataset_name = dataset.name;
        eval_report.corpus_size = dataset.corpus.size();
        eval_report.query_count = dataset.queries.size();
        eval_report.run = std::move(run);
        eval_report.metrics = agent_memory::evaluate_retrieval(
            dataset,
            eval_report.run
        );
        eval_report.latency = eval_report.metrics.latency_ms;

        agent_memory::BenchmarkMeasurements measurements;
        measurements.total_benchmark_time_ms = elapsed_ms(retrieval_start, retrieval_end);
        measurements.oov_fraction = corpus_stats.oov_fraction;
        measurements.index = index_metrics;
        return agent_memory::make_benchmark_report(
            eval_report,
            benchmark_name,
            measurements
        );
    }

    void ensure_parent_directory(const fs::path& path) {
        if(path.parent_path().empty()) {
            return;
        }
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
        if(error) {
            throw std::runtime_error(
                "cannot create output directory '" + path.parent_path().string()
                + "': " + error.message()
            );
        }
    }

    void write_text_file(const fs::path& path, std::string_view text) {
        ensure_parent_directory(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if(!output) {
            throw std::runtime_error("cannot open benchmark output: " + path.string());
        }
        output << text;
        if(!output) {
            throw std::runtime_error("cannot write benchmark output: " + path.string());
        }
    }

    [[nodiscard]] nlohmann::json report_to_json(
        const agent_memory::BenchmarkReport& report
    ) {
        return nlohmann::json::parse(
            agent_memory::serialize_benchmark_report_json(report, -1)
        );
    }

    void write_single_json_report(
        const fs::path& path,
        const agent_memory::BenchmarkReport& report
    ) {
        write_text_file(
            path,
            agent_memory::serialize_benchmark_report_json(report) + "\n"
        );
    }

    void write_sweep_json_report(
        const fs::path& path,
        std::string_view benchmark_name,
        const agent_memory::RetrievalEvalDataset& dataset,
        const std::vector<agent_memory::BenchmarkReport>& reports
    ) {
        nlohmann::json document;
        document["schema_version"] = 1;
        document["benchmark_name"] = benchmark_name;
        document["dataset_name"] = dataset.name;
        document["corpus_size"] = dataset.corpus.size();
        document["query_count"] = dataset.queries.size();
        document["reports"] = nlohmann::json::array();
        for(const auto& report : reports) {
            document["reports"].push_back(report_to_json(report));
        }
        if(reports.size() >= 2) {
            document["comparison"] = {
                {"first_baseline", reports[0].baseline_name},
                {"second_baseline", reports[1].baseline_name},
                {
                    "recall_at_10_delta_second_minus_first",
                    reports[1].quality.recall_at_10 - reports[0].quality.recall_at_10
                },
                {
                    "p95_latency_ms_delta_second_minus_first",
                    reports[1].speed.p95_latency_ms - reports[0].speed.p95_latency_ms
                }
            };
        }
        write_text_file(path, document.dump(2) + "\n");
    }

    [[nodiscard]] double per_query_ms(double total_ms, std::size_t query_count) noexcept {
        if(query_count == 0) {
            return 0.0;
        }
        return total_ms / static_cast<double>(query_count);
    }

    [[nodiscard]] double queries_per_second(
        double total_ms,
        std::size_t query_count
    ) noexcept {
        if(total_ms <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(query_count) * 1000.0 / total_ms;
    }

    void write_binary_flat_vs_float_json_report(
        const fs::path& path,
        const BenchmarkConfig& config,
        const agent_memory::BinarySignatureEncoderInfo& encoder_info,
        const BinaryFlatVsFloatResult& result
    ) {
        nlohmann::json document;
        document["schema_version"] = 1;
        document["mode"] = std::string{kModeSyntheticBinaryFlatVsFloat};
        document["benchmark_name"] = config.benchmark_name;
        document["dataset_name"] = config.dataset_name;
        document["corpus_size"] = config.document_count;
        document["query_count"] = config.query_count;
        document["embedding_dimensions"] = config.embedding_dimensions;
        document["bit_count"] = config.bit_count;
        document["result_limit"] = config.result_limit;
        document["seed"] = config.seed;
        document["encoder"] = {
            {"encoder_id", encoder_info.encoder_id},
            {"encoder_version", encoder_info.encoder_version},
            {"config_fingerprint", encoder_info.config_fingerprint}
        };
        document["quality"] = {
            {"reference_baseline", "exact_float_flat"},
            {"candidate_baseline", "flat_binary_hamming"},
            {"mean_recall_at_k_vs_exact", result.mean_recall_at_k_vs_exact},
            {"top1_agreement", result.top1_agreement}
        };
        document["speed"] = {
            {"data_generation_ms", result.data_generation_ms},
            {"exact_float_build_ms", result.exact_build_ms},
            {"binary_encode_and_build_ms", result.binary_build_ms},
            {"exact_float_query_search_ms", result.exact_query.search_ms},
            {"exact_float_query_total_ms", result.exact_query.total_ms},
            {"exact_float_mean_query_ms", per_query_ms(
                result.exact_query.total_ms,
                config.query_count
            )},
            {"exact_float_queries_per_second", queries_per_second(
                result.exact_query.total_ms,
                config.query_count
            )},
            {"binary_query_encode_ms", result.binary_query.encode_ms},
            {"binary_query_search_ms", result.binary_query.search_ms},
            {"binary_query_total_ms", result.binary_query.total_ms},
            {"binary_mean_query_ms", per_query_ms(
                result.binary_query.total_ms,
                config.query_count
            )},
            {"binary_queries_per_second", queries_per_second(
                result.binary_query.total_ms,
                config.query_count
            )},
            {"binary_search_speedup_vs_exact_search",
                result.binary_query.search_ms <= 0.0
                ? 0.0
                : result.exact_query.search_ms / result.binary_query.search_ms},
            {"binary_total_speedup_vs_exact_search_including_encode",
                result.binary_query.total_ms <= 0.0
                ? 0.0
                : result.exact_query.total_ms / result.binary_query.total_ms}
        };
        document["index"] = {
            {"exact_vector_payload_bytes", result.exact_payload_bytes},
            {"binary_signature_payload_bytes", result.binary_payload_bytes},
            {"payload_compression_ratio_exact_over_binary",
                result.binary_payload_bytes == 0
                    ? 0.0
                    : static_cast<double>(result.exact_payload_bytes)
                        / static_cast<double>(result.binary_payload_bytes)}
        };
        document["process"] = {
            {"peak_resident_set_bytes", result.process_peak_resident_set_bytes}
        };
        document["notes"] = nlohmann::json::array({
            "Synthetic clustered dense vectors; exact_float_flat top-k is the oracle.",
            "Timing is one local in-process run and is directional, not statistically stable.",
            "Exact search timing excludes quality-bookkeeping ID extraction.",
            "Binary total timing is query signature encoding plus exact flat Hamming scan.",
            "Process peak RSS is a whole benchmark-process high-water mark, not per-index memory."
        });
        write_text_file(path, document.dump(2) + "\n");
    }

    [[nodiscard]] BinaryFlatVsFloatResult run_binary_flat_vs_float_benchmark(
        const BenchmarkConfig& config,
        const agent_memory::RandomHyperplaneBinaryEncoder& encoder
    ) {
        const auto data_start = Clock::now();
        auto data = make_clustered_dense_data(config);
        const auto data_end = Clock::now();

        agent_memory::ExactVectorIndex exact_index(agent_memory::ExactVectorIndexOptions{
            config.embedding_dimensions,
            agent_memory::SimilarityMetric::Cosine
        });

        const auto signature_info = agent_memory::make_binary_signature_info(
            encoder.info(),
            make_synthetic_model_info(config),
            "symmetric_dense_projection"
        );
        agent_memory::FlatBinarySignatureIndex binary_index(
            agent_memory::FlatBinarySignatureIndexOptions{signature_info}
        );

        const auto exact_build_start = Clock::now();
        for(std::size_t index = 0; index < data.documents.size(); ++index) {
            exact_index.upsert(agent_memory::VectorRecord{
                make_chunk_id(index),
                data.documents[index],
                {}
            });
        }
        const auto exact_build_end = Clock::now();

        const auto binary_build_start = Clock::now();
        for(std::size_t index = 0; index < data.documents.size(); ++index) {
            binary_index.upsert(agent_memory::BinarySignatureRecord{
                make_chunk_id(index),
                encoder.encode(data.documents[index]),
                signature_info,
                {}
            });
        }
        const auto binary_build_end = Clock::now();

        BinaryFlatVsFloatResult result;
        result.data_generation_ms = elapsed_ms(data_start, data_end);
        result.exact_build_ms = elapsed_ms(exact_build_start, exact_build_end);
        result.binary_build_ms = elapsed_ms(binary_build_start, binary_build_end);
        result.exact_payload_bytes = checked_payload_bytes(
            config.document_count,
            config.embedding_dimensions,
            sizeof(float)
        );
        result.binary_payload_bytes = checked_payload_bytes(
            config.document_count,
            agent_memory::binary_signature_word_count(config.bit_count),
            sizeof(std::uint64_t)
        );

        std::vector<std::vector<std::string>> exact_top_k;
        exact_top_k.reserve(data.queries.size());
        for(const auto& query_embedding : data.queries) {
            const auto search_start = Clock::now();
            const auto hits = exact_index.search(agent_memory::VectorSearchQuery{
                query_embedding,
                config.result_limit,
                {}
            });
            const auto search_end = Clock::now();
            result.exact_query.search_ms += elapsed_ms(search_start, search_end);

            std::vector<std::string> ids;
            ids.reserve(hits.size());
            for(const auto& hit : hits) {
                ids.push_back(hit.chunk_id.value());
            }
            exact_top_k.push_back(std::move(ids));
        }
        result.exact_query.total_ms = result.exact_query.search_ms;

        double recall_sum = 0.0;
        std::size_t top1_match_count = 0;
        for(std::size_t query_index = 0; query_index < data.queries.size(); ++query_index) {
            const auto encode_start = Clock::now();
            const auto query_signature = encoder.encode(data.queries[query_index]);
            const auto encode_end = Clock::now();
            result.binary_query.encode_ms += elapsed_ms(encode_start, encode_end);

            const auto search_start = Clock::now();
            const auto hits = binary_index.search(agent_memory::BinarySignatureSearchQuery{
                query_signature,
                signature_info,
                config.result_limit,
                {}
            });
            const auto search_end = Clock::now();
            result.binary_query.search_ms += elapsed_ms(search_start, search_end);
            result.binary_query.total_ms =
                result.binary_query.encode_ms + result.binary_query.search_ms;

            std::unordered_set<std::string> exact_ids(
                exact_top_k[query_index].begin(),
                exact_top_k[query_index].end()
            );
            std::size_t intersection_count = 0;
            for(const auto& hit : hits) {
                if(exact_ids.find(hit.chunk_id.value()) != exact_ids.end()) {
                    ++intersection_count;
                }
            }
            if(!exact_ids.empty()) {
                recall_sum += static_cast<double>(intersection_count)
                    / static_cast<double>(exact_ids.size());
            }
            if(!exact_top_k[query_index].empty() && !hits.empty()
               && exact_top_k[query_index].front() == hits.front().chunk_id.value()) {
                ++top1_match_count;
            }
        }

        if(!data.queries.empty()) {
            result.mean_recall_at_k_vs_exact =
                recall_sum / static_cast<double>(data.queries.size());
            result.top1_agreement =
                static_cast<double>(top1_match_count)
                / static_cast<double>(data.queries.size());
        }
        result.process_peak_resident_set_bytes = peak_resident_set_bytes();
        return result;
    }

    int run_random_exact(const BenchmarkConfig& config) {
        const auto ingest_start = Clock::now();
        const auto dataset = make_random_exact_dataset(config);
        const auto ingest_end = Clock::now();

        const auto embedding_start = Clock::now();
        auto embeddings = make_embeddings(config);
        const auto embedding_end = Clock::now();

        const auto index_start = Clock::now();
        SyntheticExactEngine engine(std::move(embeddings));
        const auto index_end = Clock::now();

        const auto retrieval_start = Clock::now();
        auto retrieval_run = agent_memory::run_retrieval_engine(
            engine,
            dataset,
            config.baseline_name
        );
        const auto retrieval_end = Clock::now();

        agent_memory::RetrievalEvalReport eval_report;
        eval_report.baseline_name = config.baseline_name;
        eval_report.dataset_name = dataset.name;
        eval_report.corpus_size = dataset.corpus.size();
        eval_report.query_count = dataset.queries.size();
        eval_report.run = std::move(retrieval_run);
        eval_report.metrics = agent_memory::evaluate_retrieval(dataset, eval_report.run);
        eval_report.latency = eval_report.metrics.latency_ms;

        agent_memory::BenchmarkMeasurements measurements;
        measurements.total_benchmark_time_ms = elapsed_ms(retrieval_start, retrieval_end);
        measurements.index.document_count = config.document_count;
        measurements.index.mean_document_length = 3.0;
        measurements.index.corpus_ingest_time_ms = elapsed_ms(ingest_start, ingest_end);
        measurements.index.embedding_time_ms = elapsed_ms(embedding_start, embedding_end);
        measurements.index.index_build_time_ms = elapsed_ms(index_start, index_end);
        measurements.index.peak_resident_set_bytes = peak_resident_set_bytes();

        const auto report = agent_memory::make_benchmark_report(
            eval_report,
            config.benchmark_name,
            measurements
        );
        write_single_json_report(config.output_path, report);
        agent_memory::print_benchmark_report(std::cout, report);
        std::cout << "JSON output: " << config.output_path.string() << '\n';
        return 0;
    }

    int run_synthetic_sweep(const BenchmarkConfig& config) {
        const auto ingest_start = Clock::now();
        auto dataset = agent_memory::load_dataset_from_json_file(config.dataset_path);
        for(auto& query : dataset.queries) {
            query.limit = config.result_limit;
        }
        agent_memory::validate_retrieval_eval_dataset(dataset);
        const auto ingest_end = Clock::now();

        const auto ids = corpus_ids(dataset);
        const auto texts = corpus_texts(dataset);
        const auto metadata = corpus_metadata(dataset);
        const auto stats = compute_corpus_stats(dataset);

        std::vector<agent_memory::BenchmarkReport> reports;
        reports.reserve(config.baselines.size());
        for(const auto& baseline : config.baselines) {
            agent_memory::IndexMetrics index_metrics;
            index_metrics.document_count = dataset.corpus.size();
            index_metrics.vocabulary_size = stats.vocabulary_size;
            index_metrics.mean_document_length = stats.mean_document_length;
            index_metrics.corpus_ingest_time_ms = elapsed_ms(ingest_start, ingest_end);
            // Baselines run in-process, so process peak RSS is a lifetime
            // high-water mark and cannot be honestly attributed per baseline.
            // Leave this per-report value unsupported for synthetic_sweep
            // until the runner isolates baselines in subprocesses.
            index_metrics.peak_resident_set_bytes = 0;

            if(baseline == agent_memory::kBaselineNameBowVector) {
                const auto build_start = Clock::now();
                agent_memory::BowVectorRetriever retriever(ids, texts, 0);
                const auto build_end = Clock::now();
                // BowVectorRetriever eagerly fits the dictionary, creates
                // vectors, and builds the exact top-K index in one constructor.
                // Until that API exposes a finer timing split, report the
                // combined constructor time as index build time rather than
                // mislabeling it as pure embedding time.
                index_metrics.index_build_time_ms = elapsed_ms(build_start, build_end);
                reports.push_back(run_retriever_benchmark(
                    retriever,
                    dataset,
                    config.benchmark_name,
                    baseline,
                    stats,
                    index_metrics
                ));
            } else if(baseline == kBaselineNameBm25Exact) {
                const auto build_start = Clock::now();
                agent_memory::ExactLexicalRetriever retriever(ids, texts, metadata);
                const auto build_end = Clock::now();
                index_metrics.index_build_time_ms = elapsed_ms(build_start, build_end);
                reports.push_back(run_retriever_benchmark(
                    retriever,
                    dataset,
                    config.benchmark_name,
                    baseline,
                    stats,
                    index_metrics
                ));
            } else {
                throw std::runtime_error(
                    "unsupported synthetic_sweep baseline: " + baseline
                );
            }
        }

        write_sweep_json_report(
            config.output_path,
            config.benchmark_name,
            dataset,
            reports
        );
        for(const auto& report : reports) {
            agent_memory::print_benchmark_report(std::cout, report);
        }
        std::cout << "JSON output: " << config.output_path.string() << '\n';
        return 0;
    }

    int run_binary_flat_vs_float(const BenchmarkConfig& config) {
        agent_memory::RandomHyperplaneBinaryEncoderOptions encoder_options;
        encoder_options.input_dimension = config.embedding_dimensions;
        encoder_options.bit_count = config.bit_count;
        encoder_options.seed = config.seed;
        const agent_memory::RandomHyperplaneBinaryEncoder encoder(encoder_options);

        const auto result = run_binary_flat_vs_float_benchmark(config, encoder);
        write_binary_flat_vs_float_json_report(
            config.output_path,
            config,
            encoder.info(),
            result
        );

        std::cout << "Benchmark: " << config.benchmark_name << '\n';
        std::cout << "Dataset: " << config.dataset_name
                  << " corpus=" << config.document_count
                  << " queries=" << config.query_count
                  << " dim=" << config.embedding_dimensions
                  << " bits=" << config.bit_count
                  << " top_k=" << config.result_limit << '\n';
        std::cout << "Quality vs exact float: recall@k="
                  << result.mean_recall_at_k_vs_exact
                  << " top1_agreement=" << result.top1_agreement << '\n';
        std::cout << "Exact float query total ms: " << result.exact_query.total_ms
                  << " qps=" << queries_per_second(
                      result.exact_query.total_ms,
                      config.query_count
                  ) << '\n';
        std::cout << "Binary query total ms: " << result.binary_query.total_ms
                  << " qps=" << queries_per_second(
                      result.binary_query.total_ms,
                      config.query_count
                  ) << '\n';
        std::cout << "Binary search speedup vs exact search: "
                  << (result.binary_query.search_ms <= 0.0
                      ? 0.0
                      : result.exact_query.search_ms / result.binary_query.search_ms)
                  << '\n';
        std::cout << "Binary total speedup vs exact search including encode: "
                  << (result.binary_query.total_ms <= 0.0
                      ? 0.0
                      : result.exact_query.total_ms / result.binary_query.total_ms)
                  << '\n';
        std::cout << "Payload bytes exact/binary: " << result.exact_payload_bytes
                  << '/' << result.binary_payload_bytes << '\n';
        std::cout << "JSON output: " << config.output_path.string() << '\n';
        return 0;
    }

    int run(const BenchmarkConfig& config) {
        if(config.mode == kModeSyntheticSweep) {
            return run_synthetic_sweep(config);
        }
        if(config.mode == kModeSyntheticBinaryFlatVsFloat) {
            return run_binary_flat_vs_float(config);
        }
        return run_random_exact(config);
    }

} // namespace

int main(int argc, char** argv) {
    if(argc < 2 || argc > 3) {
        std::cerr << "usage: agent-memory-bench <config.json> [output.json]\n";
        return 2;
    }

    try {
        auto config = load_config(fs::path{argv[1]});
        if(argc == 3) {
            config.output_path = fs::path{argv[2]};
        }
        return run(config);
    } catch(const std::exception& error) {
        std::cerr << "agent-memory-bench: " << error.what() << '\n';
        return 1;
    }
}
