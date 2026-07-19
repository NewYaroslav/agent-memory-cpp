# Binary candidate-filter hypothesis matrix

This note fixes the first binary retrieval experiment ladder after the
synthetic staircase reports. It is intentionally an evaluation plan, not an API
specification and not an implementation ticket for every binary technique in
the roadmaps.

The goal is to avoid treating a BoW result as evidence for dense embeddings.
BoW is the proof of mechanism; dense embeddings are the proof of value.

## Scope

The first binary work should answer three questions:

1. Can the benchmark pipeline measure binary candidate filtering without
   ambiguity?
2. Can a no-training binary encoder preserve enough candidates for exact rerank?
3. Which encoder family is worth testing later on real dense embeddings?

Out of scope for the first implementation wave:

- MDBX bucket persistence;
- SIMD kernels;
- trained autoencoders;
- production acceptance thresholds;
- replacing exact rerank with binary-only retrieval.

## Evaluation pipeline

All early binary experiments should use the same two-stage shape:

```text
query text
    -> vector source
    -> binary query signature
    -> Hamming candidate filter
    -> exact rerank on the original representation
    -> RetrievalEvalDataset metrics
```

The exact baseline remains:

```text
query text
    -> exact baseline over the original representation
    -> RetrievalEvalDataset metrics
```

A speed result is useful only when candidate recall remains sufficient for the
exact reranker to preserve final quality.

## Vector sources

| Source | Purpose | What it can prove | What it cannot prove |
| --- | --- | --- | --- |
| `bow_vector` | Dependency-free mechanism test | Signature packing, Hamming distance, candidate filtering, metric hooks, rerank plumbing | Dense-embedding quality retention |
| Dense embeddings | Value test | Whether binary filtering helps semantic retrieval at useful quality/cost points | Basic infrastructure correctness if BoW already failed |

BoW vectors are non-negative, sparse before materialization, and grow with the
synthetic vocabulary. Dense embeddings may be centered, anisotropic, dense, and
model-dependent. Results must not be transferred between them without a separate
run.

## Encoder hypotheses

| Encoder | BoW applicability | Dense applicability | First hypothesis | First status |
| --- | --- | --- | --- | --- |
| Sign threshold (`x_i >= 0`) | Poor: non-negative BoW collapses most bits to `1` | Cheap baseline for centered embeddings | Useful only as a dense sanity baseline | Do not start here for BoW |
| Corpus mean threshold | Good dependency-free baseline | Useful when dimensions are not centered | Avoids zero-as-one collapse for non-negative BoW when thresholds are positive; occupancy still depends on document frequency | Candidate for first simple BoW baseline, with measured occupancy |
| Corpus median threshold | Risky for sparse zero-heavy BoW: the median is often zero, and tie handling can collapse or highly imbalance bits | Useful when dimensions are continuous or less tie-heavy | May be robust for dense or skewed dimensions, but is not presumed balanced for sparse BoW | Test only after bit-occupancy diagnostics |
| Random hyperplane LSH | Usable, but sensitive to sparse lexical geometry | Main no-training cosine-LSH baseline | Best first general encoder for dense embeddings | Candidate for first reusable encoder |
| SimHash / weighted projection | Good lexical-space candidate | Secondary dense candidate | Better lexical candidate filtering than raw random hyperplanes | Later no-training baseline |
| Product/block quantization style binary code | Probably overkill for BoW | Possible ANN/compression follow-up | Better recall at fixed bytes than simple LSH | Later, only after simple baselines |
| Autoencoder / learned binary encoder | Not justified for BoW | Research candidate for dense embeddings | Better quality/storage tradeoff after training | M3/research, not first wave |

## First implementation recommendation

The first code PR should implement only the shared binary primitives and one
no-training encoder. Two acceptable routes:

1. `MeanThresholdBinaryEncoder` first, because it is the simplest BoW-specific
   diagnostic baseline and avoids the sign-threshold zero collapse when corpus
   means are positive. It still needs explicit bit-occupancy measurements.
2. `RandomHyperplaneLshEncoder` first, because it is the most reusable bridge
   to dense embeddings.

`MedianThresholdBinaryEncoder` should not be the first BoW recommendation unless
bit-occupancy measurements show that zero-valued medians and tie handling do not
collapse the fixture.

Do not implement autoencoder code in the first wave. It needs a training
corpus, train/validation/test split, leakage controls, model versioning, and a
clear loss choice. It should compete after no-training baselines exist.

## Metrics to record

Quality:

- Recall@10;
- nDCG@10;
- MRR;
- candidate-set recall before exact rerank;
- final quality after exact rerank.

Filtering and speed:

- `candidate_count_before_filter`;
- `candidate_count_after_filter`;
- `candidate_reduction_ratio`;
- `filter_latency_ms`;
- `rerank_latency_ms`;
- total p95 latency.

Storage:

- bit count per signature;
- bytes per signature;
- estimated in-memory index bytes;
- later, bucket posting overhead.

Binary code health:

- fraction of `1` values per bit;
- fraction of constant bits;
- mean, minimum, and maximum bit entropy;
- duplicate-signature rate;
- mean pairwise Hamming distance;
- candidate bucket-size distribution.

## Stop/go criteria

Treat these as investigation gates, not production contracts:

- If the BoW filter cannot preserve candidate-set recall high enough for exact
  reranking to recover final quality, fix the binary plumbing before testing
  dense embeddings.
- If bit-health diagnostics show many constant bits or a high
  duplicate-signature rate, retune thresholds or projections before interpreting
  retrieval metrics.
- If BoW works but dense embeddings fail, do not discard the binary pipeline;
  discard or retune that encoder for dense geometry.
- If an encoder reduces candidates but total p95 latency does not improve, keep
  it as a compression candidate only, not a speed candidate.
- If candidate-set recall is high but final quality drops, inspect rerank
  coverage, duplicate handling, cutoff semantics, and no-answer behavior before
  blaming the encoder.
- If no-training encoders do not give a useful quality/cost point, then consider
  trained encoders, including autoencoder-style binary bottlenecks.

## Suggested PR ladder

1. Add binary value types and Hamming distance tests.
2. Add bit-health diagnostics for candidate binary codes.
3. Add one no-training encoder in memory, with deterministic seeds or corpus
   thresholds.
4. Add an in-memory candidate filter plus exact rerank path.
5. Wire roadmap-label PR #29 hook fields in `BenchmarkReport` for binary
   filtering.
6. Run synthetic BoW sweep.
7. Add dense embedding fixture or adapter and repeat the same matrix.
8. Only then evaluate learned autoencoder-style encoders.

## Relationship to existing roadmaps

- [`guides/binary-embeddings-roadmap.md`](../../guides/binary-embeddings-roadmap.md)
  remains the broad compression landscape.
- [`guides/optimization-roadmap.md`](../../guides/optimization-roadmap.md)
  remains the storage/index roadmap for binary bucket indexes.
- [`guides/advanced-binary-techniques-roadmap.md`](../../guides/advanced-binary-techniques-roadmap.md)
  remains research-preview material for techniques beyond the first binary
  candidate-filter wave.
