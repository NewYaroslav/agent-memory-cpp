# policies-roadmap.md

Спецификация политик retrieval/write/decay/speaker для MemoryStack. Документ конкретизирует политики из `guides/memory-stacks-roadmap.md` секции 6.2 с детальными диапазонами, defaults per stack, валидацией и примерами.

## 1. Purpose

- Что описывает: DecayPolicy (anti-loop, забывание, soft-suppression), WritePolicy (trigger, dedupe, importance), SpeakerScopePolicy (multi-user фильтрация), RetrievalMode (associative/targeted/hybrid), HybridRetrievalConfig (RRF/weighted/learned).
- Cross-references: memory-stacks-roadmap.md (ADR-008, ADR-009), knowledge-units-roadmap.md (SoftSuppressed lifecycle), knowledge-base-roadmap.md (DecayAwareRetriever, AntiLoopCooldown).

## 2. DecayPolicy

### 2.1. DecayMode

```cpp
enum class DecayMode { None, Linear, Exponential, Logistic };
```

### 2.2. Параметры

| Поле | Тип | Диапазон | Default | Описание |
|---|---|---|---|---|
| mode | DecayMode | None/Linear/Exponential/Logistic | None | тип затухания |
| half_life_ms | double | [0, infinity) | 0 | время half-decay (если mode != None) |
| use_boost | double | [0, 10] | 0 | log1p(use_count) * use_boost |
| cooldown_ms | double | [0, infinity) | 0 | minimum time between re-retrievals |
| self_echo_suppression | double | [0, 1] | 0 | factor для подсвинка (own-speech) |
| cold_threshold | double | [0, 1] | 0 | below this score → archive candidate |

### 2.3. Формулы scoring

**Exponential decay:**

```
score(unit, now) = base_score
                  + use_boost * log1p(use_count)
                  * exp(-elapsed_ms / half_life_ms)
```

**Linear decay:**

```
score(unit, now) = base_score
                  + use_boost * log1p(use_count)
                  * max(0, 1 - elapsed_ms / (2 * half_life_ms))
```

**Logistic decay:**

```
score(unit, now) = base_score
                  + use_boost * log1p(use_count)
                  / (1 + exp((elapsed_ms - half_life_ms) / half_life_ms * 2))
```

### 2.4. Anti-loop Cooldown

После retrieval unit получает SoftSuppressed state на `cooldown_ms`. В течение cooldown unit:
- Исключается из default retrieval (если `AntiLoopCooldown` retriever включён).
- Score умножается на `self_echo_suppression` (если SpeakerId == agent_self_id).

### 2.5. Tier-specific decay (future)

В M2 — hot/warm/cold tier-specific policies:
- Hot tier: cooldown 30-60 sec, half-life 1-6 hours.
- Warm tier: cooldown 60 sec, half-life 3-14 days.
- Cold tier: no decay или very slow (30-180 days).

### 2.6. Defaults per Stack

Из memory-stacks-roadmap.md секции 8:

| Stack | DecayPolicy |
|---|---|
| BasicRag | None (no decay) |
| QAKnowledgeBase | None (frequency-based via use_count) |
| AgentLongTermMemory | Exponential, half_life=7d, use_boost=0.35, cooldown=60s, self_echo=0.3, cold_threshold=0.01 |
| SpeakerAwareChat | None (recent-based via temporal ordering) |
| CompiledWiki | None (no decay, статьи обновляются через re-compile) |
| TemporalFactStore | None (bi-temporal) |
| FullResearchMemory | Same as AgentLTM |

### 2.7. Validation Rules

- mode != None требует half_life_ms > 0.
- cooldown_ms >= 0 (если 0 — anti-loop disabled).
- self_echo_suppression в диапазоне [0, 1].
- cold_threshold в диапазоне [0, 1].

## 3. WritePolicy

### 3.1. WriteFlushTrigger

```cpp
enum class WriteFlushTrigger {
    OnEveryEvent,        // immediate write
    OnTimer,             // flush every flush_interval_ms
    OnSizeThreshold,     // flush when buffer >= size_threshold_bytes
    OnImportance,        // flush when importance_score >= importance_threshold
};
```

### 3.2. Параметры

| Поле | Тип | Диапазон | Default | Описание |
|---|---|---|---|---|
| trigger | WriteFlushTrigger | — | OnEveryEvent | условие flush |
| flush_interval_ms | uint64 | [0, infinity) | 0 | интервал для OnTimer |
| size_threshold_bytes | uint64 | [0, infinity) | 0 | порог для OnSizeThreshold |
| importance_threshold | double | [0, 1] | 0 | skip writes below |
| dedupe_distance_threshold | double | [0, 2] | 0 | skip if vector distance > this |
| allow_supersede | bool | — | true | разрешить supersede operation |
| allow_merge | bool | — | true | разрешить merge similar units |
| allow_episode_compaction | bool | — | true | разрешить episode compaction |

### 3.3. WriteGate behavior

```cpp
class WriteGate {
    GateDecision evaluate(const WriteRequest& req);

    // 1. importance threshold check
    if (req.importance_score < policy.importance_threshold)
        return GateDecision::Skip("below importance threshold");

    // 2. dedupe check (vector distance)
    auto nearest = embedding_store.search(req.embedding, limit=1);
    if (nearest && nearest[0].distance < policy.dedupe_distance_threshold)
        return GateDecision::Deduplicate(nearest[0].unit_id);

    // 3. supersede check (bi-temporal)
    if (policy.allow_supersede)
        if (auto superseded = check_supersede(req))
            return GateDecision::Supersede(superseded, req);

    // 4. flush trigger
    switch (policy.trigger) {
        case OnEveryEvent: return GateDecision::Accept;
        case OnTimer: buffer(); if (timer_elapsed()) flush(); break;
        case OnSizeThreshold: buffer(); if (size() >= threshold) flush(); break;
        case OnImportance: if (req.importance_score >= threshold) flush(); break;
    }

    return GateDecision::Buffer;
};
```

### 3.4. Defaults per Stack

| Stack | WritePolicy |
|---|---|
| BasicRag | OnEveryEvent, importance=0, dedupe=0 |
| QAKnowledgeBase | OnEveryEvent, importance=0, dedupe=0.05 |
| AgentLongTermMemory | OnTimer 30s, importance=0.4, dedupe=0.15, supersede=true, merge=true |
| SpeakerAwareChat | OnTimer 5s, importance=0.2 |
| CompiledWiki | OnEveryEvent (manual write), importance=0 |
| TemporalFactStore | OnEveryEvent, supersede=true, merge=false |
| FullResearchMemory | Same as AgentLTM + dedupe=0.1 |

### 3.5. Validation Rules

- OnTimer требует flush_interval_ms > 0.
- OnSizeThreshold требует size_threshold_bytes > 0.
- OnImportance требует importance_threshold > 0.
- importance_threshold в диапазоне [0, 1].
- dedupe_distance_threshold в диапазоне [0, 2] (cosine distance).

## 4. SpeakerScopePolicy

### 4.1. SpeakerScope enum

```cpp
enum class SpeakerScope {
    Self = 0,        // сам агент
    Owner = 1,       // владелец / автор
    Cohost = 2,      // со-ведущий / модератор
    Audience = 3,    // зритель / обычный user
    Unknown = 4,     // speaker не идентифицирован
};
```

### 4.2. Параметры

| Поле | Тип | Default | Описание |
|---|---|---|---|
| include_self | bool | true | фильтровать собственную речь агента |
| include_owner | bool | true | фильтровать владельца |
| include_cohost | bool | true | фильтровать со-ведущего |
| include_audience | bool | false | фильтровать зрителей |
| attribute_quote | bool | true | сохранять speaker_id в SourceRef |

### 4.3. SpeakerFilter в RetrievalPlan

```cpp
struct SpeakerScopePolicy { ... };

// В RetrievalPlan:
std::optional<SpeakerScopePolicy> speaker_filter;

// Если задан, retrieval исключает units где speaker_scope не в списке include_*
```

### 4.4. Defaults per Stack

| Stack | SpeakerScopePolicy |
|---|---|
| SpeakerAwareChat | self=true, owner=true, cohost=true, audience=false, quote=true |
| AgentLongTermMemory | self=true, owner=true, cohost=true, audience=true (для stream-аудитории), quote=true |
| Другие | N/A (SpeakerAttribution не включён) |

## 5. RetrievalMode

### 5.1. Modes

```cpp
enum class RetrievalMode {
    Associative,    // быстрый канал ~50ms (СВИНОПАС-style)
    Targeted,       // медленный канал ~4s (Сторож-style)
    Hybrid,         // оба канала + fusion
    Disabled,       // retrieval отключён (для no-answer scenarios)
};
```

### 5.2. Channels

**Associative channel:**
- Быстрый lexical/vector retrieval.
- Anti-loop cooldown filter.
- Returns top-K in 50ms.
- Используется в real-time chat / streaming.

**Targeted channel:**
- Полный retrieval с graph expansion + temporal filter + QA lookup.
- Может занимать 3-4 секунды.
- Используется для целевого поиска по запросу агента.

**Hybrid:**
- Parallel execution обоих каналов.
- RRF fusion результатов.
- Default mode для AgentLongTermMemory.

**Disabled:**
- Для no-answer scenarios (Anna AI urgency=9+).
- LLM отвечает без retrieval.
- Используется когда контекст не нужен.

### 5.3. Channel timeout в RetrievalPlan

```cpp
struct RetrievalPlan {
    double associative_timeout_ms = 50.0;
    double targeted_timeout_ms = 4000.0;
    // ...
};
```

## 6. HybridRetrievalConfig

### 6.1. FusionStrategy

```cpp
enum class FusionStrategy { RRF, WeightedMax, Learned, Planner };
```

### 6.2. RRF formula

```
score(unit) = sum_r (weight_r / (k_constant + rank_r))
```

где `rank_r` — позиция unit в result list retriever'а r (1-indexed), `weight_r` — per-retriever вес.

### 6.3. Параметры

| Поле | Тип | Default | Описание |
|---|---|---|---|
| fusion | FusionStrategy | RRF | стратегия fusion |
| rrf_k_constant | double | 60.0 | k в RRF formula |
| retriever_weights | vector<double> | {1.0, ...} | вес per retriever |
| candidate_pool_size | size_t | 200 | top-K до fusion |

### 6.4. Defaults per Stack (RRF weights)

| Stack | Retriever weights (lexical, vector, qa, graph, temporal) |
|---|---|
| BasicRag | {1.0, 1.0} |
| QAKnowledgeBase | {0.5, 0.8, 2.0, 0, 0.3} |
| AgentLongTermMemory | {1.0, 1.0, 1.5, 0.5, 1.0} |
| SpeakerAwareChat | {1.0, 0, 0, 0, 1.0} |
| CompiledWiki | {1.0, 1.0, 0, 0.3, 0} |
| TemporalFactStore | {1.0, 0, 0, 0.7, 2.0} |
| FullResearchMemory | {1.0, 1.0, 1.5, 0.5, 1.0} |

### 6.5. Other Fusion Strategies

- **WeightedMax:** max(score_r * weight_r) вместо sum — выбирает лучший retriever.
- **Learned:** ML-модель учит веса на основе feedback (M2+).
- **Planner:** RLM-style recursive planner — LLM-driven выбор retriever'ов (M2+ research).

## 7. Defaults Summary Table

Сводная таблица: stack → (Decay, Write, Speaker, RetrievalMode, Hybrid).

| Stack | Decay | Write | Speaker | Mode | Hybrid weights |
|---|---|---|---|---|---|
| BasicRag | None | OnEveryEvent | N/A | Hybrid | {1.0, 1.0} |
| QAKnowledgeBase | None | OnEveryEvent | N/A | Targeted | {0.5, 0.8, 2.0, 0, 0.3} |
| AgentLongTermMemory | Exp/7d | OnTimer 30s | full | Hybrid | {1.0, 1.0, 1.5, 0.5, 1.0} |
| SpeakerAwareChat | None | OnTimer 5s | chat | Targeted | {1.0, 0, 0, 0, 1.0} |
| CompiledWiki | None | OnEveryEvent | N/A | Targeted | {1.0, 1.0, 0, 0.3, 0} |
| TemporalFactStore | None | OnEveryEvent | N/A | Targeted | {1.0, 0, 0, 0.7, 2.0} |
| FullResearchMemory | Exp/7d | OnTimer 30s | full | Hybrid | {1.0, 1.0, 1.5, 0.5, 1.0} |

## 8. Validation Rules (cross-cutting)

Все политики валидируются при `MemoryStack::open(spec)`:

| Правило | Если нарушено |
|---|---|
| DecayPolicy.mode != None → UsageStats=true | "DecayPolicy requires UsageStatsComponent" |
| SpeakerPolicy → SpeakerAttribution=true | "SpeakerPolicy requires SpeakerAttribution" |
| WritePolicy.OnTimer → flush_interval_ms > 0 | "Invalid WritePolicy" |
| WritePolicy.OnImportance → importance_threshold > 0 | "Invalid WritePolicy" |
| HybridRetrievalConfig → retriever_weights.size() == enabled retrievers count | "Weights size mismatch" |
| ContextBudget → sum(per_block) <= total_tokens | "ContextBudget overflow" |

## 9. References

- `guides/memory-stacks-roadmap.md` — секции 6.2, 6.3, 8 (defaults per stack).
- `guides/knowledge-units-roadmap.md` — SoftSuppressed lifecycle state.
- `guides/knowledge-base-roadmap.md` — DecayAwareRetriever, AntiLoopCooldown.
- `guides/compaction-roadmap.md` — как политики взаимодействуют с compaction jobs.
- ai-agent-playbook: concepts/rag-knowledge/Внешняя память LLM-агентов — система СВИНОПАС.md — anti-loop / cooldown / подсвинок.
- ai-agent-playbook: resources/llm-research/Memory for Autonomous LLM Agents survey — конспект.md — write-read-manage loop, learned forgetting.
