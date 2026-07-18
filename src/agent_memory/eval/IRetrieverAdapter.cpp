#include "IRetrieverAdapter.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace agent_memory {

    namespace {

        [[nodiscard]] RetrievalQueryRun begin_query_run(const EvalQuery& query) {
            RetrievalQueryRun query_run;
            query_run.query_id = query.id;
            return query_run;
        }

        [[nodiscard]] double elapsed_milliseconds(
            std::chrono::steady_clock::time_point start,
            std::chrono::steady_clock::time_point end
        ) noexcept {
            return std::chrono::duration<double, std::milli>(end - start).count();
        }

    } // namespace

    RetrievalRun run_retriever(
        const IRetriever& retriever,
        const RetrievalEvalDataset& dataset,
        std::string_view baseline_name
    ) {
        const std::string baseline{baseline_name};
        RetrievalRun run;
        run.name = baseline;
        run.queries.reserve(dataset.queries.size());

        for(const auto& query : dataset.queries) {
            if(query.answer_mode == EvalQueryAnswerMode::Ignore) {
                continue;
            }

            RetrievalQuery retrieval_query;
            retrieval_query.text = query.text;
            retrieval_query.limit = query.limit;
            retrieval_query.metadata_filters = query.metadata_filters;

            const auto start = std::chrono::steady_clock::now();
            const RetrievalResult result = retriever.retrieve(retrieval_query);
            const auto end = std::chrono::steady_clock::now();

            RetrievalQueryRun query_run = begin_query_run(query);
            query_run.latency_ms = elapsed_milliseconds(start, end);
            query_run.hits.reserve(result.chunks.size());
            for(const auto& chunk : result.chunks) {
                RetrievalRunHit hit;
                hit.item_id = chunk.chunk.id.value();
                hit.score = chunk.score;
                hit.retriever_name = baseline;
                query_run.hits.push_back(std::move(hit));
            }
            run.queries.push_back(std::move(query_run));
        }
        return run;
    }

    RetrievalRun run_retrieval_engine(
        const IRetrievalEngine& engine,
        const RetrievalEvalDataset& dataset,
        std::string_view baseline_name
    ) {
        const std::string baseline{baseline_name};
        RetrievalRun run;
        run.name = baseline;
        run.queries.reserve(dataset.queries.size());

        for(const auto& query : dataset.queries) {
            if(query.answer_mode == EvalQueryAnswerMode::Ignore) {
                continue;
            }

            RetrievalRequest request;
            request.query = query.text;
            request.limit = query.limit;
            request.metadata_filters = query.metadata_filters;

            const auto start = std::chrono::steady_clock::now();
            const RetrievalResponse response = engine.retrieve(request);
            const auto end = std::chrono::steady_clock::now();

            RetrievalQueryRun query_run = begin_query_run(query);
            query_run.latency_ms = elapsed_milliseconds(start, end);
            query_run.hits.reserve(response.items.size());
            for(const auto& item : response.items) {
                RetrievalRunHit hit;
                if(!item.lexical.chunk_id.empty()) {
                    hit.item_id = item.lexical.chunk_id.value();
                } else if(!item.object.id.empty()) {
                    hit.item_id = item.object.id.value();
                } else {
                    throw std::invalid_argument(
                        "retrieval engine response item id must not be empty"
                    );
                }
                hit.score = item.lexical.score;
                hit.retriever_name = baseline;
                query_run.hits.push_back(std::move(hit));
            }
            run.queries.push_back(std::move(query_run));
        }
        return run;
    }

} // namespace agent_memory
