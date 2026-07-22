# Binary index scale and lifecycle gate

## 2026-07-23 / PR #81

Question: before choosing a production binary index direction, can CI enforce a
small benchmark report that exposes the lifecycle fields we need for larger
manual scale runs?

Setup:

- command: `agent-memory-binary-lifecycle-bench`
- config: `tools/agent-memory-bench/binary-lifecycle-smoke.example.json`
- synthetic corpus: 256 normalized vectors
- queries: 64 noisy copies of corpus vectors
- vector dimension: 64
- binary encoder: `random_hyperplane_rademacher` v2
- bit count: 128
- result limit: 10
- mutation batch: erase and re-upsert 16 records
- indexes:
  - `ExactVectorIndex` as exact float oracle;
  - `FlatBinarySignatureIndex` as exact Hamming flat-scan baseline;
  - `MultiProbeHammingIndex` as the first bucketed candidate-generation
    baseline.

Expected result:

- The smoke gate should not decide the production backend.
- It should guarantee that future reports contain build/query/mutation/rebuild,
  payload, backend, and candidate-coverage fields.
- On this tiny corpus, `MultiProbeHammingIndex` may not beat flat Hamming scan
  because bucket and dedup overhead can dominate before scale.

Actual single smoke run:

| Index | Build ms | Query mean ms | Exact top-10 coverage | Erase ms | Upsert ms | Rebuild ms | Payload bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| ExactVectorIndex | 0.653 | 0.102 | 1.000 oracle | n/a | n/a | n/a | 65,536 |
| FlatBinarySignatureIndex | 1.224 | 0.037 | 0.414 | 0.042 | 0.057 | 1.113 | 4,096 |
| MultiProbeHammingIndex | 2.734 | 0.045 | 0.381 | 0.160 | 0.111 | 2.737 | 4,096 |

Multi-probe diagnostics:

| Mean candidates | Mean probed buckets | Mean visited postings |
|---:|---:|---:|
| 67.875 | 72.000 | 83.516 |

Interpretation:

- The committed CI gate is about report shape and lifecycle observability, not
  stable benchmark evidence.
- Binary payload is 16x smaller than the float payload in this setup
  (`128` bits vs `64` float32 values per record).
- Flat exact Hamming is faster than the current exact float oracle in this
  small run, but quality is candidate coverage only and still requires exact
  reranking for final retrieval.
- Multi-probe does not win at this size. That is useful: bucketed indexes should
  be judged at larger corpus sizes and with tuned table/radius/candidate
  budgets, not by a tiny smoke fixture.

Limitations:

- Single seed and tiny corpus.
- Synthetic vectors are cleaner than real embeddings.
- Timings are from one local run and are directional.
- The gate measures in-memory rebuild after `clear()`, not persisted reload
  from storage. A future persistence-aware benchmark should add reload/cold
  start once a binary index storage contract exists.

Next checks:

- Add larger manual configs for 10k, 100k, and eventually 1M records.
- Report p50/p95 over repeated runs after warm-up.
- Add exact rerank timing on binary candidates.
- Compare tuned multi-probe, MIH-style partitioning, and HNSW-Hamming once the
  scale gate shows where flat scan loses.
