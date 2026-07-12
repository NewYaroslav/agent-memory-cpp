#include <agent_memory.hpp>

#include <agent_memory/retrieval/BowVectorRetriever.hpp>
#include <agent_memory/retrieval/ExactLexicalRetriever.hpp>

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
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
        ExactLexicalRetriever retriever(ids, texts);

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
        ExactLexicalRetriever retriever_a(ids, texts);
        ExactLexicalRetriever retriever_b(ids, texts);

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
        ExactLexicalRetriever retriever(ids, texts);

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

    // 3a) Higher score than the next hit: top-K results are strictly
    //     descending by score (ties broken by chunk id ascending).
    {
        const auto ids = corpus_ids();
        const auto texts = corpus_texts();
        ExactLexicalRetriever retriever(ids, texts);

        agent_memory::RetrievalQuery query;
        query.text = "the cat sat on the mat";
        query.limit = 10;
        const auto result = retriever.retrieve(query);
        if(result.size() < 2) {
            return fail("scoring: query must return at least two hits");
        }
        if(!(result.chunks[0].score > result.chunks[1].score)) {
            return fail("scoring: top-K results must be strictly descending by score");
        }
    }

    // 4) Empty query text → empty result.
    {
        const auto ids = corpus_ids();
        const auto texts = corpus_texts();
        ExactLexicalRetriever retriever(ids, texts);

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
            ExactLexicalRetriever bad(
                std::vector<std::string>{"d:a", "d:b"},
                std::vector<std::string>{"only one"}
            );
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
        ExactLexicalRetriever retriever(
            std::vector<std::string>{},
            std::vector<std::string>{}
        );
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
        ExactLexicalRetriever retriever(ids, texts);

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
        ExactLexicalRetriever retriever(ids, texts);

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

        ExactLexicalRetriever lexical(ids, texts);
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

    return 0;
}
