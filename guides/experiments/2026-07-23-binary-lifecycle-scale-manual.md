# Binary lifecycle manual scale runs

## 2026-07-23 / PR #82

Question: once the PR #81 smoke gate has fixed the lifecycle report contract,
where do the current exact vector, flat binary, and multi-probe binary paths
start to separate by scale?

Setup:

- command: `agent-memory-binary-lifecycle-bench`
- configs:
  - `tools/agent-memory-bench/binary-lifecycle-10k.manual.json`
  - `tools/agent-memory-bench/binary-lifecycle-100k.manual.json`
- synthetic corpus: normalized vectors generated from a fixed seed
- queries: noisy copies of corpus vectors
- vector dimension: 64
- binary encoder: `random_hyperplane_rademacher` v2
- bit count: 128
- result limit: 10
- reported overlap: fraction of exact top-10 documents present in the returned
  Hamming top-10 results. This is **not** bucket-union recall; the current
  report does not expose whether exact neighbours were present in the internal
  multi-probe candidate union before Hamming top-k truncation.
- indexes:
  - `ExactVectorIndex` as exact float oracle;
  - `FlatBinarySignatureIndex` as exact Hamming flat-scan baseline;
  - `MultiProbeHammingIndex` as the first bucketed candidate-generation
    baseline.

Expected result:

- 10k should show whether flat binary is still competitive before a production
  sub-linear binary index is worth building.
- 100k should start showing whether the current multi-probe bucket overhead can
  pay for itself against flat Hamming scan.
- These are manual directional runs, not CI acceptance thresholds. The generator
  uses one RNG stream for corpus generation and query-noise generation, so the
  10k and 100k runs are not a strict same-query nested comparison.

Commands:

```powershell
cmake --build tmp\build-pr81-mingw --target agent-memory-binary-lifecycle-bench --parallel 1
tmp\build-pr81-mingw\tools\agent-memory-bench\agent-memory-binary-lifecycle-bench.exe `
  tools\agent-memory-bench\binary-lifecycle-10k.manual.json `
  tmp\build-pr81-mingw\binary-lifecycle-10k-manual.json
tmp\build-pr81-mingw\tools\agent-memory-bench\agent-memory-binary-lifecycle-bench.exe `
  tools\agent-memory-bench\binary-lifecycle-100k.manual.json `
  tmp\build-pr81-mingw\binary-lifecycle-100k-manual.json
```

Actual 10k single run:

| Index | Build ms | Query mean ms | Returned top-10 overlap | Erase ms | Upsert ms | Rebuild ms | Payload bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| ExactVectorIndex | 32.292 | 3.387 | 1.000 oracle | n/a | n/a | n/a | 2,560,000 |
| FlatBinarySignatureIndex | 68.642 | 0.840 | 0.221 | 1.266 | 1.546 | 54.014 | 160,000 |
| MultiProbeHammingIndex | 95.248 | 0.452 | 0.216 | 4.139 | 1.950 | 92.934 | 160,000 |

10k multi-probe diagnostics and process memory:

| Mean candidates | Mean probed buckets | Mean visited postings | Process peak RSS bytes |
|---:|---:|---:|---:|
| 2,595.238 | 72.000 | 3,056.852 | 29,958,144 |

Actual 100k single run:

| Index | Build ms | Query mean ms | Returned top-10 overlap | Erase ms | Upsert ms | Rebuild ms | Payload bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| ExactVectorIndex | 365.961 | 40.350 | 1.000 oracle | n/a | n/a | n/a | 25,600,000 |
| FlatBinarySignatureIndex | 610.382 | 7.797 | 0.168 | 5.392 | 6.268 | 612.986 | 1,600,000 |
| MultiProbeHammingIndex | 971.675 | 0.582 | 0.138 | 56.979 | 9.603 | 985.000 | 1,600,000 |

100k multi-probe diagnostics and process memory:

| Mean candidates | Mean probed buckets | Mean visited postings | Process peak RSS bytes |
|---:|---:|---:|---:|
| 3,600.793 | 8.000 | 3,687.484 | 231,358,464 |

Interpretation:

- The overlap column is returned Hamming top-10 overlap with exact top-10, not
  bucket-union recall and not final retrieval quality. Exact reranking is still
  required for production retrieval.
- In these single-run configs, returned top-10 overlap is lower at 100k than at
  10k. Because query-noise samples are not isolated by an independent RNG
  stream, treat this as observed directional behaviour rather than a strict
  scale-only causal claim.
- Payload bytes are raw hot payload estimates only; allocator overhead,
  per-record IDs, buckets, and persisted storage layout are separate costs.
- Process peak RSS is process-wide after corpus, records, and all indexes have
  coexisted. It is useful as a rough run envelope, not as per-backend memory.
- Multi-probe diagnostics (`mean_candidate_count`,
  `mean_probed_bucket_count`, `mean_visited_posting_count`) should guide
  whether to tune the current prototype or move to MIH/HNSW-Hamming/IVF-like
  designs.
- At 10k records the bucketed prototype is already faster than flat binary
  search on the query loop, but the difference is modest. At 100k records the
  gap is large: multi-probe is about 13x faster than flat binary query search
  in this single run.
- The 10k -> 100k query-time jump is also a probe-regime change, not just a
  smooth scale effect. With eight tables and eight bits per table, probing exact
  buckets plus radius-1 neighbours means up to `8 + 8 * 8 = 72` bucket lookups.
  At 10k the index needed that radius-1 pass; at 100k the exact buckets alone
  already exceeded the candidate target, so the run probed only eight buckets.
- The speedup is not yet a production retrieval win because the 128-bit,
  top-10-only returned overlap is low. The next quality-oriented scale run
  should use overfetch/rerank candidate limits and wider bit budgets instead of
  treating binary top-10 as the final candidate set.
- Multi-probe erase is materially more expensive than flat erase in this
  prototype because bucket postings must be maintained. Update/delete lifecycle
  cost is therefore part of the backend decision, not a side issue.

Next checks:

- Repeat each scale after warm-up and report p50/p95 query timing.
- Add 256-bit and 512-bit variants if 128-bit coverage is too low.
- Add independent RNG streams for corpus vectors and query noise before making
  strict same-query scale claims.
- Extend the benchmark contract before the overfetch/rerank grid:
  - `oracle_k`, fixed at the exact-neighbour target such as 10;
  - `returned_candidate_limit`, e.g. 10/32/64/128/256/512;
  - independent multi-probe candidate/probe budget, so output overfetch does not
    automatically change bucket search depth.
- Report `candidate_recall@oracle_k`, `reranked_recall@oracle_k`,
  binary search time, exact rerank time, and end-to-end time.
- Measure persisted reload/cold start once a binary index storage contract
  exists.
