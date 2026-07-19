# Binary flat Hamming search vs exact float vector search

## 2026-07-20 — PR #55 initial binary-search diagnostic

### What we checked

This experiment checks whether packed binary signatures can serve as a useful
candidate-search primitive compared with exact float-vector search.

The current implementation compares:

- `ExactVectorIndex` over normalized synthetic dense vectors;
- `RandomHyperplaneBinaryEncoder`;
- `FlatBinarySignatureIndex` using exact Hamming distance over packed
  signatures.

This is not a final retrieval-quality benchmark and it is not a BoW-specific
result. The quality proxy is preservation of exact-float nearest neighbours:
`ExactVectorIndex` top-k is treated as the oracle, and binary top-k is measured
against it.

### Why it matters

Binary signatures are attractive because they can reduce stored vector payload
and enable cheap candidate filtering. Before adding bucket or ANN-style indexes,
we need a flat oracle that answers two questions:

1. how much nearest-neighbour quality survives direct binary ranking;
2. how fast the exact Hamming primitive is before sub-linear indexing.

### Expected result

Before optimization, we expected:

- large payload reduction;
- weak direct top-10 quality without exact rerank;
- no architectural speedup from flat scan alone;
- possible constant-factor speedup after hardware-assisted Hamming distance.

### Setup

Local directional run:

- build: MinGW Release;
- benchmark mode: `synthetic_binary_flat_vs_float`;
- corpus: deterministic clustered synthetic dense vectors;
- query count: `200`;
- embedding dimension: `128`;
- top-k: `10`;
- seed: `42`.

Timing methodology:

- exact path times only `ExactVectorIndex::search()`;
- binary path separately times query signature encoding and
  `FlatBinarySignatureIndex::search()`;
- quality bookkeeping is outside both search timers.

### Results after optimized Hamming distance

Medium corpus: `5000` documents.

| Bits | Recall@10 vs exact float | Top-1 agreement | Binary search speedup | Binary total speedup incl. encode | Payload ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 0.0545 | 0.005 | 1.27x | 1.23x | 64x |
| 128 | 0.1040 | 0.040 | 1.16x | 1.10x | 32x |
| 256 | 0.1910 | 0.075 | 1.12x | 1.01x | 16x |
| 512 | 0.3070 | 0.195 | 1.10x | 0.90x | 8x |

Large corpus point: `20000` documents, `256` bits.

| Metric | Value |
| --- | ---: |
| Recall@10 vs exact float | 0.1555 |
| Top-1 agreement | 0.1000 |
| Exact search total | 1997.95 ms |
| Binary search speedup | 1.12x |
| Binary total speedup including encode | 1.09x |
| Payload ratio | 16x |

### Interpretation

Optimized Hamming distance provides the expected constant-factor win for flat
binary search, especially once the corpus is large enough to amortize query
encoding. The payload reduction is substantial.

Direct binary top-10 quality is weak, however. This supports treating flat
binary search as a candidate filter, not as the final ranking layer.

### Limitations

- Single local run; timings are directional, not statistically stable.
- Synthetic clustered dense vectors do not represent real embedding models.
- Direct binary top-k is compared with exact float top-k without over-fetch or
  exact reranking.
- Process RSS is a whole-process high-water mark, not per-index memory.

### Improvements already applied in PR #55

- Runtime-selected Hamming backend:
  - AVX2 SIMD nibble lookup on supported GCC/Clang x86/x64 builds;
  - hardware POPCNT on supported CPUs;
  - portable byte lookup-table fallback.
- Symmetric timing methodology.
- Process RSS reported separately from index payload estimates.

## 2026-07-20 — PR #55 flat top-k polishing

### What changed

After the optimized Hamming pass, both flat indexes still sorted every matched
record and then truncated to top-k. PR #55 was extended to use partial top-k
selection in both `ExactVectorIndex` and `FlatBinarySignatureIndex`:

- when `result_count <= limit`, sort the full result set;
- when `result_count > limit`, use `std::partial_sort()` for the top-k prefix
  and then resize.

This preserves the public ordering contract while avoiding full-corpus sorting
for small `limit` values.

### Result

Same local setup as above: MinGW Release, `200` queries, dimension `128`,
top-k `10`, seed `42`.

Medium corpus: `5000` documents.

| Bits | Recall@10 vs exact float | Top-1 agreement | Binary search speedup | Binary total speedup incl. encode | Payload ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 0.0545 | 0.005 | 2.36x | 2.15x | 64x |
| 128 | 0.1040 | 0.040 | 1.71x | 1.48x | 32x |
| 256 | 0.1910 | 0.075 | 1.87x | 1.42x | 16x |
| 512 | 0.3070 | 0.195 | 1.87x | 1.20x | 8x |

Large corpus point: `20000` documents, `256` bits.

| Metric | Value |
| --- | ---: |
| Recall@10 vs exact float | 0.1555 |
| Top-1 agreement | 0.1000 |
| Exact search total | 1366.83 ms |
| Binary search speedup | 2.62x |
| Binary total speedup including encode | 2.39x |
| Payload ratio | 16x |

### Interpretation

Flat binary search benefits from two independent constant-factor changes:
faster Hamming distance and avoiding full result sorting. Optimizing both the
binary and exact flat indexes keeps the comparison honest while making the
current baseline more useful.

Quality is still the limiting factor for direct binary top-10. The next
retrieval question should therefore focus on binary candidate over-fetch plus
exact reranking, not direct binary ranking as the final answer.

### What to check next

- Candidate over-fetch: binary top-100/top-1000 followed by exact float rerank.
- Candidate recall as a function of Hamming radius or over-fetch size.
- Bucket or multi-index hashing to avoid full-corpus scans.
- The same candidate-filter pipeline on real embedding vectors and real qrels.
