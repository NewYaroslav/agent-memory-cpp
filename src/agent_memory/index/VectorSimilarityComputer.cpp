#include "VectorSimilarityComputer.hpp"

#include <agent_memory/embedding/embedding_types.hpp>

#include <array>
#include <cstddef>
#include <stdexcept>

#if (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#define AGENT_MEMORY_HAS_GNU_X86_VECTOR_INTRINSICS 1
#define AGENT_MEMORY_HAS_SSE2_VECTOR_BACKEND 1
#else
#define AGENT_MEMORY_HAS_GNU_X86_VECTOR_INTRINSICS 0
#endif

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#include <immintrin.h>
#define AGENT_MEMORY_HAS_MSVC_X64_VECTOR_INTRINSICS 1
#define AGENT_MEMORY_HAS_SSE2_VECTOR_BACKEND 1
#else
#define AGENT_MEMORY_HAS_MSVC_X64_VECTOR_INTRINSICS 0
#endif

#ifndef AGENT_MEMORY_HAS_SSE2_VECTOR_BACKEND
#define AGENT_MEMORY_HAS_SSE2_VECTOR_BACKEND 0
#endif

namespace agent_memory {
    namespace {

        using BinaryKernel = float (*)(const float*, const float*, std::size_t) noexcept;

        [[nodiscard]] float dot_product_scalar(
            const float* lhs,
            const float* rhs,
            std::size_t size
        ) noexcept {
            std::array<float, 8> sums{};
            std::size_t index = 0;
            for(; index + sums.size() <= size; index += sums.size()) {
                for(std::size_t lane = 0; lane < sums.size(); ++lane) {
                    sums[lane] += lhs[index + lane] * rhs[index + lane];
                }
            }

            float result = 0.0F;
            for(const auto sum : sums) {
                result += sum;
            }
            for(; index < size; ++index) {
                result += lhs[index] * rhs[index];
            }
            return result;
        }

        [[nodiscard]] float negative_squared_distance_scalar(
            const float* lhs,
            const float* rhs,
            std::size_t size
        ) noexcept {
            float distance = 0.0F;
            for(std::size_t index = 0; index < size; ++index) {
                const auto delta = lhs[index] - rhs[index];
                distance += delta * delta;
            }
            return -distance;
        }

#if AGENT_MEMORY_HAS_GNU_X86_VECTOR_INTRINSICS
#define AGENT_MEMORY_SSE2_TARGET __attribute__((target("sse2")))
#define AGENT_MEMORY_AVX2_TARGET __attribute__((target("avx2")))
#else
#define AGENT_MEMORY_SSE2_TARGET
#define AGENT_MEMORY_AVX2_TARGET
#endif

#if AGENT_MEMORY_HAS_SSE2_VECTOR_BACKEND
        [[nodiscard]] AGENT_MEMORY_SSE2_TARGET float dot_product_sse2(
            const float* lhs,
            const float* rhs,
            std::size_t size
        ) noexcept {
            auto low_sums = _mm_setzero_ps();
            auto high_sums = _mm_setzero_ps();
            std::size_t index = 0;
            for(; index + 8 <= size; index += 8) {
                low_sums = _mm_add_ps(
                    low_sums,
                    _mm_mul_ps(_mm_loadu_ps(lhs + index), _mm_loadu_ps(rhs + index))
                );
                high_sums = _mm_add_ps(
                    high_sums,
                    _mm_mul_ps(
                        _mm_loadu_ps(lhs + index + 4),
                        _mm_loadu_ps(rhs + index + 4)
                    )
                );
            }

            alignas(16) float lanes[8]{};
            _mm_store_ps(lanes, low_sums);
            _mm_store_ps(lanes + 4, high_sums);
            float result = 0.0F;
            for(const auto lane : lanes) {
                result += lane;
            }
            for(; index < size; ++index) {
                result += lhs[index] * rhs[index];
            }
            return result;
        }

        [[nodiscard]] AGENT_MEMORY_SSE2_TARGET float negative_squared_distance_sse2(
            const float* lhs,
            const float* rhs,
            std::size_t size
        ) noexcept {
            auto sums = _mm_setzero_ps();
            std::size_t index = 0;
            for(; index + 4 <= size; index += 4) {
                const auto delta = _mm_sub_ps(
                    _mm_loadu_ps(lhs + index),
                    _mm_loadu_ps(rhs + index)
                );
                sums = _mm_add_ps(sums, _mm_mul_ps(delta, delta));
            }

            alignas(16) float lanes[4]{};
            _mm_store_ps(lanes, sums);
            float distance = lanes[0] + lanes[1] + lanes[2] + lanes[3];
            for(; index < size; ++index) {
                const auto delta = lhs[index] - rhs[index];
                distance += delta * delta;
            }
            return -distance;
        }
#endif

#if AGENT_MEMORY_HAS_GNU_X86_VECTOR_INTRINSICS
        [[nodiscard]] AGENT_MEMORY_AVX2_TARGET float dot_product_avx2(
            const float* lhs,
            const float* rhs,
            std::size_t size
        ) noexcept {
            auto sums = _mm256_setzero_ps();
            std::size_t index = 0;
            for(; index + 8 <= size; index += 8) {
                const auto lhs_values = _mm256_loadu_ps(lhs + index);
                const auto rhs_values = _mm256_loadu_ps(rhs + index);
                sums = _mm256_add_ps(sums, _mm256_mul_ps(lhs_values, rhs_values));
            }

            alignas(32) float lanes[8]{};
            _mm256_store_ps(lanes, sums);
            float result = 0.0F;
            for(const auto lane : lanes) {
                result += lane;
            }
            for(; index < size; ++index) {
                result += lhs[index] * rhs[index];
            }
            return result;
        }

        [[nodiscard]] AGENT_MEMORY_AVX2_TARGET float negative_squared_distance_avx2(
            const float* lhs,
            const float* rhs,
            std::size_t size
        ) noexcept {
            auto sums = _mm256_setzero_ps();
            std::size_t index = 0;
            for(; index + 8 <= size; index += 8) {
                const auto delta = _mm256_sub_ps(
                    _mm256_loadu_ps(lhs + index),
                    _mm256_loadu_ps(rhs + index)
                );
                sums = _mm256_add_ps(sums, _mm256_mul_ps(delta, delta));
            }

            alignas(32) float lanes[8]{};
            _mm256_store_ps(lanes, sums);
            float distance = 0.0F;
            for(const auto lane : lanes) {
                distance += lane;
            }
            for(; index < size; ++index) {
                const auto delta = lhs[index] - rhs[index];
                distance += delta * delta;
            }
            return -distance;
        }

        [[nodiscard]] bool runtime_supports_avx2() noexcept {
            __builtin_cpu_init();
            return __builtin_cpu_supports("avx2") != 0;
        }

        [[nodiscard]] bool runtime_supports_sse2() noexcept {
            __builtin_cpu_init();
            return __builtin_cpu_supports("sse2") != 0;
        }
#endif

        [[nodiscard]] VectorSimilarityBackend select_backend(bool enable_simd) noexcept {
            if(!enable_simd) {
                return VectorSimilarityBackend::Scalar;
            }
#if AGENT_MEMORY_HAS_GNU_X86_VECTOR_INTRINSICS
            if(runtime_supports_avx2()) {
                return VectorSimilarityBackend::Avx2;
            }
            if(runtime_supports_sse2()) {
                return VectorSimilarityBackend::Sse2;
            }
#elif AGENT_MEMORY_HAS_MSVC_X64_VECTOR_INTRINSICS
            return VectorSimilarityBackend::Sse2;
#endif
            return VectorSimilarityBackend::Scalar;
        }

        [[nodiscard]] BinaryKernel dot_product_kernel(
            VectorSimilarityBackend backend
        ) noexcept {
            switch(backend) {
#if AGENT_MEMORY_HAS_GNU_X86_VECTOR_INTRINSICS
                case VectorSimilarityBackend::Avx2:
                    return dot_product_avx2;
#endif
#if AGENT_MEMORY_HAS_SSE2_VECTOR_BACKEND
                case VectorSimilarityBackend::Sse2:
                    return dot_product_sse2;
#endif
                case VectorSimilarityBackend::Scalar:
                    return dot_product_scalar;
                default:
                    return dot_product_scalar;
            }
        }

        [[nodiscard]] BinaryKernel negative_squared_distance_kernel(
            VectorSimilarityBackend backend
        ) noexcept {
            switch(backend) {
#if AGENT_MEMORY_HAS_GNU_X86_VECTOR_INTRINSICS
                case VectorSimilarityBackend::Avx2:
                    return negative_squared_distance_avx2;
#endif
#if AGENT_MEMORY_HAS_SSE2_VECTOR_BACKEND
                case VectorSimilarityBackend::Sse2:
                    return negative_squared_distance_sse2;
#endif
                case VectorSimilarityBackend::Scalar:
                    return negative_squared_distance_scalar;
                default:
                    return negative_squared_distance_scalar;
            }
        }

        void require_equal_dimensions(const Embedding& lhs, const Embedding& rhs) {
            if(lhs.dimension() != rhs.dimension()) {
                throw std::invalid_argument(
                    "vector similarity requires equal-width embeddings"
                );
            }
        }

    } // namespace

    std::string_view vector_similarity_backend_name(
        VectorSimilarityBackend backend
    ) noexcept {
        switch(backend) {
            case VectorSimilarityBackend::Scalar:
                return "scalar";
            case VectorSimilarityBackend::Sse2:
                return "sse2";
            case VectorSimilarityBackend::Avx2:
                return "avx2";
        }
        return "unknown";
    }

    VectorSimilarityComputer::VectorSimilarityComputer(bool enable_simd) noexcept
        : m_backend(select_backend(enable_simd)) {}

    VectorSimilarityBackend VectorSimilarityComputer::backend() const noexcept {
        return m_backend;
    }

    float VectorSimilarityComputer::dot_product(
        const Embedding& lhs,
        const Embedding& rhs
    ) const {
        require_equal_dimensions(lhs, rhs);
        return dot_product_values(
            lhs.values.data(),
            rhs.values.data(),
            lhs.values.size()
        );
    }

    float VectorSimilarityComputer::dot_product_values(
        const float* lhs,
        const float* rhs,
        std::size_t size
    ) const noexcept {
        return dot_product_kernel(m_backend)(lhs, rhs, size);
    }

    float VectorSimilarityComputer::squared_norm(const Embedding& embedding) const noexcept {
        return dot_product_kernel(m_backend)(
            embedding.values.data(),
            embedding.values.data(),
            embedding.values.size()
        );
    }

    float VectorSimilarityComputer::negative_squared_distance(
        const Embedding& lhs,
        const Embedding& rhs
    ) const {
        require_equal_dimensions(lhs, rhs);
        return negative_squared_distance_kernel(m_backend)(
            lhs.values.data(),
            rhs.values.data(),
            lhs.values.size()
        );
    }

} // namespace agent_memory
