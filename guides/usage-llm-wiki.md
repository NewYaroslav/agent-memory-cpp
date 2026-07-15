# Operator Guide — LLM Wiki on `agent-memory-cpp`

> Operator-level decision guide. This is **not** a roadmap: the canonical
> specification of `CompiledArticlePayload`, `MemoryProfileSpec`,
> `CompiledWikiStack`, `CompactionWorker`, and the per-kind payloads lives in
> [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md),
> [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md),
> [`compaction-roadmap.md`](compaction-roadmap.md), and
> [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md).
>
> This guide exists to give practitioners an actionable setup checklist and a
> map from the LLM Wiki pattern (Karpathy / Cole Medin / OpenWiki / Second
> Brain variants) onto the in-house `agent-memory-cpp` substrate.

## §1. Purpose

The LLM Wiki pattern is a recurring theme across the playbook and external
practice:

- Karpathy: "LLM writes itself a Wikipedia" — maintainer agent rewrites
  markdown articles from accumulated facts.
- Cole Medin: Self-Evolving Memory with `concepts/`, `connections/`,
  `daily-logs/`, `index.md` for Claude Code session memory.
- LLM Wiki в Obsidian: `raw/` + `wiki/` + `index.md` + `log.md` +
  `CLAUDE.md` structure for ingest → compile → query → lint cycle.
- OpenWiki: GitHub Actions daily PR pipeline that auto-generates /
  auto-updates codebase docs.
- Second Brain tooling: dashboards and starters built around the same
  pattern.

These are different products / projects but they share a pattern:
**raw notes + LLM-driven synthesis + index + log + human-review gate**. This
guide treats LLM Wiki as a pattern, not a product, and shows how to deploy it
on `agent-memory-cpp`.

This guide answers three asks:

1. **What is the LLM Wiki pattern?** — definition and variants.
2. **How do I run an LLM Wiki on `agent-memory-cpp`?** — map the pattern
   onto `CompiledWikiStack`, `CompactionWorker`, `HybridRetrievalEngine`, and
   the `KnowledgeUnit` substrate.
3. **What goes wrong?** — failure modes and mitigations.

Non-goals:

- Not a tutorial on `CompiledArticlePayload`. See
  [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md) §5.6.
- Not a tutorial on `CompactionWorker` API. See
  [`compaction-roadmap.md`](compaction-roadmap.md) §2.
- Not an LLM Wiki CLI tool. The CLI is part of the planned
  `agent-memory-cli` target (`memory-stacks-roadmap.md` §16 step 16).

## §2. Source attribution policy

This guide synthesises material from several sources. Citations follow the
two-tier pattern:

- `[Source: <public citation>]` — primary citation of a published work,
  project documentation, repository revision, or article. When citing an
  external source, the public link is authoritative.
- `[Source: internal note — no public source available. Path: <path>]` —
  statement comes only from internal research notes, with no equivalent
  public source at the time of writing. Requires additional verification.

Internal notes are discovery aids, not authoritative citations. Public
roadmap statements cite a public source first; internal notes serve only as
a marker of private provenance.

This convention is shared with
[`memory-architectures-roadmap.md`](memory-architectures-roadmap.md),
[`binary-embeddings-roadmap.md`](binary-embeddings-roadmap.md), and
[`usage-memory-models.md`](usage-memory-models.md). When the same fact
appears in multiple documents, all should cite the same public source if
one exists.

### §2.1. PR number disambiguation

This codebase uses the phrase "PR #N" in two unrelated senses:

- **GitHub PR #N** — a real pull request at <https://github.com/.../agent-memory-cpp/pulls>.
  Examples below that refer to specific GitHub PRs use the full phrase
  "GitHub PR #N".
- **Roadmap-label "PR #N"** — a label on an in-house roadmap section
  (e.g. "PR #26", "PR #27", "PR #28" inside the benchmark-tooling roadmap,
  or "PR #29 (BinarySignature)", "PR #30" inside
  `memory-stacks-roadmap.md`). These are roadmap-internal identifiers and do
  **not** correspond to GitHub PR numbers 26/27/28/29/30.

When a sentence could be read either way, the disambiguation is explicit.
Default convention: a bare "PR #N" without qualifier refers to the roadmap
label, matching the surrounding guides. GitHub PRs are always written out
fully.

## §3. What is an LLM Wiki

An LLM Wiki is a knowledge base whose content is **written and maintained
primarily by an LLM**, structured so an LLM (or human) can navigate it on
demand. The pattern has four components:

1. **Raw notes / source corpus** — original material to be ingested.
   Could be session logs, PDFs, chat transcripts, codebase, or a personal
   Obsidian vault.
2. **Synthesized wiki** — LLM-written or LLM-curated articles, each
   addressing a coherent topic. The articles are cross-linked and
   keyword-tagged for navigation.
3. **Index** — navigable summary of the synthesized wiki, used as the
   agent's entry point. Often a single `index.md` that fits in the LLM
   context window.
4. **Log + human-review gate** — record of every operation, plus a step
   where a human can accept, edit, or reject changes.

The four components are present in every variant below. What varies is
storage medium, automation cadence, and tooling.

[Source: github.com/langchain-ai/openwiki — OpenWiki continuous documentation]
<br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/ai-agents/Self-Evolving Memory для Claude Code на основе LLM Knowledge Base.md]
<br>[Source: internal note — no public source available. Path: ai-agent-playbook/playbooks/ai-agents/LLM Wiki в Obsidian — практический сетап с Claude Code.md]
<br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/ai-agents/Трёхслойная память для AI-агентов - метод Карпати.md]

The pattern is **not a single product**; it is an architectural idea that
appears under different names (LLM Knowledge Base, Self-Evolving Memory,
Agent Wiki, Living Documentation, Continuous Documentation, Second Brain).

## §4. Architecture variants

Observed variants in the playbook and external practice.

### §4.1. Raw + Wiki + Index + Log + CLAUDE.md (Obsidian playbook variant)

The variant from
`ai-agent-playbook/playbooks/ai-agents/LLM Wiki в Obsidian — практический сетап с Claude Code.md`:

```text
Project One/
  raw/          ← сырые данные (operator drops files here)
  wiki/         ← auto-generated markdown pages (do not edit by hand)
  index.md      ← auto-updated index of all wiki pages
  log.md        ← log of every ingest operation
  CLAUDE.md     ← schema + rules for the LLM
```

Workflow: ingest scans `raw/`, analyses new files, creates or updates wiki
pages in `wiki/`, updates `index.md`, writes operation to `log.md`. Query
reads `index.md`, finds relevant pages, recursively walks `[[cross-references]]`,
forms answer.

Limits from the playbook: up to ~100 raw files per project, several hundred
wiki pages comfortably, `index.md` must fit in the LLM context window.

[Source: internal note — no public source available. Path: ai-agent-playbook/playbooks/ai-agents/LLM Wiki в Obsidian — практический сетап с Claude Code.md]

### §4.2. Self-Evolving Memory (Cole Medin + Karpathy compiler analogy)

The variant from
`ai-agent-playbook/concepts/ai-agents/Self-Evolving Memory для Claude Code на основе LLM Knowledge Base.md`:

```text
vault/
  concepts/        ← extracted concepts and ideas
  connections/     ← links between concepts
  daily-logs/      ← session summaries
  index.md         ← navigation file (critical)
  agents.md        ← global rules for the agent
```

Cole Medin uses Claude Code hooks for daily flush and session-end
summarisation. The "compiler analogy" (Source → Compile → Executable →
Health Checks → Query) frames the daily flush as the "compile" step.

[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/ai-agents/Self-Evolving Memory для Claude Code на основе LLM Knowledge Base.md]

### §4.3. Karpathy Wiki (3-layer memory variant)

The variant from
`ai-agent-playbook/concepts/ai-agents/Трёхслойная память для AI-агентов - метод Карпати.md`:

```text
wiki/
  WIKI.md          ← rules for the maintainer agent
  INDEX.md         ← table of contents
  shared/          ← articles shared across agents
    infrastructure.md
    active-projects.md
    people-and-clients.md
    decisions-log.md
  agent-name/      ← per-agent articles
  archive/         ← stale, not injected, kept in git
```

Each article has frontmatter with `injection_count`. Articles with
`injection_count == 0` and `last_updated > 60 days` go to `archive/` on
weekly compact. Maintainer agent rewrites articles every 6 hours.

[Source: Karpathy talks on LLM-as-OS (2025)]
<br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/ai-agents/Трёхслойная память для AI-агентов - метод Карпати.md]

### §4.4. OpenWiki (continuous documentation pipeline)

The variant from
`ai-agent-playbook/concepts/ai-agents/Автоматическая документация как память агента — OpenWiki и подходы.md`
and `ai-agent-playbook/tools/rag/OpenWiki — CLI documentation for agents.md`:

- CLI + TUI (Ink) for interactive / headless modes.
- GitHub Actions daily PR pipeline: `cron: "0 8 * * *"`, model configurable
  (default GLM 5.2), non-interactive `--print` flag.
- AGENTS.md / CLAUDE.md augmentation: OpenWiki adds instructions to the
  agent file so the agent knows where the wiki lives.
- PR-based merge: human review before merge, avoiding doc spam.
- LangSmith tracing for observability.

[Source: github.com/langchain-ai/openwiki — OpenWiki continuous documentation]
<br>[Source: internal note — no public source available. Path: ai-agent-playbook/tools/rag/OpenWiki — CLI documentation for agents.md]

### §4.5. Second Brain tooling

The variant from
`ai-agent-playbook/tools/rag/Second Brain Starter.md` and
`ai-agent-playbook/tools/rag/Second Brain Research Dashboard.md`. Tooling
focus: starter templates + dashboards for personal knowledge management
that follow the same raw + wiki + index pattern.

[Source: internal note — no public source available. Path: ai-agent-playbook/tools/rag/Second Brain Starter.md]
<br>[Source: internal note — no public source available. Path: ai-agent-playbook/tools/rag/Second Brain Research Dashboard.md]

## §5. Integration with `agent-memory-cpp`

Map each variant onto the in-house substrate. Where a name appears below as a
real type, the file was verified by `git ls-files 'src/agent_memory/**/*.hpp'`
to exist in `src/agent_memory/`. Where a name is documented but not yet in
the source tree, it is tagged as planned.

### §5.1. Real types (verified in `src/agent_memory/`)

- `IRetriever` — abstract retriever interface (`src/agent_memory/retrieval/IRetriever.hpp`).
- `IRetrievalEngine` — retrieval engine interface
  (`src/agent_memory/retrieval/IRetrievalEngine.hpp`); distinct from
  `IRetriever`.
- `HybridRetrievalEngine` — concrete hybrid retriever implementation
  (`src/agent_memory/retrieval/HybridRetrievalEngine.hpp`).

### §5.2. Planned types (documented, not yet in source tree)

> **Planned API — not yet implemented.** Names listed below are documented
> in [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) and adjacent
> guides but are not currently present in `src/agent_memory/`. Verified by
> `git ls-files 'src/agent_memory/**/*.hpp'` against the planned names —
> none of `MemoryProfileSpec`, `MemoryStack`, or `CompactionWorker` appear
> in the source tree at the time of writing.

- `MemoryProfileSpec` (planned) — declarative specification of capabilities
  and policies; see [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §6.
- `MemoryStack` (planned) — runtime object with handles to storage and
  retrieval; see [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §7.
- `CompiledWikiStack` (planned) — `MemoryProfileSpec` factory for LLM Wiki
  use case; see [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §8.5.
- `CompiledArticlePayload` (planned) — payload for the
  `KnowledgeUnitKind::CompiledArticle` kind; see
  [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md) §5.6.
- `CompactionWorker` (planned) — async worker that runs `ICompactionJob`s;
  see [`compaction-roadmap.md`](compaction-roadmap.md) §2.
- `SummaryPromotionJob` (planned) — job type that promotes frequently-used
  facts into `CompiledArticlePayload` units; see
  [`compaction-roadmap.md`](compaction-roadmap.md) §4.5.
- `KnowledgeUnit` envelope + components + projections (planned) — the
  ADR-001 data model that replaces the older monolithic struct; see
  [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §3 and
  [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md).

### §5.3. Pattern → substrate mapping

| Pattern element | Variant § | Substrate mapping |
|---|---|---|
| **Raw notes** | §4.1 `raw/`, §4.2 `daily-logs/` | `Note` / `Fact` / `Chunk` units (see `KnowledgeUnitKind` enum in [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md) §2) |
| **Synthesized wiki** | §4.1 `wiki/`, §4.2 `concepts/`, §4.3 `shared/` + `agent-name/` | `CompiledArticle` kind + `CompiledArticlePayload` |
| **Index** | All variants | `HybridRetrievalEngine` query; `index.md` is computed from `CompiledArticlePayload.title` + `keywords` projection |
| **Log + audit** | §4.1 `log.md`, §4.3 `COMPACT-LOG.md` | `wiki_audit_log` DBI (append-only, see §6.10) + `compaction_jobs` DBI (operational state, separate) |
| **Maintainer cron** | §4.2 daily flush, §4.3 6h cron, §4.4 GitHub Actions | `CompactionWorker` running `SummaryPromotionJob` |
| **Anti-mусор mechanics** | §4.3 `injection_count` + 60-day archive, weekly compact | `CompiledArticlePayload.injection_count` + `LifecycleState::Deprecated` transition via `ArchiveColdJob` |
| **Frontmatter `keywords`** | §4.3 | `CompiledArticlePayload.keywords` + `SearchProjection::tags` (see [`lexical-search-roadmap.md`](lexical-search-roadmap.md) §"Projection Build Rules Per ProjectionKind" — `Original.tags`, `Summary.tag`) |

### §5.4. Per-variant substrate recipe

**§4.1 Obsidian playbook variant.** `BasicRagStack` for the raw side
(`Chunk` units with `ChunkPayload`); `CompiledWikiStack` for the wiki side
(`CompiledArticle` units with `CompiledArticlePayload`). One stack per
project. `HybridRetrievalEngine` (real) currently provides lexical
passthrough + extension hooks; dense + graph + cross-scope fusion are
planned but not wired in (see §5.6). `SummaryPromotionJob` (planned) runs
daily at the operator's chosen cron.

**§4.2 Self-Evolving Memory variant.** `SpeakerAwareChatStack` or
`AgentLongTermMemoryStack` for session log ingest (ConversationEpisode +
usage tracking); `CompiledWikiStack` for synthesized concepts and
connections. The "connections" directory becomes `graph_edges_by_src` /
`graph_edges_by_dst` DBI entries; see
[`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §12.3.

**§4.3 Karpathy Wiki variant.** `CompiledWikiStack` directly. The
`injection_count` frontmatter becomes `CompiledArticlePayload.injection_count`
(see [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md) §5.6.4 for
lifecycle). The 6-hour cron is a `CompactionWorker` schedule running
`SummaryPromotionJob` + `DecayJob` + `ArchiveColdJob`. The weekly compact
mirrors the playbook's LINT → ARCHIVE → MERGE → SUMMARIZE → HARD LIMIT →
REPORT pipeline; map each step to a job type.

**§4.4 OpenWiki variant.** External CLI; `agent-memory-cpp` acts as the
backend. The GitHub Actions daily PR maps to an external scheduler calling
`SummaryPromotionJob` on the MDBX file. The model is configurable per
installation (OpenRouter, Fireworks, Anthropic, etc.); the C++ core does not
care which model is used.

**§4.5 Second Brain variant.** Same substrate as §4.1 / §4.3; the
difference is dashboards and starter templates, which are external tooling
on top of the wiki.

### §5.5. The maintainer agent loop on `agent-memory-cpp`

The maintainer cron is the heart of the LLM Wiki. On `agent-memory-cpp` it
is a `CompactionWorker` (planned) running jobs:

```text
[Daily tick]
  1. CompactionWorker reads compaction_jobs queue (planned: persistent FIFO).
  2. SummaryPromotionJob runs (planned):
       - Find candidate units: usage_count >= threshold, last_decay_score >= threshold.
       - Cluster candidates (by embeddings or fixed N).
       - For each cluster:
           - Generate summary (LLM via ITextAdapter or extractive top-K).
           - Create CompiledArticle unit with CompiledArticlePayload
             (planned; see knowledge-units-roadmap.md §5.6):
               title = cluster centroid keywords
               body = generated summary
               derived_from = source unit_ids
               status = Draft  (sources remain Active — see §5.7)
       - Validation + policy gate (see §5.7) determines whether the
         article is Approved and sources flip to Superseded, or the
         article is Rejected and sources stay Active.
  3. DecayJob updates retrieval scores (planned; canonical formula in
     policies-roadmap.md §2.3):
       apply_decay_and_boost(base_score, usage, policy, now_ms,
                             last_used_at_ms)
       apply_filters(score, usage, speaker, agent_self_id, now_ms, policy)
  4. ArchiveColdJob retires articles whose injection_count == 0 and
     last_updated > 60 days (planned; ArchiveColdParams in compaction-roadmap.md §4.4).
  5. compaction_handoffs DBI records the run (planned; see
     compaction-roadmap.md §5.3).
```

The agent loop above mirrors the Cole Medin "Compiler" + the Karpathy
"wiki-maintainer" + the playbook's daily flush. On `agent-memory-cpp` the
heavy lifting is inside `CompactionWorker`; the agent (or scheduler) only
enqueues.

### §5.6. HybridRetrievalEngine: real-vs-planned capability split

**Implemented today (real code):**

- `HybridRetrievalEngine` provides a facade over the retrieval pipeline.
- Current default implementation composes the underlying `ILexicalIndex`, an `IQueryAnalyzer` (default `PassthroughQueryAnalyzer`), and an `IReranker` (default `IdentityReranker`).
- Initial behavior is a pure lexical passthrough — analyze-then-rerank is identity, so the engine is observably equivalent to calling `ILexicalIndex::search()` directly.

**Planned (NOT yet implemented):**

- Dense candidate injection alongside lexical candidates.
- Graph expansion (BFS-with-depth-bounded, spreading activation, etc.).
- Cross-scope fusion (RRF or another multi-retriever scheme).
- MemoryStack-driven stacking of retrievers.

Until the planned items ship, "HybridRetrievalEngine" is a façade with lexical passthrough + extension hooks. Do not assume graph or dense behavior is wired in automatically — verify by reading the current `HybridRetrievalEngine::retrieve` implementation before relying on it.

### §5.7. CompiledArticle lifecycle (human-review-aware)

**Current flow (DO NOT USE in production):**

```
source units remain Active
→ generate article → create CompiledArticle
→ mark source units Superseded
```

**Required staged flow:**

```
source units remain Active
→ generate article → create CompiledArticle { status: Draft }
→ validation: cross-check generated_draft against derived_from evidence
→ policy gate (auto-approve confidence ≥ threshold OR human review required)
→ on approval: CompiledArticle { status: Active }, source units → Superseded
→ otherwise: CompiledArticle { status: Rejected }, source units stay Active
```

**Critical:** sources must remain Active and retrievable as evidence until the article is Approved, NOT Superseded at generation time. Compound-hallucination failure mode:

1. LLM generates a faulty summary
2. Faulty article becomes authoritative immediately
3. Sources marked Superseded → no longer retrievable as evidence
4. Next generation references the faulty article as ground truth
5. Errors compound across iterations

`Superseded` should mean "no longer primary result for the topic", NOT "unavailable for evidence retrieval". A Superseded source should still be returnable when an audit / contradiction check is needed.

## §6. Practical setup checklist

Concrete steps to deploy an LLM Wiki on top of `agent-memory-cpp`.

### §6.1. Stack selection

Pick the in-house stack based on workload:

- Personal KB, code-sparse, dense retrieval desired → `BasicRagStack`
  + optional `enable_compiled_article` (in-place minor upgrade, see
  [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §14.1).
- Long-term agent with decay + graph overlay + CompiledArticles →
  `CompiledWikiStack` (planned) directly.
- Full research workload with everything → `FullResearchMemoryStack` (planned).

### §6.2. MemoryProfileSpec configuration

For `CompiledWikiStack` (planned) the spec is:

```text
enable_lexical_bm25f       = true
enable_dense_vectors       = true
enable_compiled_article    = true   // capability for CompiledArticle kind
enable_fact_payload        = true   // candidates for promotion are Fact units
enable_embedding_meta      = true   // track embedding provenance
enable_compaction          = true   // SummaryPromotionJob requires Compaction
context_budget             = total_tokens=6000, qa=0, chunk=2000,
                            graph=0, summary=2000, evidence=512
dense_index_config.mode    = BinaryCandidateFilter  # planned candidate mode
                            or Hnsw for >100k units
```

`enable_compaction = true` is a hard requirement: validation rule
"CompiledArticles requires Compaction" (see
[`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §10 row 7) blocks
`enable_compiled_article = true` if compaction is off.

**Note:** `BinaryCandidateFilter`, `BinarySignature`, `IBinarySignatureEncoder`, `RandomHyperplaneLSH`, and `binary_bucket_index` (MDBX) are NOT yet implemented. This block is forward-looking configuration; running it today will fail. Subject to implementation gates per `binary-embeddings-roadmap.md`.

### §6.3. Folder / DBI layout

Map the Obsidian variant's folder layout onto MDBX DBIs:

```text
raw/                → knowledge_units DBI (Chunk + Fact units)
                     + unit_components DBI (EmbeddingMeta)
                     + unit_projections DBI (Original projection)

wiki/               → compiled_article_payloads DBI
                     + knowledge_units DBI (CompiledArticle kind)
                     + unit_projections DBI (Summary projection)

index.md (computed) → generated from CompiledArticlePayload.title + keywords
                     + metadata_filters DBI reverse index on title/keywords

log.md (audit)      → wiki_audit_log DBI (append-only, see §6.10)
                     + compaction_jobs DBI (operational state, separate)

CLAUDE.md / AGENTS.md → not stored in MDBX; lives in repo or vault
```

DBI shapes are detailed in [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §12.

### §6.10. Audit log (separate from job queue)

Job queues and audit logs are DIFFERENT substrates:

- **compaction_jobs DBI**: per-job state (queued → running → done → failed). MAY be deleted after retention window. Mutable.
- **compaction_handoffs DBI**: technical completion records (which worker, when, what version). Mutable.

For human-reviewable LLM Wiki, **the audit log must be append-only** with:

```
run_id
input unit IDs + revisions (the "derived_from" set)
prompt/template version
model ID
generated_draft hash (content hash, not the full draft)
validation result + comparison vs derived_from evidence
reviewer decision (auto / human)
activated article revision (or rejection)
superseded source IDs (set, not just count)
timestamp (RFC3339, monotonic)
```

Treat this audit as immutable. Do not retroactively edit; if changes are needed, append a corrective record.

A separate table (e.g., `wiki_audit_log`) is the canonical source. Job DBI is operational; audit DBI is governance.

### §6.11. Authorization before fusion

Cross-scope retrieval ("scope A + scope B" queries) bypasses the implicit per-tenant isolation that single-scope queries preserve. Without explicit authorization, fusion can turn a namespace into a ranking hint instead of a security boundary.

Rules:

- Authorization determines the **allowed scope set** BEFORE retrieval begins.
- Fusion operates ONLY inside the pre-authorized set. Out-of-scope fusion is a security failure, not a quality issue.
- Metadata filters and ranking MUST NOT expand access. They can only narrow.
- Mem0-style tags (`user_id`, `agent_id`, `run_id`, `app_id`) are access-control, NOT search relevance.

When configuring a MemoryStack that fuses multiple scopes:

- Default to empty scope set (refuse by default).
- Authorization is required to add scopes.
- Audit each cross-scope query (request_id → scope_set → result_ids).

### §6.4. Ingest workflow

When a new raw resource arrives:

```text
1. Hash the resource for resource_id + content hash (planned:
   KnowledgeUnitKey content-addressing, see knowledge-units-roadmap.md §4).
2. IResourceStore adapter ingests raw bytes (see architecture.md
   "Planned Resource Reindexing Direction").
3. Chunkers split into Chunk units (see chunkers-roadmap.md).
4. Write units via stack.write_unit(WriteRequest):
     - envelope.kind = Chunk
     - payload = ChunkPayload
     - projections = Original projection
     - embeddings via IEmbedder (planned)
5. On-write cheap compaction:
     - Schema validation
     - LifecycleState = Active
     - CompactionMetaComponent.dirty_decay = true
     - knowledge_units_by_kind update
     - If dirty threshold reached → enqueue DecayJob
6. MultiTableWriter commits atomically.
```

### §6.5. Compile / synthesis cadence

Two options:

- **On-write synthesis.** Every Chunk write triggers a lightweight
  SummaryPromotionJob check (low threshold). Latency on the write path
  grows; freshness is high.
- **Daily / 6h synthesis.** `WritePolicy.flush_interval_ms` (planned)
  controls the cadence. Latency on the write path stays low; freshness
  is bounded by the cadence.

Recommended default: daily synthesis with `WritePolicy.flush_interval_ms
= 24h * 3600 * 1000`. Operator runs `agent-memory-cli compaction
trigger SummaryPromotion` for ad-hoc synthesis (planned CLI).

### §6.6. Query workflow

```text
1. User query arrives via stack.retrieve(plan):
     - raw_query = user text
     - scope_ids = {project scope}
     - mode = Hybrid
2. IQueryTransformer expands (optional, M2+; planned:
   HydeQueryTransformer, RewriteQueryTransformer).
3. HybridRetrievalEngine (real) currently runs:
     - lexical: BM25F over Original + Summary projections (via ILexicalIndex)
     - dense: NOT wired in (planned, see §5.6)
     - graph: NOT wired in (planned, see §5.6)
4. RRF / cross-scope fusion NOT wired in today (planned, see §5.6).
5. IContextBuilder assembles context within ContextBudget (planned):
     - chunk_tokens = 2000, summary_tokens = 2000, evidence_tokens = 512
     - trim order: evidence → summary → chunk
6. Optional IRetrievalEvaluator (M2+; planned: CragRetrievalEvaluator)
   triggers corrective action.
```

### §6.7. Lint workflow

The "lint" command from the Obsidian playbook maps to `CompactionWorker`
job types:

- LINT (broken links, missing frontmatter) → `ArchiveColdJob` with
  `metadata_filter = missing_compiled_article_payload`.
- MERGE (duplicate articles) → `MergeJob` (planned, M2+; see
  [`compaction-roadmap.md`](compaction-roadmap.md) §4.3) with
  `MergeStrategy::Summarize` and LLM-backed dedupe.
- ARCHIVE (zero-injection articles) → `ArchiveColdJob` with
  `score_threshold = 0` and `older_than_ms = 60d`.
- HARD LIMIT (>100 active articles) → tighten
  `SummaryPromotionParams.usage_threshold` and re-run.

### §6.8. Index maintenance

There is no separate `index.md` to maintain. The "index" is computed at
query time:

- `CompiledArticlePayload.title` → BM25F on `Summary.title` projection
  field (see [`lexical-search-roadmap.md`](lexical-search-roadmap.md)
  §"Field Model Per ProjectionKind" — `Summary` projection uses
  `summary`, `tag`, `title` fields).
- `CompiledArticlePayload.keywords` → BM25F on `Summary.tag` projection
  field.
- `HybridRetrievalEngine` ranks the candidates.

If the user wants a human-readable `index.md`, run `agent-memory-cli dump
compiled-articles --format markdown` (planned CLI) to generate one
on-demand.

### §6.9. Prompt caching for synthesis

Synthesis prompts (raw → article) are large and repetitive. Use
`PromptPrefixCache` (planned; see
[`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §13 runtime
services) to cache the system-prompt prefix across synthesis calls:

```text
cache_key = sha256(system_prompt_prefix + model_id + scope_id)
hit       = cache_key in prompt_prefix_cache_meta DBI
miss      = full system prompt; cache_key → response_prefix
```

`PromptPrefixCache` is scope-aware: cache key includes `scope_id` so two
projects don't share each other's prompts (see open issue 17.7 in
[`memory-stacks-roadmap.md`](memory-stacks-roadmap.md)).

## §7. Failure modes and mitigations

What goes wrong in production and how each variant handles it.

### §7.1. Compound hallucination

**Symptom.** LLM-generated wiki articles quote facts that were never in
the raw corpus. Repeated synthesis compounds the error: each article
references the previous (hallucinated) article, drifting further from the
source.

**Mitigations.**

- Always generate summaries from the **same** raw notes, not from other
  summaries. CompactionWorker job isolation: `SummaryPromotionParams.target_owner`
  and `target_readers` constraints prevent accidental cross-cluster
  contamination (see [`compaction-roadmap.md`](compaction-roadmap.md) §4.5).
- Add `SourceRef` (planned; see [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md) §3) to every
  `CompiledArticle` unit. Retrieval surfaces citations; humans can audit.
- Weekly lint cycle (LLM-driven or rule-based) flags articles with no
  in-scope `SourceRef`.

### §7.2. Stale index

**Symptom.** A wiki article is updated, but search results still show
the old version. Caused by missing or stale index entries.

**Mitigations.**

- On-write cheap index refresh: every `CompiledArticle` write triggers
  SearchProjection regeneration + BM25F reindex of `Summary` projection
  only (not `Original`). Targeted reindexing by `projection_kind` (see
  [`lexical-search-roadmap.md`](lexical-search-roadmap.md) §"Targeted
  Reindexing Per Projection Kind").
- Stale-filter via `unit_revision` (planned; see
  [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §17.11):
  `LexicalPosting.unit_revision < envelope.revision` → skip posting at
  retrieval time.

### §7.3. Wiki drift / bloat

**Symptom.** Wiki grows to thousands of articles; `index.md` no longer
fits in the context window; retrieval accuracy drops.

**Mitigations.**

- Hard limit on active articles (the playbook uses 100; agent-memory-cpp
  has no built-in limit but operator can enforce via
  `SummaryPromotionParams.max_articles_per_run` and a periodic
  `ArchiveColdJob`).
- CompactionWorker enforces `cold_threshold` (planned; see
  `policies-roadmap.md` §2.3): articles below the threshold become
  archive candidates.
- DecayPolicy with `mode = Exponential` and `half_life_ms = 7d`
  gradually retires rarely-injected articles. The default
  `AgentLongTermMemory` profile is a starting point.

### §7.4. Cross-agent contamination

**Symptom.** Two agents sharing the same wiki see each other's private
notes.

**Mitigations.**

- Scope isolation: `scope_id` is part of every secondary index key
  (see [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §12.3).
  Different agents get different `ScopeId` values; cross-scope lookup is
  impossible by default.
- `RetrievalPlan.scope_ids` filters at query time; agents cannot
  bypass the scope filter without explicit `ScopeId::global()` opt-in.

### §7.5. Synthesis cost overrun

**Symptom.** Daily synthesis exceeds the LLM budget because raw corpus
grew faster than expected.

**Mitigations.**

- `SummaryPromotionParams.max_articles_per_run` caps the work per cycle.
- DecayPolicy `cold_threshold` filters out low-relevance candidates
  before synthesis.
- Pre-filter on `usage_count` and `last_decay_score` in
  `SummaryPromotionJob` (planned; see [`compaction-roadmap.md`](compaction-roadmap.md)
  §4.5). Cheap candidates are skipped.
- Batch size control via `MultiTableWriter` (planned; see
  [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §3 + §16 step
  14). AsyncIndexer batches inserts (planned, M1+; see open issue 17.10).

### §7.6. Recompilation loops

**Symptom.** An article is promoted, then demoted, then re-promoted in a
loop.

**Mitigations.**

- `MergeJob` (planned, M2+) records `merged_into` lineage.
- `LifesycleState` is durable: a `Superseded` article cannot be
  re-promoted without an explicit `Active` transition (see lifecycle
  FSM in [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md) §6).
  Note: this durability rule applies to articles, not sources. Source
  units remain `Active` until the article is Approved — see §5.7 for the
  staged lifecycle that protects source-as-evidence.
- `DecayJob` is idempotent: repeated runs give the same result (see
  [`compaction-roadmap.md`](compaction-roadmap.md) §4.1).

### §7.7. PII / secrets leakage

**Symptom.** Raw notes contain API keys, passwords, or PII; wiki articles
inherit them.

**Mitigations.**

- This is the same concern Cole Medin flags in his playbook checklist
  ("Настроить PII-фильтрацию (опционально, но рекомендуется)").
  PII filtering happens upstream of `write_unit`.
- `CompiledArticlePayload.readers` (planned; see
  [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md) §5.6) lets
  you tag articles with audience constraints; downstream retrieval can
  filter by `readers` field.

### §7.8. Cost of the maintainer cron

**Symptom.** Daily LLM-driven synthesis on a large corpus is expensive.

**Mitigations.**

- Default to extractive summarisation (top-K sentences) rather than
  LLM-generated summaries. `SummaryPromotionParams.use_llm_summarizer =
  false` switches to extractive mode (planned; see
  [`compaction-roadmap.md`](compaction-roadmap.md) §4.5).
- Reduce synthesis cadence: 6h → 24h → weekly.
- Prompt caching (planned; `PromptPrefixCache`) cuts the per-article
  cost by amortising the system-prompt prefix.

## §8. References

### §8.1. Public sources cited

- **github.com/langchain-ai/openwiki** — OpenWiki CLI documentation for
  agents. GitHub Actions daily PR pipeline; agent-first documentation
  format; AGENTS.md augmentation.
- **Karpathy talks on LLM-as-OS (2025)** — 3-layer memory pattern
  (conversations + facts + wiki) with maintainer cron. No canonical arXiv
  or blog post; attribution is to the talks themselves.

### §8.2. Internal notes cited

- `ai-agent-playbook/concepts/ai-agents/Self-Evolving Memory для Claude Code на основе LLM Knowledge Base.md`
  — Cole Medin + Karpathy compiler analogy + Compounding Knowledge Loop.
- `ai-agent-playbook/concepts/ai-agents/Автоматическая документация как память агента — OpenWiki и подходы.md`
  — OpenWiki + symgraph composition, AGENTS.md augmentation, GitHub
  Actions cron, LangSmith tracing.
- `ai-agent-playbook/playbooks/ai-agents/LLM Wiki в Obsidian — практический сетап с Claude Code.md`
  — Obsidian variant: `raw/` + `wiki/` + `index.md` + `log.md` +
  `CLAUDE.md` structure.
- `ai-agent-playbook/playbooks/ai-agents/Построить AI Second Brain на Claude Code и Obsidian.md`
  — Second Brain build playbook.
- `ai-agent-playbook/resources/rag-knowledge/LLM Wiki Карпатого для личной базы знаний - критика - разбор статьи.md`
  — Karpathy LLM Wiki critical review; reflexive vs instrumental
  knowledge bases.
- `ai-agent-playbook/resources/rag-knowledge/RAG и LLM Wiki - как работают - расшифровка.md`
  — RAG vs LLM Wiki mechanics.
- `ai-agent-playbook/resources/ai-agents/Karpathy LLM Knowledge Base — адаптация для Claude Code памяти - расшифровка.md`
  — Karpathy LLM Knowledge Base transcription + Cole Medin adaptation.
- `ai-agent-playbook/resources/ai-agents/Второй мозг и LLM-Wiki - разбор статьи.md`
  — Second Brain + LLM Wiki article breakdown.
- `ai-agent-playbook/tools/rag/Second Brain Research Dashboard.md`
  — Second Brain research dashboard tooling.
- `ai-agent-playbook/tools/rag/Second Brain Starter.md`
  — Second Brain starter template.
- `ai-agent-playbook/tools/rag/OpenWiki — CLI documentation for agents.md`
  — OpenWiki CLI tool reference.
- `ai-agent-playbook/concepts/ai-agents/Трёхслойная память для AI-агентов - метод Карпати.md`
  — Karpathy 3-layer memory + Anna_AI 4-tier variant + 4-th layer
  Obsidian practice.

### §8.3. In-house guides

- [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) — ADR-001 through
  ADR-015, `MemoryProfileSpec`, `MemoryStack`, capability matrix, MDBX
  layout, M0/M1/M2 maturity. Section 8 lists the 7 default stacks
  including `CompiledWikiStack`.
- [`knowledge-units-roadmap.md`](knowledge-units-roadmap.md) —
  `CompiledArticlePayload` (§5.6), lifecycle FSM (§6),
  `KnowledgeUnitKind` enum (§2).
- [`compaction-roadmap.md`](compaction-roadmap.md) — `CompactionWorker`
  (§2), `SummaryPromotionJob` (§4.5), handoff structure (§5),
  scheduling (§6), default stack configurations (§8).
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — retrieval
  flow, `HybridRetrievalEngine`, `DecayAwareRetriever`, evaluation
  pipeline.
- [`lexical-search-roadmap.md`](lexical-search-roadmap.md) — BM25/BM25F,
  projections, projection build rules per `ProjectionKind`
  (§"Projection Build Rules Per ProjectionKind" covers `Summary`).
- [`policies-roadmap.md`](policies-roadmap.md) — `DecayPolicy`,
  `WritePolicy`, `SpeakerScopePolicy` with default values.
- [`optimization-roadmap.md`](optimization-roadmap.md) — vector / binary
  / ANN optimization layer behind the lexical front-end.
- [`architecture.md`](architecture.md) — 4-layer model + `IResourceStore`
  adapter pattern.
- [`usage-memory-models.md`](usage-memory-models.md) — companion operator
  guide for choosing between memory architectures.
- [`code-intelligence-roadmap.md`](code-intelligence-roadmap.md) — for
  wikis over code (Pattern 1 Bounded BFS, Pattern 2 schema
  introspection).
- [`chunkers-roadmap.md`](chunkers-roadmap.md) — format-specialized
  chunkers (OpenAPI, Markdown, AsciiDoc, PlantUML, HTML, Legal-strukturalnyy,
  Docling multimodal) for raw resource ingest.
- [`research-reading-map.md`](research-reading-map.md) — research reading
  map backing the in-house design.
