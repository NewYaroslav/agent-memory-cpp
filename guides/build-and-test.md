# Build And Test

## Requirements

- CMake 3.20 or newer.
- C++17 compiler.
- Windows, Linux, or macOS.

## CMake Targets

- Main target: `agent_memory`.
- Public alias: `agent_memory::agent_memory`.

## CMake Options

All project options use the `AGENT_MEMORY_` prefix.

| Option | Default | Description |
| --- | --- | --- |
| `AGENT_MEMORY_BUILD_TESTS` | top-level build only | Build tests and register them with CTest. |
| `AGENT_MEMORY_BUILD_EXAMPLES` | `OFF` | Build examples from `examples/`. |
| `AGENT_MEMORY_BUILD_BENCHMARKS` | `OFF` | Build benchmark tools from `tools/`. Requires `AGENT_MEMORY_ENABLE_JSON=ON`. |
| `AGENT_MEMORY_ENABLE_JSON` | `ON` | Enable JSON-backed eval loaders and benchmark report serialization. |
| `AGENT_MEMORY_ENABLE_WARNINGS` | `ON` | Enable project compiler warnings. |
| `AGENT_MEMORY_ENABLE_MDBX` | `OFF` | Enable MDBX-backed storage dependencies. |
| `AGENT_MEMORY_MDBX_CONTAINERS_SOURCE_DIR` | empty | Optional override for a local `mdbx-containers` source tree. Defaults to `external/mdbx-containers` when present. |
| `AGENT_MEMORY_MDBX_DEPS_MODE` | `AUTO` | MDBX dependency mode forwarded to `mdbx-containers` source builds: `AUTO`, `SYSTEM`, or `BUNDLED`. |

## Baseline Commands

Use `tmp/` or `build-*` for local verification outputs. These paths are
generated and should stay untracked.

```bash
cmake -S . -B tmp/build-cpp17 \
    -DCMAKE_BUILD_TYPE=Release \
    -DAGENT_MEMORY_BUILD_TESTS=ON \
    -DAGENT_MEMORY_BUILD_EXAMPLES=ON

cmake --build tmp/build-cpp17 --parallel
ctest --test-dir tmp/build-cpp17 --output-on-failure
```

Build and smoke-test the synthetic benchmark CLI:

```bash
cmake -S . -B tmp/build-bench \
    -DCMAKE_BUILD_TYPE=Release \
    -DAGENT_MEMORY_BUILD_TESTS=ON \
    -DAGENT_MEMORY_BUILD_BENCHMARKS=ON \
    -DAGENT_MEMORY_ENABLE_JSON=ON

cmake --build tmp/build-bench --parallel
ctest --test-dir tmp/build-bench --output-on-failure
./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/config.example.json \
    tmp/benchmark-report.json
```

When tests and benchmarks are enabled together, CTest also registers
`agent_memory_benchmark_cli_synthetic_sweep`, a smoke test that runs
`agent-memory-bench` against the committed synthetic sweep fixture. It also
registers benchmark CLI smoke tests for the binary flat-vs-float mode and the
binary rerank grid mode.

Run the BoW-vs-BM25 synthetic sweep fixture:

```bash
./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-sweep.example.json \
    tmp/synthetic-sweep-report.json
```

Run the binary rerank statistical grid fixture:

```bash
./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid.example.json \
    tmp/synthetic-binary-rerank-grid-report.json
```

The grid separates `data_seeds` from `encoder_seeds`. `repeat_count` controls
binary timing measurements, while `exact_timing_repeat_count` independently
controls repeated timing of the common exact baseline. Quality is sampled once
per data/encoder seed pair; timing repeats are not treated as independent
quality observations.

Run the deterministic staircase sweep helper:

```bash
py -3 tools/benchmarks/synthetic_staircase.py \
    --bench-exe tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    --output-dir tmp/synthetic-staircase-v1 \
    --summary tmp/synthetic-staircase-v1/synthetic-staircase-v1.md
```

The staircase helper keeps generated datasets, configs, and raw JSON reports in
`tmp/`. Commit only the generated Markdown summary when it changes a design
decision.

Regenerate the committed tiny synthetic fixture only when intentionally
changing the generator contract:

```bash
py -3 tools/benchmarks/synthetic_generator.py \
    --documents 50 \
    --queries 40 \
    --seed 12648430 \
    --limit 50 \
    --output tests/eval/fixtures/tiny_synthetic_v1.json
```

Configure with flat MDBX submodules:

```bash
git submodule update --init external/libmdbx external/mdbx-containers

cmake -S . -B tmp/build-mdbx \
    -DCMAKE_BUILD_TYPE=Release \
    -DAGENT_MEMORY_ENABLE_MDBX=ON \
    -DAGENT_MEMORY_MDBX_DEPS_MODE=BUNDLED \
    -DAGENT_MEMORY_BUILD_TESTS=ON

cmake --build tmp/build-mdbx --parallel
ctest --test-dir tmp/build-mdbx --output-on-failure
```

Use `AGENT_MEMORY_MDBX_CONTAINERS_SOURCE_DIR=/path/to/mdbx-containers` only
when testing a custom checkout outside `external/`.

On multi-config generators, pass the configuration to build and test commands:

```bash
cmake --build tmp/build-cpp17 --config Release --parallel
ctest --test-dir tmp/build-cpp17 --build-config Release --output-on-failure
```

## CI Expectations

GitHub Actions builds the default configuration on Windows, Linux, and macOS.
CI should configure, build, and run CTest with examples and tests enabled.

CI also includes a Linux MDBX wiring job. That job initializes only the flat
`external/libmdbx` and `external/mdbx-containers` submodules, configures with
`AGENT_MEMORY_ENABLE_MDBX=ON`, and runs CTest. Do not use recursive submodule
checkout for this job; nested dependency checkouts would hide flat-layout
regressions.

MDBX-enabled test builds register `agent_memory_mdbx_document_storage_test`,
which opens a temporary MDBX database and validates the document storage
adapter.

## Verification Heuristics

- Documentation-only changes: inspect Markdown and run `git diff --check`.
- CMake changes: configure and build at least one clean build directory.
- MDBX dependency wiring changes: configure once with `AGENT_MEMORY_ENABLE_MDBX=OFF`
  and once with `AGENT_MEMORY_ENABLE_MDBX=ON` using flat `external/libmdbx` and
  `external/mdbx-containers` submodules or an installed package.
- Public header changes: build tests and examples.
- Behavior changes: add or update tests, then run the relevant CTest subset or
  the full suite when the affected area is shared.
