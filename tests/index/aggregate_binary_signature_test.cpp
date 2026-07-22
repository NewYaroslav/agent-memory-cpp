#include <agent_memory/index/AggregateBinarySignature.hpp>

#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    using agent_memory::AggregateBinarySignatureBuilder;
    using agent_memory::BinarySignature;
    using agent_memory::BinarySignatureAggregationMode;
    using agent_memory::BinarySignatureAggregationOptions;
    using agent_memory::aggregate_binary_signatures;

    [[nodiscard]] BinarySignature make_signature(
        std::size_t bit_count,
        std::initializer_list<std::size_t> set_bits
    ) {
        BinarySignature signature(bit_count);
        for(const auto bit : set_bits) {
            signature.set_bit(bit);
        }
        return signature;
    }

    [[nodiscard]] std::string bits(const BinarySignature& signature) {
        std::string output;
        output.reserve(signature.bit_count());
        for(std::size_t bit = 0; bit < signature.bit_count(); ++bit) {
            output.push_back(signature.bit(bit) ? '1' : '0');
        }
        return output;
    }

    bool fail(const std::string& message) {
        std::cerr << message << '\n';
        return false;
    }

    template <class Exception, class Fn>
    bool throws(Fn&& fn) {
        try {
            fn();
        } catch(const Exception&) {
            return true;
        } catch(...) {
            return false;
        }
        return false;
    }

    bool any_set_bit_policy_is_union() {
        const auto aggregate = aggregate_binary_signatures({
            make_signature(6, {0, 2}),
            make_signature(6, {1, 2, 5}),
            make_signature(6, {4})
        });

        if(bits(aggregate) != "111011") {
            return fail("AnySetBit aggregation must keep the union of set bits");
        }
        return true;
    }

    bool majority_policy_uses_member_counts() {
        BinarySignatureAggregationOptions options;
        options.mode = BinarySignatureAggregationMode::MajoritySetBit;

        const auto aggregate = aggregate_binary_signatures({
            make_signature(5, {0, 1}),
            make_signature(5, {1, 2}),
            make_signature(5, {1, 3})
        }, options);

        if(bits(aggregate) != "01000") {
            return fail("MajoritySetBit aggregation must require at least half of members");
        }

        const auto tied = aggregate_binary_signatures({
            make_signature(4, {0}),
            make_signature(4, {1})
        }, options);
        if(bits(tied) != "1100") {
            return fail("MajoritySetBit aggregation must keep even-count ties for recall bias");
        }
        return true;
    }

    bool all_set_policy_is_intersection() {
        BinarySignatureAggregationOptions options;
        options.mode = BinarySignatureAggregationMode::AllSetBits;

        const auto aggregate = aggregate_binary_signatures({
            make_signature(5, {0, 1, 4}),
            make_signature(5, {1, 2, 4}),
            make_signature(5, {1, 3, 4})
        }, options);

        if(bits(aggregate) != "01001") {
            return fail("AllSetBits aggregation must keep only common set bits");
        }
        return true;
    }

    bool threshold_fraction_policy_is_configurable() {
        BinarySignatureAggregationOptions options;
        options.mode = BinarySignatureAggregationMode::ThresholdFraction;
        options.threshold_fraction = 0.75;

        const auto aggregate = aggregate_binary_signatures({
            make_signature(5, {0, 1, 4}),
            make_signature(5, {1, 2, 4}),
            make_signature(5, {1, 3, 4}),
            make_signature(5, {1, 4})
        }, options);

        if(bits(aggregate) != "01001") {
            return fail("ThresholdFraction aggregation must use configured fraction");
        }
        return true;
    }

    bool incremental_remove_updates_counters() {
        AggregateBinarySignatureBuilder builder;
        const auto first = make_signature(6, {0, 2});
        const auto second = make_signature(6, {2, 4});
        const auto third = make_signature(6, {5});

        builder.add(first);
        builder.add(second);
        builder.add(third);
        if(builder.member_count() != 3 || bits(builder.signature()) != "101011") {
            return fail("aggregate builder add() must update counts and signature");
        }

        builder.remove(second);
        if(builder.member_count() != 2 || bits(builder.signature()) != "101001") {
            return fail("aggregate builder remove() must subtract one member");
        }

        const auto& counts = builder.one_counts();
        if(counts.size() != 6 || counts[0] != 1 || counts[2] != 1 || counts[4] != 0
           || counts[5] != 1) {
            return fail("aggregate builder must expose per-bit one counters");
        }

        builder.clear();
        if(builder.member_count() != 0 || builder.bit_count() != 6
           || bits(builder.signature()) != "000000") {
            return fail("aggregate builder clear() must preserve width and reset bits");
        }
        return true;
    }

    bool failed_remove_does_not_change_state() {
        AggregateBinarySignatureBuilder builder;
        builder.add(make_signature(4, {0}));
        builder.add(make_signature(4, {1}));

        const auto member_count_before = builder.member_count();
        const auto counts_before = builder.one_counts();
        const auto signature_before = bits(builder.signature());

        if(!throws<std::invalid_argument>([&] {
               builder.remove(make_signature(4, {0, 2}));
           })) {
            return fail("aggregate builder must reject partial counter underflow");
        }

        if(builder.member_count() != member_count_before ||
           builder.one_counts() != counts_before ||
           bits(builder.signature()) != signature_before) {
            return fail(
                "aggregate builder remove() must leave state unchanged on failure"
            );
        }
        return true;
    }

    bool invalid_inputs_are_rejected() {
        AggregateBinarySignatureBuilder builder;
        builder.add(make_signature(4, {0}));
        if(!throws<std::invalid_argument>([&] {
               builder.add(make_signature(5, {0}));
           })) {
            return fail("aggregate builder must reject width mismatch on add");
        }
        if(!throws<std::invalid_argument>([&] {
               builder.remove(make_signature(5, {0}));
           })) {
            return fail("aggregate builder must reject width mismatch on remove");
        }

        AggregateBinarySignatureBuilder empty(4);
        if(!throws<std::invalid_argument>([&] {
               empty.remove(make_signature(4, {0}));
           })) {
            return fail("aggregate builder must reject removal from an empty aggregate");
        }

        if(!throws<std::invalid_argument>([&] {
               AggregateBinarySignatureBuilder invalid(0);
           })) {
            return fail("aggregate builder must reject configured zero width");
        }
        if(!throws<std::invalid_argument>([&] {
               AggregateBinarySignatureBuilder invalid;
               invalid.add(BinarySignature(0));
           })) {
            return fail("aggregate builder must reject zero-width members on add");
        }
        if(!throws<std::invalid_argument>([&] {
               AggregateBinarySignatureBuilder invalid(4);
               invalid.remove(BinarySignature(0));
           })) {
            return fail("aggregate builder must reject zero-width members on remove");
        }

        AggregateBinarySignatureBuilder underflow(4);
        underflow.add(make_signature(4, {1}));
        if(!throws<std::invalid_argument>([&] {
               underflow.remove(make_signature(4, {0}));
           })) {
            return fail("aggregate builder must reject one-counter underflow");
        }

        BinarySignatureAggregationOptions options;
        options.mode = BinarySignatureAggregationMode::ThresholdFraction;
        options.threshold_fraction = 0.0;
        if(!throws<std::invalid_argument>([&] {
               AggregateBinarySignatureBuilder invalid(options);
           })) {
            return fail("aggregate builder must reject zero threshold");
        }

        options.threshold_fraction = std::numeric_limits<double>::quiet_NaN();
        if(!throws<std::invalid_argument>([&] {
               AggregateBinarySignatureBuilder invalid(options);
           })) {
            return fail("aggregate builder must reject non-finite threshold");
        }
        return true;
    }

} // namespace

int main() {
    const std::vector<bool (*)()> tests{
        any_set_bit_policy_is_union,
        majority_policy_uses_member_counts,
        all_set_policy_is_intersection,
        threshold_fraction_policy_is_configurable,
        incremental_remove_updates_counters,
        failed_remove_does_not_change_state,
        invalid_inputs_are_rejected
    };

    for(const auto test : tests) {
        try {
            if(!test()) {
                return 1;
            }
        } catch(const std::exception& exc) {
            std::cerr << "unexpected exception: " << exc.what() << '\n';
            return 1;
        }
    }
    return 0;
}
