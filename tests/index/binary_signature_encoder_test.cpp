#include <agent_memory.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    template <typename Fn>
    bool throws_invalid_argument(Fn&& fn) {
        try {
            fn();
        } catch(const std::invalid_argument&) {
            return true;
        }
        return false;
    }

    agent_memory::RandomHyperplaneBinaryEncoderOptions make_options(
        std::size_t input_dimension,
        std::size_t bit_count,
        std::uint64_t seed
    ) {
        agent_memory::RandomHyperplaneBinaryEncoderOptions options;
        options.input_dimension = input_dimension;
        options.bit_count = bit_count;
        options.seed = seed;
        return options;
    }

    class GroupedNumpunct final : public std::numpunct<char> {
    protected:
        [[nodiscard]] char do_thousands_sep() const override {
            return ',';
        }

        [[nodiscard]] std::string do_grouping() const override {
            return "\3";
        }
    };

} // namespace

int main() {
    const auto options = make_options(3, 16, 42);
    std::unique_ptr<agent_memory::IBinarySignatureEncoder> encoder =
        std::make_unique<agent_memory::RandomHyperplaneBinaryEncoder>(options);

    const auto& info = encoder->info();
    if(info.encoder_id != "random_hyperplane_rademacher") {
        return fail("random hyperplane encoder must expose a stable encoder id");
    }
    if(info.encoder_version != "v1") {
        return fail("random hyperplane encoder must expose a stable encoder version");
    }
    if(info.input_dimension != 3 || info.bit_count != 16 || info.seed != 42) {
        return fail("random hyperplane encoder info must mirror construction options");
    }
    if(info.config_fingerprint != "random_hyperplane_rademacher_v1:dim=3:bits=16:seed=42") {
        return fail("random hyperplane encoder must expose a stable config fingerprint");
    }

    const auto previous_locale = std::locale();
    std::locale::global(std::locale(previous_locale, new GroupedNumpunct));
    const agent_memory::RandomHyperplaneBinaryEncoder locale_encoder(
        make_options(3000, 16000, 1000000)
    );
    std::locale::global(previous_locale);
    if(locale_encoder.info().config_fingerprint !=
       "random_hyperplane_rademacher_v1:dim=3000:bits=16000:seed=1000000") {
        return fail("encoder config fingerprint must not depend on the global locale");
    }

    const agent_memory::Embedding vector{{1.0F, -2.0F, 0.5F}};
    const auto signature = encoder->encode(vector);
    const auto repeated = encoder->encode(vector);
    if(signature != repeated) {
        return fail("random hyperplane encoder must be deterministic for fixed input");
    }
    if(signature.bit_count() != 16 || signature.word_count() != 1) {
        return fail("encoded signature must use the configured bit count");
    }
    if(signature.words().front() != 0xE4FBULL) {
        return fail("random hyperplane encoder output must match the pinned v1 contract");
    }

    const agent_memory::RandomHyperplaneBinaryEncoder seed_variant(make_options(3, 16, 43));
    const auto seed_variant_signature = seed_variant.encode(vector);
    if(seed_variant_signature.words().front() != 0x637DULL || seed_variant_signature == signature) {
        return fail("changing the random hyperplane seed must change the pinned signature");
    }

    const auto eight_bit_signature =
        agent_memory::RandomHyperplaneBinaryEncoder(make_options(3, 8, 42)).encode(vector);
    if(eight_bit_signature.words().front() != 0xFBULL) {
        return fail("short signatures must use the same deterministic bit stream prefix");
    }

    const agent_memory::Embedding zero_vector{{0.0F, 0.0F, 0.0F}};
    const auto zero_signature = encoder->encode(zero_vector);
    if(zero_signature.words().front() != 0ULL) {
        return fail("zero dot-product ties must encode as zero bits");
    }

    const auto wide_signature =
        agent_memory::RandomHyperplaneBinaryEncoder(make_options(3, 70, 42)).encode(vector);
    if(wide_signature.bit_count() != 70 || wide_signature.word_count() != 2) {
        return fail("wide encoded signatures must expose canonical packed storage");
    }
    const auto meaningful_tail_mask = (std::uint64_t{1} << 6U) - 1U;
    if((wide_signature.words().back() & ~meaningful_tail_mask) != 0ULL) {
        return fail("encoded signatures must keep unused tail bits cleared");
    }

    if(!throws_invalid_argument([] {
           (void)agent_memory::RandomHyperplaneBinaryEncoder(make_options(0, 16, 42));
       })) {
        return fail("encoder construction must reject zero input dimension");
    }
    if(!throws_invalid_argument([] {
           (void)agent_memory::RandomHyperplaneBinaryEncoder(make_options(3, 0, 42));
       })) {
        return fail("encoder construction must reject zero bit count");
    }
    if(!throws_invalid_argument([&] {
           (void)encoder->encode(agent_memory::Embedding{{1.0F, 2.0F}});
       })) {
        return fail("encoder must reject dimension mismatches");
    }
    if(!throws_invalid_argument([&] {
           (void)encoder->encode(agent_memory::Embedding{{
               1.0F,
               std::numeric_limits<float>::infinity(),
               0.5F,
           }});
       })) {
        return fail("encoder must reject non-finite input values");
    }
    if(!throws_invalid_argument([&] {
           (void)encoder->encode(agent_memory::Embedding{{
               1.0F,
               std::numeric_limits<float>::quiet_NaN(),
               0.5F,
           }});
       })) {
        return fail("encoder must reject NaN input values");
    }

    return 0;
}
