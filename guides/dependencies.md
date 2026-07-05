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
