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

    [[nodiscard]] agent_memory::LexicalDocumentRecord make_record_with_meta(
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

    if(index.erase_resource(agent_memory::ResourceId{"resource:alpha"}) != 0) {
        return fail("erase_resource must be idempotent (return 0 on second call)");
    }

    if(index.find_stats(agent_memory::ChunkId{"chunk:b"})) {
        return fail("erase_resource must remove stats for owned chunks");
    }

    if(!index.erase(agent_memory::ChunkId{"chunk:c"}) || index.erase(agent_memory::ChunkId{"chunk:c"})) {
        return fail("erase must report removed and missing chunks");
    }

    index.upsert(make_record(
        "chunk:idem",
        "resource:idem",
        make_tokens({"alpha"}),
        "public"
    ));
    if(!index.erase(agent_memory::ChunkId{"chunk:idem"})) {
        return fail("first erase must return true");
    }
    if(index.erase(agent_memory::ChunkId{"chunk:idem"})) {
        return fail("second erase must return false (idempotent)");
    }
    if(index.erase_resource(agent_memory::ResourceId{"resource:idem"}) != 0) {
        return fail("erase_resource on idempotency-test chunk must return 0");
    }

    index.clear();
    if(index.size() != 0) {
        return fail("clear must remove all records");
    }

    // Fix 3: regression test — explicit query BM25 numerically equal to
    // the library default (k1=1.5, b=0.75) must DIFFER from "fall back to
    // index options" when the index has a non-default BM25 configuration.
    // Use a longer document so the TF-saturation factor (k1 * (1 - b + b *
    // length_ratio)) actually depends on k1 and b (with a single-token
    // document whose length equals avgdl, the factor is 1.0 regardless).
    {
        agent_memory::ExactLexicalIndex bm25_override_index{
            agent_memory::ExactLexicalIndexOptions{
                agent_memory::Bm25Options{2.0F, 0.3F}
            }
        };
        // 5 tokens; avgdl across the index is 5.0, so |D|/avgdl = 1.0
        // and (1 - b + b * 1.0) = 1.0, making k1 still factor in. We
        // need |D| != avgdl to exercise the b parameter; we also add
        // a longer chunk to make avgdl diverge from this chunk's length.
        bm25_override_index.upsert(make_record(
            "chunk:long",
            "resource:alpha",
            make_tokens({"filler", "filler", "filler", "filler", "filler",
                         "filler", "filler", "filler", "filler", "filler"}),
            "public"
        ));
        bm25_override_index.upsert(make_record(
            "chunk:a",
            "resource:beta",
            make_tokens({"foo", "bar", "baz"}),
            "public"
        ));

        agent_memory::LexicalSearchQuery fallback_query;
        fallback_query.terms = {"foo"};
        fallback_query.limit = 10;
        fallback_query.bm25 = std::nullopt;
        const auto fallback_scores = bm25_override_index.search(fallback_query);

        agent_memory::LexicalSearchQuery explicit_query;
        explicit_query.terms = {"foo"};
        explicit_query.limit = 10;
        explicit_query.bm25 = agent_memory::Bm25Options{1.5F, 0.75F};
        const auto explicit_scores = bm25_override_index.search(explicit_query);

        if(fallback_scores.empty() || explicit_scores.empty()) {
            return fail("Fix3: both searches should return hits");
        }
        if(fallback_scores.front().score == explicit_scores.front().score) {
            return fail("Fix3: explicit query BM25 default must differ from index BM25 fallback");
        }
    }

    // Fix 7: metadata filters must use AND semantics — all filters
    // must match a chunk for it to be returned.
    {
        agent_memory::ExactLexicalIndex and_index;
        and_index.upsert(make_record_with_meta(
            "chunk:a",
            "resource:alpha",
            make_tokens({"foo"}),
            {{"scope", "public"}, {"lang", "en"}}
        ));
        and_index.upsert(make_record_with_meta(
            "chunk:b",
            "resource:alpha",
            make_tokens({"foo"}),
            {{"scope", "private"}, {"lang", "en"}}
        ));

        agent_memory::LexicalSearchQuery q;
        q.terms = {"foo"};
        q.limit = 10;
        q.metadata_filters = {
            agent_memory::MetadataFilter{"scope", "public"},
            agent_memory::MetadataFilter{"lang", "en"}
        };
        const auto and_results = and_index.search(q);
        if(and_results.size() != 1) {
            return fail("Fix7: AND filters must return chunks matching all filters");
        }
        if(and_results[0].chunk_id != agent_memory::ChunkId{"chunk:a"}) {
            return fail("Fix7: must match chunk:a (scope=public+lang=en), not chunk:b");
        }
    }

    // Fix 8: upsert can re-parent a chunk to a different resource_id;
    // the old resource must drop the chunk id, the new resource must
    // own it, and erase_resource on the old resource must return 0.
    {
        agent_memory::ExactLexicalIndex reparent_index;
        reparent_index.upsert(make_record(
            "chunk:a",
            "resource:alpha",
            make_tokens({"foo"}),
            "public"
        ));
        reparent_index.upsert(make_record(
            "chunk:a",
            "resource:beta",
            make_tokens({"foo"}),
            "public"
        ));

        if(reparent_index.erase_resource(agent_memory::ResourceId{"resource:alpha"}) != 0) {
            return fail("Fix8: alpha must be empty after re-parent");
        }
        if(reparent_index.size() != 1) {
            return fail("Fix8: total size must be 1 after re-parent (beta owns chunk:a)");
        }
        if(reparent_index.erase_resource(agent_memory::ResourceId{"resource:beta"}) != 1) {
            return fail("Fix8: beta must own 1 chunk");
        }
        if(reparent_index.size() != 0) {
            return fail("Fix8: total size must be 0 after erasing beta");
        }
    }

    // Fix 9a: df counters and stale dictionary entries must remain
    // consistent across mixed upsert/erase sequences.
    {
        agent_memory::ExactLexicalIndex df_index;
        df_index.upsert(make_record(
            "chunk:a",
            "resource:alpha",
            make_tokens({"foo", "bar"}),
            "public"
        ));
        df_index.upsert(make_record(
            "chunk:b",
            "resource:beta",
            make_tokens({"foo"}),
            "public"
        ));
        if(!df_index.erase(agent_memory::ChunkId{"chunk:a"})) {
            return fail("Fix9a: erase of chunk:a must return true");
        }
        if(df_index.size() != 1) {
            return fail("Fix9a: size after erase must be 1");
        }
        // At this point df(foo)=1, df(bar) reached 0 and its dictionary
        // entry must have been erased (stale-entry cleanup). Search for
        // 'bar' must still return 0 hits — we cannot inspect
        // m_dictionary_by_text directly, but a search for 'bar' yields
        // no result and the index remains functional.
        agent_memory::LexicalSearchQuery bar_q;
        bar_q.terms = {"bar"};
        bar_q.limit = 10;
        if(!df_index.search(bar_q).empty()) {
            return fail("Fix9a: 'bar' must produce no hits after its only chunk is erased");
        }
        // And 'foo' still has 1 hit (chunk:b).
        agent_memory::LexicalSearchQuery foo_q;
        foo_q.terms = {"foo"};
        foo_q.limit = 10;
        const auto foo_results = df_index.search(foo_q);
        if(foo_results.size() != 1 || foo_results[0].chunk_id != agent_memory::ChunkId{"chunk:b"}) {
            return fail("Fix9a: 'foo' must still match chunk:b after erasing chunk:a");
        }
    }

    // Fix 9b: search on an empty corpus must return no results and
    // must not throw.
    {
        agent_memory::ExactLexicalIndex empty_index;
        agent_memory::LexicalSearchQuery q;
        q.terms = {"foo"};
        q.limit = 10;
        const auto empty_results = empty_index.search(q);
        if(!empty_results.empty()) {
            return fail("Fix9b: empty corpus must return no results");
        }
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
