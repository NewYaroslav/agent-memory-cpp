#include <agent_memory.hpp>

#include <agent_memory/lexical/ITokenizer.hpp>
#include <agent_memory/lexical/StandardTokenizer.hpp>
#include <agent_memory/lexical/Tokenizer.hpp>
#include <agent_memory/retrieval/BowVectorRetriever.hpp>
#include <agent_memory/retrieval/ExactLexicalRetriever.hpp>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    int fail(const std::string& message) {
        std::cerr << "exact_lexical_retriever_test: " << message << '\n';
        return 1;
    }

    // Hand-crafted corpus: ten short documents with deliberate token overlap.
    // Same shape as `exact_vector_retriever_test` so cross-baseline sanity
    // checks remain comparable. The corpus_ids are kept short to keep the
    // tie-break path (id ascending) easy to reason about.
    std::vector<std::string> corpus_ids() {
        return {
            "d:cat-sat",     "d:cat-ran",   "d:dog-barked", "d:dog-ran",
            "d:bird-flew",   "d:fish-swam", "d:cat-couch",  "d:dog-park",
            "d:bird-tree",   "d:cat-yard"
        };
    }

    std::vector<std::string> corpus_texts() {
        return {
            "the cat sat on the mat",
            "the cat ran across the yard",
            "the dog barked at the cat",
            "the dog ran across the yard",
            "the bird flew over the house",
            "the fish swam in the pond",
            "the cat sat on the couch",
            "the dog played in the park",
            "the bird sat in the tree",
            "the cat wandered in the yard"
        };
    }

    /// \brief Counting tokenizer used to verify the retriever actually
    ///        invokes the injected tokenizer. Always emits at least one
    ///        token so the corpus does not trip the empty-tokens guard.
    class CountingTokenizer final : public agent_memory::ITokenizer {
    public:
        mutable std::size_t tokenize_calls = 0;

        [[nodiscard]] agent_memory::TokenizationResult tokenize(
            std::string_view text,
            const agent_memory::TokenizeOptions& /*options*/
        ) const override {
            ++tokenize_calls;
            agent_memory::TokenizationResult result;
            agent_memory::Token t;
            t.text = std::string{text};
            t.source_range = agent_memory::TextRange{0, text.size()};
            t.position = 0;
            result.tokens.push_back(std::move(t));
            return result;
        }
    };

} // namespace

int main() {
    using agent_memory::ExactLexicalRetriever;
    using agent_memory::BowVectorRetriever;

    // 1) Self-test: a query that exactly matches one document must rank
    //    that document first. This is the canonical ranking sanity case.
    {
        const std::vector<std::string> ids = {"d:cat", "d:dog"};
        const std::vector<std::string> texts = {
            "the cat sat on the mat",
            "the dog barked at the cat"
        };
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "cat sat mat";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.empty()) {
            return fail("self-test: cat query must return at least one hit");
        }
        if(result.chunks.front().chunk.id.value() != "d:cat") {
            return fail(
                "self-test: cat query must rank the cat doc first; got: "
                    + result.chunks.front().chunk.id.value()
            );
        }
    }

    // 2) Determinism: identical inputs build a byte-equal result list.
    {
        const auto ids = corpus_ids();
        const auto texts = corpus_texts();
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever_a(ids, texts, metadata);
        ExactLexicalRetriever retriever_b(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "the cat sat on the mat";
        query.limit = 10;
        const auto result_a = retriever_a.retrieve(query);
        const auto result_b = retriever_b.retrieve(query);
        if(result_a.chunks.size() != result_b.chunks.size()) {
            return fail("determinism: identical retriever builds must return same size");
        }
        for(std::size_t i = 0; i < result_a.chunks.size(); ++i) {
            if(result_a.chunks[i].chunk.id.value() != result_b.chunks[i].chunk.id.value()) {
                return fail("determinism: hit order must match across builds");
            }
            if(result_a.chunks[i].score != result_b.chunks[i].score) {
                return fail("determinism: hit scores must match across builds");
            }
        }
    }

    // 3) Top-K ordering: a query that exactly matches one document must
    //    rank that document first against the handcrafted corpus.
    {
        const auto ids = corpus_ids();
        const auto texts = corpus_texts();
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "the cat sat on the mat";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.empty()) {
            return fail("ranking: query must return at least one hit");
        }
        const std::string top = result.chunks.front().chunk.id.value();
        if(top != "d:cat-sat") {
            return fail(
                "ranking: cat-sat query must rank d:cat-sat first; got: " + top
            );
        }
    }

    // 3a) Top-K ranking contract: scores are non-increasing, and ties
    //     are broken by ascending chunk id. Construct a corpus that
    //     produces at least one tied pair so the tie-break path is
    //     exercised end-to-end.
    {
        const std::vector<std::string> ids = {"d:a", "d:b", "d:c"};
        const std::vector<std::string> texts = {
            "alpha beta gamma",
            "alpha delta epsilon",
            "completely unrelated tokens"
        };
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "alpha";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.size() != 2) {
            return fail(
                "scoring: tied corpus must yield exactly two hits; got: "
                    + std::to_string(result.size())
            );
        }
        if(!(result.chunks[0].score >= result.chunks[1].score)) {
            return fail("scoring: top-K scores must be non-increasing");
        }
        if(result.chunks[0].score == result.chunks[1].score) {
            if(!(result.chunks[0].chunk.id.value() < result.chunks[1].chunk.id.value())) {
                return fail("scoring: tied scores must break by chunk_id ascending");
            }
        }
    }

    // 4) Empty query text → empty result.
    {
        const auto ids = corpus_ids();
        const auto texts = corpus_texts();
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(!result.empty()) {
            return fail("empty query text must yield empty result");
        }
    }

    // 5) Mismatched corpus_ids / corpus_texts sizes → throws
    //    std::invalid_argument.
    {
        bool threw_invalid_argument = false;
        try {
            const std::vector<std::string> bad_ids{"d:a", "d:b"};
            const std::vector<std::string> bad_texts{"only one"};
            const std::vector<agent_memory::Metadata> bad_metadata(bad_ids.size());
            ExactLexicalRetriever bad(bad_ids, bad_texts, bad_metadata);
        } catch(const std::invalid_argument&) {
            threw_invalid_argument = true;
        } catch(...) {
            return fail(
                "size mismatch must throw std::invalid_argument, not other type"
            );
        }
        if(!threw_invalid_argument) {
            return fail("size mismatch must throw std::invalid_argument");
        }
    }

    // 6) Empty corpus: constructor succeeds and `retrieve` returns empty.
    {
        const std::vector<std::string> ids;
        const std::vector<std::string> texts;
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);
        agent_memory::RetrievalQuery query;
        query.text = "anything";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(!result.empty()) {
            return fail("empty corpus must yield empty result");
        }
    }

    // 7) Limit truncation: requesting fewer than corpus_size caps the result.
    {
        const auto ids = corpus_ids();
        const auto texts = corpus_texts();
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "the";
        query.limit = 3;
        const auto result = retriever.retrieve(query);
        if(result.chunks.size() > 3) {
            return fail(
                "limit: query.limit=3 must cap result to <=3; got: "
                    + std::to_string(result.chunks.size())
            );
        }
    }

    // 8) limit == 0 → empty result.
    {
        const auto ids = corpus_ids();
        const auto texts = corpus_texts();
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "the";
        query.limit = 0;
        const auto result = retriever.retrieve(query);
        if(!result.empty()) {
            return fail("limit=0 must yield empty result");
        }
    }

    // 9) Cross-baseline sanity vs BowVectorRetriever: both retrievers must
    //    rank a fully-matching document at rank 1 for the same query on a
    //    small corpus. This is the integration test that justifies having
    //    two baselines (one BoW / cosine, one BM25 / lexical).
    {
        const std::vector<std::string> ids = {"d:cat", "d:dog", "d:bird"};
        const std::vector<std::string> texts = {
            "the cat sat on the mat",
            "the dog barked at the cat",
            "the bird flew over the house"
        };
        const std::vector<agent_memory::Metadata> metadata(ids.size());

        ExactLexicalRetriever lexical(ids, texts, metadata);
        BowVectorRetriever bow(ids, texts, /*seed=*/0);

        agent_memory::RetrievalQuery query;
        query.text = "the cat sat on the mat";
        query.limit = 3;

        const auto lex_result = lexical.retrieve(query);
        const auto bow_result = bow.retrieve(query);

        if(lex_result.empty() || bow_result.empty()) {
            return fail("cross-baseline: both retrievers must return at least one hit");
        }
        if(lex_result.chunks.front().chunk.id.value() != "d:cat") {
            return fail(
                "cross-baseline: lexical retriever must rank d:cat first; got: "
                    + lex_result.chunks.front().chunk.id.value()
            );
        }
        if(bow_result.chunks.front().chunk.id.value() != "d:cat") {
            return fail(
                "cross-baseline: bow retriever must rank d:cat first; got: "
                    + bow_result.chunks.front().chunk.id.value()
            );
        }
    }

    // 10) throws_on_empty_corpus_id: corpus with an empty id must throw
    //     std::invalid_argument.
    {
        const std::vector<std::string> ids = {"", "doc:1"};
        const std::vector<std::string> texts = {"a", "b"};
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        try {
            ExactLexicalRetriever bad(ids, texts, metadata);
            return fail(
                "empty_corpus_id: constructor must throw on empty corpus_id"
            );
        } catch(const std::invalid_argument&) {
        } catch(...) {
            return fail(
                "empty_corpus_id: must throw std::invalid_argument, not other type"
            );
        }
    }

    // 11) throws_on_duplicate_corpus_id: duplicate ids must throw.
    {
        const std::vector<std::string> ids = {"doc:a", "doc:a"};
        const std::vector<std::string> texts = {"x", "y"};
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        bool threw_invalid_argument = false;
        bool threw_with_duplicate_message = false;
        try {
            ExactLexicalRetriever bad(ids, texts, metadata);
        } catch(const std::invalid_argument& ex) {
            threw_invalid_argument = true;
            const std::string what{ex.what()};
            threw_with_duplicate_message = what.find("duplicate") != std::string::npos;
        } catch(...) {
            return fail("duplicate_corpus_id: must throw std::invalid_argument");
        }
        if(!threw_invalid_argument) {
            return fail("duplicate_corpus_id: constructor must throw");
        }
        if(!threw_with_duplicate_message) {
            return fail("duplicate_corpus_id: error message must mention 'duplicate'");
        }
    }

    // 12) throws_on_metadata_size_mismatch: metadata vector length must
    //     match ids length.
    {
        const std::vector<std::string> ids = {"doc:1", "doc:2", "doc:3"};
        const std::vector<std::string> texts = {"x", "y", "z"};
        const std::vector<agent_memory::Metadata> metadata(2);
        try {
            ExactLexicalRetriever bad(ids, texts, metadata);
            return fail(
                "metadata_size_mismatch: constructor must throw on size mismatch"
            );
        } catch(const std::invalid_argument&) {
        } catch(...) {
            return fail(
                "metadata_size_mismatch: must throw std::invalid_argument"
            );
        }
    }

    // 13) throws_on_k_neighbours_max_zero: a 0 cap must throw.
    {
        const std::vector<std::string> ids = {"doc:1"};
        const std::vector<std::string> texts = {"hello world"};
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        agent_memory::StandardTokenizer tok;
        try {
            ExactLexicalRetriever bad(
                ids,
                texts,
                metadata,
                tok,
                /*k_neighbours_max=*/0
            );
            return fail(
                "k_neighbours_max_zero: constructor must throw on cap=0"
            );
        } catch(const std::invalid_argument&) {
        } catch(...) {
            return fail(
                "k_neighbours_max_zero: must throw std::invalid_argument"
            );
        }
    }

    // 14) clamps_k_below_query_limit: with k_neighbours_max=2 and
    //     query.limit=10, the result must contain exactly 2 chunks.
    {
        const std::vector<std::string> ids = {
            "d:a", "d:b", "d:c", "d:d", "d:e", "d:f"
        };
        const std::vector<std::string> texts = {
            "the cat sat on the mat",
            "the dog barked at the cat",
            "the bird flew over the house",
            "the fish swam in the pond",
            "the cat played in the yard",
            "the dog sat on the couch"
        };
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        agent_memory::StandardTokenizer tok;
        ExactLexicalRetriever retriever(
            ids,
            texts,
            metadata,
            tok,
            /*k_neighbours_max=*/2
        );
        agent_memory::RetrievalQuery query;
        query.text = "the";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.chunks.size() != 2) {
            return fail(
                "k_neighbours_max: cap=2 must clamp result to 2 chunks; got: "
                    + std::to_string(result.chunks.size())
            );
        }
    }

    // 15) chunk_text_equals_corpus_text: each hit's chunk.text must equal
    //     the original corpus text and source_range.length must match.
    {
        const std::vector<std::string> ids = {"doc:1", "doc:2"};
        const std::vector<std::string> texts = {
            "the BM25 paper",
            "another doc"
        };
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "BM25";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.empty()) {
            return fail("chunk_text_equals_corpus_text: query must return hits");
        }
        for(const auto& hit : result.chunks) {
            const auto& id = hit.chunk.id.value();
            const auto index = (id == "doc:1") ? 0u : 1u;
            if(hit.chunk.text != texts[index]) {
                return fail(
                    "chunk_text_equals_corpus_text: chunk.text must match corpus text for "
                        + id
                );
            }
            if(hit.chunk.source_range.length != texts[index].size()) {
                return fail(
                    "chunk_text_equals_corpus_text: source_range length must match text size for "
                        + id
                );
            }
        }
    }

    // 16) metadata_filter_matches_returns_hits: corpus with metadata,
    //     matching query filter must surface hits.
    {
        std::vector<agent_memory::Metadata> metadata(3);
        metadata[0].set("lang", "en");
        metadata[1].set("lang", "fr");
        metadata[2].set("lang", "en");

        const std::vector<std::string> ids = {"d:en-1", "d:fr-1", "d:en-2"};
        const std::vector<std::string> texts = {
            "the cat sat on the mat",
            "the dog barked at the cat",
            "the cat played in the yard"
        };
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "cat";
        query.limit = 10;
        query.metadata_filters.push_back({"lang", "en"});

        const auto result = retriever.retrieve(query);
        if(result.empty()) {
            return fail(
                "metadata_filter_matches: matching filter must surface hits"
            );
        }
        bool saw_non_en = false;
        for(const auto& hit : result.chunks) {
            const auto& id = hit.chunk.id.value();
            if(id != "d:en-1" && id != "d:en-2") {
                saw_non_en = true;
                break;
            }
        }
        if(saw_non_en) {
            return fail(
                "metadata_filter_matches: non-matching record leaked through filter"
            );
        }
    }

    // 17) metadata_filter_mismatch_returns_empty: a non-matching filter
    //     must drop every hit.
    {
        std::vector<agent_memory::Metadata> metadata(3);
        metadata[0].set("lang", "en");
        metadata[1].set("lang", "fr");
        metadata[2].set("lang", "en");

        const std::vector<std::string> ids = {"d:en-1", "d:fr-1", "d:en-2"};
        const std::vector<std::string> texts = {
            "the cat sat on the mat",
            "the dog barked at the cat",
            "the cat played in the yard"
        };
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "cat";
        query.limit = 10;
        query.metadata_filters.push_back({"lang", "de"});

        const auto result = retriever.retrieve(query);
        if(!result.empty()) {
            return fail("metadata_filter_mismatch: must yield empty result");
        }
    }

    // 17a) metadata_survives_to_retrieval_result: non-empty per-document
    //      metadata must round-trip from the corpus_metadata passed at
    //      construction through to RetrievedChunk.metadata on hits.
    {
        std::vector<agent_memory::Metadata> metadata(2);
        metadata[0].set("lang", "en");
        metadata[0].set("topic", "a");
        metadata[1].set("lang", "de");

        const std::vector<std::string> ids = {"doc:1", "doc:2"};
        const std::vector<std::string> texts = {
            "alpha bravo",
            "charlie delta"
        };
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "alpha";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.empty()) {
            return fail(
                "metadata_round_trip: alpha query must return at least one hit"
            );
        }
        if(result.chunks.front().chunk.id.value() != "doc:1") {
            return fail(
                "metadata_round_trip: alpha query must rank doc:1 first; got: "
                    + result.chunks.front().chunk.id.value()
            );
        }
        const auto lang = result.chunks.front().metadata.get("lang");
        if(!lang || *lang != "en") {
            return fail(
                "metadata_round_trip: metadata.lang must survive to retrieval result"
            );
        }
        const auto topic = result.chunks.front().metadata.get("topic");
        if(!topic || *topic != "a") {
            return fail(
                "metadata_round_trip: metadata.topic must survive to retrieval result"
            );
        }
    }

    // 17b) metadata_default_overload_uses_empty_metadata: the 4-arg
    //      constructor (no corpus_metadata param) must produce hits
    //      whose RetrievedChunk.metadata is empty.
    {
        const std::vector<std::string> ids = {"doc:1"};
        const std::vector<std::string> texts = {"alpha bravo"};
        agent_memory::StandardTokenizer tok;
        ExactLexicalRetriever retriever(ids, texts, tok, /*k_neighbours_max=*/1024);

        agent_memory::RetrievalQuery query;
        query.text = "alpha";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.empty()) {
            return fail(
                "metadata_default_overload: alpha query must return at least one hit"
            );
        }
        if(!result.chunks.front().metadata.empty()) {
            return fail(
                "metadata_default_overload: default overload must produce empty metadata"
            );
        }
    }

    // 18) oov_query_returns_empty: every term absent from corpus → empty.
    {
        const auto ids = corpus_ids();
        const auto texts = corpus_texts();
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "zzzqqqxxx";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(!result.empty()) {
            return fail("oov_query: out-of-vocabulary query must be empty");
        }
    }

    // 19) tie_break_by_chunk_id_ascending: a corpus that yields identical
    //     BM25 scores across two records must order hits by chunk id
    //     ascending. Two docs of equal length both containing a single
    //     shared query term produce tied scores under BM25.
    {
        const std::vector<std::string> ids = {"d:zebra", "d:alpha", "d:mango"};
        const std::vector<std::string> texts = {
            "alpha unrelated filler word",
            "alpha unrelated filler word",
            "completely different content here entirely"
        };
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata);

        agent_memory::RetrievalQuery query;
        query.text = "alpha";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.size() != 2) {
            return fail(
                "tie_break: tied corpus must yield exactly two hits; got: "
                    + std::to_string(result.size())
            );
        }
        if(result.chunks[0].score != result.chunks[1].score) {
            return fail("tie_break: scores must be identical");
        }
        if(result.chunks[0].chunk.id.value() != "d:alpha") {
            return fail(
                "tie_break: lower chunk_id must rank first; got: "
                    + result.chunks[0].chunk.id.value()
            );
        }
        if(result.chunks[1].chunk.id.value() != "d:zebra") {
            return fail(
                "tie_break: higher chunk_id must rank second; got: "
                    + result.chunks[1].chunk.id.value()
            );
        }
    }

    // 20) custom_tokenizer_is_used: a custom tokenizer must actually be
    //     invoked during ingestion and querying. We expose the call
    //     count via a side-channel counter and assert it grew.
    {
        CountingTokenizer custom;
        const std::vector<std::string> ids = {"d:1"};
        const std::vector<std::string> texts = {"hello world"};
        const std::vector<agent_memory::Metadata> metadata(ids.size());
        ExactLexicalRetriever retriever(ids, texts, metadata, custom);
        agent_memory::RetrievalQuery query;
        query.text = "hello";
        query.limit = 10;
        (void)retriever.retrieve(query);
        if(custom.tokenize_calls < 2) {
            return fail(
                "custom_tokenizer: tokenizer must be invoked at least twice"
            );
        }
    }

    return 0;
}
