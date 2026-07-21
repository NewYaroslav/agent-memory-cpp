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

Run the PCA-focused 128D learned-projection fixture:

```bash
./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-pca.example.json \
    tmp/synthetic-binary-rerank-grid-pca-report.json
```

Run the high-dimensional synthetic variants when investigating projection
scaling beyond the default 128D fixture:

```bash
./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-highdim-384.example.json \
    tmp/synthetic-binary-rerank-grid-highdim-384-report.json

./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-highdim-768.example.json \
    tmp/synthetic-binary-rerank-grid-highdim-768-report.json

./tmp/build-bench/tools/agent-memory-bench/agent-memory-bench \
    tools/agent-memory-bench/synthetic-binary-rerank-grid-highdim-1536.example.json \
    tmp/synthetic-binary-rerank-grid-highdim-1536-report.json
```

The grid separates `data_seeds` from `encoder_seeds`. `repeat_count` controls
binary timing measurements, while `exact_timing_repeat_count` independently
controls repeated timing of two common exact baselines. Quality is sampled once
per data/encoder seed pair; timing repeats are not treated as independent
quality observations.

The optional `encoder_families` list selects which binary encoders participate
in the grid. Supported values are:

- `random_hyperplane_rademacher`;
- `coordinate_sign`;
- `randomized_hadamard_projection`;
- `learned_pair_difference_projection`;
- `pca_projection`.

If omitted, the grid keeps the historical random-hyperplane-only behavior.
`coordinate_sign` is unseeded and emits exactly `embedding_dimensions` bits, so
the runner skips other `bit_counts` for that family. The random-hyperplane and
randomized-Hadamard families use every configured `encoder_seed`.
`learned_pair_difference_projection` trains a deterministic global artifact
from the current data seed's document vectors only; evaluation queries are not
training input. Encoder artifacts are cached per family/seed/bit inside one
data-seed run, so `repeat_count` repeats binary materialization/search timing
without retraining the same artifact. With `randomize_execution_order=true`,
the runner shuffles the full `encoder_family x encoder_seed x bit_count x
repeat` task list for each data seed; JSON reports remain grouped by family and
encoder seed for readability.

`pca_projection` also trains only on document vectors and supports
`bit_count <= embedding_dimensions`; the runner skips larger bit widths for
that family. Its training cost is not included in query timing.

The two exact baselines answer different questions:

- `current_exact_index` measures the existing `ExactVectorIndex`, including its
  public index data structures and result construction;
- `contiguous_exact` stores normalized source vectors row-major, dispatches the
  SIMD dot-product kernel once per batch, reuses scoring workspaces, and returns
  lightweight positions. It is the compute-oriented denominator for deciding
  whether binary filtering beats a well-laid-out exact scan.

Both baselines must return exactly the same top-k for every query. Their timing
samples and median speedup denominators are reported separately; do not describe
a speedup against `current_exact_index` as a speedup against contiguous exact
vector arithmetic.

Binary grid reports also record `exact_vector_similarity_backend`. GNU/Clang
x86 builds select AVX2 at runtime, fall back to SSE2 when available, and
otherwise use the scalar C++17 implementation. The current MSVC x64 build uses
SSE2 for vector arithmetic; an isolated `/arch:AVX2` translation unit and
dispatch path remain future work.

They also record `binary_hamming_backend` and
`binary_encoder_similarity_backend`. Short packed signatures prefer hardware
POPCNT, sufficiently wide signatures may select the AVX2 nibble-LUT kernel on
GNU/Clang x86, and all platforms retain a lookup-table fallback. The current
MSVC x86/x64 implementation selects POPCNT when available and does not compile
the AVX2 Hamming kernel. The random-hyperplane `v2`
encoder materializes its deterministic projection lazily and uses the same
AVX2/SSE2/scalar vector backend for dense inputs. Every dense backend and the
sparse path use the same eight-lane reduction contract, so persisted signatures
do not change with runtime CPU dispatch.

Tests and diagnostic tools can query backend availability and request a
specific backend through `HammingDistanceComputer` and
`VectorSimilarityComputer`. Forced construction rejects unavailable backends;
it never executes an unsupported instruction speculatively.

Run the decomposed Hamming hot-path benchmark:

```bash
./tmp/build-bench/tools/agent-memory-bench/agent-memory-hamming-hot-path-bench \
    tools/agent-memory-bench/hamming-hot-path.example.json \
    tmp/hamming-hot-path-report.json
```

The report separates raw contiguous distance calculation, `partial_sort`,
`nth_element`, distance-bucket selection, the complete flat-index API, and the
experimental bounded multi-probe index. Use the 100k fixtures only for manual
directional runs; the tiny smoke fixture is the one registered in CTest.

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
