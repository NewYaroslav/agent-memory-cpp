# TencentDB Agent Memory Reference Review

## 2026-07-23

### Context

Reviewed TencentCloud/TencentDB-Agent-Memory as an architectural and benchmark
reference for `agent-memory-cpp`. The review used the upstream GitHub
repository snapshot cloned into `tmp/tencentdb-agent-memory` and public GitHub
README / issue pages.

### Question

Can TencentDB Agent Memory provide useful comparison targets for this project,
and does its architecture imply additional storage requirements for
`mdbx-containers`?

### Setup

- Source: `https://github.com/TencentCloud/TencentDB-Agent-Memory`
- Local snapshot path: `tmp/tencentdb-agent-memory`
- Primary inspected files:
  - `README.md`
  - `package.json`
  - `src/core/tdai-core.ts`
  - `src/core/store/types.ts`
  - `src/core/hooks/auto-recall.ts`
  - `src/core/hooks/auto-capture.ts`
  - `src/core/record/l1-writer.ts`
  - `src/core/record/l1-extractor.ts`
  - `src/core/scene/scene-extractor.ts`
  - `src/core/persona/persona-generator.ts`
  - `src/offload/types.ts`
  - `src/offload/storage.ts`
  - `src/offload/l3-helpers.ts`

### Expected Result

The project was expected to overlap with `agent-memory-cpp` around layered
long-term memory, hybrid retrieval, local storage, and context compression.
The likely useful output was a list of comparable benchmark axes, not a direct
dependency decision.

### Actual Result

TencentDB Agent Memory is a TypeScript / Node.js OpenClaw and Hermes memory
plugin with:

- host-neutral `TdaiCore` and adapters;
- L0 raw conversation capture;
- L1 extraction into three concrete memory types:
  `persona`, `episodic`, `instruction`;
- L2 scene blocks stored as Markdown;
- L3 `persona.md` synthesis;
- SQLite + sqlite-vec + FTS5 local backend and Tencent VectorDB backend;
- hybrid keyword/vector retrieval with RRF;
- optional short-term context offload where raw tool results go to `refs/*.md`
  and compact Mermaid MMD projections carry `node_id` drill-down refs.

Public README benchmark claims:

| Area | Benchmark | Baseline | With plugin | Extra signal |
|---|---:|---:|---:|---|
| Short-term | WideSearch | 33% success | 50% success | 221.31M -> 85.64M tokens |
| Short-term | SWE-bench | 58.4% success | 64.2% success | 3474.1M -> 2375.4M tokens |
| Short-term | AA-LCR | 44.0% success | 47.5% success | 112.0M -> 77.3M tokens |
| Long-term | PersonaMem | 48% accuracy | 76% accuracy | no token figure |

No committed benchmark harness or reproducible eval dataset was found in the
repo. Tests are mostly unit/recovery tests (`vitest`, plus Hermes Python
recovery/shutdown tests). GitHub issue #106 explicitly asks the project to add
reproducible memory evaluation benchmarks and suggests axes such as storage
integrity, retrieval precision, clustering, forgetting directionality,
multi-hop reasoning, and deep retrieval.

### Interpretation

TencentDB Agent Memory is a strong architecture reference, but not a direct
library dependency or a currently reproducible benchmark baseline. Its most
important lesson for `agent-memory-cpp` is not the fixed L0-L3 hierarchy; it is
the invariant that compact memory must remain addressable:

```text
compact projection -> evidence unit -> raw artifact/source
```

This maps naturally to `agent-memory-cpp` through typed components and
projection/evidence indexes rather than a fixed persona-oriented pyramid.

### TZ Update

`guides/mdbx-containers-extension-tz.md` was extended with:

- `AddressableCompression` capability;
- `artifact_refs`, `derivation_edges`, `evidence_edges`, `drilldown_refs`,
  and `artifact_to_units` DBI entries;
- P4 follow-up primitives for `ArtifactRefTable`, `DerivationIndex`, and
  `DrillDownIndex`;
- section 12.8 with storage contracts and benchmark implications.

### Limitations

- The public Tencent benchmark numbers were not independently reproduced.
- The repo does not include the WideSearch/SWE-bench/AA-LCR/PersonaMem runner
  setup used for the README table.
- The local review did not run Tencent tests; it inspected source and docs.
- Their benchmark claims are host/runtime-level, while `agent-memory-cpp` is a
  lower-level C++ storage/retrieval substrate.

### Follow-up Checks

- Track Tencent issue #106 for any reproducible benchmark harness.
- Add our own benchmark axis for progressive disclosure:
  traceability coverage, drill-down latency/read amplification, layer selection
  accuracy, compression utility, and explicit failure classification.
- When `KnowledgeUnit` work starts, ensure derived profile/scenario claims keep
  evidence and contradiction links instead of being promoted into raw facts.
