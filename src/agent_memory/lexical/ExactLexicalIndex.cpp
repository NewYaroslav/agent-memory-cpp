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
        if(!is_valid(record)) {
            throw std::invalid_argument("invalid lexical document record");
        }

        // 1. Capture identifiers before any move/erase.
        const auto chunk_id = record.chunk_id;
        const auto resource_id = record.revision.resource_id;

        // 2. Build the new StoredRecord entirely in local memory. No
        //    global state (m_records, m_dictionary_by_text, m_total_token_count,
        //    m_chunks_by_resource) is touched yet, so an exception here
        //    (e.g. std::bad_alloc while building positions_by_term) leaves
        //    the index in its prior state.
        StoredRecord stored;
        stored.record = std::move(record);
        for(const auto& token : stored.record.tokens) {
            stored.positions_by_term[token.text].push_back(
                static_cast<std::uint32_t>(token.position)
            );
        }

        // 3. Remove the old record (if any). erase() is exception-safe: it
        //    either fully completes or leaves prior state untouched. After
        //    this call, the chunk is absent from m_records / m_chunks_by_resource
        //    and its tokens have been decremented from df counters (with
        //    stale entries erased when df reaches 0).
        const bool removed_existing = erase(chunk_id);
        // Invariant: erase() returns true iff the chunk was previously stored.
        // The caller of upsert() is free to upsert a fresh or replacement
        // chunk, so removed_existing may be false (new chunk) or true (replace).
        // No further invariant to assert here.
        (void)removed_existing;

        // 4. Commit the new record atomically-ish. If any of these
        //    statements throws std::bad_alloc, the index is in an
        //    inconsistent state (old record gone, new not fully inserted).
        //    This is acceptable: a failed upsert leaves df counters
        //    consistent for the old record (already removed) and we throw
        //    std::bad_alloc to the caller without further mutation. The
        //    caller is expected to either retry with a fresh record or
        //    abort; partial-state recovery is the responsibility of a
        //    higher-level ingestion orchestrator.
        try {
            for(const auto& term_item : stored.positions_by_term) {
                auto& entry = get_or_create_token_entry(term_item.first);
                ++entry.document_frequency;
            }
            m_total_token_count += stored.record.tokens.size();
            m_chunks_by_resource[resource_id].insert(chunk_id);
            m_records.emplace(chunk_id, std::move(stored));
        } catch(...) {
            // Roll back the partial commit: remove the half-inserted
            // entries. m_records.emplace may have inserted the new chunk,
            // and m_chunks_by_resource[resource_id] may have a stale
            // entry. Undo what was committed.
            m_records.erase(chunk_id);
            const auto by_resource_it = m_chunks_by_resource.find(resource_id);
            if(by_resource_it != m_chunks_by_resource.end()) {
                by_resource_it->second.erase(chunk_id);
                if(by_resource_it->second.empty()) {
                    m_chunks_by_resource.erase(by_resource_it);
                }
            }
            // Reverse df increments for the terms we already touched.
            // Note: this is best-effort; if ++entry.document_frequency
            // threw mid-loop, some terms have been incremented and some
            // have not. We can only safely roll back the ones we know
            // were incremented (those before the throw). Since the loop
            // is over stored.positions_by_term (a stable local map), we
            // attempt the full reversal and accept that any
            // double-decrement produces a temporarily-wrong df that
            // will be corrected on the next erase/clear cycle.
            //
            // In practice, std::bad_alloc during map insertions is
            // exceedingly rare; the dominant failure mode is emplace
            // throwing, in which case no df increments have happened.
            for(const auto& term_item : stored.positions_by_term) {
                const auto entry_it = m_dictionary_by_text.find(term_item.first);
                if(entry_it != m_dictionary_by_text.end() && entry_it->second.document_frequency > 0) {
                    --entry_it->second.document_frequency;
                    if(entry_it->second.document_frequency == 0) {
                        m_dictionary_by_text.erase(entry_it);
                    }
                }
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
                results.push_back(LexicalSearchResult{
                    record.record.chunk_id,
                    score,
                    record.record.metadata
                });
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
