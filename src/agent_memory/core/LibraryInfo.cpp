#include "LibraryInfo.hpp"

namespace agent_memory {

    namespace {

        constexpr Version CURRENT_VERSION{
            0,
            1,
            0
        };

    } // namespace

    std::string_view library_name() noexcept {
        return "Agent Memory C++";
    }

    std::string_view library_description() noexcept {
        return "Embedded C++17 memory and retrieval toolkit for AI agents";
    }

    const Version& library_version() noexcept {
        return CURRENT_VERSION;
    }

} // namespace agent_memory
