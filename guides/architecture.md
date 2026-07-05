# Architecture

## Baseline

Use a DDD-like architecture with small domain slices and explicit dependency
boundaries. The project is a static C++17 library first; optional infrastructure
should not leak into core APIs.

## Dependency Direction

Dependencies should point inward:

```text
infrastructure/adapters -> storage/index/embedding implementations
retrieval/memory/context -> domain contracts and value objects
domain/core -> standard library and stable internal primitives only
```

Rules:

- Core and domain code must not depend on MDBX, ONNX Runtime, HTTP clients,
  Python, or other optional infrastructure.
- Storage adapters implement storage contracts; they do not define memory
  strategy semantics.
- Embedding adapters implement embedding contracts; they do not own retrieval
  ranking policy.
- Retrieval implementations may compose embedding, index, and storage
  contracts, but they must not expose backend-specific dependencies through
  public retrieval contracts.
- Context assembly consumes retrieval results and memory policies; it should not
  perform storage-specific queries directly.

## Static Library Bias

The main target is `agent_memory`, built as a static library. Prefer `.cpp`
implementation files for behavior that may depend on external libraries or
change ABI/compile-time cost. Header-only code is acceptable for small value
types, templates, and trivial inline helpers.

## Boundary Guidelines

- Put stable value objects and contracts near the domain they describe.
- Put dependency-owning implementations under `infrastructure/` or a
  dependency-specific subarea.
- Keep public APIs small and easy to inspect.
- Avoid one large facade that hides all behavior.
- Avoid speculative plugin systems before at least two real implementations
  require the same extension point.

## Planned Storage Direction

Storage contracts live under `src/agent_memory/storage/` and stay dependency
free. MDBX-backed storage is expected to implement those contracts with
`mdbx-containers` as an adapter detail. The upper project should keep source
dependencies flat under `external/`: `libmdbx` and `mdbx-containers` are sibling
submodules, not nested dependency checkouts. Parent projects should be able to
provide dependency targets when possible.

MDBX support is opt-in through `AGENT_MEMORY_ENABLE_MDBX`. The build must reuse
an existing `mdbx_containers::mdbx_containers` target before adding a local
source tree or falling back to `find_package(mdbx_containers)`.

The MDBX document storage adapter lives under
`src/agent_memory/infrastructure/mdbx/`. It implements `IDocumentStorage` and
must not define memory strategy, retrieval ranking, or embedding behavior.

## Planned Resource Reindexing Direction

Ingestion should eventually track resource ownership for all derived records.
Each original source item should have a stable `ResourceId`, a current revision
or generation, and a manifest of chunks, embeddings, vector records, binary
bucket postings, lexical postings, or graph records derived from it.

This lets the system replace one markdown file, note, profile fact, or other
resource without rebuilding unrelated indexes. Resource reindexing should
compose storage, embedding, and index contracts; it should not become a large
facade that owns retrieval policy.

MDBX-backed implementations should update a resource, its manifest, and its
derived records in one writable transaction when possible. Approximate indexes
with compressed bucket lists may use stale-generation filtering or tombstones
before physical compaction.

Detailed tasks are tracked in `guides/resource-reindexing.md`.

## Planned Embedding Direction

Embedding contracts live under `src/agent_memory/embedding/` and stay
dependency free. Concrete providers such as `llama.cpp`, ONNX Runtime, or
OpenAI-compatible HTTP APIs must be implemented as optional adapters.

Do not fork chat/generation wrappers such as `cpp-llamalib` to make embeddings
fit them. The project should expose its own embedding contract with explicit
model metadata, dimensions, similarity metric, normalization, pooling, and
query/document purpose semantics.

## Planned Index Direction

Index contracts live under `src/agent_memory/index/` and stay dependency free.
Vector indexes store chunk embeddings and expose nearest-neighbour search by
query vector, result limit, score, and exact metadata filters. Exact in-memory,
MDBX-backed, or approximate indexes must implement these contracts without
owning retrieval ranking policy.

`ExactVectorIndex` is allowed in the index layer because it is dependency-free
and acts as the deterministic baseline for tests and small local workloads.

## Planned Lexical Search Direction

Lexical search contracts should stay dependency free and use `ChunkId` as the
primary retrieval key. Postings should also carry `ResourceId` and generation so
targeted reindexing can remove, replace, or skip stale entries without a full
corpus rebuild.

BM25 is the first ranked keyword baseline. Boolean, BM25F, phrase/proximity,
fuzzy, n-gram, graph, learned sparse, and late-interaction methods should be
added as separate focused layers.

Canonical persisted source and chunk text is UTF-8. UTF-32/code point buffers
are allowed only as temporary tokenizer internals or benchmarked derived
artifacts.

Detailed tasks are tracked in `guides/lexical-search-roadmap.md`.

## Planned Retrieval Direction

Retrieval contracts live under `src/agent_memory/retrieval/` and stay
dependency free. A retriever accepts text, embedding, or mixed query signals,
metadata filters, and a result limit, then returns ordered scored chunks.

Concrete retrievers should be built as composition over `IEmbedder`,
`IVectorIndex`, and `IDocumentStorage` rather than as one large facade.

Hybrid retrieval should start with metadata filters, BM25, vector search, and
Reciprocal Rank Fusion before adding planner-guided or learned reranking layers.

## Planned Optimization Direction

Optimization work should preserve the dependency-free contracts already used by
storage, embedding, index, and retrieval layers.

Text and document payloads may be compressed through an optional compression
adapter. Hot vector-search data should keep full float embeddings available for
final reranking, while approximate encodings such as binary signatures,
float16, int8, or product quantization live in separate index-specific layers.

Eigen can be used later through temporary zero-copy views such as `Eigen::Map`,
but `Embedding` must remain a `std::vector<float>` value type in the public API.

Binary signature and bucket indexes are approximate candidate filters. They
must be benchmarked against exact float search by recall@K, latency, candidate
count, storage size, read amplification, and decompression time before being
treated as production defaults.
