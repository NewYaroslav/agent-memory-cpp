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

The binary path is allowed to be faster only if it keeps enough candidate-set
recall for the exact reranker to recover final quality.

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
| Corpus mean threshold | Good dependency-free baseline | Useful when dimensions are not centered | More balanced bits than sign threshold | Candidate for first BoW implementation |
| Corpus median threshold | Good dependency-free baseline | Useful when dimensions are skewed or sparse-ish | More robust bit balance than mean threshold | Candidate for first BoW implementation |
| Random hyperplane LSH | Usable, but sensitive to sparse lexical geometry | Main no-training cosine-LSH baseline | Best first general encoder for dense embeddings | Candidate for first reusable encoder |
| SimHash / weighted projection | Good lexical-space candidate | Secondary dense candidate | Better lexical candidate filtering than raw random hyperplanes | Later no-training baseline |
| Product/block quantization style binary code | Probably overkill for BoW | Possible ANN/compression follow-up | Better recall at fixed bytes than simple LSH | Later, only after simple baselines |
| Autoencoder / learned binary encoder | Not justified for BoW | Research candidate for dense embeddings | Better quality/storage tradeoff after training | M3/research, not first wave |

## First implementation recommendation

The first code PR should implement only the shared binary primitives and one
no-training encoder. Two acceptable routes:

1. `MedianThresholdBinaryEncoder` first, because it is easiest to reason about
   on BoW and gives balanced bits on the synthetic fixture.
2. `RandomHyperplaneLshEncoder` first, because it is the most reusable bridge
   to dense embeddings.

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

## Stop/go criteria

Treat these as investigation gates, not production contracts:

- If BoW binary filtering cannot preserve candidate-set recall under exact
  rerank, fix the binary plumbing before testing dense embeddings.
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
2. Add one no-training encoder in memory, with deterministic seeds or corpus
   thresholds.
3. Add an in-memory candidate filter plus exact rerank path.
4. Wire roadmap-label PR #29 hook fields in `BenchmarkReport` for binary
   filtering.
5. Run synthetic BoW sweep.
6. Add dense embedding fixture or adapter and repeat the same matrix.
7. Only then evaluate learned autoencoder-style encoders.

## Relationship to existing roadmaps

- [`guides/binary-embeddings-roadmap.md`](../../guides/binary-embeddings-roadmap.md)
  remains the broad compression landscape.
- [`guides/optimization-roadmap.md`](../../guides/optimization-roadmap.md)
  remains the storage/index roadmap for binary bucket indexes.
- [`guides/advanced-binary-techniques-roadmap.md`](../../guides/advanced-binary-techniques-roadmap.md)
  remains research-preview material for techniques beyond the first binary
  candidate-filter wave.
