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

## 2026-07-20 — PR #56 candidate over-fetch with exact rerank

### What we checked

PR #56 extends the same benchmark mode with candidate over-fetch followed by
exact float reranking. The direct binary top-k result remains in the report, but
the new `rerank` rows answer a more realistic question:

1. retrieve `candidate_limit` chunks by binary Hamming distance;
2. rerank those candidates by exact float dot product over the original
   normalized dense vectors;
3. measure how much of the exact-float top-10 is recovered.

### Expected result

We expected direct binary top-10 to remain weak, but over-fetch plus exact rerank
to recover quality quickly while preserving part of the binary speed advantage.

### Setup

Local directional run:

- build: MinGW Release;
- corpus: deterministic clustered synthetic dense vectors;
- documents: `20000`;
- queries: `200`;
- embedding dimension: `128`;
- binary signature width: `256`;
- final top-k: `10`;
- candidate limits: `10`, `50`, `100`, `500`, `1000`, `2000`;
- seed: `42`.

### Results

| Candidate limit | Exact top-10 candidate coverage | Reranked Recall@10 vs exact | Reranked top-1 agreement | Total speedup incl. encode/search/rerank |
| ---: | ---: | ---: | ---: | ---: |
| 10 | 0.1555 | 0.1555 | 0.275 | 1.99x |
| 50 | 0.3520 | 0.3520 | 0.570 | 2.11x |
| 100 | 0.4745 | 0.4745 | 0.705 | 2.12x |
| 500 | 0.7900 | 0.7900 | 0.935 | 1.76x |
| 1000 | 0.8880 | 0.8880 | 0.950 | 1.46x |
| 2000 | 0.9520 | 0.9520 | 0.980 | 1.13x |

### Interpretation

The candidate-filter framing is much stronger than direct binary ranking. With
`1000` candidates, the pipeline recovers about `0.888` of exact top-10 while
remaining faster than exact flat search in this local run. With `2000`
candidates, recall reaches about `0.952`, but most of the speed advantage is
spent on candidate search and rerank.

For this synthetic setup, candidate limits in the `500` to `1000` range look
like the interesting next tuning band. The equal candidate and reranked
Recall@10 values are expected: any exact top-10 document present in the
candidate set is ranked above non-top-10 documents by the exact reranker.

### Practical applicability

The experiment supports a narrow but useful system shape:

```text
compact binary candidate index in RAM
+ original dense vectors available for rerank
+ exact rerank over a bounded candidate set
```

It does not support replacing float-vector search with direct binary top-k.
Direct binary top-10 is too weak in this setup.

Possible operating points:

| Candidate limit | Possible use | Why |
| ---: | --- | --- |
| 50 | Very fast coarse routing or duplicate-probe stage | About `2x` speedup, but only `0.352` top-10 coverage. Useful when missing many secondary neighbours is acceptable. |
| 100 | Fast top-1 oriented candidate generation | Top-1 agreement rises to `0.705` while speed remains about `2x`; still too low for high-recall context assembly. |
| 500 | Top-1 heavy memory lookup, routing, or small-context preselection | Top-1 agreement is `0.935` and top-10 coverage is `0.790`; good when the first result matters more than complete top-10 recall. |
| 1000 | Balanced candidate generation for semantic retrieval experiments | Top-10 coverage reaches `0.888` and speed remains `1.46x`; this is the most interesting next tuning band. |
| 2000 | Near-exact quality reference point | Top-10 coverage reaches `0.952`, but speedup drops to `1.13x`; useful as a quality curve point, not an obvious production default. |

The memory story is also nuanced. A reranked system still needs original dense
vectors somewhere. The binary index can be compact and hot in RAM, while dense
vectors may live in a slower store and be loaded only for selected candidates.
Therefore the `16x` payload ratio applies to the candidate index payload, not
to the complete two-stage retrieval system.

The practical promise is lower hot-index memory and cheaper candidate
generation, not deletion of the dense vector representation.

### Limitations

- Single local run; timings remain directional.
- The vectors are synthetic and clustered, not real embedding model outputs.
- The candidate generator is still flat Hamming scan, not a bucket or ANN index.
- The experiment uses symmetric document/query vectors; asymmetric projection
  policies are not covered.
- One seed and one sequential candidate-limit order are not enough for a stable
  production decision.

### What to check next

- Candidate over-fetch on real embedding vectors and qrels.
- Hamming-radius candidate selection, not only top-N candidate selection.
- Bucket or multi-index hashing to reduce candidate scan work before rerank.
- Bit-count and seed sensitivity for the `500` to `1000` candidate band.
- Multi-seed and repeated timing grid with randomized candidate-limit order,
  warm-up, median, and p95.
- Report `nDCG@10`, dense bytes read for rerank, and candidate storage I/O in
  addition to Recall@10 and top-1 agreement.

## 2026-07-20 — PR #56 compact bit-width grid

### What we checked

The first rerank result used only `256`-bit signatures. This follow-up grid
checks whether the promising `500` to `1000` candidate band is specific to
`256` bits or whether other signature widths offer a better quality/speed
tradeoff.

### Setup

Local directional run:

- build: MinGW Release;
- documents: `20000`;
- queries: `200`;
- embedding dimension: `128`;
- bit counts: `128`, `256`, `512`, `1024`;
- final top-k: `10`;
- candidate limits: `100`, `500`, `1000`, `2000`;
- seed: `42`.
- exact baseline: generated and measured once, then reused for every bit width.

### Results

Each cell shows:

```text
exact top-10 candidate coverage / top-1 agreement / total speedup
```

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.254 / 0.330 / 1.97x | 0.5195 / 0.670 / 1.64x | 0.674 / 0.795 / 1.33x | 0.8235 / 0.910 / 1.09x |
| 256 | 0.4745 / 0.705 / 1.95x | 0.790 / 0.935 / 1.37x | 0.888 / 0.950 / 1.18x | 0.952 / 0.980 / 1.03x |
| 512 | 0.7155 / 0.925 / 1.81x | 0.935 / 0.985 / 1.46x | 0.9785 / 1.000 / 1.20x | 0.9975 / 1.000 / 0.94x |
| 1024 | 0.8905 / 0.990 / 1.42x | 0.996 / 1.000 / 1.16x | 0.999 / 1.000 / 1.04x | 0.9995 / 1.000 / 0.89x |

Direct binary top-10 also improves with wider signatures, but remains weaker
than reranked candidate search:

| Bits | Direct Recall@10 vs exact | Direct top-1 agreement |
| ---: | ---: | ---: |
| 128 | 0.063 | 0.030 |
| 256 | 0.1555 | 0.100 |
| 512 | 0.2715 | 0.165 |
| 1024 | 0.410 | 0.285 |

### Interpretation

The `256`-bit `1000`-candidate point is not uniquely best. Wider signatures can
move the same quality target to a smaller candidate set:

- `256 bits × 1000 candidates`: `0.888` coverage, `0.950` top-1, `1.18x`
  speedup.
- `512 bits × 500 candidates`: `0.935` coverage, `0.985` top-1, `1.46x`
  speedup.
- `1024 bits × 100 candidates`: `0.8905` coverage, `0.990` top-1, `1.42x`
  speedup.
- `1024 bits × 500 candidates`: `0.996` coverage, exact top-1, `1.16x`
  speedup.

For this synthetic setup, `512` and `1024` bits look more interesting than the
original `256`-bit point. The extra Hamming and encoding cost can be offset by
needing fewer rerank candidates.

The most useful candidate bands for a repeated benchmark are:

- speed-oriented: `1024 bits × 100 candidates`;
- balanced: `512 bits × 500 candidates`;
- quality-oriented: `1024 bits × 500 candidates`.

### Limitations

This is still a single-seed, single-run, synthetic-vector grid. The grid now
uses one common exact baseline for the whole seed, but timing differences can
still reflect run order, cache state, allocator warm-up, and CPU frequency
changes. Small speed differences between bit widths should not be interpreted as
winners without repeated timings. The numbers are useful for selecting the next
experiment band, not for choosing production defaults.

### What to check next

- Repeat this grid with the new multi-seed harness and randomized candidate-limit
  order.
- Add `nDCG@10` and dense-vector bytes read for rerank.
- Repeat the best bands on real embedding vectors and qrels.
- Compare top-N candidate selection with Hamming-radius candidate selection.

## 2026-07-20 — PR #57 stat-grid harness

### What we changed

PR #56 answered the first bit-width question, but still used a lightweight
single-seed/single-run grid. PR #57 turns the grid into a more useful
experiment harness:

- `data_seeds` and `encoder_seeds` are separate;
- each data seed builds one common exact oracle reused across encoder seeds,
  bit widths, and repeats;
- `repeat_count` records repeated binary timings over the same oracle without
  treating deterministic repeats as new quality observations;
- `exact_timing_repeat_count` measures the shared exact baseline repeatedly,
  and speedups use its median query time as their denominator;
- optional `randomize_execution_order` shuffles bit/repeat tasks and candidate
  limit execution while keeping bit, repeat, and candidate JSON output sorted;
- each bit-width report includes repeat-level raw reports and summary statistics;
- the top-level report includes `aggregate_summary` with separate quality and
  timing sample counts;
- summary `median` is the conventional median, while `p95_nearest_rank` names
  the percentile method explicitly.

### Hypothesis

The earlier candidate bands should remain directionally stable when projection
seed and repeated timings are separated from data generation. The exact winner
should still not be treated as production-stable because the run is local,
synthetic, and small.

### Setup

Local directional run:

- build: MinGW Release;
- documents: `10000`;
- queries: `100`;
- embedding dimension: `128`;
- bit counts: `128`, `256`, `512`, `1024`;
- final top-k: `10`;
- candidate limits: `100`, `500`, `1000`, `2000`;
- data seeds: `42`;
- encoder seeds: `1001`, `1002`;
- repeats per data/encoder/bit: `2`;
- exact timing repeats per data seed: `5`;
- randomized execution order: enabled.

### Initial aggregate results before explicit float SIMD

Each cell shows:

```text
coverage mean / top-1 mean / median total speedup
```

Each quality cell has `2` samples: one per encoder seed. Each timing cell has
`4` measurements: two encoder seeds times two repeats. The common exact
baseline has `5` timing measurements:

```text
362.368, 421.191, 360.403, 342.740, 335.916 ms
```

Its conventional median, `360.403 ms`, is the denominator for every speedup in
the table. This run is retained as the historical pre-SIMD result; the
controlled rerun below supersedes its latency conclusions.

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.3200 / 0.395 / 2.53x | 0.6260 / 0.685 / 1.88x | 0.7795 / 0.850 / 1.47x | 0.9035 / 0.955 / 0.97x |
| 256 | 0.5065 / 0.665 / 1.87x | 0.8260 / 0.890 / 1.48x | 0.9265 / 0.955 / 1.22x | 0.9780 / 0.990 / 0.93x |
| 512 | 0.7435 / 0.895 / 2.03x | 0.9595 / 1.000 / 1.58x | 0.9895 / 1.000 / 1.25x | 0.9990 / 1.000 / 0.95x |
| 1024 | 0.9010 / 1.000 / 1.34x | 0.9960 / 1.000 / 1.05x | 1.0000 / 1.000 / 0.96x | 1.0000 / 1.000 / 0.71x |

### Interpretation

The broad shape from PR #56 remains:

- direct binary top-k is still not the main retrieval mode;
- binary candidate over-fetch plus exact rerank is the useful mode;
- wider signatures recover quality with smaller candidate sets;
- large candidate sets eventually lose the speed advantage to rerank cost.

The practical candidate bands are now slightly clearer:

- `1024 bits × 100 candidates`: strong top-1/routing candidate with high
  coverage and a `1.34x` median speedup;
- `512 bits × 500 candidates`: the strongest balanced candidate in this run,
  with `0.9595` coverage, perfect top-1 agreement, and a `1.58x` median speedup;
- `1024 bits × 500 candidates`: quality-oriented candidate with near-exact
  coverage, but its `1.05x` median speedup is too small to treat as robust;
- `128 bits × 100 candidates` is fast at `2.53x`, but its `0.3200` coverage is
  only suitable for coarse routing where that loss is acceptable.

### Limitations

This is still not a production benchmark:

- only one data seed was used;
- only two encoder seeds, two binary timing repeats, and five exact timing
  repeats were used;
- timings are local in-process measurements;
- four binary timing measurements are not enough for stable tail statistics or
  confidence intervals; `p95_nearest_rank` is diagnostic only at this scale;
- candidate order is randomized, but the machine still has cache, allocator, and
  CPU frequency effects;
- the corpus is synthetic clustered dense vectors, not real embeddings with qrels.

### SIMD-controlled rerun

The initial exact baseline used scalar source loops and recomputed cosine norms
for every query/document pair. That made the binary speedup depend on an
unnecessarily weak float implementation. In the same PR we therefore added:

- runtime-selected AVX2 and SSE2 vector arithmetic with a scalar fallback;
- cached inverse document norms and one query norm calculation per search;
- lightweight exact and binary top-k candidates, so metadata is copied only
  after partial selection;
- the same SIMD dot-product backend for exact candidate reranking;
- the selected exact backend in the JSON report.

Eigen was not added. The dependency would not improve these two compact kernels
over direct intrinsics, and the runtime-dispatched scalar fallback remains
dependency-free.

The clean rerun used the same data, seeds, bit widths, candidate limits, and
sample counts. The selected backend was `avx2`. Exact timing samples were:

```text
56.336, 56.489, 55.995, 57.868, 57.668 ms
```

The median exact time fell from `360.403 ms` to `56.489 ms`, a `6.38x`
improvement. Quality stayed unchanged, while the latency result changed
materially:

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.3200 / 0.395 / 0.80x | 0.6260 / 0.685 / 0.55x | 0.7795 / 0.850 / 0.40x | 0.9035 / 0.955 / 0.26x |
| 256 | 0.5065 / 0.665 / 0.79x | 0.8260 / 0.890 / 0.54x | 0.9265 / 0.955 / 0.39x | 0.9780 / 0.990 / 0.26x |
| 512 | 0.7435 / 0.895 / 0.56x | 0.9595 / 1.000 / 0.41x | 0.9895 / 1.000 / 0.33x | 0.9990 / 1.000 / 0.24x |
| 1024 | 0.9010 / 1.000 / 0.34x | 0.9960 / 1.000 / 0.29x | 1.0000 / 1.000 / 0.24x | 1.0000 / 1.000 / 0.19x |

No tested flat-binary candidate configuration beats the optimized exact float
scan at this `128`-dimension, `10000`-document scale. The best total latency is
`128 bits × 100 candidates` at `70.22 ms`, still `1.24x` slower than exact and
with only `0.3200` exact-top-k coverage.

The stage breakdown explains why:

| Configuration | Query encode ms | Hamming search ms | Exact rerank ms | Total ms |
| --- | ---: | ---: | ---: | ---: |
| 128 bits × 100 | 12.56 | 55.24 | 2.49 | 70.22 |
| 512 bits × 100 | 49.78 | 49.61 | 2.53 | 101.73 |
| 512 bits × 500 | 50.26 | 75.65 | 10.08 | 136.14 |
| 1024 bits × 100 | 98.77 | 62.74 | 4.12 | 165.58 |

Flat Hamming search alone can approach or slightly beat the exact float scan,
but random-hyperplane query encoding consumes the gain. Candidate over-fetch
and reranking then add more work. Binary signatures still reduce index payload,
but this flat implementation is currently a compression and candidate-quality
experiment, not a latency optimization.

### What to check next

- Vectorize or batch the random-hyperplane encoder and measure encoding
  separately before changing the retrieval architecture.
- Add a sub-linear Hamming candidate index; a full-corpus Hamming scan cannot
  exploit the main indexing benefit of binary signatures.
- Repeat on real `384`, `768`, and `1536` dimensional embeddings with qrels;
  the float/binary crossover depends strongly on source dimension.
- Increase data seed count before making architecture defaults.
- Add `nDCG@10`, dense-vector bytes read for rerank, and persisted vector fetch
  cost.
- Compare top-N candidate selection with Hamming-radius or bucket/Multi-Index
  Hashing candidate generation.

## 2026-07-20: binary hot-path completion

### Why this continuation was needed

The SIMD-controlled rerun above proved that the float baseline had previously
been too weak, but its new conclusion was also suspicious: two XOR operations
and two popcounts for a 128-bit code should not cost as much as a 128-float dot
product. The follow-up therefore decomposed and optimized the complete binary
hot path before accepting the latency conclusion.

### Changes under test

- Width-aware Hamming dispatch: short signatures use hardware POPCNT instead
  of entering an AVX2 kernel whose short tail fell back to byte lookup.
- One reusable batch Hamming kernel per index instead of validation and runtime
  dispatch for every record.
- Contiguous row-major signature words, contiguous lightweight records, and one
  shared `BinarySignatureInfo` per flat index.
- Distance-histogram top-k selection with deterministic chunk-id tie breaking.
- Empty-filter fast path so the scan does not call metadata matching twice per
  record.
- Lazy materialization of the Rademacher projection matrix, SIMD dense dot
  products, ordered batch encoding, and a sparse-input encoder path.
- An explicit encoder contract bump from `v1` to `v2`. Dense SIMD, scalar, and
  sparse paths now share a fixed eight-lane reduction order so persisted
  signatures do not depend on the runtime backend; changing from the old
  scalar accumulation order is nevertheless behavior-affecting.
- An experimental `MultiProbeHammingIndex` with configurable projected-bit
  tables and bounded radius probing.
- A separate `agent-memory-hamming-hot-path-bench` executable for raw-kernel,
  selection, flat-index, and multi-probe measurements.

Eigen was still not added. The measured kernels need runtime CPU dispatch and
packed-bit handling directly; a new general matrix dependency would not remove
the need for those paths.

### Decomposed 128-bit hot path

Setup: 10,000 uniformly generated signatures, 100 queries, top-10, five timing
repeats. The selected backend was `hardware_popcount`.

| Stage or strategy | Median total for 100 queries |
| --- | ---: |
| Raw contiguous Hamming scan | 2.00 ms |
| Top-k with `partial_sort` only | 3.98 ms |
| Top-k with `nth_element` plus final sort | 12.08 ms |
| Top-k with distance buckets | 1.54 ms |
| Complete optimized `FlatBinarySignatureIndex` | 5.12 ms |

The old flat-index measurement was about `48.20 ms`. Compact storage,
short-width POPCNT dispatch, batch scanning, distance buckets, and the empty
filter path reduce it by roughly `9x`. The remaining difference between raw
scan plus selection and the full index is result construction, identifiers,
metadata, validation, and allocator effects rather than Hamming arithmetic.

### Bounded multi-probe prototype

The prototype establishes the API and exposes candidate/bucket diagnostics,
but does not justify a production default:

| Corpus/configuration | Mean candidates | Recall@10 vs flat | Time | Speedup vs flat |
| --- | ---: | ---: | ---: | ---: |
| 10k, 8 tables x 8 bits, radius 1, target 640 | 2,486 | 0.797 | 7.32 ms | 0.70x |
| 100k, same tables, target 640 (radius 0 stopped early) | 3,087 | 0.267 | 16.85 ms | 3.01x |
| 100k, same tables, target 4,000 | 24,898 | 0.850 | 75.82 ms | 0.67x |

This simple projection scheme can be fast or high-recall, but not both in the
tested configurations. Fixed bucket parameters also have no worst-case
sub-linear guarantee. Production follow-up should compare proper Multi-Index
Hashing or HNSW-Hamming rather than promote this prototype by default.

### Final clustered-vector grid

The final rerun used the same 10,000 documents, 100 queries, 128-dimensional
clustered vectors, one data seed, two encoder seeds, two binary repeats, and
five exact repeats. Exact AVX2 samples were `58.839`, `59.127`, `59.620`,
`58.929`, and `58.692 ms`; the common median denominator was `58.929 ms`.

Each cell is `exact-top-k coverage / top-1 agreement / median total speedup`:

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.3200 / 0.395 / 6.81x | 0.6260 / 0.685 / 2.70x | 0.7795 / 0.850 / 1.60x | 0.9035 / 0.955 / 0.82x |
| 256 | 0.5065 / 0.665 / 5.58x | 0.8260 / 0.890 / 2.62x | 0.9265 / 0.955 / 1.55x | 0.9780 / 0.990 / 0.82x |
| 512 | 0.7435 / 0.895 / 4.01x | 0.9595 / 1.000 / 2.27x | 0.9895 / 1.000 / 1.43x | 0.9990 / 1.000 / 0.82x |
| 1024 | 0.9010 / 1.000 / 2.29x | 0.9960 / 1.000 / 1.58x | 1.0000 / 1.000 / 1.15x | 1.0000 / 1.000 / 0.72x |

Representative stage medians:

| Configuration | Query encode | Hamming search | Exact rerank | Total |
| --- | ---: | ---: | ---: | ---: |
| 128 bits x 100 | 0.50 ms | 6.81 ms | 1.26 ms | 8.65 ms |
| 256 bits x 500 | 0.90 ms | 16.35 ms | 5.57 ms | 22.49 ms |
| 512 bits x 500 | 1.76 ms | 18.80 ms | 5.27 ms | 25.95 ms |
| 1024 bits x 500 | 3.50 ms | 27.75 ms | 5.78 ms | 37.33 ms |

### Updated conclusion

The earlier "no latency win" conclusion is superseded. The arithmetic was not
the problem; container layout, short-code dispatch, selection, metadata calls,
and on-the-fly hyperplane generation dominated the hot path.

At this scale, `512 bits x 500 candidates` is again a strong balanced point:
`0.9595` exact-top-k coverage, perfect top-1 agreement, and `2.27x` total
speedup against the current SIMD-enabled `ExactVectorIndex`. This comparison is
qualified by the contiguous compute-oriented baseline in the continuation
below. `1024 x 500` is the quality-oriented point at `0.996` coverage and
`1.57x` against the current index. Candidate sets of 2,000 are still too large:
exact reranking and result selection erase the gain.

The next evidence gate is no longer basic flat-scan viability. It is:

- real 384/768/1536-dimensional embeddings and qrels;
- persisted float-vector fetch/read amplification during rerank;
- more data and encoder seeds with confidence intervals;
- a mature high-recall Hamming ANN implementation, because the first bounded
  multi-probe prototype did not beat the optimized flat scan at useful recall.

## 2026-07-20: comparable contiguous dense baseline

### Why this continuation was needed

The previous grid compared a compact binary filter with the production-shaped
`ExactVectorIndex`. That is a valid implementation-level comparison, but it is
not a clean answer to whether Hamming filtering beats exact dense arithmetic:
the current exact index stores records in an ordered map, keeps embeddings in
separate allocations, parses identifiers, and constructs public result objects.

This continuation adds a second exact oracle that keeps all vectors in one
row-major float buffer, caches inverse norms, dispatches the selected SIMD dot
product kernel once per batch, reuses dot-product and scoring workspaces, and
returns only document positions. The current index and contiguous baseline are
timed in alternating order. The benchmark rejects the run unless both produce
the same exact top-k for every query.

### Expected result

The contiguous baseline was expected to be faster than the current index and
therefore reduce the reported binary speedup. A meaningful binary-filter win
would need to remain visible against this stronger denominator, not merely
against object layout and API overhead in `ExactVectorIndex`.

### Configuration

- 10,000 documents and 100 queries;
- 128-dimensional clustered normalized vectors;
- data seed `42`;
- encoder seeds `1001` and `1002`;
- signature widths `128`, `256`, `512`, and `1024` bits;
- candidate limits `100`, `500`, `1000`, and `2000`;
- five binary timing repeats per encoder seed;
- seven timing repeats for each exact baseline;
- randomized grid order and alternating exact-baseline order.

### Exact-baseline result

| Baseline | Median for 100 queries | Samples, ms |
| --- | ---: | --- |
| Current `ExactVectorIndex` | 57.8083 ms | 57.8083, 59.7544, 56.8027, 58.2207, 56.5642, 59.5654, 57.7041 |
| Contiguous exact cosine | 28.7912 ms | 29.1613, 28.7912, 28.4862, 27.9653, 29.1701, 28.6445, 28.8731 |

The existing index is about `2.01x` slower in this fixture. Consequently, every
binary speedup is reported twice below. Each cell is
`coverage / top-1 / speedup vs current index / speedup vs contiguous exact`.

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.3200 / 0.395 / 6.60x / 3.29x | 0.6260 / 0.685 / 2.65x / 1.32x | 0.7795 / 0.850 / 1.51x / 0.75x | 0.9035 / 0.955 / 0.82x / 0.41x |
| 256 | 0.5065 / 0.665 / 5.64x / 2.81x | 0.8260 / 0.890 / 2.55x / 1.27x | 0.9265 / 0.955 / 1.52x / 0.76x | 0.9780 / 0.990 / 0.82x / 0.41x |
| 512 | 0.7435 / 0.895 / 3.96x / 1.97x | 0.9595 / 1.000 / 2.25x / 1.12x | 0.9895 / 1.000 / 1.43x / 0.71x | 0.9990 / 1.000 / 0.81x / 0.40x |
| 1024 | 0.9010 / 1.000 / 2.22x / 1.10x | 0.9960 / 1.000 / 1.55x / 0.77x | 1.0000 / 1.000 / 1.12x / 0.56x | 1.0000 / 1.000 / 0.72x / 0.36x |

Direct binary top-k remains a poor final ranker. Its total median and speedup
against current/contiguous exact were `5.4553 ms` and `10.60x/5.28x` at 128
bits, `7.5054 ms` and `7.70x/3.84x` at 256 bits, `11.3379 ms` and
`5.10x/2.54x` at 512 bits, and `23.1109 ms` and `2.50x/1.25x` at 1024 bits.
Mean direct top-10 recall ranged only from `0.0805` to `0.4295`.

### Revised conclusion

Binary signatures are useful as a compact candidate filter, but the latency
claim is configuration- and baseline-dependent. The earlier `512 x 500`
result remains attractive for quality (`0.9595` coverage and perfect top-1),
yet its advantage over the comparable contiguous exact scan is only `1.12x` in
this single local run. That is directional evidence, not a robust performance
win. The `1024 x 500` quality-oriented point loses to contiguous exact at
`0.77x`. Lower-width and smaller-candidate modes produce clearer speedups, but
with correspondingly lower exact-top-k coverage.

At 128 source dimensions, a high-quality flat binary filter therefore has not
yet demonstrated a decisive compute-only latency advantage. Its stronger
system-level cases remain compact hot-index payload, lower memory bandwidth,
avoiding reads of most persisted float vectors, and higher-dimensional source
embeddings where exact dense arithmetic is more expensive. The current rerank
path still uses individually allocated `Embedding` values, so a compact
row-major rerank store may improve the complete filter pipeline.

### Follow-up experiments

- Repeat on real 384/768/1536-dimensional embeddings with qrels.
- Measure persisted vector bytes fetched and read amplification for rerank.
- Add a compact row-major or blocked dense candidate store and compare it with
  both exact denominators.
- Test adaptive candidate budgets instead of one fixed limit for every query.
- Compare a mature sub-linear Hamming ANN implementation with both flat scans.
- Add more data seeds, confidence intervals, and randomized warm-up protocol
  before choosing a production default.

## 2026-07-20: zero-training encoder family grid

### Why this continuation was needed

PR #57 made random hyperplanes viable as a binary candidate filter, but it did
not prove that independent random projection is the best zero-training encoder
family. PR #59 adds two additional baselines:

- `coordinate_sign`: `bit_i = sign(x_i)`, exactly one bit per source
  coordinate;
- `randomized_hadamard_projection`: a dependency-free randomized
  Walsh-Hadamard structured projection inspired by the Faiss `IndexLSH`
  direction, but not a Faiss-compatible orthogonal/tight-frame implementation
  for arbitrary dimensions.

The question is whether the promising `500-1000` candidate band was specific
to random hyperplanes or whether a more structured zero-training projection
improves exact-top-k candidate coverage at the same bit budget.

### Expected result

Coordinate sign was expected to be very cheap and useful mostly as a diagnostic
baseline, because it preserves only source-coordinate polarity and cannot
change the bit budget. Randomized Hadamard projection was expected to beat
independent random hyperplanes on coverage at equal bit counts in this 128D
power-of-two fixture, especially at smaller candidate limits, while keeping
similar Hamming-search costs.

### Configuration

- PR context: PR #59 branch, local head before review.
- Command:

  ```bash
  ./build-codex-pr59/tools/agent-memory-bench/agent-memory-bench \
      tools/agent-memory-bench/synthetic-binary-rerank-grid.example.json \
      tmp/synthetic-binary-rerank-grid-pr59-fixed.json
  ```

- 10,000 documents and 100 queries;
- 128-dimensional clustered normalized vectors;
- data seed `42`;
- encoder families: `random_hyperplane_rademacher`, `coordinate_sign`,
  `randomized_hadamard_projection`;
- encoder seeds `1001` and `1002` for seedable families;
- signature widths `128`, `256`, `512`, and `1024` bits;
- candidate limits `100`, `500`, `1000`, and `2000`;
- five binary timing repeats per encoder seed;
- seven timing repeats for each exact baseline.

`coordinate_sign` emits exactly `128` bits in this fixture and is skipped for
the other bit counts by contract.

### Exact-baseline result

| Baseline | Median for 100 queries | Samples, ms |
| --- | ---: | --- |
| Current `ExactVectorIndex` | 58.0553 ms | 57.7324, 58.1823, 57.2652, 58.0553, 58.1381, 58.4762, 57.4997 |
| Contiguous exact cosine | 27.9438 ms | 27.9525, 27.7392, 28.2203, 27.5157, 27.9438, 27.4866, 28.1706 |

Each cell below is
`exact-top-k coverage / top-1 agreement / speedup vs current index / speedup vs contiguous exact`.

### Random hyperplane baseline

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.3200 / 0.395 / 6.64x / 3.20x | 0.6260 / 0.685 / 2.70x / 1.30x | 0.7795 / 0.850 / 1.49x / 0.72x | 0.9035 / 0.955 / 0.81x / 0.39x |
| 256 | 0.5065 / 0.665 / 5.38x / 2.59x | 0.8260 / 0.890 / 2.49x / 1.20x | 0.9265 / 0.955 / 1.51x / 0.73x | 0.9780 / 0.990 / 0.82x / 0.40x |
| 512 | 0.7435 / 0.895 / 3.94x / 1.90x | 0.9595 / 1.000 / 2.23x / 1.07x | 0.9895 / 1.000 / 1.41x / 0.68x | 0.9990 / 1.000 / 0.79x / 0.38x |
| 1024 | 0.9010 / 1.000 / 2.19x / 1.05x | 0.9960 / 1.000 / 1.51x / 0.73x | 1.0000 / 1.000 / 1.11x / 0.53x | 1.0000 / 1.000 / 0.71x / 0.34x |

### Coordinate sign baseline

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.4500 / 0.640 / 6.75x / 3.25x | 0.7800 / 0.910 / 2.76x / 1.33x | 0.8800 / 0.960 / 1.50x / 0.72x | 0.9590 / 0.970 / 0.83x / 0.40x |

### Randomized Hadamard baseline

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.4660 / 0.655 / 6.54x / 3.15x | 0.7860 / 0.890 / 2.61x / 1.26x | 0.8965 / 0.955 / 1.46x / 0.70x | 0.9590 / 0.995 / 0.81x / 0.39x |
| 256 | 0.6980 / 0.900 / 5.74x / 2.76x | 0.9390 / 0.970 / 2.61x / 1.26x | 0.9790 / 1.000 / 1.49x / 0.72x | 0.9930 / 1.000 / 0.81x / 0.39x |
| 512 | 0.8735 / 0.975 / 4.27x / 2.05x | 0.9895 / 1.000 / 2.31x / 1.11x | 0.9990 / 1.000 / 1.45x / 0.70x | 1.0000 / 1.000 / 0.81x / 0.39x |
| 1024 | 0.9805 / 1.000 / 2.38x / 1.15x | 0.9995 / 1.000 / 1.61x / 0.77x | 1.0000 / 1.000 / 1.15x / 0.55x | 1.0000 / 1.000 / 0.72x / 0.35x |

### Interpretation

The structured zero-training encoder is better than the independent random
hyperplane baseline in this synthetic 128D fixture. The clearest practical
points are:

- `randomized_hadamard_projection 256 bits x 500 candidates` reaches
  `0.9390` exact-top-k coverage at `2.61x` vs current exact and `1.26x` vs
  contiguous exact;
- `randomized_hadamard_projection 512 bits x 500 candidates` reaches
  `0.9895` coverage with perfect top-1 agreement and still keeps a small
  `1.11x` compute-oriented speedup;
- `randomized_hadamard_projection 1024 bits x 100 candidates` reaches
  `0.9805` coverage and `1.15x` vs contiguous exact, suggesting that a wider
  code with a smaller candidate set can be competitive;
- `coordinate_sign 128 bits` beats random hyperplanes at 128 bits in this
  fixture, but it is not a general bit-budget mechanism: it is tied to source
  dimensionality and should remain a diagnostic/lower-complexity baseline.

The useful system shape is still binary candidate filtering plus exact dense
rerank. Direct binary top-k remains too weak as the final ranker, and large
candidate sets such as `2000` erase the compute-only win even when quality is
excellent.

### Limitations

- One synthetic clustered dataset and one data seed.
- Two encoder seeds for seedable families.
- 128 source dimensions only.
- In-memory flat Hamming scan, not a production sub-linear binary index.
- Dense vectors are still available in memory for rerank; persisted-vector
  fetch cost is not measured.
- The randomized Hadamard implementation is dependency-free and Faiss-inspired,
  not a byte-for-byte Faiss `IndexLSH` port. This experiment uses 128 source
  dimensions, so complete Hadamard blocks are orthogonal in the tested fixture;
  the result must not be generalized to padded non-power-of-two dimensions
  without rerunning the benchmark.

### Follow-up experiments

- Repeat the zero-training family grid on 384/768/1536-dimensional real
  embeddings and qrels.
- Evaluate stronger learned global projection families, such as PCA/ITQ-style
  rotation or supervised/data-dependent hashing, against the zero-training
  winners at the same bit/candidate budgets.
- Test local projection only behind an explicit global routing stage, because
  local code spaces are not globally comparable.
- Add a production-shaped candidate store that counts persisted float-vector
  bytes fetched during rerank.

## 2026-07-20 continuation: PR60 global learned pair-difference baseline

### Question

Does a simple dependency-free learned global projection improve candidate
coverage over the zero-training encoders when trained only on document vectors?

This experiment is intentionally a first learned baseline, not a final learned
hashing method. The encoder trains one global artifact per
`data_seed x encoder_seed x bit_count` from document vectors only, then encodes
both documents and queries with that same artifact. Evaluation queries are not
used as training input.

### Configuration

- PR context: PR #60 stacked on PR #59.
- Command:

  ```bash
  ./build-codex-pr59/tools/agent-memory-bench/agent-memory-bench \
      tools/agent-memory-bench/synthetic-binary-rerank-grid.example.json \
      tmp/synthetic-binary-rerank-grid-pr60.json
  ```

- 10,000 documents and 100 queries;
- 128-dimensional clustered normalized vectors;
- data seed `42`;
- encoder families: `random_hyperplane_rademacher`, `coordinate_sign`,
  `randomized_hadamard_projection`, and
  `learned_pair_difference_projection`;
- encoder seeds `1001` and `1002` for seedable families;
- signature widths `128`, `256`, `512`, and `1024` bits;
- candidate limits `100`, `500`, `1000`, and `2000`;
- five binary timing repeats per encoder seed;
- seven timing repeats for each exact baseline.

Encoder artifacts are cached within the benchmark grid for each
`family x encoder_seed x bit_count`, so binary timing repeats do not retrain the
same artifact. Training time is not included in the query-time numbers.

### Exact-baseline result

| Baseline | Median for 100 queries | Samples, ms |
| --- | ---: | --- |
| Current `ExactVectorIndex` | 59.9260 ms | 58.8863, 60.3179, 59.3402, 60.9495, 59.9260, 61.2679, 59.7992 |
| Contiguous exact cosine | 29.8173 ms | 28.6337, 28.4074, 29.8748, 30.0288, 29.6081, 29.9143, 29.8173 |

Each cell below is
`exact-top-k coverage / top-1 agreement / speedup vs current index / speedup vs contiguous exact`.

### Learned pair-difference projection

| Bits | 100 candidates | 500 candidates | 1000 candidates | 2000 candidates |
| ---: | --- | --- | --- | --- |
| 128 | 0.3180 / 0.415 / 6.58x / 3.27x | 0.6160 / 0.750 / 2.82x / 1.40x | 0.7705 / 0.880 / 1.61x / 0.80x | 0.8940 / 0.965 / 0.85x / 0.42x |
| 256 | 0.4945 / 0.655 / 5.72x / 2.84x | 0.8250 / 0.950 / 2.63x / 1.31x | 0.9210 / 0.985 / 1.59x / 0.79x | 0.9785 / 1.000 / 0.85x / 0.42x |
| 512 | 0.7100 / 0.910 / 4.03x / 2.00x | 0.9450 / 0.995 / 2.26x / 1.12x | 0.9885 / 1.000 / 1.43x / 0.71x | 0.9985 / 1.000 / 0.83x / 0.41x |
| 1024 | 0.8630 / 0.985 / 2.27x / 1.13x | 0.9910 / 1.000 / 1.56x / 0.78x | 0.9975 / 1.000 / 1.15x / 0.57x | 1.0000 / 1.000 / 0.72x / 0.36x |

### Key comparison against previous winners

| Config | Learned pair-difference | Random hyperplane | Randomized Hadamard |
| --- | --- | --- | --- |
| 256 bits x 500 candidates | 0.8250 coverage, 1.31x vs contiguous | 0.8260 coverage, 1.31x vs contiguous | 0.9390 coverage, 1.33x vs contiguous |
| 512 bits x 500 candidates | 0.9450 coverage, 1.12x vs contiguous | 0.9595 coverage, 1.13x vs contiguous | 0.9895 coverage, 1.15x vs contiguous |
| 1024 bits x 100 candidates | 0.8630 coverage, 1.13x vs contiguous | 0.9010 coverage, 1.12x vs contiguous | 0.9805 coverage, 1.24x vs contiguous |

### Interpretation

The simple learned pair-difference encoder does not beat the structured
zero-training Hadamard baseline in this fixture. At `256 bits x 500 candidates`
it is roughly tied with independent random hyperplanes and clearly behind
Randomized Hadamard. At `512` and `1024` bits it becomes useful as a binary
candidate filter, but still remains weaker than Hadamard at the same
candidate budget.

This is still a useful negative result: merely making `R` data-dependent is not
enough. The current trainer learns directions from deterministic farthest
pairs and median thresholds, but it does not optimize quantization loss,
balance all bits jointly, decorrelate bits, or use relevance supervision.

The practical system conclusion remains unchanged:

- binary codes are useful as a candidate filter plus exact dense rerank;
- direct binary top-k is still not the final ranker;
- `500` candidates is still the most interesting speed/quality band for this
  synthetic 128D fixture;
- `2000` candidates recover excellent quality but usually lose the
  compute-oriented speedup;
- stronger learned hashing should be compared against Randomized Hadamard, not
  only against random hyperplanes.

### Follow-up experiments

- Add PCA/ITQ-style global learned projection with explicit validation split
  and bit-health diagnostics.
- Evaluate supervised or metric-aware learned hashing only after defining
  train/test qrels and leakage rules.
- Repeat the grid on real 384/768/1536-dimensional embeddings; the 128D
  synthetic result is directional, not a production decision.
- Keep local projection experiments behind an explicit global router, because
  local code spaces are not globally comparable.
