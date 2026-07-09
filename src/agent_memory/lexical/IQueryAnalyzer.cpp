#include "IQueryAnalyzer.hpp"

#include <cctype>
#include <cstddef>
#include <string_view>

namespace agent_memory {

    IQueryAnalyzer::~IQueryAnalyzer() = default;

    QueryAnalysis PassthroughQueryAnalyzer::analyze(const std::string& query) const {
        const std::string_view view(query);
        QueryAnalysis out;
        out.original = query;
        // Minimal whitespace split so the default HybridRetrievalEngine
        // pipeline (which uses keywords for BM25) can find multi-term
        // matches in ExactLexicalIndex. Real analyzers can replace this
        // with LLM-based rewrite or richer tokenization.
        std::size_t pos = 0;
        while(pos < view.size()) {
            while(pos < view.size()
                && std::isspace(static_cast<unsigned char>(view[pos]))) {
                ++pos;
            }
            const std::size_t start = pos;
            while(pos < view.size()
                && !std::isspace(static_cast<unsigned char>(view[pos]))) {
                ++pos;
            }
            if(start < pos) {
                out.keywords.emplace_back(view.substr(start, pos - start));
            }
        }
        out.type = QueryType::SemanticLookup;
        out.mode = RetrievalMode::HybridSearch;
        out.plan = AnalysisPlan::SinglePass;
        return out;
    }

} // namespace agent_memory