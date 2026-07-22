# Precomputed embedding qrels gate

## 2026-07-21 — PR #66 smoke gate

### What we checked

PR #66 adds the first deterministic path for evaluating binary candidate
retrieval on frozen dense embeddings with locked qrels:

- load a JSON fixture containing corpus/query records, qrels, embedding model
  metadata, document embeddings, and query embeddings;
- build an exact vector oracle over the frozen document embeddings;
- build binary signatures with configured encoder families;
- search binary candidates, exact-rerank those candidates with the source
  float embeddings, and evaluate the reranked run against qrels.

The smoke fixture is intentionally tiny. It tests the evaluation plumbing, not
production retrieval quality.

### Why it matters

The earlier binary experiments were synthetic and mostly used exact-float
top-k preservation as the quality proxy. That is useful for measuring candidate
loss, but it does not prove that the pipeline can consume real embedding
artifacts or report qrels-based retrieval metrics.

This gate creates the missing bridge:

```text
frozen embeddings + locked qrels
    -> exact vector oracle
    -> binary candidate filter
    -> exact float rerank
    -> shared RetrievalMetrics
```

Embedding generation remains outside the C++ test run, so future fixtures can
use OpenAI, SentenceTransformers, E5/BGE, or any other provider without adding
network/API dependencies to CI.

### Expected result

For the committed smoke fixture we expect:

- exact vector search recovers all positive qrels within the configured
  evaluation depth;
- every binary encoder report includes qrels metrics after exact rerank;
- candidate limits eventually recover the exact top-k when the candidate limit
  reaches the full corpus size;
- trained encoders report document-vector training metadata, and query vectors
  are not training input.

### Setup

Local verification command:

```bash
./tmp/build-pr66-mingw/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/precomputed-embedding-binary-rerank-grid-smoke.example.json \
    tmp/build-pr66-mingw/precomputed-embedding-binary-rerank-grid-ctest.json
```

Fixture:

- dataset: `tests/eval/fixtures/precomputed-embedding-tiny.json`;
- corpus size: `6`;
- query count: `3`;
- embedding dimension: `4`;
- metric: cosine;
- result limit: `3`;
- bit counts: `4`, `8`;
- candidate limits: `3`, `4`, `6`;
- encoder seed: `42`;
- encoder families:
  - `random_hyperplane_rademacher`;
  - `coordinate_sign`;
  - `pca_projection`;
  - `itq_rotation_projection`.

### Actual result

Exact vector oracle quality against qrels:

| Metric | Value |
| --- | ---: |
| Recall@1 | 0.6111 |
| Recall@10 | 1.0000 |
| nDCG@10 | 1.0000 |
| MRR | 1.0000 |

Representative binary rerank smoke rows:

| Encoder | Bits | Candidates | Exact top-k candidate coverage | Qrels candidate coverage | Reranked Recall@10 | Reranked nDCG@10 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| random hyperplane | 4 | 3 | 0.6667 | 1.0000 | 1.0000 | 1.0000 |
| random hyperplane | 8 | 3 | 0.7778 | 1.0000 | 1.0000 | 1.0000 |
| coordinate sign | 4 | 3 | 0.8889 | 1.0000 | 1.0000 | 1.0000 |
| PCA projection | 4 | 3 | 0.7778 | 1.0000 | 1.0000 | 1.0000 |
| ITQ rotation | 4 | 3 | 0.6667 | 1.0000 | 1.0000 | 1.0000 |
| all supported encoders | supported | 6 | 1.0000 | 1.0000 | 1.0000 | 1.0000 |

The exact-top-k coverage and qrels coverage intentionally differ: exact top-k
is a dense-neighbour preservation proxy, while qrels coverage answers whether
the judged relevant items survived the candidate stage. On this tiny fixture,
candidate limit `3` already preserves all positive qrels even when it misses
some exact-neighbour positions.

### Interpretation

The PR establishes a useful regression gate:

- real/precomputed embedding artifacts can be loaded and validated;
- document/query embedding coverage is identity-checked against the retrieval
  dataset;
- binary candidate retrieval can be evaluated against qrels after exact rerank;
- future real-embedding fixtures can be added without changing the benchmark
  architecture.

Do not interpret the smoke numbers as evidence that any encoder family is
better in production. The fixture is too small and deliberately axis-aligned.

### Limitations

- Tiny hand-authored fixture, not a real corpus.
- Single encoder seed.
- Single local run; timings are only a smoke signal.
- No externally generated embedding model fixture is committed yet.
- No held-out training/evaluation split beyond the rule that trained encoders
  use document vectors only and never query vectors.

### What to check next

- Add a medium or real externally generated fixture.
- Add a common-baseline report comparing synthetic conclusions with real
  embedding qrels.
- Keep qrels metrics and exact-top-k candidate coverage separate in all future
  reports.

## 2026-07-22 — PR #68 medium fixture and artifact provenance gate

### What we checked

PR #68 keeps embedding generation outside the C++ test run, but adds the
missing provenance surface for frozen embedding artifacts:

- optional `embedding_artifact` metadata in precomputed embedding datasets;
- validation that all provenance fields are non-empty when the block exists;
- propagation of artifact provenance into benchmark JSON reports;
- a medium hand-authored semantic-axis fixture with more documents, more
  queries, and enough structure for encoder/candidate settings to differ;
- a dedicated CTest smoke gate for that medium fixture.

The medium fixture is deliberately **not** described as output from a real
external embedding model. It is a frozen semantic-axis artifact used to test the
precomputed benchmark mechanics without network/model dependencies in CI.

### Setup

Local verification command:

```bash
./tmp/build-pr68/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/precomputed-embedding-binary-rerank-grid-medium.example.json \
    tmp/build-pr68/precomputed-embedding-binary-rerank-grid-medium-ctest.json
```

Fixture:

- dataset: `tests/eval/fixtures/precomputed-embedding-medium.json`;
- corpus size: `12`;
- query count: `4`;
- embedding dimension: `8`;
- artifact projection kind: `semantic_axes_8d`;
- metric: cosine;
- result limit: `5`;
- bit counts: `4`, `8`;
- candidate limits: `5`, `8`, `12`;
- encoder seed: `42`;
- encoder families:
  - `random_hyperplane_rademacher`;
  - `coordinate_sign`;
  - `pca_projection`;
  - `itq_rotation_projection`.

### Actual result

Exact vector oracle quality against qrels:

| Metric | Value |
| --- | ---: |
| Recall@10 | 1.0000 |
| nDCG@10 | 1.0000 |
| MRR | 1.0000 |

Representative binary rerank rows from one local CTest run:

| Encoder | Bits | Candidates | Exact top-k candidate coverage | Qrels candidate coverage | Reranked Recall@10 | Reranked nDCG@10 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| random hyperplane | 4 | 5 | 0.6000 | 0.6458 | 0.6458 | 0.8049 |
| random hyperplane | 8 | 5 | 0.6500 | 0.7083 | 0.7083 | 0.9162 |
| coordinate sign | 8 | 5 | 0.8000 | 1.0000 | 1.0000 | 1.0000 |
| PCA projection | 4 | 5 | 0.7000 | 0.7917 | 0.7917 | 0.9523 |
| PCA projection | 8 | 5 | 0.6500 | 0.7292 | 0.7292 | 0.9191 |
| ITQ rotation | 4 | 5 | 0.7000 | 0.8542 | 0.8542 | 0.9610 |
| ITQ rotation | 8 | 5 | 0.7000 | 0.8542 | 0.8542 | 0.9622 |
| ITQ rotation | 4 | 8 | 0.9000 | 1.0000 | 1.0000 | 1.0000 |
| ITQ rotation | 8 | 8 | 0.8500 | 1.0000 | 1.0000 | 1.0000 |
| all supported encoders | supported | 12 | 1.0000 | 1.0000 | 1.0000 | 1.0000 |

Training diagnostics are present in the report. Zero-training families report
zero training cost and zero artifact payload; PCA/ITQ report document-vector
training metadata. In this tiny medium fixture the measured local training
times are sub-millisecond to a few milliseconds, so they are useful only as
schema coverage, not performance evidence.

### Interpretation

This PR strengthens the regression gate in three ways:

- benchmark output can now be traced to a frozen embedding artifact identity;
- medium fixture rows are no longer all trivially equal at the smallest
  candidate limit, so the gate can catch more ranking/candidate regressions;
- full-corpus candidate rows still prove that exact rerank and qrels evaluation
  recover the oracle when candidate generation is not lossy.

The result does **not** settle production encoder choice. Coordinate sign looks
strong on this axis-aligned fixture because the fixture was built around stable
semantic axes; that is a useful diagnostic, not a claim about real embeddings.

### What to check next

- Add an externally generated embedding fixture with the same provenance fields.
- Preserve generator/model/config revisions, dataset revision, qrels revision,
  prompt identities, config hash, and artifact hash for every real fixture.
- Compare synthetic, hand-authored semantic-axis, and external-model fixtures
  side by side before making backend decisions.

## 2026-07-22 — PR #69 provenance contract hardening

### What we checked

PR #69 tightens the provenance shape before committing any external-model
embedding artifact:

- one ambiguous `source_revision` is split into `dataset_revision`,
  `generator_revision`, `model_revision`, and `qrels_revision`;
- asymmetric embedding setups get explicit `document_prompt_id` and
  `query_prompt_id`;
- `normalization`, `dtype`, and `hash_algorithm` are part of artifact identity;
- `config_hash` and `artifact_hash` are validated as lowercase 64-character
  SHA-256 hex strings;
- CTest validates that the complete provenance object survives
  dataset-load -> benchmark-run -> report-JSON serialization.

### Canonical hash scope

`config_hash` is the SHA-256 digest of the canonical generator configuration:

```text
model id/revision
document/query prompt identities
normalization and dtype
pooling/truncation
projection transform
random seed
other behavior-affecting generator settings
```

`artifact_hash` is the SHA-256 digest of the canonical embedding payload before
any self-referential provenance wrapper is added. For JSON fixtures this means
the ordered document/query embedding records with ids and float values, not the
whole JSON file containing `embedding_artifact.artifact_hash` itself.

### Interpretation

This PR is intentionally a contract hardening PR, not a new quality benchmark.
It prevents the next external fixture from becoming a pile of plausible-looking
metadata whose hashes and revisions cannot be reproduced.

### What to check next

- Add a verifier that recomputes `config_hash` and `artifact_hash` from the
  canonicalized config and payload.
- Keep real-model conclusions separate from hand-authored semantic-axis fixture
  conclusions.

## 2026-07-22 — PR #70 artifact hash verifier gate

### What we checked

PR #70 adds the missing integrity check for committed precomputed embedding
fixtures. The loader still validates schema/coverage, but CI now also runs a
separate verifier that:

- canonicalizes the generator-config identity fields;
- canonicalizes ordered document/query embedding records;
- recomputes SHA-256 for both canonical texts;
- rejects fixtures whose declared `config_hash` or `artifact_hash` no longer
  matches the actual content.

### Canonical verifier command

```bash
cmake \
    -DAGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET=tests/eval/fixtures/precomputed-embedding-medium.json \
    -P tools/agent-memory-bench/verify-precomputed-embedding-artifact.cmake
```

CTest runs the same verifier for the tiny and medium fixtures.

### Interpretation

This closes the main limitation left after PR #69: hashes are no longer only
well-formed metadata. For committed fixtures, they are recomputed from the
canonical config/payload and fail CI if they drift.

The verifier is intentionally dependency-free and does not generate embeddings.
It is the integrity gate that the next external-model fixture should pass after
an external generator writes the frozen JSON artifact.

### What to check next

- Add an externally generated embedding fixture using this verifier.
- Record the external generator command/config next to the fixture.
- If real model prompts become non-trivial, include prompt text or prompt hashes
  in the canonical generator config rather than relying only on prompt ids.
