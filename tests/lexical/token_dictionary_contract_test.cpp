#include <agent_memory/AgentMemory.hpp>

#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

    class InMemoryTokenDictionary final : public agent_memory::ITokenDictionary {
    public:
        [[nodiscard]] std::size_t size() const noexcept override {
            return m_entries_by_id.size();
        }

        agent_memory::TokenId get_or_create(std::string text) override {
            if(text.empty()) {
                throw std::invalid_argument("token text must not be empty");
            }

            const auto by_text_it = m_ids_by_text.find(text);
            if(by_text_it != m_ids_by_text.end()) {
                return by_text_it->second;
            }

            const auto id = agent_memory::TokenId{m_next_id++};
            agent_memory::TokenDictionaryEntry entry{id, text, 0};
            m_ids_by_text.emplace(entry.text, entry.id);
            m_entries_by_id.emplace(entry.id, std::move(entry));
            return id;
        }

        [[nodiscard]] std::optional<agent_memory::TokenDictionaryEntry> find_by_text(
            const std::string& text
        ) const override {
            const auto by_text_it = m_ids_by_text.find(text);
            if(by_text_it == m_ids_by_text.end()) {
                return std::nullopt;
            }
            return find_by_id(by_text_it->second);
        }

        [[nodiscard]] std::optional<agent_memory::TokenDictionaryEntry> find_by_id(
            const agent_memory::TokenId id
        ) const override {
            const auto it = m_entries_by_id.find(id);
            if(it == m_entries_by_id.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        void set_document_frequency(
            const agent_memory::TokenId id,
            const std::size_t document_frequency
        ) override {
            const auto it = m_entries_by_id.find(id);
            if(it == m_entries_by_id.end()) {
                throw std::invalid_argument("token id must exist");
            }
            it->second.document_frequency = document_frequency;
        }

        [[nodiscard]] bool erase(const agent_memory::TokenId id) override {
            const auto it = m_entries_by_id.find(id);
            if(it == m_entries_by_id.end()) {
                return false;
            }

            m_ids_by_text.erase(it->second.text);
            m_entries_by_id.erase(it);
            return true;
        }

        void clear() override {
            m_entries_by_id.clear();
            m_ids_by_text.clear();
            m_next_id = 1;
        }

    private:
        std::uint64_t m_next_id = 1;
        std::map<agent_memory::TokenId, agent_memory::TokenDictionaryEntry> m_entries_by_id;
        std::map<std::string, agent_memory::TokenId> m_ids_by_text;
    };

    int fail(const std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

} // namespace

int main() {
    agent_memory::TokenDictionaryEntry empty_entry;
    if(!empty_entry.empty()) {
        return fail("default dictionary entry must be empty");
    }

    InMemoryTokenDictionary dictionary;

    const auto agent_id = dictionary.get_or_create("agent");
    const auto same_agent_id = dictionary.get_or_create("agent");
    const auto memory_id = dictionary.get_or_create("memory");

    if(agent_id != same_agent_id) {
        return fail("dictionary must reuse existing token ids for matching text");
    }

    if(!(agent_id < memory_id) || dictionary.size() != 2) {
        return fail("dictionary must allocate deterministic increasing ids");
    }

    const auto by_text = dictionary.find_by_text("agent");
    if(!by_text || by_text->id != agent_id || by_text->text != "agent") {
        return fail("dictionary must find entries by normalized text");
    }

    dictionary.set_document_frequency(agent_id, 7);

    const auto by_id = dictionary.find_by_id(agent_id);
    if(!by_id || by_id->document_frequency != 7) {
        return fail("dictionary must update document frequency by id");
    }

    if(dictionary.find_by_text("missing")) {
        return fail("dictionary must return nullopt for missing token text");
    }

    if(!dictionary.erase(agent_id) || dictionary.find_by_id(agent_id)) {
        return fail("dictionary erase must remove existing entry");
    }

    if(dictionary.erase(agent_id)) {
        return fail("dictionary erase must report false for missing entry");
    }

    dictionary.clear();
    if(dictionary.size() != 0 || dictionary.get_or_create("reset").value() != 1) {
        return fail("dictionary clear must remove entries and reset allocation");
    }

    try {
        (void)dictionary.get_or_create("");
        return fail("dictionary must reject empty token text");
    } catch(const std::invalid_argument&) {
    }

    return 0;
}
