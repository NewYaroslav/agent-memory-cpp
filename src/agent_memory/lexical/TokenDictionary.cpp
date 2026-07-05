#include "TokenDictionary.hpp"

namespace agent_memory {

    bool TokenDictionaryEntry::empty() const noexcept {
        return id.empty() || text.empty();
    }

} // namespace agent_memory
