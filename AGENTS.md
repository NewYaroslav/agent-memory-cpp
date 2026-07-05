# AGENTS.md

This file defines project-local guidance for coding agents working in this
repository.

## Project Intent

- `agent-memory-cpp` is an embedded C++17 library for AI-agent memory,
  retrieval, indexing, and context construction.
- Keep the public scope narrow: memory, retrieval, ingestion, storage, and
  context assembly.
- Do not add agent orchestration, browser automation, participant simulation,
  TTS/ASR, or generic prompt-template collections to the core library.

## Architecture

- Use a DDD-like layout with small domain slices and explicit boundaries.
- Keep C++ headers and implementation files side by side under `src/`.
- Use static-library builds as the primary mode. Avoid a header-only design for
  core functionality so dependencies can stay isolated behind `.cpp` files.
- Keep storage, indexing, retrieval, memory strategies, and context formatting
  separate. Do not hide all behavior inside a single facade class.
- Optional infrastructure adapters must depend inward on core contracts.

Preferred top-level source areas:

```text
src/agent_memory/
    core/
    domain/
    storage/
    embedding/
    index/
    retrieval/
    ingestion/
    memory/
    context/
    infrastructure/
```

## Build

- Language baseline: C++17.
- CMake baseline: 3.20 or newer.
- Main target: `agent_memory`.
- Public alias: `agent_memory::agent_memory`.
- Build tests with `AGENT_MEMORY_BUILD_TESTS`.
- Build examples with `AGENT_MEMORY_BUILD_EXAMPLES`.

## Style

- All comments and Doxygen text must be in English.
- Use `#pragma once` and include guards in the form
  `AGENT_MEMORY_HEADER_<PATH>_<FILE>_HPP_INCLUDED`.
- Class, struct, and enum names use `PascalCase`.
- Methods and free functions use `snake_case`.
- Private and protected data members use `m_` + `snake_case`.
- Boolean names start with `is`, `has`, `use`, or `enable`.
- Enum values use `PascalCase`.
- If a file contains one class, use `PascalCase` file names.
- If a file contains helpers or aggregate includes, use `snake_case` or a
  clear aggregate name.
- Use 4 spaces for indentation.
- Keep opening braces on the same line for namespaces, classes, and methods.
- Do not use `using namespace`.
- Put project headers before system headers in include lists.
- Avoid comments that restate the code. Add comments only for non-obvious
  constraints, invariants, or boundary decisions.

## Git

- Work on feature branches; do not commit directly to `main` unless explicitly
  requested.
- Use English Conventional Commit messages.
- Keep PRs small and independently buildable.
- Before handing off code changes, run the narrowest relevant CMake configure,
  build, and CTest checks.
