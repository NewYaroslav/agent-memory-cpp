# 2026-07-21: PCA-style global binary projection baseline

## Question

Does a simple objective-based learned projection beat the earlier zero-training
and pair-difference learned baselines on the 128D synthetic binary rerank grid?

PR #60 showed that deterministic pair-difference directions do not beat
Randomized Hadamard. PR #62 adds a dependency-free PCA-style encoder that
centers document vectors, learns principal axes, and uses median projection
thresholds. This is a global shared code space and uses document vectors only;
evaluation queries are not training input.

## Expected result

We expected PCA to improve over independent random hyperplanes and the
pair-difference learned baseline, especially at low bit counts, because PCA
selects high-variance directions instead of arbitrary directions. We did not
expect it to be a final learned-hashing answer: it does not perform ITQ-style
rotation, quantization-loss optimization, supervised metric learning, or
autoencoder training.

## Configuration

- PR context: PR #62.
- Synthetic data only; no real embedding/qrels fixture is used.
- 10,000 documents and 100 queries.
- 128-dimensional clustered normalized vectors.
- Result limit: `10`.
- Data seed: `42`.
- Encoder seeds: `1001`, `1002` for seedable families.
- Encoder families:
  - `random_hyperplane_rademacher`;
  - `coordinate_sign`;
  - `randomized_hadamard_projection`;
  - `learned_pair_difference_projection`;
  - `pca_projection`.
- Bit counts: `32`, `64`, `128`.
- Candidate limits: `100`, `500`, `1000`, `2000`.
- Binary timing repeats: `3`.
- Exact timing repeats: `5`.

Command:

```bash
./build-codex-pr59/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-pca.example.json \
    tmp/synthetic-binary-rerank-grid-pca-pr62.json
```

## Result

Each cell is
`exact-top-k candidate coverage / top-1 agreement / speedup vs current index / speedup vs contiguous exact`.

### 500 candidates

| Encoder | Bits | Result |
| --- | ---: | --- |
| `random_hyperplane_rademacher` | 32 | 0.2955 / 0.340 / 2.70x / 1.29x |
| `randomized_hadamard_projection` | 32 | 0.2920 / 0.335 / 2.80x / 1.33x |
| `learned_pair_difference_projection` | 32 | 0.2865 / 0.365 / 2.70x / 1.29x |
| `pca_projection` | 32 | 0.3560 / 0.430 / 3.03x / 1.44x |
| `random_hyperplane_rademacher` | 64 | 0.4260 / 0.500 / 2.74x / 1.31x |
| `randomized_hadamard_projection` | 64 | 0.4800 / 0.590 / 2.86x / 1.37x |
| `learned_pair_difference_projection` | 64 | 0.4245 / 0.510 / 2.74x / 1.31x |
| `pca_projection` | 64 | 0.5475 / 0.660 / 2.89x / 1.38x |
| `coordinate_sign` | 128 | 0.7800 / 0.910 / 2.69x / 1.28x |
| `random_hyperplane_rademacher` | 128 | 0.6260 / 0.685 / 2.87x / 1.37x |
| `randomized_hadamard_projection` | 128 | 0.7860 / 0.890 / 2.78x / 1.32x |
| `learned_pair_difference_projection` | 128 | 0.6160 / 0.750 / 2.83x / 1.35x |
| `pca_projection` | 128 | 0.7670 / 0.860 / 2.72x / 1.30x |

### 1000 candidates

| Encoder | Bits | Result |
| --- | ---: | --- |
| `random_hyperplane_rademacher` | 32 | 0.4275 / 0.500 / 1.56x / 0.75x |
| `randomized_hadamard_projection` | 32 | 0.4290 / 0.480 / 1.60x / 0.76x |
| `learned_pair_difference_projection` | 32 | 0.4325 / 0.500 / 1.55x / 0.74x |
| `pca_projection` | 32 | 0.5200 / 0.590 / 1.66x / 0.79x |
| `random_hyperplane_rademacher` | 64 | 0.5830 / 0.680 / 1.58x / 0.75x |
| `randomized_hadamard_projection` | 64 | 0.6325 / 0.745 / 1.47x / 0.70x |
| `learned_pair_difference_projection` | 64 | 0.5885 / 0.710 / 1.56x / 0.74x |
| `pca_projection` | 64 | 0.6985 / 0.790 / 1.47x / 0.70x |
| `coordinate_sign` | 128 | 0.8800 / 0.960 / 1.48x / 0.70x |
| `random_hyperplane_rademacher` | 128 | 0.7795 / 0.850 / 1.58x / 0.75x |
| `randomized_hadamard_projection` | 128 | 0.8965 / 0.955 / 1.55x / 0.74x |
| `learned_pair_difference_projection` | 128 | 0.7705 / 0.880 / 1.63x / 0.77x |
| `pca_projection` | 128 | 0.8745 / 0.925 / 1.53x / 0.73x |

## Interpretation

PCA projection is a useful intermediate baseline but not a decisive winner.

At 32 and 64 bits it clearly beats random hyperplanes and the pair-difference
learned projection on candidate coverage. This supports the hypothesis that an
objective-based learned projection is more meaningful than arbitrary or
far-pair directions at low bit budgets.

At 128 bits, however, PCA is slightly behind both `coordinate_sign` and
`randomized_hadamard_projection` in this synthetic fixture:

- 500 candidates: PCA `0.7670`, coordinate sign `0.7800`, Hadamard `0.7860`;
- 1000 candidates: PCA `0.8745`, coordinate sign `0.8800`, Hadamard `0.8965`.

So the result does not justify replacing Hadamard as the strongest current
baseline. The next learned candidate should add a real quantization objective:
PCA + ITQ-style rotation, supervised/data-dependent hashing, or an autoencoder.

## Limitations

- One synthetic dataset and one data seed.
- Two encoder seeds.
- 128D only.
- PCA supports only `bit_count <= input_dimension`.
- PCA training time is not reported separately yet; query/build timings exclude
  training cost.
- No real embeddings or qrels.

## Follow-up

- Implement benchmark fields for learned training cost:
  `encoder_training_ms`, `training_vector_count`, and artifact byte size.
- Add PCA + ITQ-style rotation as a separately named encoder family.
- Run real-embedding/qrels experiments before making production choices.
