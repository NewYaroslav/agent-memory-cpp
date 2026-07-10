#pragma once
#ifndef AGENT_MEMORY_HEADER_CHAT_MESSAGE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_CHAT_MESSAGE_HPP_INCLUDED

/// \file Message.hpp
/// \brief Forward-looking chat/thread memory value types.
///
/// Chat memory has its own retrieval shape (thread reconstruction, reply
/// chains, author attribution). This header reserves the value types so a
/// future `ChatThreadSearch` retrieval mode has a stable payload contract.

#include <agent_memory/domain/Domain.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

    /// \brief Stable identifier for a single chat message.
    class MessageId final {
    public:
        MessageId() = default;
        explicit MessageId(std::string value);

        /// \brief Returns identifier text.
        [[nodiscard]] const std::string& value() const noexcept;

        /// \brief Checks whether the identifier has no value.
        [[nodiscard]] bool empty() const noexcept;

    private:
        std::string m_value;
    };

    /// \brief Stable identifier for a chat thread.
    class ThreadId final {
    public:
        ThreadId() = default;
        explicit ThreadId(std::string value);

        /// \brief Returns identifier text.
        [[nodiscard]] const std::string& value() const noexcept;

        /// \brief Checks whether the identifier has no value.
        [[nodiscard]] bool empty() const noexcept;

    private:
        std::string m_value;
    };

    /// \brief Stable identifier for a chat participant.
    class AuthorId final {
    public:
        AuthorId() = default;
        explicit AuthorId(std::string value);

        /// \brief Returns identifier text.
        [[nodiscard]] const std::string& value() const noexcept;

        /// \brief Checks whether the identifier has no value.
        [[nodiscard]] bool empty() const noexcept;

    private:
        std::string m_value;
    };

    /// \brief Coarse-grained author role for downstream formatting.
    enum class AuthorRole {
        User,
        Assistant,
        System,
        Tool,
        Other
    };

    /// \brief Single chat message inside a thread.
    struct Message final {
        MessageId id;
        ThreadId thread_id;
        AuthorId author_id;
        AuthorRole role = AuthorRole::User;
        std::string text;
        /// \brief Wall-clock timestamp in milliseconds since the Unix epoch.
        std::int64_t timestamp_ms = 0;
        /// \brief Reply target (nullopt for thread roots).
        std::optional<MessageId> reply_to_id;
        Metadata metadata;
    };

    /// \brief Ordered list of messages in a thread.
    struct Thread final {
        ThreadId id;
        std::string title;
        std::vector<Message> messages;
    };

    [[nodiscard]] bool operator==(const MessageId& lhs, const MessageId& rhs) noexcept;
    [[nodiscard]] bool operator!=(const MessageId& lhs, const MessageId& rhs) noexcept;
    [[nodiscard]] bool operator<(const MessageId& lhs, const MessageId& rhs) noexcept;

    [[nodiscard]] bool operator==(const ThreadId& lhs, const ThreadId& rhs) noexcept;
    [[nodiscard]] bool operator!=(const ThreadId& lhs, const ThreadId& rhs) noexcept;
    [[nodiscard]] bool operator<(const ThreadId& lhs, const ThreadId& rhs) noexcept;

    [[nodiscard]] bool operator==(const AuthorId& lhs, const AuthorId& rhs) noexcept;
    [[nodiscard]] bool operator!=(const AuthorId& lhs, const AuthorId& rhs) noexcept;
    [[nodiscard]] bool operator<(const AuthorId& lhs, const AuthorId& rhs) noexcept;

    /// \brief Stable lowercase role name.
    [[nodiscard]] std::string_view to_string(AuthorRole role) noexcept;

    /// \brief Returns true when the message carries enough data to be surfaced.
    [[nodiscard]] bool is_valid(const Message& message) noexcept;

} // namespace agent_memory

#endif
