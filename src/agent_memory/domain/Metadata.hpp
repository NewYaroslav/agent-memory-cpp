#pragma once
#ifndef AGENT_MEMORY_HEADER_DOMAIN_METADATA_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_DOMAIN_METADATA_HPP_INCLUDED

/// \file Metadata.hpp
/// \brief Deterministic string metadata container.

#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace agent_memory {

    /// \brief Ordered string metadata attached to domain records.
    class Metadata final {
    public:
        using Values = std::map<std::string, std::string>;

        /// \brief Inserts or replaces a metadata value.
        void set(std::string key, std::string value);

        /// \brief Removes a metadata value.
        /// \return True when a value was removed.
        [[nodiscard]] bool erase(const std::string& key);

        /// \brief Checks whether a key exists.
        [[nodiscard]] bool contains(const std::string& key) const;

        /// \brief Returns a metadata value copy when the key exists.
        [[nodiscard]] std::optional<std::string> get(const std::string& key) const;

        /// \brief Checks whether the container has no metadata.
        [[nodiscard]] bool empty() const noexcept;

        /// \brief Returns the number of metadata entries.
        [[nodiscard]] std::size_t size() const noexcept;

        /// \brief Returns ordered metadata entries.
        [[nodiscard]] const Values& values() const noexcept;

    private:
        Values m_values;
    };

} // namespace agent_memory

#endif
