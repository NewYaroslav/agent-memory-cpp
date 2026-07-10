#include "enums.hpp"

#include <array>
#include <cctype>
#include <string>

namespace agent_memory {

    namespace {

        template <typename Enum>
        struct EnumName final {
            Enum value;
            std::string_view name;
        };

        constexpr std::array<EnumName<EmbeddingPurpose>, 5> EMBEDDING_PURPOSE_NAMES{{
            {EmbeddingPurpose::Query, "query"},
            {EmbeddingPurpose::Document, "document"},
            {EmbeddingPurpose::Symmetric, "symmetric"},
            {EmbeddingPurpose::Classification, "classification"},
            {EmbeddingPurpose::Custom, "custom"}
        }};

        constexpr std::array<EnumName<SimilarityMetric>, 3> SIMILARITY_METRIC_NAMES{{
            {SimilarityMetric::Cosine, "cosine"},
            {SimilarityMetric::DotProduct, "dot_product"},
            {SimilarityMetric::Euclidean, "euclidean"}
        }};

        constexpr std::array<EnumName<PoolingMode>, 5> POOLING_MODE_NAMES{{
            {PoolingMode::ModelDefault, "model_default"},
            {PoolingMode::Mean, "mean"},
            {PoolingMode::Cls, "cls"},
            {PoolingMode::LastToken, "last_token"},
            {PoolingMode::Custom, "custom"}
        }};

        std::string lowercase_ascii(std::string_view text) {
            std::string result;
            result.reserve(text.size());
            for(const unsigned char c : text) {
                result.push_back(static_cast<char>(std::tolower(c)));
            }
            return result;
        }

        template <typename Enum, std::size_t Size>
        std::string_view enum_to_string(
            Enum value,
            const std::array<EnumName<Enum>, Size>& names,
            std::string_view fallback
        ) noexcept {
            for(const auto& item : names) {
                if(item.value == value) {
                    return item.name;
                }
            }
            return fallback;
        }

        template <typename Enum, std::size_t Size>
        ParseResult<Enum> parse_enum(
            std::string_view text,
            const std::array<EnumName<Enum>, Size>& names
        ) noexcept {
            const auto normalized = lowercase_ascii(text);
            for(const auto& item : names) {
                if(normalized == item.name) {
                    return ParseResult<Enum>{true, item.value};
                }
            }
            return ParseResult<Enum>{};
        }

    } // namespace

    std::string_view to_string(EmbeddingPurpose purpose) noexcept {
        return enum_to_string(purpose, EMBEDDING_PURPOSE_NAMES, "custom");
    }

    std::string_view to_string(SimilarityMetric metric) noexcept {
        return enum_to_string(metric, SIMILARITY_METRIC_NAMES, "cosine");
    }

    std::string_view to_string(PoolingMode mode) noexcept {
        return enum_to_string(mode, POOLING_MODE_NAMES, "model_default");
    }

    template <>
    ParseResult<EmbeddingPurpose> to_enum<EmbeddingPurpose>(
        std::string_view text
    ) noexcept {
        return parse_enum(text, EMBEDDING_PURPOSE_NAMES);
    }

    template <>
    ParseResult<SimilarityMetric> to_enum<SimilarityMetric>(
        std::string_view text
    ) noexcept {
        return parse_enum(text, SIMILARITY_METRIC_NAMES);
    }

    template <>
    ParseResult<PoolingMode> to_enum<PoolingMode>(
        std::string_view text
    ) noexcept {
        return parse_enum(text, POOLING_MODE_NAMES);
    }

} // namespace agent_memory
