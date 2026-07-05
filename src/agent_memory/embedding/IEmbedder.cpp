#include "IEmbedder.hpp"

namespace agent_memory {

    IEmbedder::~IEmbedder() = default;

    std::vector<Embedding> IEmbedder::embed_batch(
        const std::vector<EmbeddingRequest>& requests
    ) {
        std::vector<Embedding> result;
        result.reserve(requests.size());
        for(const auto& request : requests) {
            result.push_back(embed(request));
        }
        return result;
    }

} // namespace agent_memory
