# Code Intelligence Roadmap

> Engineering patterns borrowed from [`codebase-memory-mcp`](https://github.com/DeusData/codebase-memory-mcp) (MIT license, single-process C static-binary MCP server for code intelligence using tree-sitter + Hybrid LSP + SQLite graph, 14 MCP tools). This guide captures concrete primitives that are applicable to `agent-memory-cpp` roadmaps and where each pattern lands inside our existing guide set.

## 1. Purpose and provenance

`agent-memory-cpp` and `codebase-memory-mcp` solve different problems: the former is an embedded C++17 memory/retrieval engine for AI agents, the latter is a static-binary MCP server for indexing source code and answering structural queries against it. We are not adopting either its scope or its stack. We studied the source of `codebase-memory-mcp` because it ships, in ~162 KB of pure C, a tight set of engineering patterns that are directly applicable to our roadmap: near-clone detection, scalar-vector quantization, coverage shadowing of indexed regions, atomic shared ID generation, bounded graph traversal, schema introspection, a portable graph-artifact format, a small read-only query language, multi-pattern matching over compressed text, and an adaptive-poll background watcher.

License: MIT. Borrowing of constants, header excerpts, and high-level architectural ideas is fine. We MUST NOT copy-paste source code from `codebase-memory-mcp` into our repository; algorithms and code are copyrightable, numerical constants and standard algorithmic descriptions are not. Section 3 spells out the license-compliance rule and how constant provenance is tracked.

Cross-reference: the project-level entry for this analysis lives in
[`related-projects.md`](related-projects.md) §6 (added in 2026-07 with this
guide). All structural destinations in this guide are existing or planned
sections inside our existing guide set; cross-links at the end of each
pattern point to where the work lands.

## 2. Pattern catalogue

The nine patterns are grouped by priority. Each section gives a one-line
description, the originating file and constants in `codebase-memory-mcp`,
the destination inside our repository, a priority tag, and any open
questions to settle before implementing.

### Priority 1 — directly applicable, PR-track candidates

These four patterns map onto our existing roadmap sections and can be
considered in the next planning cycle. They are not "block the next PR";
they are candidates for an M2/M3 planning wave.

#### Pattern 1 — MinHash + LSH for near-clone detection

- **What:** Approximate Jaccard similarity over document signatures via
  MinHash + Locality-Sensitive Hashing bands, used by `codebase-memory-mcp`
  to find near-duplicate code chunks across an indexed project.
- **Their implementation:** `src/simhash/minhash.h` defines the parameters
  we are studying:

```c
#define CBM_MINHASH_K                64
#define CBM_MINHASH_MIN_NODES        30
#define CBM_MINHASH_JACCARD_THRESHOLD 0.95
#define CBM_LSH_BANDS                32
#define CBM_LSH_ROWS                 2
```

  Candidate threshold for `b = 32` bands and `r = 2` rows is approximately
  `(1/32)^(1/2) ≈ 0.177` Jaccard similarity. The pipeline: shingles a
  document into k-grams, hashes each shingle into `K = 64` MinHash
  buckets, then LSH bands of `(b = 32, r = 2)` are emitted so that pairs
  with Jaccard ≥ 0.177 collide in at least one band.
- **Our applicability:** augments — does NOT replace — the
  `binary_bucket_index` and `BinarySignatureEncoderRegistry` from
  [`optimization-roadmap.md`](optimization-roadmap.md) §"Binary Bucket
  Index Tasks". Specifically, Pattern 1 is a secondary candidate-filter on
  top of (or alongside) the Hamming distance candidate filter, intended
  for near-duplicate knowledge units (e.g. repeated QAPair canonical
  questions across sessions, or compiled articles with overlapping
  paragraph). PR #29 (binary signatures) ships the baseline encoder;
  Pattern 1 is a parallel pre-filter for a different metric (Jaccard over
  shingles vs. Hamming over embedding-derived bits).
- **Priority:** P1 — researched and ready for design. Mirrors PR #29 in
  shape but measures a different similarity and therefore does not
  conflict.
- **Open questions:**
  - Do we MinHash `primary_text` (envelope), `SearchProjection` body
    (Layer C), or `CompiledArticlePayload` body specifically? Pattern 1
    only makes sense for text payloads; dense units (`Entity`, `Relation`)
    already have Hamming binary candidates and do not need it.
  - Bands and rows trade recall vs bucket cardinality: `(b = 32, r = 2)`
    gives a 0.177 threshold on the Jaccard scale; do we want a stricter
    threshold (fewer collisions, larger posting lists per band) or a
    looser one (more candidates, larger fan-out)?
  - The `JACCARD_THRESHOLD = 0.95` constant is post-filter (after band
    collision). With the `(b = 32, r = 2)` bands that threshold is
    impossibly tight — most collisions will have Jaccard < 0.95. The
    design must reconcile "band threshold" (0.177) with "post-filter
    threshold" (0.95).

#### Pattern 2 — RaBitQ-style Rotated Scalar Quantization (RotSQ)

- **What:** A deterministic per-vector inner-product estimator that stores
  a small bit code (≈4 bits/dim) plus per-vector metadata so that the
  inner product between a query and a code can be approximated in
  constant time, with bounded error. Used by `codebase-memory-mcp` as a
  fast, low-storage ranker between a query embedding and corpus
  embeddings.
- **Their implementation:** `src/semantic/rotsq.h` — constants live
  inside an `enum` (not `#define`):

```c
enum {
    CBM_RSQ_IN_DIM = 768,                 /* input dimension (CBM_SEM_DIM) */
    CBM_RSQ_DIM = 1024,                   /* padded pow2 rotation dimension */
    CBM_RSQ_BITS = 4,                     /* bits per coordinate */
    CBM_RSQ_CODE_BYTES = CBM_RSQ_DIM / 2, /* two 4-bit codes per byte */
};
```

  Note: `CBM_RSQ_CODE_BYTES` is derived from `CBM_RSQ_DIM` (the padded
  pow2 rotation dimension, 1024), not from `CBM_RSQ_IN_DIM` (the input
  dimension, 768). Per-vector cost for a 768-dim embed: `512 B codes +
  12 B metadata (scale + offset + 4-byte Σ codes) ≈ 524 B` vs `3 072 B`
  for raw float32, ≈6× compression. Code expansion is deterministic and
  uses a fixed rotation matrix baked into the encoder header, so the
  same encoder applied to the same vector always yields the same code.
- **Our applicability:** slot alongside the Matryoshka and Product
  Quantization codecs documented in
  [`optimization-roadmap.md`](optimization-roadmap.md) §"Future
  Encodings". RotSQ is a different point in the compression/quality
  tradeoff: lower compression (~6×) than PQ (~96×), closer to float
  quality, and the inner-product estimator is a single matmul +
  per-coordinate lookup at query time. Useful for the
  `ApproximateVector` mode in
  [`optimization-roadmap.md`](optimization-roadmap.md) §"Dense Index
  Modes (Backend Selection)" as an additional codec backend, behind the
  `DenseIndexMode` interface.
- **Priority:** P1 — research direction only. Implementation not
  planned until benchmarks validate the workload match; PQ and Matryoshka
  codecs are M2+ candidates already, RotSQ would enter alongside them.
- **Open questions:**
  - Where in our codec registry does RotSQ live? It is not a PQ variant
    and not a Matryoshka truncation; it is a quantization with a
    per-vector deterministic estimator. Add to the codec id enum in
    [`optimization-roadmap.md`](optimization-roadmap.md) §"Future
    Encodings" alongside `Float32` / `Float16` / `Int8` /
    `BinarySignature` / `ProductQuantized`.
  - The 4-bit code requires a fixed-rotation matrix; we need a
    reproducibility story for it (encoder header vs per-deployment
    random matrix). Likely upstream-fixed (avoids drift across stacks).
  - Quality target: we need `Recall@10(RotSQ-only) ≈ 0.95x
    Recall@10(float_baseline)` as a minimum to enter the codec matrix.
    Validate before adding to the candidates list.

#### Pattern 3 — Coverage shadow graph

- **What:** A separate `Project → Folder → File` graph that records which
  files/regions were actually indexed (parsed fully, partially read,
  extract-only, or skipped/oversized). Queryable through normal graph
  queries without polluting the primary index. Lets `codebase-memory-mcp`
  answer "which files are missing from my index?" in one Cypher query.
- **Their implementation:** `src/store/store.h`. The coverage table
  stores flat rows with a kind + free-form detail; the `Project →
  Folder → File` graph under `<project>::missed` is a *derived* view
  materialized by `cbm_store_coverage_replace` (so user Cypher queries
  can traverse it without touching the real project's graph):

```c
typedef struct {
    const char *rel_path;
    const char *kind;       /* "parse_partial" | "read" | "extract" | "oversized" */
    const char *detail;     /* line ranges or reason */
} cbm_coverage_row_t;

int cbm_store_coverage_replace(cbm_store_t *s, const char *project,
                                const cbm_coverage_row_t *rows, int count);

void cbm_store_coverage_shadow_project(char *dst, size_t dstsz,
                                       const char *project);
```

  `cbm_store_coverage_shadow_project` is a **getter** that writes the
  derived shadow project name (`<project>::missed`) into `dst` — it
  does not create the shadow graph itself. The writer is
  `cbm_store_coverage_replace`, which materializes the `Project → Folder
  → File` graph under that derived name. Storage is the separate
  `index_coverage` table (coverage is metadata *about* the graph,
  never mixed into the graph itself). There are exactly four `kind`
  values: `"parse_partial"` (file was indexed but the parse tree had
  ERROR/MISSING regions), `"read"` / `"extract"` / `"oversized"` (file
  was not indexed at all; `detail` carries the reason).
- **Our applicability:** maps to a new Layer-1 table family in
  [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md)
  §5.7 (proposed addition). Shadow rows live in dedicated DBIs
  `coverage_units`, `coverage_files`, `coverage_regions` keyed by
  `(scope_id, miss_class, path_hash)` with `miss_class ∈ {"parse_partial",
  "read", "extract", "oversized"}`. The shadow data answers "what
  coverage does my stack have?" and "which units were truncated at
  indexing time?" — useful for the
  `RuntimeServices::AsyncIndexer` diagnostics
  ([`runtime-services-roadmap.md`](runtime-services-roadmap.md) §4) and
  for `ResourceIndexer` (see
  [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §"Stage B:
  Indexing").
- **Priority:** P0 — small implementation surface, big operational
  payoff (visibility into indexer). Single-week PR candidate.
- **Open questions:**
  - Do we replicate the `Project/Folder/File` triple exactly, or do we
    fold it into a flat `(scope_id, source_path, miss_class)` key?
    Flat keys are simpler and match our scope-aware key layout (ADR-012
    in [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md)).
  - Shadow rows are append-only at index time and pruned when the
    underlying resource is reindexed — confirm a compactable layout
    (delete-by-`(scope_id, path)` on reindex).

#### Pattern 4 — Atomic shared ID generator

- **What:** A single 64-bit monotonic ID generator that lives in
  shared-memory-compatible storage and is incremented under atomic
  semantics (`std::atomic<std::int64_t>` or `__sync_fetch_and_add`)
  rather than under a transaction lock. Allows multiple workers
  (indexing threads, parallel reindex batches) to allocate distinct IDs
  without serialising on a coordinator.
- **Their implementation:** `src/graph_buffer/graph_buffer.h` — creates
  a graph buffer whose internal ID counter is shared with another buffer
  so that parallel extractors mint distinct, unique IDs:

```c
cbm_gbuf_t *cbm_gbuf_new_shared_ids(const char *project, const char *root_path,
                                    _Atomic int64_t *id_source);
```

  Subsequent `cbm_gbuf_upsert_node` calls atomically advance the shared
  counter via `atomic_fetch_add` on `*id_source`. Passing `NULL` makes
  the function behave like the regular `cbm_gbuf_new` (per-source
  counter).
- **Our applicability:** extends the existing
  `mdbx_containers::SequenceTable` (see
  [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md)
  §3 "SequenceTable extension" — already planned at P3 priority). The
  shared-atomic backend is a different concrete implementation of the
  same allocator contract: it drops the per-thread transaction cost for
  ID minting and lets `ResourceIndexer` workers run free of the MDBX
  write lock during the ID allocation phase.
- **Priority:** P1 — wait until the MDBX TZ §3 P3 SequenceTable
  extension is actually scheduled; do not pre-empt.
- **Open questions:**
  - Atomic backend persistence: a process-local atomic survives only
    across calls within a single process; across crash the ID source
    must be rehydrated from disk. The current `SequenceTable` already
    handles persistence via MDBX; the atomic backend must do an
    "atomic-fill + MDBX-sync-when-crossing-threshold" dance to stay
    crash-safe.
  - Multi-process concern: single-process only in M0/M1 (per-process
    atomic, no cross-process claim). M3+ multi-process would require
    shared-memory IPC + a separate atomic allocator; out of scope here.

### Priority 2 — graph introspection and artifact portability

These two patterns address operational concerns (debugging, team-shared
databases). They are useful but not on the critical path for M1/M2
ship-it criteria.

#### Pattern 5 — Bounded BFS + schema introspection

- **What:** Two graph-store APIs that answer "what is reachable from this
  node within N edges and of these edge kinds?" (BFS) and "what node/edge
  labels and property keys exist in this graph?" (schema introspection).
  Both are read-only and stable enough to ship as part of the public
  graph store contract.
- **Their implementation:** `src/store/store.h`. Both APIs are
  read-only and operate on a returned `cbm_traverse_result_t` /
  `cbm_schema_info_t` rather than a callback:

```c
int cbm_store_bfs(cbm_store_t *s, int64_t start_id, const char *direction,
                  const char **edge_types, int edge_type_count,
                  int max_depth, int max_results, cbm_traverse_result_t *out);

int cbm_store_get_schema(cbm_store_t *s, const char *project, cbm_schema_info_t *out);
```

  `BFS` walks from `start_id`, returning visited nodes (with `hop`
  depth) and the traversed edges (with endpoints, types, and raw
  properties_json — useful for carrying CALLS argument expressions).
  `direction` is `"inbound"` / `"outbound"` / `"any"`; `edge_types` and
  `max_results` narrow the traversal; `max_depth` is an unbounded `int`
  (no documented `[1, 5]` cap — that range was a fabrication in an
  earlier draft). `get_schema` returns label/type counts, distinct
  property keys per label/type, observed relationship patterns, and
  sample names/qualified-names for the project.
- **Our applicability:** maps to ADR-006 in
  [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) (GraphRelations
  capability). Our `GraphStore` does not currently expose BFS or schema
  introspection; both are likely P1 additions to support retrieval-flow
  debugging and context-block assembly (a common ask: "give me everything
  connected to this entity within 2 hops, weighted by relevance").
- **Priority:** P1 — useful for retrieval-flow debugging and for the
  M2+ `IQueryTransformer`/`IRetrievalEvaluator` hooks (see
  [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) §"M2+
  Retrieval Hooks").
- **Open questions:**
  - Should BFS enumerate nodes or only edges? `codebase-memory-mcp`
    exposes a callback API — for our purposes a visitor pattern with
    early-stop is more useful than a bulk collection.
  - Schema introspection is operational metadata; do we gate it behind
    a "diagnostic mode" build flag (matching
    [`critical-defaults.md`](critical-defaults.md)) or expose it
    always?

#### Pattern 6 — Team-shared graph artifact

- **What:** A serialized snapshot of a `codebase-memory-mcp` graph
  stored as a single zstd-compressed `.codebase-memory/graph.db.zst`
  file that can be checked into a git repo under a `.gitattributes`
  merge=ours rule (so branches diverge their artifacts cleanly) and
  re-opened by any team member. Two quality levels exist: `FAST` uses
  `zstd -3`, `BEST` uses `zstd -9` plus an index-strip plus
  `VACUUM INTO`.
- **Their implementation:** `src/pipeline/artifact.h`:

```c
#define CBM_ARTIFACT_SCHEMA_VERSION 2

typedef enum cbm_artifact_quality_e {
    CBM_ARTIFACT_FAST = 0,   /* zstd -3 only */
    CBM_ARTIFACT_BEST = 1,   /* zstd -9 + index strip + VACUUM INTO */
} cbm_artifact_quality_t;
```

  Output path: `.codebase-memory/graph.db.zst`. Auto-created
  `.gitattributes` with `*.zst merge=ours` for branches that have not
  synced on the same artifact.
- **Our applicability:** aligns with §"Compaction/ExportJob" in
  [`compaction-roadmap.md`](compaction-roadmap.md) (the planned
  `SnapshotExportJob` for a single-team-stack handoff). Reuses the
  zstd compression contract already documented in
  [`optimization-roadmap.md`](optimization-roadmap.md) §"Compression
  Contracts" (§"Optional Zstd Adapter"). The
  `.gitattributes merge=ours` rule is novel and worth documenting
  alongside the job so teams can adopt it without re-discovering the
  pattern.
- **Priority:** P2 — wait for the SnapshotExportJob TZ to land first;
  this pattern is a "value-add" rather than a foundational capability.
- **Open questions:**
  - Schema version: `CBM_ARTIFACT_SCHEMA_VERSION = 2` is upstream's
    current value; ours will start at 1 and follow ADR versioning
    rules. Do NOT inherit upstream's `2`.
  - Where does the artifact live in our MDBX-stack? Our stack already
    serialises DBIs as part of the env on disk; the artifact is an
    *export* (offline) bundle, not a primary storage path. Reuse the
    same `zstd` codec and document explicitly that the bundle is
    read-only at the team-shared layer.

### Priority 3 — exploration-stage, watch-list

These patterns are interesting but have low expected leverage for our
M1/M2 roadmap. Watching them is enough; we will not plan work in this
cycle.

#### Pattern 7 — Cypher read subset

- **What:** A 162 KB pure-C implementation of a Cypher read-only
  query language. Supports `MATCH`, `OPTIONAL MATCH`, `WHERE`, `WITH`,
  `RETURN`, `ORDER BY`, `SKIP`, `LIMIT`, `UNION`, `UNWIND`, `CASE`,
  variable-length paths `[*1..3]`, `EXISTS` predicates for dead-code
  detection, and `COALESCE`. Unsupported features return explicit error
  messages ("unsupported aggregate …", etc.) rather than silently
  misbehaving.
- **Their implementation:** `src/cypher/cypher.h` with the full grammar
  + parser + evaluator in pure C. The "unsupported …" error catalogue is
  documented in the per-construct header.
- **Our applicability:** optional, M2+ thinking. Our
  `IRetrievalEngine` already composes retrievers via RRF (see
  [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) §"M2+
  Retrieval Hooks"); a graph-traversal query language on top would be
  powerful for knowledge-base-heavy profiles. Mark as an
  optional second-stage retrieval layer; the implementing language
  does NOT need to be Cypher (a custom declarative IR would do), but
  the explicit error-message contract is worth borrowing verbatim.
- **Priority:** P2 — optional.
- **Open questions:**
  - Do we adopt the Cypher dialect verbatim or design our own IR
    designed for our Envelope + Components shape? The latter is
    probably less work overall, since Cypher's match-against-property-
    on-edge style assumes a property graph that we may not want to
    normalise to.
  - What is the minimum subset we would expose? `MATCH` + variable-
    length paths + `WHERE` + ordered `RETURN` covers ~80% of read-
    only retrieval queries in their evaluator.

#### Pattern 8 — Aho-Corasick over LZ4

- **What:** Multi-pattern matching (Aho-Corasick automaton) that
  scans LZ4-compressed text directly, emitting bitmask hits into
  the original uncompressed offsets. Lets
  `codebase-memory-mcp` do token-level forbidden-pattern detection
  without ever decompressing the corpus.
- **Their implementation:** `internal/cbm/ac.h` — opaque automaton
  type `CBMAutomaton`, `CBMLz4Match` for batch output, `CBMLz4Entry`
  for batch input; `cbm_ac_scan_lz4_bitmask` returns the bitmask
  directly (no out-param), and the batch variant takes an array of
  pre-built LZ4 entries:

```c
typedef struct CBMAutomaton CBMAutomaton;

typedef struct { int file_index; uint64_t bitmask; } CBMLz4Match;

typedef struct {
    const char *data;
    int compressed_len;
    int original_len;
} CBMLz4Entry;

uint64_t cbm_ac_scan_lz4_bitmask(const CBMAutomaton *ac, const char *compressed,
                                 int compressed_len, int original_len);

int cbm_ac_scan_lz4_batch(const CBMAutomaton *ac, const CBMLz4Entry *entries,
                          int num_entries, CBMLz4Match *out_matches, int max_matches);
```

  The automaton is built externally (via `cbm_ac_build`); the scan
  operates directly on compressed LZ4 blocks. Useful for very large
  logs/codestores where decompression for every query is too
  expensive.
- **Our applicability:** niche. Relevant only if we add a "search over
  compressed text without decompressing" capability to the lexical or
  document store. Our lexical index is built at write time over the
  uncompressed text (per [`lexical-search-roadmap.md`](lexical-search-roadmap.md)
  and
  [`optimization-roadmap.md`](optimization-roadmap.md) §"Compressed Text
  Storage"), and we never scan the compressed blob at query time.
  Watch only.
- **Priority:** P3 — research only.

#### Pattern 9 — Adaptive-poll background watcher

- **What:** A background watcher that polls the indexed root path and
  classifies observed errors. Importantly, it exposes a single pure,
  public, unit-testable classifier function —
  `cbm_watcher_root_missing_errno(int err)` — that asks "given this
  errno from a stat/open syscall, is the root gone or is this a
  transient EACCES/EIO?". The watcher loop is the easy part; the
  reusable part is the classifier.
- **Their implementation:** `src/watcher/watcher.h`. The classifier is
  a small predicate that returns `true` ONLY for permanent-gone
  conditions (ENOENT, ENOTDIR); any other failure (EACCES, EIO,
  transient mounts, macOS TCC revocation) must NOT count as gone —
  per the source comment, "the cached DB holds user-authored data and
  is unrecoverable once pruned":

```c
bool cbm_watcher_root_missing_errno(int err);
```

  Note: there is no three-state enum upstream — the earlier draft's
  `cbm_root_status_e {PRESENT, GONE, TRANSIENT_ERR}` block was a
  fabrication. The function is the entire exposed classifier
  surface; everything else (the watcher loop, debouncing,
  inotify/FSEvents) lives outside the header as project-specific
  glue.

- **Our applicability:** aligns with
  [`runtime-services-roadmap.md`](runtime-services-roadmap.md) §4
  (AsyncIndexer) for the resource-watcher portion. Our `AsyncIndexer`
  reads resource manifests (see also `resource-reindexing.md`) and
  needs an errno classifier for its poll loop. The classifier is
  small, pure, and unit-testable (exposed by upstream specifically
  for direct unit testing with injected `errno` values) — adopt
  without ceremony. The rest of the watcher (file-system
  notifications, debouncing) is more project-specific.
- **Priority:** P2 — straightforward import.
- **Open questions:**
  - The classifier is platform-specific (POSIX `errno` values);
    Windows equivalents (`ERROR_FILE_NOT_FOUND`,
    `ERROR_PATH_NOT_FOUND`) need to map to the same permanent-gone
    predicate. We have a Windows-supported baseline (per
    [`build-and-test.md`](build-and-test.md)), so the mapping must
    be cross-platform and must preserve the "do not count
    revocable failures" rule.
  - The watcher loop in `codebase-memory-mcp` ties to inotify/FSEvents;
    `AsyncIndexer` currently uses timer-driven polling
    (per [`runtime-services-roadmap.md`](runtime-services-roadmap.md) §4).
    Keep timer-driven; do not pick up inotify in this iteration.

## 3. Cross-cutting concerns

### 3.1. License compliance

`codebase-memory-mcp` is MIT-licensed (Copyright (c) Deus Data contributors).
Borrowing constants (e.g. `K = 64`, `b = 32 / r = 2`), algorithm
descriptions (Jaccard LSH banding, RotSQ inner-product estimator
form), and high-level architectural ideas (coverage shadow graph, shared
atomic ID generator) is allowed and does not require attribution per
se. Borrowing source code — verbatim or with light renaming — is not:
source code is copyrightable regardless of license permissiveness, and
copying it makes our project carry upstream maintainer decisions.

The implementation rule is therefore:

| Level | Examples from upstream | Action in `agent-memory-cpp` |
|---|---|---|
| Constants | `CBM_MINHASH_K = 64`, `CBM_LSH_BANDS = 32`, `CBM_RSQ_BITS = 4` | Re-derive our own values, cite provenance in the docstring. |
| Algorithms | LSH banding, RotSQ inner-product estimator, BFS bounded by depth | Re-implement in our own code, document the reference. |
| Source code | A block of `.c` from `src/simhash/minhash.h` | Do not copy. Re-derive independently, or skip. |
| API names | `cbm_store_coverage_shadow_project` | Rename to fit our naming. |
| API signatures | `cbm_store_bfs`, `cbm_watcher_root_missing_errno`, `cbm_gbuf_new_shared_ids` | Re-derive our own signatures; do not invent plausible-but-fake upstream-shaped APIs. |

### 3.2. Constant provenance

Every constant introduced from `codebase-memory-mcp` should be linked
back to the upstream origin in the docstring/comment:

```text
// Adopted from codebase-memory-mcp src/simhash/minhash.h (MIT).
// Rationale: their tuned (bands=32, rows=2) gives Jaccard threshold 0.177
// at a manageable bucket cardinality for corpus sizes we expect.
constexpr std::uint32_t kLshBands = 32;
constexpr std::uint32_t kLshRows  = 2;
```

Adopt the per-constant provenance comment style in either the header
where the constant lives, or the `code-intelligence-roadmap.md` table
above (this guide serves as the prose side of the same audit).

LSH banding (Pattern 1) is the classic Leskovec/Rajaraman/Ulldevoll
b-and-r threshold — generally unencumbered for academic/OSS use. We
re-derive constants from this section and do not inherit
`codebase-memory-mcp`'s specific tunings verbatim.

### 3.3. Integration with existing roadmap

The patterns above interact with existing PR-track work in
`agent-memory-cpp`:

| Pattern | Existing track | Relationship |
|---|---|---|
| 1. MinHash | [`optimization-roadmap.md`](optimization-roadmap.md) §"Binary Bucket Index Tasks" (PR #29 binary signatures) | Augments — separate metric (Jaccard vs Hamming). |
| 2. RotSQ    | [`optimization-roadmap.md`](optimization-roadmap.md) §"Future Encodings" (Matryoshka, PQ codecs) | Sibling codec — adds to `IEmbeddingCodec` registry. |
| 3. Coverage shadow | [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md) §5.5 (memory-stack layer) | New Layer-1 table family (proposed §5.7). |
| 4. Atomic shared ID | [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md) §3 (SequenceTable extension, P3) | Bumps P3 to P1 if MDBX TZ §3 lands. |
| 5. Bounded BFS | [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) ADR-006 (GraphRelations) | Direct extension to `GraphStore`. |
| 6. Team-shared artifact | [`compaction-roadmap.md`](compaction-roadmap.md) ("SnapshotExportJob") | Value-add on top of zstd compression already in [`optimization-roadmap.md`](optimization-roadmap.md) §"Optional Zstd Adapter". |
| 7. Cypher | [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) §"M2+ Retrieval Hooks" | Optional second-stage retrieval. |
| 8. AC over LZ4 | none | Watch only. |
| 9. Adaptive-poll watcher | [`runtime-services-roadmap.md`](runtime-services-roadmap.md) §4 AsyncIndexer | Import the classifier, not the loop. |

### 3.4. Build dependencies

Each pattern introduces (or assumes) a small set of build dependencies:

- **Pattern 1 (MinHash + LSH):** SHA-1 or a 64-bit hash (e.g. xxHash)
  for the per-shingle hash; no other deps. Our current project already
  pulls in `xxhash` candidates through
  [`dependencies.md`](dependencies.md) for general use; reuse.
- **Pattern 2 (RotSQ):** deterministic code expansion needs a fixed
  rotation matrix (encoder-side constant) and a small SIMD-friendly
  inner-product routine; the latter overlaps with
  [`optimization-roadmap.md`](optimization-roadmap.md) §"Eigen и SIMD
  стратегия" (popcount64 wrapper + AVX2/AVX-512 dispatch). No new
  external deps.
- **Pattern 3 (Coverage shadow):** nothing new — uses MDBX via the
  existing TZ.
- **Pattern 4 (Atomic shared ID):** C++17 `<atomic>` (or GCC builtin
  `__atomic_fetch_add` for the C-style surface). No new deps.
- **Pattern 5 (BFS + schema introspection):** nothing new; reads from
  GraphStore (ADR-006).
- **Pattern 6 (Team-shared artifact):** `zstd` library (already an
  optional dep per
  [`optimization-roadmap.md`](optimization-roadmap.md) §"Optional Zstd
  Adapter").
- **Pattern 7 (Cypher read subset):** a parser (custom hand-written or
  parser-combinator library); we have no current dependency on a
  parser library. Re-implement with a hand-written recursive-descent
  parser; reject any external grammar library at this scope.
- **Pattern 8 (AC over LZ4):** `lz4` library + an Aho-Corasick
  construction. No current `lz4` dependency; would add one — which is
  why Pattern 8 is P3.
- **Pattern 9 (Adaptive-poll watcher):** nothing new; cross-platform
  errno mapping in `<errno.h>` / `<errno>` only.

## 4. Implementation order

Ordered for review-board submission. Status: `planned` = not yet on a
PR; `proposed` = on a candidate-PR list; `blocked` = depends on
another track that has not started.

| # | Pattern | Target file / section | Complexity | Depends on | Status |
|---|---|---|---|---|---|
| 1 | Pattern 3 — Coverage shadow | new `src/agent_memory/infrastructure/mdbx/CoverageStore.{hpp,cpp}`, [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md) §5.7 | small (1 PR) | MDBX TZ §3 P0 deliverable landed | planned |
| 2 | Pattern 5 — Bounded BFS + schema introspection | extension to `src/agent_memory/knowledge_base/GraphStore.{hpp,cpp}`, [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) ADR-006 | small (1 PR) | MDBX TZ §5.5 GraphStore shim landed | planned |
| 3 | Pattern 9 — Adaptive-poll watcher (errno classifier only) | extension to `src/agent_memory/runtime/AsyncIndexer.{hpp,cpp}`, new `src/agent_memory/runtime/RootStatusClassifier.{hpp,cpp}`, [`runtime-services-roadmap.md`](runtime-services-roadmap.md) §4 | small (1 PR) | none | planned |
| 4 | Pattern 6 — Team-shared artifact | post-SnapshotExportJob addition; bundle reader in `src/agent_memory/infrastructure/artifacts/`, [`compaction-roadmap.md`](compaction-roadmap.md) §"SnapshotExportJob" | medium (1-2 PRs) | SnapshotExportJob TZ landed, zstd adapter landed | blocked |
| 5 | Pattern 1 — MinHash + LSH | `src/agent_memory/lexical/MinhashLshIndex.{hpp,cpp}`, secondary stage after Hamming bucket lookup, [`optimization-roadmap.md`](optimization-roadmap.md) §"Binary Bucket Index Tasks" | medium (1-2 PRs) | PR #29 binary signatures shipped | proposed |
| 6 | Pattern 4 — Atomic shared ID | extension to `external/mdbx-containers/include/mdbx_containers/SequenceTable.hpp`, [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md) §3 (P3 → P1) | small (1 PR) | MDBX TZ §3 P3 scheduled | blocked |
| 7 | Pattern 2 — RotSQ codec | `src/agent_memory/embedding/RotsqCodec.{hpp,cpp}`, [`optimization-roadmap.md`](optimization-roadmap.md) §"Future Encodings" | medium (1-2 PRs) | `IEmbeddingCodec` interface + per-bit Recall@10 baseline landed | proposed |
| 8 | Pattern 7 — Cypher read subset | `src/agent_memory/cypher/{parser,evaluator}.{hpp,cpp}`, [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) §"Retrieval Composition" | large (≥2 PRs) | `GraphStore` introspection (Pattern 5) | blocked |
| 9 | Pattern 8 — AC over LZ4 | speculative; no concrete target | large | lz4 dependency added + use case validated | blocked |

Small = 1 PR, ≤300 LOC, ≤2 files. Medium = 1-2 PRs, ≤1 500 LOC, ≤5
files. Large = ≥2 PRs, multiple subsystems.

## 5. Patterns explicitly NOT adopted

Be honest about the patterns that do not fit us:

- **Pattern 7 (full Cypher evaluator).** Adopting the full grammar is
  the wrong scope for our codebase — we are an embedded memory
  engine, not a graph query platform. We may add a small read-only
  filter language (Pattern 7 redeclared as a tiny custom IR), but we
  will not ship a Cypher-conformant parser.
- **Pattern 8 (Aho-Corasick over LZ4).** Requires an `lz4` dependency
  for marginal benefit (we already decompress at write time and never
  scan compressed blobs). Watch only.
- **Pattern 4 (multi-process support).** The shared-atomic ID
  generator is single-process only; multi-process is explicitly
  out-of-scope per
  [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md)
  §1 (non-goal). No change to that boundary.
- **`codebase-memory-mcp` itself as a comparable system.** The two
  systems solve different problems at different scopes. They are
  comparable at the level of engineering patterns but not as
  benchmarks — there is no `vs` benchmark to plan in
  [`related-projects.md`](related-projects.md) §5.

## 6. References

- Upstream: <https://github.com/DeusData/codebase-memory-mcp>.
- License: MIT.
- Hash-function candidate: <https://github.com/Cyan4973/xxHash> (BSD-2,
  used optionally per [`dependencies.md`](dependencies.md)).
- Cross-links to our roadmap:
  - [`related-projects.md`](related-projects.md) — §6 entry added in
    this cycle, tagged "Research source / pattern donor".
  - [`optimization-roadmap.md`](optimization-roadmap.md) —
    Patterns 1 (MinHash) + 2 (RotSQ).
  - [`mdbx-containers-extension-tz.md`](mdbx-containers-extension-tz.md)
    — Patterns 3 (coverage shadow) + 4 (atomic shared ID) + 6
    (team-shared artifact).
  - [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) — ADR-006
    (Pattern 5: Bounded BFS + schema introspection).
  - [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) —
    Optional second-stage retrieval layer (Pattern 7: Cypher read
    subset).
  - [`compaction-roadmap.md`](compaction-roadmap.md) — SnapshotExportJob
    cross-link (Pattern 6: team-shared artifact).
  - [`runtime-services-roadmap.md`](runtime-services-roadmap.md) — §4
    AsyncIndexer (Pattern 9: adaptive-poll watcher errno classifier).
