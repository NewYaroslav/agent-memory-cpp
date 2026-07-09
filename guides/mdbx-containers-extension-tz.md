# Техническое задание: расширение header-only библиотеки `mdbx-containers`

## 0. Архитектурный контекст

Этот документ описывает storage primitives уровня Layer 1 (см. `guides/memory-stacks-roadmap.md`, секция 11) для новой компонентной архитектуры памяти: `Envelope + Components + SearchProjections`. Использование описанных ниже таблиц в DBI-схеме конкретных профилей — в `guides/memory-stacks-roadmap.md`, секция 12.

Ключевые компоненты архитектуры, для которых задаются таблицы:

- `KnowledgeUnitEnvelope` — lookup-critical hot path (DBI: `knowledge_units`).
- `Components` — operational + per-kind payloads, дискриминированы по `ComponentKind` (DBI: `unit_components` через `TypeDiscriminatedTable`).
- `SearchProjections` — retrieval-specific text views, multi-version по `revision` (DBI: `unit_projections`).
- Embeddings — multi-projection, multi-model (DBI: `embedding_meta`, `embedding_vectors`).
- Secondary indexes — scope-aware (`scope_id` как префикс ключа), см. ADR-012 в `guides/memory-stacks-roadmap.md`.

Применение storage primitives в MDBX env (бюджет 64 DBI, см. секцию 12.6 roadmap-а) согласуется с capability matrix из `guides/memory-stacks-roadmap.md`, секция 9.

## 1. Цели и non-goals

### Цели

Расширить `external/mdbx-containers/include/mdbx_containers/` набором классов и утилит, необходимых для следующих направлений `agent-memory-cpp`:

1. **Knowledge Units** — обобщение над `Chunk`, `QAPair`, `Fact`, `Event`, `Entity`, `Relation`, `Section`, `Summary`, `ConversationEpisode`, `Note`. Все типы живут в одной таблице с type tag.
2. **Fielded BM25F** — отдельные поля per source: `title`, `heading_path`, `body`, `code_blocks`, `tags`, `symbols`, `metadata_typed`, `qa_question`, `qa_answer`, `summary`. Поля должны быть доступны как inverted index с composite key `(token_id, field_id)`.
3. **Typed graph storage** — `NodeKind`/`EdgeKind` enum, хранение outgoing/incoming edges через reverse index, bounded expansion.
4. **QA Knowledge Base** — `QAPair` с полями `valid`, `last_verified`, `priority`, `freq`, `supersedes`. Inverted index по `question` tokens.
5. **Temporal index** — range queries по timestamp (`valid_from`, `valid_until`, `recorded_at`) с inclusive/exclusive bounds.
6. **Metadata filters** — поддержка `(metadata_key, metadata_value) -> ResourceId` reverse index для pre-filter.
7. **ContextBuilder** — multi-table write в одной транзакции (primary + secondary indexes атомарно).
8. **Retrieval composition** — multi-retriever + RRF + cross-encoder rerank slot.

### Non-goals

- **Backward compatibility** — не ломать публичный API существующих классов.
- **ANN / HNSW / approximate vector search** — не входит в scope. Существующий `vector::VectorStore` остаётся exact baseline.
- **Full-text search primitives** (phrase, proximity, fuzzy, n-gram) — реализуются на уровне agent-memory-cpp поверх BM25-индекса.
- **Schema versioning framework поверх mdbx-containers** — payload versioning остаётся на стороне приложения (текущие префиксы `agent_memory.document.v1` и т.п. сохраняются).
- **Background compaction API** — не в scope первой итерации.
- **Bloom filters** — не в scope.
- **Multi-process support** — не в scope; per-thread transaction модель сохраняется.

## 2. Принципы

1. **Header-only, C++11 baseline с C++17 guarded features.** Новые фичи используют те же guards, что и существующий код (см. `external/mdbx-containers/include/mdbx_containers/detail/utils.hpp:12-16`).
2. **ABI compatibility.** Существующие сигнатуры публичных методов не меняются. Новые методы добавляются без переупорядочивания vtable или изменения layout.
3. **Death-of-the-author** (`external/mdbx-containers/PHILOSOPHY.md`). Библиотека оценивается по техническим качествам; `include/` остаётся первичным source of truth.
4. **Per-thread transaction model.** Каждый поток владеет не более чем одной активной транзакцией (см. `common/Connection.hpp`). Multi-table write обеспечивается одним `Transaction` объектом, разделяемым между таблицами, а не координацией отдельных транзакций.
5. **DRY через композицию.** Не дублировать логику existing классов. Новые классы строятся поверх `KeyValueTable`, `KeyMultiValueTable`, `AnyValueTable`.
6. **Naming conventions.** PascalCase для классов и публичных методов, `m_` prefix для private полей, snake_case для free functions в `detail::` namespace.
7. **Include guards** — `MDBX_CONTAINERS_HEADER_<PATH>_<FILE>_<EXT>_INCLUDED` (см. `external/mdbx-containers/AGENTS.md`).
8. **Doxygen комментарии** на английском для всех публичных API.

## 3. Новые классы

### 3.1 Расширения `KeyValueTable` (без нового файла)

Добавить в `external/mdbx-containers/include/mdbx_containers/KeyValueTable.hpp`:

```cpp
// Batch update
template <typename Fn>
std::size_t update_many(const std::vector<std::pair<K, Fn>>& updates,
                        const Transaction& txn);

// Merge from another container (same K, V types)
void merge(const KeyValueTable& other, const Transaction& txn);

// Consistent snapshot
template <typename ContainerT = std::vector<std::pair<K, V>>>
ContainerT snapshot(const Transaction& txn) const;

// Force reindex (drop and rebuild from caller-provided source)
template <typename Source>
void reindex(Source&& source, const Transaction& txn,
             std::function<void(std::size_t, std::size_t)> progress = nullptr);
```

Используют MDBX-флаги: те же, что у `KeyValueTable`. Thread-safety: per-thread transaction. Backward compat: только добавление методов. Пример:

```cpp
tbl.update_many({{"k1", [](V& v){ v.x = 1; }}, {"k2", [](V& v){ v.y = 2; }}}, txn);
```

### 3.2 `ReverseIndexTable<InvertedKey, PrimaryKey>`

**Файл:** `external/mdbx-containers/include/mdbx_containers/ReverseIndexTable.hpp`.

**Назначение:** secondary index pattern с consistency через composable transaction.

```cpp
class ReverseIndexTable<InvertedKey, PrimaryKey> {
public:
    explicit ReverseIndexTable(std::shared_ptr<Connection> conn,
                               const std::string& name = "reverse_index",
                               const TableOptions& opts = {});

    void add(const InvertedKey& inv, const PrimaryKey& primary, const Transaction& txn);
    void remove(const InvertedKey& inv, const PrimaryKey& primary, const Transaction& txn);
    std::vector<PrimaryKey> find(const InvertedKey& inv, const Transaction& txn) const;
    std::optional<PrimaryKey> find_one(const InvertedKey& inv, const Transaction& txn) const;
    std::size_t count(const InvertedKey& inv, const Transaction& txn) const;

    std::vector<std::pair<InvertedKey, std::vector<PrimaryKey>>>
    find_by_prefix(const InvertedKey& prefix, const Transaction& txn) const;

    std::size_t remove_all_for(const PrimaryKey& primary, const Transaction& txn);

    std::vector<std::pair<InvertedKey, std::vector<PrimaryKey>>>
    range(const InvertedKey& from, const InvertedKey& to, const Transaction& txn) const;
};
```

Внутри: `KeyMultiValueTable<InvertedKey, PrimaryKey>` с флагом `MDBX_DUPSORT`. Удаление последнего reference для `InvertedKey` стирает ключ целиком.

MDBX flags: `MDBX_DUPSORT`. Thread-safety: per-thread transaction. Backward compat: новый класс.

Use cases: `token -> chunk_id` (lexical), `(token, field) -> posting` (BM25F), `(metadata_key, metadata_value) -> ResourceId` (pre-filter), `(src_node, edge_kind) -> EdgePayload` (graph outgoing), `(dst_node) -> src_node` (graph incoming reverse), `timestamp -> EventPayload` (temporal).

Scope-aware варианты (ADR-012 в `guides/memory-stacks-roadmap.md`): `InvertedKey` расширяется префиксом `ScopeId`, чтобы secondary indexes были multi-tenant. Примеры из DBI-схемы roadmap-а:

- `(scope_id, speaker_id) -> UnitId` — `speaker_to_units` (capability `SpeakerAttribution`).
- `(scope_id, session_id) -> UnitId` — `session_to_units` (capability `SpeakerAttribution`).
- `(scope_id, metadata_key, metadata_value) -> UnitId` — `metadata_filters` (всегда открыт, lightweight pre-filter).
- `(scope_id, projection_kind, field_id, token_id, unit_id) -> PostingStats` — `field_to_postings` (BM25F по projections).

Реализация: `InvertedKey = CompositeKey<ScopeId, ...>` либо `ScopeId`-prefixed key struct с `to_bytes()`/`from_bytes()`. Класс `ReverseIndexTable` остаётся generic; scope-awareness обеспечивается композицией ключей на уровне вызывающего кода.

```cpp
ReverseIndexTable<std::string, ChunkId> rev(conn, "token_to_chunk");
rev.add("algorithm", chunk_a, txn);
auto chunks = rev.find("algorithm", txn);
```

### 3.3 `RangeIndexTable<SortableKey, Payload>`

**Файл:** `external/mdbx-containers/include/mdbx_containers/RangeIndexTable.hpp`.

**Назначение:** efficient range queries с inclusive/exclusive bounds и cursor-based pagination.

```cpp
class RangeIndexTable<SortableKey, Payload> {
public:
    void add(const SortableKey& key, const Payload& payload, const Transaction& txn);
    std::optional<Payload> find(const SortableKey& key, const Transaction& txn) const;

    std::vector<std::pair<SortableKey, Payload>>
    range(const SortableKey& from, const SortableKey& to,
          bool exclusive_left = false, bool exclusive_right = false,
          const Transaction& txn) const;

    std::vector<std::pair<SortableKey, Payload>>
    range_paginated(const SortableKey& from, const SortableKey& to,
                    std::size_t offset, std::size_t limit,
                    const Transaction& txn) const;

    std::size_t range_count(const SortableKey& from, const SortableKey& to,
                            const Transaction& txn) const;

    std::optional<std::pair<SortableKey, Payload>>
    lower_bound(const SortableKey& key, const Transaction& txn) const;

    std::optional<std::pair<SortableKey, Payload>>
    upper_bound(const SortableKey& key, const Transaction& txn) const;

    std::optional<std::pair<SortableKey, Payload>> first(const Transaction& txn) const;
    std::optional<std::pair<SortableKey, Payload>> last(const Transaction& txn) const;
};
```

Внутри: `KeyValueTable<SortableKey, Payload>` (single payload per key) или `KeyMultiValueTable` (multi-payload). `SortableKey` ограничен `uint32_t`/`uint64_t`/trivially copyable struct с `to_bytes`/`from_bytes`.

Use cases: `temporal_event_index` (timestamp range), `binary_bucket_index` (short_key range + neighbor expansion), `lexical_postings` (token_id range).

### 3.4 `TypeDiscriminatedTable<EnumTag, Key, ValueVariant>`

**Файл:** `external/mdbx-containers/include/mdbx_containers/TypeDiscriminatedTable.hpp`.

**Назначение:** одна таблица для разных value types с runtime type tag.

```cpp
template <typename EnumTag, typename Key, typename... ValueTypes>
class TypeDiscriminatedTable {
public:
    template <typename T>
    void add(EnumTag tag, const Key& key, const T& value, const Transaction& txn);

    template <typename T>
    std::optional<T> find(EnumTag tag, const Key& key, const Transaction& txn) const;

    template <typename T>
    bool update(EnumTag tag, const Key& key,
                std::function<void(T&)> mutator,
                bool create_if_missing, const Transaction& txn);

    bool remove(EnumTag tag, const Key& key, const Transaction& txn);
    std::vector<Key> list_keys_by_tag(EnumTag tag, const Transaction& txn) const;
    std::size_t count_by_tag(EnumTag tag, const Transaction& txn) const;

    void set_type_tag_check(bool enabled);
};
```

Внутри: `AnyValueTable<Key>` (см. `external/mdbx-containers/include/mdbx_containers/AnyValueTable.hpp`) с type-tag prefix. Опциональная проверка через `set_type_tag_check(true)`.

Use cases:

- `knowledge_units` (Chunk, QAPair, Fact, Event, Entity, Relation, Section, Summary, Episode).
- `unit_components` (Layer B, см. ADR-001 в `guides/memory-stacks-roadmap.md`): `ComponentKind` tag ∈ {`UsageStats`, `Speaker`, `Temporal`, `EmbeddingMeta`, `CompactionMeta`}, ключ — `UnitId`, value — `ValueVariant<UsageStatsComponent, SpeakerComponent, TemporalComponent, EmbeddingMetaComponent, CompactionMetaComponent>`. Operational компоненты, читаемые лениво из retrieval layer; tag-prefix позволяет держать все компоненты в одной DBI с минимальным I/O envelope-а.
- per-kind payload-компоненты (`QAPayload`, `FactPayload`, `ConversationEpisodePayload`, `CompiledArticlePayload`, `ChunkPayload`) — при необходимости выносятся в отдельные DBI; см. `guides/memory-stacks-roadmap.md`, секция 12.2.

### 3.5 `CompositeKey<P1, P2>` и хелперы

**Файл:** `external/mdbx-containers/include/mdbx_containers/CompositeKey.hpp`.

**Назначение:** typed composite key без `std::pair<K1, K2>` everywhere.

```cpp
template <typename P1, typename P2>
struct CompositeKey {
    P1 part1;
    P2 part2;
    // operators: ==, !=, <, hash (если C++17 std::hash доступен)
    // to_bytes() / from_bytes() требуются от P1 и P2
};

template <typename P1, typename P2>
CompositeKey<P1, P2> make_composite_key(const P1& p1, const P2& p2);

// В detail/utils.hpp:
template <typename P1, typename P2>
std::array<std::uint8_t, sizeof(P1) + sizeof(P2)>
composite_key_to_bytes(const P1& p1, const P2& p2);  // trivially copyable only

template <typename P1, typename P2>
std::tuple<P1, P2> composite_key_from_bytes(const std::uint8_t* data, std::size_t size);
```

Требование: `P1`, `P2` — trivially copyable с `to_bytes()`/`from_bytes()`. Use cases: `(token_id, field_id)` для BM25F, `(src_node, edge_kind)` для graph outgoing.

### 3.6 Расширения `paginated_range`

Добавить в `KeyValueTable`/`KeyMultiValueTable`:

```cpp
template <typename Callback>
void paginated_range(const K& from, const K& to, std::size_t page_size,
                     Callback&& cb, const Transaction& txn) const;

struct PaginationStats { std::size_t pages_returned; std::size_t items_per_page; };
PaginationStats paginated_range_stats(const K& from, const K& to,
                                      std::size_t page_size, const Transaction& txn) const;
```

Существующий `range_reverse(from, to, [limit,])` остаётся; добавляется forward direction.

### 3.7 `MultiTableWriter` (RAII helper)

**Файл:** `external/mdbx-containers/include/mdbx_containers/MultiTableWriter.hpp`.

**Назначение:** гарантировать атомарность multi-table write через одну `Transaction`.

```cpp
class MultiTableWriter {
public:
    explicit MultiTableWriter(Connection& conn);
    ~MultiTableWriter();  // rollback если не commit

    Transaction& txn();  // вернуть текущую транзакцию

    template <typename Op>
    void write(Op&& op);  // op получает Transaction&

    void commit();   // throws MdbxException on conflict
    void rollback();
};

// Connection extension:
template <typename Op>
void Connection::multi_write(Op&& op);  // RAII: commit/rollback
```

Документировать best practice: обновление primary + secondary indexes в одной транзакции (например, добавление chunk в `agent_memory_chunks` + соответствующих postings в `lexical_postings`).

Ключевой use case под Layer 1 архитектуру из `guides/memory-stacks-roadmap.md` (Layer B + C, ADR-001): `MemoryStack::write_unit` обязан записать атомарно через один `MultiTableWriter`:

1. Envelope в `knowledge_units` (DBI: `KeyValueTable<UnitId, KnowledgeUnitEnvelope>`).
2. Tag-prefixed компоненты в `unit_components` (DBI: `TypeDiscriminatedTable<ComponentKind, UnitId, ValueVariant<UsageStats | Speaker | Temporal | EmbeddingMeta | CompactionMeta>>`).
3. Projections в `unit_projections` (DBI: `KeyValueTable<(scope_id, UnitId, ProjectionKind, revision), SearchProjection>`).
4. Embedding metadata + vectors (`embedding_meta`, `embedding_vectors`).
5. Все scope-aware secondary indexes, относящиеся к данной write: `inverted_token_to_unit` (по каждой projection), `field_to_postings`, `temporal_event_index`/`temporal_unit_index`, `speaker_to_units`/`session_to_units`, `usage_stats_index`, `graph_edges_by_src`/`graph_edges_by_dst`, `metadata_filters`.
6. `generation_index` для stale-filter, `compaction_jobs` enqueue (если требуется фоновая работа).

При исключении в любом шаге `MultiTableWriter` откатывает все 6 групп записей; частично записанный unit не наблюдаем. `commit()` бросает `MdbxException` на conflict; retry policy остаётся на стороне `MemoryStack::write_unit` (см. `WritePolicy` в `guides/memory-stacks-roadmap.md`, секция 6.2).

### 3.8 Расширения `Connection` и `Config`

`common/Config.hpp`:

```cpp
bool enable_metrics = false;  // optional metrics integration
std::function<void(const std::string& dbi_name)> table_creation_callback = nullptr;
```

`max_dupsort_value_size` уже есть; возможно, увеличить default с 512 до 1024 для больших payloads (обосновать benchmark-ом).

### 3.9 Утилиты в `detail/utils.hpp`

Добавить в `external/mdbx-containers/include/mdbx_containers/detail/utils.hpp`:

```cpp
inline std::uint64_t sortable_key_from_uint64(std::uint64_t v);   // identity
inline std::uint64_t sortable_key_from_int64(std::int64_t v);    // sign-bit flip + offset

inline int hamming_distance_uint64(std::uint64_t a, std::uint64_t b);  // popcount XOR
inline std::vector<std::uint64_t> hamming_neighbors_uint64(
    std::uint64_t key, int radius);  // для binary bucket expansion

inline std::string to_hex_string(const std::uint8_t* data, std::size_t size);
inline std::vector<std::uint8_t> from_hex_string(const std::string& hex);
```

## 4. Расширения существующих классов

Для каждого класса добавить методы (без поломки ABI):

**`KeyValueTable<K, V, Options>`:** `update_many`, `merge`, `snapshot`, `reindex`, `try_update(key, Fn, default_value, txn)`, `add_many(container, txn)`, `erase_many(keys, txn) → size_t`, `bulk_erase_if(pred, txn) → size_t`, `diagnostics() → {size, key_size_avg, value_size_avg, key_size_p95, value_size_p95, fragmentation_pct}`.

**`KeyMultiValueTable<K, V, Options>`:** те же batch методы плюс `deduplicate(txn) → size_t`, `count_unique_keys(txn)`.

**`KeyTable<K, Options>`:** `insert_many(container, txn) → vector<bool>`, `erase_many(keys, txn) → size_t`.

**`ValueTable<V>`:** `set_many(container, txn)`, `merge_from(other, txn)`.

**`SequenceTable<V>`:** `reserve(begin, end) → vector<id>`, `append_if_absent(value) → optional<id>`.

**`AnyValueTable<K, Options>`:** `bulk_set_of_type<T>(container, txn)`, `list_keys_by_type<T>(txn)`.

Все additions только аддитивные. Сигнатуры существующих методов не меняются.

## 5. MDBX-таблицы, которые появятся в `agent-memory-cpp`

### 5.1 Lexical (по `guides/lexical-search-roadmap.md`)

```text
lexical_token_by_text   KeyValueTable<string, uint64_t>
lexical_token_by_id     KeyValueTable<uint64_t, string>
lexical_postings        KeyMultiValueTable<uint64_t, LexicalPosting>
lexical_chunk_stats     KeyValueTable<ChunkId, ChunkLexicalStats>
lexical_token_stats     KeyValueTable<TokenId, LexicalTokenStats>
lexical_field_postings  ReverseIndexTable<CompositeKey<TokenId, uint8_t>, LexicalPosting>
```

### 5.2 Optimization (по `guides/optimization-roadmap.md`)

```text
binary_bucket_index     RangeIndexTable<uint64_t_short_key, BucketPostingBlob>
embedding_store         KeyValueTable<ChunkId, vector<float>>
chunk_store             KeyValueTable<ChunkId, CompressedBlob>
```

### 5.3 Knowledge base (новые)

```text
knowledge_units             TypeDiscriminatedTable<KnowledgeUnitKind, KnowledgeUnitId, ...>
qa_knowledge                KeyValueTable<QAPairId, QAPairPayload>
qa_inverted                 ReverseIndexTable<token, QAPairId>
fact_store                  KeyValueTable<FactId, FactPayload>
fact_inverted               ReverseIndexTable<token, FactId>
event_store                 KeyValueTable<EventId, EventPayload>
temporal_event_index        RangeIndexTable<uint64_timestamp, EventPayload>
graph_nodes                 KeyValueTable<NodeId, NodePayload>
graph_edges_by_src          ReverseIndexTable<CompositeKey<src_node, edge_kind>, EdgePayload>
graph_edges_by_dst          ReverseIndexTable<dst_node, src_node>
resource_metadata_filters   ReverseIndexTable<CompositeKey<metadata_key, metadata_value>, ResourceId>
resource_kinds              TypeDiscriminatedTable<DerivedRecordKind, ResourceId, payload>
```

### 5.4 Infrastructure (existing, остаются)

```text
agent_memory_documents           KeyValueTable<DocumentId, string>
agent_memory_chunks              KeyValueTable<ChunkId, string>
agent_memory_document_chunks     KeyValueTable<DocumentId, string>
agent_memory_resource_manifests  KeyValueTable<ResourceId, string>
```

### 5.5 Memory-stack layer (по `guides/memory-stacks-roadmap.md`, секция 12)

DBI для компонентной архитектуры `Envelope + Components + Projections`. Открываются по capability (см. capability matrix, `guides/memory-stacks-roadmap.md`, секция 9).

```text
// Layer A — envelope (hot path), всегда открыт
knowledge_units                       KeyValueTable<UnitId, KnowledgeUnitEnvelope>
knowledge_units_by_kind               KeyMultiValueTable<KnowledgeUnitKind, UnitId>     // DUPSORT

// Layer B — components (operational + per-kind)
unit_components                       TypeDiscriminatedTable<ComponentKind, UnitId,
                                                            ValueVariant<UsageStats | Speaker | Temporal
                                                                       | EmbeddingMeta | CompactionMeta>>
qa_payloads                           KeyValueTable<UnitId, QAPayload>                   // capability QAPairs
fact_payloads                         KeyValueTable<UnitId, FactPayload>                 // capability TemporalFact
conversation_episode_payloads         KeyValueTable<UnitId, ConversationEpisodePayload> // capability ConversationMemory
compiled_article_payloads             KeyValueTable<UnitId, CompiledArticlePayload>     // capability CompiledArticles
chunk_payloads                        KeyValueTable<UnitId, ChunkPayload>                // для kind == Chunk

// Layer C — search projections (multi-version)
unit_projections                      KeyValueTable<CompositeKey<ScopeId, UnitId, ProjectionKind, uint64_revision>,
                                                            SearchProjection>
                                      // key содержит revision для хранения истории; активная projection
                                      // определяется максимальным revision для (scope, unit, kind).

// Embeddings — multi-projection, multi-model (ADR-007)
embedding_meta                        KeyValueTable<CompositeKey<ScopeId, UnitId, ProjectionKind, ModelId, Version>,
                                                            EmbeddingMeta>
embedding_vectors                     KeyValueTable<CompositeKey<ScopeId, ModelId, ProjectionKind, UnitId>,
                                                            vector_blob>
                                      // порядок ключа выбран для cluster-friendly access при ANN-free exact scan.

// Secondary indexes — scope-aware (ADR-012)
inverted_token_to_unit                ReverseIndexTable<CompositeKey<ScopeId, TokenId, ProjectionKind, FieldId>, UnitId>
field_to_postings                     ReverseIndexTable<CompositeKey<ScopeId, ProjectionKind, FieldId, TokenId>, PostingStats>
metadata_filters                      ReverseIndexTable<CompositeKey<ScopeId, MetadataKey, MetadataValue>, UnitId>
graph_edges_by_src                    ReverseIndexTable<CompositeKey<ScopeId, FromUnitId, EdgeKind>, EdgePayload>
graph_edges_by_dst                    ReverseIndexTable<CompositeKey<ScopeId, ToUnitId, EdgeKind>, EdgePayload>
temporal_event_index                  RangeIndexTable<uint64_timestamp, EventPayload>     // valid_from/valid_until
temporal_unit_index                   RangeIndexTable<uint64_timestamp, UnitId>          // observed_at
speaker_to_units                      ReverseIndexTable<CompositeKey<ScopeId, SpeakerId>, UnitId>
session_to_units                      ReverseIndexTable<CompositeKey<ScopeId, SessionId>, UnitId>
usage_stats_index                     ReverseIndexTable<CompositeKey<ScopeId, UnitId>, UsageStatsComponent>

// Schema metadata
schema_info                           KeyValueTable<string, SchemaInfo>                  // envelope_version, component_versions[], profile_signature
```

Замечания:

- `unit_projections` использует multi-version ключ `(scope_id, UnitId, ProjectionKind, revision)`. При write активной projection инкрементируется `revision`; старые revisions остаются до compaction purge (см. `guides/memory-stacks-roadmap.md`, open issue 17.4).
- `embedding_meta` хранит версионированную мета-информацию (model_id + version), чтобы CompactionWorker мог удалять versions старше N дней при отсутствии ссылок (см. roadmap, open issue 17.3).
- `embedding_vectors` упорядочен по `(scope_id, model_id, ProjectionKind, UnitId)` для cluster-friendly чтения при exact scan; для ANN-расширений порядок может быть пересмотрен в `guides/optimization-roadmap.md`.
- Все secondary indexes начинаются с `ScopeId` (ADR-012); profile validation в `MemoryStack::open()` проверяет обязательность `scope_id` для каждого write (см. roadmap, секция 10).

Все таблицы используют префикс payload versioning (`agent_memory.knowledge_unit.v1`, `agent_memory.qa.v1`, и т.п.), `Config::max_dbs` увеличивается с 16 до 64.

## 6. Порядок реализации (приоритеты)

**P0 (блокер для knowledge units):**
- `MultiTableWriter` + `Connection::multi_write`
- `ReverseIndexTable`
- `TypeDiscriminatedTable`
- `composite_key_to_bytes/from_bytes`

**P1 (блокер для BM25F + temporal):**
- `RangeIndexTable` с exclusive bounds и pagination
- `CompositeKey<P1, P2>` struct + trait
- Расширения `KeyValueTable`: `update_many`, `add_many`, `erase_many`

**P2 (расширения):**
- `paginated_range` для существующих классов
- `diagnostics()` для всех таблиц
- Расширения `Config`: `enable_metrics`, `table_creation_callback`
- Утилиты: `hamming_neighbors_uint64`, `to_hex_string`, `from_hex_string`

**P3 (последующие итерации):**
- `KeyValueTable::merge`, `snapshot`, `reindex`
- `SequenceTable::reserve`, `append_if_absent`
- `AnyValueTable::bulk_set_of_type<T>`

## 7. Backward compatibility

- **ABI:** новые методы добавляются как overloads или шаблоны. Template instantiations расширяются, но layout существующих классов не меняется. `MDBX_DUPSORT` flag применяется только в новых классах.
- **Source compat:** старый код с `KeyValueTable<K, V>` компилируется без правок.
- **C++11 baseline:** все новые C++17 фичи (`std::byte`, `std::optional`, structured bindings, `if constexpr`) guarded через `MDBXC_HAS_CPP17` или `__cplusplus >= 201703L`. `_compat` псевдонимы для C++11 fallback (`optional_compat` → `std::optional` на C++17, boost::optional на C++11).
- **Include guards:** новые файлы используют `MDBX_CONTAINERS_HEADER_<PATH>_<FILE>_HPP_INCLUDED`.
- **Doxygen:** все новые публичные API документированы на английском (см. `external/mdbx-containers/guides/coding-style.md`).

## 8. Тестирование

### 8.1 Контрактные тесты в `external/mdbx-containers/tests/`

Новые файлы:
- `reverse_index_table_test.cpp` — add/find/remove, prefix scan, range, remove_all_for.
- `range_index_table_test.cpp` — inclusive/exclusive bounds, pagination, lower/upper bound, first/last, empty ranges.
- `type_discriminated_table_test.cpp` — type discrimination correctness, optional validation, list_keys_by_tag.
- `composite_key_test.cpp` — round-trip, byte layout, ordering.
- `multi_table_writer_test.cpp` — atomicity (rollback на exception), commit/rollback контракты.
- `utils_extensions_test.cpp` — hamming_distance, hamming_neighbors, hex encoding, sortable_key_from_int64.

Каждый тест верифицирует:
1. Range boundaries (inclusive/exclusive оба).
2. DUPSORT semantics (identical keys + identical values).
3. Transaction isolation (read-only snapshot во время write).
4. Multi-table write atomicity (rollback отменяет все таблицы).
5. Empty ranges, single-element ranges, ranges spanning много DBI pages.

### 8.2 Integration тесты в `agent-memory-cpp/tests/`

- `MdbxKnowledgeUnitStore` использует `TypeDiscriminatedTable` для всех 9 типов.
- `MdbxQAKnowledgeBase` использует `KeyValueTable<QAPairId, QAPairPayload>` + `ReverseIndexTable` для inverted index.
- `MdbxGraphStore` использует два `ReverseIndexTable` для outgoing/incoming.
- `MdbxTemporalIndex` использует `RangeIndexTable<uint64_timestamp, ...>` для range queries.

Все интеграционные тесты должны проходить в C++11 и C++17 режимах.

## 9. Документация и примеры

### 9.1 Обновления в `external/mdbx-containers/README.md` и `README-RU.md`

Добавить секцию "New table types" с описанием каждого нового класса, мотивацией, code snippet.

### 9.2 Новые примеры в `external/mdbx-containers/examples/`

- `reverse_index_demo.cpp` — secondary index pattern для inverted token → chunk_id.
- `range_index_demo.cpp` — temporal range query + binary bucket neighbor expansion.
- `type_discriminated_demo.cpp` — knowledge unit с Chunk + QAPair + Fact в одной таблице.
- `multi_table_transaction_demo.cpp` — atomic write primary + secondary.
- `composite_key_demo.cpp` — BM25F field posting с `(token_id, field_id)`.

### 9.3 Обновления в `guides/`

Добавить cross-reference из `lexical-search-roadmap.md` и `optimization-roadmap.md` к новым mdbx-containers классам. Новый `knowledge-units-roadmap.md` описывает контракты `KnowledgeUnitKind` и то, как `TypeDiscriminatedTable` используется.

## 10. Метрики успеха

1. **ABI compatibility:** все существующие тесты `external/mdbx-containers/tests/` (15 файлов) проходят без изменений после расширения.
2. **Performance:** range queries через `RangeIndexTable` показывают O(log N) seek + O(K) page reads на бенчмарке 100K keys. `ReverseIndexTable::find` показывает O(log N) DUPSORT lookup.
3. **Adoption:** новые классы используются как storage backend для `MdbxKnowledgeUnitStore`, `MdbxQAKnowledgeBase`, `MdbxGraphStore`, `MdbxTemporalIndex`, `MdbxBinaryBucketIndex`.
4. **Documentation:** каждый новый публичный метод имеет Doxygen comment + usage example.
5. **Cross-version:** новый код компилируется и работает в C++11 и C++17 режимах (verified двумя CMake конфигурациями, см. `external/mdbx-containers/build-mingw-11-tests.bat` и `build-mingw-17-tests.bat`).

## 11. Открытые вопросы и риски

1. **ABI break risk для trivially copyable composite key.** Если `P1` или `P2` содержит padding, byte layout может быть нестабильным
 между компиляторами. Решение: документировать требование `static_assert(std::is_trivially_copyable<P1>::value)`, использовать `__attribute__((packed))` или фиксированный `to_bytes`/`from_bytes` контракт.

2. **DUPSORT performance на больших posting lists (>10K entries per token).** MDBX DUPSORT оптимизирован для sorted duplicates, но insertion sort O(N) при unordered insert. Решение: enforced sort order при insert, или переход на segmented postings (см. `guides/lexical-search-roadmap.md` секция "Posting Segments").

3. **Per-thread transaction модель ограничивает параллельный write.** Multi-table write через `MultiTableWriter` сериализует все writes в одном потоке. Для multi-writer сценариев потребуется connection-per-thread pool; это за рамками TZ.

4. **C++11 vs C++17 feature guards.** `std::optional`, `std::byte`, structured bindings, `if constexpr` — все guarded. Boost polyfill нежелателен (dependency-free-first). Fallback: собственные минимальные replacements в `detail/` namespace.

5. **Schema versioning остаётся на стороне приложения.** `TypeDiscriminatedTable` не навязывает payload version. Контракт: payload prefix `agent_memory.knowledge_unit.v1` etc. валидируется на уровне `agent-memory-cpp`, не в mdbx-containers.

6. **`max_dbs` увеличение.** Текущее значение 16 недостаточно для всех таблиц секции 5. План: 64 (16 существующих + 48 новых). Verify, что MDBX env flags поддерживают это без `MDBX_NOTLS` reconfiguration.

## 12. Дополнительные требования к API (из обзора существующих roadmap-документов upstream)

Данный раздел фиксирует самостоятельный набор требований к публичному API `mdbx-containers`, выявленных при обзоре существующих roadmap-документов `external/mdbx-containers/` и смежных направлений `agent-memory-cpp`. Требования сформулированы как независимые и не привязаны к каким-либо внешним системам учёта.

### 12.1 HybridSearch helper (RRF)

Требование: предоставить free function для Reciprocal Rank Fusion (RRF) как часть публичного API `mdbx-containers`. Helper должен жить в отдельном header `include/mdbx_containers/HybridSearch.hpp` и не зависеть от внутренних деталей конкретных контейнеров.

Сигнатура (ориентировочная):

```cpp
namespace mdbx_containers {

/// @brief Identifier with an associated retrieval score.
struct ScoredId {
    std::uint64_t id;
    double score;
};

/// @brief Reciprocal Rank Fusion of multiple ranked result lists.
///
/// Merges the input lists, deduplicating by @c id and summing
/// reciprocal-rank contributions across lists. Output is sorted
/// in descending order of the fused score; ties are broken
/// deterministically by ascending @c id.
///
/// @param lists       Ranked result lists, each sorted by descending
///                    relevance in the originating retriever.
/// @param top_k       Maximum number of returned items.
/// @param k_constant  RRF smoothing constant (default 60).
/// @return Top-@c top_k ScoredId values, fused and deduplicated.
std::vector<ScoredId> rrf_fuse(
    const std::vector<std::vector<ScoredId>>& lists,
    std::size_t top_k,
    int k_constant = 60);

} // namespace mdbx_containers
```

Контрактные требования:

- **Детерминизм.** При равном fused score порядок сортировки должен быть стабильным и определяться сравнением `id` (по возрастанию). Это критично для воспроизводимости retrieval traces и eval-датасетов.
- **Merge дублей по `id`.** Если один и тот же `id` встречается в нескольких списках, его fused score вычисляется как сумма вкладов RRF: `sum_i 1.0 / (k_constant + rank_i(id))`. Значение `score` из входных `ScoredId` используется только как tie-breaker при прочих равных и **не** входит в формулу RRF напрямую.
- **Top-k обрезка.** Возвращается не более `top_k` элементов; при `top_k == 0` возвращается пустой вектор.
- **Header-only.** Реализация размещается в `include/mdbx_containers/HybridSearch.hpp` и не требует линковки с дополнительными translation units.
- **C++11 baseline.** Использовать `MDBXC_HAS_CPP17` guard для `std::optional` в расширенных вариантах API (если такие появятся).

Use case: `agent-memory-cpp::HybridRetrievalEngine` будет вызывать `rrf_fuse` для слияния результатов BM25-retriever, vector-retriever и (опционально) graph-retriever, заменяя локальную RRF-имплементацию в `agent-memory-cpp`.

### 12.2 Soft-link expiration в GraphStore

Требование: расширить `GraphStore` (или эквивалентный domain-модуль, описанный в upstream roadmap) поддержкой soft-edges с временем жизни. Edge-структура получает дополнительное опциональное поле `expires_at_ms`.

```cpp
namespace mdbx_containers {

struct Edge {
    NodeId from;
    NodeId to;
    EdgeKind kind;
    EdgePayload payload;
    std::int64_t created_at_ms;
    std::optional<std::int64_t> expires_at_ms; ///< nullopt или 0 — нет expiration
};

class GraphStore {
public:
    /// @brief True, если edge истёк на момент @c now_ms.
    bool is_expired(const Edge& edge, std::int64_t now_ms) const noexcept;

    /// @brief Traversal должен пропускать edges, для которых
    ///        is_expired(edge, traversal_now_ms) == true.
    std::vector<Edge> out_edges(NodeId node, std::int64_t now_ms,
                                const Transaction& txn) const;
    std::vector<Edge> in_edges(NodeId node, std::int64_t now_ms,
                               const Transaction& txn) const;

    /// @brief Удалить все истёкшие edges, возвращает количество удалённых.
    std::size_t purge_expired(std::int64_t now_ms, const Transaction& txn);
};

} // namespace mdbx_containers
```

Контрактные требования:

- **Семантика expiration.** `expires_at_ms == nullopt` или `expires_at_ms == 0` трактуются как «нет expiration» (edge живёт вечно). Иначе `is_expired(edge, now_ms) == (now_ms >= edge.expires_at_ms)`.
- **Traversal фильтрация.** Все методы обхода графа, принимающие `now_ms`, обязаны пропускать expired edges на уровне storage. Существующие API без `now_ms` сохраняют старое поведение (без фильтрации).
- **Атомарность `purge_expired`.** Удаление выполняется в одной транзакции, идемпотентно.
- **Payload-совместимость.** Расширение `Edge` — только аддитивное. Старые сериализованные payloads без поля `expires_at_ms` десериализуются как `nullopt`.

Use case: knowledge graph с soft-edges вида `related_to`, временный `supersedes`, `co_mentioned_until` — для memory-агентов, где временные связи между сущностями автоматически истекают.

### 12.3 Event `observed_at_ms` и `source_id`

Требование: Event-структура обязана различать время, когда событие произошло в реальности, и время, когда система о нём узнала. Это два независимых timestamp-поля, плюс явная ссылка на источник.

```cpp
namespace mdbx_containers {

struct Event {
    EventId id;
    EventPayload payload;

    /// @brief When the event occurred in reality. 0 == unknown.
    std::int64_t occurred_at_ms;

    /// @brief When the system learned about the event
    ///        (typically == indexing time).
    std::int64_t observed_at_ms;

    /// @brief Reference to the source that reported the event.
    std::optional<SourceId> source_id;
};

class EventStore {
public:
    /// @brief Last N events ordered by @c observed_at_ms (default)
    ///        or @c occurred_at_ms (when @c by_occurred == true).
    std::vector<Event> recent(std::size_t n, bool by_occurred,
                              const Transaction& txn) const;

    /// @brief All events attributed to the given source.
    std::vector<Event> by_source(SourceId source_id,
                                 const Transaction& txn) const;

    /// @brief Events with @c occurred_at_ms in [from_ms, to_ms].
    ///        Bounds are inclusive on both sides.
    std::vector<Event> range_time(std::int64_t from_ms, std::int64_t to_ms,
                                  const Transaction& txn) const;
};

} // namespace mdbx_containers
```

Контрактные требования:

- **Оба timestamp-поля обязательны.** При создании Event оба поля заполняются явно; `observed_at_ms == 0` допустим, но `occurred_at_ms == 0` имеет специальный смысл «unknown».
- **`source_id` опционально.** Поле `std::optional<SourceId>`, отсутствие значения допустимо (событие без явного источника).
- **`recent` упорядочивание.** По умолчанию `by_occurred == false` — последние N по `observed_at_ms` (что соответствует «свежести для пользователя»). При `by_occurred == true` — последние N по `occurred_at_ms` (для temporal queries).
- **Inclusive bounds в `range_time`.** Оба конца диапазона включительно; семантика совпадает с `RangeIndexTable::range` при `exclusive_left == false` и `exclusive_right == false`.
- **Payload-совместимость.** Расширение `Event` — только аддитивное; старые payloads без `source_id` десериализуются как `nullopt`.

Use case: новостной RAG-pipeline вида `Article → Event → Entity`, где статья опубликована сегодня, но описывает событие, произошедшее вчера. Поле `source_id` ссылается на `ResourceId` исходной статьи, `occurred_at_ms` используется для temporal joins, `observed_at_ms` — для freshness ranking.

### 12.4 Decay-механика в MemoryStore

Требование: расширить `MemoryStore` (или эквивалентный domain-модуль для long-term facts) полями decay-механики. Цель — поддержать relevance-decay на основе давности и частоты использования.

```cpp
namespace mdbx_containers {

struct Fact {
    FactId id;
    FactPayload payload;
    EntityId entity_id;

    /// @brief Last retrieval-time use of this fact.
    std::int64_t last_used_at_ms;

    /// @brief Number of times the fact was returned in retrieval.
    std::uint64_t use_count;

    /// @brief Fused decay-aware score. Read-only; recomputed by
    ///        MemoryStore::candidates() and similar query methods.
    double decay_score;
};

struct CandidatesOptions {
    std::size_t limit = 32;
    double decay_half_life_ms = 30LL * 24 * 3600 * 1000; ///< 30 days
    double use_boost = 0.5;
    bool use_observed_at_ms = false;
};

class MemoryStore {
public:
    /// @brief Update last_used_at_ms and increment use_count.
    void mark_used(FactId id, std::int64_t now_ms, const Transaction& txn);

    /// @brief Top facts for @c entity_id, ranked by decay_score.
    std::vector<Fact> candidates(EntityId entity_id, std::int64_t now_ms,
                                 const CandidatesOptions& options,
                                 const Transaction& txn) const;
};

} // namespace mdbx_containers
```

Контрактные требования:

- **Поля `last_used_at_ms` и `use_count` обязательны**, инициализируются при insert: `last_used_at_ms = created_at_ms`, `use_count = 0`.
- **`decay_score` — вычисляемое поле.** Storage не обязан сохранять его в payload; `candidates()` возвращает объекты, у которых `decay_score` посчитан на момент вызова. Если payload содержит закэшированное значение, оно игнорируется (storage-of-truth — функция `candidates`).
- **Формула decay (ориентировочная, уточняется в tests).** `decay_score = use_boost * log1p(use_count) - elapsed_ms / decay_half_life_ms`, где `elapsed_ms = (use_observed_at_ms ? observed_at_ms : created_at_ms) → now_ms`.
- **`mark_used` идемпотентен по факту update-а.** При повторных вызовах в один и тот же `now_ms` итоговое `use_count` равно числу вызовов; дедупликация по `now_ms` не требуется.
- **Атомарность.** `mark_used` и `candidates` работают в рамках переданной `Transaction`; параллельные `mark_used` разных фактов не конфликтуют.

Use case: long-term facts в memory-агентах, где часто используемые факты «всплывают» выше, а забытые со временем опускаются вниз, не удаляясь физически.

### 12.5 TaskQueue / JobStore для async jobs

Требование: предоставить доменный модуль `TaskQueue` (он же `JobStore`) для persistent очереди задач с retry-семантикой. Цель — дать приложениям стандартный механизм для async-работы (embedding extraction, LLM enrichment, фоновая индексация), не изобретая свой job queue поверх `KeyValueTable`.

```cpp
namespace mdbx_containers {

enum class JobStatus { Pending, Running, Done, Failed, Dead };

struct Job {
    JobId id;
    std::string type;          ///< free-form job type discriminator
    std::string payload_json;  ///< opaque payload (JSON-encoded)
    JobStatus status;
    std::int64_t created_at_ms;
    std::int64_t run_after_ms; ///< earliest claim time
    std::uint32_t attempts;
    std::string last_error;
    std::optional<std::string> worker_id; ///< set while status == Running
};

class TaskQueue {
public:
    /// @brief Append a new job to the queue.
    void enqueue(Job job, const Transaction& txn);

    /// @brief Atomically claim the next runnable job for @c worker_id.
    /// @return std::nullopt if no job is currently runnable.
    std::optional<Job> claim_next(const std::string& worker_id,
                                  std::int64_t now_ms,
                                  const Transaction& txn);

    /// @brief Mark a previously claimed job as successfully done.
    void mark_done(JobId id, const Transaction& txn);

    /// @brief Mark a job as failed. Increments @c attempts; if
    ///        @c retry_after_ms is set, status returns to Pending
    ///        with updated @c run_after_ms, otherwise transitions to Dead.
    void mark_failed(JobId id, const std::string& error,
                     std::optional<std::int64_t> retry_after_ms,
                     const Transaction& txn);
};

} // namespace mdbx_containers
```

Контрактные требования:

- **Атомарность `claim_next`.** Выбор job-а и обновление `status = Running` с записью `worker_id` выполняется в одной write-транзакции. Два параллельных вызова получают разные job-ы (или оба получают `nullopt` при пустой очереди).
- **Runnability.** Job считается доступным для claim, если `status == Pending` и `run_after_ms <= now_ms`.
- **Retry policy.** `mark_failed` с заданным `retry_after_ms` возвращает job в `Pending` с обновлённым `run_after_ms`; `nullopt` — финальный fail (`status = Dead`).
- **`max_attempts` опционально.** На уровне API не задаётся жёсткий лимит попыток; при необходимости приложение само интерпретирует `attempts` и решает, переводить ли job в `Dead` явно.
- **Файл.** Реализация размещается в `include/mdbx_containers/TaskQueue.hpp`.
- **C++11 baseline.** `std::optional` и structured bindings guarded через `MDBXC_HAS_CPP17`.

Use case (опционально для `agent-memory-cpp`): async embedding extraction после добавления документа, async LLM enrichment (Contextual Retrieval), retry-able background jobs (re-parse документа, отправка telemetry). На первых итерациях допускается реализация на уровне приложения; модуль нужен, чтобы избежать дублирования при появлении второго сценария.

### 12.6 Готовые наборы сигнатур для наших generic-примитивов

Дополнить описания `ReverseIndexTable` и `RangeIndexTable` (см. секции 3.2 и 3.3) следующими методами, если их ещё нет в upstream-спецификации. Все дополнения — аддитивные, без изменения layout существующих классов.

#### 12.6.1 `ReverseIndexTable`

```cpp
/// @brief Outgoing references for @c node_id (used by GraphStore).
/// @return Vector of (inverted_key, primary) pairs where @c node_id
///         appears as the forward key.
std::vector<std::pair<InvertedKey, PrimaryKey>>
out_edges(const PrimaryKey& node_id, const Transaction& txn) const;

/// @brief Incoming references for @c node_id (reverse direction).
std::vector<std::pair<InvertedKey, PrimaryKey>>
in_edges(const PrimaryKey& node_id, const Transaction& txn) const;

/// @brief Neighbors of @c node_id reachable through edges of a
///        given logical type, where the type is encoded into
///        @c InvertedKey (e.g. as CompositeKey).
std::vector<PrimaryKey>
neighbors(const PrimaryKey& node_id, const InvertedKey& edge_type,
          const Transaction& txn) const;

/// @brief Cardinality of postings for an inverted key.
///        Equivalent to find(inv, txn).size() but possibly cheaper.
std::size_t count_by_inverted(const InvertedKey& inv,
                              const Transaction& txn) const;

/// @brief Bulk-remove all references to @c primary_ref. Returns
///        the number of (inverted_key, primary) pairs removed.
///        Already specified in 3.2; verify presence in upstream.
std::size_t remove_all_for(const PrimaryKey& primary_ref,
                           const Transaction& txn);
```

#### 12.6.2 `RangeIndexTable`

```cpp
/// @brief Last N entries in ascending key order. Backed by
///        @c range with @c from = first() and page size = N
///        taken from the tail. Used by EventStore.recent().
std::vector<std::pair<SortableKey, Payload>>
recent(std::size_t n, const Transaction& txn) const;

/// @brief Filter entries by source. Implemented either via a
///        secondary ReverseIndexTable on Payload::source_id,
///        or via a payload-side field check, depending on
///        the table configuration chosen by the caller.
std::vector<std::pair<SortableKey, Payload>>
by_source(const SourceId& source_id, const Transaction& txn) const;

/// @brief Inclusive lower bound. Returns the first entry whose
///        key is >= @c key, or std::nullopt if no such entry exists.
std::optional<std::pair<SortableKey, Payload>>
lower_bound_inclusive(const SortableKey& key, const Transaction& txn) const;

/// @brief Inclusive upper bound. Returns the first entry whose
///        key is > @c key, or std::nullopt if no such entry exists.
std::optional<std::pair<SortableKey, Payload>>
upper_bound_inclusive(const SortableKey& key, const Transaction& txn) const;
```

Контрактные требования к обоим наборам:

- **Аддитивность.** Ни один существующий публичный метод не переименовывается и не меняет сигнатуру.
- **`noexcept` политика.** Методы-обёртки над уже существующих batch-методов помечаются `noexcept` только если базовая операция noexcept; иначе исключения пробрасываются (см. `guides/coding-style.md` upstream-проекта).
- **Determinism.** Все методы, возвращающие коллекции, обязаны возвращать их в стабильном порядке (по `InvertedKey` / `SortableKey`).
- **Тесты.** В `external/mdbx-containers/tests/` добавляются контрактные тесты для каждого нового метода, в стиле существующих 15 тестовых файлов.

### 12.7 Пример использования в наших планах

Ниже — короткие сценарии, иллюстрирующие, как `agent-memory-cpp` будет использовать API, описанный в секциях 12.1–12.6. Сценарии не описывают полную реализацию, а показывают форму интеграции.

#### 12.7.1 `rrf_fuse` в `HybridRetrievalEngine`

```cpp
// src/agent_memory/retrieval/HybridRetrievalEngine.cpp (псевдокод)
std::vector<mdbx_containers::ScoredId> HybridRetrievalEngine::fuse(
    const std::vector<ScoredChunk>& bm25_hits,
    const std::vector<ScoredChunk>& vector_hits,
    std::size_t top_k) const {
    std::vector<std::vector<mdbx_containers::ScoredId>> lists{
        to_scored_ids(bm25_hits),
        to_scored_ids(vector_hits)
    };
    return mdbx_containers::rrf_fuse(lists, top_k, /*k_constant=*/60);
}
```

Заменяет локальный RRF в `agent-memory-cpp::detail::rrf_merge` единой реализацией из `mdbx-containers`.

#### 12.7.2 `EventStore` для temporal queries

```cpp
// MdbxTemporalIndex.cpp
std::vector<Event> MdbxTemporalIndex::events_on(std::int64_t day_ms,
                                                const Transaction& txn) const {
    const auto from = day_ms;
    const auto to   = day_ms + 24LL * 3600 * 1000 - 1;
    return event_store_.range_time(from, to, txn);
}

std::vector<Event> MdbxTemporalIndex::fresh(std::size_t n,
                                            const Transaction& txn) const {
    return event_store_.recent(n, /*by_occurred=*/false, txn);
}
```

Прямое использование API 12.3 без собственной сортировки и фильтрации в `agent-memory-cpp`.

#### 12.7.3 `mark_used` для QAPair-decay

```cpp
// MdbxQAKnowledgeBase.cpp
void MdbxQAKnowledgeBase::on_retrieval(QAPairId id,
                                       std::int64_t now_ms,
                                       const Transaction& txn) {
    // QAPair рассматривается как разновидность Fact для целей decay.
    memory_store_.mark_used(/*fact_id=*/to_fact_id(id), now_ms, txn);
}
```

Демонстрирует интеграцию decay-механики (12.4) с QA-слоем `agent-memory-cpp` без расширения payload-формата QAPair.

#### 12.7.4 `TaskQueue` для async embedding extraction

```cpp
// EmbeddingExtractionWorker.cpp
void EmbeddingExtractionWorker::tick(std::int64_t now_ms,
                                      const Transaction& txn) {
    auto job = task_queue_.claim_next(worker_id_, now_ms, txn);
    if (!job) return;

    try {
        extract_and_store(job->payload_json, txn);
        task_queue_.mark_done(job->id, txn);
    } catch (const std::exception& e) {
        task_queue_.mark_failed(job->id, e.what(),
                                /*retry_after_ms=*/60'000, txn);
    }
}
```

Использует API 12.5; сценарий помечен как опциональный для `agent-memory-cpp` и активируется при появлении второго реального потребителя job-очереди.

## Перекрёстные ссылки

- `external/mdbx-containers/PHILOSOPHY.md` — death-of-the-author, header-only
- `external/mdbx-containers/AGENTS.md` — coding agent workflow, critical defaults
- `external/mdbx-containers/guides/table-api-guide.md` — decision guide для выбора table classes
- `external/mdbx-containers/guides/coding-style.md` — naming, file layout, Doxygen
- `external/mdbx-containers/guides/implementation-notes.md` — transactions, serialization, table naming
- `external/mdbx-containers/include/mdbx_containers/KeyValueTable.hpp` — existing map-like
- `external/mdbx-containers/include/mdbx_containers/KeyMultiValueTable.hpp` — DUPSORT
- `external/mdbx-containers/include/mdbx_containers/AnyValueTable.hpp` — heterogeneous typed
- `external/mdbx-containers/include/mdbx_containers/SequenceTable.hpp` — uint64 IDs
- `external/mdbx-containers/include/mdbx_containers/detail/utils.hpp` — utilities
- `external/mdbx-containers/common/Connection.hpp` — transaction management
- `guides/lexical-search-roadmap.md` — направление lexical search
- `guides/optimization-roadmap.md` — binary bucket, vector encoding
- `guides/architecture.md` — DDD boundaries, dependency direction
- `guides/memory-stacks-roadmap.md` — Layer 1–4 архитектура памяти; секция 11 описывает Layer 1 (storage primitives, к которому относится этот ТЗ), секция 12 — DBI-схему с envelope + components + projections, секция 9 — capability matrix (открытие DBI по capability), секция 10 — validation rules (включая обязательность `scope_id`)
- `src/agent_memory/infrastructure/mdbx/MdbxDocumentStorage.cpp` — current usage
- `src/agent_memory/infrastructure/mdbx/MdbxResourceManifestStorage.cpp` — current usage
