#include <agent_memory/embedding/embedding_types.hpp>
#include <agent_memory/index/BinarySignatureInfo.hpp>
#include <agent_memory/index/ExactVectorIndex.hpp>
#include <agent_memory/index/FlatBinarySignatureIndex.hpp>
#include <agent_memory/index/MultiProbeHammingIndex.hpp>
#include <agent_memory/index/RandomHyperplaneBinaryEncoder.hpp>
#include <agent_memory/index/VectorIndex.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
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

    struct Config final {
        fs::path output_path;
        std::size_t document_count = 0;
        std::size_t query_count = 0;
        std::size_t embedding_dimensions = 0;
        std::size_t bit_count = 0;
        std::size_t result_limit = 0;
        std::size_t mutation_count = 0;
        std::uint64_t seed = 0;
        std::size_t multiprobe_table_count = 8;
        std::size_t multiprobe_bits_per_table = 8;
        std::size_t multiprobe_max_probe_radius = 1;
        std::size_t multiprobe_candidate_multiplier = 64;
        std::size_t multiprobe_minimum_candidate_count = 128;
    };

    struct Dataset final {
        std::vector<agent_memory::Embedding> documents;
        std::vector<agent_memory::Embedding> queries;
    };

    [[nodiscard]] double elapsed_ms(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    [[nodiscard]] std::uint64_t peak_resident_set_bytes() {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        if(GetProcessMemoryInfo(
               GetCurrentProcess(),
               &counters,
               static_cast<DWORD>(sizeof(counters))
           )) {
            return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
        }
        return 0;
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

    [[nodiscard]] const nlohmann::json* find_optional(
        const nlohmann::json& document,
        std::string_view field
    ) {
        const auto it = document.find(std::string{field});
        return it == document.end() ? nullptr : &*it;
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
        std::uint64_t value = 0;
        if(it->is_number_unsigned()) {
            value = it->get<std::uint64_t>();
        } else {
            const auto signed_value = it->get<std::int64_t>();
            if(signed_value <= 0) {
                throw std::runtime_error(
                    "config field '" + name + "' must be greater than zero"
                );
            }
            value = static_cast<std::uint64_t>(signed_value);
        }
        if(value == 0) {
            throw std::runtime_error(
                "config field '" + name + "' must be greater than zero"
            );
        }
        return value;
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

    [[nodiscard]] std::size_t optional_positive_size(
        const nlohmann::json& document,
        std::string_view field,
        std::size_t fallback
    ) {
        const auto* node = find_optional(document, field);
        if(node == nullptr) {
            return fallback;
        }
        const nlohmann::json wrapper = {{std::string{field}, *node}};
        return checked_size(require_positive_integer(wrapper, field), field);
    }

    [[nodiscard]] Config load_config(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if(!input) {
            throw std::runtime_error("cannot open binary lifecycle config");
        }
        nlohmann::json document;
        input >> document;
        if(!document.is_object()) {
            throw std::runtime_error("binary lifecycle config root must be an object");
        }

        Config config;
        const auto* output = find_optional(document, "output_path");
        if(output != nullptr) {
            if(!output->is_string() || output->get<std::string>().empty()) {
                throw std::runtime_error("config field 'output_path' must be a string");
            }
            config.output_path = output->get<std::string>();
        }
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
        config.bit_count = checked_size(
            require_positive_integer(document, "bit_count"),
            "bit_count"
        );
        config.result_limit = checked_size(
            require_positive_integer(document, "result_limit"),
            "result_limit"
        );
        config.mutation_count = checked_size(
            require_positive_integer(document, "mutation_count"),
            "mutation_count"
        );
        config.seed = require_positive_integer(document, "seed");
        config.multiprobe_table_count = optional_positive_size(
            document,
            "multiprobe_table_count",
            config.multiprobe_table_count
        );
        config.multiprobe_bits_per_table = optional_positive_size(
            document,
            "multiprobe_bits_per_table",
            config.multiprobe_bits_per_table
        );
        config.multiprobe_max_probe_radius = optional_positive_size(
            document,
            "multiprobe_max_probe_radius",
            config.multiprobe_max_probe_radius
        );
        config.multiprobe_candidate_multiplier = optional_positive_size(
            document,
            "multiprobe_candidate_multiplier",
            config.multiprobe_candidate_multiplier
        );
        config.multiprobe_minimum_candidate_count = optional_positive_size(
            document,
            "multiprobe_minimum_candidate_count",
            config.multiprobe_minimum_candidate_count
        );
        if(config.result_limit > config.document_count) {
            throw std::runtime_error("result_limit must not exceed document_count");
        }
        if(config.mutation_count > config.document_count) {
            throw std::runtime_error("mutation_count must not exceed document_count");
        }
        return config;
    }

    void normalize(agent_memory::Embedding& embedding) {
        double squared_norm = 0.0;
        for(const auto value : embedding.values) {
            squared_norm += static_cast<double>(value) * static_cast<double>(value);
        }
        if(squared_norm == 0.0) {
            throw std::runtime_error("cannot normalize a zero vector");
        }
        const auto scale = static_cast<float>(1.0 / std::sqrt(squared_norm));
        for(auto& value : embedding.values) {
            value *= scale;
        }
    }

    [[nodiscard]] Dataset generate_dataset(const Config& config) {
        std::mt19937_64 rng(config.seed);
        std::normal_distribution<float> normal(0.0F, 1.0F);
        std::normal_distribution<float> query_noise(0.0F, 0.08F);

        Dataset dataset;
        dataset.documents.reserve(config.document_count);
        dataset.queries.reserve(config.query_count);
        for(std::size_t index = 0; index < config.document_count; ++index) {
            agent_memory::Embedding embedding;
            embedding.values.reserve(config.embedding_dimensions);
            for(std::size_t dim = 0; dim < config.embedding_dimensions; ++dim) {
                embedding.values.push_back(normal(rng));
            }
            normalize(embedding);
            dataset.documents.push_back(std::move(embedding));
        }
        for(std::size_t query = 0; query < config.query_count; ++query) {
            agent_memory::Embedding embedding = dataset.documents[
                query % config.document_count
            ];
            for(auto& value : embedding.values) {
                value += query_noise(rng);
            }
            normalize(embedding);
            dataset.queries.push_back(std::move(embedding));
        }
        return dataset;
    }

    [[nodiscard]] agent_memory::ChunkId make_chunk_id(std::size_t index) {
        return agent_memory::ChunkId{"chunk:" + std::to_string(index)};
    }

    [[nodiscard]] agent_memory::BinarySignatureInfo make_signature_info(
        const Config& config,
        const agent_memory::BinarySignatureEncoderInfo& encoder_info
    ) {
        agent_memory::EmbeddingModelInfo model;
        model.model_id = "synthetic-lifecycle-normalized-v1";
        model.dimension = config.embedding_dimensions;
        model.similarity_metric = agent_memory::SimilarityMetric::Cosine;
        model.normalized = true;
        return agent_memory::make_binary_signature_info(
            encoder_info,
            model,
            "synthetic_dense_normalized"
        );
    }

    [[nodiscard]] std::vector<agent_memory::VectorRecord> make_vector_records(
        const Dataset& dataset
    ) {
        std::vector<agent_memory::VectorRecord> records;
        records.reserve(dataset.documents.size());
        for(std::size_t index = 0; index < dataset.documents.size(); ++index) {
            records.push_back({make_chunk_id(index), dataset.documents[index], {}});
        }
        return records;
    }

    [[nodiscard]] std::vector<agent_memory::BinarySignatureRecord>
    make_binary_records(
        const std::vector<agent_memory::BinarySignature>& signatures,
        const agent_memory::BinarySignatureInfo& signature_info
    ) {
        std::vector<agent_memory::BinarySignatureRecord> records;
        records.reserve(signatures.size());
        for(std::size_t index = 0; index < signatures.size(); ++index) {
            records.push_back({make_chunk_id(index), signatures[index], signature_info, {}});
        }
        return records;
    }

    template <class Index, class Records>
    [[nodiscard]] double build_index(Index& index, const Records& records) {
        const auto start = Clock::now();
        for(const auto& record : records) {
            index.upsert(record);
        }
        return elapsed_ms(start, Clock::now());
    }

    [[nodiscard]] std::vector<std::unordered_set<std::string>> exact_top_k_sets(
        const agent_memory::ExactVectorIndex& index,
        const Dataset& dataset,
        std::size_t limit
    ) {
        std::vector<std::unordered_set<std::string>> sets;
        sets.reserve(dataset.queries.size());
        for(const auto& query : dataset.queries) {
            agent_memory::VectorSearchQuery search_query;
            search_query.embedding = query;
            search_query.limit = limit;
            std::unordered_set<std::string> set;
            for(const auto& result : index.search(search_query)) {
                set.insert(result.chunk_id.value());
            }
            sets.push_back(std::move(set));
        }
        return sets;
    }

    template <class SearchFn>
    [[nodiscard]] nlohmann::json measure_binary_queries(
        const Dataset& dataset,
        const std::vector<agent_memory::BinarySignature>& query_signatures,
        const agent_memory::BinarySignatureInfo& signature_info,
        const std::vector<std::unordered_set<std::string>>& exact_sets,
        std::size_t limit,
        SearchFn&& search_fn
    ) {
        double coverage_sum = 0.0;
        std::size_t result_count = 0;
        const auto start = Clock::now();
        for(std::size_t query = 0; query < dataset.queries.size(); ++query) {
            agent_memory::BinarySignatureSearchQuery search_query;
            search_query.signature = query_signatures[query];
            search_query.signature_info = signature_info;
            search_query.limit = limit;

            const auto results = search_fn(search_query);
            result_count += results.size();
            std::size_t hits = 0;
            for(const auto& result : results) {
                if(exact_sets[query].find(result.chunk_id.value()) != exact_sets[query].end()) {
                    ++hits;
                }
            }
            coverage_sum += static_cast<double>(hits) /
                            static_cast<double>(exact_sets[query].size());
        }
        const auto total_ms = elapsed_ms(start, Clock::now());
        return {
            {"total_ms", total_ms},
            {"mean_ms", total_ms / static_cast<double>(dataset.queries.size())},
            {"mean_result_count", static_cast<double>(result_count) /
                                      static_cast<double>(dataset.queries.size())},
            {"exact_top_k_candidate_coverage",
             coverage_sum / static_cast<double>(dataset.queries.size())}
        };
    }

    template <class Index, class Records>
    [[nodiscard]] nlohmann::json measure_mutations(
        Index& index,
        const Records& records,
        std::size_t mutation_count
    ) {
        const auto erase_start = Clock::now();
        std::size_t erased = 0;
        for(std::size_t index_number = 0; index_number < mutation_count; ++index_number) {
            if(index.erase(records[index_number].chunk_id)) {
                ++erased;
            }
        }
        const auto erase_ms = elapsed_ms(erase_start, Clock::now());

        const auto upsert_start = Clock::now();
        for(std::size_t index_number = 0; index_number < mutation_count; ++index_number) {
            index.upsert(records[index_number]);
        }
        const auto upsert_ms = elapsed_ms(upsert_start, Clock::now());

        return {
            {"mutation_count", mutation_count},
            {"erased_count", erased},
            {"erase_ms", erase_ms},
            {"upsert_ms", upsert_ms}
        };
    }

    [[nodiscard]] nlohmann::json run_benchmark(const Config& config) {
        const auto data_start = Clock::now();
        const auto dataset = generate_dataset(config);
        const auto data_generation_ms = elapsed_ms(data_start, Clock::now());

        agent_memory::RandomHyperplaneBinaryEncoder encoder({
            config.embedding_dimensions,
            config.bit_count,
            config.seed
        });
        const auto signature_info = make_signature_info(config, encoder.info());
        const auto encode_start = Clock::now();
        const auto document_signatures = encoder.encode_batch(dataset.documents);
        const auto query_signatures = encoder.encode_batch(dataset.queries);
        const auto binary_encoding_ms = elapsed_ms(encode_start, Clock::now());

        const auto vector_records = make_vector_records(dataset);
        const auto binary_records = make_binary_records(document_signatures, signature_info);

        agent_memory::ExactVectorIndex exact({
            config.embedding_dimensions,
            agent_memory::SimilarityMetric::Cosine,
            true
        });
        const auto exact_build_ms = build_index(exact, vector_records);
        const auto exact_query_start = Clock::now();
        const auto exact_sets = exact_top_k_sets(exact, dataset, config.result_limit);
        const auto exact_query_ms = elapsed_ms(exact_query_start, Clock::now());

        agent_memory::FlatBinarySignatureIndex flat({signature_info});
        const auto flat_build_ms = build_index(flat, binary_records);

        agent_memory::MultiProbeHammingIndexOptions multiprobe_options;
        multiprobe_options.signature_info = signature_info;
        multiprobe_options.table_count = config.multiprobe_table_count;
        multiprobe_options.bits_per_table = config.multiprobe_bits_per_table;
        multiprobe_options.max_probe_radius = config.multiprobe_max_probe_radius;
        multiprobe_options.candidate_multiplier =
            config.multiprobe_candidate_multiplier;
        multiprobe_options.minimum_candidate_count =
            config.multiprobe_minimum_candidate_count;
        agent_memory::MultiProbeHammingIndex multiprobe(multiprobe_options);
        const auto multiprobe_build_ms = build_index(multiprobe, binary_records);

        const auto flat_query = measure_binary_queries(
            dataset,
            query_signatures,
            signature_info,
            exact_sets,
            config.result_limit,
            [&](const agent_memory::BinarySignatureSearchQuery& query) {
                return flat.search(query);
            }
        );

        double multiprobe_candidate_sum = 0.0;
        double multiprobe_bucket_sum = 0.0;
        double multiprobe_posting_sum = 0.0;
        const auto multiprobe_query = measure_binary_queries(
            dataset,
            query_signatures,
            signature_info,
            exact_sets,
            config.result_limit,
            [&](const agent_memory::BinarySignatureSearchQuery& query) {
                const auto result = multiprobe.search_with_diagnostics(query);
                multiprobe_candidate_sum += static_cast<double>(result.candidate_count);
                multiprobe_bucket_sum += static_cast<double>(result.probed_bucket_count);
                multiprobe_posting_sum += static_cast<double>(result.visited_posting_count);
                return result.results;
            }
        );

        auto flat_mutations = measure_mutations(flat, binary_records, config.mutation_count);
        auto multiprobe_mutations = measure_mutations(
            multiprobe,
            binary_records,
            config.mutation_count
        );

        const auto flat_rebuild_start = Clock::now();
        flat.clear();
        for(const auto& record : binary_records) {
            flat.upsert(record);
        }
        const auto flat_rebuild_ms = elapsed_ms(flat_rebuild_start, Clock::now());

        const auto multiprobe_rebuild_start = Clock::now();
        multiprobe.clear();
        for(const auto& record : binary_records) {
            multiprobe.upsert(record);
        }
        const auto multiprobe_rebuild_ms = elapsed_ms(
            multiprobe_rebuild_start,
            Clock::now()
        );

        const auto query_count = static_cast<double>(dataset.queries.size());
        const auto word_count = agent_memory::binary_signature_word_count(config.bit_count);
        const auto exact_payload_bytes = static_cast<std::uint64_t>(
            config.document_count * config.embedding_dimensions * sizeof(float)
        );
        const auto binary_payload_bytes = static_cast<std::uint64_t>(
            config.document_count * word_count * sizeof(std::uint64_t)
        );

        nlohmann::json report;
        report["schema_version"] = 1;
        report["mode"] = "synthetic_binary_lifecycle";
        report["document_count"] = config.document_count;
        report["query_count"] = config.query_count;
        report["embedding_dimensions"] = config.embedding_dimensions;
        report["bit_count"] = config.bit_count;
        report["result_limit"] = config.result_limit;
        report["mutation_count"] = config.mutation_count;
        report["seed"] = config.seed;
        report["timing_ms"] = {
            {"data_generation", data_generation_ms},
            {"binary_chunk_and_query_encoding", binary_encoding_ms}
        };
        report["exact_vector"] = {
            {"build_ms", exact_build_ms},
            {"query_total_ms", exact_query_ms},
            {"query_mean_ms", exact_query_ms / query_count},
            {"similarity_backend",
             std::string(agent_memory::vector_similarity_backend_name(
                 exact.similarity_backend()
             ))},
            {"payload_bytes", exact_payload_bytes}
        };
        report["flat_binary"] = {
            {"build_ms", flat_build_ms},
            {"query", flat_query},
            {"mutations", flat_mutations},
            {"rebuild_ms", flat_rebuild_ms},
            {"hamming_backend",
             std::string(agent_memory::hamming_distance_backend_name(
                 *flat.hamming_backend()
             ))},
            {"payload_bytes", binary_payload_bytes}
        };
        report["multiprobe_binary"] = {
            {"build_ms", multiprobe_build_ms},
            {"query", multiprobe_query},
            {"mutations", multiprobe_mutations},
            {"rebuild_ms", multiprobe_rebuild_ms},
            {"mean_candidate_count", multiprobe_candidate_sum / query_count},
            {"mean_probed_bucket_count", multiprobe_bucket_sum / query_count},
            {"mean_visited_posting_count", multiprobe_posting_sum / query_count},
            {"options",
             {
                 {"table_count", config.multiprobe_table_count},
                 {"bits_per_table", config.multiprobe_bits_per_table},
                 {"max_probe_radius", config.multiprobe_max_probe_radius},
                 {"candidate_multiplier", config.multiprobe_candidate_multiplier},
                 {"minimum_candidate_count", config.multiprobe_minimum_candidate_count}
             }},
            {"payload_bytes", binary_payload_bytes}
        };
        report["process_peak_resident_set_bytes"] = peak_resident_set_bytes();
        return report;
    }

} // namespace

int main(int argc, char** argv) {
    if(argc < 2 || argc > 3) {
        std::cerr
            << "usage: agent-memory-binary-lifecycle-bench <config.json> "
               "[output.json]\n";
        return 2;
    }

    try {
        auto config = load_config(fs::path{argv[1]});
        if(argc == 3) {
            config.output_path = fs::path{argv[2]};
        }
        if(config.output_path.empty()) {
            throw std::runtime_error(
                "config field 'output_path' is required unless output path is passed"
            );
        }
        const auto report = run_benchmark(config);
        std::ofstream output(config.output_path, std::ios::binary);
        if(!output) {
            throw std::runtime_error(
                "cannot open binary lifecycle output: " + config.output_path.string()
            );
        }
        output << report.dump(2) << '\n';
        std::cout << "JSON output: " << config.output_path.string() << '\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "agent-memory-binary-lifecycle-bench: " << error.what()
                  << '\n';
        return 1;
    }
}
