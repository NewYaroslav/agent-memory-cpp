# Dependencies

## External Layout

Project-owned source dependencies live as flat Git submodules under
`external/`:

```text
external/
    libmdbx/
    mdbx-containers/
```

Do not add project-owned dependencies as nested submodules inside another
dependency just because the upstream project supports that layout. If a
dependency needs another dependency and both are owned by this checkout, keep
them as siblings under `external/` and pass targets through CMake.

## CMake Direction

Optional dependencies must stay behind CMake options and implementation
adapters. The core domain and contract layers remain dependency-free.

Preferred lookup order:

1. Reuse an already existing parent-provided CMake target.
2. Use a flat source dependency from `external/`.
3. Use an installed package when the selected dependency mode allows it.
4. Allow upstream fallback logic only as a last resort.

Avoid making `BUNDLED` a strict override over parent-provided targets. Parent
projects must be able to keep the dependency graph flat.

`AGENT_MEMORY_MDBX_DEPS_MODE` is forwarded to the source build of
`mdbx-containers` and controls how MDBXC provides libmdbx. It is not a strict
global selector for how `agent-memory-cpp` finds `mdbx-containers` itself.

## MDBX Chain

`agent-memory-cpp` owns these flat submodules for MDBX-backed storage work:

- `external/libmdbx`
- `external/mdbx-containers`

When `AGENT_MEMORY_ENABLE_MDBX=ON` and local source dependencies are present,
the build adds `external/libmdbx` first. `mdbx-containers` then sees the MDBX
target as parent-provided and exposes `mdbx_containers::mdbx_containers`
without creating a nested MDBX checkout.

Initialize dependencies with:

```bash
git submodule update --init external/libmdbx external/mdbx-containers
```

Use `AGENT_MEMORY_MDBX_CONTAINERS_SOURCE_DIR` only as an override for a custom
`mdbx-containers` checkout. The default source checkout is
`external/mdbx-containers` when it exists.

## Planned Optional Dependencies

Future optimization work may add optional dependencies, but each must stay
behind a CMake option and an adapter boundary:

- Zstd for text/chunk compression and benchmarked bucket compression.
- Eigen for zero-copy math views and scoring/reranking helpers.
- Miniz/zlib or LZ4 only as later codec alternatives when justified.

Planned options should be introduced only with corresponding implementation:

```cmake
AGENT_MEMORY_ENABLE_ZSTD
AGENT_MEMORY_ENABLE_EIGEN
```

Planned public feature macros:

```cpp
AGENT_MEMORY_HAS_ZSTD
AGENT_MEMORY_HAS_EIGEN
```

Define feature macros consistently as `0` or `1` once the corresponding option
exists.

Do not make Eigen or Zstd part of dependency-free public contracts.
