#include <agent_memory/AgentMemory.hpp>

#include <iostream>
#include <string_view>

namespace {

    int fail(const std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

} // namespace

int main() {
    const agent_memory::TokenId empty_id;
    const agent_memory::TokenId alpha_id{1};
    const agent_memory::TokenId same_alpha_id{1};
    const agent_memory::TokenId beta_id{2};

    if(!empty_id.empty()) {
        return fail("default token id must be empty");
    }

    if(alpha_id.empty() || alpha_id.value() != 1) {
        return fail("token id must expose its numeric value");
    }

    if(alpha_id != same_alpha_id || !(alpha_id < beta_id)) {
        return fail("token id must support equality and deterministic ordering");
    }

    const agent_memory::Bm25Options default_bm25;
    if(!agent_memory::is_valid(default_bm25)) {
        return fail("default BM25 options must be valid");
    }

    if(agent_memory::is_valid(agent_memory::Bm25Options{0.0F, 0.75F})) {
        return fail("BM25 k1 must be positive");
    }

    if(agent_memory::is_valid(agent_memory::Bm25Options{1.5F, 1.5F})) {
        return fail("BM25 b must stay inside [0, 1]");
    }

    const agent_memory::ResourceRevision revision{
        agent_memory::ResourceId{"resource:alpha"},
        3,
        11,
        17
    };

    agent_memory::LexicalDocumentRecord record{
        agent_memory::ChunkId{"chunk:alpha:0"},
        revision,
        {
            agent_memory::Token{
                "agent",
                agent_memory::TextRange{0, 5},
                0,
                agent_memory::TokenKind::Word
            },
            agent_memory::Token{
                "memory",
                agent_memory::TextRange{6, 6},
                1,
                agent_memory::TokenKind::Word
            }
        },
        {}
    };

    if(record.empty()) {
        return fail("record with tokens must not be empty");
    }

    record.metadata.set("language", "en");

    const agent_memory::LexicalDocumentStats stats{
        record.chunk_id,
        record.revision,
        record.tokens.size(),
        2,
        record.metadata
    };

    if(stats.token_count != 2 || stats.unique_token_count != 2) {
        return fail("lexical stats must store token and unique-token counts");
    }

    const agent_memory::LexicalPosting posting{
        alpha_id,
        record.chunk_id,
        revision,
        2,
        {0, 4}
    };

    if(posting.term_frequency != posting.positions.size()) {
        return fail("posting positions should line up with term frequency in tests");
    }

    agent_memory::LexicalSearchQuery query;
    if(!query.empty() || query.limit != 10) {
        return fail("default lexical query must be empty with default limit");
    }

    query.terms = {"agent", "memory"};
    query.metadata_filters.push_back(agent_memory::MetadataFilter{"language", "en"});

    if(query.empty() || query.metadata_filters.size() != 1) {
        return fail("lexical query must store normalized terms and metadata filters");
    }

    const agent_memory::LexicalSearchResult result{
        record.chunk_id,
        4.25F,
        record.metadata
    };

    if(result.score <= 0.0F || result.chunk_id != record.chunk_id) {
        return fail("lexical search result must expose chunk id and higher-is-better score");
    }

    return 0;
}
