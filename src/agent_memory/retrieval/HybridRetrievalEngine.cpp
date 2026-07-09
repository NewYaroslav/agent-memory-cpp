#include "HybridRetrievalEngine.hpp"

#include "../lexical/IQueryAnalyzer.hpp"
#include "../lexical/IReranker.hpp"

#include <utility>

namespace agent_memory {

    HybridRetrievalEngine::HybridRetrievalEngine()
        : m_reranker(std::make_unique<IdentityReranker>())
        , m_analyzer(std::make_unique<PassthroughQueryAnalyzer>()) {}

    HybridRetrievalEngine::HybridRetrievalEngine(const ILexicalIndex& index)
        : m_index(&index)
        , m_reranker(std::make_unique<IdentityReranker>())
        , m_analyzer(std::make_unique<PassthroughQueryAnalyzer>()) {}

    void HybridRetrievalEngine::set_lexical_index(const ILexicalIndex& index) noexcept {
        m_index = &index;
    }

    void HybridRetrievalEngine::set_reranker(std::unique_ptr<IReranker> reranker) noexcept {
        if(reranker) {
            m_reranker = std::move(reranker);
        } else {
            m_reranker = std::make_unique<IdentityReranker>();
        }
    }

    void HybridRetrievalEngine::set_query_analyzer(std::unique_ptr<IQueryAnalyzer> analyzer) noexcept {
        if(analyzer) {
            m_analyzer = std::move(analyzer);
        } else {
            m_analyzer = std::make_unique<PassthroughQueryAnalyzer>();
        }
    }

    RetrievalResponse HybridRetrievalEngine::retrieve(
        const RetrievalRequest& request
    ) const {
        RetrievalResponse response;

        if(request.limit == 0 || request.query.empty() || m_index == nullptr) {
            return response;
        }

        const QueryAnalysis analysis = m_analyzer->analyze(request.query);

        LexicalSearchQuery lexical_query;
        lexical_query.terms = analysis.keywords;
        if(lexical_query.terms.empty()) {
            lexical_query.terms.push_back(request.query);
        }
        lexical_query.limit = request.limit;
        lexical_query.metadata_filters = request.metadata_filters;

        std::vector<LexicalSearchResult> hits = m_index->search(lexical_query);
        if(hits.size() > request.limit) {
            hits.resize(request.limit);
        }

        hits = m_reranker->rerank(request.query, std::move(hits));

        response.items.reserve(hits.size());
        for(const auto& hit : hits) {
            RetrievalResponseItem item;
            item.lexical = hit;
            // The lexical hit already carries section_id, resource_id,
            // and result_tier; readers should pull those values from
            // item.lexical. We only copy chunk-id and metadata onto
            // item.object, plus the enrichment level carried by the
            // upstream enricher.
            item.object.type = ObjectType::Chunk;
            item.object.metadata = hit.metadata;
            item.object.enrichment_level = hit.enrichment_level;
            if(hit.result_tier == 0) {
                item.object.id = MemoryObjectId{std::string(hit.chunk_id.value())};
            }
            response.items.push_back(std::move(item));
        }

        return response;
    }

} // namespace agent_memory