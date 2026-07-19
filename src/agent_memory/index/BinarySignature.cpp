#include "BinarySignature.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace agent_memory {
    namespace {

        constexpr std::size_t kBitsPerWord = 64;

        [[nodiscard]] std::uint64_t valid_tail_mask(std::size_t bit_count) noexcept {
            const auto remainder = bit_count % kBitsPerWord;
            if(remainder == 0) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return (std::uint64_t{1} << remainder) - std::uint64_t{1};
        }

        [[nodiscard]] std::size_t popcount64(std::uint64_t value) noexcept {
            std::size_t count = 0;
            while(value != 0) {
                value &= value - std::uint64_t{1};
                ++count;
            }
            return count;
        }

        [[nodiscard]] double bit_entropy(double probability_one) noexcept {
            if(probability_one <= 0.0 || probability_one >= 1.0) {
                return 0.0;
            }

            const double probability_zero = 1.0 - probability_one;
            const double log_two = std::log(2.0);
            return -(
                probability_one * (std::log(probability_one) / log_two) +
                probability_zero * (std::log(probability_zero) / log_two)
            );
        }

        [[nodiscard]] std::size_t deterministic_pair_index(
            std::size_t sample_index,
            std::size_t signature_count,
            std::uint64_t multiplier,
            std::uint64_t increment
        ) noexcept {
            const auto value =
                (static_cast<std::uint64_t>(sample_index) * multiplier) + increment;
            return static_cast<std::size_t>(value % static_cast<std::uint64_t>(signature_count));
        }

        [[nodiscard]] std::size_t total_pair_count(std::size_t n) {
            if(n < 2) {
                return 0;
            }
            if(n > (std::numeric_limits<std::size_t>::max() / (n - 1))) {
                throw std::overflow_error("Binary code pair count overflow");
            }
            return (n * (n - 1)) / 2;
        }

    } // namespace

    std::size_t binary_signature_word_count(std::size_t bit_count) noexcept {
        return (bit_count + (kBitsPerWord - 1)) / kBitsPerWord;
    }

    BinarySignature::BinarySignature(std::size_t bit_count)
        : m_bit_count(bit_count),
          m_words(binary_signature_word_count(bit_count), std::uint64_t{0}) {}

    BinarySignature::BinarySignature(std::size_t bit_count, std::vector<std::uint64_t> words)
        : m_bit_count(bit_count),
          m_words(std::move(words)) {
        validate_words(m_bit_count, m_words);
    }

    std::size_t BinarySignature::bit_count() const noexcept {
        return m_bit_count;
    }

    std::size_t BinarySignature::word_count() const noexcept {
        return m_words.size();
    }

    bool BinarySignature::empty() const noexcept {
        return m_bit_count == 0;
    }

    bool BinarySignature::bit(std::size_t index) const {
        if(index >= m_bit_count) {
            throw std::out_of_range("BinarySignature bit index out of range");
        }

        const auto word_index = index / kBitsPerWord;
        const auto bit_index = index % kBitsPerWord;
        return (m_words[word_index] & (std::uint64_t{1} << bit_index)) != 0;
    }

    void BinarySignature::set_bit(std::size_t index, bool value) {
        if(index >= m_bit_count) {
            throw std::out_of_range("BinarySignature bit index out of range");
        }

        const auto word_index = index / kBitsPerWord;
        const auto bit_index = index % kBitsPerWord;
        const auto mask = std::uint64_t{1} << bit_index;
        if(value) {
            m_words[word_index] |= mask;
        } else {
            m_words[word_index] &= ~mask;
        }
    }

    const std::vector<std::uint64_t>& BinarySignature::words() const noexcept {
        return m_words;
    }

    void BinarySignature::validate_words(
        std::size_t bit_count,
        const std::vector<std::uint64_t>& words
    ) {
        if(words.size() != binary_signature_word_count(bit_count)) {
            throw std::invalid_argument("BinarySignature word count does not match bit count");
        }

        if(words.empty()) {
            return;
        }

        const auto tail_mask = valid_tail_mask(bit_count);
        if((words.back() & ~tail_mask) != 0) {
            throw std::invalid_argument("BinarySignature unused tail bits must be zero");
        }
    }

    bool operator==(const BinarySignature& lhs, const BinarySignature& rhs) noexcept {
        return lhs.m_bit_count == rhs.m_bit_count && lhs.m_words == rhs.m_words;
    }

    bool operator!=(const BinarySignature& lhs, const BinarySignature& rhs) noexcept {
        return !(lhs == rhs);
    }

    std::size_t hamming_distance(const BinarySignature& lhs, const BinarySignature& rhs) {
        if(lhs.bit_count() != rhs.bit_count()) {
            throw std::invalid_argument("Hamming distance requires equal-width signatures");
        }

        std::size_t distance = 0;
        for(std::size_t i = 0; i < lhs.words().size(); ++i) {
            distance += popcount64(lhs.words()[i] ^ rhs.words()[i]);
        }
        return distance;
    }

    BinaryCodeHealthMetrics analyze_binary_code_health(
        const std::vector<BinarySignature>& signatures,
        BinaryCodeHealthOptions options
    ) {
        BinaryCodeHealthMetrics metrics;
        metrics.signature_count = signatures.size();
        if(signatures.empty()) {
            return metrics;
        }

        metrics.bit_count = signatures.front().bit_count();
        for(const auto& signature : signatures) {
            if(signature.bit_count() != metrics.bit_count) {
                throw std::invalid_argument(
                    "Binary code health analysis requires equal-width signatures"
                );
            }
        }

        metrics.fraction_ones_per_bit.assign(metrics.bit_count, 0.0);
        if(metrics.bit_count > 0) {
            for(std::size_t bit = 0; bit < metrics.bit_count; ++bit) {
                std::size_t ones = 0;
                for(const auto& signature : signatures) {
                    if(signature.bit(bit)) {
                        ++ones;
                    }
                }
                metrics.fraction_ones_per_bit[bit] =
                    static_cast<double>(ones) / static_cast<double>(signatures.size());
            }

            std::size_t constant_bits = 0;
            double entropy_sum = 0.0;
            metrics.min_bit_entropy = std::numeric_limits<double>::infinity();
            metrics.max_bit_entropy = 0.0;

            for(const auto fraction : metrics.fraction_ones_per_bit) {
                if(fraction == 0.0 || fraction == 1.0) {
                    ++constant_bits;
                }

                const auto entropy = bit_entropy(fraction);
                entropy_sum += entropy;
                metrics.min_bit_entropy = std::min(metrics.min_bit_entropy, entropy);
                metrics.max_bit_entropy = std::max(metrics.max_bit_entropy, entropy);
            }

            metrics.constant_bit_fraction =
                static_cast<double>(constant_bits) / static_cast<double>(metrics.bit_count);
            metrics.mean_bit_entropy = entropy_sum / static_cast<double>(metrics.bit_count);
        }

        std::map<std::vector<std::uint64_t>, std::size_t> buckets;
        for(const auto& signature : signatures) {
            ++buckets[signature.words()];
        }

        metrics.exact_signature_bucket_sizes.reserve(buckets.size());
        for(const auto& item : buckets) {
            metrics.exact_signature_bucket_sizes.push_back(item.second);
        }
        std::sort(
            metrics.exact_signature_bucket_sizes.begin(),
            metrics.exact_signature_bucket_sizes.end(),
            std::greater<std::size_t>{}
        );

        metrics.duplicate_signature_rate =
            static_cast<double>(signatures.size() - buckets.size()) /
            static_cast<double>(signatures.size());

        const auto all_pairs = total_pair_count(signatures.size());
        const auto sample_limit = std::min(options.max_pairwise_samples, all_pairs);
        if(sample_limit == 0) {
            return metrics;
        }

        std::size_t distance_sum = 0;
        if(all_pairs <= options.max_pairwise_samples) {
            for(std::size_t i = 0; i < signatures.size(); ++i) {
                for(std::size_t j = i + 1; j < signatures.size(); ++j) {
                    distance_sum += hamming_distance(signatures[i], signatures[j]);
                    ++metrics.sampled_pair_count;
                }
            }
        } else {
            for(std::size_t sample = 0; sample < sample_limit; ++sample) {
                const auto i = deterministic_pair_index(
                    sample,
                    signatures.size(),
                    1103515245ULL,
                    12345ULL
                );
                auto j = deterministic_pair_index(
                    sample,
                    signatures.size() - 1,
                    2654435761ULL,
                    1013904223ULL
                );
                if(j >= i) {
                    ++j;
                }
                distance_sum += hamming_distance(signatures[i], signatures[j]);
                ++metrics.sampled_pair_count;
            }
        }

        metrics.sampled_mean_pairwise_hamming_distance =
            static_cast<double>(distance_sum) / static_cast<double>(metrics.sampled_pair_count);
        return metrics;
    }

} // namespace agent_memory
