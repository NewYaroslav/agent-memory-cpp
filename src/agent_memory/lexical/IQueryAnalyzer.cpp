#include "IQueryAnalyzer.hpp"

namespace agent_memory {

    IQueryAnalyzer::~IQueryAnalyzer() = default;

    QueryAnalysis PassthroughQueryAnalyzer::analyze(const std::string& query) const {
        QueryAnalysis analysis;
        analysis.original = query;
        analysis.type = QueryType::SemanticLookup;
        analysis.mode = RetrievalMode::HybridSearch;
        analysis.plan = AnalysisPlan::SinglePass;
        return analysis;
    }

} // namespace agent_memory