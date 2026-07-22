#include <agent_memory/eval/BenchmarkReport.hpp>
#include <agent_memory/eval/DatasetLoader.hpp>
#include <agent_memory/eval/Evaluation.hpp>
#include <agent_memory/eval/IRetrieverAdapter.hpp>
#include <agent_memory/eval/PrecomputedEmbeddingDataset.hpp>
#include <agent_memory/eval/RetrievalEvalRunner.hpp>
#include <agent_memory/index/BinarySignatureInfo.hpp>
#include <agent_memory/index/CoordinateSignBinaryEncoder.hpp>
#include <agent_memory/index/ExactVectorIndex.hpp>
#include <agent_memory/index/FlatBinarySignatureIndex.hpp>
#include <agent_memory/index/IBinarySignatureEncoder.hpp>
#include <agent_memory/index/ItqRotationBinaryEncoder.hpp>
#include <agent_memory/index/LearnedProjectionBinaryEncoder.hpp>
#include <agent_memory/index/PcaProjectionBinaryEncoder.hpp>
#include <agent_memory/index/RandomHyperplaneBinaryEncoder.hpp>
#include <agent_memory/index/RandomizedHadamardBinaryEncoder.hpp>
#include <agent_memory/index/VectorSimilarityComputer.hpp>
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
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
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
    constexpr std::string_view kModeSyntheticBinaryRerankGrid =
        "synthetic_binary_rerank_grid";
    constexpr std::string_view kModePrecomputedEmbeddingBinaryRerankGrid =
        "precomputed_embedding_binary_rerank_grid";
    constexpr std::string_view kBaselineNameBm25Exact = "bm25_exact";
    constexpr std::string_view kEncoderFamilyRandomHyperplane =
        "random_hyperplane_rademacher";
    constexpr std::string_view kEncoderFamilyCoordinateSign = "coordinate_sign";
    constexpr std::string_view kEncoderFamilyRandomizedHadamard =
        "randomized_hadamard_projection";
    constexpr std::string_view kEncoderFamilyLearnedPairDifference =
        "learned_pair_difference_projection";
    constexpr std::string_view kEncoderFamilyPcaProjection = "pca_projection";
    constexpr std::string_view kEncoderFamilyItqRotation = "itq_rotation_projection";

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
        std::vector<std::size_t> bit_counts;
        std::vector<std::size_t> rerank_candidate_limits;
        std::vector<std::uint32_t> seeds;
        std::vector<std::uint32_t> data_seeds;
        std::vector<std::uint32_t> encoder_seeds;
        std::vector<std::string> encoder_families;
        std::size_t repeat_count = 1;
        std::size_t exact_timing_repeat_count = 1;
        bool randomize_execution_order = false;
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

    struct BinaryEncoderBuildMetrics final {
        double encoder_training_ms = 0.0;
        std::size_t training_vector_count = 0;
        std::uint64_t artifact_payload_bytes = 0;
        std::string training_source = "none";
    };

    struct BinaryRerankCandidateResult final {
        std::size_t candidate_limit = 0;
        SearchTiming binary_candidate_query;
        double exact_rerank_ms = 0.0;
        double total_ms = 0.0;
        double exact_top_k_candidate_coverage = 0.0;
        double reranked_recall_at_k_vs_exact = 0.0;
        double reranked_top1_agreement = 0.0;
    };

    struct BinaryFlatVsFloatResult final {
        double data_generation_ms = 0.0;
        double current_exact_build_ms = 0.0;
        double contiguous_exact_build_ms = 0.0;
        double binary_build_ms = 0.0;
        std::string current_exact_similarity_backend;
        std::string contiguous_exact_similarity_backend;
        std::string binary_hamming_backend;
        std::string binary_encoder_similarity_backend;
        BinaryEncoderBuildMetrics encoder_build;
        SearchTiming current_exact_query;
        std::vector<double> current_exact_query_total_ms_samples;
        SearchTiming contiguous_exact_query;
        std::vector<double> contiguous_exact_query_total_ms_samples;
        SearchTiming binary_query;
        std::vector<BinaryRerankCandidateResult> rerank_candidates;
        double mean_recall_at_k_vs_exact = 0.0;
        double top1_agreement = 0.0;
        std::uint64_t exact_payload_bytes = 0;
        std::uint64_t binary_payload_bytes = 0;
        std::uint64_t process_peak_resident_set_bytes = 0;
    };

    struct BinaryBenchmarkOracle final {
        SyntheticDenseData data;
        double data_generation_ms = 0.0;
        double current_exact_build_ms = 0.0;
        double contiguous_exact_build_ms = 0.0;
        std::string current_exact_similarity_backend;
        std::string contiguous_exact_similarity_backend;
        SearchTiming current_exact_query;
        std::vector<double> current_exact_query_total_ms_samples;
        SearchTiming contiguous_exact_query;
        std::vector<double> contiguous_exact_query_total_ms_samples;
        std::vector<std::vector<std::string>> exact_top_k;
        std::vector<std::unordered_set<std::string>> exact_top_k_sets;
        std::uint64_t exact_payload_bytes = 0;
    };

    struct BinaryGridTask final {
        std::size_t run_index = 0;
        std::size_t bit_count = 0;
        std::size_t repeat_index = 0;
    };

    struct BinaryGridRunState final {
        std::string encoder_family;
        std::uint32_t encoder_seed = 0;
        nlohmann::json seed_run;
        std::vector<std::vector<BinaryFlatVsFloatResult>> results_by_bit;
        std::vector<std::vector<nlohmann::json>> reports_by_bit;
        std::vector<std::unique_ptr<agent_memory::IBinarySignatureEncoder>>
            encoders_by_bit;
        std::vector<BinaryEncoderBuildMetrics> encoder_build_metrics_by_bit;
    };

    struct BinaryEncoderBuild final {
        std::unique_ptr<agent_memory::IBinarySignatureEncoder> encoder;
        BinaryEncoderBuildMetrics metrics;
    };

    struct PrecomputedExactOracle final {
        double exact_build_ms = 0.0;
        double exact_query_ms = 0.0;
        std::string exact_similarity_backend;
        std::vector<std::vector<std::string>> exact_top_k;
        std::vector<std::unordered_set<std::string>> exact_top_k_sets;
        agent_memory::RetrievalMetrics qrels_quality;
        std::uint64_t exact_payload_bytes = 0;
    };

    struct PrecomputedRerankResult final {
        std::size_t candidate_limit = 0;
        SearchTiming binary_candidate_query;
        double exact_rerank_ms = 0.0;
        double total_ms = 0.0;
        double exact_top_k_candidate_coverage = 0.0;
        double qrels_candidate_relevant_coverage = 0.0;
        double reranked_recall_at_10 = 0.0;
        double reranked_ndcg_at_10 = 0.0;
        double reranked_mrr = 0.0;
    };

    struct PrecomputedEncoderResult final {
        std::string encoder_family;
        std::uint32_t encoder_seed = 0;
        std::size_t bit_count = 0;
        agent_memory::BinarySignatureEncoderInfo encoder_info;
        BinaryEncoderBuildMetrics encoder_build;
        double binary_build_ms = 0.0;
        std::string binary_hamming_backend;
        std::string binary_encoder_similarity_backend;
        std::uint64_t binary_payload_bytes = 0;
        std::vector<PrecomputedRerankResult> rerank;
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

    [[nodiscard]] bool optional_bool(
        const nlohmann::json& document,
        std::string_view field,
        bool fallback
    ) {
        const auto* node = find_optional(document, field);
        if(node == nullptr) {
            return fallback;
        }
        if(!node->is_boolean()) {
            throw std::runtime_error(
                "config field '" + std::string{field} + "' must be a boolean"
            );
        }
        return node->get<bool>();
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

    [[nodiscard]] std::vector<std::string> read_optional_string_list(
        const nlohmann::json& document,
        std::string_view field
    ) {
        const auto* node = find_optional(document, field);
        if(node == nullptr) {
            return {};
        }
        if(!node->is_array() || node->empty()) {
            throw std::runtime_error(
                "config field '" + std::string{field}
                + "' must be a non-empty string array"
            );
        }

        std::vector<std::string> values;
        values.reserve(node->size());
        std::set<std::string> seen;
        for(const auto& entry : *node) {
            if(!entry.is_string()) {
                throw std::runtime_error(
                    "config field '" + std::string{field}
                    + "' entries must be strings"
                );
            }
            auto value = entry.get<std::string>();
            if(value.empty()) {
                throw std::runtime_error(
                    "config field '" + std::string{field}
                    + "' entries must not be empty"
                );
            }
            if(!seen.insert(value).second) {
                throw std::runtime_error(
                    "config field '" + std::string{field}
                    + "' must not contain duplicate values"
                );
            }
            values.push_back(std::move(value));
        }
        return values;
    }

    [[nodiscard]] std::vector<std::size_t> read_optional_size_list(
        const nlohmann::json& document,
        std::string_view field
    ) {
        const auto* node = find_optional(document, field);
        if(node == nullptr) {
            return {};
        }
        if(!node->is_array() || node->empty()) {
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
            if(raw == 0) {
                throw std::runtime_error(
                    "config field '" + std::string{field}
                    + "' entries must be greater than zero"
                );
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

    [[nodiscard]] std::vector<std::uint32_t> read_optional_uint32_list(
        const nlohmann::json& document,
        std::string_view field
    ) {
        const auto values = read_optional_size_list(document, field);
        std::vector<std::uint32_t> out;
        out.reserve(values.size());
        for(const auto value : values) {
            if(value > static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max()
            )) {
                throw std::runtime_error(
                    "config field '" + std::string{field} + "' entries exceed uint32_t"
                );
            }
            out.push_back(static_cast<std::uint32_t>(value));
        }
        return out;
    }

    [[nodiscard]] std::vector<std::size_t> default_rerank_candidate_limits(
        std::size_t result_limit,
        std::size_t document_count
    ) {
        const std::vector<std::size_t> multipliers = {1, 5, 10, 50};
        std::vector<std::size_t> limits;
        std::set<std::size_t> seen;
        for(const auto multiplier : multipliers) {
            std::size_t limit = result_limit;
            if(multiplier != 0 && result_limit <= (
                std::numeric_limits<std::size_t>::max() / multiplier
            )) {
                limit = result_limit * multiplier;
            } else {
                limit = document_count;
            }
            limit = std::min(limit, document_count);
            if(limit != 0 && seen.insert(limit).second) {
                limits.push_back(limit);
            }
        }
        return limits;
    }

    void validate_rerank_candidate_limits(const BenchmarkConfig& config) {
        for(const auto limit : config.rerank_candidate_limits) {
            if(limit < config.result_limit) {
                throw std::runtime_error(
                    "config field 'rerank_candidate_limits' entries must be at least result_limit"
                );
            }
            if(limit > config.document_count) {
                throw std::runtime_error(
                    "config field 'rerank_candidate_limits' entries must not exceed document_count"
                );
            }
        }
    }

    [[nodiscard]] bool is_supported_encoder_family(const std::string& family) noexcept {
        return family == kEncoderFamilyRandomHyperplane
            || family == kEncoderFamilyCoordinateSign
            || family == kEncoderFamilyRandomizedHadamard
            || family == kEncoderFamilyLearnedPairDifference
            || family == kEncoderFamilyPcaProjection
            || family == kEncoderFamilyItqRotation;
    }

    [[nodiscard]] bool encoder_family_uses_seed(const std::string& family) noexcept {
        return family != kEncoderFamilyCoordinateSign;
    }

    [[nodiscard]] std::uint32_t encoder_family_salt(const std::string& family) noexcept {
        if(family == kEncoderFamilyRandomHyperplane) {
            return 0xA511E9B3U;
        }
        if(family == kEncoderFamilyCoordinateSign) {
            return 0x63D83595U;
        }
        if(family == kEncoderFamilyRandomizedHadamard) {
            return 0x9E3779B9U;
        }
        if(family == kEncoderFamilyLearnedPairDifference) {
            return 0xC2B2AE35U;
        }
        if(family == kEncoderFamilyPcaProjection) {
            return 0x27D4EB2FU;
        }
        if(family == kEncoderFamilyItqRotation) {
            return 0x94D049BBU;
        }
        return 0x85EBCA6BU;
    }

    [[nodiscard]] bool encoder_family_supports_bit_count(
        const std::string& family,
        std::size_t input_dimension,
        std::size_t bit_count
    ) noexcept {
        if(family == kEncoderFamilyCoordinateSign) {
            return bit_count == input_dimension;
        }
        if(family == kEncoderFamilyPcaProjection
           || family == kEncoderFamilyItqRotation) {
            return bit_count <= input_dimension;
        }
        return true;
    }

    void validate_encoder_families(const BenchmarkConfig& config) {
        for(const auto& family : config.encoder_families) {
            if(!is_supported_encoder_family(family)) {
                throw std::runtime_error(
                    "unsupported binary encoder family: " + family
                );
            }
            if(family == kEncoderFamilyCoordinateSign
               && std::find(
                   config.bit_counts.begin(),
                   config.bit_counts.end(),
                   config.embedding_dimensions
               ) == config.bit_counts.end()) {
                throw std::runtime_error(
                    "coordinate_sign encoder family requires bit_counts to include "
                    "embedding_dimensions"
                );
            }
            if((family == kEncoderFamilyPcaProjection
                || family == kEncoderFamilyItqRotation)
               && std::none_of(
                   config.bit_counts.begin(),
                   config.bit_counts.end(),
                   [&](std::size_t bit_count) {
                       return bit_count <= config.embedding_dimensions;
                   }
               )) {
                throw std::runtime_error(
                    family
                    + " encoder family requires at least one bit_count not greater "
                      "than embedding_dimensions"
                );
            }
        }
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
        const bool binary_rerank_grid =
            config.mode == kModeSyntheticBinaryRerankGrid;
        const bool precomputed_binary_rerank_grid =
            config.mode == kModePrecomputedEmbeddingBinaryRerankGrid;
        if(config.mode != kModeRandomExact
           && !binary_flat_vs_float
           && !binary_rerank_grid
           && !precomputed_binary_rerank_grid) {
            throw std::runtime_error(
                "config field 'mode' must be synthetic_random_exact, synthetic_sweep, "
                "synthetic_binary_flat_vs_float, synthetic_binary_rerank_grid, "
                "or precomputed_embedding_binary_rerank_grid"
            );
        }

        if(precomputed_binary_rerank_grid) {
            config.dataset_path = require_string(document, "dataset_path");
            config.result_limit = checked_size(
                require_positive_integer(document, "result_limit"),
                "result_limit"
            );
            config.bit_counts = read_optional_size_list(document, "bit_counts");
            if(config.bit_counts.empty()) {
                throw std::runtime_error(
                    "config field 'bit_counts' must be provided for "
                    "precomputed_embedding_binary_rerank_grid"
                );
            }
            config.rerank_candidate_limits = read_optional_size_list(
                document,
                "rerank_candidate_limits"
            );
            config.encoder_families = read_optional_string_list(
                document,
                "encoder_families"
            );
            if(config.encoder_families.empty()) {
                config.encoder_families = {std::string{kEncoderFamilyRandomHyperplane}};
            }
            if(find_optional(document, "seed") != nullptr) {
                const auto seed = require_positive_integer(document, "seed");
                if(seed > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::runtime_error("config field 'seed' exceeds uint32_t");
                }
                config.seed = static_cast<std::uint32_t>(seed);
                config.encoder_seeds = {config.seed};
            }
            auto seeds = read_optional_uint32_list(document, "seeds");
            if(!seeds.empty()) {
                config.encoder_seeds = std::move(seeds);
                config.seed = config.encoder_seeds.front();
            }
            auto encoder_seeds = read_optional_uint32_list(document, "encoder_seeds");
            if(!encoder_seeds.empty()) {
                config.encoder_seeds = std::move(encoder_seeds);
                config.seed = config.encoder_seeds.front();
            }
            if(config.encoder_seeds.empty()) {
                throw std::runtime_error(
                    "precomputed_embedding_binary_rerank_grid requires 'seed', "
                    "'seeds', or 'encoder_seeds'"
                );
            }
            return config;
        }

        if(!binary_flat_vs_float && !binary_rerank_grid) {
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
            config.rerank_candidate_limits = read_optional_size_list(
                document,
                "rerank_candidate_limits"
            );
        } else if(binary_rerank_grid) {
            config.bit_counts = read_optional_size_list(document, "bit_counts");
            if(config.bit_counts.empty()) {
                throw std::runtime_error(
                    "config field 'bit_counts' must be provided for synthetic_binary_rerank_grid"
                );
            }
            config.rerank_candidate_limits = read_optional_size_list(
                document,
                "rerank_candidate_limits"
            );
        }
        config.result_limit = checked_size(
            require_positive_integer(document, "result_limit"),
            "result_limit"
        );
        std::optional<std::uint32_t> scalar_seed;
        if(find_optional(document, "seed") != nullptr) {
            const auto seed = require_positive_integer(document, "seed");
            if(seed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("config field 'seed' exceeds uint32_t");
            }
            scalar_seed = static_cast<std::uint32_t>(seed);
            config.seed = *scalar_seed;
            config.seeds = {*scalar_seed};
        } else if(!binary_rerank_grid) {
            throw std::runtime_error("config field 'seed' must be an integer");
        }
        if(binary_rerank_grid) {
            auto seeds = read_optional_uint32_list(document, "seeds");
            if(!seeds.empty()) {
                config.seeds = std::move(seeds);
                config.seed = config.seeds.front();
            }
            config.data_seeds = read_optional_uint32_list(document, "data_seeds");
            config.encoder_seeds = read_optional_uint32_list(document, "encoder_seeds");
            if(config.data_seeds.empty()) {
                config.data_seeds = config.seeds;
            }
            if(config.encoder_seeds.empty()) {
                config.encoder_seeds = config.seeds;
            }
            if(config.data_seeds.empty() && !config.encoder_seeds.empty()) {
                config.data_seeds = config.encoder_seeds;
            }
            if(config.encoder_seeds.empty() && !config.data_seeds.empty()) {
                config.encoder_seeds = config.data_seeds;
            }
            if(config.data_seeds.empty() || config.encoder_seeds.empty()) {
                throw std::runtime_error(
                    "synthetic_binary_rerank_grid requires 'seed', 'seeds', "
                    "'data_seeds', or 'encoder_seeds'"
                );
            }
            if(!config.data_seeds.empty()) {
                config.seed = config.data_seeds.front();
            } else if(scalar_seed) {
                config.seed = *scalar_seed;
            }
            config.repeat_count = optional_positive_size(
                document,
                "repeat_count",
                config.repeat_count
            );
            config.exact_timing_repeat_count = optional_positive_size(
                document,
                "exact_timing_repeat_count",
                config.repeat_count
            );
            config.randomize_execution_order = optional_bool(
                document,
                "randomize_execution_order",
                false
            );
            config.encoder_families = read_optional_string_list(
                document,
                "encoder_families"
            );
            if(config.encoder_families.empty()) {
                config.encoder_families = {std::string{kEncoderFamilyRandomHyperplane}};
            }
            validate_encoder_families(config);
        }
        if(binary_flat_vs_float || binary_rerank_grid) {
            if(config.rerank_candidate_limits.empty()) {
                config.rerank_candidate_limits = default_rerank_candidate_limits(
                    config.result_limit,
                    config.document_count
                );
            }
            validate_rerank_candidate_limits(config);
        }
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

    /// Benchmark-local exact baseline with contiguous storage and no metadata layer.
    class ContiguousDenseExactBaseline final {
    public:
        ContiguousDenseExactBaseline(
            const std::vector<agent_memory::Embedding>& documents,
            std::size_t dimension
        )
            : m_dimension(dimension) {
            if(dimension != 0
               && documents.size()
                   > std::numeric_limits<std::size_t>::max() / dimension) {
                throw std::length_error(
                    "contiguous exact baseline payload size overflows size_t"
                );
            }
            m_values.reserve(documents.size() * dimension);
            m_inverse_norms.reserve(documents.size());
            for(const auto& document : documents) {
                if(document.dimension() != dimension) {
                    throw std::invalid_argument(
                        "contiguous exact baseline document dimension mismatch"
                    );
                }
                m_values.insert(
                    m_values.end(),
                    document.values.begin(),
                    document.values.end()
                );
                const auto squared_norm = m_similarity.squared_norm(document);
                m_inverse_norms.push_back(
                    squared_norm > 0.0F ? 1.0F / std::sqrt(squared_norm) : 0.0F
                );
            }
        }

        [[nodiscard]] agent_memory::VectorSimilarityBackend backend() const noexcept {
            return m_similarity.backend();
        }

        [[nodiscard]] std::vector<std::size_t> search(
            const agent_memory::Embedding& query,
            std::size_t limit
        ) {
            if(query.dimension() != m_dimension) {
                throw std::invalid_argument(
                    "contiguous exact baseline query dimension mismatch"
                );
            }
            if(limit == 0 || m_inverse_norms.empty()) {
                return {};
            }

            m_dot_products.resize(m_inverse_norms.size());
            m_similarity.dot_products(
                query.values.data(),
                m_values.data(),
                m_inverse_norms.size(),
                m_dimension,
                m_dot_products.data()
            );
            const auto query_squared_norm = m_similarity.squared_norm(query);
            const auto query_inverse_norm = query_squared_norm > 0.0F
                ? 1.0F / std::sqrt(query_squared_norm)
                : 0.0F;

            m_scored.resize(m_inverse_norms.size());
            for(std::size_t index = 0; index < m_scored.size(); ++index) {
                m_scored[index] = ScoredDocument{
                    index,
                    m_dot_products[index] * query_inverse_norm * m_inverse_norms[index]
                };
            }
            const auto better = [](const ScoredDocument& lhs, const ScoredDocument& rhs) {
                if(lhs.score == rhs.score) {
                    return lhs.index < rhs.index;
                }
                return lhs.score > rhs.score;
            };
            const auto result_count = std::min(limit, m_scored.size());
            if(m_scored.size() > result_count) {
                std::partial_sort(
                    m_scored.begin(),
                    m_scored.begin() + static_cast<std::ptrdiff_t>(result_count),
                    m_scored.end(),
                    better
                );
            } else {
                std::sort(m_scored.begin(), m_scored.end(), better);
            }

            std::vector<std::size_t> positions;
            positions.reserve(result_count);
            for(std::size_t index = 0; index < result_count; ++index) {
                positions.push_back(m_scored[index].index);
            }
            return positions;
        }

    private:
        std::size_t m_dimension = 0;
        agent_memory::VectorSimilarityComputer m_similarity;
        std::vector<float> m_values;
        std::vector<float> m_inverse_norms;
        std::vector<float> m_dot_products;
        std::vector<ScoredDocument> m_scored;
    };

    [[nodiscard]] std::optional<std::size_t> parse_prefixed_index(
        std::string_view text,
        std::string_view prefix
    ) noexcept {
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

    [[nodiscard]] std::vector<std::string> rerank_binary_candidates_exact(
        const agent_memory::Embedding& query,
        const std::vector<agent_memory::Embedding>& documents,
        const std::vector<agent_memory::BinarySignatureSearchResult>& candidates,
        std::size_t result_limit,
        const agent_memory::VectorSimilarityComputer& similarity
    ) {
        std::vector<ScoredDocument> scored;
        scored.reserve(candidates.size());
        for(const auto& candidate : candidates) {
            const auto index = parse_prefixed_index(candidate.chunk_id.value(), "doc:");
            if(!index || *index >= documents.size()) {
                throw std::runtime_error(
                    "binary rerank candidate chunk id is not a synthetic document id"
                );
            }
            scored.push_back(ScoredDocument{
                *index,
                similarity.dot_product(query, documents[*index])
            });
        }

        const auto compare = [](const ScoredDocument& lhs, const ScoredDocument& rhs) {
            if(lhs.score == rhs.score) {
                return lhs.index < rhs.index;
            }
            return lhs.score > rhs.score;
        };
        if(scored.size() > result_limit) {
            std::partial_sort(
                scored.begin(),
                scored.begin() + static_cast<std::ptrdiff_t>(result_limit),
                scored.end(),
                compare
            );
            scored.resize(result_limit);
        } else {
            std::sort(scored.begin(), scored.end(), compare);
        }

        std::vector<std::string> ids;
        ids.reserve(scored.size());
        for(const auto& item : scored) {
            ids.push_back("doc:" + std::to_string(item.index));
        }
        return ids;
    }

    [[nodiscard]] double recall_against_exact_top_k(
        const std::vector<std::string>& actual,
        const std::unordered_set<std::string>& exact_ids
    ) {
        if(exact_ids.empty()) {
            return 0.0;
        }
        std::size_t intersection_count = 0;
        for(const auto& id : actual) {
            if(exact_ids.find(id) != exact_ids.end()) {
                ++intersection_count;
            }
        }
        return static_cast<double>(intersection_count)
            / static_cast<double>(exact_ids.size());
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

    [[nodiscard]] std::uint64_t saturating_add_bytes(
        std::uint64_t lhs,
        std::uint64_t rhs
    ) noexcept {
        const auto max_value = std::numeric_limits<std::uint64_t>::max();
        if(lhs > max_value - rhs) {
            return max_value;
        }
        return lhs + rhs;
    }

    [[nodiscard]] std::uint64_t learned_projection_artifact_payload_bytes(
        std::size_t input_dimension,
        std::size_t bit_count
    ) {
        const auto rows = checked_payload_bytes(
            bit_count,
            input_dimension,
            sizeof(float)
        );
        const auto thresholds = checked_payload_bytes(bit_count, 1, sizeof(float));
        return saturating_add_bytes(rows, thresholds);
    }

    [[nodiscard]] std::uint64_t pca_projection_artifact_payload_bytes(
        std::size_t input_dimension,
        std::size_t bit_count
    ) {
        const auto mean = checked_payload_bytes(input_dimension, 1, sizeof(float));
        return saturating_add_bytes(
            mean,
            learned_projection_artifact_payload_bytes(input_dimension, bit_count)
        );
    }

    [[nodiscard]] nlohmann::json encoder_build_metrics_to_json(
        const BinaryEncoderBuildMetrics& metrics
    ) {
        return {
            {"encoder_training_ms", metrics.encoder_training_ms},
            {"training_vector_count", metrics.training_vector_count},
            {"artifact_payload_bytes", metrics.artifact_payload_bytes},
            {"training_source", metrics.training_source},
            {"training_included_in_query_timing", false},
            {"training_included_in_binary_build_timing", false}
        };
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

    [[nodiscard]] double mean_value(const std::vector<double>& values) noexcept {
        if(values.empty()) {
            return 0.0;
        }
        double sum = 0.0;
        for(const auto value : values) {
            sum += value;
        }
        return sum / static_cast<double>(values.size());
    }

    [[nodiscard]] double median_value(std::vector<double> values) {
        if(values.empty()) {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const auto midpoint = values.size() / 2;
        if((values.size() % 2) != 0) {
            return values[midpoint];
        }
        return (values[midpoint - 1] + values[midpoint]) / 2.0;
    }

    [[nodiscard]] double percentile_nearest_rank(
        std::vector<double> values,
        double percentile
    ) {
        if(values.empty()) {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        if(percentile <= 0.0) {
            return values.front();
        }
        if(percentile >= 1.0) {
            return values.back();
        }
        const auto rank = static_cast<std::size_t>(
            std::ceil(percentile * static_cast<double>(values.size()))
        );
        return values[std::min(rank == 0 ? 0 : rank - 1, values.size() - 1)];
    }

    [[nodiscard]] nlohmann::json samples_to_stats_json(
        const std::vector<double>& samples
    ) {
        if(samples.empty()) {
            return {
                {"sample_count", 0},
                {"mean", 0.0},
                {"median", 0.0},
                {"p95_nearest_rank", 0.0},
                {"min", 0.0},
                {"max", 0.0}
            };
        }
        const auto [min_it, max_it] = std::minmax_element(
            samples.begin(),
            samples.end()
        );
        return {
            {"sample_count", samples.size()},
            {"mean", mean_value(samples)},
            {"median", median_value(samples)},
            {"p95_nearest_rank", percentile_nearest_rank(samples, 0.95)},
            {"min", *min_it},
            {"max", *max_it}
        };
    }

    void sort_rerank_candidates_by_limit(BinaryFlatVsFloatResult& result) {
        std::sort(
            result.rerank_candidates.begin(),
            result.rerank_candidates.end(),
            [](const BinaryRerankCandidateResult& lhs,
               const BinaryRerankCandidateResult& rhs) {
                return lhs.candidate_limit < rhs.candidate_limit;
            }
        );
    }

    [[nodiscard]] const BinaryRerankCandidateResult& require_rerank_candidate(
        const BinaryFlatVsFloatResult& result,
        std::size_t candidate_limit
    ) {
        const auto it = std::find_if(
            result.rerank_candidates.begin(),
            result.rerank_candidates.end(),
            [candidate_limit](const BinaryRerankCandidateResult& candidate) {
                return candidate.candidate_limit == candidate_limit;
            }
        );
        if(it == result.rerank_candidates.end()) {
            throw std::runtime_error(
                "binary grid repeat is missing a candidate_limit result"
            );
        }
        return *it;
    }

    void require_repeat_quality_consistency(
        const BenchmarkConfig& config,
        const std::vector<BinaryFlatVsFloatResult>& repeats
    ) {
        if(repeats.empty()) {
            throw std::runtime_error("binary grid summary requires at least one repeat");
        }
        const auto& expected = repeats.front();
        for(std::size_t repeat_index = 1; repeat_index < repeats.size(); ++repeat_index) {
            const auto& actual = repeats[repeat_index];
            if(actual.mean_recall_at_k_vs_exact != expected.mean_recall_at_k_vs_exact
               || actual.top1_agreement != expected.top1_agreement) {
                throw std::runtime_error(
                    "binary grid quality changed between deterministic timing repeats"
                );
            }
            for(const auto candidate_limit : config.rerank_candidate_limits) {
                const auto& expected_candidate = require_rerank_candidate(
                    expected,
                    candidate_limit
                );
                const auto& actual_candidate = require_rerank_candidate(
                    actual,
                    candidate_limit
                );
                if(actual_candidate.exact_top_k_candidate_coverage
                        != expected_candidate.exact_top_k_candidate_coverage
                   || actual_candidate.reranked_recall_at_k_vs_exact
                        != expected_candidate.reranked_recall_at_k_vs_exact
                   || actual_candidate.reranked_top1_agreement
                        != expected_candidate.reranked_top1_agreement) {
                    throw std::runtime_error(
                        "binary rerank quality changed between deterministic timing repeats"
                    );
                }
            }
        }
    }

    [[nodiscard]] nlohmann::json summarize_binary_grid_repeats(
        const BenchmarkConfig& config,
        const std::vector<BinaryFlatVsFloatResult>& repeats
    ) {
        require_repeat_quality_consistency(config, repeats);
        std::vector<double> direct_recall;
        std::vector<double> top1;
        std::vector<double> binary_total_ms;
        std::vector<double> binary_search_ms;
        std::vector<double> direct_current_exact_speedup;
        std::vector<double> direct_contiguous_exact_speedup;
        direct_recall.reserve(1);
        top1.reserve(1);
        binary_total_ms.reserve(repeats.size());
        binary_search_ms.reserve(repeats.size());
        direct_current_exact_speedup.reserve(repeats.size());
        direct_contiguous_exact_speedup.reserve(repeats.size());

        direct_recall.push_back(repeats.front().mean_recall_at_k_vs_exact);
        top1.push_back(repeats.front().top1_agreement);
        for(const auto& result : repeats) {
            binary_total_ms.push_back(result.binary_query.total_ms);
            binary_search_ms.push_back(result.binary_query.search_ms);
            direct_current_exact_speedup.push_back(
                result.binary_query.total_ms <= 0.0
                    ? 0.0
                    : result.current_exact_query.total_ms / result.binary_query.total_ms
            );
            direct_contiguous_exact_speedup.push_back(
                result.binary_query.total_ms <= 0.0
                    ? 0.0
                    : result.contiguous_exact_query.total_ms
                        / result.binary_query.total_ms
            );
        }

        nlohmann::json summary;
        summary["repeat_count"] = repeats.size();
        summary["quality_sample_count"] = 1;
        summary["timing_sample_count"] = repeats.size();
        summary["quality"] = {
            {"mean_recall_at_k_vs_exact", samples_to_stats_json(direct_recall)},
            {"top1_agreement", samples_to_stats_json(top1)}
        };
        summary["speed"] = {
            {"binary_query_search_ms", samples_to_stats_json(binary_search_ms)},
            {"binary_query_total_ms", samples_to_stats_json(binary_total_ms)},
            {"binary_total_speedup_vs_current_exact_index_including_encode",
                samples_to_stats_json(direct_current_exact_speedup)},
            {"binary_total_speedup_vs_contiguous_exact_including_encode",
                samples_to_stats_json(direct_contiguous_exact_speedup)}
        };
        summary["encoder_training"] =
            encoder_build_metrics_to_json(repeats.front().encoder_build);

        summary["rerank"] = nlohmann::json::array();
        for(const auto candidate_limit : config.rerank_candidate_limits) {
            std::vector<double> coverage;
            std::vector<double> reranked_recall;
            std::vector<double> reranked_top1;
            std::vector<double> total_ms;
            std::vector<double> current_exact_speedup;
            std::vector<double> contiguous_exact_speedup;

            const auto& quality_candidate = require_rerank_candidate(
                repeats.front(),
                candidate_limit
            );
            coverage.push_back(quality_candidate.exact_top_k_candidate_coverage);
            reranked_recall.push_back(quality_candidate.reranked_recall_at_k_vs_exact);
            reranked_top1.push_back(quality_candidate.reranked_top1_agreement);
            for(const auto& result : repeats) {
                const auto& candidate = require_rerank_candidate(result, candidate_limit);
                total_ms.push_back(candidate.total_ms);
                current_exact_speedup.push_back(
                    candidate.total_ms <= 0.0
                        ? 0.0
                        : result.current_exact_query.total_ms / candidate.total_ms
                );
                contiguous_exact_speedup.push_back(
                    candidate.total_ms <= 0.0
                        ? 0.0
                        : result.contiguous_exact_query.total_ms / candidate.total_ms
                );
            }

            summary["rerank"].push_back({
                {"candidate_limit", candidate_limit},
                {"exact_top_k_candidate_coverage", samples_to_stats_json(coverage)},
                {"reranked_recall_at_k_vs_exact", samples_to_stats_json(reranked_recall)},
                {"reranked_top1_agreement", samples_to_stats_json(reranked_top1)},
                {"total_ms", samples_to_stats_json(total_ms)},
                {"total_speedup_vs_current_exact_index",
                    samples_to_stats_json(current_exact_speedup)},
                {"total_speedup_vs_contiguous_exact",
                    samples_to_stats_json(contiguous_exact_speedup)}
            });
        }
        return summary;
    }

    [[nodiscard]] nlohmann::json aggregate_binary_grid_summary(
        const BenchmarkConfig& config,
        const std::vector<nlohmann::json>& seed_runs
    ) {
        nlohmann::json aggregate = nlohmann::json::array();
        for(const auto& encoder_family : config.encoder_families) {
            for(const auto bit_count : config.bit_counts) {
                std::vector<double> direct_recall;
                std::vector<double> top1;
                std::vector<double> binary_total_ms;
                std::vector<double> direct_current_exact_speedup;
                std::vector<double> direct_contiguous_exact_speedup;
                std::vector<double> encoder_training_ms;
                std::vector<double> training_vector_count;
                std::vector<double> artifact_payload_bytes;
                std::vector<std::vector<double>> coverage_by_candidate(
                    config.rerank_candidate_limits.size()
                );
                std::vector<std::vector<double>> reranked_recall_by_candidate(
                    config.rerank_candidate_limits.size()
                );
                std::vector<std::vector<double>> reranked_top1_by_candidate(
                    config.rerank_candidate_limits.size()
                );
                std::vector<std::vector<double>> total_ms_by_candidate(
                    config.rerank_candidate_limits.size()
                );
                std::vector<std::vector<double>> current_exact_speedup_by_candidate(
                    config.rerank_candidate_limits.size()
                );
                std::vector<std::vector<double>> contiguous_exact_speedup_by_candidate(
                    config.rerank_candidate_limits.size()
                );

                for(const auto& seed_run : seed_runs) {
                    if(seed_run.at("encoder_family").get<std::string>()
                       != encoder_family) {
                        continue;
                    }
                    for(const auto& bit_report : seed_run.at("reports")) {
                        if(bit_report.at("bit_count").get<std::size_t>() != bit_count) {
                            continue;
                        }
                        const auto& repeats = bit_report.at("repeats");
                        if(repeats.empty()) {
                            throw std::runtime_error(
                                "binary grid aggregate found an empty repeat collection"
                            );
                        }
                        const auto& quality_report = repeats.front();
                        direct_recall.push_back(
                            quality_report.at("quality")
                                .at("mean_recall_at_k_vs_exact")
                                .get<double>()
                        );
                        top1.push_back(
                            quality_report.at("quality").at("top1_agreement")
                                .get<double>()
                        );
                        const auto& encoder_training =
                            bit_report.at("summary").at("encoder_training");
                        encoder_training_ms.push_back(
                            encoder_training.at("encoder_training_ms").get<double>()
                        );
                        training_vector_count.push_back(
                            static_cast<double>(
                                encoder_training.at("training_vector_count")
                                    .get<std::size_t>()
                            )
                        );
                        artifact_payload_bytes.push_back(
                            static_cast<double>(
                                encoder_training.at("artifact_payload_bytes")
                                    .get<std::uint64_t>()
                            )
                        );
                        for(const auto& rerank : quality_report.at("rerank")) {
                            const auto candidate_limit =
                                rerank.at("candidate_limit").get<std::size_t>();
                            const auto limit_it = std::find(
                                config.rerank_candidate_limits.begin(),
                                config.rerank_candidate_limits.end(),
                                candidate_limit
                            );
                            if(limit_it == config.rerank_candidate_limits.end()) {
                                throw std::runtime_error(
                                    "binary grid aggregate found unknown candidate_limit"
                                );
                            }
                            const auto candidate_index = static_cast<std::size_t>(
                                std::distance(
                                    config.rerank_candidate_limits.begin(),
                                    limit_it
                                )
                            );
                            coverage_by_candidate[candidate_index].push_back(
                                rerank.at("exact_top_k_candidate_coverage").get<double>()
                            );
                            reranked_recall_by_candidate[candidate_index].push_back(
                                rerank.at("reranked_recall_at_k_vs_exact").get<double>()
                            );
                            reranked_top1_by_candidate[candidate_index].push_back(
                                rerank.at("reranked_top1_agreement").get<double>()
                            );
                        }

                        for(const auto& repeat_report : repeats) {
                            binary_total_ms.push_back(
                                repeat_report.at("speed")
                                    .at("binary_query_total_ms")
                                    .get<double>()
                            );
                            direct_current_exact_speedup.push_back(
                                repeat_report.at("speed")
                                    .at(
                                        "binary_total_speedup_vs_current_exact_index_including_encode"
                                    )
                                    .get<double>()
                            );
                            direct_contiguous_exact_speedup.push_back(
                                repeat_report.at("speed")
                                    .at(
                                        "binary_total_speedup_vs_contiguous_exact_including_encode"
                                    )
                                    .get<double>()
                            );

                            for(const auto& rerank : repeat_report.at("rerank")) {
                                const auto candidate_limit =
                                    rerank.at("candidate_limit").get<std::size_t>();
                                const auto limit_it = std::find(
                                    config.rerank_candidate_limits.begin(),
                                    config.rerank_candidate_limits.end(),
                                    candidate_limit
                                );
                                if(limit_it == config.rerank_candidate_limits.end()) {
                                    throw std::runtime_error(
                                        "binary grid aggregate found unknown candidate_limit"
                                    );
                                }
                                const auto candidate_index = static_cast<std::size_t>(
                                    std::distance(
                                        config.rerank_candidate_limits.begin(),
                                        limit_it
                                    )
                                );
                                total_ms_by_candidate[candidate_index].push_back(
                                    rerank.at("total_ms").get<double>()
                                );
                                current_exact_speedup_by_candidate[candidate_index]
                                    .push_back(
                                        rerank.at("total_speedup_vs_current_exact_index")
                                            .get<double>()
                                    );
                                contiguous_exact_speedup_by_candidate[candidate_index]
                                    .push_back(
                                        rerank.at("total_speedup_vs_contiguous_exact")
                                            .get<double>()
                                    );
                            }
                        }
                    }
                }

                if(direct_recall.empty()) {
                    continue;
                }

                nlohmann::json bit_summary;
                bit_summary["encoder_family"] = encoder_family;
                bit_summary["bit_count"] = bit_count;
                bit_summary["quality_sample_count"] = direct_recall.size();
                bit_summary["timing_sample_count"] = binary_total_ms.size();
                bit_summary["quality"] = {
                    {"mean_recall_at_k_vs_exact", samples_to_stats_json(direct_recall)},
                    {"top1_agreement", samples_to_stats_json(top1)}
                };
                bit_summary["speed"] = {
                    {"binary_query_total_ms", samples_to_stats_json(binary_total_ms)},
                    {"binary_total_speedup_vs_current_exact_index_including_encode",
                        samples_to_stats_json(direct_current_exact_speedup)},
                    {"binary_total_speedup_vs_contiguous_exact_including_encode",
                        samples_to_stats_json(direct_contiguous_exact_speedup)}
                };
                bit_summary["encoder_training"] = {
                    {"encoder_training_ms", samples_to_stats_json(encoder_training_ms)},
                    {"training_vector_count",
                        samples_to_stats_json(training_vector_count)},
                    {"artifact_payload_bytes",
                        samples_to_stats_json(artifact_payload_bytes)}
                };
                bit_summary["rerank"] = nlohmann::json::array();
                for(std::size_t index = 0; index < config.rerank_candidate_limits.size();
                    ++index) {
                    bit_summary["rerank"].push_back({
                        {"candidate_limit", config.rerank_candidate_limits[index]},
                        {"exact_top_k_candidate_coverage",
                            samples_to_stats_json(coverage_by_candidate[index])},
                        {"reranked_recall_at_k_vs_exact",
                            samples_to_stats_json(reranked_recall_by_candidate[index])},
                        {"reranked_top1_agreement",
                            samples_to_stats_json(reranked_top1_by_candidate[index])},
                        {"total_ms", samples_to_stats_json(total_ms_by_candidate[index])},
                        {"total_speedup_vs_current_exact_index",
                            samples_to_stats_json(
                                current_exact_speedup_by_candidate[index]
                            )},
                        {"total_speedup_vs_contiguous_exact",
                            samples_to_stats_json(
                                contiguous_exact_speedup_by_candidate[index]
                            )}
                    });
                }
                aggregate.push_back(std::move(bit_summary));
            }
        }
        return aggregate;
    }

    [[nodiscard]] nlohmann::json binary_flat_vs_float_report_to_json(
        const BenchmarkConfig& config,
        const agent_memory::BinarySignatureEncoderInfo& encoder_info,
        const BinaryFlatVsFloatResult& result
    );

    void write_binary_flat_vs_float_json_report(
        const fs::path& path,
        const BenchmarkConfig& config,
        const agent_memory::BinarySignatureEncoderInfo& encoder_info,
        const BinaryFlatVsFloatResult& result
    ) {
        write_text_file(
            path,
            binary_flat_vs_float_report_to_json(config, encoder_info, result).dump(2) + "\n"
        );
    }

    [[nodiscard]] nlohmann::json binary_flat_vs_float_report_to_json(
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
            {"config_fingerprint", encoder_info.config_fingerprint},
            {"training", encoder_build_metrics_to_json(result.encoder_build)}
        };
        document["quality"] = {
            {"reference_baseline", "exact_cosine_top_k"},
            {"candidate_baseline", "flat_binary_hamming"},
            {"mean_recall_at_k_vs_exact", result.mean_recall_at_k_vs_exact},
            {"top1_agreement", result.top1_agreement}
        };
        document["speed"] = {
            {"data_generation_ms", result.data_generation_ms},
            {"current_exact_index_build_ms", result.current_exact_build_ms},
            {"contiguous_exact_build_ms", result.contiguous_exact_build_ms},
            {"current_exact_index_similarity_backend",
                result.current_exact_similarity_backend},
            {"contiguous_exact_similarity_backend",
                result.contiguous_exact_similarity_backend},
            {"binary_hamming_backend", result.binary_hamming_backend},
            {"binary_encoder_similarity_backend", result.binary_encoder_similarity_backend},
            {"encoder_training_ms", result.encoder_build.encoder_training_ms},
            {"binary_encode_and_build_ms", result.binary_build_ms},
            {"current_exact_index_query_total_ms",
                result.current_exact_query.total_ms},
            {"current_exact_index_query_timing_repeat_count",
                result.current_exact_query_total_ms_samples.size()},
            {"current_exact_index_query_total_ms_stats",
                samples_to_stats_json(result.current_exact_query_total_ms_samples)},
            {"contiguous_exact_query_total_ms",
                result.contiguous_exact_query.total_ms},
            {"contiguous_exact_query_timing_repeat_count",
                result.contiguous_exact_query_total_ms_samples.size()},
            {"contiguous_exact_query_total_ms_stats",
                samples_to_stats_json(result.contiguous_exact_query_total_ms_samples)},
            {"speedup_denominators", {
                {"current_exact_index", "median_current_exact_index_query_total_ms"},
                {"contiguous_exact", "median_contiguous_exact_query_total_ms"}
            }},
            {"current_exact_index_mean_query_ms", per_query_ms(
                result.current_exact_query.total_ms,
                config.query_count
            )},
            {"current_exact_index_queries_per_second", queries_per_second(
                result.current_exact_query.total_ms,
                config.query_count
            )},
            {"contiguous_exact_mean_query_ms", per_query_ms(
                result.contiguous_exact_query.total_ms,
                config.query_count
            )},
            {"contiguous_exact_queries_per_second", queries_per_second(
                result.contiguous_exact_query.total_ms,
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
            {"binary_search_speedup_vs_current_exact_index",
                result.binary_query.search_ms <= 0.0
                ? 0.0
                : result.current_exact_query.search_ms / result.binary_query.search_ms},
            {"binary_search_speedup_vs_contiguous_exact",
                result.binary_query.search_ms <= 0.0
                ? 0.0
                : result.contiguous_exact_query.search_ms
                    / result.binary_query.search_ms},
            {"binary_total_speedup_vs_current_exact_index_including_encode",
                result.binary_query.total_ms <= 0.0
                ? 0.0
                : result.current_exact_query.total_ms / result.binary_query.total_ms},
            {"binary_total_speedup_vs_contiguous_exact_including_encode",
                result.binary_query.total_ms <= 0.0
                ? 0.0
                : result.contiguous_exact_query.total_ms
                    / result.binary_query.total_ms}
        };
        document["rerank"] = nlohmann::json::array();
        for(const auto& candidate : result.rerank_candidates) {
            document["rerank"].push_back({
                {"candidate_limit", candidate.candidate_limit},
                {"exact_top_k_candidate_coverage",
                    candidate.exact_top_k_candidate_coverage},
                {"reranked_recall_at_k_vs_exact",
                    candidate.reranked_recall_at_k_vs_exact},
                {"reranked_top1_agreement", candidate.reranked_top1_agreement},
                {"binary_candidate_encode_ms",
                    candidate.binary_candidate_query.encode_ms},
                {"binary_candidate_search_ms",
                    candidate.binary_candidate_query.search_ms},
                {"exact_rerank_ms", candidate.exact_rerank_ms},
                {"total_ms", candidate.total_ms},
                {"mean_query_ms", per_query_ms(candidate.total_ms, config.query_count)},
                {"queries_per_second", queries_per_second(
                    candidate.total_ms,
                    config.query_count
                )},
                {"total_speedup_vs_current_exact_index",
                    candidate.total_ms <= 0.0
                    ? 0.0
                    : result.current_exact_query.total_ms / candidate.total_ms},
                {"total_speedup_vs_contiguous_exact",
                    candidate.total_ms <= 0.0
                    ? 0.0
                    : result.contiguous_exact_query.total_ms / candidate.total_ms}
            });
        }
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
            "Synthetic clustered dense vectors; both exact implementations must return identical top-k.",
            "Timing is local and in-process; samples remain directional, not production-stable.",
            "Current ExactVectorIndex and contiguous exact denominators use separate median timing samples.",
            "Current-index and contiguous-baseline speedups are reported separately.",
            "Exact search timing excludes quality-bookkeeping ID extraction and comparison.",
            "Binary total timing is query signature encoding plus exact flat Hamming scan.",
            "encoder_training_ms is reported as cold-start artifact training cost and is excluded from query/build timing.",
            "Rerank rows measure binary candidate over-fetch followed by exact float rerank.",
            "Process peak RSS is a whole benchmark-process high-water mark, not per-index memory."
        });
        return document;
    }

    [[nodiscard]] BinaryBenchmarkOracle prepare_binary_benchmark_oracle(
        const BenchmarkConfig& config
    ) {
        BinaryBenchmarkOracle oracle;

        const auto data_start = Clock::now();
        oracle.data = make_clustered_dense_data(config);
        const auto data_end = Clock::now();
        oracle.data_generation_ms = elapsed_ms(data_start, data_end);

        agent_memory::ExactVectorIndex exact_index(agent_memory::ExactVectorIndexOptions{
            config.embedding_dimensions,
            agent_memory::SimilarityMetric::Cosine
        });
        oracle.current_exact_similarity_backend = std::string{
            agent_memory::vector_similarity_backend_name(
                exact_index.similarity_backend()
            )
        };

        const auto current_exact_build_start = Clock::now();
        for(std::size_t index = 0; index < oracle.data.documents.size(); ++index) {
            exact_index.upsert(agent_memory::VectorRecord{
                make_chunk_id(index),
                oracle.data.documents[index],
                {}
            });
        }
        const auto current_exact_build_end = Clock::now();
        oracle.current_exact_build_ms = elapsed_ms(
            current_exact_build_start,
            current_exact_build_end
        );

        const auto contiguous_exact_build_start = Clock::now();
        ContiguousDenseExactBaseline contiguous_exact(
            oracle.data.documents,
            config.embedding_dimensions
        );
        const auto contiguous_exact_build_end = Clock::now();
        oracle.contiguous_exact_build_ms = elapsed_ms(
            contiguous_exact_build_start,
            contiguous_exact_build_end
        );
        oracle.contiguous_exact_similarity_backend = std::string{
            agent_memory::vector_similarity_backend_name(contiguous_exact.backend())
        };

        oracle.exact_payload_bytes = checked_payload_bytes(
            config.document_count,
            config.embedding_dimensions,
            sizeof(float)
        );

        oracle.exact_top_k.reserve(oracle.data.queries.size());
        oracle.current_exact_query_total_ms_samples.reserve(
            config.exact_timing_repeat_count
        );
        oracle.contiguous_exact_query_total_ms_samples.reserve(
            config.exact_timing_repeat_count
        );
        for(std::size_t timing_repeat = 0;
            timing_repeat < config.exact_timing_repeat_count;
            ++timing_repeat) {
            const auto measure_current_exact = [&] {
                double search_ms = 0.0;
                for(const auto& query_embedding : oracle.data.queries) {
                    const auto search_start = Clock::now();
                    const auto hits = exact_index.search(agent_memory::VectorSearchQuery{
                        query_embedding,
                        config.result_limit,
                        {}
                    });
                    const auto search_end = Clock::now();
                    search_ms += elapsed_ms(search_start, search_end);

                    if(timing_repeat == 0) {
                        std::vector<std::string> ids;
                        ids.reserve(hits.size());
                        for(const auto& hit : hits) {
                            ids.push_back(hit.chunk_id.value());
                        }
                        oracle.exact_top_k.push_back(std::move(ids));
                    }
                }
                oracle.current_exact_query_total_ms_samples.push_back(search_ms);
            };
            const auto measure_contiguous_exact = [&] {
                double search_ms = 0.0;
                std::size_t query_index = 0;
                for(const auto& query_embedding : oracle.data.queries) {
                    const auto search_start = Clock::now();
                    const auto positions = contiguous_exact.search(
                        query_embedding,
                        config.result_limit
                    );
                    const auto search_end = Clock::now();
                    search_ms += elapsed_ms(search_start, search_end);

                    if(timing_repeat == 0) {
                        std::vector<std::string> ids;
                        ids.reserve(positions.size());
                        for(const auto position : positions) {
                            ids.push_back("doc:" + std::to_string(position));
                        }
                        if(ids != oracle.exact_top_k.at(query_index)) {
                            throw std::runtime_error(
                                "contiguous exact baseline disagrees with ExactVectorIndex"
                            );
                        }
                    }
                    ++query_index;
                }
                oracle.contiguous_exact_query_total_ms_samples.push_back(search_ms);
            };

            if((timing_repeat % 2U) == 0U) {
                measure_current_exact();
                measure_contiguous_exact();
            } else {
                measure_contiguous_exact();
                measure_current_exact();
            }
        }
        oracle.current_exact_query.search_ms = median_value(
            oracle.current_exact_query_total_ms_samples
        );
        oracle.current_exact_query.total_ms = oracle.current_exact_query.search_ms;
        oracle.contiguous_exact_query.search_ms = median_value(
            oracle.contiguous_exact_query_total_ms_samples
        );
        oracle.contiguous_exact_query.total_ms = oracle.contiguous_exact_query.search_ms;

        oracle.exact_top_k_sets.reserve(oracle.exact_top_k.size());
        for(const auto& ids : oracle.exact_top_k) {
            oracle.exact_top_k_sets.emplace_back(ids.begin(), ids.end());
        }
        return oracle;
    }

    [[nodiscard]] BinaryEncoderBuild make_binary_encoder(
        const std::string& family,
        std::size_t input_dimension,
        std::size_t bit_count,
        std::uint64_t seed,
        const std::vector<agent_memory::Embedding>& training_vectors
    ) {
        if(family == kEncoderFamilyRandomHyperplane) {
            agent_memory::RandomHyperplaneBinaryEncoderOptions options;
            options.input_dimension = input_dimension;
            options.bit_count = bit_count;
            options.seed = seed;
            BinaryEncoderBuild build;
            build.encoder =
                std::make_unique<agent_memory::RandomHyperplaneBinaryEncoder>(
                    options
                );
            return build;
        }
        if(family == kEncoderFamilyCoordinateSign) {
            if(bit_count != input_dimension) {
                throw std::runtime_error(
                    "coordinate_sign encoder supports only bit_count == embedding_dimensions"
                );
            }
            BinaryEncoderBuild build;
            build.encoder =
                std::make_unique<agent_memory::CoordinateSignBinaryEncoder>(
                    agent_memory::CoordinateSignBinaryEncoderOptions{input_dimension}
                );
            return build;
        }
        if(family == kEncoderFamilyRandomizedHadamard) {
            agent_memory::RandomizedHadamardBinaryEncoderOptions options;
            options.input_dimension = input_dimension;
            options.bit_count = bit_count;
            options.seed = seed;
            BinaryEncoderBuild build;
            build.encoder =
                std::make_unique<agent_memory::RandomizedHadamardBinaryEncoder>(
                    options
                );
            return build;
        }
        if(family == kEncoderFamilyLearnedPairDifference) {
            agent_memory::LearnedProjectionTrainingOptions options;
            options.input_dimension = input_dimension;
            options.bit_count = bit_count;
            options.seed = seed;
            const auto training_start = Clock::now();
            auto artifact = agent_memory::train_learned_projection_encoder(
                training_vectors,
                options
            );
            const auto training_end = Clock::now();
            BinaryEncoderBuild build;
            build.metrics.encoder_training_ms = elapsed_ms(
                training_start,
                training_end
            );
            build.metrics.training_vector_count = artifact.training_vector_count;
            build.metrics.artifact_payload_bytes =
                learned_projection_artifact_payload_bytes(input_dimension, bit_count);
            build.metrics.training_source = "document_vectors";
            build.encoder =
                std::make_unique<agent_memory::LearnedProjectionBinaryEncoder>(
                    std::move(artifact)
                );
            return build;
        }
        if(family == kEncoderFamilyPcaProjection) {
            agent_memory::PcaProjectionTrainingOptions options;
            options.input_dimension = input_dimension;
            options.bit_count = bit_count;
            options.seed = seed;
            const auto training_start = Clock::now();
            auto artifact = agent_memory::train_pca_projection_encoder(
                training_vectors,
                options
            );
            const auto training_end = Clock::now();
            BinaryEncoderBuild build;
            build.metrics.encoder_training_ms = elapsed_ms(
                training_start,
                training_end
            );
            build.metrics.training_vector_count = artifact.training_vector_count;
            build.metrics.artifact_payload_bytes =
                pca_projection_artifact_payload_bytes(input_dimension, bit_count);
            build.metrics.training_source = "document_vectors";
            build.encoder = std::make_unique<agent_memory::PcaProjectionBinaryEncoder>(
                std::move(artifact)
            );
            return build;
        }
        if(family == kEncoderFamilyItqRotation) {
            agent_memory::ItqRotationTrainingOptions options;
            options.input_dimension = input_dimension;
            options.bit_count = bit_count;
            options.seed = seed;
            const auto training_start = Clock::now();
            auto artifact = agent_memory::train_itq_rotation_encoder(
                training_vectors,
                options
            );
            const auto training_end = Clock::now();
            BinaryEncoderBuild build;
            build.metrics.encoder_training_ms = elapsed_ms(
                training_start,
                training_end
            );
            build.metrics.training_vector_count = artifact.training_vector_count;
            build.metrics.artifact_payload_bytes =
                pca_projection_artifact_payload_bytes(input_dimension, bit_count);
            build.metrics.training_source = "document_vectors";
            build.encoder = std::make_unique<agent_memory::ItqRotationBinaryEncoder>(
                std::move(artifact)
            );
            return build;
        }
        throw std::runtime_error("unsupported binary encoder family: " + family);
    }

    [[nodiscard]] std::string binary_encoder_compute_backend_name(
        const agent_memory::IBinarySignatureEncoder& encoder
    ) {
        if(const auto* random =
               dynamic_cast<const agent_memory::RandomHyperplaneBinaryEncoder*>(
                   &encoder
               )) {
            return std::string{
                agent_memory::vector_similarity_backend_name(
                    random->similarity_backend()
                )
            };
        }
        if(dynamic_cast<const agent_memory::CoordinateSignBinaryEncoder*>(&encoder)
           != nullptr) {
            return "coordinate_sign";
        }
        if(dynamic_cast<const agent_memory::RandomizedHadamardBinaryEncoder*>(
               &encoder
           )
           != nullptr) {
            return agent_memory::RandomizedHadamardBinaryEncoder::compute_backend_name();
        }
        if(const auto* learned =
               dynamic_cast<const agent_memory::LearnedProjectionBinaryEncoder*>(
                   &encoder
               )) {
            return std::string{
                agent_memory::vector_similarity_backend_name(
                    learned->similarity_backend()
                )
            };
        }
        if(const auto* pca =
               dynamic_cast<const agent_memory::PcaProjectionBinaryEncoder*>(
                   &encoder
               )) {
            return std::string{
                agent_memory::vector_similarity_backend_name(
                    pca->similarity_backend()
                )
            };
        }
        if(const auto* itq =
               dynamic_cast<const agent_memory::ItqRotationBinaryEncoder*>(
                   &encoder
               )) {
            return std::string{
                agent_memory::vector_similarity_backend_name(
                    itq->similarity_backend()
                )
            };
        }
        return "unknown";
    }

    [[nodiscard]] BinaryFlatVsFloatResult run_binary_flat_vs_float_benchmark_with_oracle(
        const BenchmarkConfig& config,
        const agent_memory::IBinarySignatureEncoder& encoder,
        const BinaryBenchmarkOracle& oracle,
        const BinaryEncoderBuildMetrics& encoder_build
    ) {
        const auto signature_info = agent_memory::make_binary_signature_info(
            encoder.info(),
            make_synthetic_model_info(config),
            "symmetric_dense_projection"
        );
        agent_memory::FlatBinarySignatureIndex binary_index(
            agent_memory::FlatBinarySignatureIndexOptions{signature_info}
        );

        const auto binary_build_start = Clock::now();
        const auto document_signatures = encoder.encode_batch(oracle.data.documents);
        for(std::size_t index = 0; index < oracle.data.documents.size(); ++index) {
            binary_index.upsert(agent_memory::BinarySignatureRecord{
                make_chunk_id(index),
                document_signatures[index],
                signature_info,
                {}
            });
        }
        const auto binary_build_end = Clock::now();

        BinaryFlatVsFloatResult result;
        result.data_generation_ms = oracle.data_generation_ms;
        result.current_exact_build_ms = oracle.current_exact_build_ms;
        result.contiguous_exact_build_ms = oracle.contiguous_exact_build_ms;
        result.binary_build_ms = elapsed_ms(binary_build_start, binary_build_end);
        result.current_exact_similarity_backend =
            oracle.current_exact_similarity_backend;
        result.contiguous_exact_similarity_backend =
            oracle.contiguous_exact_similarity_backend;
        result.binary_hamming_backend = std::string(
            agent_memory::hamming_distance_backend_name(*binary_index.hamming_backend())
        );
        result.binary_encoder_similarity_backend =
            binary_encoder_compute_backend_name(encoder);
        result.encoder_build = encoder_build;
        result.current_exact_query = oracle.current_exact_query;
        result.current_exact_query_total_ms_samples =
            oracle.current_exact_query_total_ms_samples;
        result.contiguous_exact_query = oracle.contiguous_exact_query;
        result.contiguous_exact_query_total_ms_samples =
            oracle.contiguous_exact_query_total_ms_samples;
        result.exact_payload_bytes = oracle.exact_payload_bytes;
        result.binary_payload_bytes = checked_payload_bytes(
            config.document_count,
            agent_memory::binary_signature_word_count(config.bit_count),
            sizeof(std::uint64_t)
        );

        double recall_sum = 0.0;
        std::size_t top1_match_count = 0;
        for(std::size_t query_index = 0; query_index < oracle.data.queries.size(); ++query_index) {
            const auto encode_start = Clock::now();
            const auto query_signature = encoder.encode(oracle.data.queries[query_index]);
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

            std::vector<std::string> binary_ids;
            binary_ids.reserve(hits.size());
            for(const auto& hit : hits) {
                binary_ids.push_back(hit.chunk_id.value());
            }
            recall_sum += recall_against_exact_top_k(
                binary_ids,
                oracle.exact_top_k_sets[query_index]
            );
            if(!oracle.exact_top_k[query_index].empty() && !hits.empty()
               && oracle.exact_top_k[query_index].front() == hits.front().chunk_id.value()) {
                ++top1_match_count;
            }
        }

        if(!oracle.data.queries.empty()) {
            result.mean_recall_at_k_vs_exact =
                recall_sum / static_cast<double>(oracle.data.queries.size());
            result.top1_agreement =
                static_cast<double>(top1_match_count)
                / static_cast<double>(oracle.data.queries.size());
        }

        const agent_memory::VectorSimilarityComputer rerank_similarity;
        result.rerank_candidates.reserve(config.rerank_candidate_limits.size());
        for(const auto candidate_limit : config.rerank_candidate_limits) {
            BinaryRerankCandidateResult candidate_result;
            candidate_result.candidate_limit = candidate_limit;

            double candidate_recall_sum = 0.0;
            double reranked_recall_sum = 0.0;
            std::size_t reranked_top1_match_count = 0;
            for(std::size_t query_index = 0; query_index < oracle.data.queries.size(); ++query_index) {
                const auto encode_start = Clock::now();
                const auto query_signature = encoder.encode(oracle.data.queries[query_index]);
                const auto encode_end = Clock::now();
                candidate_result.binary_candidate_query.encode_ms += elapsed_ms(
                    encode_start,
                    encode_end
                );

                const auto search_start = Clock::now();
                const auto candidates = binary_index.search(
                    agent_memory::BinarySignatureSearchQuery{
                        query_signature,
                        signature_info,
                        candidate_limit,
                        {}
                    }
                );
                const auto search_end = Clock::now();
                candidate_result.binary_candidate_query.search_ms += elapsed_ms(
                    search_start,
                    search_end
                );

                std::vector<std::string> candidate_ids;
                candidate_ids.reserve(candidates.size());
                for(const auto& candidate : candidates) {
                    candidate_ids.push_back(candidate.chunk_id.value());
                }
                candidate_recall_sum += recall_against_exact_top_k(
                    candidate_ids,
                    oracle.exact_top_k_sets[query_index]
                );

                const auto rerank_start = Clock::now();
                const auto reranked_ids = rerank_binary_candidates_exact(
                    oracle.data.queries[query_index],
                    oracle.data.documents,
                    candidates,
                    config.result_limit,
                    rerank_similarity
                );
                const auto rerank_end = Clock::now();
                candidate_result.exact_rerank_ms += elapsed_ms(
                    rerank_start,
                    rerank_end
                );

                reranked_recall_sum += recall_against_exact_top_k(
                    reranked_ids,
                    oracle.exact_top_k_sets[query_index]
                );
                if(!oracle.exact_top_k[query_index].empty() && !reranked_ids.empty()
                   && oracle.exact_top_k[query_index].front() == reranked_ids.front()) {
                    ++reranked_top1_match_count;
                }
            }

            candidate_result.binary_candidate_query.total_ms =
                candidate_result.binary_candidate_query.encode_ms
                + candidate_result.binary_candidate_query.search_ms;
            candidate_result.total_ms =
                candidate_result.binary_candidate_query.total_ms
                + candidate_result.exact_rerank_ms;
            if(!oracle.data.queries.empty()) {
                candidate_result.exact_top_k_candidate_coverage =
                    candidate_recall_sum / static_cast<double>(oracle.data.queries.size());
                candidate_result.reranked_recall_at_k_vs_exact =
                    reranked_recall_sum / static_cast<double>(oracle.data.queries.size());
                candidate_result.reranked_top1_agreement =
                    static_cast<double>(reranked_top1_match_count)
                    / static_cast<double>(oracle.data.queries.size());
            }
            result.rerank_candidates.push_back(candidate_result);
        }

        result.process_peak_resident_set_bytes = peak_resident_set_bytes();
        return result;
    }

    [[nodiscard]] BinaryFlatVsFloatResult run_binary_flat_vs_float_benchmark(
        const BenchmarkConfig& config,
        const agent_memory::IBinarySignatureEncoder& encoder,
        const BinaryEncoderBuildMetrics& encoder_build
    ) {
        const auto oracle = prepare_binary_benchmark_oracle(config);
        return run_binary_flat_vs_float_benchmark_with_oracle(
            config,
            encoder,
            oracle,
            encoder_build
        );
    }

    [[nodiscard]] std::unordered_map<std::string, agent_memory::Embedding>
    embeddings_by_id(const std::vector<agent_memory::PrecomputedEmbeddingRecord>& records) {
        std::unordered_map<std::string, agent_memory::Embedding> result;
        result.reserve(records.size());
        for(const auto& record : records) {
            result.emplace(record.id, record.embedding);
        }
        return result;
    }

    [[nodiscard]] std::vector<agent_memory::Embedding> embedding_vectors(
        const std::vector<agent_memory::PrecomputedEmbeddingRecord>& records
    ) {
        std::vector<agent_memory::Embedding> result;
        result.reserve(records.size());
        for(const auto& record : records) {
            result.push_back(record.embedding);
        }
        return result;
    }

    [[nodiscard]] std::unordered_map<std::string, std::unordered_set<std::string>>
    positive_qrels_by_query(const agent_memory::RetrievalEvalDataset& dataset) {
        std::unordered_map<std::string, std::unordered_set<std::string>> result;
        for(const auto& judgment : dataset.judgments) {
            if(judgment.relevant()) {
                result[judgment.query_id].insert(judgment.item_id);
            }
        }
        return result;
    }

    [[nodiscard]] double qrels_candidate_relevant_coverage(
        const agent_memory::RetrievalEvalDataset& dataset,
        const std::unordered_map<std::string, std::unordered_set<std::string>>& qrels,
        const std::vector<std::unordered_set<std::string>>& candidate_sets
    ) {
        double coverage_sum = 0.0;
        std::size_t evaluated_count = 0;
        for(std::size_t index = 0; index < dataset.queries.size(); ++index) {
            const auto& query = dataset.queries[index];
            if(query.answer_mode != agent_memory::EvalQueryAnswerMode::JudgedRetrieval) {
                continue;
            }
            const auto qrel_it = qrels.find(query.id);
            if(qrel_it == qrels.end() || qrel_it->second.empty()) {
                continue;
            }
            std::size_t found = 0;
            for(const auto& item_id : qrel_it->second) {
                if(candidate_sets[index].find(item_id) != candidate_sets[index].end()) {
                    ++found;
                }
            }
            coverage_sum += static_cast<double>(found)
                / static_cast<double>(qrel_it->second.size());
            ++evaluated_count;
        }
        return evaluated_count == 0
            ? 0.0
            : coverage_sum / static_cast<double>(evaluated_count);
    }

    [[nodiscard]] nlohmann::json retrieval_metrics_to_json(
        const agent_memory::RetrievalMetrics& metrics
    ) {
        return {
            {"recall_at_1", agent_memory::metric_value_at(metrics.recall_at, 1).value_or(0.0)},
            {"recall_at_5", agent_memory::metric_value_at(metrics.recall_at, 5).value_or(0.0)},
            {"recall_at_10", agent_memory::metric_value_at(metrics.recall_at, 10).value_or(0.0)},
            {"recall_at_50", agent_memory::metric_value_at(metrics.recall_at, 50).value_or(0.0)},
            {"ndcg_at_10", agent_memory::metric_value_at(metrics.ndcg_at, 10).value_or(0.0)},
            {"mrr", metrics.mrr},
            {"empty_result_fraction", metrics.empty_result_fraction},
            {"evaluated_query_count", metrics.evaluated_query_count}
        };
    }

    [[nodiscard]] agent_memory::RetrievalRun make_retrieval_run(
        std::string name,
        const agent_memory::RetrievalEvalDataset& dataset,
        const std::vector<std::vector<agent_memory::RetrievalRunHit>>& hits_by_query
    ) {
        if(hits_by_query.size() != dataset.queries.size()) {
            throw std::runtime_error("precomputed run/query size mismatch");
        }
        agent_memory::RetrievalRun run;
        run.name = std::move(name);
        run.queries.reserve(dataset.queries.size());
        for(std::size_t index = 0; index < dataset.queries.size(); ++index) {
            agent_memory::RetrievalQueryRun query_run;
            query_run.query_id = dataset.queries[index].id;
            query_run.hits = hits_by_query[index];
            run.queries.push_back(std::move(query_run));
        }
        return run;
    }

    [[nodiscard]] PrecomputedExactOracle prepare_precomputed_exact_oracle(
        const BenchmarkConfig& config,
        const agent_memory::PrecomputedEmbeddingEvalDataset& dataset
    ) {
        const auto query_embeddings = embeddings_by_id(dataset.query_embeddings);

        agent_memory::ExactVectorIndex exact_index(agent_memory::ExactVectorIndexOptions{
            dataset.embedding_model.dimension,
            dataset.embedding_model.similarity_metric
        });
        PrecomputedExactOracle oracle;
        oracle.exact_similarity_backend = std::string{
            agent_memory::vector_similarity_backend_name(exact_index.similarity_backend())
        };

        const auto build_start = Clock::now();
        for(const auto& document : dataset.document_embeddings) {
            exact_index.upsert(agent_memory::VectorRecord{
                agent_memory::ChunkId{document.id},
                document.embedding,
                {}
            });
        }
        const auto build_end = Clock::now();
        oracle.exact_build_ms = elapsed_ms(build_start, build_end);

        std::vector<std::vector<agent_memory::RetrievalRunHit>> hits_by_query;
        hits_by_query.reserve(dataset.retrieval.queries.size());
        oracle.exact_top_k.reserve(dataset.retrieval.queries.size());
        const auto query_start = Clock::now();
        for(const auto& query : dataset.retrieval.queries) {
            const auto query_embedding = query_embeddings.at(query.id);
            const auto hits = exact_index.search(agent_memory::VectorSearchQuery{
                query_embedding,
                config.result_limit,
                {}
            });

            std::vector<std::string> ids;
            ids.reserve(hits.size());
            std::vector<agent_memory::RetrievalRunHit> run_hits;
            run_hits.reserve(hits.size());
            for(const auto& hit : hits) {
                ids.push_back(hit.chunk_id.value());
                run_hits.push_back(agent_memory::RetrievalRunHit{
                    hit.chunk_id.value(),
                    hit.score,
                    0,
                    "exact_vector"
                });
            }
            oracle.exact_top_k.push_back(std::move(ids));
            hits_by_query.push_back(std::move(run_hits));
        }
        const auto query_end = Clock::now();
        oracle.exact_query_ms = elapsed_ms(query_start, query_end);

        oracle.exact_top_k_sets.reserve(oracle.exact_top_k.size());
        for(const auto& ids : oracle.exact_top_k) {
            oracle.exact_top_k_sets.emplace_back(ids.begin(), ids.end());
        }
        const auto exact_run = make_retrieval_run(
            "precomputed_exact_vector",
            dataset.retrieval,
            hits_by_query
        );
        oracle.qrels_quality = agent_memory::evaluate_retrieval(
            dataset.retrieval,
            exact_run
        );
        oracle.exact_payload_bytes = checked_payload_bytes(
            dataset.document_embeddings.size(),
            dataset.embedding_model.dimension,
            sizeof(float)
        );
        return oracle;
    }

    [[nodiscard]] float score_embedding_pair(
        const agent_memory::Embedding& query,
        const agent_memory::Embedding& document,
        agent_memory::SimilarityMetric metric,
        const agent_memory::VectorSimilarityComputer& similarity
    ) {
        if(metric == agent_memory::SimilarityMetric::Euclidean) {
            return similarity.negative_squared_distance(query, document);
        }
        const auto dot = similarity.dot_product(query, document);
        if(metric == agent_memory::SimilarityMetric::DotProduct) {
            return dot;
        }
        const auto query_norm = similarity.squared_norm(query);
        const auto document_norm = similarity.squared_norm(document);
        if(query_norm <= 0.0F || document_norm <= 0.0F) {
            return 0.0F;
        }
        return dot / std::sqrt(query_norm * document_norm);
    }

    [[nodiscard]] std::vector<agent_memory::RetrievalRunHit> rerank_precomputed_candidates(
        const agent_memory::Embedding& query,
        const std::unordered_map<std::string, agent_memory::Embedding>& document_embeddings,
        const std::vector<agent_memory::BinarySignatureSearchResult>& candidates,
        std::size_t result_limit,
        agent_memory::SimilarityMetric metric,
        const agent_memory::VectorSimilarityComputer& similarity
    ) {
        struct ScoredId final {
            std::string id;
            float score = 0.0F;
        };
        std::vector<ScoredId> scored;
        scored.reserve(candidates.size());
        for(const auto& candidate : candidates) {
            const auto id = candidate.chunk_id.value();
            const auto document_it = document_embeddings.find(id);
            if(document_it == document_embeddings.end()) {
                throw std::runtime_error(
                    "binary candidate references unknown precomputed document id: "
                    + id
                );
            }
            scored.push_back(ScoredId{
                id,
                score_embedding_pair(query, document_it->second, metric, similarity)
            });
        }
        const auto compare = [](const ScoredId& lhs, const ScoredId& rhs) {
            if(lhs.score == rhs.score) {
                return lhs.id < rhs.id;
            }
            return lhs.score > rhs.score;
        };
        if(scored.size() > result_limit) {
            std::partial_sort(
                scored.begin(),
                scored.begin() + static_cast<std::ptrdiff_t>(result_limit),
                scored.end(),
                compare
            );
            scored.resize(result_limit);
        } else {
            std::sort(scored.begin(), scored.end(), compare);
        }

        std::vector<agent_memory::RetrievalRunHit> hits;
        hits.reserve(scored.size());
        for(const auto& item : scored) {
            hits.push_back(agent_memory::RetrievalRunHit{
                item.id,
                item.score,
                0,
                "binary_candidate_exact_rerank"
            });
        }
        return hits;
    }

    [[nodiscard]] PrecomputedEncoderResult run_precomputed_encoder_benchmark(
        const BenchmarkConfig& config,
        const agent_memory::PrecomputedEmbeddingEvalDataset& dataset,
        const PrecomputedExactOracle& oracle,
        const std::string& encoder_family,
        std::uint32_t encoder_seed,
        std::size_t bit_count
    ) {
        auto build = make_binary_encoder(
            encoder_family,
            dataset.embedding_model.dimension,
            bit_count,
            encoder_seed,
            embedding_vectors(dataset.document_embeddings)
        );
        const auto signature_info = agent_memory::make_binary_signature_info(
            build.encoder->info(),
            dataset.embedding_model,
            "precomputed_dense_projection"
        );
        agent_memory::FlatBinarySignatureIndex binary_index(
            agent_memory::FlatBinarySignatureIndexOptions{signature_info}
        );

        const auto binary_build_start = Clock::now();
        for(const auto& record : dataset.document_embeddings) {
            binary_index.upsert(agent_memory::BinarySignatureRecord{
                agent_memory::ChunkId{record.id},
                build.encoder->encode(record.embedding),
                signature_info,
                {}
            });
        }
        const auto binary_build_end = Clock::now();

        PrecomputedEncoderResult result;
        result.encoder_family = encoder_family;
        result.encoder_seed = encoder_seed;
        result.bit_count = bit_count;
        result.encoder_info = build.encoder->info();
        result.encoder_build = std::move(build.metrics);
        result.binary_build_ms = elapsed_ms(binary_build_start, binary_build_end);
        result.binary_hamming_backend = std::string{
            agent_memory::hamming_distance_backend_name(*binary_index.hamming_backend())
        };
        result.binary_encoder_similarity_backend =
            binary_encoder_compute_backend_name(*build.encoder);
        result.binary_payload_bytes = checked_payload_bytes(
            dataset.document_embeddings.size(),
            agent_memory::binary_signature_word_count(bit_count),
            sizeof(std::uint64_t)
        );

        const auto query_embeddings = embeddings_by_id(dataset.query_embeddings);
        const auto document_embeddings = embeddings_by_id(dataset.document_embeddings);
        const auto positive_qrels = positive_qrels_by_query(dataset.retrieval);
        const agent_memory::VectorSimilarityComputer rerank_similarity;

        result.rerank.reserve(config.rerank_candidate_limits.size());
        for(const auto candidate_limit : config.rerank_candidate_limits) {
            PrecomputedRerankResult row;
            row.candidate_limit = candidate_limit;
            double exact_candidate_coverage_sum = 0.0;
            std::vector<std::unordered_set<std::string>> candidate_sets;
            candidate_sets.reserve(dataset.retrieval.queries.size());
            std::vector<std::vector<agent_memory::RetrievalRunHit>> reranked_hits;
            reranked_hits.reserve(dataset.retrieval.queries.size());

            for(std::size_t query_index = 0;
                query_index < dataset.retrieval.queries.size();
                ++query_index) {
                const auto& query = dataset.retrieval.queries[query_index];
                const auto& query_embedding = query_embeddings.at(query.id);

                const auto encode_start = Clock::now();
                const auto query_signature = build.encoder->encode(query_embedding);
                const auto encode_end = Clock::now();
                row.binary_candidate_query.encode_ms += elapsed_ms(
                    encode_start,
                    encode_end
                );

                const auto search_start = Clock::now();
                const auto candidates = binary_index.search(
                    agent_memory::BinarySignatureSearchQuery{
                        query_signature,
                        signature_info,
                        candidate_limit,
                        {}
                    }
                );
                const auto search_end = Clock::now();
                row.binary_candidate_query.search_ms += elapsed_ms(
                    search_start,
                    search_end
                );

                std::vector<std::string> candidate_ids;
                candidate_ids.reserve(candidates.size());
                std::unordered_set<std::string> candidate_set;
                candidate_set.reserve(candidates.size());
                for(const auto& candidate : candidates) {
                    candidate_ids.push_back(candidate.chunk_id.value());
                    candidate_set.insert(candidate.chunk_id.value());
                }
                exact_candidate_coverage_sum += recall_against_exact_top_k(
                    candidate_ids,
                    oracle.exact_top_k_sets[query_index]
                );
                candidate_sets.push_back(std::move(candidate_set));

                const auto rerank_start = Clock::now();
                reranked_hits.push_back(rerank_precomputed_candidates(
                    query_embedding,
                    document_embeddings,
                    candidates,
                    config.result_limit,
                    dataset.embedding_model.similarity_metric,
                    rerank_similarity
                ));
                const auto rerank_end = Clock::now();
                row.exact_rerank_ms += elapsed_ms(rerank_start, rerank_end);
            }

            row.binary_candidate_query.total_ms =
                row.binary_candidate_query.encode_ms
                + row.binary_candidate_query.search_ms;
            row.total_ms = row.binary_candidate_query.total_ms + row.exact_rerank_ms;
            if(!dataset.retrieval.queries.empty()) {
                row.exact_top_k_candidate_coverage =
                    exact_candidate_coverage_sum
                    / static_cast<double>(dataset.retrieval.queries.size());
            }
            row.qrels_candidate_relevant_coverage = qrels_candidate_relevant_coverage(
                dataset.retrieval,
                positive_qrels,
                candidate_sets
            );
            const auto reranked_run = make_retrieval_run(
                encoder_family + "_reranked_" + std::to_string(bit_count)
                    + "b_candidates_" + std::to_string(candidate_limit),
                dataset.retrieval,
                reranked_hits
            );
            const auto metrics = agent_memory::evaluate_retrieval(
                dataset.retrieval,
                reranked_run
            );
            row.reranked_recall_at_10 =
                agent_memory::metric_value_at(metrics.recall_at, 10).value_or(0.0);
            row.reranked_ndcg_at_10 =
                agent_memory::metric_value_at(metrics.ndcg_at, 10).value_or(0.0);
            row.reranked_mrr = metrics.mrr;
            result.rerank.push_back(row);
        }

        return result;
    }

    [[nodiscard]] nlohmann::json precomputed_encoder_result_to_json(
        const agent_memory::PrecomputedEmbeddingEvalDataset& dataset,
        const PrecomputedExactOracle& oracle,
        const PrecomputedEncoderResult& result
    ) {
        nlohmann::json row;
        row["encoder_family"] = result.encoder_family;
        row["encoder_seed"] = result.encoder_seed;
        row["bit_count"] = result.bit_count;
        row["encoder"] = {
            {"encoder_id", result.encoder_info.encoder_id},
            {"encoder_version", result.encoder_info.encoder_version},
            {"config_fingerprint", result.encoder_info.config_fingerprint},
            {"training", encoder_build_metrics_to_json(result.encoder_build)}
        };
        row["speed"] = {
            {"binary_hamming_backend", result.binary_hamming_backend},
            {"binary_encoder_similarity_backend", result.binary_encoder_similarity_backend},
            {"encoder_training_ms", result.encoder_build.encoder_training_ms},
            {"binary_encode_and_build_ms", result.binary_build_ms}
        };
        row["index"] = {
            {"exact_vector_payload_bytes", oracle.exact_payload_bytes},
            {"binary_signature_payload_bytes", result.binary_payload_bytes},
            {"payload_compression_ratio_exact_over_binary",
                result.binary_payload_bytes == 0
                    ? 0.0
                    : static_cast<double>(oracle.exact_payload_bytes)
                        / static_cast<double>(result.binary_payload_bytes)}
        };
        row["rerank"] = nlohmann::json::array();
        for(const auto& candidate : result.rerank) {
            row["rerank"].push_back({
                {"candidate_limit", candidate.candidate_limit},
                {"exact_top_k_candidate_coverage",
                    candidate.exact_top_k_candidate_coverage},
                {"qrels_candidate_relevant_coverage",
                    candidate.qrels_candidate_relevant_coverage},
                {"reranked_recall_at_10", candidate.reranked_recall_at_10},
                {"reranked_ndcg_at_10", candidate.reranked_ndcg_at_10},
                {"reranked_mrr", candidate.reranked_mrr},
                {"binary_candidate_encode_ms",
                    candidate.binary_candidate_query.encode_ms},
                {"binary_candidate_search_ms",
                    candidate.binary_candidate_query.search_ms},
                {"exact_rerank_ms", candidate.exact_rerank_ms},
                {"total_ms", candidate.total_ms},
                {"mean_query_ms", per_query_ms(
                    candidate.total_ms,
                    dataset.retrieval.queries.size()
                )},
                {"queries_per_second", queries_per_second(
                    candidate.total_ms,
                    dataset.retrieval.queries.size()
                )},
                {"total_speedup_vs_exact_index",
                    candidate.total_ms <= 0.0
                        ? 0.0
                        : oracle.exact_query_ms / candidate.total_ms}
            });
        }
        return row;
    }

    void write_precomputed_binary_rerank_grid_json_report(
        const fs::path& path,
        const BenchmarkConfig& config,
        const agent_memory::PrecomputedEmbeddingEvalDataset& dataset,
        const PrecomputedExactOracle& oracle,
        const std::vector<PrecomputedEncoderResult>& reports
    ) {
        nlohmann::json document;
        document["schema_version"] = 1;
        document["mode"] = std::string{kModePrecomputedEmbeddingBinaryRerankGrid};
        document["benchmark_name"] = config.benchmark_name;
        document["dataset_name"] = dataset.retrieval.name;
        document["dataset_path"] = config.dataset_path.string();
        document["corpus_size"] = dataset.retrieval.corpus.size();
        document["query_count"] = dataset.retrieval.queries.size();
        document["result_limit"] = config.result_limit;
        document["bit_counts"] = config.bit_counts;
        document["rerank_candidate_limits"] = config.rerank_candidate_limits;
        document["encoder_families"] = config.encoder_families;
        document["encoder_seeds"] = config.encoder_seeds;
        document["embedding_model"] = {
            {"model_id", dataset.embedding_model.model_id},
            {"dimension", dataset.embedding_model.dimension},
            {"max_tokens", dataset.embedding_model.max_tokens},
            {"similarity_metric",
                std::string{agent_memory::to_string(dataset.embedding_model.similarity_metric)}},
            {"pooling_mode",
                std::string{agent_memory::to_string(dataset.embedding_model.pooling_mode)}},
            {"normalized", dataset.embedding_model.normalized}
        };
        if(dataset.embedding_artifact) {
            const auto& artifact = *dataset.embedding_artifact;
            document["embedding_artifact"] = {
                {"generator_id", artifact.generator_id},
                {"generator_version", artifact.generator_version},
                {"dataset_revision", artifact.dataset_revision},
                {"generator_revision", artifact.generator_revision},
                {"model_revision", artifact.model_revision},
                {"qrels_revision", artifact.qrels_revision},
                {"document_prompt_id", artifact.document_prompt_id},
                {"query_prompt_id", artifact.query_prompt_id},
                {"projection_kind", artifact.projection_kind},
                {"normalization", artifact.normalization},
                {"dtype", artifact.dtype},
                {"hash_algorithm", artifact.hash_algorithm},
                {"config_hash", artifact.config_hash},
                {"artifact_hash", artifact.artifact_hash}
            };
        } else {
            document["embedding_artifact"] = nullptr;
        }
        document["exact_oracle"] = {
            {"quality", retrieval_metrics_to_json(oracle.qrels_quality)},
            {"speed", {
                {"exact_index_build_ms", oracle.exact_build_ms},
                {"exact_index_query_total_ms", oracle.exact_query_ms},
                {"exact_index_mean_query_ms", per_query_ms(
                    oracle.exact_query_ms,
                    dataset.retrieval.queries.size()
                )},
                {"exact_index_queries_per_second", queries_per_second(
                    oracle.exact_query_ms,
                    dataset.retrieval.queries.size()
                )},
                {"exact_index_similarity_backend", oracle.exact_similarity_backend}
            }}
        };
        document["reports"] = nlohmann::json::array();
        for(const auto& report : reports) {
            document["reports"].push_back(precomputed_encoder_result_to_json(
                dataset,
                oracle,
                report
            ));
        }
        document["process"] = {
            {"peak_resident_set_bytes", peak_resident_set_bytes()}
        };
        document["notes"] = nlohmann::json::array({
            "Precomputed embeddings and qrels are loaded from a frozen JSON fixture; no embedding backend or network call is used.",
            "Exact oracle quality is evaluated against qrels with the shared RetrievalMetrics pipeline.",
            "exact_top_k_candidate_coverage measures how much of the exact vector top-K entered the binary candidate set.",
            "qrels_candidate_relevant_coverage measures how many positively judged qrel documents entered the binary candidate set.",
            "Reranked quality is measured against qrels after exact float reranking of binary candidates.",
            "Encoder training uses document vectors only; query embeddings are evaluation input, not training input.",
            "This mode is intended as a locked real-embedding regression gate, not as a statistically stable production benchmark."
        });
        write_text_file(path, document.dump(2) + "\n");
    }

    int run_precomputed_binary_rerank_grid(BenchmarkConfig config) {
        const auto dataset =
            agent_memory::load_precomputed_embedding_dataset_from_json_file(
                config.dataset_path
            );
        config.dataset_name = dataset.retrieval.name;
        config.document_count = dataset.retrieval.corpus.size();
        config.query_count = dataset.retrieval.queries.size();
        config.embedding_dimensions = dataset.embedding_model.dimension;
        if(config.rerank_candidate_limits.empty()) {
            config.rerank_candidate_limits = default_rerank_candidate_limits(
                config.result_limit,
                config.document_count
            );
        }
        validate_rerank_candidate_limits(config);
        validate_encoder_families(config);

        const auto oracle = prepare_precomputed_exact_oracle(config, dataset);
        std::vector<PrecomputedEncoderResult> reports;
        for(const auto& encoder_family : config.encoder_families) {
            const std::vector<std::uint32_t> active_encoder_seeds =
                encoder_family_uses_seed(encoder_family)
                    ? config.encoder_seeds
                    : std::vector<std::uint32_t>{0};
            for(const auto encoder_seed : active_encoder_seeds) {
                for(const auto bit_count : config.bit_counts) {
                    if(!encoder_family_supports_bit_count(
                           encoder_family,
                           config.embedding_dimensions,
                           bit_count
                       )) {
                        continue;
                    }
                    reports.push_back(run_precomputed_encoder_benchmark(
                        config,
                        dataset,
                        oracle,
                        encoder_family,
                        encoder_seed,
                        bit_count
                    ));
                }
            }
        }
        if(reports.empty()) {
            throw std::runtime_error(
                "precomputed_embedding_binary_rerank_grid produced no reports"
            );
        }

        write_precomputed_binary_rerank_grid_json_report(
            config.output_path,
            config,
            dataset,
            oracle,
            reports
        );
        std::cout << "Precomputed exact qrels Recall@10="
                  << agent_memory::metric_value_at(
                         oracle.qrels_quality.recall_at,
                         10
                     ).value_or(0.0)
                  << " nDCG@10="
                  << agent_memory::metric_value_at(
                         oracle.qrels_quality.ndcg_at,
                         10
                     ).value_or(0.0)
                  << '\n';
        std::cout << "JSON output: " << config.output_path.string() << '\n';
        return 0;
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
        const BinaryEncoderBuildMetrics encoder_build;

        const auto result = run_binary_flat_vs_float_benchmark(
            config,
            encoder,
            encoder_build
        );
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
        std::cout << "Current ExactVectorIndex query total ms: "
                  << result.current_exact_query.total_ms
                  << " qps=" << queries_per_second(
                      result.current_exact_query.total_ms,
                      config.query_count
                  ) << '\n';
        std::cout << "Contiguous exact query total ms: "
                  << result.contiguous_exact_query.total_ms
                  << " qps=" << queries_per_second(
                      result.contiguous_exact_query.total_ms,
                      config.query_count
                  ) << '\n';
        std::cout << "Binary query total ms: " << result.binary_query.total_ms
                  << " qps=" << queries_per_second(
                      result.binary_query.total_ms,
                      config.query_count
                  ) << '\n';
        std::cout << "Binary search speedup vs current ExactVectorIndex: "
                  << (result.binary_query.search_ms <= 0.0
                      ? 0.0
                      : result.current_exact_query.search_ms
                          / result.binary_query.search_ms)
                  << '\n';
        std::cout << "Binary total speedup vs current ExactVectorIndex including encode: "
                  << (result.binary_query.total_ms <= 0.0
                      ? 0.0
                      : result.current_exact_query.total_ms
                          / result.binary_query.total_ms)
                  << '\n';
        std::cout << "Binary total speedup vs contiguous exact including encode: "
                  << (result.binary_query.total_ms <= 0.0
                      ? 0.0
                      : result.contiguous_exact_query.total_ms
                          / result.binary_query.total_ms)
                  << '\n';
        for(const auto& candidate : result.rerank_candidates) {
            std::cout << "Rerank candidates=" << candidate.candidate_limit
                      << " exact_top_k_candidate_coverage="
                      << candidate.exact_top_k_candidate_coverage
                      << " final_recall@k="
                      << candidate.reranked_recall_at_k_vs_exact
                      << " top1_agreement="
                      << candidate.reranked_top1_agreement
                      << " total_speedup_current/contiguous="
                      << (candidate.total_ms <= 0.0
                          ? 0.0
                          : result.current_exact_query.total_ms / candidate.total_ms)
                      << '/'
                      << (candidate.total_ms <= 0.0
                          ? 0.0
                          : result.contiguous_exact_query.total_ms
                              / candidate.total_ms)
                      << '\n';
        }
        std::cout << "Payload bytes exact/binary: " << result.exact_payload_bytes
                  << '/' << result.binary_payload_bytes << '\n';
        std::cout << "JSON output: " << config.output_path.string() << '\n';
        return 0;
    }

    [[nodiscard]] nlohmann::json common_exact_oracle_to_json(
        const BenchmarkConfig& config,
        const BinaryBenchmarkOracle& oracle
    ) {
        return {
            {"data_generation_ms", oracle.data_generation_ms},
            {"current_exact_index_build_ms", oracle.current_exact_build_ms},
            {"contiguous_exact_build_ms", oracle.contiguous_exact_build_ms},
            {"current_exact_index_similarity_backend",
                oracle.current_exact_similarity_backend},
            {"contiguous_exact_similarity_backend",
                oracle.contiguous_exact_similarity_backend},
            {"exact_timing_repeat_count",
                oracle.current_exact_query_total_ms_samples.size()},
            {"current_exact_index_query_total_ms_samples",
                oracle.current_exact_query_total_ms_samples},
            {"current_exact_index_query_total_ms_stats",
                samples_to_stats_json(
                    oracle.current_exact_query_total_ms_samples
                )},
            {"contiguous_exact_query_total_ms_samples",
                oracle.contiguous_exact_query_total_ms_samples},
            {"contiguous_exact_query_total_ms_stats",
                samples_to_stats_json(
                    oracle.contiguous_exact_query_total_ms_samples
                )},
            {"speedup_denominators", {
                {"current_exact_index", "median_current_exact_index_query_total_ms"},
                {"contiguous_exact", "median_contiguous_exact_query_total_ms"}
            }},
            {"current_exact_index_query_total_ms",
                oracle.current_exact_query.total_ms},
            {"contiguous_exact_query_total_ms",
                oracle.contiguous_exact_query.total_ms},
            {"current_exact_index_mean_query_ms", per_query_ms(
                oracle.current_exact_query.total_ms,
                config.query_count
            )},
            {"current_exact_index_queries_per_second", queries_per_second(
                oracle.current_exact_query.total_ms,
                config.query_count
            )},
            {"contiguous_exact_mean_query_ms", per_query_ms(
                oracle.contiguous_exact_query.total_ms,
                config.query_count
            )},
            {"contiguous_exact_queries_per_second", queries_per_second(
                oracle.contiguous_exact_query.total_ms,
                config.query_count
            )}
        };
    }

    void write_binary_rerank_grid_json_report(
        const fs::path& path,
        const BenchmarkConfig& config,
        const std::vector<nlohmann::json>& seed_runs
    ) {
        nlohmann::json document;
        document["schema_version"] = 1;
        document["mode"] = std::string{kModeSyntheticBinaryRerankGrid};
        document["benchmark_name"] = config.benchmark_name;
        document["dataset_name"] = config.dataset_name;
        document["corpus_size"] = config.document_count;
        document["query_count"] = config.query_count;
        document["embedding_dimensions"] = config.embedding_dimensions;
        document["result_limit"] = config.result_limit;
        document["seed"] = config.seed;
        document["seeds"] = config.seeds;
        document["data_seeds"] = config.data_seeds;
        document["encoder_seeds"] = config.encoder_seeds;
        document["encoder_families"] = config.encoder_families;
        document["repeat_count"] = config.repeat_count;
        document["exact_timing_repeat_count"] = config.exact_timing_repeat_count;
        document["randomize_execution_order"] = config.randomize_execution_order;
        document["bit_counts"] = config.bit_counts;
        document["rerank_candidate_limits"] = config.rerank_candidate_limits;
        document["seed_runs"] = seed_runs;
        document["aggregate_summary"] = aggregate_binary_grid_summary(
            config,
            seed_runs
        );
        if(seed_runs.size() == 1) {
            document["common_exact"] = seed_runs.front().at("common_exact");
            document["reports"] = seed_runs.front().at("reports");
        }
        document["notes"] = nlohmann::json::array({
            "Synthetic binary rerank grid reuses one exact oracle per data seed across all bit widths.",
            "data_seeds control synthetic data; encoder_seeds control seedable encoder families.",
            "learned_pair_difference_projection trains only on document vectors for the current data_seed; evaluation queries are not training input.",
            "pca_projection trains only on document vectors for the current data_seed and supports bit_count <= embedding_dimensions.",
            "itq_rotation_projection trains only on document vectors, starts from PCA, then learns an unsupervised ITQ-style orthogonal rotation; it supports bit_count <= embedding_dimensions.",
            "Per-report encoder.training records cold-start artifact training cost and is excluded from query/build timing.",
            "coordinate_sign emits only embedding_dimensions bits and is skipped for other bit_counts.",
            "repeat_count repeats binary timings; quality is sampled once per data/encoder seed pair.",
            "Current ExactVectorIndex and contiguous exact timing use separate repeated medians.",
            "Timing statistics are still local in-process measurements.",
            "Use multi-seed grids to choose the next real-embedding experiment band."
        });
        write_text_file(path, document.dump(2) + "\n");
    }

    int run_binary_rerank_grid(const BenchmarkConfig& config) {
        std::vector<nlohmann::json> seed_runs;
        seed_runs.reserve(config.data_seeds.size() * config.encoder_seeds.size());

        for(const auto data_seed : config.data_seeds) {
            BenchmarkConfig data_config = config;
            data_config.seed = data_seed;
            const auto oracle = prepare_binary_benchmark_oracle(data_config);

            std::cout << "Grid data_seed=" << data_seed
                      << " current_exact_ms="
                      << oracle.current_exact_query.total_ms
                      << " contiguous_exact_ms="
                      << oracle.contiguous_exact_query.total_ms << '\n';

            std::vector<BinaryGridRunState> run_states;
            std::vector<BinaryGridTask> tasks;

            for(const auto& encoder_family : config.encoder_families) {
                const std::vector<std::uint32_t> active_encoder_seeds =
                    encoder_family_uses_seed(encoder_family)
                        ? config.encoder_seeds
                        : std::vector<std::uint32_t>{0};
                for(const auto encoder_seed : active_encoder_seeds) {
                    BinaryGridRunState state;
                    state.encoder_family = encoder_family;
                    state.encoder_seed = encoder_seed;
                    state.seed_run["data_seed"] = data_seed;
                    state.seed_run["encoder_family"] = encoder_family;
                    state.seed_run["encoder_seed"] = encoder_seed;
                    state.seed_run["common_exact"] = common_exact_oracle_to_json(
                        data_config,
                        oracle
                    );
                    state.seed_run["reports"] = nlohmann::json::array();
                    state.results_by_bit.resize(config.bit_counts.size());
                    state.reports_by_bit.resize(config.bit_counts.size());
                    state.encoders_by_bit.resize(config.bit_counts.size());
                    state.encoder_build_metrics_by_bit.resize(config.bit_counts.size());

                    const auto run_index = run_states.size();
                    bool state_has_tasks = false;
                    for(const auto bit_count : config.bit_counts) {
                        if(!encoder_family_supports_bit_count(
                               encoder_family,
                               config.embedding_dimensions,
                               bit_count
                           )) {
                            continue;
                        }
                        const auto bit_it = std::find(
                            config.bit_counts.begin(),
                            config.bit_counts.end(),
                            bit_count
                        );
                        const auto bit_index = static_cast<std::size_t>(
                            std::distance(config.bit_counts.begin(), bit_it)
                        );
                        state.results_by_bit[bit_index].resize(config.repeat_count);
                        state.reports_by_bit[bit_index].resize(config.repeat_count);
                        for(std::size_t repeat = 0; repeat < config.repeat_count;
                            ++repeat) {
                            tasks.push_back(BinaryGridTask{run_index, bit_count, repeat});
                            state_has_tasks = true;
                        }
                    }
                    if(state_has_tasks) {
                        run_states.push_back(std::move(state));
                    }
                }
            }

            if(config.randomize_execution_order) {
                std::mt19937 generator{
                    data_seed ^ static_cast<std::uint32_t>(config.bit_counts.size())
                    ^ static_cast<std::uint32_t>(config.encoder_families.size() << 8U)
                    ^ static_cast<std::uint32_t>(config.encoder_seeds.size() << 16U)
                };
                std::shuffle(tasks.begin(), tasks.end(), generator);
            }

            for(const auto& task : tasks) {
                auto& run_state = run_states[task.run_index];
                const auto& encoder_family = run_state.encoder_family;
                const auto encoder_seed = run_state.encoder_seed;

                const auto bit_it = std::find(
                    config.bit_counts.begin(),
                    config.bit_counts.end(),
                    task.bit_count
                );
                if(bit_it == config.bit_counts.end()) {
                    throw std::runtime_error("binary grid task has unknown bit_count");
                }
                const auto bit_index = static_cast<std::size_t>(
                    std::distance(config.bit_counts.begin(), bit_it)
                );

                BenchmarkConfig run_config = data_config;
                run_config.mode = std::string{kModeSyntheticBinaryFlatVsFloat};
                run_config.seed = encoder_seed;
                run_config.bit_count = task.bit_count;
                run_config.benchmark_name =
                    config.benchmark_name + "_data" + std::to_string(data_seed)
                    + "_" + encoder_family + "_encoder" + std::to_string(encoder_seed)
                    + "_" + std::to_string(task.bit_count) + "b" + "_r"
                    + std::to_string(task.repeat_index);
                run_config.rerank_candidate_limits = config.rerank_candidate_limits;
                if(config.randomize_execution_order) {
                    std::mt19937 limit_generator{
                        data_seed ^ (encoder_seed * 0x85EBCA6BU)
                        ^ encoder_family_salt(encoder_family)
                        ^ static_cast<std::uint32_t>(task.bit_count)
                        ^ static_cast<std::uint32_t>(task.repeat_index)
                    };
                    std::shuffle(
                        run_config.rerank_candidate_limits.begin(),
                        run_config.rerank_candidate_limits.end(),
                        limit_generator
                    );
                }

                auto& encoder = run_state.encoders_by_bit[bit_index];
                if(!encoder) {
                    auto build = make_binary_encoder(
                        encoder_family,
                        run_config.embedding_dimensions,
                        run_config.bit_count,
                        encoder_seed,
                        oracle.data.documents
                    );
                    run_state.encoder_build_metrics_by_bit[bit_index] =
                        std::move(build.metrics);
                    encoder = std::move(build.encoder);
                }

                auto result = run_binary_flat_vs_float_benchmark_with_oracle(
                    run_config,
                    *encoder,
                    oracle,
                    run_state.encoder_build_metrics_by_bit[bit_index]
                );
                sort_rerank_candidates_by_limit(result);
                run_state.results_by_bit[bit_index][task.repeat_index] = result;

                auto report = binary_flat_vs_float_report_to_json(
                    run_config,
                    encoder->info(),
                    result
                );
                report["repeat_index"] = task.repeat_index;
                run_state.reports_by_bit[bit_index][task.repeat_index] =
                    std::move(report);
            }

            for(auto& run_state : run_states) {
                for(std::size_t bit_index = 0; bit_index < config.bit_counts.size();
                    ++bit_index) {
                    if(run_state.results_by_bit[bit_index].empty()) {
                        continue;
                    }
                    const auto bit_count = config.bit_counts[bit_index];
                    BenchmarkConfig summary_config = data_config;
                    summary_config.seed = run_state.encoder_seed;
                    summary_config.bit_count = bit_count;
                    summary_config.rerank_candidate_limits =
                        config.rerank_candidate_limits;

                    nlohmann::json bit_report;
                    bit_report["encoder_family"] = run_state.encoder_family;
                    bit_report["bit_count"] = bit_count;
                    bit_report["repeat_count"] =
                        run_state.results_by_bit[bit_index].size();
                    bit_report["summary"] = summarize_binary_grid_repeats(
                        summary_config,
                        run_state.results_by_bit[bit_index]
                    );
                    bit_report["repeats"] = run_state.reports_by_bit[bit_index];
                    if(run_state.results_by_bit[bit_index].size() == 1) {
                        bit_report["report"] = bit_report["repeats"].front();
                    }
                    run_state.seed_run["reports"].push_back(std::move(bit_report));

                    const auto& last_result =
                        run_state.results_by_bit[bit_index].back();
                    std::cout << "  encoder_family=" << run_state.encoder_family
                              << " encoder_seed=" << run_state.encoder_seed
                              << " bits=" << bit_count
                              << " repeats="
                              << run_state.results_by_bit[bit_index].size()
                              << " direct_recall@k="
                              << last_result.mean_recall_at_k_vs_exact
                              << " direct_top1=" << last_result.top1_agreement
                              << '\n';
                }

                seed_runs.push_back(std::move(run_state.seed_run));
            }
        }

        write_binary_rerank_grid_json_report(config.output_path, config, seed_runs);
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
        if(config.mode == kModeSyntheticBinaryRerankGrid) {
            return run_binary_rerank_grid(config);
        }
        if(config.mode == kModePrecomputedEmbeddingBinaryRerankGrid) {
            return run_precomputed_binary_rerank_grid(config);
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
