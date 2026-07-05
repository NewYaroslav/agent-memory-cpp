#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_I_RETRIEVER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_I_RETRIEVER_HPP_INCLUDED

/// \file IRetriever.hpp
/// \brief Backend-independent retrieval contract.

#include "Retrieval.hpp"

namespace agent_memory {

    /// \brief Dependency-free contract implemented by retrieval pipelines.
    class IRetriever {
    public:
        virtual ~IRetriever();

        /// \brief Retrieves chunks for a text, embedding, or mixed query.
        /// \param query Query signal, result limit, and optional metadata filters.
        /// \return Ordered retrieved chunks. Higher scores must rank before lower scores.
        /// \note `query.limit == 0` requests an empty result.
        [[nodiscard]] virtual RetrievalResult retrieve(const RetrievalQuery& query) const = 0;
    };

} // namespace agent_memory

#endif
