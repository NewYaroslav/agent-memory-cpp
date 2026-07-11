#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_STUB_DATASET_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_STUB_DATASET_HPP_INCLUDED

/// \file StubDataset.hpp
/// \brief Header-only fixture generator for retrieval evaluation plumbing tests.

#include <agent_memory/eval/Evaluation.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>

namespace agent_memory {

    /// \brief Tunables for the synthetic retrieval-evaluation fixture.
    struct StubDatasetOptions final {
        std::size_t corpus_size = 64;
        std::size_t query_count = 24;
        std::uint32_t seed = 0xC0FFEEu;
        std::string dataset_name = "stub_dataset";
    };

    /// \brief Builds a deterministic synthetic `RetrievalEvalDataset`.
    ///
    /// Most queries carry an `id:<doc>` prefix that `StubRetriever` answers
    /// exactly; the remainder exercise its deterministic random-rank path.
    inline RetrievalEvalDataset make_stub_dataset(
        const StubDatasetOptions& options = {}
    ) {
        if(options.corpus_size == 0 || options.query_count == 0) {
            throw std::invalid_argument(
                "StubDatasetOptions.corpus_size and query_count must be positive"
            );
        }

        RetrievalEvalDataset dataset;
        dataset.name = options.dataset_name;

        dataset.corpus.reserve(options.corpus_size);
        for(std::size_t index = 0; index < options.corpus_size; ++index) {
            EvalCorpusItem item;
            item.id = "doc:" + std::to_string(index);
            item.title = "title-" + std::to_string(index);
            item.text = "text payload #" + std::to_string(index);
            dataset.corpus.push_back(std::move(item));
        }

        std::mt19937 rng(options.seed);
        std::uniform_int_distribution<std::size_t> corpus_index_dist(
            0,
            options.corpus_size - 1
        );
        std::bernoulli_distribution exact_bias(0.75);

        dataset.queries.reserve(options.query_count);
        for(std::size_t q = 0; q < options.query_count; ++q) {
            EvalQuery query;
            query.id = "q:" + std::to_string(q);
            query.query_type = "StubLookup";
            query.limit = 10;
            query.text = exact_bias(rng)
                ? "id:doc:" + std::to_string(corpus_index_dist(rng))
                : "noise:" + std::to_string(q);
            dataset.queries.push_back(std::move(query));
        }

        std::uniform_int_distribution<std::int32_t> grade_dist(1, 3);
        dataset.judgments.reserve(options.query_count);
        for(const auto& query : dataset.queries) {
            constexpr std::string_view prefix = "id:";
            RelevanceJudgment judgment;
            judgment.query_id = query.id;
            if(query.text.rfind(prefix, 0) == 0) {
                judgment.item_id = query.text.substr(prefix.size());
            } else {
                judgment.item_id = "doc:" + std::to_string(corpus_index_dist(rng));
            }
            judgment.relevance_grade = grade_dist(rng);
            dataset.judgments.push_back(std::move(judgment));
        }

        return dataset;
    }

} // namespace agent_memory

#endif