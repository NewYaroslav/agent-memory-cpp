# Binary lifecycle overfetch and exact rerank

## 2026-07-23 / PR #84

Question: can the binary lifecycle benchmark measure binary overfetch followed
by exact float rerank while keeping the exact oracle target fixed?

Why this matters:

- PR #83 split `oracle_k` from the returned binary candidate count.
- The next practical retrieval question is not whether binary top-10 equals
  exact top-10. It is whether binary search can return a wider candidate set
  cheaply enough for exact float rerank to recover useful exact neighbours.
- Multi-probe Hamming search also has an internal bucket-search regime, so the
  report must expose per-limit diagnostics instead of only final quality.

Setup:

- command: `agent-memory-binary-lifecycle-bench`
- configs:
  - `tools/agent-memory-bench/binary-lifecycle-10k.manual.json`
  - `tools/agent-memory-bench/binary-lifecycle-100k.manual.json`
- synthetic normalized vectors;
- vector dimension: 64;
- binary encoder: `random_hyperplane_rademacher` v2;
- bit count: 128;
- exact oracle target: `oracle_k = 10`;
- baseline returned candidate limit: 10;
- rerank candidate limits: `10`, `32`, `64`, `128`, `256`, `512`;
- independent query noise seed inherited from PR #83.

Contract change:

- The report now contains `rerank_candidate_limits`.
- `flat_binary.rerank` and `multiprobe_binary.rerank` contain one row per
  candidate limit.
- Each rerank row reports:
  - binary search timing;
  - returned binary result count;
  - exact top-k candidate coverage;
  - exact rerank timing;
  - reranked recall vs exact top-k;
  - reranked top-1 agreement;
  - binary-search + exact-rerank end-to-end timing.
- Multi-probe rerank rows additionally report per-limit:
  - mean unique candidate count;
  - mean probed bucket count;
  - mean visited posting count.

The CTest smoke gate keeps a smaller grid `16`, `32`, `64` and validates the
rerank report structure without pinning directional timing or quality values.

Commands:

```powershell
cmake --build tmp\build-pr81-mingw --target agent-memory-binary-lifecycle-bench --parallel 1
tmp\build-pr81-mingw\tools\agent-memory-bench\agent-memory-binary-lifecycle-bench.exe `
  tools\agent-memory-bench\binary-lifecycle-10k.manual.json `
  tmp\build-pr81-mingw\binary-lifecycle-10k-pr84.json
tmp\build-pr81-mingw\tools\agent-memory-bench\agent-memory-binary-lifecycle-bench.exe `
  tools\agent-memory-bench\binary-lifecycle-100k.manual.json `
  tmp\build-pr81-mingw\binary-lifecycle-100k-pr84.json
```

Baseline snapshot:

| Scale | Exact query mean ms | Flat binary mean ms | Multi-probe mean ms | Flat returned top-10 overlap | Multi-probe returned top-10 overlap |
|---:|---:|---:|---:|---:|---:|
| 10k | 3.126 | 0.748 | 0.393 | 0.219 | 0.218 |
| 100k | 40.237 | 7.744 | 0.589 | 0.166 | 0.139 |

Build/lifecycle snapshot:

| Scale | Exact build ms | Flat build ms | Multi-probe build ms | Process peak RSS bytes |
|---:|---:|---:|---:|---:|
| 10k | 34.649 | 53.665 | 92.834 | 42,885,120 |
| 100k | 361.916 | 613.267 | 980.388 | 245,776,384 |

Flat binary overfetch + exact rerank:

| Scale | Candidates | Coverage@10 | Reranked top-1 agreement | Binary search mean ms | Exact rerank mean ms | End-to-end mean ms |
|---:|---:|---:|---:|---:|---:|---:|
| 10k | 10 | 0.219 | 1.000 | 0.776 | 0.013 | 0.789 |
| 10k | 32 | 0.341 | 1.000 | 0.764 | 0.030 | 0.794 |
| 10k | 64 | 0.444 | 1.000 | 0.806 | 0.066 | 0.872 |
| 10k | 128 | 0.564 | 1.000 | 0.873 | 0.085 | 0.958 |
| 10k | 256 | 0.697 | 1.000 | 0.995 | 0.152 | 1.147 |
| 10k | 512 | 0.820 | 1.000 | 1.247 | 0.295 | 1.541 |
| 100k | 10 | 0.166 | 1.000 | 7.781 | 0.015 | 7.795 |
| 100k | 32 | 0.244 | 1.000 | 7.818 | 0.034 | 7.852 |
| 100k | 64 | 0.303 | 1.000 | 7.597 | 0.058 | 7.654 |
| 100k | 128 | 0.386 | 1.000 | 7.496 | 0.100 | 7.596 |
| 100k | 256 | 0.488 | 1.000 | 7.641 | 0.188 | 7.828 |
| 100k | 512 | 0.600 | 1.000 | 8.447 | 0.362 | 8.809 |

Multi-probe overfetch + exact rerank:

| Scale | Candidates | Coverage@10 | Reranked top-1 agreement | Binary search mean ms | Exact rerank mean ms | End-to-end mean ms | Mean bucket candidates | Mean probed buckets |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 10k | 10 | 0.218 | 1.000 | 0.412 | 0.014 | 0.425 | 2,604.4 | 72.0 |
| 10k | 32 | 0.336 | 1.000 | 0.440 | 0.028 | 0.468 | 2,604.4 | 72.0 |
| 10k | 64 | 0.425 | 1.000 | 0.507 | 0.051 | 0.558 | 2,604.4 | 72.0 |
| 10k | 128 | 0.524 | 1.000 | 0.674 | 0.083 | 0.757 | 2,604.4 | 72.0 |
| 10k | 256 | 0.623 | 1.000 | 0.869 | 0.158 | 1.028 | 2,604.4 | 72.0 |
| 10k | 512 | 0.695 | 1.000 | 1.311 | 0.298 | 1.610 | 2,604.4 | 72.0 |
| 100k | 10 | 0.139 | 0.844 | 0.564 | 0.015 | 0.579 | 3,578.6 | 8.0 |
| 100k | 32 | 0.185 | 0.844 | 0.625 | 0.033 | 0.659 | 3,645.8 | 8.2 |
| 100k | 64 | 0.290 | 0.984 | 3.862 | 0.072 | 3.933 | 22,525.0 | 62.8 |
| 100k | 128 | 0.378 | 1.000 | 4.944 | 0.108 | 5.052 | 26,038.3 | 72.0 |
| 100k | 256 | 0.479 | 1.000 | 4.995 | 0.191 | 5.186 | 26,038.3 | 72.0 |
| 100k | 512 | 0.568 | 1.000 | 5.668 | 0.371 | 6.039 | 26,038.3 | 72.0 |

Interpretation:

- Exact rerank does what it should: `reranked_recall_at_k_vs_exact` equals
  candidate coverage because the reranker uses the same exact float scorer as
  the oracle.
- Top-1 is much easier than top-10 on this fixture. Flat keeps top-1 agreement
  at `1.000` even when top-10 coverage is low; multi-probe reaches `1.000` at
  100k once overfetch reaches 128 candidates.
- At 10k, multi-probe already probes the full radius-1 budget for all tested
  candidate limits, so overfetch mostly changes final Hamming truncation and
  rerank cost.
- At 100k, multi-probe has a visible regime switch:
  - 10/32 candidates stop after roughly eight buckets;
  - 64 candidates expand to about 63 buckets;
  - 128+ candidates hit the full 72-bucket radius-1 budget.
- Rerank cost is small compared with binary search at these candidate limits.
  The bottleneck is candidate generation/search, not exact reranking.
- 128-bit random-hyperplane signatures are still not enough for high top-10
  recall on this synthetic fixture. Overfetch helps, but even 512 candidates
  reaches only `0.600` flat and `0.568` multi-probe coverage at 100k.
- Multi-probe can still be a useful latency filter at 100k. For example,
  128 candidates gives `0.378` coverage with about `5.052 ms/query`, versus
  exact full scan at about `40.237 ms/query`. That is a candidate-filter result,
  not a final retrieval-quality win.

Caveats:

- Single local run, one seed, synthetic normalized vectors.
- Process peak RSS is whole-process high-water memory, not per-backend memory.
- Multi-probe search depth is still indirectly tied to returned candidate limit
  through `max(minimum_candidate_count, limit * candidate_multiplier)`.
- The exact reranker currently reranks only returned Hamming results; this note
  does not yet measure raw bucket-union recall before Hamming truncation.

Next checks:

- Add an explicit multi-probe probe/candidate budget independent from returned
  output limit.
- Report raw bucket-union recall separately from returned Hamming candidate
  coverage.
- Run bit-count grids such as 128/256/512/1024 at fixed overfetch limits.
- Repeat with real/precomputed embedding fixtures once the synthetic contract is
  stable.
