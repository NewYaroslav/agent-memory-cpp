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

### Aggregate results

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
the table.

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

### What to check next

- Increase data seed count before making architecture defaults.
- Add real embedding vectors and qrels.
- Add `nDCG@10`, dense-vector bytes read for rerank, and persisted vector fetch
  cost.
- Compare top-N candidate selection with Hamming-radius or bucket/Multi-Index
  Hashing candidate generation.
