#include "IReranker.hpp"

namespace agent_memory {

    IReranker::~IReranker() = default;

    std::vector<LexicalSearchResult> IdentityReranker::rerank(
        const std::string& /*query*/,
        std::vector<LexicalSearchResult> candidates
    ) const {
        return candidates;
    }

} // namespace agent_memory