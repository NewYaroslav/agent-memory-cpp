#include <agent_memory/eval/IRetrieverAdapter.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

    int fail(const std::string& message) {
        std::cerr << message << '\n';
        return 1;
    }

    class RecordingEngine final : public agent_memory::IRetrievalEngine {
    public:
        [[nodiscard]] agent_memory::RetrievalResponse retrieve(
            const agent_memory::RetrievalRequest& request
        ) const override {
            requests.push_back(request);
            agent_memory::RetrievalResponse response;
            agent_memory::RetrievalResponseItem item;
            if(request.query == "chunk") {
                item.lexical.chunk_id = agent_memory::ChunkId{"chunk:1"};
                item.lexical.score = 0.75F;
                response.items.push_back(std::move(item));
            } else if(request.query == "object") {
                item.object.id = agent_memory::MemoryObjectId{"object:2"};
                item.lexical.score = 0.5F;
                response.items.push_back(std::move(item));
            } else if(request.query == "invalid") {
                response.items.push_back(std::move(item));
            }
            return response;
        }

        mutable std::vector<agent_memory::RetrievalRequest> requests;
    };

    [[nodiscard]] agent_memory::RetrievalEvalDataset make_dataset() {
        agent_memory::RetrievalEvalDataset dataset;
        dataset.name = "adapter_fixture";

        agent_memory::EvalQuery first;
        first.id = "query:chunk";
        first.text = "chunk";
        first.limit = 3;
        dataset.queries.push_back(std::move(first));

        agent_memory::EvalQuery ignored;
        ignored.id = "query:ignored";
        ignored.text = "ignored";
        ignored.answer_mode = agent_memory::EvalQueryAnswerMode::Ignore;
        dataset.queries.push_back(std::move(ignored));

        agent_memory::EvalQuery second;
        second.id = "query:object";
        second.text = "object";
        second.limit = 7;
        dataset.queries.push_back(std::move(second));
        return dataset;
    }

} // namespace

int main() {
    RecordingEngine engine;
    const auto run = agent_memory::run_retrieval_engine(
        engine,
        make_dataset(),
        "recording_engine"
    );

    if(run.name != "recording_engine") {
        return fail("adapter must preserve the baseline name");
    }
    if(engine.requests.size() != 2 || run.queries.size() != 2) {
        return fail("adapter must skip Ignore queries before calling the engine");
    }
    if(engine.requests[0].query != "chunk" || engine.requests[0].limit != 3
        || engine.requests[1].query != "object" || engine.requests[1].limit != 7) {
        return fail("adapter must preserve query text and limit");
    }
    if(run.queries[0].query_id != "query:chunk"
        || run.queries[0].hits.size() != 1
        || run.queries[0].hits[0].item_id != "chunk:1") {
        return fail("adapter must map lexical chunk ids into run hits");
    }
    if(run.queries[1].query_id != "query:object"
        || run.queries[1].hits.size() != 1
        || run.queries[1].hits[0].item_id != "object:2") {
        return fail("adapter must fall back to MemoryObject ids");
    }
    if(std::fabs(run.queries[0].hits[0].score - 0.75F) > 1e-6F
        || run.queries[0].hits[0].retriever_name != "recording_engine") {
        return fail("adapter must preserve score and retriever name");
    }
    for(const auto& query_run : run.queries) {
        if(!query_run.latency_ms || *query_run.latency_ms < 0.0) {
            return fail("adapter must record non-negative latency for every engine call");
        }
    }

    agent_memory::RetrievalEvalDataset invalid_dataset;
    agent_memory::EvalQuery invalid_query;
    invalid_query.id = "query:invalid";
    invalid_query.text = "invalid";
    invalid_dataset.queries.push_back(std::move(invalid_query));
    bool threw = false;
    try {
        (void)agent_memory::run_retrieval_engine(
            engine,
            invalid_dataset,
            "recording_engine"
        );
    } catch(const std::invalid_argument&) {
        threw = true;
    }
    if(!threw) {
        return fail("adapter must reject engine results without a stable id");
    }

    return 0;
}
