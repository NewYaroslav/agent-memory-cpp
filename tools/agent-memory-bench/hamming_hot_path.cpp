#include <agent_memory.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    using Clock = std::chrono::steady_clock;

    struct Config final {
        std::size_t record_count = 10000;
        std::size_t query_count = 100;
        std::size_t bit_count = 128;
        std::size_t result_limit = 10;
        std::size_t repeat_count = 5;
        std::uint64_t seed = 42;
        std::size_t multi_table_count = 8;
        std::size_t multi_bits_per_table = 8;
        std::size_t multi_probe_radius = 1;
        std::size_t multi_candidate_multiplier = 64;
        std::size_t multi_minimum_candidate_count = 128;
    };

    [[nodiscard]] double elapsed_ms(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    [[nodiscard]] std::size_t read_size(
        const nlohmann::json& document,
        const char* field,
        std::size_t fallback
    ) {
        if(!document.contains(field)) {
            return fallback;
        }
        if(!document.at(field).is_number_unsigned()) {
            throw std::invalid_argument(std::string("config field '") + field + "' must be unsigned");
        }
        return document.at(field).get<std::size_t>();
    }

    [[nodiscard]] Config load_config(const std::string& path) {
        std::ifstream input(path);
        if(!input) {
            throw std::runtime_error("failed to open Hamming hot-path config: " + path);
        }
        nlohmann::json document;
        input >> document;

        Config config;
        config.record_count = read_size(document, "record_count", config.record_count);
        config.query_count = read_size(document, "query_count", config.query_count);
        config.bit_count = read_size(document, "bit_count", config.bit_count);
        config.result_limit = read_size(document, "result_limit", config.result_limit);
        config.repeat_count = read_size(document, "repeat_count", config.repeat_count);
        config.multi_table_count = read_size(
            document,
            "multi_table_count",
            config.multi_table_count
        );
        config.multi_bits_per_table = read_size(
            document,
            "multi_bits_per_table",
            config.multi_bits_per_table
        );
        config.multi_probe_radius = read_size(
            document,
            "multi_probe_radius",
            config.multi_probe_radius
        );
        config.multi_candidate_multiplier = read_size(
            document,
            "multi_candidate_multiplier",
            config.multi_candidate_multiplier
        );
        config.multi_minimum_candidate_count = read_size(
            document,
            "multi_minimum_candidate_count",
            config.multi_minimum_candidate_count
        );
        config.seed = document.value("seed", config.seed);
        if(config.record_count == 0 || config.query_count == 0 || config.bit_count < 16
           || config.result_limit == 0 || config.result_limit > config.record_count
           || config.repeat_count == 0) {
            throw std::invalid_argument("Hamming hot-path config contains invalid zero or range values");
        }
        return config;
    }

    [[nodiscard]] agent_memory::BinarySignatureInfo make_info(const Config& config) {
        agent_memory::BinarySignatureInfo info;
        info.encoder_id = "hamming_hot_path_fixture";
        info.encoder_version = "v1";
        info.encoder_config_fingerprint = "hamming_hot_path_fixture:" +
            std::to_string(config.bit_count);
        info.source_model_id = "synthetic:binary";
        info.projection_kind = "symmetric";
        info.source_dimension = config.bit_count;
        info.bit_count = config.bit_count;
        info.source_similarity_metric = agent_memory::SimilarityMetric::Cosine;
        info.source_normalized = true;
        info.seed = config.seed;
        return info;
    }

    [[nodiscard]] agent_memory::BinarySignature random_signature(
        std::size_t bit_count,
        std::mt19937_64& random
    ) {
        std::vector<std::uint64_t> words(
            agent_memory::binary_signature_word_count(bit_count)
        );
        for(auto& word : words) {
            word = random();
        }
        const auto remainder = bit_count % 64;
        if(remainder != 0) {
            words.back() &= (std::uint64_t{1} << remainder) - 1ULL;
        }
        return agent_memory::BinarySignature(bit_count, std::move(words));
    }

    struct ScoredPosition final {
        std::size_t position = 0;
        std::size_t distance = 0;
    };

    [[nodiscard]] bool closer_position(
        const ScoredPosition& lhs,
        const ScoredPosition& rhs
    ) noexcept {
        return lhs.distance == rhs.distance
            ? lhs.position < rhs.position
            : lhs.distance < rhs.distance;
    }

    [[nodiscard]] std::size_t selected_checksum(
        const std::vector<ScoredPosition>& selected
    ) noexcept {
        std::size_t checksum = 0;
        for(const auto& item : selected) {
            checksum += (item.position + 1) * (item.distance + 1);
        }
        return checksum;
    }

    [[nodiscard]] std::vector<ScoredPosition> make_scored_positions(
        const std::vector<std::size_t>& distances
    ) {
        std::vector<ScoredPosition> scored;
        scored.reserve(distances.size());
        for(std::size_t position = 0; position < distances.size(); ++position) {
            scored.push_back({position, distances[position]});
        }
        return scored;
    }

    [[nodiscard]] std::size_t select_top_k_partial_sort_checksum(
        const std::vector<std::size_t>& distances,
        std::size_t limit
    ) {
        auto scored = make_scored_positions(distances);
        std::partial_sort(
            scored.begin(),
            scored.begin() + static_cast<std::ptrdiff_t>(limit),
            scored.end(),
            closer_position
        );
        scored.resize(limit);
        return selected_checksum(scored);
    }

    [[nodiscard]] std::size_t select_top_k_nth_element_checksum(
        const std::vector<std::size_t>& distances,
        std::size_t limit
    ) {
        auto scored = make_scored_positions(distances);
        if(limit < scored.size()) {
            std::nth_element(
                scored.begin(),
                scored.begin() + static_cast<std::ptrdiff_t>(limit),
                scored.end(),
                closer_position
            );
            scored.resize(limit);
        }
        std::sort(scored.begin(), scored.end(), closer_position);
        return selected_checksum(scored);
    }

    [[nodiscard]] std::size_t select_top_k_bucket_checksum(
        const std::vector<std::size_t>& distances,
        std::size_t bit_count,
        std::size_t limit
    ) {
        std::vector<std::size_t> counts(bit_count + 1, 0);
        for(const auto distance : distances) {
            ++counts[distance];
        }
        std::size_t cutoff = 0;
        std::size_t cumulative = 0;
        for(; cutoff < counts.size(); ++cutoff) {
            cumulative += counts[cutoff];
            if(cumulative >= limit) {
                break;
            }
        }

        std::vector<ScoredPosition> selected;
        selected.reserve(cumulative);
        for(std::size_t position = 0; position < distances.size(); ++position) {
            if(distances[position] <= cutoff) {
                selected.push_back({position, distances[position]});
            }
        }
        std::sort(selected.begin(), selected.end(), closer_position);
        selected.resize(limit);
        return selected_checksum(selected);
    }

    [[nodiscard]] double median(std::vector<double> values) {
        std::sort(values.begin(), values.end());
        const auto middle = values.size() / 2;
        if(values.size() % 2 != 0) {
            return values[middle];
        }
        return (values[middle - 1] + values[middle]) / 2.0;
    }

    [[nodiscard]] double recall_at_k(
        const std::vector<agent_memory::BinarySignatureSearchResult>& actual,
        const std::vector<agent_memory::BinarySignatureSearchResult>& expected
    ) {
        std::set<std::string> expected_ids;
        for(const auto& item : expected) {
            expected_ids.insert(item.chunk_id.value());
        }
        std::size_t matches = 0;
        for(const auto& item : actual) {
            if(expected_ids.count(item.chunk_id.value()) != 0) {
                ++matches;
            }
        }
        return expected_ids.empty()
            ? 0.0
            : static_cast<double>(matches) / static_cast<double>(expected_ids.size());
    }

    int run(const Config& config, const std::string& output_path) {
        const auto info = make_info(config);
        agent_memory::FlatBinarySignatureIndex flat({info});

        agent_memory::MultiProbeHammingIndexOptions multi_options;
        multi_options.signature_info = info;
        multi_options.bits_per_table = config.multi_bits_per_table;
        multi_options.table_count = config.multi_table_count;
        multi_options.max_probe_radius = config.multi_probe_radius;
        multi_options.candidate_multiplier = config.multi_candidate_multiplier;
        multi_options.minimum_candidate_count = config.multi_minimum_candidate_count;
        agent_memory::MultiProbeHammingIndex multi(multi_options);

        std::mt19937_64 random(config.seed);
        const auto word_count = agent_memory::binary_signature_word_count(config.bit_count);
        std::vector<std::uint64_t> contiguous_words;
        contiguous_words.reserve(config.record_count * word_count);
        for(std::size_t record = 0; record < config.record_count; ++record) {
            auto signature = random_signature(config.bit_count, random);
            contiguous_words.insert(
                contiguous_words.end(),
                signature.words().begin(),
                signature.words().end()
            );
            agent_memory::BinarySignatureRecord value{
                agent_memory::ChunkId{"doc:" + std::to_string(record)},
                signature,
                info,
                {}
            };
            flat.upsert(value);
            multi.upsert(std::move(value));
        }

        std::vector<agent_memory::BinarySignatureSearchQuery> queries;
        queries.reserve(config.query_count);
        for(std::size_t query = 0; query < config.query_count; ++query) {
            queries.push_back({random_signature(config.bit_count, random), info, config.result_limit, {}});
        }

        const agent_memory::HammingDistanceComputer distance(word_count);
        std::vector<std::size_t> distances(config.record_count);
        std::vector<double> raw_samples;
        std::vector<double> partial_sort_samples;
        std::vector<double> nth_element_samples;
        std::vector<double> bucket_selection_samples;
        std::vector<double> flat_samples;
        std::vector<double> multi_samples;
        double multi_candidate_sum = 0.0;
        double multi_bucket_sum = 0.0;
        double multi_recall_sum = 0.0;
        std::size_t checksum = 0;

        for(std::size_t repeat = 0; repeat < config.repeat_count; ++repeat) {
            double raw_ms = 0.0;
            double partial_sort_ms = 0.0;
            double nth_element_ms = 0.0;
            double bucket_selection_ms = 0.0;
            double flat_ms = 0.0;
            double multi_ms = 0.0;
            for(const auto& query : queries) {
                auto start = Clock::now();
                distance.compute_distances(
                    query.signature.words().data(),
                    contiguous_words.data(),
                    config.record_count,
                    distances.data()
                );
                auto end = Clock::now();
                raw_ms += elapsed_ms(start, end);

                start = Clock::now();
                const auto partial_checksum =
                    select_top_k_partial_sort_checksum(distances, config.result_limit);
                end = Clock::now();
                partial_sort_ms += elapsed_ms(start, end);

                start = Clock::now();
                const auto nth_checksum =
                    select_top_k_nth_element_checksum(distances, config.result_limit);
                end = Clock::now();
                nth_element_ms += elapsed_ms(start, end);

                start = Clock::now();
                const auto bucket_checksum = select_top_k_bucket_checksum(
                    distances,
                    config.bit_count,
                    config.result_limit
                );
                end = Clock::now();
                bucket_selection_ms += elapsed_ms(start, end);
                if(partial_checksum != nth_checksum || partial_checksum != bucket_checksum) {
                    throw std::logic_error("Hamming top-k selection strategies disagree");
                }
                checksum += partial_checksum + nth_checksum + bucket_checksum;

                start = Clock::now();
                const auto flat_results = flat.search(query);
                end = Clock::now();
                flat_ms += elapsed_ms(start, end);
                checksum += flat_results.size();

                start = Clock::now();
                const auto multi_result = multi.search_with_diagnostics(query);
                end = Clock::now();
                multi_ms += elapsed_ms(start, end);
                checksum += multi_result.results.size();
                if(repeat == 0) {
                    multi_candidate_sum += static_cast<double>(multi_result.candidate_count);
                    multi_bucket_sum += static_cast<double>(multi_result.probed_bucket_count);
                    multi_recall_sum += recall_at_k(multi_result.results, flat_results);
                }
            }
            raw_samples.push_back(raw_ms);
            partial_sort_samples.push_back(partial_sort_ms);
            nth_element_samples.push_back(nth_element_ms);
            bucket_selection_samples.push_back(bucket_selection_ms);
            flat_samples.push_back(flat_ms);
            multi_samples.push_back(multi_ms);
        }

        const auto raw_median = median(raw_samples);
        const auto partial_sort_median = median(partial_sort_samples);
        const auto nth_element_median = median(nth_element_samples);
        const auto bucket_selection_median = median(bucket_selection_samples);
        const auto flat_median = median(flat_samples);
        const auto multi_median = median(multi_samples);
        nlohmann::json report{
            {"schema_version", 1},
            {"benchmark_name", "hamming_hot_path_v1"},
            {"record_count", config.record_count},
            {"query_count", config.query_count},
            {"bit_count", config.bit_count},
            {"result_limit", config.result_limit},
            {"repeat_count", config.repeat_count},
            {"multi_probe_config", {
                {"table_count", config.multi_table_count},
                {"bits_per_table", config.multi_bits_per_table},
                {"probe_radius", config.multi_probe_radius},
                {"candidate_multiplier", config.multi_candidate_multiplier},
                {"minimum_candidate_count", config.multi_minimum_candidate_count}
            }},
            {
                "hamming_backend",
                std::string(agent_memory::hamming_distance_backend_name(distance.backend()))
            },
            {"raw_hamming_ms", raw_median},
            {"selection_partial_sort_ms", partial_sort_median},
            {"selection_nth_element_ms", nth_element_median},
            {"selection_distance_buckets_ms", bucket_selection_median},
            {"flat_index_ms", flat_median},
            {"multi_probe_index_ms", multi_median},
            {"flat_over_raw_ratio", raw_median == 0.0 ? 0.0 : flat_median / raw_median},
            {"multi_probe_speedup_vs_flat", multi_median == 0.0 ? 0.0 : flat_median / multi_median},
            {"multi_probe_mean_candidate_count", multi_candidate_sum / config.query_count},
            {"multi_probe_mean_bucket_count", multi_bucket_sum / config.query_count},
            {"multi_probe_recall_at_k_vs_flat", multi_recall_sum / config.query_count},
            {"checksum", checksum}
        };

        std::ofstream output(output_path);
        if(!output) {
            throw std::runtime_error("failed to open Hamming hot-path report: " + output_path);
        }
        output << report.dump(2) << '\n';
        std::cout << report.dump(2) << '\n';
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    if(argc != 3) {
        std::cerr << "usage: agent-memory-hamming-hot-path-bench <config.json> <report.json>\n";
        return 2;
    }
    try {
        return run(load_config(argv[1]), argv[2]);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
