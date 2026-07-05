#include "VectorIndex.hpp"

namespace agent_memory {

    bool matches_metadata_filters(
        const Metadata& metadata,
        const std::vector<MetadataFilter>& filters
    ) {
        for(const auto& filter : filters) {
            const auto value = metadata.get(filter.key);
            if(!value || *value != filter.value) {
                return false;
            }
        }
        return true;
    }

} // namespace agent_memory
