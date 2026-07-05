#include <agent_memory/AgentMemory.hpp>

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    class InMemoryLexicalIndex final : public agent_memory::ILexicalIndex {
    public:
        [[nodiscard]] std::size_t size() const noexcept override {
            return m_records.size();
        }

        void upsert(agent_memory::LexicalDocumentRecord record) override {
            if(!agent_memory::is_valid(record)) {
                throw std::invalid_argument("invalid lexical document record");
            }

            const auto chunk_id = record.chunk_id;
            const auto resource_id = record.revision.resource_id;
            const bool removed_existing = erase(chunk_id);
            (void)removed_existing;

            m_chunks_by_resource[resource_id].insert(chunk_id);
            m_records.emplace(chunk_id, std::move(record));
        }

        [[nodiscard]] std::optional<agent_memory::LexicalDocumentStats> find_stats(
            const agent_memory::ChunkId& chunk_id
        ) const override {
            const auto it = m_records.find(chunk_id);
            if(it == m_records.end()) {
                return std::nullopt;
            }

            std::set<std::string> unique_terms;
            for(const auto& token : it->second.tokens) {
                unique_terms.insert(token.text);
            }

            return agent_memory::LexicalDocumentStats{
                it->second.chunk_id,
                it->second.revision,
                it->second.tokens.size(),
                unique_terms.size(),
                it->second.metadata
            };
        }

        [[nodiscard]] std::vector<agent_memory::LexicalSearchResult> search(
            const agent_memory::LexicalSearchQuery& query
        ) const override {
            if(query.limit == 0) {
                return {};
            }

            if(!agent_memory::is_valid(query)) {
                throw std::invalid_argument("invalid lexical search query");
            }

            std::vector<agent_memory::LexicalSearchResult> results;

            for(const auto& item : m_records) {
                const auto& record = item.second;
                if(!agent_memory::matches_metadata_filters(record.metadata, query.metadata_filters)) {
                    continue;
                }

                float score = 0.0F;
                for(const auto& query_term : query.terms) {
                    for(const auto& token : record.tokens) {
                        if(token.text == query_term) {
                            score += 1.0F;
                        }
                    }
                }

                if(score > 0.0F) {
                    results.push_back(agent_memory::LexicalSearchResult{
                        record.chunk_id,
                        score,
                        record.metadata
                    });
                }
            }

            std::sort(
                results.begin(),
                results.end(),
                [](const auto& lhs, const auto& rhs) {
                    if(lhs.score == rhs.score) {
                        return lhs.chunk_id < rhs.chunk_id;
                    }
                    return lhs.score > rhs.score;
                }
            );

            if(results.size() > query.limit) {
                results.resize(query.limit);
            }

            return results;
        }

        [[nodiscard]] bool erase(const agent_memory::ChunkId& chunk_id) override {
            const auto it = m_records.find(chunk_id);
            if(it == m_records.end()) {
                return false;
            }

            const auto resource_id = it->second.revision.resource_id;
            m_records.erase(it);

            const auto by_resource_it = m_chunks_by_resource.find(resource_id);
            if(by_resource_it != m_chunks_by_resource.end()) {
                by_resource_it->second.erase(chunk_id);
                if(by_resource_it->second.empty()) {
                    m_chunks_by_resource.erase(by_resource_it);
                }
            }

            return true;
        }

        [[nodiscard]] std::size_t erase_resource(
            const agent_memory::ResourceId& resource_id
        ) override {
            const auto by_resource_it = m_chunks_by_resource.find(resource_id);
            if(by_resource_it == m_chunks_by_resource.end()) {
                return 0;
            }

            const auto chunk_ids = by_resource_it->second;
            std::size_t removed = 0;
            for(const auto& chunk_id : chunk_ids) {
                if(erase(chunk_id)) {
                    ++removed;
                }
            }
            return removed;
        }

        void clear() override {
            m_records.clear();
            m_chunks_by_resource.clear();
        }

    private:
        std::map<agent_memory::ChunkId, agent_memory::LexicalDocumentRecord> m_records;
        std::map<agent_memory::ResourceId, std::set<agent_memory::ChunkId>> m_chunks_by_resource;
    };

    int fail(const std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    [[nodiscard]] agent_memory::Token make_token(
        std::string text,
        const std::size_t position
    ) {
        return agent_memory::Token{
            std::move(text),
            agent_memory::TextRange{position, 1},
            position,
            agent_memory::TokenKind::Word
        };
    }

    [[nodiscard]] agent_memory::LexicalDocumentRecord make_record(
        const std::string& chunk_id,
        const std::string& resource_id,
        std::vector<agent_memory::Token> tokens,
        const std::string& scope
    ) {
        agent_memory::Metadata metadata;
        metadata.set("scope", scope);

        return agent_memory::LexicalDocumentRecord{
            agent_memory::ResourceRevision{
                agent_memory::ResourceId{resource_id},
                1,
                11,
                17
            },
            agent_memory::ChunkId{chunk_id},
            std::move(tokens),
            metadata
        };
    }

} // namespace

int main() {
    InMemoryLexicalIndex index;

    const auto invalid_record = agent_memory::LexicalDocumentRecord{};
    if(agent_memory::is_valid(invalid_record)) {
        return fail("empty lexical document record must be invalid");
    }

    const auto invalid_query = agent_memory::LexicalSearchQuery{};
    if(agent_memory::is_valid(invalid_query)) {
        return fail("query without normalized terms must be invalid");
    }

    index.upsert(make_record(
        "chunk:a",
        "resource:alpha",
        {make_token("agent", 0), make_token("memory", 1), make_token("agent", 2)},
        "public"
    ));
    index.upsert(make_record(
        "chunk:b",
        "resource:alpha",
        {make_token("memory", 0)},
        "private"
    ));
    index.upsert(make_record(
        "chunk:c",
        "resource:beta",
        {make_token("agent", 0)},
        "public"
    ));

    if(index.size() != 3) {
        return fail("lexical index must store upserted chunks");
    }

    const auto stats = index.find_stats(agent_memory::ChunkId{"chunk:a"});
    if(!stats || stats->token_count != 3 || stats->unique_token_count != 2) {
        return fail("lexical index must expose document stats");
    }

    const auto results = index.search(agent_memory::LexicalSearchQuery{
        {"agent"},
        10,
        {},
        {}
    });

    if(
        results.size() != 2 ||
        results[0].chunk_id != agent_memory::ChunkId{"chunk:a"} ||
        results[0].score <= results[1].score
    ) {
        return fail("lexical search must rank higher scores first");
    }

    const auto filtered = index.search(agent_memory::LexicalSearchQuery{
        {"agent"},
        10,
        {agent_memory::MetadataFilter{"scope", "public"}},
        {}
    });

    if(filtered.size() != 2) {
        return fail("lexical search must apply metadata filters");
    }

    const auto limited = index.search(agent_memory::LexicalSearchQuery{
        {"agent"},
        1,
        {},
        {}
    });

    if(limited.size() != 1) {
        return fail("lexical search must honor result limit");
    }

    const auto zero_limit = index.search(agent_memory::LexicalSearchQuery{
        {"agent"},
        0,
        {},
        {}
    });

    if(!zero_limit.empty()) {
        return fail("limit zero must return no lexical results");
    }

    index.upsert(make_record(
        "chunk:a",
        "resource:alpha",
        {make_token("updated", 0)},
        "public"
    ));

    const auto replaced = index.search(agent_memory::LexicalSearchQuery{
        {"agent"},
        10,
        {},
        {}
    });

    if(replaced.size() != 1 || replaced[0].chunk_id != agent_memory::ChunkId{"chunk:c"}) {
        return fail("upsert must replace existing chunk records");
    }

    if(index.erase_resource(agent_memory::ResourceId{"resource:alpha"}) != 2) {
        return fail("erase_resource must remove all chunks for a resource");
    }

    if(index.size() != 1 || index.find_stats(agent_memory::ChunkId{"chunk:b"})) {
        return fail("erase_resource must remove indexed stats");
    }

    if(!index.erase(agent_memory::ChunkId{"chunk:c"}) || index.erase(agent_memory::ChunkId{"chunk:c"})) {
        return fail("erase must report removed and missing chunks");
    }

    index.clear();
    if(index.size() != 0) {
        return fail("clear must remove all lexical records");
    }

    try {
        index.upsert(agent_memory::LexicalDocumentRecord{});
        return fail("lexical index must reject invalid records");
    } catch(const std::invalid_argument&) {
    }

    try {
        (void)index.search(agent_memory::LexicalSearchQuery{});
        return fail("lexical index must reject invalid non-zero-limit queries");
    } catch(const std::invalid_argument&) {
    }

    return 0;
}
