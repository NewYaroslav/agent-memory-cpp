#include "KeywordOverlapIndex.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

namespace agent_memory {

    std::size_t KeywordOverlapIndex::size() const noexcept {
        return m_records.size();
    }

    void KeywordOverlapIndex::upsert(LexicalDocumentRecord record) {
        if(!is_valid(record)) {
            throw std::invalid_argument("invalid lexical document record");
        }

        const auto chunk_id = record.chunk_id;
        const auto resource_id = record.revision.resource_id;

        StoredRecord stored;
        stored.record = std::move(record);
        for(const auto& token : stored.record.tokens) {
            stored.unique_terms.insert(token.text);
        }

        std::optional<StoredRecord> old_snapshot;
        {
            const auto old_it = m_records.find(chunk_id);
            if(old_it != m_records.end()) {
                old_snapshot = old_it->second;
            }
        }

        if(old_snapshot.has_value()) {
            static_cast<void>(erase(chunk_id));
        }

        bool record_inserted = false;
        try {
            m_records.emplace(chunk_id, std::move(stored));
            record_inserted = true;

            m_chunks_by_resource[resource_id].insert(chunk_id);
        } catch(...) {
            const auto resource_it = m_chunks_by_resource.find(resource_id);
            if(resource_it != m_chunks_by_resource.end()) {
                resource_it->second.erase(chunk_id);
                if(resource_it->second.empty()) {
                    m_chunks_by_resource.erase(resource_it);
                }
            }
            if(record_inserted) {
                m_records.erase(chunk_id);
            }
            if(old_snapshot.has_value()) {
                const auto old_resource_id = old_snapshot->record.revision.resource_id;
                m_records.emplace(chunk_id, *old_snapshot);
                m_chunks_by_resource[old_resource_id].insert(chunk_id);
            }
            throw;
        }
    }

    std::optional<LexicalDocumentStats> KeywordOverlapIndex::find_stats(
        const ChunkId& chunk_id
    ) const {
        const auto it = m_records.find(chunk_id);
        if(it == m_records.end()) {
            return std::nullopt;
        }

        return LexicalDocumentStats{
            it->second.record.chunk_id,
            it->second.record.revision,
            it->second.record.tokens.size(),
            it->second.unique_terms.size(),
            it->second.record.metadata
        };
    }

    std::vector<LexicalSearchResult> KeywordOverlapIndex::search(
        const LexicalSearchQuery& query
    ) const {
        if(query.limit == 0) {
            return {};
        }

        if(!is_valid(query)) {
            throw std::invalid_argument("invalid lexical search query");
        }

        std::set<std::string> unique_query_terms;
        for(const auto& term : query.terms) {
            unique_query_terms.insert(term);
        }

        std::vector<LexicalSearchResult> results;
        for(const auto& item : m_records) {
            const auto& record = item.second;
            if(!matches_metadata_filters(record.record.metadata, query.metadata_filters)) {
                continue;
            }

            const auto score = score_record(record, unique_query_terms);
            if(score > 0.0F) {
                LexicalSearchResult hit;
                hit.chunk_id = record.record.chunk_id;
                hit.score = score;
                hit.metadata = record.record.metadata;
                hit.section_id = record.record.section_id;
                hit.resource_id = record.record.revision.resource_id;
                hit.enrichment_level = record.record.enrichment_level;
                hit.result_tier = 0;
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

    bool KeywordOverlapIndex::erase(const ChunkId& chunk_id) {
        const auto it = m_records.find(chunk_id);
        if(it == m_records.end()) {
            return false;
        }

        const auto resource_id = it->second.record.revision.resource_id;
        m_records.erase(it);

        const auto resource_it = m_chunks_by_resource.find(resource_id);
        if(resource_it != m_chunks_by_resource.end()) {
            resource_it->second.erase(chunk_id);
            if(resource_it->second.empty()) {
                m_chunks_by_resource.erase(resource_it);
            }
        }

        return true;
    }

    std::size_t KeywordOverlapIndex::erase_resource(const ResourceId& resource_id) {
        const auto resource_it = m_chunks_by_resource.find(resource_id);
        if(resource_it == m_chunks_by_resource.end()) {
            return 0;
        }

        const auto chunk_ids = resource_it->second;
        std::size_t removed = 0;
        for(const auto& chunk_id : chunk_ids) {
            if(erase(chunk_id)) {
                ++removed;
            }
        }
        return removed;
    }

    void KeywordOverlapIndex::clear() {
        m_records.clear();
        m_chunks_by_resource.clear();
    }

    float KeywordOverlapIndex::score_record(
        const StoredRecord& record,
        const std::set<std::string>& unique_query_terms
    ) {
        std::size_t overlap = 0;
        for(const auto& term : unique_query_terms) {
            if(record.unique_terms.find(term) != record.unique_terms.end()) {
                ++overlap;
            }
        }

        return static_cast<float>(overlap);
    }

} // namespace agent_memory
