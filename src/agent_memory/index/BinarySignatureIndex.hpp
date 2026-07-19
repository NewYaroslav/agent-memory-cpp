#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_INDEX_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_BINARY_SIGNATURE_INDEX_HPP_INCLUDED

/// \file BinarySignatureIndex.hpp
/// \brief Value types for binary-signature candidate indexes.

#include "BinarySignature.hpp"
#include "BinarySignatureInfo.hpp"

#include <agent_memory/domain/Identifiers.hpp>
#include <agent_memory/domain/Metadata.hpp>
#include <agent_memory/domain/MetadataFilter.hpp>

#include <cstddef>
#include <vector>

namespace agent_memory {

    /// \brief Binary signature payload stored for one chunk.
    struct BinarySignatureRecord final {
        ChunkId chunk_id;
        BinarySignature signature;
        BinarySignatureInfo signature_info;
        Metadata metadata;
    };

    /// \brief Nearest-neighbour query over packed binary signatures.
    struct BinarySignatureSearchQuery final {
        BinarySignature signature;
        BinarySignatureInfo signature_info;
        std::size_t limit = 10;
        std::vector<MetadataFilter> metadata_filters;
    };

    /// \brief Scored binary-signature search hit.
    struct BinarySignatureSearchResult final {
        ChunkId chunk_id;
        /// \brief Exact Hamming distance from the query signature. Lower is better.
        std::size_t hamming_distance = 0;
        Metadata metadata;
    };

} // namespace agent_memory

#endif
