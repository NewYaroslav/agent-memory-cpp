#include "BowVectorRetriever.hpp"

#include <agent_memory/domain/Document.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agent_memory {

    namespace {

        RetrievedChunk make_chunk(const std::string& item_id, float score) {
            DocumentChunk chunk;
            chunk.id = ChunkId{item_id};
            chunk.document_id = DocumentId{item_id};
            chunk.source_range = TextRange{0, item_id.size()};
            chunk.text = item_id;
            RetrievedChunk retrieved;
            retrieved.chunk = std::move(chunk);
            retrieved.score = score;
            return retrieved;
        }

    } // namespace

    BowVectorRetriever::BowVectorRetriever(
        std::vector<std::string> corpus_ids,
        std::vector<std::string> corpus_texts,
        std::uint32_t seed
    ) : m_corpus_ids(std::move(corpus_ids)) {
        // Seed parameter is intentionally unused; kept in the signature so
        // a future stochastic embedder can be swapped in without churn.
        (void)seed;
        if(m_corpus_ids.size() != corpus_texts.size()) {
            throw std::invalid_argument(
                "BowVectorRetriever: corpus_ids and corpus_texts must be parallel"
            );
        }
        for(const auto& text : corpus_texts) {
            m_embedder.add_corpus_text(text);
        }
        m_embedder.build();
        for(std::size_t i = 0; i < corpus_texts.size(); ++i) {
            m_index.add(m_corpus_ids[i], m_embedder.embed(corpus_texts[i]));
        }
        m_index.build();
    }

    RetrievalResult BowVectorRetriever::retrieve(const RetrievalQuery& query) const {
        RetrievalResult result;
        if(query.limit == 0 || m_corpus_ids.empty()) {
            return result;
        }
        if(!query.has_text()) {
            return result;
        }

        const auto query_vector = m_embedder.embed(query.text);
        if(query_vector.empty()) {
            return result;
        }

        const auto limit = std::min<std::size_t>(query.limit, m_corpus_ids.size());
        const auto hits = m_index.top_k(query_vector, limit);
        result.chunks.reserve(hits.size());
        for(const auto& hit : hits) {
            result.chunks.push_back(
                make_chunk(hit.first, static_cast<float>(hit.second))
            );
        }
        return result;
    }

} // namespace agent_memory