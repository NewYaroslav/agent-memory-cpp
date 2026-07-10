#include "BowEmbedder.hpp"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace agent_memory {

    namespace {

        // Lowercase a single byte when it is an ASCII uppercase letter.
        // Other bytes (UTF-8 continuations, digits, punctuation) are kept as-is
        // because this is a control-point tokenizer, not a Unicode-aware one.
        char to_lower_ascii(char ch) noexcept {
            if(ch >= 'A' && ch <= 'Z') {
                return static_cast<char>(ch + ('a' - 'A'));
            }
            return ch;
        }

        // Returns true when the byte is an ASCII alphanumeric character.
        bool is_alnum_ascii(char ch) noexcept {
            return (ch >= 'a' && ch <= 'z')
                || (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9');
        }

        // Tokenizer: lowercase ASCII + split on non-alphanumeric + drop empties +
        // drop tokens shorter than 2 chars. Yields normalized tokens in order.
        std::vector<std::string> tokenize(std::string_view text) {
            std::vector<std::string> tokens;
            std::string current;
            current.reserve(8);
            for(const char raw : text) {
                const char ch = to_lower_ascii(raw);
                if(is_alnum_ascii(ch)) {
                    current.push_back(ch);
                } else if(!current.empty()) {
                    if(current.size() >= 2) {
                        tokens.push_back(std::move(current));
                    }
                    current.clear();
                }
            }
            if(!current.empty() && current.size() >= 2) {
                tokens.push_back(std::move(current));
            }
            return tokens;
        }

        // Allocates a dense index for an unseen term; returns the index.
        std::uint32_t intern(
            std::unordered_map<std::string, std::uint32_t>& dictionary,
            std::string term
        ) {
            const auto it = dictionary.find(term);
            if(it != dictionary.end()) {
                return it->second;
            }
            const auto index = static_cast<std::uint32_t>(dictionary.size());
            dictionary.emplace(std::move(term), index);
            return index;
        }

    } // namespace

    BowEmbedder::BowEmbedder() = default;

    void BowEmbedder::add_corpus_text(std::string_view text) {
        const auto tokens = tokenize(text);
        for(const auto& token : tokens) {
            (void)intern(m_dictionary, token);
        }
    }

    void BowEmbedder::build() {
        m_built = true;
    }

    std::vector<float> BowEmbedder::embed(std::string_view text) const {
        const auto tokens = tokenize(text);
        std::vector<float> values(m_dictionary.size(), 0.0F);
        for(const auto& token : tokens) {
            const auto it = m_dictionary.find(token);
            if(it == m_dictionary.end()) {
                // Out-of-vocabulary terms are silently dropped. The embedder
                // contract requires callers to add corpus text before
                // embedding so the dictionary is sealed prior to any query.
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
        return m_dictionary.size();
    }

    bool BowEmbedder::is_built() const noexcept {
        return m_built;
    }

} // namespace agent_memory