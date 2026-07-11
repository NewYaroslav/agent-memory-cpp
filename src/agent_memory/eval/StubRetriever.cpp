#include "StubRetriever.hpp"

#include <agent_memory/domain/Document.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <random>
#include <string>
#include <utility>

namespace agent_memory {

    namespace {

        constexpr std::string_view kExactIdPrefix = "id:";

        // Builds a retrieved chunk carrying the supplied item id at the given
        // score. The chunk's text and document id mirror the corpus item id so
        // downstream adapters have a stable lookup key.
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

    StubRetriever::StubRetriever(
        std::vector<std::string> corpus_ids,
        std::uint32_t seed
    ) : m_corpus_ids(std::move(corpus_ids)), m_seed(seed) {}

    RetrievalResult StubRetriever::retrieve(const RetrievalQuery& query) const {
        RetrievalResult result;
        if(query.limit == 0 || m_corpus_ids.empty()) {
            return result;
        }
        const auto limit = std::min<std::size_t>(query.limit, m_corpus_ids.size());

        // Exact-id fast path: queries prefixed with `id:` bubble the matching
        // corpus item to rank one. Remaining slots are filled by a stable
        // shuffle keyed off the query text and the constructor seed.
        if(query.text.rfind(kExactIdPrefix, 0) == 0) {
            const auto target = query.text.substr(kExactIdPrefix.size());
            const auto found = std::find(
                m_corpus_ids.begin(),
                m_corpus_ids.end(),
                target
            );
            if(found != m_corpus_ids.end()) {
                const auto index = static_cast<std::size_t>(
                    std::distance(m_corpus_ids.begin(), found)
                );
                result.chunks.push_back(make_chunk(m_corpus_ids[index], 1.0F));
            }
        }

        const auto seed_for_query = static_cast<std::uint32_t>(
            m_seed + std::hash<std::string>{}(query.text)
        );
        std::vector<std::size_t> order(m_corpus_ids.size());
        for(std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::mt19937 rng(seed_for_query);
        std::shuffle(order.begin(), order.end(), rng);

        for(const auto corpus_index : order) {
            if(result.chunks.size() >= limit) {
                break;
            }
            const auto& emitted_id = m_corpus_ids[corpus_index];
            const bool already_emitted = std::any_of(
                result.chunks.begin(),
                result.chunks.end(),
                [&](const RetrievedChunk& existing) {
                    return existing.chunk.id.value() == emitted_id;
                }
            );
            if(already_emitted) {
                continue;
            }
            // Degrade scores after the first slot so higher scores rank first.
            const float score = result.empty()
                ? 1.0F
                : (1.0F - 0.001F * static_cast<float>(result.size()));
            result.chunks.push_back(make_chunk(emitted_id, score));
        }

        return result;
    }

} // namespace agent_memory