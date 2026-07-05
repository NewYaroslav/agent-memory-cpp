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
| `AGENT_MEMORY_ENABLE_WARNINGS` | `ON` | Enable project compiler warnings. |

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

On multi-config generators, pass the configuration to build and test commands:

```bash
cmake --build tmp/build-cpp17 --config Release --parallel
ctest --test-dir tmp/build-cpp17 --build-config Release --output-on-failure
```

## CI Expectations

GitHub Actions builds on Windows, Linux, and macOS. CI should configure, build,
and run CTest with examples and tests enabled.

## Verification Heuristics

- Documentation-only changes: inspect Markdown and run `git diff --check`.
- CMake changes: configure and build at least one clean build directory.
- Public header changes: build tests and examples.
- Behavior changes: add or update tests, then run the relevant CTest subset or
  the full suite when the affected area is shared.
