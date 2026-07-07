#pragma once
#ifndef AGENT_MEMORY_HEADER_DOMAIN_METADATA_FILTER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_DOMAIN_METADATA_FILTER_HPP_INCLUDED

/// \file MetadataFilter.hpp
/// \brief Shared exact metadata equality filter used by search queries.

#include "Metadata.hpp"

#include <string>
#include <vector>

namespace agent_memory {

    /// \brief Exact metadata equality filter used by search queries.
    struct MetadataFilter final {
        std::string key;
        std::string value;
    };

    /// \brief Returns true when metadata satisfies all exact filters.
    [[nodiscard]] bool matches_metadata_filters(
        const Metadata& metadata,
        const std::vector<MetadataFilter>& filters
    );

} // namespace agent_memory

#endif
