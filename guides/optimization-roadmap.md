# Optimization Roadmap

> C++17 compliance: кодовые сниппеты используют const std::vector& вместо std::span и явные конструкторы вместо designated initializers. Dedupe distance — cosine, lower = stricter.

## Purpose

This document describes the optimization layers (vector math, vector encoding,
binary signatures, binary bucket index, secondary indexes) for the retrieval
primitives in `agent-memory-cpp`. It concretizes **Layer 2** of
[`guides/memory-stacks-roadmap.md`](memory-stacks-roadmap.md), defining how the
storage-shape primitives used by retrieval (dense vectors, binary signatures,
reverse indexes) are organized under the component architecture
(Envelope + Components + SearchProjections), the capability-aware MDBX layout,
the multi-projection / multi-model embedding model (ADR-007), and the
scope-aware multi-tenancy rule (ADR-012).

Non-goals of this document:

- The component model itself and the Envelope schema (see
  [`memory-stacks-roadmap.md` Section 3](memory-stacks-roadmap.md#3-adr-001-memory-data-model)).
- BM25F postings, tokenization, projection_kind-aware lexical indexing (see
  [`lexical-search-roadmap.md`](lexical-search-roadmap.md)).
- CompactionWorker and EmbeddingRecomputeJob semantics (see
  [`compaction-roadmap.md`](compaction-roadmap.md), future).
- CLI and tooling (see [`cli-roadmap.md`](cli-roadmap.md), future).

## Core Rules

- Keep `Embedding` as `std::vector<float>` in the public contract.
- Treat `Embedding` as a portable storage/data format, not as a math engine.
- Use Eigen only behind optional adapter or algorithm boundaries.
- Use `Eigen::Map` for temporary zero-copy views; do not store long-lived maps
  over vectors that may be destroyed or reallocated.
- Compress text and document payloads before compressing hot vector-search data.
- Keep full float embeddings available for final reranking.
- Treat binary signatures as candidate filters, not as final ranking truth.
- Measure approximate search quality by recall and latency against an exact
  float baseline.
- Dense vector storage is keyed by `(scope_id, model_id, projection_kind,
  unit_id)`; binary bucket keys by `(scope_id, projection_kind, short_key)`
  when DenseVectors is enabled. If only BM25F without dense vectors is used,
  the projection_kind component is optional and keys may collapse to scope-only.
- All secondary indexes are scope-aware: every key begins with `scope_id`.
- Multi-projection and multi-model embeddings live side by side in the same
  `embedding_vectors` DBI, addressed by `projection_kind` and `model_id`
  respectively.

## Near-Term Tasks

### Vector Math Baseline

- Add a small dependency-free `math` or `index` helper for dot product, cosine,
  negative squared Euclidean distance, normalization, and score ordering.
- Reuse the helper from exact search and future rerank code.
- Keep the first version std-only.
- Add tests for dimension mismatch, zero vectors, normalized vectors, and score
  ordering.

### Optional Eigen Adapter

- Add an optional CMake option only when real Eigen-backed code is introduced:

```cmake
AGENT_MEMORY_ENABLE_EIGEN
```

- Expose a public feature macro with a `0` or `1` value when the option is
  introduced:

```cpp
AGENT_MEMORY_HAS_EIGEN
```

- Use `Eigen::Map<const Eigen::VectorXf>` over `Embedding::values` for dot,
  norm, cosine, batch scoring, centroid, averaging, and reranking experiments.
- Keep Eigen out of `Embedding.hpp`, `IEmbedder.hpp`, `IEmbeddingStore`, and
  other dependency-free contracts.
- Benchmark Eigen paths against the std-only baseline before making them the
  default for any algorithm.

### Compression Contracts

- Add dependency-free compression value types before adding a codec backend.
- Prefer a lightweight `ByteView` for C++17 instead of `std::span`.
- Model at least:

```cpp
enum class CompressionCodec {
    None,
    Zstd
};

struct CompressedBlobHeader final {
    CompressionCodec codec = CompressionCodec::None;
    std::uint32_t codec_level = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t uncompressed_hash = 0;
    std::string dictionary_id;
};
```

- Store hashes of uncompressed content so change detection does not depend on a
  codec, level, or dictionary choice.
- Keep dictionaries identified by stable `dictionary_id` values.
- Note: per ADR-007, embedding vectors share the same `CompressionCodec`
  pipeline as text payloads; dictionaries are still trained per domain
  (text chunks, binary bucket lists, embedding blobs).

### Optional Zstd Adapter

- Add Zstd as an optional dependency after the compression contracts exist:

```cmake
AGENT_MEMORY_ENABLE_ZSTD
```

```cpp
AGENT_MEMORY_HAS_ZSTD
```

- Use Zstd as the preferred text/chunk compression backend.
- Keep `None` as the baseline codec for tests and minimal builds.
- Treat miniz/zlib or LZ4 as later alternatives, not as the first required
  implementation.
- Train separate dictionaries for different payload domains, for example text
  chunks, binary bucket lists, and embedding blobs.
- Do not use a text-trained dictionary for binary vector blobs without a
  benchmark.

### Compressed Text Storage

- Compress document and chunk text independently enough to preserve random
  access by chunk id.
- Avoid requiring decompression of a full document just to return one retrieved
  chunk.
- Keep metadata small and usually uncompressed unless a benchmark shows value.
- Add tests that verify round-trip content, uncompressed hash, codec metadata,
  and missing-dictionary failures.

## Vector Encoding Tasks

### Canonical Float Storage

- Keep float32 embeddings as the source of truth for quality-sensitive rerank.
- Store compressed float vectors only for cold storage, archives, or benchmarked
  bucket layouts.
- Do not run hot vector search by decompressing every candidate from a generic
  compressed blob unless a benchmark proves it is faster for the target
  workload.

### Future Encodings

Plan a separate vector encoding layer instead of changing `Embedding`:

```cpp
enum class VectorEncoding {
    Float32,
    Float16,
    Int8,
    BinarySignature,
    ProductQuantized
};
```

Possible follow-up tasks:

- Add `EncodedVector` value types for persisted or index-specific encodings.
- Add float16 and int8 experiments after exact float baselines are available.
- Add product quantization only with benchmark and quality reporting.
- Keep encoding metadata tied to model id, dimension, similarity metric, and
  normalization.
- Encoding metadata also carries `projection_kind`, so two encodings of the same
  unit by different projections are not silently mixed.

## Projection-Aware Vector Storage

Per ADR-007 in `memory-stacks-roadmap.md`, dense vector storage is multi-model
and multi-projection. A single unit may carry independent embeddings for:

- different models (e.g. `bge-m3` and `openai-3-small`), and
- different projection kinds (e.g. `Original` and `DenseContextual`).

The `embedding_vectors` DBI is keyed accordingly:

```text
embedding_vectors
    key   = (scope_id, model_id, ProjectionKind, UnitId)
    value = vector_blob  (float32, optionally compressed)
```

The companion metadata table records provenance, encoder configuration, and
compaction state:

```text
embedding_meta
    key   = (scope_id, UnitId, ProjectionKind, model_id, version)
    value = EmbeddingMetaComponent
            { model_id, version, dim, encoder_id, seed,
              written_at_ms, is_active, superseded_by_revision }
```

### Multi-Projection Example

```cpp
auto& store = stack.embeddings();

EmbeddingWriteRequest req_original;
req_original.unit_id = my_chunk_id;
req_original.scope_id = my_scope_id;
req_original.projection_kind = ProjectionKind::Original;
req_original.model_id = "bge-m3";
req_original.model_version = "1.0";
req_original.vector = dense_vector_original_projection;
store.upsert(req_original);

EmbeddingWriteRequest req_contextual;
req_contextual.unit_id = my_chunk_id;
req_contextual.scope_id = my_scope_id;
req_contextual.projection_kind = ProjectionKind::DenseContextual;
req_contextual.model_id = "bge-m3";
req_contextual.model_version = "1.0";
req_contextual.vector = dense_vector_dense_contextual_projection;
store.upsert(req_contextual);
```

Both writes coexist in the same DBI; they are independent rows with distinct
keys.

### Multi-Model Example

```cpp
EmbeddingWriteRequest req_openai;
req_openai.unit_id = my_chunk_id;
req_openai.scope_id = my_scope_id;
req_openai.projection_kind = ProjectionKind::Original;
req_openai.model_id = "openai-3-small";
req_openai.model_version = "2024-01";
req_openai.vector = openai_vector_original_projection;
store.upsert(req_openai);
```

After both models are present, retrieval can fuse their results with RRF:

```cpp
SearchOptions search_opts;
search_opts.scope_ids = {my_scope_id};
search_opts.models = {"bge-m3", "openai-3-small"};
search_opts.projection_kinds = {ProjectionKind::Original,
                                ProjectionKind::DenseContextual};
search_opts.fusion = FusionStrategy::RRF;
search_opts.limit = 32;
auto hits = store.search_multi_model(query_vector, search_opts);
```

The RRF fusion (or weighted alternative) is implemented in the retrieval layer
(`retrieval/`) over per-(model, projection) candidate lists produced by
`IEmbeddingStore`.

### EmbeddingMeta Component

`EmbeddingMetaComponent` (per `memory-stacks-roadmap.md` Section 12.2) carries
metadata that lets the retrieval layer pick the right embedding at query time:

```text
EmbeddingMetaComponent {
    ProjectionKind projection_kind;
    std::string    model_id;
    std::string    version;
    std::uint32_t  dim;
    std::string    encoder_id;
    std::uint64_t  seed;
    std::int64_t   written_at_ms;
    bool           is_active;             // false during migration
    std::uint32_t  superseded_by_revision; // 0 if active
}
```

Two embeddings with the same `(scope_id, unit_id)` but different
`projection_kind` or `model_id` are siblings, not duplicates.

### Invariants

- For a given `(scope_id, unit_id, projection_kind)`, at most one row per
  `model_id` has `is_active == true`. Older revisions may persist until
  compaction purge.
- A read that requests a `projection_kind` with no `is_active` row falls back
  to the most recent `is_active` sibling of the same unit, or returns an empty
  vector if none exists.
- Cross-model fusion requires at least two `is_active` rows from different
  `model_id` values within the same `(scope_id, projection_kind)`.

## Embedding Migration Workflow

Per ADR-007 and open issue 17.8 in `memory-stacks-roadmap.md`, embedding models
are not pinned forever. The migration path is the `EmbeddingRecomputeJob`,
executed by `CompactionWorker`.

### Migration Job Shape

```cpp
struct EmbeddingRecomputeJob final {
    JobId                 job_id;
    ScopeId               scope_id;
    std::string           source_model_id;
    std::string           target_model_id;
    std::vector<ProjectionKind> projection_kinds; // subset, e.g. {Original}
    std::optional<KnowledgeUnitId> unit_filter;   // nullopt = all units in scope
    std::uint32_t         new_version;
    CompactionHandoff     handoff;
};
```

Progress is reported through `compaction_handoffs` (see
`memory-stacks-roadmap.md` Section 12.4 and `compaction-roadmap.md`). The
handoff payload includes the recomputed `(unit_id, projection_kind)` count, the
target `version`, and a recovery token for crash-safe resume.

### Per-(unit_id, projection_kind) Granularity

The job recomputes one embedding per `(unit_id, projection_kind)` it owns:

```text
for each (unit_id, projection_kind) in scope:
    fetch SearchProjection[projection_kind] for unit_id
    embed it with the target model
    write new embedding_meta row with version = new_version, is_active = true
    write new embedding_vectors row keyed by (scope_id, target_model_id,
                                              projection_kind, unit_id)
```

This means a unit migrated under one projection does not need to redo the
other projections. Migration cost scales with the chosen subset.

### Old Embeddings During Migration

Old rows remain in `embedding_meta` and `embedding_vectors` until compaction
purge. Their state is:

- `EmbeddingMetaComponent::is_active = false`.
- `EmbeddingMetaComponent::superseded_by_revision = new_version`.

The retrieval layer still sees them, but with `is_active = false` they are not
fused into the primary candidate list unless explicitly requested (for A/B
comparison or migration telemetry).

### Multi-Model Search During Migration

A query arriving while migration is in flight may encounter:

- new embedding for unit A,
- old embedding for unit B,
- no embedding yet for unit C.

The retrieval layer merges candidates from both active models. Per-unit score
selection prefers the active revision, but `is_active = false` rows may still
participate in RRF for short windows if the worker has not yet flushed them.
This is intentional: the migration is eventually consistent, but visibility is
never empty.

### Migration Telemetry

Track during migration:

```text
units_total
units_recomputed
units_failed
avg_recompute_latency_ms
p95_recompute_latency_ms
old_active_count (pre)
new_active_count (post)
rrf_skip_rate_old (window)
```

These counters land in `compaction_handoffs` and feed the embedding-migration
metrics used by M2 ship-it criteria (`memory-stacks-roadmap.md` Section 13.3).

## Binary Signature Index Tasks

### Dependency-Free Signature Types

- Add `BinarySignature` with `std::vector<std::uint64_t> words` and
  `std::size_t bit_count`.
- Ensure unused bits in the last word are zeroed.
- Add `hamming_distance(lhs, rhs)` implemented through XOR and popcount.
- While the project remains C++17, use compiler intrinsics or a local fallback
  instead of requiring C++20 `std::popcount`.
- Add `BinarySignatureInfo` with encoder id, source model id, source
  projection kind, source dimension, signature bits, approximated metric, and
  seed.
- Add `IBinarySignatureEncoder` as a dependency-free contract.

A binary signature is always tied to a specific projection kind and model id.
A unit with two projections has two signatures; a unit migrated to a new
model has new signatures alongside (or superseding) the old ones.

### Baseline Encoder

- Implement random-hyperplane LSH first:

```text
bit_i = sign(dot(embedding, random_hyperplane_i))
```

- Make generation deterministic by seed, dimension, signature bit count, and
  projection kind.
- Store encoder config with the index.
- Reject or rebuild indexes when model id, projection kind, dimension,
  encoder id, seed, or signature bit count does not match.

### Experimental Encoder

- Add a Haar-like encoder only after the random-hyperplane baseline:

```text
bit_i = sum(group_a) > sum(group_b)
```

- Mark it experimental.
- Compare recall and latency against random hyperplanes.
- Do not treat Haar-like signatures as the default without benchmark evidence.

### Short Key Generation

- Add short-key strategies:

```text
prefix bits
mixed bits
```

- Use `uint32_t` for 24, 28, and 32 bit keys.
- Use `uint64_t` for 64 bit keys.
- Make short-key strategy part of index config and require rebuild when it
  changes.
- Prefer mixed bits if prefix bits create hot buckets.
- Short-key layout for a bucket index covering dense vectors includes the
  projection kind: a query over `Original` projections must never pick up
  candidates from `DenseContextual` projections.

### Neighbor Bucket Masks

- Add `make_hamming_masks(bit_count, radius)` for short-key lookup.
- Cache masks by `(bit_count, radius)`.
- Add a guard against excessive lookup counts.
- Expected lookup counts:

```text
24 bits, radius 0 -> 1 bucket
24 bits, radius 1 -> 25 buckets
24 bits, radius 2 -> 301 buckets
24 bits, radius 3 -> 2325 buckets

28 bits, radius 0 -> 1 bucket
28 bits, radius 1 -> 29 buckets
28 bits, radius 2 -> 407 buckets
28 bits, radius 3 -> 3683 buckets
```

## Binary Bucket Index Tasks

### In-Memory Prototype

- Build the first binary bucket index in memory, without MDBX and without Zstd.
- Use short signature keys to collect candidate postings.
- Filter by full-signature Hamming distance.
- Fetch or keep float embeddings for exact rerank.
- Compare against `ExactVectorIndex` for recall and latency.

### MDBX Prototype

- Add MDBX-backed bucket storage after the in-memory prototype.
- Integrate with resource manifests before treating bucket storage as mutable
  production storage; bucket postings need `unit_id` and generation or an
  equivalent stale-entry guard.
- Prefer the two-stage layout first:

```text
binary_bucket_index:
    key   = (scope_id, projection_kind, short signature key)
    value = compressed or uncompressed posting list
            [ { unit_id, full_signature, generation }, ... ]

embedding_store:
    key   = (scope_id, model_id, projection_kind, unit_id)
    value = float32 vector blob or encoded vector blob

unit_store:
    key   = (scope_id, unit_id)
    value = compressed chunk text and metadata
```

- Store bucket values as compact posting lists of `{unit_id, full_signature,
  generation}`.
- Keep one-stage bucket values with embedded float vectors as an experimental
  benchmark variant.
- Prefer sparse MDBX key-value lookup for 64-bit short keys.
- Treat dense direct-address directories as possible only for small key sizes
  and only after measuring memory cost.
- When `DenseVectors` is not enabled (BM25F-only profiles), the
  `projection_kind` component may be omitted from the bucket key, collapsing
  it to `(scope_id, short signature key)`. This keeps legacy lexical-only
  stacks on the simpler layout.

### Mutable Bucket Updates

- Do not rely on stable entry offsets inside compressed bucket blobs.
- For deletion or reindexing, filter bucket entries by `unit_id`, `generation`,
  and `projection_kind` after decompression.
- For frequently updated memory, allow stale bucket entries and skip them at
  query time until compaction rewrites affected buckets.
- Keep compaction observable through benchmark counters such as stale entry
  ratio, rewritten bucket count, and reclaimed bytes.
- Embedding migration reuses the same stale-entry pattern: old signatures
  remain in their buckets until compaction rewrite.

### Query Pipeline

The target approximate pipeline is:

```text
query text
    -> query embedding
    -> projection_kind filter (Original, DenseContextual, ...)
    -> full binary signature
    -> short key
    -> neighbor keys by short Hamming radius
    -> bucket lookups (scope_id, projection_kind, short_key)
    -> decode/decompress posting lists
    -> full-signature Hamming filter
    -> unique unit ids
    -> batch fetch float embeddings for the requested (model_id, projection_kind)
    -> optional cross-model RRF
    -> exact rerank
    -> fetch unit text
```

Float rerank remains mandatory for quality-sensitive retrieval.

## Secondary & Reverse Indexes

The knowledge base layers (lexical, graph, temporal, QA, metadata filters)
all share a common storage pattern: a primary table plus a set of secondary
indexes that turn a secondary key into a primary record reference. The full
design lives in [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) Section
12.3 and in [`lexical-search-roadmap.md`](lexical-search-roadmap.md). The
section here is the storage-shape view of that pattern.

All secondary indexes are scope-aware (ADR-012): every key begins with
`scope_id`. The reverse indexes that opt into DenseVectors also include
`projection_kind` so that one bucket never serves two projections.

### Pattern

```text
primary table:
    key   = primary_record_id (KnowledgeUnitId, ChunkId, etc.)
    value = payload blob

secondary index:
    key   = (scope_id, inverted_key [+, projection_kind])
            e.g. (scope_id, token, projection_kind, field)
                 (scope_id, metadata_key, encoded_value)
                 (scope_id, src_node_id, edge_kind, dst_node_id)
    value = primary_record_id or compact posting list (DUPSORT)
```

Secondary indexes are derived data. They are updated transactionally with
the primary table when the backend supports it; otherwise they are rebuilt by a
`compact_index()` pass. Backends are allowed to omit secondary indexes and
answer queries by scanning the primary table, but the public contract says
"should have them" so a future MDBX adapter can be benchmarked fairly against
the in-memory baseline.

### Concrete Index Catalog

```text
inverted_token_to_unit:
    key   = (scope_id, token_id, projection_kind, field_id)
    value = DUPSORT UnitId
    used by:  lexical search (pre-filter for phrase/proximity),
              lexical BM25F fallback when dense is unavailable

metadata_to_resource:
    key   = (scope_id, metadata_key, encoded_value, unit_id)
    value = empty
    [ReverseIndexTable]
    used by:  MetadataFilter, MetadataInFilter, MetadataTagFilter

field_to_postings:
    key   = (scope_id, projection_kind, field_id, token_id, unit_id)
    value = PostingStats
    used by:  BM25F and fielded sparse retrieval

graph_edges_by_src:
    key   = (scope_id, from_unit_id, edge_kind, to_unit_id)
    value = GraphEdgePayload (weight, reason, generation)
    used by:  IGraphStore::expand, outgoing lookup

graph_edges_by_dst:
    key   = (scope_id, to_unit_id, edge_kind, from_unit_id)
    value = GraphEdgePayload (edge_kind, weight, reason, generation)
    used by:  IRelationStore::incoming, reverse expansion

temporal_event_index:
    key   = (scope_id, valid_from_ms, valid_until_ms, unit_id)
    value = empty
    used by:  ITemporalIndex::range, ITemporalIndex::at

temporal_unit_index:
    key   = (scope_id, observed_at_ms, unit_id)
    value = empty
    used by:  ITemporalIndex::at, observed-at lookup

speaker_to_units:
    key   = (scope_id, speaker_id, unit_id)
    value = empty
    used by:  SpeakerScopePolicy::include_*

session_to_units:
    key   = (scope_id, session_id, unit_id)
    value = empty
    used by:  session-scoped retrieval

usage_stats_index:
    key   = (scope_id, unit_id)
    value = UsageStatsComponent (copy for fast ranking)

generation_index:
    key   = (scope_id, kind, generation, unit_id)
    value = empty (presence-only)
    used by:  stale-filter fast path during reindex

binary_bucket_index:                     // if DenseVectors
    key   = (scope_id, projection_kind, short_key)
    value = posting list
            [ { unit_id, full_signature, generation }, ... ]
    used by:  binary signature candidate filter

embedding_meta:                          // if EmbeddingMeta or EmbeddingMigration
    key   = (scope_id, unit_id, projection_kind, model_id, version)
    value = EmbeddingMetaComponent
    used by:  projection/model selection at retrieval time

embedding_vectors:                       // if DenseVectors
    key   = (scope_id, model_id, projection_kind, unit_id)
    value = vector_blob
    used by:  IDenseIndex::search, IDenseIndex::search_multi_model
```

The `inverted_token_to_unit` index is a thin convenience over the field
postings: the lexical index already knows the token -> unit mapping, but a
reverse index lets the QA and fact layers skip a full lexical scan when they
only need unit ids by token.

### Cross-Table Transaction Pattern

When a backend supports transactions, the writer follows this pattern:

```text
begin writable transaction
    upsert primary row(s)
    upsert secondary index entries
    update resource manifest if affected
    upsert embedding_meta + embedding_vectors if DenseVectors
    upsert binary_bucket_index entry if DenseVectors
commit
```

A partial failure (e.g. MDBX map full) rolls back the whole transaction.
Without transactions, secondary indexes are eventually-consistent: a
`compact_index()` pass reconciles them with the primary table.

### Bulk Index Build Versus Incremental Updates

```text
bulk build (startup, full reindex):
    scan primary table in (scope_id, key) order
    for each row, derive and upsert its secondary index entries
    one pass; O(N) work, single transaction when supported

incremental (per unit update):
    load old manifest
    delete old secondary entries for affected rows
    upsert new primary rows
    upsert new secondary entries
    upsert new embedding rows (per active projection_kind + model_id)
    commit
```

The bulk build path is the deterministic baseline; CI runs it on every test
corpus before benchmarking.

### Compression Of Secondary Index Values

Secondary index values are usually small (a `KnowledgeUnitId` is 36 chars or a
UUID; an `EdgePayload` is a few hundred bytes). Generic compression rarely
helps and breaks the key layout assumptions of DUPSORT tables. The default for
secondary indexes is no compression. Compression is only considered when:

- the value carries a long list (e.g. an embedded posting list per token), and
- a benchmark shows a measured gain on the target corpus.

### Stale Filter Pattern

Generation filtering is the cheap way to remove stale entries:

```text
unit_id matches secondary index lookup
    -> load current generation from generation_index or unit_stats
    -> if stored generation != current generation: skip
    -> else: accept
```

The pattern must not turn secondary index lookups into many random reads.
Implementations can amortize generation checks by:

- keeping the current generation in a small in-memory cache per resource;
- batching the current-generation lookup with the secondary index read in the
  same transaction;
- storing a per-unit `last_seen_generation` inside the secondary index value
  itself (e.g. `EdgePayload::generation`).

### Schema Versioning

Every secondary index carries a `schema_version` byte in its value so old and
new indexes can be detected and rebuilt side by side during a rolling upgrade.
The first implementation pins `schema_version = 1`; bumping it is a breaking
change that requires a `compact_index()` rebuild.

When `DenseVectors` is enabled, the bucket index schema_version also reflects
its `projection_kind` layout. Adding a new projection kind bumps the version
once per index rebuild, never per write.

## Benchmark Tasks

### Lexical Baselines

- Keep a dependency-free BM25 baseline for hybrid retrieval experiments.
- Compare hybrid methods against BM25-only and vector-only baselines.
- Use [`guides/lexical-search-roadmap.md`](lexical-search-roadmap.md) for
  tokenization, postings, Unicode, phrase/proximity, fuzzy search, BM25F,
  and raw resource store planning.

### Exact Baseline

- Keep an exact brute-force float baseline for every approximate experiment.
- Use the same similarity metric as the approximate pipeline.
- If vectors are normalized, benchmark cosine optimized as dot product.
- Run the exact baseline per `(projection_kind, model_id)` pair so the
  approximate pipeline can be compared within the same slice.

### Projection-Aware Metrics

In addition to the standard metrics, record per projection kind:

```text
recall@1_by_projection
recall@5_by_projection
recall@10_by_projection
recall@50_by_projection
candidate_count_before_full_filter_by_projection
candidate_count_after_full_filter_by_projection
cross_model_rrf_lift   = recall@10(multi-model RRF) / max recall@10(single model)
```

Per-model slices are reported similarly when more than one embedding model is
active in the profile.

### Metrics

Record at least:

```text
recall@1
recall@5
recall@10
recall@50
candidate_count_before_full_filter
candidate_count_after_full_filter
latency
storage_size
read_amplification
decompression_time
rerank_time
migration_window_recall_drift     // migration telemetry
```

Initial target:

```text
recall@10 >= 0.95
candidate_count_after_full_filter << total_vectors
latency below brute force for large enough datasets
storage overhead acceptable for embedded use
```

### Matrix

Benchmark:

```text
short signature bits: 24, 28, 32, 64
full signature bits: 128, 256, 512, 1024
short Hamming radius: 0, 1, 2, 3
projection kinds: Original, DenseContextual, [Bm25Body/Title/Heading/Symbols]
model ids: { bge-m3 }, { openai-3-small }, { bge-m3 + openai-3-small }
fusion: none, RRF k=60, WeightedMax
compression: none, zstd level 1, zstd level 3, zstd level 6, zstd dictionary
layout: two-stage bucket vs one-stage bucket
datasets: random, clustered, real text chunks with real embeddings
```

The cross-model matrix is gated on at least one profile having multi-model
embeddings enabled (FullResearchMemoryStack, for instance).

### Bucket Diagnostics

Track:

```text
total_vectors
total_buckets_used
empty_bucket_ratio
avg_bucket_size
max_bucket_size
p50_bucket_size
p95_bucket_size
p99_bucket_size
hot_bucket_count
bucket_fragmentation     // stale entries per bucket
rewrite_dwell_ms         // compaction rewrite latency p95
```

When buckets are too hot, test mixed short keys, another seed, another
encoder, a larger short-key bit count, or splitting the bucket by
`projection_kind`.

## Planned CMake Options

Add these only when the corresponding implementation exists:

```cmake
AGENT_MEMORY_ENABLE_BINARY_INDEX
AGENT_MEMORY_ENABLE_ZSTD
AGENT_MEMORY_ENABLE_EIGEN
AGENT_MEMORY_BUILD_BENCHMARKS
```

Planned public feature macros:

```cpp
AGENT_MEMORY_HAS_BINARY_INDEX
AGENT_MEMORY_HAS_ZSTD
AGENT_MEMORY_HAS_EIGEN
```

Once introduced, these macros should be defined consistently as `0` or `1`.

## Recommended Implementation Order

This order is aligned with the 16-step sequence in
[`memory-stacks-roadmap.md` Section 16](memory-stacks-roadmap.md#16-recommended-implementation-order).
Steps 1-3 mirror Layer 1 math and compression baseline; steps 4-6 introduce
projection-aware vector encoding; steps 7-9 add the binary signature and
bucket layers; steps 10-13 add scope-aware secondary indexes; steps 14-15
add projection-aware benchmarks.

### Steps 1-3: Math, compression, and Zstd baseline (unchanged)

1. Dependency-free vector math helpers.
2. Compression contracts with `CompressionCodec::None`.
3. Optional Zstd adapter.

### Steps 4-6: Vector encoding with projection awareness

4. Compressed document/chunk text storage (projection-agnostic).
5. Dependency-free binary signature value types and Hamming distance, with
   `projection_kind` recorded in `BinarySignatureInfo`.
6. Random-hyperplane binary encoder keyed by
   `(model_id, projection_kind, dim, seed)`. Compressed float storage
   benchmark for embedding blobs.

### Steps 7-9: Binary signature, bucket, and projection layout

7. Short-key generation and Hamming neighbor masks. Bucket key layout is
   `(scope_id, projection_kind, short_key)`.
8. In-memory binary bucket index prototype with scope-aware and
   projection-aware buckets.
9. Recall/latency benchmark against exact float search, run per
   `(model_id, projection_kind)` slice.

### Steps 10-13: Scope-aware secondary indexes and resource manifest

10. Resource manifest contracts for targeted reindexing, scope-aware.
11. MDBX-backed two-stage bucket storage with
    `embedding_vectors: (scope_id, model_id, projection_kind, unit_id)` and
    `binary_bucket_index: (scope_id, projection_kind, short_key)`.
12. Bucket compression benchmarks and bucket diagnostics.
13. Generation-aware stale filtering and compaction hooks.

### Steps 14-15: Projection-aware benchmarks and one-stage variants

14. Optional Eigen rerank/scoring adapter, applied per
    `(model_id, projection_kind)` candidate list.
15. One-stage bucket benchmark variant, projection-aware matrix, and
    cross-model RRF benchmark when at least two models are present.

### Steps 16-19: Knowledge base integration (unchanged shape, scope-aware)

16. Cross-table transaction pattern: a single MDBX writable transaction
    updates a primary row plus its scope-aware secondary index entries plus
    its resource manifest entries plus its embedding rows. Includes a failure
    test that asserts atomicity (no half-committed secondary index).
17. MDBX-backed `KnowledgeUnitStore` with `(scope_id, unit_id)` and
    `knowledge_units_by_kind` reverse index. Each kind has a kind-specific
    layout. The store ships with a `compact_index()` pass that rebuilds the
    reverse index from the primary table.
18. MDBX-backed `FactStore`, `QAKnowledgeBase`, `GraphStore`, and
    `TemporalIndex`. Each one is a thin shim over its in-memory baseline that
    uses the cross-table transaction pattern from step 16. Each store ships
    with a generation-aware reindex path that is wired into the
    `ResourceIndexer`.
19. Schema versioning: every secondary index value gains a `schema_version`
    byte (default `1`). Bumping the version requires a `compact_index()`
    rebuild. Tests assert that a downgrade does not silently corrupt lookups.

### Step 20: Embedding migration workflow

20. EmbeddingRecomputeJob end-to-end. Validates the migration path described
    in the "Embedding Migration Workflow" section above: per-(unit_id,
    projection_kind) recompute, dual-write of new embeddings with
    `is_active = true`, old embeddings retained with
    `is_active = false`, progress reported through `compaction_handoffs`,
    and cross-model RRF searches during migration without empty windows.

### Step 21: Final storage layer integration

21. Final storage layer integration: the `ResourceIndexer` calls the unit
    store, the fact store, the QA store, the graph store, the temporal
    index, the lexical field postings, the embedding store, and the binary
    bucket index in a single transaction. The golden dataset
    (`memory-stacks-roadmap.md` Section 13 ship-it criteria) is run
    end-to-end and the hybrid lift target (Recall@10 hybrid >= 1.20x
    BM25-only) is asserted, including the projection-aware and
    cross-model slices.

## Non-Goals For The First Optimization Pass

- Do not replace `std::vector<float>` in `Embedding`.
- Do not make Eigen a required public dependency.
- Do not compress hot vector-search data with a generic compressor without a
  benchmark.
- Do not treat binary Hamming results as final retrieval ranking.
- Do not add product quantization before simpler float16/int8/binary
  experiments and exact baselines exist.
- Do not collapse multi-projection embeddings into a single `(scope_id,
  unit_id)` key: ADR-007 requires `(scope_id, model_id, projection_kind,
  unit_id)`.
- Do not skip `scope_id` in any secondary index: ADR-012 mandates
  scope-awareness for multi-tenancy.
- Do not silently mix embeddings across `projection_kind` values during
  fusion or rerank.

## References

- [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) — Layer 2 context,
  ADR-007 (multi-projection embeddings), ADR-012 (scope-aware secondary
  indexes), capability matrix, MDBX layout (Section 12), implementation
  order (Section 16), open issues (Section 17, especially 17.8 for
  embedding migration).
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — Envelope +
  retrieval flow that this optimization layer serves.
- [`lexical-search-roadmap.md`](lexical-search-roadmap.md) — BM25F over
  projections, scope-aware inverted indexes, postings.
- [`compaction-roadmap.md`](compaction-roadmap.md) — CompactionWorker job
  types, handoff structure used by EmbeddingRecomputeJob.
- [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md) —
  TypeDiscriminatedTable for components, MultiTableWriter for atomic
  cross-table writes.
- [`architecture.md`](architecture.md) — 4-layer model and Maturity Levels.
- [`cli-roadmap.md`](cli-roadmap.md) (future) — `agent-memory-cli` target,
  embedding-migration telemetry inspection.