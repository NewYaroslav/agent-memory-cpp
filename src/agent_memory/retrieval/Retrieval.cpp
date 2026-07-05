#include "Retrieval.hpp"

namespace agent_memory {

    bool RetrievalQuery::has_text() const noexcept {
        return !text.empty();
    }

    bool RetrievalQuery::has_embedding() const noexcept {
        return embedding.has_value() && !embedding->empty();
    }

    bool RetrievalResult::empty() const noexcept {
        return chunks.empty();
    }

    std::size_t RetrievalResult::size() const noexcept {
        return chunks.size();
    }

} // namespace agent_memory
