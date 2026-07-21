# 2026-07-21 — ITQ-style rotation binary baseline

## Question

Can an unsupervised ITQ-style rotation on top of the existing PCA projection
improve binary candidate quality on the synthetic clustered dense benchmark?

This experiment checks the first dependency-free implementation only. It is not
a production benchmark and not a supervised/data-dependent hashing result.

## Setup

- PR scope: `ItqRotationBinaryEncoder` v1 and benchmark wiring.
- Dataset: `synthetic_clustered_dense_v1_itq_128d`.
- Documents: 10,000.
- Queries: 100.
- Dense dimension: 128.
- `result_limit`: 10.
- Bit counts: 32, 64, 128.
- Candidate limits: 100, 500, 1000, 2000.
- Data seed: 42.
- Encoder seeds: 1001, 1002.
- Timing repeats: 3 for binary paths, 5 for exact baselines.
- Exact denominators from this local run:
  - current `ExactVectorIndex`: 60.252 ms / 100 queries.
  - contiguous exact baseline: 29.582 ms / 100 queries.

Command:

```bash
./build-codex-pr59/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-itq.example.json \
    tmp/synthetic-binary-rerank-grid-itq-pr65.json
```

## Expected result

ITQ should be at least competitive with PCA at the same bit width because it
keeps the PCA subspace but rotates it toward balanced binary cube vertices.
The expected gain is candidate quality, not necessarily full rerank speed,
because this PR still uses flat full-corpus Hamming scan and then exact float
rerank over selected candidates.

## Aggregate result

`coverage@1000` below means `exact_top_k_candidate_coverage` for
`candidate_limit = 1000`.

| Encoder | Bits | Direct Recall@10 vs exact | Top-1 agreement | Binary query total ms, median | Speedup vs contiguous exact | Coverage@1000 | Rerank@1000 total ms, median | Rerank@1000 speedup vs contiguous exact |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| random_hyperplane_rademacher | 32 | 0.0160 | 0.0000 | 4.692 | 6.304 | 0.4275 | 37.771 | 0.783 |
| random_hyperplane_rademacher | 64 | 0.0425 | 0.0000 | 4.781 | 6.188 | 0.5830 | 38.515 | 0.768 |
| random_hyperplane_rademacher | 128 | 0.0805 | 0.0250 | 5.657 | 5.231 | 0.7795 | 37.270 | 0.794 |
| learned_pair_difference_projection | 32 | 0.0145 | 0.0000 | 4.801 | 6.162 | 0.4325 | 39.740 | 0.744 |
| learned_pair_difference_projection | 64 | 0.0355 | 0.0100 | 4.658 | 6.350 | 0.5885 | 37.794 | 0.783 |
| learned_pair_difference_projection | 128 | 0.0830 | 0.0350 | 5.526 | 5.353 | 0.7705 | 37.628 | 0.786 |
| pca_projection | 32 | 0.0230 | 0.0150 | 4.665 | 6.343 | 0.5200 | 36.304 | 0.815 |
| pca_projection | 64 | 0.0655 | 0.0100 | 4.623 | 6.399 | 0.6980 | 39.637 | 0.746 |
| pca_projection | 128 | 0.1310 | 0.0800 | 5.722 | 5.171 | 0.8745 | 38.551 | 0.767 |
| itq_rotation_projection | 32 | 0.0265 | 0.0000 | 4.758 | 6.217 | 0.5560 | 36.507 | 0.810 |
| itq_rotation_projection | 64 | 0.0640 | 0.0250 | 4.727 | 6.258 | 0.7130 | 40.505 | 0.730 |
| itq_rotation_projection | 128 | 0.1490 | 0.0800 | 5.683 | 5.206 | 0.8990 | 40.289 | 0.734 |

## Interpretation

- ITQ improves the strongest 128-bit candidate coverage in this fixture:
  `0.8990` at top-1000 versus PCA `0.8745`, random hyperplane `0.7795`, and
  pair-difference `0.7705`.
- The gain is directional and modest. With only two encoder seeds, it should be
  treated as a baseline signal, not a stable ranking of encoder families.
- Direct binary top-10 recall remains low. The useful mode is over-fetching
  candidates and exact reranking, not returning Hamming top-10 directly.
- Rerank@1000 still does not beat the contiguous exact baseline end to end in
  this implementation. Flat Hamming scan plus dense rerank is useful for
  candidate-quality research, but a real speed win still needs a sub-linear
  binary index and/or cheaper candidate materialization/rerank.
- Training time is visible in the JSON but excluded from query/build timings.
  ITQ training is a cold-start artifact cost and must be tracked when comparing
  operational modes.

## Follow-ups

- Run the same family grid on higher-dimensional synthetic embeddings and real
  embedding fixtures.
- Add multi-seed statistical runs before promoting any ITQ acceptance threshold.
- Compare ITQ with supervised/data-dependent hashing once qrels or hard-negative
  pairs are available.
- Re-test after a production-oriented Hamming ANN or persisted bucket index is
  available; the current result is limited by flat full-corpus scan.
