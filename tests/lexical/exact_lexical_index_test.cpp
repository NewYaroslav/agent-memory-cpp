#include <agent_memory/AgentMemory.hpp>

#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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

    [[nodiscard]] std::vector<agent_memory::Token> make_tokens(
        std::initializer_list<std::string_view> terms
    ) {
        std::vector<agent_memory::Token> tokens;
        std::size_t position = 0;
        for(const auto term : terms) {
            tokens.push_back(make_token(std::string{term}, position));
            ++position;
        }
        return tokens;
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
            agent_memory::ChunkId{chunk_id},
            agent_memory::ResourceRevision{
                agent_memory::ResourceId{resource_id},
                1,
                11,
                17
            },
            std::move(tokens),
            metadata
        };
    }

} // namespace

int main() {
    agent_memory::ExactLexicalIndex index;

    index.upsert(make_record(
        "chunk:a",
        "resource:alpha",
        make_tokens({"agent", "memory", "agent", "system"}),
        "public"
    ));
    index.upsert(make_record(
        "chunk:b",
        "resource:alpha",
        make_tokens({"memory", "system"}),
        "private"
    ));
    index.upsert(make_record(
        "chunk:c",
        "resource:beta",
        make_tokens({"agent"}),
        "public"
    ));

    if(index.size() != 3) {
        return fail("exact lexical index must store upserted records");
    }

    const auto stats = index.find_stats(agent_memory::ChunkId{"chunk:a"});
    if(!stats || stats->token_count != 4 || stats->unique_token_count != 3) {
        return fail("exact lexical index must expose token stats");
    }

    const auto ranked = index.search(agent_memory::LexicalSearchQuery{
        {"agent", "memory"},
        10,
        {},
        {}
    });

    if(ranked.empty() || ranked[0].chunk_id != agent_memory::ChunkId{"chunk:a"}) {
        return fail("BM25 search must rank the best matching chunk first");
    }

    const auto filtered = index.search(agent_memory::LexicalSearchQuery{
        {"system"},
        10,
        {agent_memory::MetadataFilter{"scope", "public"}},
        {}
    });

    if(filtered.size() != 1 || filtered[0].chunk_id != agent_memory::ChunkId{"chunk:a"}) {
        return fail("exact lexical index must apply metadata filters");
    }

    const auto limited = index.search(agent_memory::LexicalSearchQuery{
        {"agent"},
        1,
        {},
        {}
    });

    if(limited.size() != 1) {
        return fail("exact lexical index must honor result limit");
    }

    const auto zero_limit = index.search(agent_memory::LexicalSearchQuery{
        {"agent"},
        0,
        {},
        {}
    });

    if(!zero_limit.empty()) {
        return fail("limit zero must return no results");
    }

    index.upsert(make_record(
        "chunk:a",
        "resource:alpha",
        make_tokens({"updated"}),
        "public"
    ));

    const auto replaced = index.search(agent_memory::LexicalSearchQuery{
        {"agent", "memory"},
        10,
        {},
        {}
    });

    if(replaced.empty() || replaced[0].chunk_id == agent_memory::ChunkId{"chunk:a"}) {
        return fail("upsert must replace old lexical terms");
    }

    agent_memory::ExactLexicalIndex tie_index;
    tie_index.upsert(make_record(
        "chunk:b",
        "resource:tie",
        make_tokens({"tie"}),
        "public"
    ));
    tie_index.upsert(make_record(
        "chunk:a",
        "resource:tie",
        make_tokens({"tie"}),
        "public"
    ));

    const auto tie_results = tie_index.search(agent_memory::LexicalSearchQuery{
        {"tie"},
        2,
        {},
        {}
    });

    if(
        tie_results.size() != 2 ||
        tie_results[0].chunk_id != agent_memory::ChunkId{"chunk:a"} ||
        tie_results[1].chunk_id != agent_memory::ChunkId{"chunk:b"}
    ) {
        return fail("exact lexical index must break score ties by chunk id");
    }

    if(index.erase_resource(agent_memory::ResourceId{"resource:alpha"}) != 2) {
        return fail("erase_resource must remove all chunks owned by the resource");
    }

    if(index.find_stats(agent_memory::ChunkId{"chunk:b"})) {
        return fail("erase_resource must remove stats for owned chunks");
    }

    if(!index.erase(agent_memory::ChunkId{"chunk:c"}) || index.erase(agent_memory::ChunkId{"chunk:c"})) {
        return fail("erase must report removed and missing chunks");
    }

    index.clear();
    if(index.size() != 0) {
        return fail("clear must remove all records");
    }

    try {
        agent_memory::ExactLexicalIndex invalid_index{
            agent_memory::ExactLexicalIndexOptions{
                agent_memory::Bm25Options{0.0F, 0.75F}
            }
        };
        (void)invalid_index;
        return fail("exact lexical index must reject invalid BM25 options");
    } catch(const std::invalid_argument&) {
    }

    try {
        index.upsert(agent_memory::LexicalDocumentRecord{});
        return fail("exact lexical index must reject invalid records");
    } catch(const std::invalid_argument&) {
    }

    try {
        (void)index.search(agent_memory::LexicalSearchQuery{});
        return fail("exact lexical index must reject invalid non-zero-limit queries");
    } catch(const std::invalid_argument&) {
    }

    return 0;
}
