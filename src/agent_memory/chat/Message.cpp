#include "Message.hpp"

namespace agent_memory {

    MessageId::MessageId(std::string value)
        : m_value(std::move(value)) {}

    const std::string& MessageId::value() const noexcept {
        return m_value;
    }

    bool MessageId::empty() const noexcept {
        return m_value.empty();
    }

    ThreadId::ThreadId(std::string value)
        : m_value(std::move(value)) {}

    const std::string& ThreadId::value() const noexcept {
        return m_value;
    }

    bool ThreadId::empty() const noexcept {
        return m_value.empty();
    }

    AuthorId::AuthorId(std::string value)
        : m_value(std::move(value)) {}

    const std::string& AuthorId::value() const noexcept {
        return m_value;
    }

    bool AuthorId::empty() const noexcept {
        return m_value.empty();
    }

    bool operator==(const MessageId& lhs, const MessageId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const MessageId& lhs, const MessageId& rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const MessageId& lhs, const MessageId& rhs) noexcept {
        return lhs.value() < rhs.value();
    }

    bool operator==(const ThreadId& lhs, const ThreadId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const ThreadId& lhs, const ThreadId& rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const ThreadId& lhs, const ThreadId& rhs) noexcept {
        return lhs.value() < rhs.value();
    }

    bool operator==(const AuthorId& lhs, const AuthorId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const AuthorId& lhs, const AuthorId& rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const AuthorId& lhs, const AuthorId& rhs) noexcept {
        return lhs.value() < rhs.value();
    }

    std::string_view to_string(AuthorRole role) noexcept {
        switch (role) {
            case AuthorRole::User:      return "user";
            case AuthorRole::Assistant: return "assistant";
            case AuthorRole::System:    return "system";
            case AuthorRole::Tool:      return "tool";
            case AuthorRole::Other:     return "other";
        }
        return "other";
    }

    bool is_valid(const Message& message) noexcept {
        return !message.id.empty()
            && !message.thread_id.empty()
            && !message.text.empty();
    }

} // namespace agent_memory