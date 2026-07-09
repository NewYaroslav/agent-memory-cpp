# Storage Stack Boundaries: agent-memory-cpp ↔ mdbx-containers

## Назначение

Этот документ фиксирует разделение ответственности между проектом `agent-memory-cpp` и upstream-библиотекой `external/mdbx-containers/`. Документ предназначен для AI-агентов, работающих над `agent-memory-cpp`. Для расширений самой `mdbx-containers` см. `guides/mdbx-containers-extension-tz.md`.

## Принцип

agent-memory-cpp реализует ДОМЕННЫЕ КОНТРАКТЫ (KnowledgeUnit, RetrievalPlan, IContextBuilder, eval, ...) и АДАПТЕРЫ к хранилищу (MdbxKnowledgeUnitStore, MdbxQAKnowledgeBase, MdbxGraphStore, ...). Адаптеры строятся ПОВЕРХ generic-примитивов и upstream-модулей mdbx-containers.

## Что реализуется в mdbx-containers (upstream)

### Generic-примитивы (наши требования в TZ)

- `ReverseIndexTable` — secondary index через DUPSORT
- `RangeIndexTable` — range queries + pagination
- `TypeDiscriminatedTable<EnumTag, ValueVariant>` — type-tag polymorphic
- `CompositeKey<K1, K2>` + helpers — typed composite keys
- `MultiTableWriter` / `Connection::multi_write` — atomic multi-table writes
- `KeyValueTable` extensions (batch, diagnostics, paginated_range)
- `Config` extensions (enable_metrics, table_creation_callback)
- `HybridSearch::rrf_fuse` — RRF helper
- Utility functions (hamming_*, sortable_key_from_*, hex)

### Domain-модули (по upstream roadmap)

- `DocumentStore`, `ChunkStore` — базовые контейнеры для документов/чанков
- `TextIndex` (inverted) — token → posting
- `GraphStore` (typed directed edges) — node/edge storage
- `EventStore` / `TimelineStore` — события с range queries
- `MemoryStore` / `MemoryUsageStore` — facts с decay
- `EntityStore` / `AliasTable` — entities с alias resolution
- `EvidenceSet` / `RetrievalTraceStore` — provenance для retrieval
- `TaskQueue` / `JobStore` — persistent job queue
- `IdAllocatorTable` / `CounterTable` — стабильные ID
- `CollectionMetadata` / `MetadataTable` — typed metadata для коллекций

### Что НЕ реализуется в mdbx-containers

- LLM-вызовы (любые модели, провайдеры, prompt engineering)
- Embedding model execution (ONNX, llama.cpp, OpenAI-compat, ...)
- Парсеры (markdown, PDF, DOCX, code, ASR, VLM)
- Cross-encoder rerankers
- Chunker'ы (HybridChunker, sentence splitter, sliding window)
- RAG orchestration (planner-guided retrieval, agent loops)
- HTTP/MCP/API серверы
- UI и приложения

## Что реализуется в agent-memory-cpp

### Доменные контракты (`src/agent_memory/...`)

- `domain/KnowledgeUnit` — обобщение над Chunk/QAPair/Fact/Event/Entity/Relation/Section/Summary/Episode
- `domain/SourceRef` — first-class provenance (миграция из facts/)
- `domain/TypedMetadata` + расширенные `MetadataFilter` (Range, In, Tag)
- `retrieval/RetrievalPlan` — что включать в retrieval
- `retrieval/IUnitRetriever` — multi-retriever composition
- `retrieval/IContextBuilder` — budgeted context assembly
- `eval/RetrievalTrace` + `eval/RetrievalDataset` + `eval/RetrievalMetrics` — eval harness
- `lexical/IChunkEnricher` (расширенный до `SearchProjection`) — Contextual Retrieval
- `lexical/IReranker`, `lexical/IQueryAnalyzer` — pluggable slots для LLM-адаптеров
- `qa/IQAKnowledgeBase` — QAPair storage и retrieval
- `facts/IFactStore`, `facts/ITemporalIndex`, `facts/Event` — facts/events
- `graph/IGraphStore`, `graph/GraphTypes` — граф с bounded expansion
- `context/ContextBudget`, `context/ContextBlock` — budgeted structures

### Адаптеры к mdbx-containers (`src/agent_memory/infrastructure/mdbx/`)

- `MdbxKnowledgeUnitStore` (planned) — реализует `IKnowledgeUnitStore` через `TypeDiscriminatedTable<KnowledgeUnitKind, ...>` + reverse indexes
- `MdbxQAKnowledgeBase` (planned) — реализует `IQAKnowledgeBase` через `KeyValueTable<QAPairId, QAPairPayload>` + `ReverseIndexTable<token, QAPairId>`
- `MdbxFactStore` (planned) — реализует `IFactStore` через `KeyValueTable<FactId, FactPayload>` + `ReverseIndexTable<token, FactId>` + MemoryUsageStore integration для decay
- `MdbxTemporalIndex` (planned) — реализует `ITemporalIndex` через `RangeIndexTable<sortable_uint64_timestamp, EventPayload>`
- `MdbxGraphStore` (planned) — реализует `IGraphStore` через `KeyValueTable<NodeId, NodePayload>` + `ReverseIndexTable<(src, edge_kind), EdgePayload>` + `ReverseIndexTable<dst, src>`
- `MdbxResourceMetadataFilters` (planned) — `ReverseIndexTable<(metadata_key, metadata_value), ResourceId>` для pre-filter
- Существующие: `MdbxDocumentStorage`, `MdbxResourceManifestStorage`

### Retrieval composition

- `HybridRetrievalEngine` — multi-retriever + RRF + cross-encoder rerank slot + LLM intent analyzer slot
- `CompositeRetrievalEngine` (planned) — generic composition поверх IUnitRetriever-ов
- `TrimmedContextBuilder` (planned) — budgeted assembly с order: QA → chunks → summaries → graph

### LLM/embedding адаптеры (`src/agent_memory/adapters/` или `examples/`)

- `OpenAICompatEmbedder`, `OnnxEmbedder`, `LlamaCppEmbedder` — embedding backends
- `AnthropicContextualizer` — Contextual Retrieval через Claude Haiku
- `LlmQueryRewriter` — RAG Fusion
- `LlmRelevanceClassifier` — Corrective RAG validator
- `CrossEncoderReranker` — через ONNX
- `MarkdownHybridChunker`, `DoclingParser`, `WhisperTranscriber` — примеры парсеров

## Правила зависимости

1. **agent-memory-cpp ЗАВИСИТ от mdbx-containers** через PUBLIC interface в `include/mdbx_containers/` (через target `mdbx_containers::mdbx_containers`)
2. **agent-memory-cpp НЕ МОЖЕТ** редактировать файлы в `external/mdbx-containers/` (это upstream submodule)
3. **Новые требования к mdbx-containers** оформляются в `guides/mdbx-containers-extension-tz.md` как спецификация для апстрима
4. **Использование upstream-примитивов** в адаптерах agent-memory-cpp — норма, это идиоматический путь
5. **Fallback на in-memory реализации** (planned: `InMemoryKnowledgeUnitStore`, `InMemoryQAKnowledgeBase`, `InMemoryTemporalIndex`, `MemoryGraphStore`) для тестов и проектов без MDBX

## Когда какой путь выбирать

| Сценарий | Путь |
|---|---|
| Нужен persistent storage в production | MDBX adapter поверх upstream примитивов |
| Unit-тесты, контракт-тесты | In-memory адаптер (planned) |
| Embedded deployment без MDBX | In-memory адаптер |
| Новая таблица в agent-memory-cpp | Сначала проверить: есть ли generic-примитив в mdbx-containers? Если да — использовать. Если нет — добавить в TZ |
| Новая доменная абстракция (например, новый KnowledgeUnitKind) | Только в agent-memory-cpp, в mdbx-containers НЕ идёт |
| Новый generic storage pattern (например, новый вид индекса) | Сначала в TZ для mdbx-containers |

## Связь с другими документами

- `guides/mdbx-containers-extension-tz.md` — спецификация расширений mdbx-containers
- `guides/knowledge-base-roadmap.md` — общий roadmap KB архитектуры
- `guides/knowledge-units-roadmap.md` — спецификация KnowledgeUnit типов
- `guides/lexical-search-roadmap.md` — BM25F fielded storage
- `guides/optimization-roadmap.md` — compression, binary signature index, bucket index
- `guides/architecture.md` — общая архитектура проекта
- `guides/memory-stacks-roadmap.md` — центральный манифест архитектуры (CapabilitySet, MemoryProfileSpec, MemoryStack, MDBX layout, Maturity Levels M0/M1/M2).