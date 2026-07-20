#include <agent_memory.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

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

    agent_memory::RandomizedHadamardBinaryEncoderOptions make_hadamard_options(
        std::size_t input_dimension,
        std::size_t bit_count,
        std::uint64_t seed
    ) {
        agent_memory::RandomizedHadamardBinaryEncoderOptions options;
        options.input_dimension = input_dimension;
        options.bit_count = bit_count;
        options.seed = seed;
        return options;
    }

    std::vector<std::vector<int>> materialize_effective_rows(
        const agent_memory::RandomizedHadamardBinaryEncoder& encoder,
        std::size_t input_dimension,
        std::size_t bit_count
    ) {
        std::vector<std::vector<int>> rows(bit_count, std::vector<int>(input_dimension));
        for(std::size_t coordinate = 0; coordinate < input_dimension; ++coordinate) {
            agent_memory::Embedding basis;
            basis.values.assign(input_dimension, 0.0F);
            basis.values[coordinate] = 1.0F;
            const auto signature = encoder.encode(basis);
            for(std::size_t bit = 0; bit < bit_count; ++bit) {
                rows[bit][coordinate] = signature.bit(bit) ? 1 : -1;
            }
        }
        return rows;
    }

    int dot_rows(
        const std::vector<std::vector<int>>& rows,
        std::size_t lhs,
        std::size_t rhs
    ) {
        int dot = 0;
        for(std::size_t coordinate = 0; coordinate < rows[lhs].size(); ++coordinate) {
            dot += rows[lhs][coordinate] * rows[rhs][coordinate];
        }
        return dot;
    }

    int dot_columns(
        const std::vector<std::vector<int>>& rows,
        std::size_t lhs,
        std::size_t rhs
    ) {
        int dot = 0;
        for(const auto& row : rows) {
            dot += row[lhs] * row[rhs];
        }
        return dot;
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

    const agent_memory::RandomizedHadamardBinaryEncoder hadamard_encoder(
        make_hadamard_options(3, 10, 7)
    );
    const auto& hadamard_info = hadamard_encoder.info();
    if(hadamard_info.encoder_id != "randomized_hadamard_projection"
       || hadamard_info.encoder_version != "v1"
       || hadamard_info.input_dimension != 3
       || hadamard_info.bit_count != 10
       || hadamard_info.seed != 7
       || hadamard_info.config_fingerprint !=
           "randomized_hadamard_projection_v1:dim=3:padded_dim=4:bits=10:seed=7") {
        return fail("randomized Hadamard encoder info must expose its projection contract");
    }
    if(hadamard_encoder.padded_dimension() != 4
       || std::string_view{
              agent_memory::RandomizedHadamardBinaryEncoder::compute_backend_name()
          } != "fwht_scalar") {
        return fail("randomized Hadamard encoder must expose its padded transform backend");
    }
    const auto hadamard_signature = hadamard_encoder.encode(vector);
    const auto hadamard_repeated = hadamard_encoder.encode(vector);
    if(hadamard_signature != hadamard_repeated) {
        return fail("randomized Hadamard encoder must be deterministic");
    }
    if(hadamard_signature.bit_count() != 10 || hadamard_signature.word_count() != 1) {
        return fail("randomized Hadamard encoder must use the configured bit count");
    }
    const auto hadamard_prefix =
        agent_memory::RandomizedHadamardBinaryEncoder(
            make_hadamard_options(3, 8, 7)
        ).encode(vector);
    if((hadamard_signature.words().front() & 0xFFULL)
       != hadamard_prefix.words().front()) {
        return fail("randomized Hadamard shorter signatures must be prefix-stable");
    }
    const auto hadamard_zero = hadamard_encoder.encode(zero_vector);
    if(hadamard_zero.words().front() != 0ULL) {
        return fail("randomized Hadamard zero-vector ties must encode as zero bits");
    }
    const auto hadamard_wide =
        agent_memory::RandomizedHadamardBinaryEncoder(
            make_hadamard_options(3, 70, 7)
        ).encode(vector);
    if(hadamard_wide.bit_count() != 70 || hadamard_wide.word_count() != 2) {
        return fail("randomized Hadamard wide signatures must expose packed storage");
    }
    if((hadamard_wide.words().back() & ~meaningful_tail_mask) != 0ULL) {
        return fail("randomized Hadamard signatures must keep unused tail bits cleared");
    }
    const agent_memory::RandomizedHadamardBinaryEncoder hadamard_seed_variant(
        make_hadamard_options(3, 70, 8)
    );
    if(hadamard_seed_variant.encode(vector) == hadamard_wide) {
        return fail("changing the randomized Hadamard seed must change output bits");
    }
    const auto hadamard_batch = hadamard_encoder.encode_batch({
        vector,
        zero_vector,
        agent_memory::Embedding{{-1.0F, 2.0F, -0.5F}},
    });
    if(hadamard_batch.size() != 3
       || hadamard_batch[0] != hadamard_encoder.encode(vector)
       || hadamard_batch[1] != hadamard_encoder.encode(zero_vector)
       || hadamard_batch[2] !=
           hadamard_encoder.encode(agent_memory::Embedding{{-1.0F, 2.0F, -0.5F}})) {
        return fail("randomized Hadamard batch encoding must match sequential encode");
    }

    const agent_memory::RandomizedHadamardBinaryEncoder power_of_two_hadamard(
        make_hadamard_options(4, 4, 9)
    );
    const auto power_of_two_rows =
        materialize_effective_rows(power_of_two_hadamard, 4, 4);
    for(std::size_t lhs = 0; lhs < 4; ++lhs) {
        for(std::size_t rhs = 0; rhs < 4; ++rhs) {
            const auto expected = lhs == rhs ? 4 : 0;
            if(dot_rows(power_of_two_rows, lhs, rhs) != expected) {
                return fail(
                    "randomized Hadamard rows must be orthogonal for power-of-two input"
                );
            }
        }
    }

    const agent_memory::RandomizedHadamardBinaryEncoder padded_hadamard(
        make_hadamard_options(3, 4, 9)
    );
    const auto padded_rows = materialize_effective_rows(padded_hadamard, 3, 4);
    for(std::size_t lhs = 0; lhs < 3; ++lhs) {
        for(std::size_t rhs = 0; rhs < 3; ++rhs) {
            const auto expected = lhs == rhs ? 4 : 0;
            if(dot_columns(padded_rows, lhs, rhs) != expected) {
                return fail(
                    "a full padded randomized Hadamard block must be a tight frame"
                );
            }
        }
    }
    bool saw_non_orthogonal_truncated_rows = false;
    for(std::size_t lhs = 0; lhs < 4; ++lhs) {
        for(std::size_t rhs = lhs + 1; rhs < 4; ++rhs) {
            saw_non_orthogonal_truncated_rows =
                saw_non_orthogonal_truncated_rows
                || dot_rows(padded_rows, lhs, rhs) != 0;
        }
    }
    if(!saw_non_orthogonal_truncated_rows) {
        return fail(
            "non-power-of-two randomized Hadamard rows must not be assumed orthogonal"
        );
    }
    if(!throws_invalid_argument([] {
           (void)agent_memory::RandomizedHadamardBinaryEncoder(
               make_hadamard_options(0, 16, 7)
           );
       })) {
        return fail("randomized Hadamard construction must reject zero dimension");
    }
    if(!throws_invalid_argument([] {
           (void)agent_memory::RandomizedHadamardBinaryEncoder(
               make_hadamard_options(3, 0, 7)
           );
       })) {
        return fail("randomized Hadamard construction must reject zero bit count");
    }
    if(!throws_invalid_argument([&] {
           (void)hadamard_encoder.encode(agent_memory::Embedding{{1.0F, 2.0F}});
       })) {
        return fail("randomized Hadamard encoder must reject dimension mismatches");
    }
    if(!throws_invalid_argument([&] {
           (void)hadamard_encoder.encode(agent_memory::Embedding{{
               1.0F,
               -2.0F,
               std::numeric_limits<float>::infinity(),
           }});
       })) {
        return fail("randomized Hadamard encoder must reject non-finite input values");
    }

    return 0;
}
