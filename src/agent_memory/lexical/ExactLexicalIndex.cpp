#include "ExactLexicalIndex.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace agent_memory {

    ExactLexicalIndex::ExactLexicalIndex()
        : ExactLexicalIndex(ExactLexicalIndexOptions{}) {}

    ExactLexicalIndex::ExactLexicalIndex(ExactLexicalIndexOptions options)
        : m_options(options) {
        if(!is_valid(m_options.bm25)) {
            throw std::invalid_argument("invalid BM25 options");
        }
    }

    std::size_t ExactLexicalIndex::size() const noexcept {
        return m_records.size();
    }

    void ExactLexicalIndex::upsert(LexicalDocumentRecord record) {
        // 1. Validate before any global state is touched.
        if(!is_valid(record)) {
            throw std::invalid_argument("invalid lexical document record");
        }

        // 2. Capture identifiers before any move.
        const auto chunk_id = record.chunk_id;
        const auto resource_id = record.revision.resource_id;

        // 3. Build the new StoredRecord entirely in local memory. No
        //    global state (m_records, m_dictionary_by_text,
        //    m_total_token_count, m_chunks_by_resource) is touched yet,
        //    so an exception here (e.g. std::bad_alloc while building
        //    positions_by_term) leaves the index in its prior state.
        StoredRecord new_record;
        new_record.record = std::move(record);
        for(const auto& token : new_record.record.tokens) {
            new_record.positions_by_term[token.text].push_back(
                static_cast<std::uint32_t>(token.position)
            );
        }

        // Basic exception-safety contract:
        //   - validate() runs before any global mutation; on
        //     std::invalid_argument no state changes.
        //   - The commit phase (m_records.emplace and friends) may
        //     throw std::bad_alloc; we attempt to roll back exactly
        //     the steps already applied via the trackers. A second
        //     throw inside the rollback itself (e.g. re-emplace of
        //     the old snapshot) would leave the index in a degraded
        //     state; this is a known limitation and we do not promise
        //     strong guarantee here.

        // 4. Capture the existing record (if any) before any erase. The
        //    snapshot is a complete copy of StoredRecord; if the commit
        //    below throws, this snapshot lets us restore the pre-upsert
        //    state exactly.
        std::optional<StoredRecord> old_snapshot;
        {
            const auto it = m_records.find(chunk_id);
            if(it != m_records.end()) {
                old_snapshot = it->second;
            }
        }
        const std::size_t total_before = m_total_token_count;

        // 5. If the chunk was previously stored, remove the old record.
        //    std::map::erase is noexcept, so this step cannot leave us
        //    in a half-erased state.
        if(old_snapshot.has_value()) {
            static_cast<void>(erase(chunk_id));
        }

        // 6. Commit the new record. We track every mutation via local
        //    flags so we can undo exactly what was done if any step
        //    throws.
        std::vector<std::string> incremented_terms;
        bool m_records_inserted = false;
        bool resource_added = false;
        bool total_added = false;

        try {
            // 6a. df increments. A throw here means some df have been
            //     bumped; the rollback below reverses the ones we
            //     recorded in incremented_terms.
            for(const auto& term_item : new_record.positions_by_term) {
                const auto& term = term_item.first;
                auto& entry = get_or_create_token_entry(term);
                ++entry.document_frequency;
                incremented_terms.push_back(term);
            }

            // 6b. m_total_token_count -- straightforward counter update.
            m_total_token_count += new_record.record.tokens.size();
            total_added = true;

            // 6c. resource membership.
            m_chunks_by_resource[resource_id].insert(chunk_id);
            resource_added = true;

            // 6d. m_records emplace -- if this throws, roll back all
            //     the above via the catch block.
            m_records.emplace(chunk_id, new_record);
            m_records_inserted = true;
        } catch(...) {
            // Precise rollback: reverse the commit in opposite order,
            // undoing only what was actually applied.

            if(m_records_inserted) {
                m_records.erase(chunk_id);
            }
            if(resource_added) {
                const auto rit = m_chunks_by_resource.find(resource_id);
                if(rit != m_chunks_by_resource.end()) {
                    rit->second.erase(chunk_id);
                    if(rit->second.empty()) {
                        m_chunks_by_resource.erase(rit);
                    }
                }
            }
            if(total_added) {
                m_total_token_count -= new_record.record.tokens.size();
            }
            for(const auto& term : incremented_terms) {
                const auto dit = m_dictionary_by_text.find(term);
                if(dit != m_dictionary_by_text.end()) {
                    if(dit->second.document_frequency <= 1) {
                        m_dictionary_by_text.erase(dit);
                    } else {
                        --dit->second.document_frequency;
                    }
                }
            }

            // Restore the old record from snapshot if we had one. This
            // re-inserts the entry into m_records, re-increments df for
            // each old unique term (get_or_create_token_entry will
            // recreate any dictionary entry that was erased during
            // step 5 because df reached zero), re-adds the resource
            // membership, and restores m_total_token_count to its
            // pre-upsert value.
            if(old_snapshot.has_value()) {
                m_records.emplace(chunk_id, *old_snapshot);
                for(const auto& term_item : old_snapshot->positions_by_term) {
                    auto& entry = get_or_create_token_entry(term_item.first);
                    ++entry.document_frequency;
                }
                const auto& old_rid = old_snapshot->record.revision.resource_id;
                m_chunks_by_resource[old_rid].insert(chunk_id);
                m_total_token_count = total_before;
            }

            throw;
        }
    }

    std::optional<LexicalDocumentStats> ExactLexicalIndex::find_stats(
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
            it->second.positions_by_term.size(),
            it->second.record.metadata
        };
    }

    std::vector<LexicalSearchResult> ExactLexicalIndex::search(
        const LexicalSearchQuery& query
    ) const {
        if(query.limit == 0) {
            return {};
        }

        if(!is_valid(query)) {
            throw std::invalid_argument("invalid lexical search query");
        }

        std::vector<LexicalSearchResult> results;
        for(const auto& item : m_records) {
            const auto& record = item.second;
            if(!matches_metadata_filters(record.record.metadata, query.metadata_filters)) {
                continue;
            }

            const auto score = score_record(record, query);
            if(score > 0.0F) {
                LexicalSearchResult hit;
                hit.chunk_id = record.record.chunk_id;
                hit.score = score;
                hit.metadata = record.record.metadata;
                hit.section_id = record.record.section_id;
                hit.resource_id = record.record.revision.resource_id;
                hit.enrichment_level = record.record.enrichment_level;
                hit.result_tier = 0;  // chunk-level result
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

    bool ExactLexicalIndex::erase(const ChunkId& chunk_id) {
        const auto it = m_records.find(chunk_id);
        if(it == m_records.end()) {
            return false;
        }

        const auto resource_id = it->second.record.revision.resource_id;
        remove_record_stats(it->second);
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

    std::size_t ExactLexicalIndex::erase_resource(const ResourceId& resource_id) {
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

    void ExactLexicalIndex::clear() {
        m_records.clear();
        m_chunks_by_resource.clear();
        m_dictionary_by_text.clear();
        m_next_token_id = 1;
        m_total_token_count = 0;
    }

    TokenDictionaryEntry& ExactLexicalIndex::get_or_create_token_entry(
        const std::string& term
    ) {
        const auto it = m_dictionary_by_text.find(term);
        if(it != m_dictionary_by_text.end()) {
            return it->second;
        }

        auto inserted = m_dictionary_by_text.emplace(
            term,
            TokenDictionaryEntry{
                TokenId{m_next_token_id++},
                term,
                0
            }
        );
        return inserted.first->second;
    }

    const TokenDictionaryEntry* ExactLexicalIndex::find_token_entry(
        const std::string& term
    ) const noexcept {
        const auto it = m_dictionary_by_text.find(term);
        if(it == m_dictionary_by_text.end()) {
            return nullptr;
        }
        return &it->second;
    }

    float ExactLexicalIndex::score_record(
        const StoredRecord& record,
        const LexicalSearchQuery& query
    ) const {
        const Bm25Options& bm25 = query.bm25.has_value()
            ? *query.bm25
            : m_options.bm25;
        float score = 0.0F;
        for(const auto& term : query.terms) {
            score += score_term(record, term, bm25);
        }
        return score;
    }

    float ExactLexicalIndex::score_term(
        const StoredRecord& record,
        const std::string& term,
        const Bm25Options& options
    ) const {
        const auto positions_it = record.positions_by_term.find(term);
        if(positions_it == record.positions_by_term.end()) {
            return 0.0F;
        }

        const auto* entry = find_token_entry(term);
        if(entry == nullptr || entry->document_frequency == 0 || m_records.empty()) {
            return 0.0F;
        }

        const auto document_count = static_cast<float>(m_records.size());
        const auto document_frequency = static_cast<float>(entry->document_frequency);
        const auto idf = std::log(
            1.0F + ((document_count - document_frequency + 0.5F) /
                (document_frequency + 0.5F))
        );

        const auto term_frequency = static_cast<float>(positions_it->second.size());
        const auto document_length = static_cast<float>(record.record.tokens.size());
        const auto average_length = average_document_length();
        const auto length_ratio = average_length > 0.0F
            ? document_length / average_length
            : 1.0F;
        const auto denominator = term_frequency +
            options.k1 * (1.0F - options.b + options.b * length_ratio);

        // Denominator is mathematically >= tf >= 1 given k1 > 0,
        // b in [0, 1] and length_ratio >= 0 (validated by is_valid(Bm25Options)
        // and the avgdl > 0 fallback). The guard is dead; assert it instead.
        assert(denominator > 0.0F);

        return idf * ((term_frequency * (options.k1 + 1.0F)) / denominator);
    }

    float ExactLexicalIndex::average_document_length() const noexcept {
        if(m_records.empty()) {
            return 0.0F;
        }

        return static_cast<float>(m_total_token_count) /
            static_cast<float>(m_records.size());
    }

    void ExactLexicalIndex::remove_record_stats(const StoredRecord& record) {
        if(m_total_token_count >= record.record.tokens.size()) {
            m_total_token_count -= record.record.tokens.size();
        } else {
            m_total_token_count = 0;
        }

        for(const auto& term_item : record.positions_by_term) {
            const auto entry_it = m_dictionary_by_text.find(term_item.first);
            if(entry_it != m_dictionary_by_text.end() && entry_it->second.document_frequency > 0) {
                --entry_it->second.document_frequency;
                // Erase the entry once df reaches 0 to avoid stale entries
                // accumulating in the dictionary over a long-lived index.
                if(entry_it->second.document_frequency == 0) {
                    m_dictionary_by_text.erase(entry_it);
                }
            }
        }
    }

} // namespace agent_memory
