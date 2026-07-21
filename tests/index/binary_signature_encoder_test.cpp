#include <agent_memory.hpp>

#include <cmath>
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

    agent_memory::LearnedProjectionTrainingOptions make_learned_options(
        std::size_t input_dimension,
        std::size_t bit_count,
        std::uint64_t seed
    ) {
        agent_memory::LearnedProjectionTrainingOptions options;
        options.input_dimension = input_dimension;
        options.bit_count = bit_count;
        options.seed = seed;
        options.max_training_vectors = 0;
        return options;
    }

    agent_memory::PcaProjectionTrainingOptions make_pca_options(
        std::size_t input_dimension,
        std::size_t bit_count,
        std::uint64_t seed
    ) {
        agent_memory::PcaProjectionTrainingOptions options;
        options.input_dimension = input_dimension;
        options.bit_count = bit_count;
        options.seed = seed;
        options.power_iterations = 16;
        options.max_training_vectors = 0;
        return options;
    }

    bool almost_equal(double lhs, double rhs, double epsilon = 1.0e-5) {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    double projection_row_dot(
        const agent_memory::PcaProjectionBinaryEncoderOptions& artifact,
        std::size_t lhs,
        std::size_t rhs
    ) {
        const auto* lhs_row =
            artifact.projection_rows.data() + lhs * artifact.input_dimension;
        const auto* rhs_row =
            artifact.projection_rows.data() + rhs * artifact.input_dimension;
        double value = 0.0;
        for(std::size_t dimension = 0; dimension < artifact.input_dimension;
            ++dimension) {
            value += static_cast<double>(lhs_row[dimension])
                * static_cast<double>(rhs_row[dimension]);
        }
        return value;
    }

    double projection_row_norm(
        const agent_memory::PcaProjectionBinaryEncoderOptions& artifact,
        std::size_t row
    ) {
        return std::sqrt(projection_row_dot(artifact, row, row));
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

    const std::vector<agent_memory::Embedding> learned_training{
        agent_memory::Embedding{{1.0F, 0.0F}},
        agent_memory::Embedding{{0.0F, 1.0F}},
        agent_memory::Embedding{{-1.0F, 0.0F}},
        agent_memory::Embedding{{0.0F, -1.0F}},
    };
    const auto learned_artifact = agent_memory::train_learned_projection_encoder(
        learned_training,
        make_learned_options(2, 8, 123)
    );
    if(learned_artifact.input_dimension != 2
       || learned_artifact.bit_count != 8
       || learned_artifact.seed != 123
       || learned_artifact.training_vector_count != learned_training.size()
       || learned_artifact.projection_rows.size() != 16
       || learned_artifact.thresholds.size() != 8) {
        return fail("learned projection training must produce a complete artifact");
    }
    for(std::size_t bit = 0; bit < learned_artifact.bit_count; ++bit) {
        const auto row = learned_artifact.projection_rows.data()
            + bit * learned_artifact.input_dimension;
        const auto norm =
            std::sqrt(static_cast<double>(row[0]) * row[0]
                      + static_cast<double>(row[1]) * row[1]);
        if(!almost_equal(norm, 1.0)) {
            return fail("learned projection rows must be normalized");
        }
        if(!std::isfinite(learned_artifact.thresholds[bit])) {
            return fail("learned projection thresholds must be finite");
        }
    }

    const agent_memory::LearnedProjectionBinaryEncoder learned_encoder(
        learned_artifact
    );
    const auto& learned_info = learned_encoder.info();
    if(learned_info.encoder_id != "learned_pair_difference_projection"
       || learned_info.encoder_version != "v1"
       || learned_info.input_dimension != 2
       || learned_info.bit_count != 8
       || learned_info.seed != 123) {
        return fail("learned projection encoder info must mirror the artifact");
    }
    const std::string learned_prefix =
        "learned_pair_difference_projection_v1:dim=2:bits=8:seed=123:train=4:artifact=";
    if(learned_info.config_fingerprint.compare(
           0,
           learned_prefix.size(),
           learned_prefix
       ) != 0
       || learned_info.config_fingerprint.size() != learned_prefix.size() + 16U) {
        return fail("learned projection fingerprint must include artifact identity");
    }

    const auto learned_signature =
        learned_encoder.encode(agent_memory::Embedding{{0.75F, -0.25F}});
    if(learned_signature !=
       learned_encoder.encode(agent_memory::Embedding{{0.75F, -0.25F}})) {
        return fail("learned projection encoder must be deterministic");
    }
    if(learned_signature.bit_count() != 8 || learned_signature.word_count() != 1) {
        return fail("learned projection signature must use the configured width");
    }
    const auto learned_batch = learned_encoder.encode_batch({
        agent_memory::Embedding{{0.75F, -0.25F}},
        agent_memory::Embedding{{-0.75F, 0.25F}},
    });
    if(learned_batch.size() != 2 || learned_batch.front() != learned_signature) {
        return fail("learned projection batch encoding must match single encode");
    }

    const auto learned_seed_variant = agent_memory::train_learned_projection_encoder(
        learned_training,
        make_learned_options(2, 8, 124)
    );
    const agent_memory::LearnedProjectionBinaryEncoder learned_variant_encoder(
        learned_seed_variant
    );
    if(learned_variant_encoder.info().config_fingerprint
       == learned_encoder.info().config_fingerprint) {
        return fail("learned projection seed must affect artifact identity");
    }

    if(!throws_invalid_argument([] {
           (void)agent_memory::train_learned_projection_encoder(
               std::vector<agent_memory::Embedding>{
                   agent_memory::Embedding{{1.0F, 0.0F}},
               },
               make_learned_options(2, 8, 123)
           );
       })) {
        return fail("learned projection training must reject fewer than two vectors");
    }
    if(!throws_invalid_argument([&] {
           auto limited_options = make_learned_options(2, 8, 123);
           limited_options.max_training_vectors = 1;
           (void)agent_memory::train_learned_projection_encoder(
               learned_training,
               limited_options
           );
       })) {
        return fail("learned projection training must reject one-vector samples");
    }
    if(!throws_invalid_argument([] {
           (void)agent_memory::train_learned_projection_encoder(
               std::vector<agent_memory::Embedding>{
                   agent_memory::Embedding{{1.0F, 0.0F}},
                   agent_memory::Embedding{{1.0F}},
               },
               make_learned_options(2, 8, 123)
           );
       })) {
        return fail("learned projection training must reject dimension mismatches");
    }
    if(!throws_invalid_argument([] {
           (void)agent_memory::train_learned_projection_encoder(
               std::vector<agent_memory::Embedding>{
                   agent_memory::Embedding{{1.0F, 0.0F}},
                   agent_memory::Embedding{{
                       0.0F,
                       std::numeric_limits<float>::quiet_NaN(),
                   }},
               },
               make_learned_options(2, 8, 123)
           );
       })) {
        return fail("learned projection training must reject non-finite values");
    }
    if(!throws_invalid_argument([&] {
           auto invalid_artifact = learned_artifact;
           invalid_artifact.projection_rows.pop_back();
           (void)agent_memory::LearnedProjectionBinaryEncoder(invalid_artifact);
       })) {
        return fail("learned projection encoder must reject matrix size mismatch");
    }
    if(!throws_invalid_argument([&] {
           auto invalid_artifact = learned_artifact;
           invalid_artifact.thresholds.front() =
               std::numeric_limits<float>::infinity();
           (void)agent_memory::LearnedProjectionBinaryEncoder(invalid_artifact);
       })) {
        return fail("learned projection encoder must reject non-finite thresholds");
    }
    if(!throws_invalid_argument([&] {
           (void)learned_encoder.encode(agent_memory::Embedding{{1.0F, 2.0F, 3.0F}});
       })) {
        return fail("learned projection encoder must reject dimension mismatches");
    }
    if(!throws_invalid_argument([&] {
           (void)learned_encoder.encode(agent_memory::Embedding{{
               1.0F,
               std::numeric_limits<float>::quiet_NaN(),
           }});
       })) {
        return fail("learned projection encoder must reject NaN input values");
    }

    const std::vector<agent_memory::Embedding> pca_training{
        agent_memory::Embedding{{3.0F, 0.1F}},
        agent_memory::Embedding{{2.0F, -0.1F}},
        agent_memory::Embedding{{1.0F, 0.0F}},
        agent_memory::Embedding{{-1.0F, 0.0F}},
        agent_memory::Embedding{{-2.0F, 0.1F}},
        agent_memory::Embedding{{-3.0F, -0.1F}},
    };
    const auto pca_artifact = agent_memory::train_pca_projection_encoder(
        pca_training,
        make_pca_options(2, 2, 321)
    );
    if(pca_artifact.input_dimension != 2
       || pca_artifact.bit_count != 2
       || pca_artifact.seed != 321
       || pca_artifact.training_vector_count != pca_training.size()
       || pca_artifact.power_iterations != 16
       || pca_artifact.mean.size() != 2
       || pca_artifact.projection_rows.size() != 4
       || pca_artifact.thresholds.size() != 2) {
        return fail("PCA projection training must produce a complete artifact");
    }
    const auto first_axis_x = std::fabs(pca_artifact.projection_rows[0]);
    const auto first_axis_y = std::fabs(pca_artifact.projection_rows[1]);
    if(first_axis_x < 0.95 || first_axis_y > 0.35) {
        return fail("PCA projection first row must follow the dominant variance axis");
    }
    for(std::size_t bit = 0; bit < pca_artifact.bit_count; ++bit) {
        if(!almost_equal(projection_row_norm(pca_artifact, bit), 1.0)) {
            return fail("PCA projection rows must be normalized");
        }
        if(!std::isfinite(pca_artifact.thresholds[bit])) {
            return fail("PCA projection thresholds must be finite");
        }
    }
    if(std::fabs(projection_row_dot(pca_artifact, 0, 1)) > 1.0e-5) {
        return fail("PCA projection rows must be mutually orthogonal");
    }

    const agent_memory::PcaProjectionBinaryEncoder pca_encoder(pca_artifact);
    const auto& pca_info = pca_encoder.info();
    if(pca_info.encoder_id != "pca_projection"
       || pca_info.encoder_version != "v1"
       || pca_info.input_dimension != 2
       || pca_info.bit_count != 2
       || pca_info.seed != 321) {
        return fail("PCA projection encoder info must mirror the artifact");
    }
    const std::string pca_prefix =
        "pca_projection_v1:dim=2:bits=2:seed=321:train=6:iters=16:artifact=";
    if(pca_info.config_fingerprint.compare(0, pca_prefix.size(), pca_prefix) != 0
       || pca_info.config_fingerprint.size() != pca_prefix.size() + 16U) {
        return fail("PCA projection fingerprint must include artifact identity");
    }

    const auto pca_signature =
        pca_encoder.encode(agent_memory::Embedding{{2.5F, 0.0F}});
    if(pca_signature != pca_encoder.encode(agent_memory::Embedding{{2.5F, 0.0F}})) {
        return fail("PCA projection encoder must be deterministic");
    }
    if(pca_signature.bit_count() != 2 || pca_signature.word_count() != 1) {
        return fail("PCA projection signature must use the configured width");
    }
    const auto pca_batch = pca_encoder.encode_batch({
        agent_memory::Embedding{{2.5F, 0.0F}},
        agent_memory::Embedding{{-2.5F, 0.0F}},
    });
    if(pca_batch.size() != 2 || pca_batch.front() != pca_signature) {
        return fail("PCA projection batch encoding must match single encode");
    }

    const auto pca_seed_variant = agent_memory::train_pca_projection_encoder(
        pca_training,
        make_pca_options(2, 2, 322)
    );
    const agent_memory::PcaProjectionBinaryEncoder pca_variant_encoder(
        pca_seed_variant
    );
    if(pca_variant_encoder.info().config_fingerprint
       == pca_encoder.info().config_fingerprint) {
        return fail("PCA projection seed must affect artifact identity");
    }

    const std::vector<agent_memory::Embedding> identical_pca_training{
        agent_memory::Embedding{{1.0F, 1.0F}},
        agent_memory::Embedding{{1.0F, 1.0F}},
        agent_memory::Embedding{{1.0F, 1.0F}},
    };
    const auto zero_covariance_pca_artifact =
        agent_memory::train_pca_projection_encoder(
            identical_pca_training,
            make_pca_options(2, 2, 0)
        );
    for(std::size_t bit = 0; bit < zero_covariance_pca_artifact.bit_count; ++bit) {
        if(!almost_equal(projection_row_norm(zero_covariance_pca_artifact, bit), 1.0)) {
            return fail("PCA fallback rows must remain normalized");
        }
    }
    for(std::size_t lhs = 0; lhs < zero_covariance_pca_artifact.bit_count; ++lhs) {
        for(std::size_t rhs = lhs + 1; rhs < zero_covariance_pca_artifact.bit_count;
            ++rhs) {
            if(std::fabs(
                   projection_row_dot(zero_covariance_pca_artifact, lhs, rhs)
               ) > 1.0e-5) {
                return fail(
                    "PCA fallback rows must remain orthogonal for zero covariance"
                );
            }
        }
    }

    if(!throws_invalid_argument([] {
           (void)agent_memory::train_pca_projection_encoder(
               std::vector<agent_memory::Embedding>{
                   agent_memory::Embedding{{1.0F, 0.0F}},
                   agent_memory::Embedding{{-1.0F, 0.0F}},
               },
               make_pca_options(2, 3, 321)
           );
       })) {
        return fail("PCA projection training must reject bit_count > dimension");
    }
    if(!throws_invalid_argument([] {
           auto options = make_pca_options(2, 2, 321);
           options.power_iterations = 0;
           (void)agent_memory::train_pca_projection_encoder(
               std::vector<agent_memory::Embedding>{
                   agent_memory::Embedding{{1.0F, 0.0F}},
                   agent_memory::Embedding{{-1.0F, 0.0F}},
               },
               options
           );
       })) {
        return fail("PCA projection training must reject zero power iterations");
    }
    if(!throws_invalid_argument([&] {
           auto options = make_pca_options(2, 2, 321);
           options.max_training_vectors = 1;
           (void)agent_memory::train_pca_projection_encoder(pca_training, options);
       })) {
        return fail("PCA projection training must reject one-vector samples");
    }
    if(!throws_invalid_argument([&] {
           auto invalid_artifact = pca_artifact;
           invalid_artifact.mean.pop_back();
           (void)agent_memory::PcaProjectionBinaryEncoder(invalid_artifact);
       })) {
        return fail("PCA projection encoder must reject mean size mismatch");
    }
    if(!throws_invalid_argument([&] {
           auto invalid_artifact = pca_artifact;
           invalid_artifact.projection_rows.pop_back();
           (void)agent_memory::PcaProjectionBinaryEncoder(invalid_artifact);
       })) {
        return fail("PCA projection encoder must reject matrix size mismatch");
    }
    if(!throws_invalid_argument([&] {
           (void)pca_encoder.encode(agent_memory::Embedding{{1.0F, 2.0F, 3.0F}});
       })) {
        return fail("PCA projection encoder must reject dimension mismatches");
    }
    if(!throws_invalid_argument([&] {
           (void)pca_encoder.encode(agent_memory::Embedding{{
               1.0F,
               std::numeric_limits<float>::quiet_NaN(),
           }});
       })) {
        return fail("PCA projection encoder must reject NaN input values");
    }

    return 0;
}
