#include <agent_memory.hpp>

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
        std::initializer_list<std::pair<std::string, std::string>> kv
    ) {
        agent_memory::Metadata metadata;
        for(const auto& item : kv) {
            metadata.set(item.first, item.second);
        }

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
    agent_memory::KeywordOverlapIndex index;

    index.upsert(make_record(
        "chunk:a",
        "resource:alpha",
        make_tokens({"export", "billing", "account"}),
        {{"scope", "public"}, {"lang", "en"}}
    ));
    index.upsert(make_record(
        "chunk:b",
        "resource:beta",
        make_tokens({"export", "export", "export"}),
        {{"scope", "public"}, {"lang", "en"}}
    ));
    index.upsert(make_record(
        "chunk:c",
        "resource:gamma",
        make_tokens({"export", "billing", "delete", "archive"}),
        {{"scope", "private"}, {"lang", "en"}}
    ));

    if(index.size() != 3) {
        return fail("keyword overlap index must store upserted records");
    }

    const auto stats = index.find_stats(agent_memory::ChunkId{"chunk:b"});
    if(!stats || stats->token_count != 3 || stats->unique_token_count != 1) {
        return fail("keyword overlap stats must expose total and unique token counts");
    }

    const auto ranked = index.search(agent_memory::LexicalSearchQuery{
        {"export", "billing", "delete"},
        10,
        {},
        {}
    });

    if(ranked.size() != 3 || ranked[0].chunk_id != agent_memory::ChunkId{"chunk:c"}) {
        return fail("keyword overlap must rank by unique query-term overlap");
    }
    if(ranked[0].score != 3.0F || ranked[1].score != 2.0F || ranked[2].score != 1.0F) {
        return fail("keyword overlap score must equal unique matching query-term count");
    }

    const auto duplicate_query_terms = index.search(agent_memory::LexicalSearchQuery{
        {"export", "export", "billing"},
        10,
        {},
        {}
    });

    if(
        duplicate_query_terms.size() != 3 ||
        duplicate_query_terms[0].score != 2.0F ||
        duplicate_query_terms[1].score != 2.0F ||
        duplicate_query_terms[2].score != 1.0F
    ) {
        return fail("duplicate query terms must not inflate keyword-overlap score");
    }

    if(duplicate_query_terms[0].chunk_id != agent_memory::ChunkId{"chunk:a"}) {
        return fail("equal keyword-overlap scores must be ordered by chunk id");
    }

    const auto filtered = index.search(agent_memory::LexicalSearchQuery{
        {"delete", "billing", "export"},
        10,
        {agent_memory::MetadataFilter{"scope", "public"}},
        {}
    });

    if(filtered.size() != 2 || filtered[0].chunk_id != agent_memory::ChunkId{"chunk:a"}) {
        return fail("keyword overlap index must apply metadata filters");
    }

    const auto limited = index.search(agent_memory::LexicalSearchQuery{
        {"export"},
        1,
        {},
        {}
    });

    if(limited.size() != 1) {
        return fail("keyword overlap index must honor result limit");
    }

    const auto zero_limit = index.search(agent_memory::LexicalSearchQuery{
        {"export"},
        0,
        {},
        {}
    });

    if(!zero_limit.empty()) {
        return fail("limit zero must return no keyword-overlap results");
    }

    const auto no_match = index.search(agent_memory::LexicalSearchQuery{
        {"refund"},
        10,
        {},
        {}
    });

    if(!no_match.empty()) {
        return fail("keyword overlap index must not return zero-score chunks");
    }

    index.upsert(make_record(
        "chunk:a",
        "resource:delta",
        make_tokens({"refund"}),
        {{"scope", "public"}, {"lang", "en"}}
    ));

    const auto after_replace = index.search(agent_memory::LexicalSearchQuery{
        {"billing"},
        10,
        {},
        {}
    });

    if(after_replace.size() != 1 || after_replace[0].chunk_id != agent_memory::ChunkId{"chunk:c"}) {
        return fail("upsert must replace old keyword-overlap terms");
    }

    if(index.erase_resource(agent_memory::ResourceId{"resource:alpha"}) != 0) {
        return fail("upsert reparenting must remove old resource ownership");
    }
    if(index.erase_resource(agent_memory::ResourceId{"resource:delta"}) != 1) {
        return fail("erase_resource must remove the reparented chunk");
    }
    if(index.erase(agent_memory::ChunkId{"chunk:b"}) != true) {
        return fail("erase must report removed keyword-overlap chunks");
    }
    if(index.erase(agent_memory::ChunkId{"chunk:b"}) != false) {
        return fail("erase must be idempotent for missing keyword-overlap chunks");
    }

    index.clear();
    if(index.size() != 0) {
        return fail("clear must remove all keyword-overlap records");
    }

    try {
        index.upsert(agent_memory::LexicalDocumentRecord{});
        return fail("keyword overlap index must reject invalid records");
    } catch(const std::invalid_argument&) {
    }

    try {
        (void)index.search(agent_memory::LexicalSearchQuery{});
        return fail("keyword overlap index must reject invalid non-zero-limit queries");
    } catch(const std::invalid_argument&) {
    }

    return 0;
}
