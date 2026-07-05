#include "SourceKind.hpp"

#include <array>
#include <cctype>
#include <string>

namespace agent_memory {

    namespace {

        struct SourceKindName final {
            SourceKind kind = SourceKind::Unknown;
            std::string_view name;
        };

        constexpr std::array<SourceKindName, 8> SOURCE_KIND_NAMES{{
            {SourceKind::Unknown, "unknown"},
            {SourceKind::Text, "text"},
            {SourceKind::Markdown, "markdown"},
            {SourceKind::Chat, "chat"},
            {SourceKind::Code, "code"},
            {SourceKind::Structured, "structured"},
            {SourceKind::Event, "event"},
            {SourceKind::Custom, "custom"}
        }};

        std::string lowercase_ascii(std::string_view text) {
            std::string result;
            result.reserve(text.size());
            for(const unsigned char c : text) {
                result.push_back(static_cast<char>(std::tolower(c)));
            }
            return result;
        }

    } // namespace

    std::string_view to_string(SourceKind kind) noexcept {
        for(const auto& item : SOURCE_KIND_NAMES) {
            if(item.kind == kind) {
                return item.name;
            }
        }
        return "unknown";
    }

    bool parse_source_kind(std::string_view text, SourceKind& kind) {
        const auto normalized = lowercase_ascii(text);
        for(const auto& item : SOURCE_KIND_NAMES) {
            if(normalized == item.name) {
                kind = item.kind;
                return true;
            }
        }
        return false;
    }

} // namespace agent_memory
