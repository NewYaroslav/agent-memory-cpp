#include <agent_memory/embedding/embedding_types.hpp>
#include <agent_memory/index/AggregateBinarySignature.hpp>
#include <agent_memory/index/BinarySignature.hpp>
#include <agent_memory/index/RandomHyperplaneBinaryEncoder.hpp>
#include <agent_memory/index/VectorSimilarityComputer.hpp>

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
#include <utility>
#include <vector>

namespace {

    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    struct Config final {
        fs::path output_path;
        std::size_t document_count = 0;
        std::size_t chunks_per_document = 0;
        std::size_t query_count = 0;
        std::size_t embedding_dimensions = 0;
        std::size_t bit_count = 0;
        std::size_t result_limit = 0;
        std::uint64_t seed = 0;
        double chunk_noise = 0.20;
        double query_noise = 0.08;
        double threshold_fraction = 0.5;
        std::vector<std::size_t> candidate_limits;
        std::vector<agent_memory::BinarySignatureAggregationMode> modes;
    };

    struct Dataset final {
        std::vector<agent_memory::Embedding> chunks;
        std::vector<agent_memory::Embedding> queries;
    };

    struct RankedDocument final {
        std::size_t document_index = 0;
        double score = 0.0;
    };

    [[nodiscard]] double elapsed_ms(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
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

    [[nodiscard]] double optional_positive_double(
        const nlohmann::json& document,
        std::string_view field,
        double fallback
    ) {
        const auto* node = find_optional(document, field);
        if(node == nullptr) {
            return fallback;
        }
        if(!node->is_number()) {
            throw std::runtime_error(
                "config field '" + std::string{field} + "' must be a number"
            );
        }
        const auto value = node->get<double>();
        if(!std::isfinite(value) || value <= 0.0) {
            throw std::runtime_error(
                "config field '" + std::string{field}
                + "' must be finite and greater than zero"
            );
        }
        return value;
    }

    [[nodiscard]] std::vector<std::size_t> read_size_list(
        const nlohmann::json& document,
        std::string_view field
    ) {
        const auto* node = find_optional(document, field);
        if(node == nullptr || !node->is_array() || node->empty()) {
            throw std::runtime_error(
                "config field '" + std::string{field}
                + "' must be a non-empty integer array"
            );
        }
        std::vector<std::size_t> values;
        values.reserve(node->size());
        std::set<std::size_t> seen;
        for(const auto& entry : *node) {
            if(!entry.is_number_integer()) {
                throw std::runtime_error(
                    "config field '" + std::string{field}
                    + "' entries must be integers"
                );
            }
            std::uint64_t raw = 0;
            if(entry.is_number_unsigned()) {
                raw = entry.get<std::uint64_t>();
            } else {
                const auto signed_value = entry.get<std::int64_t>();
                if(signed_value <= 0) {
                    throw std::runtime_error(
                        "config field '" + std::string{field}
                        + "' entries must be greater than zero"
                    );
                }
                raw = static_cast<std::uint64_t>(signed_value);
            }
            const auto value = checked_size(raw, field);
            if(!seen.insert(value).second) {
                throw std::runtime_error(
                    "config field '" + std::string{field}
                    + "' must not contain duplicate values"
                );
            }
            values.push_back(value);
        }
        return values;
    }

    [[nodiscard]] agent_memory::BinarySignatureAggregationMode parse_mode(
        const std::string& value
    ) {
        if(value == "any_set_bit") {
            return agent_memory::BinarySignatureAggregationMode::AnySetBit;
        }
        if(value == "majority_set_bit") {
            return agent_memory::BinarySignatureAggregationMode::MajoritySetBit;
        }
        if(value == "all_set_bits") {
            return agent_memory::BinarySignatureAggregationMode::AllSetBits;
        }
        if(value == "threshold_fraction") {
            return agent_memory::BinarySignatureAggregationMode::ThresholdFraction;
        }
        throw std::runtime_error("unsupported aggregation mode: " + value);
    }

    [[nodiscard]] std::string mode_name(
        agent_memory::BinarySignatureAggregationMode mode
    ) {
        switch(mode) {
            case agent_memory::BinarySignatureAggregationMode::AnySetBit:
                return "any_set_bit";
            case agent_memory::BinarySignatureAggregationMode::MajoritySetBit:
                return "majority_set_bit";
            case agent_memory::BinarySignatureAggregationMode::AllSetBits:
                return "all_set_bits";
            case agent_memory::BinarySignatureAggregationMode::ThresholdFraction:
                return "threshold_fraction";
        }
        return "unknown";
    }

    [[nodiscard]] std::vector<agent_memory::BinarySignatureAggregationMode>
    read_modes(const nlohmann::json& document) {
        const auto* node = find_optional(document, "aggregation_modes");
        if(node == nullptr || !node->is_array() || node->empty()) {
            throw std::runtime_error(
                "config field 'aggregation_modes' must be a non-empty string array"
            );
        }
        std::vector<agent_memory::BinarySignatureAggregationMode> modes;
        modes.reserve(node->size());
        std::set<std::string> seen;
        for(const auto& entry : *node) {
            if(!entry.is_string()) {
                throw std::runtime_error(
                    "config field 'aggregation_modes' entries must be strings"
                );
            }
            auto value = entry.get<std::string>();
            if(!seen.insert(value).second) {
                throw std::runtime_error(
                    "config field 'aggregation_modes' must not contain duplicates"
                );
            }
            modes.push_back(parse_mode(value));
        }
        return modes;
    }

    [[nodiscard]] Config load_config(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if(!input) {
            throw std::runtime_error("cannot open aggregate benchmark config");
        }

        nlohmann::json document;
        input >> document;
        if(!document.is_object()) {
            throw std::runtime_error("aggregate benchmark config root must be an object");
        }

        Config config;
        const auto output = find_optional(document, "output_path");
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
        config.chunks_per_document = checked_size(
            require_positive_integer(document, "chunks_per_document"),
            "chunks_per_document"
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
        config.seed = require_positive_integer(document, "seed");
        config.candidate_limits = read_size_list(document, "candidate_limits");
        config.modes = read_modes(document);
        config.chunk_noise = optional_positive_double(document, "chunk_noise", 0.20);
        config.query_noise = optional_positive_double(document, "query_noise", 0.08);
        config.threshold_fraction = optional_positive_double(
            document,
            "threshold_fraction",
            0.5
        );
        if(config.result_limit > config.document_count) {
            throw std::runtime_error("result_limit must not exceed document_count");
        }
        for(const auto limit : config.candidate_limits) {
            if(limit > config.document_count) {
                throw std::runtime_error(
                    "candidate_limits entries must not exceed document_count"
                );
            }
        }
        if(config.threshold_fraction > 1.0) {
            throw std::runtime_error("threshold_fraction must not exceed 1.0");
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

    [[nodiscard]] agent_memory::Embedding noisy_copy(
        const agent_memory::Embedding& source,
        double noise,
        std::mt19937_64& rng
    ) {
        std::normal_distribution<float> distribution(
            0.0F,
            static_cast<float>(noise)
        );
        agent_memory::Embedding output;
        output.values.reserve(source.values.size());
        for(const auto value : source.values) {
            output.values.push_back(value + distribution(rng));
        }
        normalize(output);
        return output;
    }

    [[nodiscard]] Dataset generate_dataset(const Config& config) {
        std::mt19937_64 rng(config.seed);
        std::normal_distribution<float> normal(0.0F, 1.0F);
        Dataset dataset;
        dataset.chunks.reserve(config.document_count * config.chunks_per_document);
        dataset.queries.reserve(config.query_count);

        std::vector<agent_memory::Embedding> centers;
        centers.reserve(config.document_count);
        for(std::size_t document = 0; document < config.document_count; ++document) {
            agent_memory::Embedding center;
            center.values.reserve(config.embedding_dimensions);
            for(std::size_t dim = 0; dim < config.embedding_dimensions; ++dim) {
                center.values.push_back(normal(rng));
            }
            normalize(center);
            centers.push_back(center);

            for(std::size_t chunk = 0; chunk < config.chunks_per_document; ++chunk) {
                dataset.chunks.push_back(noisy_copy(
                    centers.back(),
                    config.chunk_noise,
                    rng
                ));
            }
        }

        for(std::size_t query = 0; query < config.query_count; ++query) {
            const auto document = query % config.document_count;
            const auto chunk = query % config.chunks_per_document;
            const auto chunk_index = (document * config.chunks_per_document) + chunk;
            dataset.queries.push_back(noisy_copy(
                dataset.chunks[chunk_index],
                config.query_noise,
                rng
            ));
        }

        return dataset;
    }

    [[nodiscard]] std::vector<RankedDocument> exact_rank_documents(
        const Config& config,
        const Dataset& dataset,
        const agent_memory::Embedding& query,
        const agent_memory::VectorSimilarityComputer& similarity
    ) {
        std::vector<RankedDocument> ranking;
        ranking.reserve(config.document_count);
        for(std::size_t document = 0; document < config.document_count; ++document) {
            double best_score = -std::numeric_limits<double>::infinity();
            for(std::size_t chunk = 0; chunk < config.chunks_per_document; ++chunk) {
                const auto chunk_index = (document * config.chunks_per_document) + chunk;
                best_score = std::max(
                    best_score,
                    static_cast<double>(
                        similarity.dot_product(dataset.chunks[chunk_index], query)
                    )
                );
            }
            ranking.push_back({document, best_score});
        }
        const auto top = ranking.begin() +
                         static_cast<std::ptrdiff_t>(config.result_limit);
        std::partial_sort(
            ranking.begin(),
            top,
            ranking.end(),
            [](const RankedDocument& lhs, const RankedDocument& rhs) {
                if(lhs.score != rhs.score) {
                    return lhs.score > rhs.score;
                }
                return lhs.document_index < rhs.document_index;
            }
        );
        ranking.resize(config.result_limit);
        return ranking;
    }

    [[nodiscard]] std::vector<RankedDocument> aggregate_rank_documents(
        const std::vector<agent_memory::BinarySignature>& aggregates,
        const agent_memory::BinarySignature& query,
        std::size_t candidate_limit
    ) {
        std::vector<RankedDocument> ranking;
        ranking.reserve(aggregates.size());
        for(std::size_t document = 0; document < aggregates.size(); ++document) {
            ranking.push_back({
                document,
                -static_cast<double>(
                    agent_memory::hamming_distance(query, aggregates[document])
                )
            });
        }
        const auto top = ranking.begin() + static_cast<std::ptrdiff_t>(candidate_limit);
        std::partial_sort(
            ranking.begin(),
            top,
            ranking.end(),
            [](const RankedDocument& lhs, const RankedDocument& rhs) {
                if(lhs.score != rhs.score) {
                    return lhs.score > rhs.score;
                }
                return lhs.document_index < rhs.document_index;
            }
        );
        ranking.resize(candidate_limit);
        return ranking;
    }

    [[nodiscard]] double exact_top_k_coverage(
        const std::vector<RankedDocument>& exact_top_k,
        const std::vector<RankedDocument>& candidates
    ) {
        std::unordered_set<std::size_t> candidate_set;
        candidate_set.reserve(candidates.size());
        for(const auto& candidate : candidates) {
            candidate_set.insert(candidate.document_index);
        }

        std::size_t hits = 0;
        for(const auto& exact : exact_top_k) {
            if(candidate_set.find(exact.document_index) != candidate_set.end()) {
                ++hits;
            }
        }
        return static_cast<double>(hits) / static_cast<double>(exact_top_k.size());
    }

    [[nodiscard]] std::vector<agent_memory::BinarySignature> aggregate_documents(
        const Config& config,
        const std::vector<agent_memory::BinarySignature>& chunk_signatures,
        agent_memory::BinarySignatureAggregationMode mode
    ) {
        agent_memory::BinarySignatureAggregationOptions options;
        options.mode = mode;
        options.threshold_fraction = config.threshold_fraction;

        std::vector<agent_memory::BinarySignature> aggregates;
        aggregates.reserve(config.document_count);
        for(std::size_t document = 0; document < config.document_count; ++document) {
            agent_memory::AggregateBinarySignatureBuilder builder(
                config.bit_count,
                options
            );
            for(std::size_t chunk = 0; chunk < config.chunks_per_document; ++chunk) {
                const auto chunk_index = (document * config.chunks_per_document) + chunk;
                builder.add(chunk_signatures[chunk_index]);
            }
            aggregates.push_back(builder.signature());
        }
        return aggregates;
    }

    [[nodiscard]] nlohmann::json run_benchmark(const Config& config) {
        const auto data_start = Clock::now();
        const auto dataset = generate_dataset(config);
        const auto data_generation_ms = elapsed_ms(data_start, Clock::now());

        agent_memory::VectorSimilarityComputer similarity;
        std::vector<std::vector<RankedDocument>> exact_rankings;
        exact_rankings.reserve(dataset.queries.size());
        const auto exact_start = Clock::now();
        for(const auto& query : dataset.queries) {
            exact_rankings.push_back(exact_rank_documents(
                config,
                dataset,
                query,
                similarity
            ));
        }
        const auto exact_query_ms = elapsed_ms(exact_start, Clock::now());

        agent_memory::RandomHyperplaneBinaryEncoder encoder({
            config.embedding_dimensions,
            config.bit_count,
            config.seed
        });
        const auto encode_start = Clock::now();
        const auto chunk_signatures = encoder.encode_batch(dataset.chunks);
        const auto query_signatures = encoder.encode_batch(dataset.queries);
        const auto binary_encoding_ms = elapsed_ms(encode_start, Clock::now());

        nlohmann::json report;
        report["schema_version"] = 1;
        report["mode"] = "synthetic_aggregate_binary_signature";
        report["document_count"] = config.document_count;
        report["chunks_per_document"] = config.chunks_per_document;
        report["query_count"] = config.query_count;
        report["embedding_dimensions"] = config.embedding_dimensions;
        report["bit_count"] = config.bit_count;
        report["result_limit"] = config.result_limit;
        report["candidate_limits"] = config.candidate_limits;
        report["seed"] = config.seed;
        report["chunk_noise"] = config.chunk_noise;
        report["query_noise"] = config.query_noise;
        report["encoder_family"] = encoder.info().encoder_id;
        report["encoder_version"] = encoder.info().encoder_version;
        report["hamming_backend"] =
            std::string(agent_memory::hamming_distance_backend_name(
                agent_memory::HammingDistanceComputer(
                    agent_memory::binary_signature_word_count(config.bit_count)
                ).backend()
            ));
        report["vector_similarity_backend"] =
            std::string(agent_memory::vector_similarity_backend_name(
                similarity.backend()
            ));
        report["timing_ms"] = {
            {"data_generation", data_generation_ms},
            {"exact_object_query", exact_query_ms},
            {"binary_chunk_and_query_encoding", binary_encoding_ms}
        };

        for(const auto mode : config.modes) {
            const auto aggregation_start = Clock::now();
            const auto aggregates = aggregate_documents(config, chunk_signatures, mode);
            const auto aggregation_ms = elapsed_ms(aggregation_start, Clock::now());

            nlohmann::json mode_report;
            mode_report["aggregation_mode"] = mode_name(mode);
            if(mode == agent_memory::BinarySignatureAggregationMode::ThresholdFraction) {
                mode_report["threshold_fraction"] = config.threshold_fraction;
            }
            mode_report["aggregation_build_ms"] = aggregation_ms;

            for(const auto candidate_limit : config.candidate_limits) {
                double coverage_sum = 0.0;
                double top1_sum = 0.0;
                const auto query_start = Clock::now();
                for(std::size_t query = 0; query < dataset.queries.size(); ++query) {
                    const auto candidates = aggregate_rank_documents(
                        aggregates,
                        query_signatures[query],
                        candidate_limit
                    );
                    coverage_sum += exact_top_k_coverage(exact_rankings[query], candidates);
                    top1_sum += candidates.front().document_index ==
                                exact_rankings[query].front().document_index
                                    ? 1.0
                                    : 0.0;
                }
                const auto aggregate_query_ms = elapsed_ms(query_start, Clock::now());
                const auto denominator = static_cast<double>(dataset.queries.size());
                mode_report["candidate_limits"].push_back({
                    {"candidate_limit", candidate_limit},
                    {"exact_top_k_candidate_coverage", coverage_sum / denominator},
                    {"top1_agreement_vs_exact", top1_sum / denominator},
                    {"aggregate_query_ms", aggregate_query_ms}
                });
            }
            report["aggregation_reports"].push_back(std::move(mode_report));
        }
        return report;
    }

} // namespace

int main(int argc, char** argv) {
    if(argc < 2 || argc > 3) {
        std::cerr
            << "usage: agent-memory-aggregate-signature-bench <config.json> "
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

        auto report = run_benchmark(config);
        std::ofstream output(config.output_path, std::ios::binary);
        if(!output) {
            throw std::runtime_error(
                "cannot open aggregate benchmark output: " +
                config.output_path.string()
            );
        }
        output << report.dump(2) << '\n';
        std::cout << "JSON output: " << config.output_path.string() << '\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "agent-memory-aggregate-signature-bench: " << error.what()
                  << '\n';
        return 1;
    }
}
