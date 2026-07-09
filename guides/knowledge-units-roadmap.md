# knowledge-units-roadmap.md

Спецификация per-kind payload-компонентов и lifecycle FSM для подсистемы памяти `agent-memory-cpp`. Документ конкретизирует компонентную модель, описанную в `guides/memory-stacks-roadmap.md` (ADR-001), и согласована с retrieval-флоу в `guides/knowledge-base-roadmap.md`.

> В будущем планируется переименование файла в `knowledge-payloads-roadmap.md` после завершения всех изменений; текущее имя сохранено для совместимости cross-references.

## 1. Purpose

Этот документ описывает:

- `KnowledgeUnitKind` (canonical enum) и kind → payload mapping (какой kind использует какой payload).
- Per-kind правила генерации `primary_text` и `SearchProjection`.
- `SourceRef` contract и migration mapping.
- `KnowledgeUnitId` hash-scheme (с разделителями `<kind>:<scope>:<hash>`).
- Lifecycle FSM (включая `SoftSuppressed` state) и anti-loop подсвинок.
- Manifest integration через `DerivedRecordKind`.
- Storage pattern: per-component DBI allocation и capability-aware DBI creation.
- Migration plan от старого монолитного `KnowledgeUnit` к новой envelope + components модели.

Cross-references:

- `guides/memory-stacks-roadmap.md` — ADR-001 (envelope + components), `MemoryProfileSpec`, `MemoryStack`, MDBX layout, maturity levels.
- `guides/knowledge-base-roadmap.md` — envelope shape, retrieval flow, decay-aware scoring, evaluation pipeline.
- `guides/lexical-search-roadmap.md` — BM25F по `SearchProjection`s, field-weighted indexing.
- `guides/optimization-roadmap.md` — vector/binary storage, multi-projection embeddings.

Non-goals: BM25F scoring internals, embedding model адаптеры, compaction job types (см. `compaction-roadmap.md`, future).

## 2. KnowledgeUnitKind (canonical enum)

Канонический enum фиксирован в `memory-stacks-roadmap.md` (см. ADR-001, ADR-004) и используется в `KnowledgeUnitEnvelope.kind` как дискриминатор per-kind semantics. Изменение значений или имён — breaking change.

```cpp
enum class KnowledgeUnitKind : uint16_t {
    Chunk = 0,
    QAPair = 1,
    Fact = 2,
    Event = 3,
    Entity = 4,
    Relation = 5,
    Summary = 6,
    CompiledArticle = 7,
    ConversationEpisode = 8,
    Note = 9,
    Task = 10,        // reserved
    Decision = 11,    // reserved
    Custom = 12,      // escape hatch через metadata_typed["payload"]
    // NUM_KINDS = 13
};
```

Ссылка на `memory-stacks-roadmap.md` секцию 6.3 для размещения `kind` в envelope.

### 2.1. Kind → Payload mapping

| Kind | Primary payload | Optional additional | Notes |
|---|---|---|---|
| Chunk | ChunkPayload | — | default kind для indexed retrieval |
| QAPair | QAPayload | — | short-circuit через QALookup slot |
| Fact | FactPayload | — | для temporal/bi-temporal knowledge |
| Event | (no specific payload) | Embedded в primary_text | lightweight, декларативный |
| Entity | (no specific payload) | Embedded в primary_text | name + type |
| Relation | (no specific payload) | Embedded в primary_text, graph edges | |
| Summary | (no specific payload) | Embedded в primary_text | компрессированное представление |
| CompiledArticle | CompiledArticlePayload | — | Karpathy-style wiki articles |
| ConversationEpisode | ConversationEpisodePayload | — | multi-utterance bundle |
| Note | (no specific payload) | Embedded в primary_text | generic free-form |
| Task | (reserved) | — | для future handoff structure |
| Decision | (reserved) | — | для future handoff structure |
| Custom | metadata_typed["payload"] | — | escape hatch через JSON-like value |

Примечание: для kinds без dedicated payload (Event, Entity, Relation, Summary, Note) основная информация содержится в `envelope.primary_text`/`display_text` и/или operational components (`SpeakerComponent`, `TemporalComponent` и т.д.).

### 2.2. Reserved kinds

`Task` (handoff records) и `Decision` (decision points в agent reasoning) зарезервированы для follow-up. Контракты будут определены, когда соответствующие use-cases материализуются (M2+).

## 3. SourceRef (canonical contract)

`SourceRef` — first-class provenance для всех retrieval-eligible units. Обязателен для Fact, QAPair, Relation, Event, и любого GraphNode, доступного через expansion. Рекомендуется для Chunk и CompiledArticle.

```cpp
struct TextRange {
    uint64_t byte_offset;
    uint64_t byte_length;
};

struct SourceRef {
    ResourceId resource_id;          // обязательно
    std::string uri;                 // обязательно (для citations)
    TextRange excerpt;               // обязательно для quote-based refs
    std::string excerpt_text;        // сам excerpt (verbatim UTF-8)
    std::array<uint8_t, 16> quote_hash;  // обязательно: SHA1(prefix(excerpt))[:16]
    double confidence;               // [0.0, 1.0]
    std::optional<KnowledgeUnitId> anchor_unit_id;  // связь на parent unit
    std::optional<uint64_t> observed_at_ms;
};
```

### 3.1. Обязательные поля и инварианты

- `resource_id` — обязателен для reverse lookup по ресурсу.
- `uri` — обязателен для citation fidelity (`excerpt_text + uri` = stable citation).
- `quote_hash` — обязателен: 16-byte hex of SHA1(`prefix(excerpt_text)`). Обеспечивает детерминированную идентификацию цитат без хранения полного текста в каждом индексе.

### 3.2. Storage

`SourceRef[]` хранится в `envelope.sources`. Secondary index по `resource_id` строится через `metadata_filters` (DBI) — см. `memory-stacks-roadmap.md` секция 12.3. При необходимости быстрого reverse lookup добавляется отдельный DBI `source_refs_by_resource` (опционально, не в M1 budget).

## 4. KnowledgeUnitId (hash-based scheme)

`KnowledgeUnitId` — opaque strong-typedef, монотонно аллоцируется per backend, никогда не reused после erase.

```cpp
class KnowledgeUnitId {
public:
    static KnowledgeUnitId from_content(
        KnowledgeUnitKind kind,
        ScopeId scope,
        std::span<const uint8_t> content_bytes,
        std::span<const uint8_t> pipeline_salt);

    std::string to_string() const;  // "<kind>:<scope>:<hash_hex>"
    bool operator==(const KnowledgeUnitId&) const;
    std::strong_ordering operator<=>(const KnowledgeUnitId&) const;
private:
    KnowledgeUnitKind m_kind;
    ScopeId m_scope;
    std::array<uint8_t, 16> m_hash;
};
```

### 4.1. Format

`<kind>:<scope>:<16-byte-hex>`, где hash = SHA256(content_bytes || pipeline_salt || kind_salt)[0..16].

Примеры:

- `chunk:user:yaroslav:9b3f4e2a81c5d670`
- `qa:agent:nika:7a1c5d6708b3f4e2`
- `fact:project:agent_memory:5d6708b3f4e2a81c`

### 4.2. Правила

- Ids opaque для storage layer; не парсить hash для routing.
- Префикс `kind:` обязателен (для читаемых логов и traces).
- Hash пересчитывается при изменении `content_bytes`, `pipeline_salt` или `kind_salt`.
- Reuse id после erase запрещён.
- Strong typedef: нет implicit conversion в `std::string` или `ChunkId`.

`KnowledgeUnitId` обязателен для map key, `envelope.id`, `projection.unit_id`, `RetrievalHit.unit_id` и любых cross-reference (anchor_unit_id, supersedes, superseded_by, derived_from).

## 5. Per-Kind Specifications

Детальная спецификация payload-компонентов для каждого kind с dedicated payload. Per-kind `primary_text` rules и projections.

### 5.1. ChunkPayload

Payload для `KnowledgeUnitKind::Chunk`. Chunk — default kind для indexed retrieval.

```cpp
struct ChunkPayload {
    uint64_t byte_offset;
    uint64_t byte_length;
    std::vector<HeadingPathItem> heading_path;  // ["Section 1", "Subsection 2", ...]
    std::vector<std::string> code_blocks;       // detected code fences
    std::vector<std::string> symbols;           // extracted identifiers
    std::optional<std::string> detected_language;
};
```

#### 5.1.1. Per-kind primary_text generation rule

`[H1] > [H2] > first 500 chars of body`. Если `heading_path` пуст — только first 500 chars body. Generation выполняется в `generate_primary_text` (см. `knowledge-base-roadmap.md` секция 3.3).

#### 5.1.2. Per-kind projections

- Original: full body text.
- CodeSymbols: extracted identifiers через code-aware tokenizer (если Chunk содержит code).

#### 5.1.3. Storage

DBI `chunk_payloads` (key = UnitId). `ChunkPayload` всегда присутствует для Chunk kind (в отличие от optional payloads для других kinds). Включается автоматически при `kind == Chunk`, не требует capability flag.

### 5.2. QAPayload

Payload для `KnowledgeUnitKind::QAPair`. Используется в QAKnowledgeBase stack и FullResearch stack.

```cpp
struct QAPayload {
    std::string canonical_question;
    std::vector<std::string> question_variants;  // regex/keywords для QA matching
    std::string answer;
    std::string category;                         // taxonomy
    uint64_t last_verified_at_ms;
    std::optional<std::string> expected_format;  // "text", "json", "code", ...
};
```

#### 5.2.1. Per-kind primary_text

`canonical_question` + первые 200 chars из `answer` как preview. Полный текст хранится в `qa_payloads` DBI, retrieval работает через `SearchProjection`s.

#### 5.2.2. Per-kind projections

- Original: question + answer (full).
- QAQuestion: только `canonical_question` + variants (для matching).
- QAAnswer: только `answer` (для retrieval когда question уже matched).

#### 5.2.3. Storage

DBI `qa_payloads`. Включается через `enable_qa_payload=true`.

#### 5.2.4. Retrieval shortcut

QALookup slot — прямой lookup по `canonical_question` + variants до BM25F, для high-precision коротких запросов. Используется `QARetriever` (см. `knowledge-base-roadmap.md` секция 7.2).

### 5.3. FactPayload

Payload для `KnowledgeUnitKind::Fact`. Используется в AgentLongTermMemory, TemporalFactStore, CompiledWiki и FullResearch stacks.

```cpp
struct FactPayload {
    std::string subject;
    std::string predicate;
    std::string object;
    std::string value_type;            // "string", "number", "date", "bool"
    std::optional<std::string> unit;   // "USD", "kg", "Celsius", ...
    std::vector<std::string> aliases;  // alternative formulations
};
```

#### 5.3.1. Per-kind primary_text

`<subject> <predicate> <object> [<unit>]`. Пример: `Einstein born_in Ulm`. С aliases — через пробел: `Einstein born_in Ulm | Альберт Эйнштейн`.

#### 5.3.2. Per-kind projections

- Original: `"<subject> <predicate> <object> [<unit>]"`.

#### 5.3.3. Storage

DBI `fact_payloads`. Включается через `enable_fact_payload=true`.

#### 5.3.4. Temporal bi-temporal

Facts используют `TemporalComponent.valid_from_ms` / `valid_until_ms` для supersedence chains. Устаревший fact помечается `Superseded` через Lifecycle FSM; новый становится `Active`. Retrieval возвращает только `Active` (если не указан bi-temporal query).

### 5.4. EventPayload (опциональный)

Для `KnowledgeUnitKind::Event` dedicated payload не обязателен — обычно достаточно `TemporalComponent` + `primary_text`. Если use-case требует structured event data, добавляется:

```cpp
struct EventPayload {
    std::vector<std::string> actors;
    std::string action;
    std::optional<std::string> location;
    uint64_t occurred_at_ms;          // когда произошло (≠ observed_at_ms)
    uint64_t observed_at_ms;          // когда записан в memory
};
```

Примечание: в M1 Event обходится без dedicated payload, используя `TemporalComponent` + `envelope.primary_text`. Если payload нужен — включается аналогично FactPayload, через `enable_event_payload` (M2+).

### 5.5. ConversationEpisodePayload

Payload для `KnowledgeUnitKind::ConversationEpisode`. Используется в SpeakerAwareChatStack и FullResearch stack.

```cpp
struct ConversationEpisodePayload {
    std::vector<UtteranceId> utterance_ids;
    uint64_t started_at_ms;
    uint64_t ended_at_ms;
    uint64_t turn_count;
    std::vector<SpeakerId> participants;
    std::optional<std::string> topic;
    std::optional<UnitId> previous_episode_id;  // chain в stream
};
```

#### 5.5.1. Per-kind primary_text

Первые 1-2 реплики (utterance excerpts) как seed для retrieval. Полный текст доступен через `episode_payloads` DBI.

#### 5.5.2. Per-kind projections

- Original: concat всех utterances до max 4000 chars (для context).

#### 5.5.3. Storage

DBI `conversation_episode_payloads`. Включается через `enable_conversation_episode=true`. Требует `SpeakerAttribution` capability (см. validation rules в `memory-stacks-roadmap.md` секция 10).

#### 5.5.4. Compaction

`episode_compaction` job (см. `compaction-roadmap.md`, future) сливает N соседних episodes в один SuperEpisode при превышении context budget или по retention policy.

### 5.6. CompiledArticlePayload

Payload для `KnowledgeUnitKind::CompiledArticle`. Используется в CompiledWikiStack и FullResearch stack.

```cpp
struct CompiledArticlePayload {
    std::string title;
    std::string owner;
    std::vector<std::string> readers;
    std::vector<std::string> keywords;
    std::string status;                       // "draft", "published", "archived"
    uint64_t last_compiled_at_ms;
    std::vector<UnitId> derived_from;         // source units
    uint64_t injection_count = 0;
    uint64_t last_injected_at_ms = 0;
};
```

#### 5.6.1. Per-kind primary_text

`title` + first paragraph (до 500 chars).

#### 5.6.2. Per-kind projections

- Original: full markdown body.
- Summary: first 1000 chars + status.

#### 5.6.3. Storage

DBI `compiled_article_payloads`. Включается через `enable_compiled_article=true`. Требует `enable_compaction=true` (для `SummaryPromotionJob`).

#### 5.6.4. Lifecycle

`status` field управляет lifecycle transitions: `draft` → `published` → `archived`. `archived` соответствует `Deprecated` lifecycle state; retrieval фильтрует по умолчанию.

### 5.7. Reserved Kinds (Task, Decision)

```cpp
// TODO: определить структуру в M2.
// Сейчас зарезервированы для follow-up:
// - Task: handoff records (operational handoff structure)
// - Decision: ссылки на decision points в agent reasoning
```

Контракты будут определены, когда соответствующие use-cases материализуются (M2+).

### 5.8. Custom (escape hatch)

```cpp
// Custom unit: нет dedicated payload, всё через:
// - envelope.primary_text / display_text
// - metadata_typed["payload"] (typed JSON-like value)
// - components[] (любые operational components)
```

Используется для экспериментальных типов до добавления dedicated payload. Custom unit обязан нести `KnowledgeUnitId`, `KnowledgeUnitKind`, `SourceRef[]`, lifecycle fields и проходит стандартные validation rules. Custom payload хранится в `metadata_typed["payload"]` через typed value (variant).

## 6. Lifecycle FSM

Lifecycle FSM определён в `memory-stacks-roadmap.md` ADR-011 и расширен здесь с детальной семантикой `SoftSuppressed`.

### 6.1. States

```cpp
enum class LifecycleState : uint8_t {
    Active = 0,           // active, retrieval-eligible
    SoftSuppressed = 1,   // пониженный приоритет (anti-loop подсвинок)
    Superseded = 2,       // заменён другим unit (bi-temporal chain)
    Deprecated = 3,       // помечен на удаление (compaction candidate)
    Erased = 4,           // физически удалён (или logical erase)
};
```

### 6.2. Transitions

| From | To | Trigger | Action |
|---|---|---|---|
| (new) | Active | write | initial state |
| Active | SoftSuppressed | retrieval retrieves this unit + cooldown | priority умножается на self_echo_suppression (default 0.3) |
| SoftSuppressed | Active | cooldown_until_ms elapsed | priority восстанавливается |
| Active | Superseded | supersede operation (newer unit supersedes older) | старый unit помечается Superseded, новый становится Active |
| Superseded | Deprecated | compaction review | после N дней в Superseded |
| Active | Deprecated | manual deprecation | авторский сигнал через WriteRequest |
| Deprecated | Erased | compaction job | физическое удаление или logical remove |

### 6.3. Retrieval semantics

- **Active**: применяется full scoring (BM25F + decay + boost). Участвует в RRF fusion с полным весом.
- **SoftSuppressed**: применяется full scoring, но с пониженным приоритетом в fusion (RRF multiplier или scoring weight умножается на self_echo_suppression).
- **Superseded**: filtered out by default, доступен только через явный filter (bi-temporal query).
- **Deprecated**: filtered out, доступен только через audit dump.
- **Erased**: не возвращается ни в одном retrieval; id не reuse'ится.

### 6.4. Anti-loop подсвинок

Поведение `SoftSuppressed` используется в `AgentLongTermMemory` stack для предотвращения "только что использованный факт снова в retrieval":

- После retrieval unit получает `SoftSuppressed` на `cooldown_ms` (default 60s).
- `priority_weight` (или RRF contribution) умножается на `self_echo_suppression` (default 0.3).
- Используется в `DecayAwareRetriever` (см. `knowledge-base-roadmap.md` секция 7.4).
- Усиливается фильтром `AntiLoopCooldown` для строгого respect cooldown.

Подсвинок активируется при `SpeakerComponent.speaker_id == agent_self_id` (см. `memory-stacks-roadmap.md` ADR-008).

## 7. Manifest Integration

### 7.1. DerivedRecordKind

Enum расширяется новыми значениями для component-уровня записей:

```cpp
enum class DerivedRecordKind : uint32_t {
    // Existing kinds ...
    KnowledgeUnitEnvelope = 100,
    UsageStatsComponent = 101,
    SpeakerComponent = 102,
    TemporalComponent = 103,
    EmbeddingMetaComponent = 104,
    CompactionMetaComponent = 105,
    QAPayload = 110,
    FactPayload = 111,
    ChunkPayload = 112,
    ConversationEpisodePayload = 113,
    CompiledArticlePayload = 114,
    SearchProjection = 120,
};
```

Значения 100-120 зарезервированы для новых типов. Расширение additive; старые manifests остаются валидными.

### 7.2. Resource Manifest Records

При write unit в `ResourceManifest` добавляются:

- 1 запись для envelope (`DerivedRecordKind::KnowledgeUnitEnvelope`).
- N записей для payload-компонентов (если payload задан для kind).
- M записей для operational components (`UsageStatsComponent`, `SpeakerComponent` и т.д.).
- P записей для `SearchProjection`s (по одной на projection_kind).

Это позволяет отслеживать происхождение каждого слоя через manifest и обеспечивает auditability при supersedence/compaction.

## 8. Storage Pattern

### 8.1. Per-Component DBI Allocation

| Component / Payload | DBI имя | Open when |
|---|---|---|
| KnowledgeUnitEnvelope | `knowledge_units` | always |
| KnowledgeUnit by kind | `knowledge_units_by_kind` | always (DUPSORT) |
| UsageStatsComponent | `unit_components` (tag=UsageStats) | UsageStats=true |
| SpeakerComponent | `unit_components` (tag=Speaker) | SpeakerAttribution=true |
| TemporalComponent | `unit_components` (tag=Temporal) | TemporalValidity=true |
| EmbeddingMetaComponent | `unit_components` (tag=EmbeddingMeta) | EmbeddingMeta=true |
| CompactionMetaComponent | `unit_components` (tag=CompactionMeta) | Compaction=true |
| QAPayload | `qa_payloads` | QAPairs=true (enable_qa_payload) |
| FactPayload | `fact_payloads` | enable_fact_payload=true |
| ChunkPayload | `chunk_payloads` | Chunk kind (default, всегда) |
| ConversationEpisodePayload | `conversation_episode_payloads` | ConversationMemory=true |
| CompiledArticlePayload | `compiled_article_payloads` | CompiledArticles=true |
| SearchProjections | `unit_projections` | indexed retrieval (always для BasicRag+) |

Operational components живут в единой `unit_components` DBI через `TypeDiscriminatedTable` (из `mdbx-containers-extension-tz.md`). Per-kind payloads — отдельные таблицы для изоляции schema и быстрого scan по kind.

### 8.2. Capability-aware DBI Creation

При `MemoryStack::open(spec)`:

1. Всегда создаются core DBI: `knowledge_units`, `knowledge_units_by_kind`, `generation_index`, `schema_info`.
2. По capability флагам создаются дополнительные DBI (см. таблицу 8.1).
3. При drift detected (см. `memory-stacks-roadmap.md` секция 14): error или auto-migrate (per ADR-003, ADR-004).

DBI budget target ≤ 64 (расширение `max_dbs` 16→64 в `mdbx-containers`). Typical M1 stack: 18-22 DBI. Headroom есть.

### 8.3. Multi-Component Writes

При write unit с envelope + N components + M projections:

- Одна транзакция через `MultiTableWriter` (из `mdbx-containers-extension-tz.md`).
- Atomic commit: либо всё записано, либо ничего.
- Sequence: envelope → components → projections → secondary indexes → manifest records.

## 9. Migration Plan (от старого KnowledgeUnit к новой модели)

### 9.1. Breaking Refactor Strategy

В рамках v0.x — допустим breaking refactor (per ADR-004 в `memory-stacks-roadmap.md`):

1. Старый монолитный `KnowledgeUnit` struct удаляется.
2. Создаётся `KnowledgeUnitEnvelope` + Components.
3. Все references обновляются на envelope-centric API.

Если уже есть старые базы — отдельный migration script (`agent-memory-cli migrate-monolithic-ku`), не часть core API.

### 9.2. SourceRef Schema Migration

```cpp
// Старый SourceRef → новый SourceRef
struct OldSourceRef {
    std::string uri;
    std::string excerpt;
    uint64_t byte_offset;
    uint64_t byte_length;
    std::optional<std::string> quote_hash;
};

// Mapping:
// - uri → uri (kept)
// - excerpt + TextRange → excerpt + excerpt_text
// - quote_hash (optional) → quote_hash (required, вычисляется из excerpt если missing)
// - ResourceId добавляется через reverse lookup по uri
// - anchor_unit_id добавляется (default = nullopt)
// - observed_at_ms добавляется (default = nullopt)
```

Migration вычисляет missing `quote_hash` через SHA1(`excerpt`)[:16], добавляет `ResourceId` через reverse lookup по uri (resource registry mapping), сохраняет остальные поля.

### 9.3. Migration Steps

1. Сканировать все `knowledge_units`, прочитать envelope.
2. Для каждого unit:
   - Извлечь components из envelope (если были вшиты в старый monolithic struct).
   - Извлечь payload (если был).
   - Записать в новую структуру (envelope + components).
3. Создать projections по per-kind правилам (см. секцию 5).
4. Перестроить secondary indexes: `inverted_token_to_unit`, `field_to_postings`, `metadata_filters`.
5. Обновить `schema_info.profile_signature`.

Round-trip test обязателен: `basic_rag` → `agent_ltm` → `basic_rag` без потери данных и индексов.

## 10. Recommended Implementation Order

Шаги из `memory-stacks-roadmap.md` секция 16, конкретизированные для per-kind payloads и lifecycle FSM:

| Шаг | Что добавляется | Payload/component |
|---|---|---|
| 1 | KnowledgeUnitEnvelope + базовые DBI | envelope только |
| 5 | Component infrastructure | operational components (UsageStats, Speaker, Temporal, EmbeddingMeta, CompactionMeta) |
| 6 | Payload components per kind | QAPayload, FactPayload, ChunkPayload, ConversationEpisodePayload, CompiledArticlePayload |
| 9 | SoftSuppressed state в Lifecycle FSM | lifecycle extension с anti-loop подсвинком |
| 9.1 | Anti-loop подсвинок | self_echo_suppression в DecayAwareRetriever + AntiLoopCooldown фильтр |
| 12 | Round-trip тесты для default stacks | все payloads (write → close → reopen → verify) |

Дополнительные шаги (M2+): EventPayload (если потребуется), Task/Decision payloads.

## 11. References

Внутренние документы:

- `guides/memory-stacks-roadmap.md` — ADR-001 (envelope + components), ADR-003 (profile/scope), ADR-004 (KnowledgeUnit миграция), ADR-005 (search text), ADR-008 (decay/anti-loop), ADR-011 (lifecycle FSM). `MemoryProfileSpec`, `MemoryStack`, MDBX layout, maturity levels.
- `guides/knowledge-base-roadmap.md` — `KnowledgeUnitEnvelope` contract, retrieval flow, `IComponentStore`/`IProjectionStore`, `SearchProjection` generation rules, `DecayAwareRetriever`, evaluation pipeline.
- `guides/lexical-search-roadmap.md` — BM25F по projections, field-weighted indexing, postings structure.
- `guides/optimization-roadmap.md` — vector storage per `(model_id, projection_kind)`, binary signature indexes, scope-aware secondary indexes.
- `guides/mdbx-containers-extension-tz.md` — `TypeDiscriminatedTable` для `unit_components`, `MultiTableWriter` для atomic coordinated writes, `ReverseIndexTable` для `metadata_filters`.
- `guides/compaction-roadmap.md` (future) — `episode_compaction`, `SummaryPromotionJob`, handoff structure.
- `guides/runtime-services-roadmap.md` (future) — PromptCache, AsyncIndexer, WriteGate.
- `guides/policies-roadmap.md` (future) — детальная спецификация DecayPolicy/WritePolicy/SpeakerScopePolicy с диапазонами и defaults.
- `guides/architecture.md` — 4-слойная модель, maturity levels.

External references (ai-agent-playbook):

- `concepts/rag-knowledge/Внешняя память LLM-агентов — система СВИНОПАС.md` — anti-loop / cooldown паттерны, lifecycle states.
- `concepts/llm-research/Сравнение подходов организации памяти и контекста LLM.md` — taxonomy memory levels (hot/warm/cold), retrieval composition.
- `concepts/ai-agents/AI-агенты и AI-VTuber — архитектурные паттерны из видео 2026.md` — hot/cold path separation, layered memory.
- `resources/llm-research/Memory for Autonomous LLM Agents survey — конспект.md` — write-read-manage loop, lifecycle management.