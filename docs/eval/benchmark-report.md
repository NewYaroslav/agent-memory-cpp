# BenchmarkReport contract (v1)

`agent_memory::BenchmarkReport` is the stable hand-off record for benchmark
tools. It wraps the existing retrieval evaluation metrics with the extra
measurements a benchmark runner knows but `evaluate_retrieval()` deliberately
does not compute.

## Sections

- `quality` — Recall@1/5/10/50, nDCG@10, unbounded MRR, no-answer accuracy,
  OOV fraction, and empty-result fraction.
- `speed` — measured query count, mean/p50/p95/p99 latency, throughput, and
  total retrieval-loop wall time.
- `index` — document count, vocabulary size, mean document length, ingest time,
  embedding time, index build time, and peak resident-set bytes.
- `pr29_hooks` — forward-compatible candidate-filter counters for the
  roadmap-label "PR #29" binary-signature work. Exact baselines leave these
  values at zero.

The schema is versioned by `BenchmarkReport::kSchemaVersion`. JSON output uses
`schema_version: 1` and preserves the four sections above as top-level objects.

## Construction path

Use `make_benchmark_report(eval_report, benchmark_name, measurements)` after a
runner has produced a `RetrievalEvalReport`. The helper copies quality and
latency metrics from the eval report, derives the empty-result fraction, and
copies driver-provided measurements for index/build/process data.

`validate_benchmark_report()` rejects empty names, unsupported schema versions,
NaN/infinite values, negative timings, fraction fields outside `[0, 1]`, and
non-monotonic p50/p95/p99 latency.

## CLI smoke benchmark

The synthetic CLI lives under `tools/agent-memory-bench/`. It is intentionally a
small exact-vector smoke benchmark, not the full BEIR or MemoryStack sweep. It
exists to prove that benchmark reports can be generated, serialized, and
rendered without adding external dataset requirements.

```bash
cmake -S . -B tmp/build-bench \
    -DCMAKE_BUILD_TYPE=Release \
    -DAGENT_MEMORY_BUILD_BENCHMARKS=ON \
    -DAGENT_MEMORY_ENABLE_JSON=ON

cmake --build tmp/build-bench --parallel
./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/config.example.json \
    tmp/benchmark-report.json
```

On Windows, append `.exe` to the executable path or run it from the generated
build tree. The optional second CLI argument overrides the `output_path` field
from the config file.
