# Project Overview

## Intent

`agent-memory-cpp` is an embedded C++17 toolkit for building memory and
retrieval systems for AI agents. It should provide deterministic, inspectable
local components rather than a hosted service or a generic agent framework.

## Current Status

The repository is in the project-skeleton stage. The current code provides:

- static library target `agent_memory`;
- public alias `agent_memory::agent_memory`;
- public aggregate header `agent_memory/AgentMemory.hpp`;
- `core::LibraryInfo` smoke API;
- dependency-free document, chunk, metadata, and source-kind primitives;
- dependency-free document storage contract;
- dependency-free embedding value types, model metadata, and embedder contract;
- dependency-free vector index value types and index contract;
- opt-in MDBX dependency wiring for future storage backends;
- optional MDBX-backed document storage adapter;
- CMake options for tests, examples, warnings, and MDBX wiring;
- smoke/domain/storage/embedding/index tests and one basic example.

## Core Scope

The library scope is limited to:

- memory records and memory strategies;
- ingestion and chunking;
- persistent storage;
- embedding interfaces and adapters;
- exact and approximate indexes;
- retrieval and ranking;
- context assembly for downstream agents or LLM calls.

## Non-Goals

Do not add these to the core library:

- autonomous-agent orchestration;
- browser automation;
- participant simulation;
- TTS/ASR pipelines;
- LLM inference engines;
- hosted vector database services;
- generic prompt-template collections.

Adapters for external systems may be added later, but they must stay outside
core domain logic.

## Planned Source Areas

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

The layout is expected to grow incrementally. Do not create empty directories or
placeholder layers unless a PR needs them.
