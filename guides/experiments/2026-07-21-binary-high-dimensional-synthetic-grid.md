# 2026-07-21: high-dimensional synthetic binary rerank grid

## Question

Do the 128-dimensional conclusions from the binary rerank grid carry over to
higher-dimensional dense vectors?

PR #59 showed that `randomized_hadamard_projection` is the strongest
zero-training baseline in the 128D synthetic fixture. PR #60 added a simple
global learned pair-difference projection, but it did not beat Hadamard in
128D. This follow-up checks 384D, 768D, and 1536D synthetic clustered vectors.

## Expected result

We expected the old `500-1000` candidate band to become less reliable as source
dimension grows, unless the bit budget also grows. We also expected
Randomized Hadamard to remain the strongest zero-training projection, while the
simple learned pair-difference baseline would probably still lag because it
does not optimize quantization loss or bit decorrelation.

## Configuration

- PR context: PR #61 stacked on PR #60.
- Synthetic data only; no real embedding/qrels fixture is used in this PR.
- 5,000 documents and 50 queries.
- Result limit: `10`.
- Data seed: `42`.
- Encoder seed: `1001` for seedable families.
- Encoder families:
  - `random_hyperplane_rademacher`;
  - `coordinate_sign`;
  - `randomized_hadamard_projection`;
  - `learned_pair_difference_projection`.
- Candidate limits: `100`, `500`, `1000`.
- Binary timing repeats: `3`.
- Exact timing repeats: `5`.
- Execution order is randomized within each data seed.

Commands:

```bash
./build-codex-pr59/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-highdim-384.example.json \
    tmp/synthetic-binary-rerank-grid-highdim-384-pr61.json

./build-codex-pr59/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-highdim-768.example.json \
    tmp/synthetic-binary-rerank-grid-highdim-768-pr61.json

./build-codex-pr59/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-highdim-1536.example.json \
    tmp/synthetic-binary-rerank-grid-highdim-1536-pr61.json
```

## Exact-baseline timings

| Dimension | Current `ExactVectorIndex`, ms | Contiguous exact cosine, ms |
| ---: | ---: | ---: |
| 384 | 88.5995 | 36.7132 |
| 768 | 47.3266 | 35.1594 |
| 1536 | 133.0425 | 97.9592 |

These are local medians for 50 queries. They are useful as run context, not as
portable benchmark numbers.

## Coverage at 500 candidates

Each cell is `exact-top-k candidate coverage / top-1 agreement`.

### 384D

| Encoder | Bits | 500 candidates |
| --- | ---: | --- |
| `coordinate_sign` | 384 | 0.8260 / 0.86 |
| `random_hyperplane_rademacher` | 256 | 0.6180 / 0.72 |
| `random_hyperplane_rademacher` | 384 | 0.7240 / 0.88 |
| `random_hyperplane_rademacher` | 512 | 0.8040 / 0.94 |
| `random_hyperplane_rademacher` | 1024 | 0.9220 / 0.98 |
| `randomized_hadamard_projection` | 256 | 0.6400 / 0.76 |
| `randomized_hadamard_projection` | 384 | 0.7700 / 0.92 |
| `randomized_hadamard_projection` | 512 | 0.8680 / 0.96 |
| `randomized_hadamard_projection` | 1024 | 0.9800 / 1.00 |
| `learned_pair_difference_projection` | 256 | 0.5760 / 0.72 |
| `learned_pair_difference_projection` | 384 | 0.6840 / 0.82 |
| `learned_pair_difference_projection` | 512 | 0.7360 / 0.88 |
| `learned_pair_difference_projection` | 1024 | 0.8860 / 0.96 |

### 768D

| Encoder | Bits | 500 candidates |
| --- | ---: | --- |
| `coordinate_sign` | 768 | 0.8340 / 0.88 |
| `random_hyperplane_rademacher` | 256 | 0.4160 / 0.48 |
| `random_hyperplane_rademacher` | 512 | 0.5960 / 0.62 |
| `random_hyperplane_rademacher` | 768 | 0.6820 / 0.74 |
| `random_hyperplane_rademacher` | 1024 | 0.7720 / 0.84 |
| `randomized_hadamard_projection` | 256 | 0.4680 / 0.44 |
| `randomized_hadamard_projection` | 512 | 0.6560 / 0.68 |
| `randomized_hadamard_projection` | 768 | 0.7960 / 0.90 |
| `randomized_hadamard_projection` | 1024 | 0.9000 / 1.00 |
| `learned_pair_difference_projection` | 256 | 0.3780 / 0.46 |
| `learned_pair_difference_projection` | 512 | 0.5180 / 0.52 |
| `learned_pair_difference_projection` | 768 | 0.6480 / 0.70 |
| `learned_pair_difference_projection` | 1024 | 0.7060 / 0.84 |

### 1536D

| Encoder | Bits | 500 candidates |
| --- | ---: | --- |
| `coordinate_sign` | 1536 | 0.8280 / 0.94 |
| `random_hyperplane_rademacher` | 512 | 0.3900 / 0.38 |
| `random_hyperplane_rademacher` | 1024 | 0.5520 / 0.56 |
| `random_hyperplane_rademacher` | 1536 | 0.6960 / 0.76 |
| `random_hyperplane_rademacher` | 2048 | 0.7600 / 0.84 |
| `randomized_hadamard_projection` | 512 | 0.4480 / 0.38 |
| `randomized_hadamard_projection` | 1024 | 0.6000 / 0.66 |
| `randomized_hadamard_projection` | 1536 | 0.7620 / 0.90 |
| `randomized_hadamard_projection` | 2048 | 0.8820 / 0.98 |
| `learned_pair_difference_projection` | 512 | 0.3940 / 0.36 |
| `learned_pair_difference_projection` | 1024 | 0.5400 / 0.60 |
| `learned_pair_difference_projection` | 1536 | 0.6240 / 0.76 |
| `learned_pair_difference_projection` | 2048 | 0.7080 / 0.84 |

## Coverage at 1000 candidates

| Dimension | Best flexible projection | Coverage / top-1 | Coordinate-sign diagnostic | Coverage / top-1 |
| ---: | --- | --- | --- | --- |
| 384 | Hadamard, 1024 bits | 1.0000 / 1.00 | 384 bits | 0.9400 / 0.96 |
| 768 | Hadamard, 1024 bits | 0.9760 / 1.00 | 768 bits | 0.9180 / 0.98 |
| 1536 | Hadamard, 2048 bits | 0.9620 / 1.00 | 1536 bits | 0.9380 / 0.98 |

The best learned pair-difference results at 1000 candidates were:

| Dimension | Bits | Coverage / top-1 |
| ---: | ---: | --- |
| 384 | 1024 | 0.9500 / 1.00 |
| 768 | 1024 | 0.8720 / 0.94 |
| 1536 | 2048 | 0.8440 / 0.92 |

## Interpretation

The 128D result does not transfer unchanged to higher dimensions.

Randomized Hadamard remains the strongest flexible projection in this synthetic
suite, but the useful bit/candidate band shifts upward:

- 384D can still reach near-exact candidate coverage with 1024 bits and 500
  candidates.
- 768D needs 1000 candidates for the 1024-bit Hadamard code to recover
  `0.9760` exact-top-k coverage.
- 1536D needs 2048 bits and 1000 candidates to reach `0.9620` coverage.

`coordinate_sign` is surprisingly strong at `bit_count == input_dimension`,
especially at 768D and 1536D, but it remains a diagnostic baseline rather than
a general bit-budget mechanism: it cannot choose 512 bits for a 1536D source
without becoming a different coordinate-subset encoder.

The simple learned pair-difference baseline still does not beat Hadamard. It
improves as the bit budget grows, but it lags the structured zero-training
projection at the same candidate budget. The next learned candidate should be
PCA/ITQ-style or otherwise optimize a real objective, not just select
deterministic far-pair directions.

## Timing caveat

The 1536D local run showed large timing outliers in a few repeat rows. Therefore
this note uses coverage as the main PR61 signal and treats speedups as
directional only. A production decision needs a separate benchmark harness with
repeated process-level runs, interleaved exact/binary epochs, and better thermal
control.

## Follow-up

- Add a stronger global learned projection: PCA/ITQ-style rotation or another
  objective-based data-dependent hashing baseline.
- Run the same candidate-filter experiment on real embeddings and qrels.
- Consider larger bit budgets for 768D/1536D before concluding that binary
  filtering is weak in high dimensions.
- Keep exact dense rerank in the system shape; direct binary top-k is still not
  established as a final ranker.
