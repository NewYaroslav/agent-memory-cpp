#include "AggregateBinarySignature.hpp"

#include <cmath>
#include <stdexcept>

namespace agent_memory {
    namespace {

        [[nodiscard]] bool valid_threshold(double value) noexcept {
            return std::isfinite(value) && value > 0.0 && value <= 1.0;
        }

    } // namespace

    AggregateBinarySignatureBuilder::AggregateBinarySignatureBuilder(
        BinarySignatureAggregationOptions options
    )
        : m_options(options) {
        validate_options();
    }

    AggregateBinarySignatureBuilder::AggregateBinarySignatureBuilder(
        std::size_t bit_count,
        BinarySignatureAggregationOptions options
    )
        : m_options(options),
          m_bit_count(bit_count),
          m_one_counts(bit_count, std::size_t{0}) {
        validate_options();
    }

    std::size_t AggregateBinarySignatureBuilder::bit_count() const noexcept {
        return m_bit_count;
    }

    std::size_t AggregateBinarySignatureBuilder::member_count() const noexcept {
        return m_member_count;
    }

    const std::vector<std::size_t>&
    AggregateBinarySignatureBuilder::one_counts() const noexcept {
        return m_one_counts;
    }

    void AggregateBinarySignatureBuilder::add(const BinarySignature& signature) {
        ensure_width(signature);
        for(std::size_t bit = 0; bit < m_bit_count; ++bit) {
            if(signature.bit(bit)) {
                ++m_one_counts[bit];
            }
        }
        ++m_member_count;
    }

    void AggregateBinarySignatureBuilder::remove(const BinarySignature& signature) {
        require_width(signature);
        if(m_member_count == 0) {
            throw std::invalid_argument(
                "cannot remove a binary signature from an empty aggregate"
            );
        }
        for(std::size_t bit = 0; bit < m_bit_count; ++bit) {
            if(signature.bit(bit)) {
                if(m_one_counts[bit] == 0) {
                    throw std::invalid_argument(
                        "binary signature aggregate removal would underflow bit counter"
                    );
                }
                --m_one_counts[bit];
            }
        }
        --m_member_count;
    }

    void AggregateBinarySignatureBuilder::clear() noexcept {
        m_member_count = 0;
        for(auto& count : m_one_counts) {
            count = 0;
        }
    }

    BinarySignature AggregateBinarySignatureBuilder::signature() const {
        BinarySignature output(m_bit_count);
        if(m_member_count == 0) {
            return output;
        }
        for(std::size_t bit = 0; bit < m_bit_count; ++bit) {
            output.set_bit(bit, aggregate_bit(m_one_counts[bit]));
        }
        return output;
    }

    void AggregateBinarySignatureBuilder::validate_options() const {
        if(m_options.mode == BinarySignatureAggregationMode::ThresholdFraction
           && !valid_threshold(m_options.threshold_fraction)) {
            throw std::invalid_argument(
                "binary signature aggregation threshold must be finite and in (0, 1]"
            );
        }
    }

    void AggregateBinarySignatureBuilder::ensure_width(const BinarySignature& signature) {
        if(m_bit_count == 0) {
            m_bit_count = signature.bit_count();
            m_one_counts.assign(m_bit_count, std::size_t{0});
        }
        require_width(signature);
    }

    void AggregateBinarySignatureBuilder::require_width(
        const BinarySignature& signature
    ) const {
        if(signature.bit_count() != m_bit_count) {
            throw std::invalid_argument(
                "binary signature aggregation requires equal-width signatures"
            );
        }
    }

    bool AggregateBinarySignatureBuilder::aggregate_bit(
        std::size_t one_count
    ) const noexcept {
        switch(m_options.mode) {
            case BinarySignatureAggregationMode::AnySetBit:
                return one_count != 0;
            case BinarySignatureAggregationMode::MajoritySetBit:
            {
                const auto majority_threshold =
                    (m_member_count / 2) + (m_member_count % 2);
                return one_count >= majority_threshold;
            }
            case BinarySignatureAggregationMode::AllSetBits:
                return one_count == m_member_count;
            case BinarySignatureAggregationMode::ThresholdFraction:
                return static_cast<double>(one_count) >=
                       (m_options.threshold_fraction *
                        static_cast<double>(m_member_count));
        }
        return false;
    }

    BinarySignature aggregate_binary_signatures(
        const std::vector<BinarySignature>& signatures,
        BinarySignatureAggregationOptions options
    ) {
        AggregateBinarySignatureBuilder builder(options);
        for(const auto& signature : signatures) {
            builder.add(signature);
        }
        return builder.signature();
    }

} // namespace agent_memory
