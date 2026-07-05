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
- dependency-free resource revision and manifest value types;
- dependency-free document storage contract;
- dependency-free resource manifest storage contract;
- dependency-free embedding value types, model metadata, and embedder contract;
- dependency-free vector index value types and index contract;
- exact in-memory vector index baseline;
- dependency-free retrieval value types and retriever contract;
- dependency-free resource indexer orchestration over storage, embedding, and
  vector index contracts;
- dependency-free tokenizer value types and tokenizer contract;
- std-only tokenizer baseline for UTF-8 text, markdown, and code-like text;
- dependency-free lexical search value types for postings, stats, queries, and
  results;
- dependency-free token dictionary contract for token-id allocation and
  document-frequency stats;
- planned lexical/BM25 retrieval architecture;
- opt-in MDBX dependency wiring for future storage backends;
- optional MDBX-backed document storage adapter;
- optional MDBX-backed resource manifest storage adapter;
- CMake options for tests, examples, warnings, and MDBX wiring;
- smoke/domain/storage/embedding/index tests and one basic example.

## Core Scope

The library scope is limited to:

- memory records and memory strategies;
- ingestion and chunking;
- resource ownership and targeted reindexing;
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
    compression/
    math/
    ingestion/
    memory/
    context/
    infrastructure/
```

The layout is expected to grow incrementally. Do not create empty directories or
placeholder layers unless a PR needs them.

## Optimization Backlog

Detailed follow-up tasks for compression, optional Eigen/SIMD scoring, vector
encoding, binary signatures, MDBX bucket indexes, and recall/latency benchmarks
are tracked in `guides/optimization-roadmap.md`.

## Reindexing Backlog

Resource manifests, source revision tracking, targeted reindexing, tombstones,
and compaction for mutable memory are tracked in
`guides/resource-reindexing.md`.

## Lexical Search Backlog

BM25, token dictionaries, postings, Unicode tokenization, raw resource stores,
phrase/proximity, fuzzy search, BM25F, graph retrieval, hybrid retrieval, and
planner-guided retrieval are tracked in `guides/lexical-search-roadmap.md`.
