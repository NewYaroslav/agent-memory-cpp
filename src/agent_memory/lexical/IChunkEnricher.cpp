#include "IChunkEnricher.hpp"

namespace agent_memory {

    IChunkEnricher::~IChunkEnricher() = default;

    ChunkEnrichment PassthroughChunkEnricher::enrich(
        const std::string& chunk_text
    ) const {
        ChunkEnrichment result;
        result.original_text = chunk_text;
        result.enriched_text = chunk_text;
        result.level = 0;
        return result;
    }

} // namespace agent_memory