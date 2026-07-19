#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_HPP_INCLUDED

/// \file BinarySignature.hpp
/// \brief Packed binary signatures and diagnostic metrics for candidate filters.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace agent_memory {

    /// \brief Returns the number of 64-bit storage words needed for `bit_count`.
    [[nodiscard]] std::size_t binary_signature_word_count(std::size_t bit_count) noexcept;

    /// \brief Packed binary code used by approximate candidate filters.
    class BinarySignature final {
    public:
        BinarySignature() = default;
        explicit BinarySignature(std::size_t bit_count);
        BinarySignature(std::size_t bit_count, std::vector<std::uint64_t> words);

        /// \brief Number of meaningful bits in the signature.
        [[nodiscard]] std::size_t bit_count() const noexcept;

        /// \brief Number of 64-bit words in the packed representation.
        [[nodiscard]] std::size_t word_count() const noexcept;

        /// \brief Returns true when the signature has no bits.
        [[nodiscard]] bool empty() const noexcept;

        /// \brief Reads one bit by zero-based index.
        [[nodiscard]] bool bit(std::size_t index) const;

        /// \brief Sets or clears one bit by zero-based index.
        void set_bit(std::size_t index, bool value = true);

        /// \brief Canonical packed words. Unused tail bits are always zero.
        [[nodiscard]] const std::vector<std::uint64_t>& words() const noexcept;

        friend bool operator==(
            const BinarySignature& lhs,
            const BinarySignature& rhs
        ) noexcept;
        friend bool operator!=(
            const BinarySignature& lhs,
            const BinarySignature& rhs
        ) noexcept;

    private:
        static void validate_words(std::size_t bit_count, const std::vector<std::uint64_t>& words);

        std::size_t m_bit_count = 0;
        std::vector<std::uint64_t> m_words;
    };

    /// \brief Hamming distance between two equal-width binary signatures.
    ///
    /// Uses the best available implementation for the current build and CPU
    /// support, falling back to a portable lookup-table implementation.
    [[nodiscard]] std::size_t hamming_distance(
        const BinarySignature& lhs,
        const BinarySignature& rhs
    );

    /// \brief Options for binary-code health analysis.
    struct BinaryCodeHealthOptions final {
        /// \brief Maximum deterministic pair samples used for pairwise Hamming.
        std::size_t max_pairwise_samples = 4096;
    };

    /// \brief Diagnostics that reveal degenerate or unbalanced binary codes.
    struct BinaryCodeHealthMetrics final {
        /// \brief Number of signatures included in the diagnostic pass.
        std::size_t signature_count = 0;
        /// \brief Shared width of every signature.
        std::size_t bit_count = 0;
        /// \brief Per-bit occupancy, where `0.0` means always zero and `1.0` always one.
        std::vector<double> fraction_ones_per_bit;
        /// \brief Fraction of bit positions whose occupancy is exactly `0.0` or `1.0`.
        double constant_bit_fraction = 0.0;
        /// \brief Mean normalized binary entropy across bit positions.
        double mean_bit_entropy = 0.0;
        /// \brief Minimum normalized binary entropy across bit positions.
        double min_bit_entropy = 0.0;
        /// \brief Maximum normalized binary entropy across bit positions.
        double max_bit_entropy = 0.0;
        /// \brief Fraction of signatures beyond the unique-signature count.
        double duplicate_signature_rate = 0.0;
        /// \brief Mean Hamming distance across all pairs or a deterministic sample.
        double sampled_mean_pairwise_hamming_distance = 0.0;
        /// \brief Number of pairs used for sampled_mean_pairwise_hamming_distance.
        std::size_t sampled_pair_count = 0;
        /// \brief Descending sizes of exact-signature buckets.
        std::vector<std::size_t> exact_signature_bucket_sizes;
    };

    /// \brief Computes deterministic health diagnostics for same-width signatures.
    [[nodiscard]] BinaryCodeHealthMetrics analyze_binary_code_health(
        const std::vector<BinarySignature>& signatures,
        BinaryCodeHealthOptions options = {}
    );

} // namespace agent_memory

#endif
