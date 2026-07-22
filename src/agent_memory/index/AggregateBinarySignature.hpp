#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_AGGREGATE_BINARY_SIGNATURE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_AGGREGATE_BINARY_SIGNATURE_HPP_INCLUDED

/// \file AggregateBinarySignature.hpp
/// \brief Incremental aggregation helpers for document-level binary signatures.

#include "BinarySignature.hpp"

#include <cstddef>
#include <vector>

namespace agent_memory {

    /// \brief Policy used to convert per-bit member counts into one aggregate bit.
    enum class BinarySignatureAggregationMode {
        /// \brief Set the aggregate bit when any member has the bit set.
        AnySetBit,
        /// \brief Set the aggregate bit when at least half of members have it set.
        MajoritySetBit,
        /// \brief Set the aggregate bit only when every member has it set.
        AllSetBits,
        /// \brief Set the bit when the set-bit fraction reaches `threshold_fraction`.
        ThresholdFraction
    };

    /// \brief Options for document or memory-level binary signature aggregation.
    struct BinarySignatureAggregationOptions final {
        BinarySignatureAggregationMode mode = BinarySignatureAggregationMode::AnySetBit;
        /// \brief Fraction in `(0, 1]` used by ThresholdFraction mode.
        double threshold_fraction = 0.5;
    };

    /// \brief Incremental accumulator for compatible same-width binary signatures.
    ///
    /// Addition increments the aggregate member count and per-bit one counters.
    /// Removal performs the inverse operation and is intended for exact removal
    /// of a previously added member signature. The accumulator cannot fully
    /// prove membership for signatures whose set bits are already covered by
    /// other members, so callers should pair removals with their own chunk or
    /// document lifecycle bookkeeping.
    class AggregateBinarySignatureBuilder final {
    public:
        AggregateBinarySignatureBuilder() = default;
        explicit AggregateBinarySignatureBuilder(
            BinarySignatureAggregationOptions options
        );
        AggregateBinarySignatureBuilder(
            std::size_t bit_count,
            BinarySignatureAggregationOptions options = {}
        );

        /// \brief Shared width accepted by the accumulator, or zero before adoption.
        [[nodiscard]] std::size_t bit_count() const noexcept;

        /// \brief Number of member signatures currently represented.
        [[nodiscard]] std::size_t member_count() const noexcept;

        /// \brief Per-bit number of members whose bit is set.
        [[nodiscard]] const std::vector<std::size_t>& one_counts() const noexcept;

        /// \brief Adds one member signature, adopting width on first insertion.
        void add(const BinarySignature& signature);

        /// \brief Removes one previously added member signature.
        /// \throws std::invalid_argument when the width mismatches or removal
        ///         would violate counter invariants.
        void remove(const BinarySignature& signature);

        /// \brief Removes all members while preserving configured/adopted width.
        void clear() noexcept;

        /// \brief Builds the current aggregate signature.
        ///
        /// Empty accumulators return an all-zero signature with the configured
        /// or adopted width.
        [[nodiscard]] BinarySignature signature() const;

    private:
        void validate_options() const;
        void ensure_width(const BinarySignature& signature);
        void require_width(const BinarySignature& signature) const;
        [[nodiscard]] bool aggregate_bit(std::size_t one_count) const noexcept;

        BinarySignatureAggregationOptions m_options;
        std::size_t m_bit_count = 0;
        std::size_t m_member_count = 0;
        std::vector<std::size_t> m_one_counts;
    };

    /// \brief Aggregates a batch of compatible binary signatures in one pass.
    [[nodiscard]] BinarySignature aggregate_binary_signatures(
        const std::vector<BinarySignature>& signatures,
        BinarySignatureAggregationOptions options = {}
    );

} // namespace agent_memory

#endif
