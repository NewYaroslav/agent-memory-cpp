# Optimization Roadmap

## Purpose

This guide captures follow-up optimization work that should be explored after
the current dependency-free storage, embedding, index, and retrieval contracts
are in place.

The roadmap is intentionally split into tasks. Do not pull optional
dependencies such as Eigen or Zstd into public contracts just because a later
optimization can benefit from them.

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
- Keep Eigen out of `Embedding.hpp`, `IEmbedder.hpp`, and other dependency-free
  contracts.
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

## Binary Signature Index Tasks

### Dependency-Free Signature Types

- Add `BinarySignature` with `std::vector<std::uint64_t> words` and
  `std::size_t bit_count`.
- Ensure unused bits in the last word are zeroed.
- Add `hamming_distance(lhs, rhs)` implemented through XOR and popcount.
- While the project remains C++17, use compiler intrinsics or a local fallback
  instead of requiring C++20 `std::popcount`.
- Add `BinarySignatureInfo` with encoder id, source model id, source dimension,
  signature bits, approximated metric, and seed.
- Add `IBinarySignatureEncoder` as a dependency-free contract.

### Baseline Encoder

- Implement random-hyperplane LSH first:

```text
bit_i = sign(dot(embedding, random_hyperplane_i))
```

- Make generation deterministic by seed, dimension, and signature bit count.
- Store encoder config with the index.
- Reject or rebuild indexes when model id, dimension, encoder id, seed, or
  signature bit count does not match.

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
- Prefer the two-stage layout first:

```text
binary_bucket_index:
    key   = short signature key
    value = compressed or uncompressed posting list

embedding_store:
    key   = chunk_id
    value = float32 vector blob or encoded vector blob

chunk_store:
    key   = chunk_id
    value = compressed chunk text and metadata
```

- Store bucket values as compact posting lists of `{chunk_id, full_signature}`.
- Keep one-stage bucket values with embedded float vectors as an experimental
  benchmark variant.
- Prefer sparse MDBX key-value lookup for 64-bit short keys.
- Treat dense direct-address directories as possible only for small key sizes
  and only after measuring memory cost.

### Query Pipeline

The target approximate pipeline is:

```text
query text
    -> query embedding
    -> full binary signature
    -> short key
    -> neighbor keys by short Hamming radius
    -> bucket lookups
    -> decode/decompress posting lists
    -> full-signature Hamming filter
    -> unique chunk ids
    -> batch fetch float embeddings
    -> exact rerank
    -> fetch chunk/resource text
```

Float rerank remains mandatory for quality-sensitive retrieval.

## Benchmark Tasks

### Exact Baseline

- Keep an exact brute-force float baseline for every approximate experiment.
- Use the same similarity metric as the approximate pipeline.
- If vectors are normalized, benchmark cosine optimized as dot product.

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
compression: none, zstd level 1, zstd level 3, zstd level 6, zstd dictionary
layout: two-stage bucket vs one-stage bucket
datasets: random, clustered, real text chunks with real embeddings
```

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
```

When buckets are too hot, test mixed short keys, another seed, another encoder,
or a larger short-key bit count.

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

1. Dependency-free vector math helpers.
2. Compression contracts with `CompressionCodec::None`.
3. Optional Zstd adapter.
4. Compressed document/chunk text storage.
5. Compression benchmarks for markdown, code, chat, and bucket blobs.
6. Dependency-free binary signature value types and Hamming distance.
7. Random-hyperplane binary encoder.
8. Short-key generation and Hamming neighbor masks.
9. In-memory binary bucket index prototype.
10. Recall/latency benchmark against exact float search.
11. MDBX-backed two-stage bucket storage.
12. Bucket compression benchmarks.
13. Optional Eigen rerank/scoring adapter.
14. One-stage bucket benchmark variant.
15. Haar-like experimental encoder.

## Non-Goals For The First Optimization Pass

- Do not replace `std::vector<float>` in `Embedding`.
- Do not make Eigen a required public dependency.
- Do not compress hot vector-search data with a generic compressor without a
  benchmark.
- Do not treat binary Hamming results as final retrieval ranking.
- Do not add product quantization before simpler float16/int8/binary
  experiments and exact baselines exist.
