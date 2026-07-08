#include "IRetrievalEngine.hpp"

namespace agent_memory {

    IRetrievalEngine::~IRetrievalEngine() = default;

    bool RetrievalResponse::empty() const noexcept {
        return items.empty();
    }

    std::size_t RetrievalResponse::size() const noexcept {
        return items.size();
    }

} // namespace agent_memory