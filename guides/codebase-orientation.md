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
        Resource.hpp
        Resource.cpp
        SourceKind.hpp
        SourceKind.cpp
    embedding/
        Embedding.hpp
        Embedding.cpp
        IEmbedder.hpp
        IEmbedder.cpp
    index/
        ExactVectorIndex.hpp
        ExactVectorIndex.cpp
        VectorIndex.hpp
        VectorIndex.cpp
        IVectorIndex.hpp
        IVectorIndex.cpp
    retrieval/
        Retrieval.hpp
        Retrieval.cpp
        IRetriever.hpp
        IRetriever.cpp
    infrastructure/
        mdbx/
            MdbxDocumentStorage.hpp
            MdbxDocumentStorage.cpp
    storage/
        IDocumentStorage.hpp
        IDocumentStorage.cpp
tests/
    CMakeLists.txt
    domain/
        domain_primitives_test.cpp
        resource_manifest_test.cpp
    embedding/
        embedding_contracts_test.cpp
    index/
        vector_index_contracts_test.cpp
        exact_vector_index_test.cpp
    retrieval/
        retrieval_contracts_test.cpp
    infrastructure/
        mdbx/
            mdbx_document_storage_test.cpp
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
- `ResourceId`;
- `Metadata`;
- `SourceKind`;
- `Document`, `DocumentChunk`, and `TextRange`.
- `ResourceRevision`, `DerivedRecordKind`, `DerivedRecordRef`, and
  `ResourceManifest`.

## Storage Contracts

The storage layer currently provides `IDocumentStorage`, a dependency-free
contract for persisting a `DocumentSnapshot` and loading/removing documents and
their chunks. Concrete backends, including MDBX, must implement this contract
outside the domain layer.

`ChunkId` values are globally unique within a storage backend, not scoped only
to one document.

MDBX dependency wiring lives in `cmake/AgentMemoryDependencies.cmake` and is
disabled by default. It exposes `AGENT_MEMORY_HAS_MDBX` as a public compile
definition with value `0` or `1`.

When source dependencies are used, `external/libmdbx` and
`external/mdbx-containers` are flat sibling submodules. The build adds libmdbx
first so `mdbx-containers` can reuse the parent-provided MDBX target.

`MdbxDocumentStorage` lives in `src/agent_memory/infrastructure/mdbx/` and is
compiled only when `AGENT_MEMORY_ENABLE_MDBX=ON`. It implements
`IDocumentStorage` through `mdbx-containers` tables and stores adapter details
behind a Pimpl so public storage contracts stay dependency-free.

Future resource manifest storage should keep the same boundary: contracts stay
dependency-free, while MDBX tables for resource ownership and partial
reindexing belong behind infrastructure adapters.

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

`ExactVectorIndex` is the dependency-free in-memory baseline implementation. It
supports cosine, dot product, and Euclidean scoring, applies exact metadata
filters, and keeps result ordering deterministic by chunk id when scores tie.

## Retrieval Contracts

Retrieval contracts live in `src/agent_memory/retrieval/`:

- `RetrievalQuery`, `RetrievedChunk`, and `RetrievalResult`;
- `IRetriever` for text, embedding, or mixed retrieval queries.

Concrete retrievers may compose embedding, index, and storage contracts, but
backend-specific details must stay behind adapter boundaries.

## Where To Add Code

- Core metadata and stable library primitives: `src/agent_memory/core/`.
- Domain value objects and contracts: `src/agent_memory/domain/` or the
  relevant domain slice.
- Storage contracts and neutral types: `src/agent_memory/storage/`.
- MDBX-specific implementation details: future infrastructure/storage area.
- Embedding contracts: `src/agent_memory/embedding/`.
- Retrieval/ranking behavior: `src/agent_memory/retrieval/`.
- Resource ownership and targeted reindexing: start with domain/storage
  contracts, then add infrastructure adapters.
- Memory policies and composition: `src/agent_memory/memory/`.
- Context builders and formatting: `src/agent_memory/context/`.

Create a new area only when the PR introduces real code for that area.

## Tests And Examples

- Put unit and smoke tests under `tests/`.
- Put small consumer-style examples under `examples/`.
- Keep examples non-interactive unless the user explicitly requests an
  interactive example.
