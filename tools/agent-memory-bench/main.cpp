#include <agent_memory/eval/BenchmarkReport.hpp>
#include <agent_memory/eval/Evaluation.hpp>
#include <agent_memory/eval/IRetrieverAdapter.hpp>
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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

    struct BenchmarkConfig final {
        std::string benchmark_name;
        std::string baseline_name;
        std::string dataset_name;
        std::size_t document_count = 0;
        std::size_t query_count = 0;
        std::size_t embedding_dimensions = 0;
        std::size_t result_limit = 0;
        std::uint32_t seed = 0;
        fs::path output_path;
    };

    struct ScoredDocument final {
        std::size_t index = 0;
        float score = 0.0F;
    };

    [[nodiscard]] double elapsed_ms(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
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
        config.benchmark_name = require_string(document, "benchmark_name");
        config.baseline_name = require_string(document, "baseline_name");
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
        config.result_limit = checked_size(
            require_positive_integer(document, "result_limit"),
            "result_limit"
        );
        const auto seed = require_positive_integer(document, "seed");
        if(seed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("config field 'seed' exceeds uint32_t");
        }
        config.seed = static_cast<std::uint32_t>(seed);
        config.output_path = require_string(document, "output_path");
        return config;
    }

    [[nodiscard]] agent_memory::RetrievalEvalDataset make_dataset(
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

    void write_json_report(
        const fs::path& path,
        const agent_memory::BenchmarkReport& report
    ) {
        if(!path.parent_path().empty()) {
            std::error_code error;
            fs::create_directories(path.parent_path(), error);
            if(error) {
                throw std::runtime_error(
                    "cannot create output directory '" + path.parent_path().string()
                    + "': " + error.message()
                );
            }
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if(!output) {
            throw std::runtime_error("cannot open benchmark output: " + path.string());
        }
        output << agent_memory::serialize_benchmark_report_json(report) << '\n';
        if(!output) {
            throw std::runtime_error("cannot write benchmark output: " + path.string());
        }
    }

    int run(const BenchmarkConfig& config) {
        const auto ingest_start = Clock::now();
        const auto dataset = make_dataset(config);
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
        write_json_report(config.output_path, report);
        agent_memory::print_benchmark_report(std::cout, report);
        std::cout << "JSON output: " << config.output_path.string() << '\n';
        return 0;
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
