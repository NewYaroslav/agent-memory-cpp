#include <agent_memory.hpp>

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

    int fail(const std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    // -- QueryType / RetrievalMode / QueryAnalysis ----------------------------

    int test_query_type_enum_is_compile_time_in_range() {
        static_assert(static_cast<int>(agent_memory::QueryType::SemanticLookup) == 0);
        static_assert(static_cast<int>(agent_memory::QueryType::DiagnosticQuery) >= 7);
        const auto type = agent_memory::QueryType::SemanticLookup;
        if(type != agent_memory::QueryType::SemanticLookup) {
            return fail("QueryType::SemanticLookup must be comparable");
        }
        return 0;
    }

    int test_retrieval_mode_enum_is_compile_time_in_range() {
        static_assert(static_cast<int>(agent_memory::RetrievalMode::HybridSearch) == 0);
        static_assert(static_cast<int>(agent_memory::RetrievalMode::ChatThreadSearch) >= 5);
        return 0;
    }

    int test_query_analysis_default_is_identity() {
        agent_memory::QueryAnalysis analysis;
        if(!agent_memory::is_identity(analysis)) {
            return fail("default QueryAnalysis must be identity");
        }
        if(analysis.type != agent_memory::QueryType::SemanticLookup
            || analysis.mode != agent_memory::RetrievalMode::HybridSearch) {
            return fail("default QueryAnalysis must pick SemanticLookup + HybridSearch");
        }
        return 0;
    }

    int test_query_analysis_populated_is_not_identity() {
        agent_memory::QueryAnalysis analysis;
        analysis.rewritten = "rewritten text";
        if(agent_memory::is_identity(analysis)) {
            return fail("QueryAnalysis with rewritten text must not be identity");
        }
        return 0;
    }

    // -- Passthrough analyzer --------------------------------------------------

    int test_passthrough_query_analyzer_preserves_query() {
        agent_memory::PassthroughQueryAnalyzer analyzer;
        const auto analysis = analyzer.analyze("agent memory toolkit");
        if(analysis.original != "agent memory toolkit") {
            return fail("passthrough analyzer must preserve original query");
        }
        if(!analysis.rewritten.empty()) {
            return fail("passthrough analyzer must leave rewritten empty");
        }
        // Passthrough analyzer now performs a minimal whitespace split
        // so the default HybridRetrievalEngine pipeline can find
        // multi-term matches; keywords is therefore no longer empty.
        if(analysis.keywords.size() != 3
            || analysis.keywords[0] != "agent"
            || analysis.keywords[1] != "memory"
            || analysis.keywords[2] != "toolkit") {
            return fail("passthrough analyzer must populate keywords from whitespace split");
        }
        if(analysis.type != agent_memory::QueryType::SemanticLookup) {
            return fail("passthrough analyzer must default to SemanticLookup");
        }
        return 0;
    }

    int test_passthrough_query_analyzer_handles_empty_query() {
        agent_memory::PassthroughQueryAnalyzer analyzer;
        const auto analysis = analyzer.analyze("");
        if(analysis.original != "") {
            return fail("passthrough analyzer must accept empty input");
        }
        return 0;
    }

    // -- Passthrough enricher --------------------------------------------------

    int test_passthrough_enricher_returns_original() {
        agent_memory::PassthroughChunkEnricher enricher;
        const auto result = enricher.enrich("some chunk text");
        if(result.original_text != "some chunk text") {
            return fail("passthrough enricher must preserve original_text");
        }
        if(result.enriched_text != "some chunk text") {
            return fail("passthrough enricher must preserve enriched_text");
        }
        if(result.level != 0) {
            return fail("passthrough enricher must report level 0");
        }
        return 0;
    }

    // -- Identity reranker -----------------------------------------------------

    int test_identity_reranker_preserves_order() {
        agent_memory::IdentityReranker reranker;

        std::vector<agent_memory::LexicalSearchResult> candidates;
        for(int i = 0; i < 5; ++i) {
            agent_memory::LexicalSearchResult r;
            r.chunk_id = agent_memory::ChunkId{"chunk:" + std::to_string(i)};
            r.score = static_cast<float>(5 - i);
            candidates.push_back(std::move(r));
        }

        const auto reranked = reranker.rerank("test query", candidates);
        if(reranked.size() != candidates.size()) {
            return fail("identity reranker must not change cardinality");
        }
        for(std::size_t i = 0; i < reranked.size(); ++i) {
            if(reranked[i].chunk_id.value() != candidates[i].chunk_id.value()) {
                return fail("identity reranker must preserve chunk order");
            }
            if(reranked[i].score != candidates[i].score) {
                return fail("identity reranker must preserve scores");
            }
        }
        return 0;
    }

    // -- Memory hierarchy ------------------------------------------------------

    int test_memory_object_construction() {
        agent_memory::MemoryObject object;
        object.id = agent_memory::MemoryObjectId{"obj:1"};
        object.type = agent_memory::ObjectType::Section;
        object.resource_id = agent_memory::ResourceId{"resource:alpha"};
        object.section_id = 7;
        object.enrichment_level = 1;
        object.result_tier = 1;

        if(!agent_memory::is_valid(object)) {
            return fail("MemoryObject with id+resource+section must be valid");
        }
        if(object.section_id != 7 || object.enrichment_level != 1) {
            return fail("MemoryObject must retain section_id and enrichment_level");
        }
        if(agent_memory::to_string(object.type) != "section") {
            return fail("ObjectType::Section must stringify to 'section'");
        }
        return 0;
    }

    int test_memory_object_id_ordering() {
        const agent_memory::MemoryObjectId a{"alpha"};
        const agent_memory::MemoryObjectId b{"beta"};
        if(!(a < b)) {
            return fail("MemoryObjectId must support deterministic ordering");
        }
        if(a == b) {
            return fail("distinct MemoryObjectId must not compare equal");
        }
        return 0;
    }

    // -- Structured facts ------------------------------------------------------

    int test_fact_construction() {
        agent_memory::Fact fact;
        fact.id = agent_memory::FactId{"fact:1"};
        fact.subject = "inflation";
        fact.metric = "rate";
        fact.value = "3.4";
        fact.period = "2023";
        fact.source.resource_id = agent_memory::ResourceId{"resource:alpha"};
        fact.source.object_id = agent_memory::MemoryObjectId{"obj:1"};
        fact.confidence = 0.92F;

        if(!agent_memory::is_valid(fact)) {
            return fail("Fact with id+subject+value must be valid");
        }
        if(fact.value != "3.4" || fact.confidence.value_or(0.0F) < 0.9F) {
            return fail("Fact must retain value and confidence");
        }
        return 0;
    }

    int test_fact_id_ordering() {
        const agent_memory::FactId a{"a"};
        const agent_memory::FactId b{"b"};
        if(!(a < b) || a == b) {
            return fail("FactId must support deterministic ordering and inequality");
        }
        return 0;
    }

    // -- Chat memory -----------------------------------------------------------

    int test_message_construction() {
        agent_memory::Message msg;
        msg.id = agent_memory::MessageId{"m:1"};
        msg.thread_id = agent_memory::ThreadId{"thread:1"};
        msg.author_id = agent_memory::AuthorId{"user:42"};
        msg.role = agent_memory::AuthorRole::User;
        msg.text = "hello world";
        msg.timestamp_ms = 1700000000000LL;
        msg.reply_to_id = agent_memory::MessageId{"m:0"};

        if(!agent_memory::is_valid(msg)) {
            return fail("Message with id+thread+text must be valid");
        }
        if(!msg.reply_to_id.has_value() || msg.reply_to_id->value() != "m:0") {
            return fail("Message reply_to_id must be preserved");
        }
        if(agent_memory::to_string(msg.role) != "user") {
            return fail("AuthorRole::User must stringify to 'user'");
        }
        return 0;
    }

    int test_thread_id_and_author_id_inequality() {
        const agent_memory::ThreadId a{"a"};
        const agent_memory::ThreadId b{"b"};
        if(a == b || !(a < b)) {
            return fail("ThreadId must support inequality and ordering");
        }
        const agent_memory::AuthorId author_a{"x"};
        const agent_memory::AuthorId author_b{"y"};
        if(author_a == author_b || !(author_a < author_b)) {
            return fail("AuthorId must support inequality and ordering");
        }
        return 0;
    }

    // -- Lexical extension fields ---------------------------------------------

    int test_lexical_document_record_carries_section_and_enrichment() {
        agent_memory::LexicalDocumentRecord record;
        record.section_id = 42;
        record.enrichment_level = 2;
        if(record.section_id != 42 || record.enrichment_level != 2) {
            return fail("LexicalDocumentRecord must carry section_id and enrichment_level");
        }
        const agent_memory::LexicalDocumentRecord defaulted;
        if(defaulted.section_id != 0 || defaulted.enrichment_level != 0) {
            return fail("LexicalDocumentRecord defaults must be section_id=0, enrichment_level=0");
        }
        return 0;
    }

    int test_lexical_search_result_carries_section_resource_tier() {
        agent_memory::LexicalSearchResult hit;
        hit.section_id = 11;
        hit.resource_id = agent_memory::ResourceId{"resource:alpha"};
        hit.result_tier = 2;
        if(hit.section_id != 11 || hit.result_tier != 2) {
            return fail("LexicalSearchResult must carry section_id and result_tier");
        }
        if(hit.resource_id != agent_memory::ResourceId{"resource:alpha"}) {
            return fail("LexicalSearchResult must carry resource_id");
        }
        return 0;
    }

    // -- Hybrid retrieval engine smoke -----------------------------------------

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
            const auto inserted = m_chunks_by_resource[resource_id].insert(chunk_id);
            try {
                m_records.emplace(chunk_id, std::move(record));
            } catch(...) {
                if(inserted.second) {
                    m_chunks_by_resource[resource_id].erase(chunk_id);
                }
                throw;
            }
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
                    agent_memory::LexicalSearchResult hit;
                    hit.chunk_id = record.chunk_id;
                    hit.score = score;
                    hit.metadata = record.metadata;
                    hit.section_id = record.section_id;
                    hit.resource_id = record.revision.resource_id;
                    results.push_back(std::move(hit));
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

    [[nodiscard]] agent_memory::Token make_token(
        const std::string& text,
        const std::size_t position
    ) {
        return agent_memory::Token{
            text,
            agent_memory::TextRange{position, 1},
            position,
            agent_memory::TokenKind::Word
        };
    }

    int test_hybrid_retrieval_engine_returns_index_results() {
        InMemoryLexicalIndex index;
        agent_memory::LexicalDocumentRecord record;
        record.chunk_id = agent_memory::ChunkId{"chunk:alpha:0"};
        record.revision = agent_memory::ResourceRevision{
            agent_memory::ResourceId{"resource:alpha"},
            1,
            11,
            17
        };
        record.tokens = {
            make_token("agent", 0),
            make_token("memory", 1)
        };
        record.section_id = 5;
        index.upsert(std::move(record));

        agent_memory::HybridRetrievalEngine engine(index);
        agent_memory::RetrievalRequest request;
        request.query = "agent";
        request.limit = 5;

        const auto response = engine.retrieve(request);
        if(response.size() != 1) {
            return fail("HybridRetrievalEngine must return one result for matching query");
        }
        const auto& item = response.items.front();
        if(item.lexical.chunk_id.value() != "chunk:alpha:0") {
            return fail("HybridRetrievalEngine must forward chunk_id from index");
        }
        if(item.lexical.resource_id != agent_memory::ResourceId{"resource:alpha"}) {
            return fail("HybridRetrievalEngine must forward resource_id from index");
        }
        if(item.lexical.section_id != 5) {
            return fail("HybridRetrievalEngine must forward section_id from index");
        }
        if(item.object.type != agent_memory::ObjectType::Chunk) {
            return fail("HybridRetrievalEngine must surface MemoryObject of type Chunk");
        }
        if(item.object.id.value() != "chunk:alpha:0") {
            return fail("HybridRetrievalEngine must set object.id for chunk-tier results");
        }
        if(!item.object.resource_id.empty() || item.object.section_id != 0) {
            return fail("HybridRetrievalEngine must not duplicate section/resource onto MemoryObject; "
                        "callers should read them from item.lexical");
        }
        return 0;
    }

    int test_hybrid_retrieval_engine_respects_zero_limit() {
        InMemoryLexicalIndex index;
        agent_memory::HybridRetrievalEngine engine(index);

        agent_memory::RetrievalRequest request;
        request.query = "anything";
        request.limit = 0;

        const auto response = engine.retrieve(request);
        if(!response.empty()) {
            return fail("HybridRetrievalEngine must return empty response when limit == 0");
        }
        return 0;
    }

    int test_exact_lexical_index_populates_search_result_fields() {
        agent_memory::ExactLexicalIndex index;

        agent_memory::LexicalDocumentRecord record;
        record.chunk_id = agent_memory::ChunkId{"chunk:alpha:7"};
        record.revision = agent_memory::ResourceRevision{
            agent_memory::ResourceId{"resource:alpha"},
            1,
            11,
            17
        };
        record.tokens = {
            make_token("hello", 0),
            make_token("world", 1)
        };
        record.section_id = 42;
        record.enrichment_level = 2;
        index.upsert(std::move(record));

        agent_memory::LexicalSearchQuery query;
        query.terms = {"hello"};
        query.limit = 10;

        const auto hits = index.search(query);
        if(hits.empty()) {
            return fail("ExactLexicalIndex must return at least one hit for matching term");
        }
        const auto& front = hits.front();
        if(front.chunk_id != agent_memory::ChunkId{"chunk:alpha:7"}) {
            return fail("ExactLexicalIndex hit must preserve chunk_id from record");
        }
        if(front.section_id != 42) {
            return fail("ExactLexicalIndex hit must surface section_id from record");
        }
        if(front.resource_id != agent_memory::ResourceId{"resource:alpha"}) {
            return fail("ExactLexicalIndex hit must surface resource_id from record.revision");
        }
        if(front.enrichment_level != 2) {
            return fail("ExactLexicalIndex hit must surface enrichment_level from record");
        }
        if(front.result_tier != 0) {
            return fail("ExactLexicalIndex hit must report result_tier == 0 for chunk-level results");
        }
        return 0;
    }

} // namespace

int main() {
    int failures = 0;
    failures += test_query_type_enum_is_compile_time_in_range();
    failures += test_retrieval_mode_enum_is_compile_time_in_range();
    failures += test_query_analysis_default_is_identity();
    failures += test_query_analysis_populated_is_not_identity();
    failures += test_passthrough_query_analyzer_preserves_query();
    failures += test_passthrough_query_analyzer_handles_empty_query();
    failures += test_passthrough_enricher_returns_original();
    failures += test_identity_reranker_preserves_order();
    failures += test_memory_object_construction();
    failures += test_memory_object_id_ordering();
    failures += test_fact_construction();
    failures += test_fact_id_ordering();
    failures += test_message_construction();
    failures += test_thread_id_and_author_id_inequality();
    failures += test_lexical_document_record_carries_section_and_enrichment();
    failures += test_lexical_search_result_carries_section_resource_tier();
    failures += test_hybrid_retrieval_engine_returns_index_results();
    failures += test_hybrid_retrieval_engine_respects_zero_limit();
    failures += test_exact_lexical_index_populates_search_result_fields();
    return failures > 0 ? 1 : 0;
}