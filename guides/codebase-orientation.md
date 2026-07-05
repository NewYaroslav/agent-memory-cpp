# Codebase Orientation

## Current Tree

```text
cmake/
    AgentMemoryOptions.cmake
examples/
    basic_usage.cpp
src/agent_memory/
    AgentMemory.hpp
    core/
        LibraryInfo.hpp
        LibraryInfo.cpp
tests/
    CMakeLists.txt
    smoke/
        agent_memory_smoke.cpp
```

## Public Includes

Consumers include project headers through the `agent_memory/` prefix:

```cpp
#include <agent_memory/AgentMemory.hpp>
```

The build interface currently exposes `src/` as the public include root.

## Existing Smoke API

`agent_memory::core::LibraryInfo` provides minimal project metadata for the
initial smoke test and example:

- `library_name()`;
- `library_description()`;
- `library_version()`.

Do not treat this class as the future application facade.

## Where To Add Code

- Core metadata and stable library primitives: `src/agent_memory/core/`.
- Domain value objects and contracts: `src/agent_memory/domain/` or the
  relevant domain slice.
- Storage contracts and neutral types: `src/agent_memory/storage/`.
- MDBX-specific implementation details: future infrastructure/storage area.
- Embedding contracts: `src/agent_memory/embedding/`.
- Retrieval/ranking behavior: `src/agent_memory/retrieval/`.
- Memory policies and composition: `src/agent_memory/memory/`.
- Context builders and formatting: `src/agent_memory/context/`.

Create a new area only when the PR introduces real code for that area.

## Tests And Examples

- Put unit and smoke tests under `tests/`.
- Put small consumer-style examples under `examples/`.
- Keep examples non-interactive unless the user explicitly requests an
  interactive example.
