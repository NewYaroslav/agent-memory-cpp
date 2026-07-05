# Codebase Orientation

## Current Tree

```text
cmake/
    AgentMemoryDependencies.cmake
    AgentMemoryOptions.cmake
examples/
    basic_usage.cpp
external/
    libmdbx/
    mdbx-containers/
src/agent_memory/
    AgentMemory.hpp
    core/
        LibraryInfo.hpp
        LibraryInfo.cpp
    domain/
        Document.hpp
        Document.cpp
        Identifiers.hpp
        Identifiers.cpp
        Metadata.hpp
        Metadata.cpp
        SourceKind.hpp
        SourceKind.cpp
    embedding/
        Embedding.hpp
        Embedding.cpp
        IEmbedder.hpp
        IEmbedder.cpp
    index/
        VectorIndex.hpp
        VectorIndex.cpp
        IVectorIndex.hpp
        IVectorIndex.cpp
    storage/
        IDocumentStorage.hpp
        IDocumentStorage.cpp
tests/
    CMakeLists.txt
    domain/
        domain_primitives_test.cpp
    embedding/
        embedding_contracts_test.cpp
    index/
        vector_index_contracts_test.cpp
    storage/
        feature_flags_test.cpp
        document_storage_contract_test.cpp
    smoke/
        agent_memory_smoke.cpp
```

## Public Includes

Consumers include project headers through the `agent_memory/` prefix:

```cpp
#include <agent_memory/AgentMemory.hpp>
```

The build interface currently exposes `src/` as the public include root.

## Existing Smoke API

`agent_memory::core::LibraryInfo` provides minimal project metadata for the
initial smoke test and example:

- `library_name()`;
- `library_description()`;
- `library_version()`.

Do not treat this class as the future application facade.

## Domain Primitives

The domain layer currently provides dependency-free value objects for the first
storage and indexing steps:

- `DocumentId` and `ChunkId`;
- `Metadata`;
- `SourceKind`;
- `Document`, `DocumentChunk`, and `TextRange`.

## Storage Contracts

The storage layer currently provides `IDocumentStorage`, a dependency-free
contract for persisting a `DocumentSnapshot` and loading/removing documents and
their chunks. Concrete backends, including MDBX, must implement this contract
outside the domain layer.

MDBX dependency wiring lives in `cmake/AgentMemoryDependencies.cmake` and is
disabled by default. It exposes `AGENT_MEMORY_HAS_MDBX` as a public compile
definition with value `0` or `1`.

When source dependencies are used, `external/libmdbx` and
`external/mdbx-containers` are flat sibling submodules. The build adds libmdbx
first so `mdbx-containers` can reuse the parent-provided MDBX target.

## Embedding Direction

Embedding code starts with dependency-free contracts in
`src/agent_memory/embedding/`:

- `Embedding`, `EmbeddingRequest`, and `EmbeddingModelInfo`;
- `EmbeddingPurpose`, `SimilarityMetric`, and `PoolingMode`;
- `IEmbedder` with single-item and batch embedding methods.

Concrete providers such as `llama.cpp`, ONNX Runtime, or HTTP APIs belong
behind optional adapter boundaries. Do not fork `cpp-llamalib` to turn a
chat/generation wrapper into the project embedding API.

## Index Contracts

Vector index contracts live in `src/agent_memory/index/`:

- `VectorRecord`, `VectorSearchQuery`, `VectorSearchResult`, and
  `MetadataFilter`;
- `matches_metadata_filters()`;
- `IVectorIndex` for upsert/find/search/erase/clear operations.

Concrete exact or approximate vector indexes should implement this contract
without defining retrieval policy.

## Where To Add Code

- Core metadata and stable library primitives: `src/agent_memory/core/`.
- Domain value objects and contracts: `src/agent_memory/domain/` or the
  relevant domain slice.
- Storage contracts and neutral types: `src/agent_memory/storage/`.
- MDBX-specific implementation details: future infrastructure/storage area.
- Embedding contracts: `src/agent_memory/embedding/`.
- Retrieval/ranking behavior: `src/agent_memory/retrieval/`.
- Memory policies and composition: `src/agent_memory/memory/`.
- Context builders and formatting: `src/agent_memory/context/`.

Create a new area only when the PR introduces real code for that area.

## Tests And Examples

- Put unit and smoke tests under `tests/`.
- Put small consumer-style examples under `examples/`.
- Keep examples non-interactive unless the user explicitly requests an
  interactive example.
