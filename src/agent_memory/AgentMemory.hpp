#pragma once
#ifndef AGENT_MEMORY_HEADER_AGENT_MEMORY_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_AGENT_MEMORY_HPP_INCLUDED

/// \file AgentMemory.hpp
/// \brief Public aggregate include for Agent Memory C++.

#include "core/LibraryInfo.hpp"
#include "domain/Document.hpp"
#include "domain/Identifiers.hpp"
#include "domain/Metadata.hpp"
#include "domain/MetadataFilter.hpp"
#include "domain/Resource.hpp"
#include "domain/SourceKind.hpp"
#include "embedding/Embedding.hpp"
#include "embedding/IEmbedder.hpp"
#include "ingestion/ResourceIndexer.hpp"
#include "index/ExactVectorIndex.hpp"
#include "index/IVectorIndex.hpp"
#include "index/VectorIndex.hpp"
#include "lexical/ExactLexicalIndex.hpp"
#include "lexical/ILexicalIndex.hpp"
#include "lexical/ITokenDictionary.hpp"
#include "lexical/ITokenizer.hpp"
#include "lexical/Lexical.hpp"
#include "lexical/LexicalHash.hpp"
#include "lexical/StandardTokenizer.hpp"
#include "lexical/TokenDictionary.hpp"
#include "lexical/Tokenizer.hpp"
#include "retrieval/IRetriever.hpp"
#include "retrieval/Retrieval.hpp"
#include "storage/IDocumentStorage.hpp"
#include "storage/IResourceManifestStorage.hpp"

#endif
