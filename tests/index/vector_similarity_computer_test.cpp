#include <agent_memory/index.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    [[nodiscard]] bool almost_equal(float lhs, float rhs) noexcept {
        const auto scale = std::max({1.0F, std::fabs(lhs), std::fabs(rhs)});
        return std::fabs(lhs - rhs) <= 1.0e-5F * scale;
    }

    template <typename Fn>
    [[nodiscard]] bool throws_invalid_argument(Fn&& fn) {
        try {
            fn();
        } catch(const std::invalid_argument&) {
            return true;
        }
        return false;
    }

} // namespace

int main() {
    const agent_memory::VectorSimilarityComputer scalar(false);
    const agent_memory::VectorSimilarityComputer automatic;

    if(scalar.backend() != agent_memory::VectorSimilarityBackend::Scalar) {
        return fail("disabled SIMD must select the scalar vector backend");
    }
    if(agent_memory::vector_similarity_backend_name(automatic.backend()).empty()) {
        return fail("automatic vector backend must expose a diagnostic name");
    }

    const agent_memory::Embedding lhs{{
        1.0F, -2.0F, 3.0F, -4.0F, 5.0F, -6.0F, 7.0F,
        -8.0F, 9.0F, -10.0F, 11.0F, -12.0F, 13.0F
    }};
    const agent_memory::Embedding rhs{{
        0.5F, 1.5F, -2.5F, 3.5F, -4.5F, 5.5F, -6.5F,
        7.5F, -8.5F, 9.5F, -10.5F, 11.5F, -12.5F
    }};

    const auto expected_dot = scalar.dot_product(lhs, rhs);
    const auto expected_norm = scalar.squared_norm(lhs);
    const auto expected_distance = scalar.negative_squared_distance(lhs, rhs);
    if(!almost_equal(automatic.dot_product(lhs, rhs), expected_dot)) {
        return fail("SIMD dot product must match the scalar tail-aware reference");
    }
    if(!almost_equal(automatic.squared_norm(lhs), expected_norm)) {
        return fail("SIMD squared norm must match the scalar tail-aware reference");
    }
    if(!almost_equal(
        automatic.negative_squared_distance(lhs, rhs),
        expected_distance
    )) {
        return fail("SIMD squared distance must match the scalar tail-aware reference");
    }

    const agent_memory::Embedding mismatched{{1.0F, 2.0F}};
    if(!throws_invalid_argument([&automatic, &lhs, &mismatched] {
        (void)automatic.dot_product(lhs, mismatched);
    })) {
        return fail("dot product must reject dimension mismatches");
    }
    if(!throws_invalid_argument([&automatic, &lhs, &mismatched] {
        (void)automatic.negative_squared_distance(lhs, mismatched);
    })) {
        return fail("squared distance must reject dimension mismatches");
    }

    return 0;
}
