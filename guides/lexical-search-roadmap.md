# Lexical Search Roadmap

## Purpose

This guide captures the planned keyword, BM25, and hybrid lexical retrieval
direction. Lexical search is a first-version retrieval capability, not a
late optimization, because it handles exact technical terms better than dense
embeddings: APIs, macros, file paths, class names, error messages, command
fragments, and rare identifiers.

The baseline should be small, embeddable, and dependency-free. Larger search
engines and Unicode libraries can be studied or added later as optional
adapters.

## Core Rules

- Keep source and chunk text as canonical UTF-8.
- Do not store source text as UTF-32 by default, even when compression is
  enabled.
- Use UTF-32/code point buffers only temporarily inside tokenizer backends that
  need them.
- Keep lexical contracts dependency-free.
- Use `ChunkId` as the primary retrieval key.
- Store `ResourceId` and generation with postings for targeted reindexing and
  stale-entry filtering.
- Use `ResourceManifest` records for lexical postings so one resource can be
  reindexed without rebuilding the whole corpus.
- Keep BM25 as the first ranked lexical baseline.
- Treat phrase, proximity, fuzzy, SPLADE, ColBERT-like, and graph retrieval as
  later layers unless a PR introduces focused contracts for them.
- Prefer RRF as the first hybrid fusion method because it does not require
  cross-backend score normalization.

## Lexical Data Model

The logical indexing pipeline is:

```text
UTF-8 source text
    -> tokenizer
    -> normalized tokens
    -> token ids
    -> per-chunk postings and stats
    -> BM25 / keyword search
```

Suggested value types:

```cpp
using TokenId = std::uint64_t;

struct LexicalPosting final {
    TokenId token_id = 0;
    ChunkId chunk_id;
    ResourceId resource_id;
    std::uint64_t generation = 0;
    std::uint32_t term_frequency = 0;
    std::vector<std::uint32_t> positions;
};

struct ChunkLexicalStats final {
    ChunkId chunk_id;
    ResourceId resource_id;
    std::uint64_t generation = 0;
    std::uint32_t token_count = 0;
    std::uint32_t unique_token_count = 0;
};

struct LexicalTokenStats final {
    TokenId token_id = 0;
    std::uint64_t document_frequency = 0;
    std::uint64_t collection_frequency = 0;
};

struct LexicalCollectionStats final {
    std::uint64_t chunk_count = 0;
    std::uint64_t total_token_count = 0;
};
```

BM25 only needs term frequency and chunk length. Positions are included in the
roadmap because phrase search, proximity search, snippets, and highlighting need
token order.

## Token Dictionary

Token strings should be normalized before they receive ids:

```text
lexical_token_by_text:
    normalized_token -> token_id

lexical_token_by_id:
    token_id -> normalized_token
```

The reverse dictionary is not required for scoring, but it is useful for
debugging, index dumps, explain output, and relevance diagnostics.

## Tokenization

The first tokenizer should be std-only and code-aware:

```text
ExactVectorIndex      -> exactvectorindex, exact, vector, index
AGENT_MEMORY_HAS_MDBX -> agent_memory_has_mdbx, agent, memory, has, mdbx
src/agent_memory/...  -> src, agent_memory, agent, memory, ...
error 11001           -> error, 11001
```

Near-term tokenizer contracts should model:

- normalized token text;
- token position;
- source byte range;
- token kind where useful, for example word, number, path, identifier, symbol.

Optional tokenizer backends can be introduced later:

- `simdutf` for fast UTF-8 validation and UTF-8/UTF-32 transcoding;
- ICU for normalization, Unicode case folding, and word boundary segmentation.

Libraries such as `utfcpp` and `tiny-utf8` are useful references, but the core
tokenizer contract should remain project-owned.

## UTF Storage Rule

Canonical persisted text is UTF-8:

```text
resource text -> UTF-8, optionally compressed later
chunk text    -> UTF-8, optionally compressed later
tokenization  -> temporary UTF-32/code points only when needed
index storage -> token ids, stats, postings, and positions
```

Zstd may compress UTF-32 zero bytes well, but decompression still produces a
larger working buffer and worse cache behavior. Compression should be benchmarked
on UTF-8 text, normalized text, tokens, and posting lists before any alternative
text storage format is used.

## BM25 Baseline

Use Okapi BM25 as the first ranked lexical scorer:

```text
score(chunk, query) =
sum over query terms:
    idf(term) * tf_saturation(term, chunk) * length_normalization(chunk)
```

Default tunables:

```text
k1 = 1.5
b  = 0.75
```

The index should expose these as options instead of hard-coding them into the
public contract.

## Search Contracts

Planned dependency-free contracts:

```cpp
struct LexicalQuery final {
    std::string text;
    std::size_t limit = 10;
    std::vector<MetadataFilter> metadata_filters;
};

struct LexicalSearchResult final {
    ChunkId chunk_id;
    double score = 0.0;
    Metadata metadata;
};

class ILexicalIndex {
public:
    virtual ~ILexicalIndex();

    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    virtual void upsert(
        const ResourceRevision& revision,
        const std::vector<DocumentChunk>& chunks
    ) = 0;

    [[nodiscard]] virtual std::vector<LexicalSearchResult> search(
        const LexicalQuery& query
    ) const = 0;

    [[nodiscard]] virtual bool erase_resource(
        const ResourceId& resource_id
    ) = 0;

    virtual void clear() = 0;
};
```

Exact method names can change during implementation. The important part is that
lexical indexing is resource-aware and chunk-addressed.

## MDBX Layout

The first MDBX lexical backend can use simple blobs:

```text
lexical_token_by_text:
    key   = normalized token
    value = token_id

lexical_token_by_id:
    key   = token_id
    value = normalized token

lexical_postings:
    key   = token_id
    value = posting list blob

lexical_chunk_stats:
    key   = chunk_id
    value = token_count, unique_token_count, resource_id, generation

lexical_token_stats:
    key   = token_id
    value = document_frequency, collection_frequency

lexical_collection_stats:
    key   = fixed stats key
    value = chunk_count, total_token_count
```

The first implementation may rewrite a full posting list on resource updates.
For larger mutable corpora, add segmented postings, tombstones, generation
filtering, and compaction.

## Targeted Reindexing

When a resource is replaced:

```text
1. Load the old resource manifest.
2. Remove or invalidate lexical postings referenced by the old manifest.
3. Tokenize the new chunks.
4. Upsert token dictionary entries.
5. Write postings and chunk stats.
6. Update token and collection stats.
7. Write new lexical manifest refs.
8. Commit through a backend transaction where available.
```

Lexical `DerivedRecordRef` entries can use:

```text
kind = LexicalPosting
key  = token_id or an encoded posting segment key
```

The manifest does not need stable offsets into compressed posting blobs. Removal
should filter by `chunk_id`, `resource_id`, and generation.

## Raw Resource Stores

The project should support raw source corpora as first-class inputs:

```text
.md
.txt
source code
structured records
later: PDF and office document adapters
```

Planned contract shape:

```cpp
class IResourceStore {
public:
    virtual ~IResourceStore();

    [[nodiscard]] virtual std::vector<ResourceDescriptor> list_resources() const = 0;

    [[nodiscard]] virtual std::optional<ResourceSnapshot> read_resource(
        const ResourceId& resource_id
    ) const = 0;
};
```

Initial adapters should focus on file-system resources:

```text
FileSystemResourceStore
    root_path
    include globs: *.md, *.txt, *.cpp, *.hpp
    exclude globs: .git, build, node_modules
```

The same indexing pipeline should support two source modes:

```text
raw-first mode:
    files are the source of truth; indexes are derived

memory-native mode:
    agent-created notes/facts live in storage; indexes are still derived
```

## Future Search Layers

### Boolean Search

Support `AND`, `OR`, and `NOT` as an exact filtering layer. This is useful for
technical queries, user constraints, and structured query builders.

### BM25F

BM25F should be the first BM25 variant because agent-memory resources naturally
have fields:

```text
title
path
markdown headings
body
code blocks
tags
symbol names
metadata
```

Field weights should be explicit index options.

### Phrase And Proximity Search

Use token positions to support exact phrases and near-term windows:

```text
"exact vector index"
exact NEAR/5 index
```

This should also support snippets and highlighting.

### Fuzzy Search

Add fuzzy search after the BM25 baseline. Initial candidates:

```text
Levenshtein distance
Damerau-Levenshtein distance
trigram similarity
BK-tree over token dictionary
```

Technical identifiers need careful thresholds because a one-character change can
mean a different symbol.

### N-Gram Search

N-gram or substring search is useful for identifiers, partial paths, and unknown
languages. It should be a separate index or optional mode because it increases
storage size and noise.

### Graph Retrieval

Graph retrieval is likely needed earlier than learned sparse or late-interaction
methods for agent assistants and expert-system style workflows. It should expand
from retrieved chunks/resources to related symbols, concepts, people, tasks, and
dependencies.

### SPLADE And Learned Sparse Retrieval

SPLADE-like learned sparse retrieval is future work. It needs a model, inference
adapter, vocabulary management, and sparse vector storage.

### ColBERT-Like Late Interaction

ColBERT-like retrieval is future research work. It stores token-level embeddings
and has much higher storage and indexing complexity than one embedding per
chunk.

## Hybrid Retrieval

The first hybrid pipeline should be:

```text
metadata filters
    -> BM25 / keyword search
    -> vector search
    -> optional graph expansion
    -> fusion
    -> optional rerank
```

Start with Reciprocal Rank Fusion:

```text
score = sum(1 / (k + rank_i))
```

RRF is robust because BM25 scores, vector similarities, graph weights, and future
reranker scores do not need to be normalized into one scale.

Other future fusion modes:

```text
weighted score
max score per source
learned reranker
planner-guided fusion
```

## Planner-Guided Retrieval

RLM-style recursive retrieval is not a first lexical-index task, but it is worth
tracking as a future context planning layer. The C++ version should not execute
arbitrary Python. It can instead run controlled retrieval functions over the
database:

```text
root planner:
    inspect metadata and corpus stats
    call lexical/vector/graph retrieval functions
    split large result sets
    request sub-summaries or sub-answers from an external model adapter
    aggregate final context or answer
```

This belongs in a later retrieval planning or context assembly layer, not in
`ILexicalIndex`.

## Reference Projects

- Xapian: mature C++ search engine with BM25 weighting. Useful as a reference or
  optional adapter, not a core dependency.
- PISA: performance-oriented C++ inverted index research engine. Useful for
  compression, WAND/BMW, and query processing ideas.
- simdutf: optional Unicode validation and transcoding backend.
- ICU: optional heavy Unicode normalization, case folding, and word boundary
  backend.
- utfcpp and tiny-utf8: useful UTF handling references.
- symgraph and OpenWiki: useful references for graph/document retrieval shapes.
- RLM and rlm-minimal: references for planner-guided, recursive retrieval.

## Recommended Implementation Order

1. Add this roadmap.
2. Add tokenizer value types and `ITokenizer`.
3. Add a std-only code-aware tokenizer.
4. Add lexical query, result, posting, and stats value types.
5. Add `ILexicalIndex`.
6. Add in-memory BM25 lexical index.
7. Integrate lexical indexing into resource reindexing.
8. Add hybrid retrieval with BM25 + vector through RRF.
9. Add file-system raw resource store for `.md` and `.txt`.
10. Add phrase/proximity over positions.
11. Add MDBX lexical index with simple posting-list blobs.
12. Add segmented postings, tombstones, and compaction.
13. Add BM25F field weighting.
14. Add fuzzy and n-gram search.
15. Add graph retrieval integration.
16. Add optional Unicode backends.
17. Add optional external search adapters if benchmarks justify them.
