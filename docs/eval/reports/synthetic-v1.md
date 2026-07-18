# Synthetic benchmark sweep v1

This report documents the first committed synthetic sweep fixture used to
compare the two dependency-free lexical baselines:

- `bow_vector` — dense bag-of-words vectors with exact top-K search.
- `bm25_exact` — in-memory BM25 via `ExactLexicalRetriever`.

The committed fixture is intentionally tiny and deterministic. It is a pipeline
sanity check, not a production-quality benchmark. Larger 1k/2.5k/5k/10k sweeps
should be generated per seed with `tools/benchmarks/synthetic_generator.py` and
kept out of the repository.

## Dataset

- Fixture: `tests/eval/fixtures/tiny_synthetic_v1.json`
- Generator: `tools/benchmarks/synthetic_generator.py`
- Seed: `12648430`
- Corpus: 50 documents
- Queries: 40 queries
- Query classes: exact, partial, graded, distractor-heavy, OOV, no-answer,
  length-bias, repeated-term
- Result limit: 50

Generation command:

```bash
py -3 tools/benchmarks/synthetic_generator.py \
    --documents 50 \
    --queries 40 \
    --seed 12648430 \
    --limit 50 \
    --output tests/eval/fixtures/tiny_synthetic_v1.json
```

On platforms where `python3` is available directly, replace `py -3` with
`python3`.

## Smoke-run command

```bash
cmake -S . -B tmp/build-bench \
    -DCMAKE_BUILD_TYPE=Release \
    -DAGENT_MEMORY_BUILD_TESTS=ON \
    -DAGENT_MEMORY_BUILD_BENCHMARKS=ON \
    -DAGENT_MEMORY_ENABLE_JSON=ON

cmake --build tmp/build-bench --parallel
./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-sweep.example.json \
    tmp/synthetic-sweep-report.json
```

## Tiny-fixture results

Local smoke run on the tiny fixture:

| Baseline | Recall@1 | Recall@5 | Recall@10 | nDCG@10 | MRR | No-answer accuracy | OOV fraction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `bow_vector` | 0.442 | 0.708 | 0.923 | 0.731 | 0.717 | 1.000 | 0.133 |
| `bm25_exact` | 0.585 | 0.820 | 1.000 | 0.844 | 0.827 | 1.000 | 0.133 |

Timing numbers are intentionally omitted from the table because the fixture is
too small for stable latency conclusions. The JSON report still records
mean/p50/p95/p99 latency, throughput, index build timings, vocabulary size,
document count, mean document length, peak RSS, and roadmap-label PR #29 hook
fields.

## Interpretation

The tiny fixture validates that:

1. both baselines can be driven through the same `RetrievalEvalDataset` and
   `BenchmarkReport` pipeline;
2. query classes with graded qrels and no-answer intent survive JSON loading and
   metric validation;
3. BM25 behaves at least as well as the BoW control point on this lexical
   synthetic fixture;
4. PR #29 hook fields remain present and zero for exact baselines.

The next useful measurement step is a generated staircase sweep:

- 1k documents;
- 2.5k documents;
- 5k documents;
- 10k documents only after inspecting the 5k run.

Do not commit the larger generated JSON datasets; commit only the resulting
summary report if it changes an architectural decision.
