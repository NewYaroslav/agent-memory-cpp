# runtime-services-roadmap.md

Спецификация cross-cutting runtime сервисов (PromptCache, AsyncIndexer, WriteGate) для подсистемы памяти `agent-memory-cpp`. Документ конкретизирует ADR-013 (Runtime services) из `guides/memory-stacks-roadmap.md` секции 11.

## 1. Purpose

- Что описывает: PromptCache (LRU по prompt prefix + AnthropicCacheControl adapter), AsyncIndexer (batch вставки в lexical/vector индексы), WriteGate (применяет WritePolicy). Все сервисы ортогональны profile (доступны через интерфейсы, не зависят от конкретного MemoryStack).
- Cross-references: memory-stacks-roadmap.md (ADR-013, секции 7, 12.4), knowledge-base-roadmap.md (RetrievalTrace), policies-roadmap.md (WritePolicy), compaction-roadmap.md (job submission).

## 2. Layer Architecture Review

Per memory-stacks-roadmap.md секция 11:

```
Layer 1: Storage Primitives
Layer 2: Retrieval Primitives
Layer 3: Memory Stacks
Layer 4: Applications

Cross-cutting Runtime Services (orthogonal):
  PromptCache, CompactionWorker, WriteGate, AsyncIndexer
  Используют Layer 1 + Layer 2 через интерфейсы
```

Runtime-сервисы доступны из любого layer, но сами не зависят от конкретного MemoryStack. Каждый сервис — singleton per MemoryStack (если включён в spec).

## 3. PromptCache

### 3.1. Purpose

Кэширование LLM responses по prompt prefix. Экономит 60-90% токенов для long-running сессий через `cache_control: ephemeral` (Anthropic API) или аналог.

Per `concepts/llm-research/Управление контекстом LLM-агента - стратегии снижения стоимости.md` (ai-agent-playbook): cache hit rate > 70% — основная цель.

### 3.2. Interface

```cpp
class IPromptCacheProvider {
public:
    virtual ~IPromptCacheProvider() = default;

    virtual std::optional<CachedResponse> lookup(
        const PromptKey& key) = 0;

    virtual void store(
        const PromptKey& key,
        const CachedResponse& response) = 0;

    virtual void invalidate(
        const PromptKey& key) = 0;

    virtual void invalidate_scope(ScopeId scope) = 0;

    virtual PromptCacheMetrics metrics() const = 0;
};

struct PromptKey {
    ScopeId scope_id;          // scope-aware
    std::string provider_id;   // "anthropic", "openai", "ollama"
    std::string model_id;
    std::string prompt_prefix;  // normalized, hashed
    std::optional<std::string> suffix;  // optional suffix for partial match
};

struct CachedResponse {
    std::string response_text;
    uint64_t input_tokens;
    uint64_t output_tokens;
    uint64_t cache_read_input_tokens;
    uint64_t cache_write_input_tokens;
    uint64_t created_at_ms;
    std::chrono::seconds ttl{3600};  // default 1 hour
};
```

### 3.3. PromptPrefixCache (LRU implementation)

```cpp
class PromptPrefixCache : public IPromptCacheProvider {
public:
    explicit PromptPrefixCache(
        size_t max_entries = 10000,
        size_t max_bytes = 100 * 1024 * 1024);  // 100 MB

    std::optional<CachedResponse> lookup(const PromptKey& key) override;
    void store(const PromptKey& key, const CachedResponse& response) override;
    void invalidate(const PromptKey& key) override;
    void invalidate_scope(ScopeId scope) override;
    PromptCacheMetrics metrics() const override;

private:
    struct Entry {
        PromptKey key;
        CachedResponse response;
        uint64_t last_access_ms;
        size_t size_bytes;
    };

    std::list<Entry> m_lru;  // front = most recent
    std::unordered_map<PromptKey, list_iterator> m_index;
    size_t m_max_entries;
    size_t m_max_bytes;
    size_t m_current_bytes = 0;
    mutable std::shared_mutex m_mutex;
    PromptCacheMetrics m_metrics;
};
```

LRU eviction по size_bytes (когда превышен max_bytes) и по age (TTL на каждую запись).

### 3.4. PromptCacheMetrics

```cpp
struct PromptCacheMetrics {
    uint64_t lookups = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t stores = 0;
    uint64_t evictions = 0;
    uint64_t invalidations = 0;
    uint64_t bytes_saved_input = 0;     // input tokens saved through cache
    uint64_t bytes_saved_output = 0;
    uint64_t total_input_tokens = 0;   // raw baseline
    uint64_t total_output_tokens = 0;

    double hit_rate() const {
        return lookups > 0 ? double(hits) / double(lookups) : 0.0;
    }

    double token_savings_ratio() const {
        auto total = total_input_tokens + total_output_tokens;
        return total > 0 ? double(bytes_saved_input + bytes_saved_output) / double(total) : 0.0;
    }
};
```

### 3.5. Adapters

```cpp
// AnthropicCacheControlAdapter — translates cache hit/miss to Anthropic API metadata
class AnthropicCacheControlAdapter : public IPromptCacheProvider {
    // На lookup hit: возвращает CachedResponse с cache_read_input_tokens > 0.
    // На lookup miss: возвращает nullopt, caller делает API call.
    // На store: после API call с cache_control: ephemeral, сохраняет response.
};

// NoOpAdapter — для профилей без cache
class NoOpPromptCache : public IPromptCacheProvider {
    // Всегда nullopt на lookup, ничего не делает на store.
};
```

### 3.6. Persistence (опционально)

Для M1+ PromptCache может опционально персистить в MDBX DBI:

```
prompt_cache
  key = (scope_id, prompt_key_hash) → CachedResponse
```

При MemoryStack::open() — загрузить из DBI. На eviction (LRU в memory) — удалить из DBI. Это переживает restart.

Для M0 — in-memory only.

### 3.7. Validation Rules

- `enable_prompt_cache=true` → MemoryStack создаёт PromptCache singleton.
- scope-aware key: разные scope_id имеют разные cache entries.
- TTL: default 1 час, configurable per provider.

## 4. AsyncIndexer

### 4.1. Purpose

Батчинг вставок в lexical/vector индексы для уменьшения write latency. Пишет в MemoryStack немедленно, но propagation в secondary indexes (inverted_token_to_unit, embedding_vectors) — async через очередь.

### 4.2. Interface

```cpp
class IAsyncIndexer {
public:
    virtual ~IAsyncIndexer() = default;

    // Enqueue indexing job
    virtual void enqueue(IndexJob job) = 0;

    // Force flush (для admin/test)
    virtual void flush() = 0;

    // Status
    virtual AsyncIndexerStats stats() const = 0;
};

struct IndexJob {
    enum class Op { Upsert, Erase };
    Op op;
    KnowledgeUnitId unit_id;
    std::vector<SearchProjection> projections;
    std::vector<EmbeddingWriteRequest> embeddings;
};

struct AsyncIndexerStats {
    uint64_t jobs_enqueued = 0;
    uint64_t jobs_processed = 0;
    uint64_t jobs_failed = 0;
    uint64_t batches_processed = 0;
    uint64_t current_queue_size = 0;
    std::chrono::milliseconds avg_latency{0};
};
```

### 4.3. Implementation

```cpp
class BackgroundIndexer : public IAsyncIndexer {
public:
    explicit BackgroundIndexer(
        MemoryStack& stack,
        size_t batch_size = 1000,
        size_t max_bytes = 50 * 1024 * 1024);

    void enqueue(IndexJob job) override;
    void flush() override;
    AsyncIndexerStats stats() const override;

private:
    void worker_loop();
    void process_batch(std::vector<IndexJob>& batch);

    MemoryStack& m_stack;
    size_t m_batch_size;
    size_t m_max_bytes;

    std::queue<IndexJob> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    std::atomic<bool> m_running{true};
    AsyncIndexerStats m_stats;
};
```

Worker thread батчит до batch_size или max_bytes, потом flushes через MultiTableWriter. На stop — flushes остаток.

### 4.4. Batch triggers

- Size: batch_size (default 1000 jobs).
- Bytes: max_bytes (default 50 MB).
- Time: max_wait_ms (default 100 ms).

### 4.5. Failure handling

При ошибке в batch:
- Save checkpoint (последний успешный job).
- Re-enqueue failed jobs.
- Increment stats.jobs_failed.
- Если retry > 3 — log error и drop.

## 5. WriteGate

### 5.1. Purpose

Применяет WritePolicy из spec к каждой WriteRequest. Реализует importance threshold, dedupe, supersede, flush triggers.

### 5.2. Interface

```cpp
class IWriteGate {
public:
    virtual ~IWriteGate() = default;

    virtual GateDecision evaluate(const WriteRequest& req) = 0;

    // Manual flush (для тестов)
    virtual void flush() = 0;
};

enum class GateAction {
    Accept,        // write immediately
    Buffer,        // buffer до flush trigger
    Deduplicate,   // skip (existing similar unit)
    Supersede,     // replace old unit
    Merge,         // merge with existing
    Skip,          // skip (below importance threshold)
};

struct GateDecision {
    GateAction action;
    std::optional<KnowledgeUnitId> related_unit_id;  // для Deduplicate / Supersede / Merge
    std::string reason;
};
```

### 5.3. Implementation

```cpp
class DefaultWriteGate : public IWriteGate {
public:
    explicit DefaultWriteGate(
        MemoryStack& stack,
        WritePolicy policy);

    GateDecision evaluate(const WriteRequest& req) override;
    void flush() override;

private:
    MemoryStack& m_stack;
    WritePolicy m_policy;
    std::vector<WriteRequest> m_buffer;
    std::mutex m_mutex;
    std::chrono::steady_clock::time_point m_last_flush;
};
```

Реализует per policies-roadmap.md секция 3.3 (WriteGate behavior).

### 5.4. Integration с WritePolicy

Все правила из policies-roadmap.md секция 3 применяются:
- importance_threshold check.
- dedupe_distance_threshold через vector search.
- supersede check (bi-temporal).
- Flush trigger (OnTimer / OnSizeThreshold / OnImportance).

## 6. Service Lifecycle

### 6.1. Опциональность

Каждый сервис — opt-in через MemoryProfileSpec:

| Service | Capability | Default |
|---|---|---|
| PromptCache | `enable_prompt_cache = true` | opt-in |
| AsyncIndexer | (always on for write perf) | always |
| WriteGate | (always on if WritePolicy set) | conditional |
| CompactionWorker | `enable_compaction = true` | opt-in |

### 6.2. Инициализация

При MemoryStack::open(spec):
1. Создаются DBI по capabilities.
2. Инициализируются runtime-сервисы: PromptCache если opt-in, AsyncIndexer всегда, CompactionWorker если opt-in.
3. WriteGate создаётся если spec.write_policy задан.

### 6.3. Shutdown

При MemoryStack::close():
1. Stop accepting new requests.
2. AsyncIndexer.flush() — finish pending batches.
3. CompactionWorker.stop() — finish current job, then exit.
4. PromptCache — опционально persist to DBI.
5. Освобождение handles.

### 6.4. Graceful degradation

Если runtime-сервис не может стартовать (например, MDBX не хватает места):
- Log error.
- MemoryStack продолжает работать в degraded mode (без этого сервиса).
- Service выбрасывает `RuntimeServiceUnavailable` при обращении.

## 7. Interaction Patterns

### 7.1. Write path

```
Application
  ↓ stack.write_unit(request)
  ↓
WriteGate.evaluate(request)
  ↓
  ├── Accept → Enqueue to AsyncIndexer + MultiTableWriter (envelope + components + projections)
  ├── Buffer → wait for trigger
  ├── Deduplicate → return existing unit_id
  ├── Supersede → mark old as Superseded, write new
  ├── Merge → combine with existing
  └── Skip → return Skip decision
```

### 7.2. Read path

```
Application
  ↓ stack.retrieve(plan)
  ↓
PromptCache.lookup(prompt_key)  // если opt-in
  ├── hit → return cached response
  └── miss → continue
  ↓
HybridRetriever.retrieve(plan)
  ├── LexicalRetriever (per lexical-search-roadmap.md)
  ├── DenseRetriever (per optimization-roadmap.md)
  ├── ...
  ↓
RRF fusion
  ↓
ContextBuilder
  ↓
LLM call (or cached)
  ↓
PromptCache.store()  // для next call
```

### 7.3. Background path

```
CompactionWorker
  ↓
ICompactionJob.run()
  ├── DecayJob → uses UsageStatsComponent
  ├── DedupeJob → uses EmbeddingStore + scope
  ├── ArchiveColdJob → uses Lifecycle FSM
  ├── ...
  ↓
MultiTableWriter (atomic per job)
```

## 8. Observability

### 8.1. Метрики (per service)

Каждый сервис экспортирует свои метрики (см. секции выше). Все метрики доступны через:

```cpp
auto stats = stack.stats();
// stats.prompt_cache, stats.async_indexer, stats.compaction, stats.write_gate
```

### 8.2. RetrievalTrace integration

Per knowledge-base-roadmap.md: `RetrievalTrace.trace` содержит:
- `cache_hit` (true/false).
- `cache_key` (если hit).
- `async_indexer_queue_size` (snapshot при retrieval).
- `compaction_active_jobs` (snapshot).

### 8.3. CLI integration

```
agent-memory-cli cache stats
agent-memory-cli cache clear [--scope <scope_id>]
agent-memory-cli indexer status
agent-memory-cli indexer flush
```

## 9. Implementation Order

Per memory-stacks-roadmap.md секция 16, конкретизация:

| Шаг | Что |
|---|---|
| 11.1 | WriteGate (impl WritePolicy logic) |
| 11.2 | AsyncIndexer (background thread + batch processing) |
| 12.5 | PromptCache (in-memory LRU) |
| 12.6 | AnthropicCacheControlAdapter |
| 12.7 | PromptCache persistence (M2) |

## 10. Open Issues

- PromptCache invalidation при обновлении knowledge (когда unit перезаписан, cache entries могут быть stale). Решение: scope-based invalidation при bulk update.
- AsyncIndexer backpressure: если consumer (retrieval) медленнее producer (writes), queue растёт. Решение: bounded queue + drop policy.
- WriteGate flush trigger: на скольких units считать "OnSizeThreshold" — bytes или count?
- Multi-stack coordination: если несколько MemoryStack разделяют scope, runtime services не координируются. M2+.

## 11. References

- `guides/memory-stacks-roadmap.md` — секции 7, 11, 12.4, 16.
- `guides/knowledge-base-roadmap.md` — RetrievalTrace интеграция.
- `guides/policies-roadmap.md` — WritePolicy.
- `guides/compaction-roadmap.md` — CompactionWorker.
- `guides/lexical-search-roadmap.md` — LexicalRetriever.
- `guides/optimization-roadmap.md` — DenseRetriever.
- ai-agent-playbook: concepts/llm-research/Управление контекстом LLM-агента - стратегии снижения стоимости.md — prompt caching economics.
- ai-agent-playbook: concepts/ai-agents/AI-VTuber с нуля на TypeScript - модульная архитектура.md — модуль memory с async indexer.
