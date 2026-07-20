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

    agent_memory::OrthogonalProjectionBinaryEncoderOptions make_orthogonal_options(
        std::size_t input_dimension,
        std::size_t bit_count,
        std::uint64_t seed
    ) {
        agent_memory::OrthogonalProjectionBinaryEncoderOptions options;
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
    if(info.encoder_version != "v2") {
        return fail("random hyperplane encoder must expose a stable encoder version");
    }
    if(info.input_dimension != 3 || info.bit_count != 16 || info.seed != 42) {
        return fail("random hyperplane encoder info must mirror construction options");
    }
    if(info.config_fingerprint != "random_hyperplane_rademacher_v2:dim=3:bits=16:seed=42") {
        return fail("random hyperplane encoder must expose a stable config fingerprint");
    }

    const auto previous_locale = std::locale();
    std::locale::global(std::locale(previous_locale, new GroupedNumpunct));
    const agent_memory::RandomHyperplaneBinaryEncoder locale_encoder(
        make_options(3000, 16000, 1000000)
    );
    std::locale::global(previous_locale);
    if(locale_encoder.info().config_fingerprint !=
       "random_hyperplane_rademacher_v2:dim=3000:bits=16000:seed=1000000") {
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
        return fail("random hyperplane encoder output must match the pinned v2 contract");
    }

    const auto batch = encoder->encode_batch({vector, agent_memory::Embedding{{-1.0F, 2.0F, -0.5F}}});
    if(batch.size() != 2 || batch.front() != signature) {
        return fail("batch encoding must preserve order and the single-vector contract");
    }
    if(!encoder->encode_batch({}).empty()) {
        return fail("empty batch encoding must return an empty result");
    }

    const auto sparse_signature = static_cast<agent_memory::RandomHyperplaneBinaryEncoder&>(
        *encoder
    ).encode_sparse(3, {
        {0, 1.0F},
        {1, -2.0F},
        {2, 0.5F},
    });
    if(sparse_signature != signature) {
        return fail("sparse encoding must match the equivalent dense input");
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
    if(!throws_invalid_argument([&] {
           (void)static_cast<agent_memory::RandomHyperplaneBinaryEncoder&>(*encoder)
               .encode_sparse(3, {{1, 1.0F}, {1, 2.0F}});
       })) {
        return fail("sparse encoding must reject duplicate indices");
    }

    const agent_memory::CoordinateSignBinaryEncoder coordinate_encoder(
        agent_memory::CoordinateSignBinaryEncoderOptions{4}
    );
    const auto& coordinate_info = coordinate_encoder.info();
    if(coordinate_info.encoder_id != "coordinate_sign"
       || coordinate_info.encoder_version != "v1"
       || coordinate_info.input_dimension != 4
       || coordinate_info.bit_count != 4
       || coordinate_info.seed != 0
       || coordinate_info.config_fingerprint != "coordinate_sign_v1:dim=4") {
        return fail("coordinate-sign encoder info must expose its fixed-width contract");
    }
    const auto coordinate_signature =
        coordinate_encoder.encode(agent_memory::Embedding{{1.0F, -2.0F, 0.0F, 3.0F}});
    if(coordinate_signature.bit_count() != 4
       || coordinate_signature.words().front() != 0x9ULL) {
        return fail("coordinate-sign encoder must set only strictly positive coordinate bits");
    }
    if(!throws_invalid_argument([] {
           (void)agent_memory::CoordinateSignBinaryEncoder(
               agent_memory::CoordinateSignBinaryEncoderOptions{0}
           );
       })) {
        return fail("coordinate-sign encoder construction must reject zero dimension");
    }
    if(!throws_invalid_argument([&] {
           (void)coordinate_encoder.encode(agent_memory::Embedding{{1.0F, 2.0F}});
       })) {
        return fail("coordinate-sign encoder must reject dimension mismatches");
    }
    if(!throws_invalid_argument([&] {
           (void)coordinate_encoder.encode(agent_memory::Embedding{{
               1.0F,
               std::numeric_limits<float>::quiet_NaN(),
               0.0F,
               3.0F,
           }});
       })) {
        return fail("coordinate-sign encoder must reject NaN input values");
    }

    const agent_memory::OrthogonalProjectionBinaryEncoder orthogonal_encoder(
        make_orthogonal_options(3, 10, 7)
    );
    const auto& orthogonal_info = orthogonal_encoder.info();
    if(orthogonal_info.encoder_id != "orthogonal_tight_frame_projection"
       || orthogonal_info.encoder_version != "v1"
       || orthogonal_info.input_dimension != 3
       || orthogonal_info.bit_count != 10
       || orthogonal_info.seed != 7
       || orthogonal_info.config_fingerprint !=
           "orthogonal_tight_frame_projection_v1:dim=3:padded_dim=4:bits=10:seed=7") {
        return fail("orthogonal projection encoder info must expose its projection contract");
    }
    if(orthogonal_encoder.padded_dimension() != 4
       || std::string_view{
              agent_memory::OrthogonalProjectionBinaryEncoder::compute_backend_name()
          } != "fwht_scalar") {
        return fail("orthogonal projection encoder must expose its padded transform backend");
    }
    const auto orthogonal_signature = orthogonal_encoder.encode(vector);
    const auto orthogonal_repeated = orthogonal_encoder.encode(vector);
    if(orthogonal_signature != orthogonal_repeated) {
        return fail("orthogonal projection encoder must be deterministic");
    }
    if(orthogonal_signature.bit_count() != 10 || orthogonal_signature.word_count() != 1) {
        return fail("orthogonal projection encoder must use the configured bit count");
    }
    const auto orthogonal_prefix =
        agent_memory::OrthogonalProjectionBinaryEncoder(
            make_orthogonal_options(3, 8, 7)
        ).encode(vector);
    if((orthogonal_signature.words().front() & 0xFFULL)
       != orthogonal_prefix.words().front()) {
        return fail("orthogonal projection shorter signatures must be prefix-stable");
    }
    const auto orthogonal_zero = orthogonal_encoder.encode(zero_vector);
    if(orthogonal_zero.words().front() != 0ULL) {
        return fail("orthogonal projection zero-vector ties must encode as zero bits");
    }
    const auto orthogonal_wide =
        agent_memory::OrthogonalProjectionBinaryEncoder(
            make_orthogonal_options(3, 70, 7)
        ).encode(vector);
    if(orthogonal_wide.bit_count() != 70 || orthogonal_wide.word_count() != 2) {
        return fail("orthogonal projection wide signatures must expose packed storage");
    }
    if((orthogonal_wide.words().back() & ~meaningful_tail_mask) != 0ULL) {
        return fail("orthogonal projection signatures must keep unused tail bits cleared");
    }
    if(!throws_invalid_argument([] {
           (void)agent_memory::OrthogonalProjectionBinaryEncoder(
               make_orthogonal_options(0, 16, 7)
           );
       })) {
        return fail("orthogonal projection construction must reject zero dimension");
    }
    if(!throws_invalid_argument([] {
           (void)agent_memory::OrthogonalProjectionBinaryEncoder(
               make_orthogonal_options(3, 0, 7)
           );
       })) {
        return fail("orthogonal projection construction must reject zero bit count");
    }
    if(!throws_invalid_argument([&] {
           (void)orthogonal_encoder.encode(agent_memory::Embedding{{1.0F, 2.0F}});
       })) {
        return fail("orthogonal projection encoder must reject dimension mismatches");
    }
    if(!throws_invalid_argument([&] {
           (void)orthogonal_encoder.encode(agent_memory::Embedding{{
               1.0F,
               -2.0F,
               std::numeric_limits<float>::infinity(),
           }});
       })) {
        return fail("orthogonal projection encoder must reject non-finite input values");
    }

    return 0;
}
