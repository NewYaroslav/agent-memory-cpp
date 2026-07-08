#include <agent_memory/AgentMemory.hpp>

#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

    class InMemoryTokenDictionary final : public agent_memory::ITokenDictionary {
    public:
        [[nodiscard]] std::size_t size() const noexcept override {
            return m_entries_by_id.size();
        }

        agent_memory::TokenId get_or_create(std::string_view text) override {
            if(text.empty()) {
                throw std::invalid_argument("token text must not be empty");
            }

            const auto by_text_it = m_ids_by_text.find(text);
            if(by_text_it != m_ids_by_text.end()) {
                return by_text_it->second;
            }

            const auto id = agent_memory::TokenId{m_next_id++};
            agent_memory::TokenDictionaryEntry entry{id, std::string{text}, 0};
            m_ids_by_text.emplace(entry.text, entry.id);
            m_entries_by_id.emplace(entry.id, std::move(entry));
            return id;
        }

        [[nodiscard]] std::optional<agent_memory::TokenDictionaryEntry> find_by_text(
            std::string_view text
        ) const override {
            const auto by_text_it = m_ids_by_text.find(text);
            if(by_text_it == m_ids_by_text.end()) {
                return std::nullopt;
            }
            return find_by_id(by_text_it->second);
        }

        [[nodiscard]] std::optional<agent_memory::TokenDictionaryEntry> find_by_id(
            const agent_memory::TokenId& id
        ) const override {
            const auto it = m_entries_by_id.find(id);
            if(it == m_entries_by_id.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        void assign_document_frequency(
            const agent_memory::TokenId& id,
            const std::size_t document_frequency
        ) override {
            const auto it = m_entries_by_id.find(id);
            if(it == m_entries_by_id.end()) {
                throw std::invalid_argument("token id must exist");
            }
            it->second.document_frequency = document_frequency;
        }

        void increment_document_frequency(
            const agent_memory::TokenId& id,
            const std::size_t delta
        ) override {
            const auto it = m_entries_by_id.find(id);
            if(it == m_entries_by_id.end()) {
                throw std::invalid_argument("token id must exist");
            }
            it->second.document_frequency += delta;
        }

        [[nodiscard]] bool erase(const agent_memory::TokenId& id) override {
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
        std::map<agent_memory::TokenId, agent_memory::TokenDictionaryEntry, std::less<>> m_entries_by_id;
        std::map<std::string, agent_memory::TokenId, std::less<>> m_ids_by_text;
    };

    int fail(const std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    int test_default_entry_is_empty() {
        agent_memory::TokenDictionaryEntry empty_entry;
        if(!empty_entry.empty()) {
            return fail("default dictionary entry must be empty");
        }
        return 0;
    }

    int test_get_or_create_reuses_ids() {
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
        return 0;
    }

    int test_find_by_text_returns_entry() {
        InMemoryTokenDictionary dictionary;

        const auto agent_id = dictionary.get_or_create("agent");
        const auto by_text = dictionary.find_by_text("agent");
        if(!by_text || by_text->id != agent_id || by_text->text != "agent") {
            return fail("dictionary must find entries by normalized text");
        }
        return 0;
    }

    int test_assign_document_frequency_updates_entry() {
        InMemoryTokenDictionary dictionary;

        const auto agent_id = dictionary.get_or_create("agent");
        dictionary.assign_document_frequency(agent_id, 7);

        const auto by_id = dictionary.find_by_id(agent_id);
        if(!by_id || by_id->document_frequency != 7) {
            return fail("dictionary must update document frequency by id");
        }
        return 0;
    }

    int test_increment_document_frequency_adds_delta() {
        InMemoryTokenDictionary dictionary;

        const auto agent_id = dictionary.get_or_create("agent");
        dictionary.assign_document_frequency(agent_id, 5);
        dictionary.increment_document_frequency(agent_id, 3);

        const auto by_id = dictionary.find_by_id(agent_id);
        if(!by_id || by_id->document_frequency != 8) {
            return fail("dictionary increment_document_frequency must add delta to existing value");
        }
        return 0;
    }

    int test_assign_unknown_id_throws() {
        InMemoryTokenDictionary dictionary;

        bool threw = false;
        try {
            dictionary.assign_document_frequency(agent_memory::TokenId{999}, 7);
        } catch(const std::invalid_argument&) {
            threw = true;
        }
        if(!threw) {
            return fail("assign_document_frequency must throw std::invalid_argument on unknown id");
        }

        threw = false;
        try {
            dictionary.increment_document_frequency(agent_memory::TokenId{999}, 1);
        } catch(const std::invalid_argument&) {
            threw = true;
        }
        if(!threw) {
            return fail("increment_document_frequency must throw std::invalid_argument on unknown id");
        }
        return 0;
    }

    int test_find_by_text_missing_returns_nullopt() {
        InMemoryTokenDictionary dictionary;

        if(dictionary.find_by_text("missing")) {
            return fail("dictionary must return nullopt for missing token text");
        }
        return 0;
    }

    int test_find_by_text_and_find_by_id_agree() {
        InMemoryTokenDictionary dictionary;

        const auto agent_id = dictionary.get_or_create("agent");
        dictionary.assign_document_frequency(agent_id, 7);

        const auto by_text = dictionary.find_by_text("agent");
        const auto by_id = dictionary.find_by_id(agent_id);
        if(!by_text || !by_id) {
            return fail("both lookups should find agent");
        }
        if(by_text->id != by_id->id) {
            return fail("by_text and by_id must agree on id");
        }
        if(by_text->text != by_id->text) {
            return fail("by_text and by_id must agree on text");
        }
        if(by_text->document_frequency != by_id->document_frequency) {
            return fail("by_text and by_id must agree on document_frequency");
        }
        return 0;
    }

    int test_erase_removes_existing_entry() {
        InMemoryTokenDictionary dictionary;

        const auto agent_id = dictionary.get_or_create("agent");
        if(!dictionary.erase(agent_id) || dictionary.find_by_id(agent_id)) {
            return fail("dictionary erase must remove existing entry");
        }
        return 0;
    }

    int test_erase_synchronizes_find_by_text_and_find_by_id() {
        InMemoryTokenDictionary dictionary;

        const auto agent_id = dictionary.get_or_create("agent");
        (void)dictionary.erase(agent_id);

        if(dictionary.find_by_id(agent_id).has_value()) {
            return fail("find_by_id must be empty after erase");
        }
        if(dictionary.find_by_text("agent").has_value()) {
            return fail("find_by_text must be empty after erase");
        }
        return 0;
    }

    int test_erase_missing_returns_false() {
        InMemoryTokenDictionary dictionary;

        const auto agent_id = dictionary.get_or_create("agent");
        (void)dictionary.erase(agent_id);
        if(dictionary.erase(agent_id)) {
            return fail("dictionary erase must report false for missing entry");
        }
        return 0;
    }

    int test_clear_resets_state() {
        InMemoryTokenDictionary dictionary;

        (void)dictionary.get_or_create("anything");
        dictionary.clear();
        if(dictionary.size() != 0 || dictionary.get_or_create("reset").value() != 1) {
            return fail("dictionary clear must remove entries and reset allocation");
        }
        return 0;
    }

    int test_get_or_create_rejects_empty_text() {
        InMemoryTokenDictionary dictionary;

        try {
            (void)dictionary.get_or_create("");
            return fail("dictionary must reject empty token text");
        } catch(const std::invalid_argument&) {
        }
        return 0;
    }

    using TestFn = int (*)();

    struct TestCase final {
        const char* name;
        TestFn fn;
    };

    int run(const TestCase* cases, const std::size_t count) {
        for(std::size_t i = 0; i < count; ++i) {
            if(const int rc = cases[i].fn(); rc != 0) {
                std::cerr << "[" << cases[i].name << "] FAILED" << '\n';
                return rc;
            }
        }
        return 0;
    }

} // namespace

int main() {
    const TestCase cases[] = {
        {"default_entry_is_empty",          &test_default_entry_is_empty},
        {"get_or_create_reuses_ids",        &test_get_or_create_reuses_ids},
        {"find_by_text_returns_entry",      &test_find_by_text_returns_entry},
        {"assign_document_frequency",       &test_assign_document_frequency_updates_entry},
        {"increment_document_frequency",    &test_increment_document_frequency_adds_delta},
        {"assign_unknown_id_throws",        &test_assign_unknown_id_throws},
        {"find_by_text_missing_returns",    &test_find_by_text_missing_returns_nullopt},
        {"find_by_text_and_find_by_id",     &test_find_by_text_and_find_by_id_agree},
        {"erase_removes_existing_entry",    &test_erase_removes_existing_entry},
        {"erase_synchronizes_lookups",      &test_erase_synchronizes_find_by_text_and_find_by_id},
        {"erase_missing_returns_false",     &test_erase_missing_returns_false},
        {"clear_resets_state",              &test_clear_resets_state},
        {"get_or_create_rejects_empty",     &test_get_or_create_rejects_empty_text},
    };
    return run(cases, sizeof(cases) / sizeof(cases[0]));
}