#include "Document.hpp"

namespace agent_memory {

    bool is_empty(const TextRange& range) noexcept {
        return range.length == 0;
    }

    bool has_content(const Document& document) noexcept {
        return !document.text.empty();
    }

    bool has_content(const DocumentChunk& chunk) noexcept {
        return !chunk.text.empty();
    }

} // namespace agent_memory
