#pragma once
#ifndef AGENT_MEMORY_HEADER_EMBEDDING_I_EMBEDDER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EMBEDDING_I_EMBEDDER_HPP_INCLUDED

/// \file IEmbedder.hpp
/// \brief Backend-independent embedding provider contract.

#include "Embedding.hpp"

#include <vector>

namespace agent_memory {

    /// \brief Dependency-free contract implemented by embedding backends.
    class IEmbedder {
    public:
        virtual ~IEmbedder();

        /// \brief Returns stable metadata for embeddings produced by this backend.
        [[nodiscard]] virtual const EmbeddingModelInfo& info() const noexcept = 0;

        /// \brief Embeds one text request.
        /// \param request Text and purpose to embed.
        /// \return Dense embedding vector produced by the backend.
        [[nodiscard]] virtual Embedding embed(const EmbeddingRequest& request) = 0;

        /// \brief Embeds requests in order.
        /// \param requests Requests to embed in order.
        /// \return Embeddings in the same order as `requests`.
        /// \note Backends may override this method when they support native batching.
        [[nodiscard]] virtual std::vector<Embedding> embed_batch(
            const std::vector<EmbeddingRequest>& requests
        );
    };

} // namespace agent_memory

#endif
