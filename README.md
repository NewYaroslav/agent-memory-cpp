# Agent Memory C++

Embedded C++17 toolkit for building memory and retrieval systems for AI agents.

Agent Memory C++ provides modular components for persistent agent memory,
retrieval-augmented generation, semantic search, hybrid BM25/vector retrieval,
Markdown knowledge bases, and knowledge graphs.

The library is designed for local and embedded use. It does not require a
separate vector database server.

## Current status

The repository is in the early foundation stage. The first PR established a
static C++17 library target, CMake options, a smoke test, and a small example.
Follow-up PRs are adding stable domain primitives before storage, retrieval, and
embedding integrations.

## Goals

* Native C++17 API
* Embedded persistent storage
* Modular memory strategies
* Pluggable embedding backends
* Exact and approximate vector search
* BM25 and hybrid retrieval
* Markdown knowledge-base ingestion
* Knowledge graphs and named entities
* Deterministic, inspectable storage
* No mandatory Python runtime
* Optional MCP and HTTP adapters

## Planned memory types

* Conversation history
* Sliding-window memory
* Summary memory
* Semantic memory
* Episodic memory
* Entity memory
* User and character profiles
* Procedural memory
* Markdown knowledge bases
* Graph-based memory
* Temporal memory
* Multi-scope agent memory

## Planned retrieval methods

* Exact vector search
* HNSW approximate nearest-neighbour search
* BM25 full-text search
* Hybrid BM25 + vector search
* Reciprocal Rank Fusion
* Metadata filtering
* Graph traversal
* Temporal filtering
* Reranking
* Query expansion
* Multi-stage retrieval

## Architecture

```text
Sources
|-- Markdown documents
|-- Chat messages
|-- Code and symbols
|-- Structured records
`-- Agent events
        |
        v
Ingestion
|-- Parsers
|-- Chunkers
|-- Metadata extraction
|-- Entity extraction
`-- Change detection
        |
        v
Indexes
|-- BM25
|-- Dense vectors
|-- Sparse vectors
|-- Knowledge graph
`-- Temporal indexes
        |
        v
Memory strategies
|-- Recent memory
|-- Semantic memory
|-- Summary memory
|-- Episodic memory
|-- Entity memory
`-- Composite memory
        |
        v
Retrieval and context building
        |
        v
LLM or AI agent
```

## Source layout

Headers and implementation files live side by side under `src/`:

```text
src/agent_memory/
|-- AgentMemory.hpp
|-- domain/
|   |-- Document.hpp
|   |-- Identifiers.hpp
|   |-- Metadata.hpp
|   `-- SourceKind.hpp
|-- storage/
|   `-- IDocumentStorage.hpp
`-- core/
    |-- LibraryInfo.hpp
    `-- LibraryInfo.cpp
```

Consumers include public headers through the `agent_memory` include prefix:

```cpp
#include <agent_memory/AgentMemory.hpp>
```

## Storage

The storage layer starts with dependency-free document/chunk contracts. The
initial concrete backend is planned around:

* [libmdbx](https://github.com/erthink/libmdbx)
* [mdbx-containers](https://github.com/NewYaroslav/mdbx-containers)

Storage interfaces are separated from memory and retrieval algorithms so that
additional backends can be added later.

## Embeddings

Embedding generation will be exposed through a backend-independent interface.

Planned backends:

* ONNX Runtime
* local E5 models
* llama.cpp
* OpenAI-compatible embedding APIs
* custom user-provided implementations

Example interface:

```cpp
class IEmbedder {
public:
    virtual ~IEmbedder() = default;

    virtual const EmbeddingModelInfo& info() const noexcept = 0;

    virtual std::vector<float> encode(
        std::string_view text,
        EmbeddingPurpose purpose
    ) = 0;
};
```

## Project status

The project is in the initial design and prototyping stage.

The first milestone will provide:

* MDBX-backed document storage
* Markdown ingestion
* multilingual E5 embeddings through ONNX Runtime
* exact cosine-similarity search
* BM25 search
* Reciprocal Rank Fusion
* semantic memory
* recent conversation memory
* a minimal context builder
* unit tests and benchmarks

## Non-goals for the first milestone

* Distributed vector database
* Multi-node replication
* Hosted embedding service
* General-purpose agent orchestration
* LLM inference engine
* Autonomous-agent framework

## Building

```bash
git clone --recursive https://github.com/<owner>/agent-memory-cpp.git
cd agent-memory-cpp

cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DAGENT_MEMORY_BUILD_TESTS=ON \
    -DAGENT_MEMORY_BUILD_EXAMPLES=ON

cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

Available CMake options:

* `AGENT_MEMORY_BUILD_TESTS`
* `AGENT_MEMORY_BUILD_EXAMPLES`
* `AGENT_MEMORY_ENABLE_WARNINGS`

## Requirements

* C++17 compiler
* CMake 3.20 or newer
* Windows, Linux, or macOS

## License

MIT
