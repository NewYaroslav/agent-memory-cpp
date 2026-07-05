#pragma once
#ifndef AGENT_MEMORY_HEADER_CORE_LIBRARY_INFO_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_CORE_LIBRARY_INFO_HPP_INCLUDED

/// \file LibraryInfo.hpp
/// \brief Build-independent library identity helpers.

#include <string_view>

namespace agent_memory {

    /// \brief Semantic library version.
    struct Version final {
        int major = 0;
        int minor = 0;
        int patch = 0;
    };

    /// \brief Returns stable library display name.
    [[nodiscard]] std::string_view library_name() noexcept;

    /// \brief Returns short library description.
    [[nodiscard]] std::string_view library_description() noexcept;

    /// \brief Returns compiled library version.
    [[nodiscard]] const Version& library_version() noexcept;

} // namespace agent_memory

#endif
