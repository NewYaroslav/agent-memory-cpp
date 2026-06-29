# Agent Memory C++

Embedded C++17 toolkit for building memory and retrieval systems for AI agents.

Agent Memory C++ provides modular components for persistent agent memory, retrieval-augmented generation, semantic search, hybrid BM25/vector retrieval, Markdown knowledge bases, and knowledge graphs.

The library is designed for local and embedded use. It does not require a separate vector database server.

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
├── Markdown documents
├── Chat messages
├── Code and symbols
├── Structured records
└── Agent events
        │
        ▼
Ingestion
├── Parsers
├── Chunkers
├── Metadata extraction
├── Entity extraction
└── Change detection
        │
        ▼
Indexes
├── BM25
├── Dense vectors
├── Sparse vectors
├── Knowledge graph
└── Temporal indexes
        │
        ▼
Memory strategies
├── Recent memory
├── Semantic memory
├── Summary memory
├── Episodic memory
├── Entity memory
└── Composite memory
        │
        ▼
Retrieval and context building
        │
        ▼
LLM or AI agent
```

## Storage

The initial storage backend is based on:

* [libmdbx](https://github.com/erthink/libmdbx)
* [mdbx-containers](https://github.com/NewYaroslav/mdbx-containers)

Storage interfaces are separated from memory and retrieval algorithms so that additional backends can be added later.

## Embeddings

Embedding generation is exposed through a backend-independent interface.

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

    virtual std::size_t dimension() const noexcept = 0;

    virtual Embedding encode(
        std::string_view text,
        EmbeddingPurpose purpose
    ) = 0;
};
```

`EmbeddingPurpose` distinguishes document, query, classification, and other model-specific input modes.

## Example

```cpp
#include <agent_memory/memory/semantic_memory.hpp>
#include <agent_memory/embedding/onnx_embedder.hpp>
#include <agent_memory/storage/mdbx_storage.hpp>

int main()
{
    agent_memory::MdbxStorage storage{"./memory"};

    agent_memory::OnnxEmbedder embedder{
        "./models/multilingual-e5-small"
    };

    agent_memory::SemanticMemory memory{
        storage,
        embedder
    };

    memory.remember({
        .content = "The user is developing a robotic arm.",
        .scope = "user:42"
    });

    const auto results = memory.recall({
        .query = "What robotics project is the user working on?",
        .scope = "user:42",
        .limit = 5
    });

    for (const auto& result : results) {
        std::cout
            << result.score
            << " "
            << result.content
            << '\n';
    }
}
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
ctest --test-dir build --output-on-failure
```

## Requirements

* C++17 compiler
* CMake 3.20 or newer
* Windows, Linux, or macOS

## License

MIT
