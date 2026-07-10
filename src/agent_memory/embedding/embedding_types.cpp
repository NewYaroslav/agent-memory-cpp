#include "embedding_types.hpp"

namespace agent_memory {

    bool Embedding::empty() const noexcept {
        return values.empty();
    }

    std::size_t Embedding::dimension() const noexcept {
        return values.size();
    }

} // namespace agent_memory
