#include "BowEmbedder.hpp"

#include "BoWTokenizer.hpp"

#include <agent_memory/lexical/ITokenDictionary.hpp>
#include <agent_memory/lexical/ITokenizer.hpp>
#include <agent_memory/lexical/TokenDictionary.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agent_memory {

    namespace {

        // Minimal in-memory ITokenDictionary used as the default wiring for
        // BowEmbedder. It assigns TokenIds monotonically starting at 1 and
        // never reuses an id within the dictionary's lifetime, satisfying
        // the ITokenDictionary id-allocation contract. Document frequencies
        // are tracked but not consumed by BowEmbedder today; they are kept
        // so the implementation remains a faithful example of the contract.
        class DefaultBowTokenDictionary final : public ITokenDictionary {
        public:
            [[nodiscard]] std::size_t size() const noexcept override {
                return m_entries_by_id.size();
            }

            [[nodiscard]] TokenId get_or_create(std::string_view text) override {
                if(text.empty()) {
                    throw std::invalid_argument(
                        "DefaultBowTokenDictionary: token text must not be empty"
                    );
                }
                const auto by_text_it = m_ids_by_text.find(text);
                if(by_text_it != m_ids_by_text.end()) {
                    return by_text_it->second;
                }
                const auto id = TokenId{m_next_id++};
                TokenDictionaryEntry entry{id, std::string{text}, 0};
                m_ids_by_text.emplace(entry.text, entry.id);
                m_entries_by_id.emplace(entry.id, std::move(entry));
                return id;
            }

            [[nodiscard]] std::optional<TokenDictionaryEntry> find_by_text(
                std::string_view text
            ) const override {
                const auto by_text_it = m_ids_by_text.find(text);
                if(by_text_it == m_ids_by_text.end()) {
                    return std::nullopt;
                }
                return find_by_id(by_text_it->second);
            }

            [[nodiscard]] std::optional<TokenDictionaryEntry> find_by_id(
                const TokenId& id
            ) const override {
                const auto it = m_entries_by_id.find(id);
                if(it == m_entries_by_id.end()) {
                    return std::nullopt;
                }
                return it->second;
            }

            void assign_document_frequency(
                const TokenId& id,
                const std::size_t document_frequency
            ) override {
                const auto it = m_entries_by_id.find(id);
                if(it == m_entries_by_id.end()) {
                    throw std::invalid_argument(
                        "DefaultBowTokenDictionary: token id must exist"
                    );
                }
                it->second.document_frequency = document_frequency;
            }

            void increment_document_frequency(
                const TokenId& id,
                const std::size_t delta
            ) override {
                const auto it = m_entries_by_id.find(id);
                if(it == m_entries_by_id.end()) {
                    throw std::invalid_argument(
                        "DefaultBowTokenDictionary: token id must exist"
                    );
                }
                it->second.document_frequency += delta;
            }

            [[nodiscard]] bool erase(const TokenId& id) override {
                const auto it = m_entries_by_id.find(id);
                if(it == m_entries_by_id.end()) {
                    return false;
                }
                m_ids_by_text.erase(it->second.text);
                m_entries_by_id.erase(it);
                return true;
            }

            void clear() override {
                m_entries_by_id.clear();
                m_ids_by_text.clear();
                m_next_id = 1;
            }

        private:
            std::uint64_t m_next_id = 1;
            std::map<TokenId, TokenDictionaryEntry, std::less<>> m_entries_by_id;
            std::map<std::string, TokenId, std::less<>> m_ids_by_text;
        };

        // Pulls the normalized token text out of an ITokenizer::TokenizationResult
        // in a way that preserves the BoW contract: drop empty tokens and tokens
        // shorter than 2 characters. The BoWTokenizer already enforces this
        // contract; the additional checks here defend against alternative
        // ITokenizer implementations that produce shorter runs.
        [[nodiscard]] std::vector<std::string> extract_bow_tokens(
            const ITokenizer& tokenizer,
            std::string_view text
        ) {
            const auto result = tokenizer.tokenize(text);
            std::vector<std::string> tokens;
            tokens.reserve(result.tokens.size());
            for(const auto& token : result.tokens) {
                if(token.text.size() >= 2) {
                    tokens.push_back(token.text);
                }
            }
            return tokens;
        }

    } // namespace

    BowEmbedder::BowEmbedder()
        : m_tokenizer(nullptr),
          m_dictionary(nullptr),
          m_owned_tokenizer(std::make_unique<BoWTokenizer>()),
          m_owned_dictionary(std::make_unique<DefaultBowTokenDictionary>()),
          m_id_to_index(),
          m_built(false) {
        m_tokenizer = m_owned_tokenizer.get();
        m_dictionary = m_owned_dictionary.get();
    }

    BowEmbedder::BowEmbedder(ITokenizer& tokenizer, ITokenDictionary& dictionary)
        : m_tokenizer(&tokenizer),
          m_dictionary(&dictionary),
          m_owned_tokenizer(),
          m_owned_dictionary(),
          m_id_to_index(),
          m_built(false) {}

    BowEmbedder::~BowEmbedder() = default;

    void BowEmbedder::add_corpus_text(std::string_view text) {
        const auto tokens = extract_bow_tokens(*m_tokenizer, text);
        for(const auto& token : tokens) {
            const auto id = m_dictionary->get_or_create(token);
            const auto it = m_id_to_index.find(id);
            if(it == m_id_to_index.end()) {
                const auto index = static_cast<std::uint32_t>(m_id_to_index.size());
                m_id_to_index.emplace(id, index);
            }
        }
    }

    void BowEmbedder::build() {
        m_built = true;
    }

    std::vector<float> BowEmbedder::embed(std::string_view text) const {
        const auto tokens = extract_bow_tokens(*m_tokenizer, text);
        std::vector<float> values(m_id_to_index.size(), 0.0F);
        for(const auto& token : tokens) {
            const auto id_opt = m_dictionary->find_by_text(token);
            if(!id_opt) {
                // Out-of-vocabulary terms are silently dropped — matches the
                // documented contract: the embedder expects callers to
                // add_corpus_text + build before embedding so the dictionary
                // is sealed prior to any query.
                continue;
            }
            const auto it = m_id_to_index.find(id_opt->id);
            if(it == m_id_to_index.end()) {
                continue;
            }
            values[it->second] += 1.0F;
        }

        // L2-normalize so cosine similarity == dot product at search time.
        double sum_sq = 0.0;
        for(const float v : values) {
            sum_sq += static_cast<double>(v) * static_cast<double>(v);
        }
        if(sum_sq > 0.0) {
            const float inv_norm = static_cast<float>(1.0 / std::sqrt(sum_sq));
            for(float& v : values) {
                v *= inv_norm;
            }
        }
        return values;
    }

    std::size_t BowEmbedder::dictionary_size() const noexcept {
        return m_id_to_index.size();
    }

    bool BowEmbedder::is_built() const noexcept {
        return m_built;
    }

} // namespace agent_memory