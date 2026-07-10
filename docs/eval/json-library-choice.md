# ADR: JSON library for the benchmark tooling layer

- Status: Accepted 2026-07-10
- Deciders: agent-memory-cpp maintainers
- Context: PR #27 introduces the JSON file loader for retrieval evaluation
  datasets.

## Context

The retrieval evaluation layer (PR #26) and the JSON dataset loader it now
ships with (PR #27) need a JSON read/write library. Constraints:

- C++17, no language extensions.
- No Boost dependency is acceptable: the project is Boost-free by design.
- The repo currently has zero JSON dependencies.
- Workloads are BEIR-style megabyte-class datasets loaded once per benchmark
  run; latency matters, but the parse step is not the hot loop.
- Schema is pinned in `docs/eval/dataset-schema.md`; the loader is type-driven
  and uses `contains()`-style defensive checks on every required field.

## Decision

Adopt **nlohmann/json** as the default JSON library for benchmark tooling.

The CMake glue lives in `cmake/AgentMemoryJson.cmake`:

1. Primary: `find_package(nlohmann_json CONFIG)` (system install / vcpkg /
   conan / Brew / apt).
2. Fallback: CMake `FetchContent` pulling the pinned tag `v3.11.3` from
   `https://github.com/nlohmann/json.git`. This is the dev-environment
   escape hatch when the system has no installed copy.
3. No-op stub when `AGENT_MEMORY_ENABLE_JSON=OFF`: an empty INTERFACE target
   `agent_memory::json` is still exported so downstream `target_link_libraries`
   calls don't have to gate on the option. The `DatasetLoader` header still
   `#error`s out when JSON is disabled.

The full vendored path (in-tree `external/nlohmann` via git submodule) is
deliberately deferred to a later PR and documented in the "Future work"
section below.

## Rationale

- **Header-only by default** (when used with the single-header distribution
  or `FetchContent`). No additional compiled library to wire up beyond what
  CMake already handles.
- **Ergonomic C++ API** that mirrors the value types we already expose
  (`std::map`, `std::vector`, `std::string`). The loader writes
  `node.is_string()`, `node.get<std::string>()`, etc., which matches the
  style of the rest of the project.
- **No Boost dependency** and no transitive forced third-party deps.
- **Bidirectional** (parse + serialize) so a future `write_dataset_to_json`
  helper falls out for free.
- **Performance is adequate for the workload.** BEIR datasets land in the
  tens of megabytes; even the slow corner (multi-hundred-MB corpora like
  Wikipedia BEIR) parses in seconds, which is well below the time spent
  running the retriever.
- **Wide adoption and stable release cadence.** `v3.11.3` is the pinned tag
  because it is the latest 3.11.x at the time of writing and is ABI-stable.

## Alternatives considered

| Library    | Why not now |
|------------|-------------|
| RapidJSON  | Excellent throughput, but the SAX/DOM split and bespoke allocator model cost more developer time than the project needs at this scale. Worth revisiting if profiling shows parse time on the critical path. |
| simdjson   | On-parse-stage SIMD performance is exceptional, but the DOM shape differs from `std::map`/`std::vector` and adds a second mental model. Streaming-style access is overkill for one-shot dataset loads. |
| Boost.JSON | Works fine, but pulls Boost into the dependency tree, which violates the project-wide Boost-free rule. |
| Glaze      | Promising and very fast, but the ecosystem around it (build integration, long-term support, packaging) is not mature enough for an external-facing API in a 0.1.x project. |

## Consequences

### Positive

- Dataset loading is one call to `load_dataset_from_json_file(path)` from
  user code, with errors that include the path and field name that failed.
- A future write-side helper (`write_dataset_to_json_file`) writes itself
  from the existing parser; no new dependency is required.
- The CMake gate (`AGENT_MEMORY_ENABLE_JSON`) defaults to ON so the eval
  tooling is functional out-of-the-box on most developer machines.

### Negative / risks

- nlohmann/json's parse time is the slowest of the four considered options
  on large inputs. If a benchmark run spends more than ~10% of its wall
  time in the loader, we should re-evaluate this choice.
- `FetchContent` requires network access on the first configure when no
  system install is present. CI environments without internet must
  pre-install nlohmann_json.

### When to switch

Reopen this ADR if any of the following become true:

1. Profile data shows parse time above 5% of total benchmark runtime.
2. Datasets grow past hundreds of megabytes and parse latency becomes user
   visible.
3. The loader evolves to streaming (line-delimited JSON, NDJSON) where
   `simdjson`'s streaming API would be a clear win.
4. The project picks up a Boost dependency for unrelated reasons, in which
   case Boost.JSON becomes a viable single-dependency consolidation.

### Future work

- **Vendored submodule**: add `external/nlohmann-json` as a git submodule
  (mirroring how `external/libmdbx` and `external/mdbx-containers` are
  handled) and have `AgentMemoryJson.cmake` prefer it before `find_package`
  and `FetchContent`. This removes the network dependency for first-time
  configure and is the recommended upgrade path once a release tag is
  chosen.
- **Write-side helper**: add `write_dataset_to_json_file` mirroring the
  loader; same schema doc, same `Metadata` mapping.
- **Streaming hook**: if BEIR-scale corpora (multi-GB) become the norm,
  revisit `simdjson` or introduce a chunked reader behind the existing
  interface.