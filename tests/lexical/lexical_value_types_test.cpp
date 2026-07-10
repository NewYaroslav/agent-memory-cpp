#include <agent_memory.hpp>

#include <iostream>
#include <string_view>

namespace {

    int fail(const std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    int test_default_token_id_is_empty() {
        const agent_memory::TokenId empty_id;
        if(!empty_id.empty()) {
            return fail("default token id must be empty");
        }
        return 0;
    }

    int test_token_id_value() {
        const agent_memory::TokenId alpha_id{1};
        if(alpha_id.empty() || alpha_id.value() != 1) {
            return fail("token id must expose its numeric value");
        }
        return 0;
    }

    int test_token_id_equality_and_ordering() {
        const agent_memory::TokenId alpha_id{1};
        const agent_memory::TokenId same_alpha_id{1};
        const agent_memory::TokenId beta_id{2};
        if(alpha_id != same_alpha_id || !(alpha_id < beta_id)) {
            return fail("token id must support equality and deterministic ordering");
        }
        return 0;
    }

    int test_default_bm25_options_valid() {
        const agent_memory::Bm25Options default_bm25;
        if(!agent_memory::is_valid(default_bm25)) {
            return fail("default BM25 options must be valid");
        }
        return 0;
    }

    int test_bm25_k1_must_be_positive() {
        if(agent_memory::is_valid(agent_memory::Bm25Options{0.0F, 0.75F})) {
            return fail("BM25 k1 must be positive");
        }
        return 0;
    }

    int test_bm25_b_in_unit_interval() {
        if(agent_memory::is_valid(agent_memory::Bm25Options{1.5F, 1.5F})) {
            return fail("BM25 b must stay inside [0, 1]");
        }
        return 0;
    }

    int test_record_with_tokens_not_empty() {
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
        return 0;
    }

    int test_lexical_stats_counts() {
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
        return 0;
    }

    int test_posting_alignment() {
        const agent_memory::ResourceRevision revision{
            agent_memory::ResourceId{"resource:alpha"},
            3,
            11,
            17
        };
        const agent_memory::LexicalPosting posting{
            agent_memory::TokenId{1},
            agent_memory::ChunkId{"chunk:alpha:0"},
            revision,
            2,
            {0, 4}
        };
        if(posting.term_frequency != posting.positions.size()) {
            return fail("posting positions should line up with term frequency in tests");
        }
        return 0;
    }

    int test_default_lexical_query() {
        agent_memory::LexicalSearchQuery query;
        if(!query.empty() || query.limit != 10) {
            return fail("default lexical query must be empty with default limit");
        }
        return 0;
    }

    int test_lexical_query_terms_and_filters() {
        agent_memory::LexicalSearchQuery query;
        query.terms = {"agent", "memory"};
        query.metadata_filters.push_back(agent_memory::MetadataFilter{"language", "en"});
        if(query.empty() || query.metadata_filters.size() != 1) {
            return fail("lexical query must store normalized terms and metadata filters");
        }
        return 0;
    }

    int test_lexical_search_result() {
        const agent_memory::LexicalSearchResult result{
            agent_memory::ChunkId{"chunk:alpha:0"},
            4.25F,
            agent_memory::Metadata{}
        };
        if(result.score <= 0.0F || result.chunk_id != agent_memory::ChunkId{"chunk:alpha:0"}) {
            return fail("lexical search result must expose chunk id and higher-is-better score");
        }
        return 0;
    }

    int test_matches_metadata_filters_accepts_matching() {
        agent_memory::Metadata record;
        record.set("language", "en");
        record.set("category", "docs");

        if(!agent_memory::matches_metadata_filters(record, {{"language", "en"}})) {
            return fail("matches_metadata_filters must accept matching metadata");
        }
        if(agent_memory::matches_metadata_filters(record, {{"category", "src"}})) {
            return fail("matches_metadata_filters must reject mismatched metadata");
        }
        return 0;
    }

} // namespace

int main() {
    int failures = 0;
    failures += test_default_token_id_is_empty();
    failures += test_token_id_value();
    failures += test_token_id_equality_and_ordering();
    failures += test_default_bm25_options_valid();
    failures += test_bm25_k1_must_be_positive();
    failures += test_bm25_b_in_unit_interval();
    failures += test_record_with_tokens_not_empty();
    failures += test_lexical_stats_counts();
    failures += test_posting_alignment();
    failures += test_default_lexical_query();
    failures += test_lexical_query_terms_and_filters();
    failures += test_lexical_search_result();
    failures += test_matches_metadata_filters_accepts_matching();
    return failures > 0 ? 1 : 0;
}