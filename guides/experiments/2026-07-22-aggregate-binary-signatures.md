# Aggregate binary signatures for document-level routing

## 2026-07-22 / PR #79 stacked on PR #77

Question: after PR #77 adds `AggregateBinarySignatureBuilder`, can a single
document or memory-level binary code act as a useful candidate filter for an
object that contains several chunk signatures?

Setup:

- command: `agent-memory-aggregate-signature-bench`
- config:
  `tools/agent-memory-bench/aggregate-signature-smoke.example.json`
- synthetic object layout: 24 documents, 3 chunks per document
- query layout: 24 queries, each generated near one chunk
- vector dimension: 64
- binary encoder: `random_hyperplane_rademacher` v2
- bit count: 128
- exact object oracle: max cosine similarity over the object's child chunks
- aggregate candidate score: Hamming distance between query code and one
  aggregate document code
- candidate limits: 3, 5, 10, 24

Expected result:

- OR-style aggregation should favor recall by firing when any child chunk has a
  bit.
- All-bits aggregation should be more selective and may preserve the nearest
  obvious object, but can miss broader exact top-k coverage.
- Majority and threshold modes should sit between those extremes.

Actual single smoke run:

| Mode | Candidates | Exact top-5 coverage | Top-1 agreement |
|---|---:|---:|---:|
| AnySetBit | 3 | 0.425 | 0.875 |
| AnySetBit | 5 | 0.575 | 0.875 |
| AnySetBit | 10 | 0.833 | 0.875 |
| AnySetBit | 24 | 1.000 | 0.875 |
| MajoritySetBit | 3 | 0.442 | 0.875 |
| MajoritySetBit | 5 | 0.567 | 0.875 |
| MajoritySetBit | 10 | 0.817 | 0.875 |
| MajoritySetBit | 24 | 1.000 | 0.875 |
| AllSetBits | 3 | 0.383 | 1.000 |
| AllSetBits | 5 | 0.525 | 1.000 |
| AllSetBits | 10 | 0.767 | 1.000 |
| AllSetBits | 24 | 1.000 | 1.000 |
| ThresholdFraction 0.5 | 3 | 0.442 | 0.875 |
| ThresholdFraction 0.5 | 5 | 0.567 | 0.875 |
| ThresholdFraction 0.5 | 10 | 0.817 | 0.875 |
| ThresholdFraction 0.5 | 24 | 1.000 | 0.875 |

Interpretation:

- On this tiny synthetic fixture, aggregate signatures look plausible as a
  coarse document/memory routing signal, not as a final ranked retrieval result.
- `AnySetBit` gives the best top-10 exact-candidate coverage in this run.
- `AllSetBits` gives perfect top-1 agreement here, but lower exact top-5
  coverage. That suggests a stricter aggregate code can keep the most obvious
  target while losing weaker neighboring objects.
- `MajoritySetBit` and `ThresholdFraction(0.5)` are equivalent for three chunks
  in this setup and behave between OR and intersection.

Limitations:

- Single seed and tiny corpus.
- Synthetic clusters are much cleaner than real chunk collections.
- No exact rerank stage is measured here; the metric is candidate coverage for
  a later object-level or chunk-level reranker.
- Aggregates use one shared global binary space. This is not ASMS/MSBSE,
  multi-slot document hashing, a learned set encoder, or a local projection.
- The CTest validator is intentionally stronger than a process-exit smoke test:
  it checks schema/mode identity, aggregation-mode coverage, candidate-limit
  ordering, metric ranges, monotonic exact top-k coverage, and full coverage
  when the candidate limit equals the document count.

Next checks:

- Run a larger grid over chunk counts, bit counts, aggregation thresholds, and
  seeds.
- Compare object-level aggregate routing followed by exact child-chunk rerank.
- Test on frozen precomputed fixtures once they contain true multi-chunk
  documents or memory objects rather than one record per document.
