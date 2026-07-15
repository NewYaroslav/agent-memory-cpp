# Memory Routing Roadmap

> C++17 compliance: кодовые сниппеты используют `const std::vector<T>&` вместо `std::span` и явные конструкторы вместо designated initializers. Routing решает задачу cheap pre-filter перед полным retrieval; это не замена BM25 / dense ANN / reranker, а первый слой над ними. Реализационные слои (RFF projection matrix, class-mean accumulator) живут в [`optimization-roadmap.md`](optimization-roadmap.md) §"RFF KDE Semantic Router"; здесь — типология подходов и decision tree.

## Source attribution policy

Этот гайд синтезирует материал из нескольких источников. Цитаты следуют двухуровневому паттерну:

- `[Source: <public citation>]` — основная цитата на опубликованную работу, документацию проекта, ревизию репозитория или статью. При цитировании внешнего источника публичная ссылка авторитетна.
- `[Source: internal note — no public source available. Path: <path>]` — утверждение взято только из внутренних исследовательских заметок без эквивалентного публичного источника на момент написания. Требует дополнительной проверки.

Внутренние заметки — это discovery aids, а не авторитетные цитаты. Публичные roadmap-заявления цитируют публичный источник первым; внутренние заметки служат лишь маркером частного происхождения.

Этот гайд покрывает спектр **memory routing** подходов — pre-retrieval фильтров, сокращающих search space перед полным BM25 + dense ANN + reranker пайплайном. Цель — дать набор решений «когда что выбирать», а не канонизировать один источник истины.

Related roadmaps:

- [`policies-roadmap.md`](policies-roadmap.md) — `AdaptiveRetrievalConfig` и `RetrievalMode` контракт, на котором построен pre-retrieval routing.
- [`optimization-roadmap.md`](optimization-roadmap.md) — реализационные слои RFF KDE router (binary signatures, encoder registry, SIMD path).
- [`memory-architectures-roadmap.md`](memory-architectures-roadmap.md) — broader memory architectures (Mem0, Zep, A-MEM, RLM); routing как pre-filter не конкурирует с ними.
- [`retrieval-techniques-roadmap.md`](retrieval-techniques-roadmap.md) — типология retrieval techniques; routing sits «до» них.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — `HybridRetrievalEngine` контракт и retrieval flow, который router предваряет.

## §1. Purpose

Этот гайд существует для трёх целей:

1. **Comparison framework.** Свести в одну карту routing approaches — RFF-density pre-filter (Sivertsen & Eidheim 2026), LLM-independent adaptive RAG (Marina et al., 2025; Jeong et al., 2024), плюс DLP-style profiling (user-modeling layer, не routing в строгом смысле: требует explicit policy layer; см. §6) — по единому набору осей: needs labels?, compute cost, cold-cache suitability, latency, accuracy tradeoff.
2. **Adoption guidance.** Для каждого подхода пометить, что из него `agent-memory-cpp` может воспроизвести через наши MDBX primitives (envelope/components/projections, posting DBI, embedding DBI) и какие требуют нового контракта (`IMemoryRouter`).
3. **Decision tree.** Дать читателю короткое дерево «когда какой подход», чтобы не канонизировать одну технику.

Non-goals:

- Не описывать internal-классы `agent-memory-cpp` ADR-уровня (это в `memory-stacks-roadmap.md`).
- Не каталогизировать ANN-выбор (это в `optimization-roadmap.md`).
- Не путать routing (pre-filter) с retrieval (top-K внутри выбранного partition). Routing решает «куда смотреть», retrieval — «что именно в этой зоне».

Routing **не заменяет** retrieval. Это первый дешёвый слой поверх полного BM25 + dense ANN + reranker пайплайна; цель — сократить search space с N единиц до N/K по разделам памяти (по проекту, по типу, по домену, по временному диапазону).

## §2. Two routing paradigms plus one routing-adjacent profiling layer

Три подхода (RFF-density, Adaptive RAG, и DLP-style profiling), различающиеся по тому, **где** маршрутизация принимает решение. DLP-style axes — это profiling layer, НЕ routing в строгом смысле: для превращения профиля в routing decision требуется explicit policy layer (см. §6).

| Paradigm | Где решение | Метки | Cold-cache | Пример |
|---|---|---|---|---|
| **RFF-density pre-filter** | В embedding-пространстве (до retrieval) | Требуются (по разделам) | Да (RFF projection matrix детерминирована, class means — на диске) | Sivertsen & Eidheim 2026 |
| **Adaptive RAG (no LLM)** | Над вопросом (feature-based classifier) | Требуются (нужен ли retrieval) | Частично (knowledgability предвычислена офлайн) | Marina et al. 2025; Jeong et al. 2024 |
| **DLP-style profiling axes** | Вне retrieval loop (per-user axis scores) | Требуются (по осям собеседника) | Да (axes — accumulated state на диске) | DLP-bridge pattern |

Каждый из них **дополняет** BM25 + dense ANN + reranker, а не заменяет. Routing — pre-filter на top-K partitions; retrieval — fine-grained top-K внутри partition.

## §3. Comparison Table

Routing paradigms по единой сетке осей. Значения — качественный обзор по источникам; конкретные цифры зависят от workload и подлежат эмпирической валидации.

| Ось | RFF-density pre-filter | Adaptive RAG (no LLM) | DLP-style profiling axes |
|---|---|---|---|
| **Needs labels?** | Да (per-class examples) | Да (нужен ли retrieval) | Да (per-axis examples) |
| **Compute cost (training)** | One matmul × classes (offline) | Soft-voting ensemble of small classifiers (offline) | Per-axis accumulation (online streaming) |
| **Compute cost (online per query)** | Single RFF projection + C dot products (~µs) | ~1.0 LLM calls avoided, but feature extraction + classifier (~ms) | Per-axis lookup + softmax (~µs) |
| **Cold-cache suitability** | Yes (RFF matrix on disk, class means on disk) | Partial (Wikidata / Wikipedia API requires warm caches; knowledgability offline) | Yes (axes table on disk) |
| **Latency per query** | O(C · D) ≈ tens of µs at C=10, D=512 | 1-10 ms (feature extraction dominates) | O(A · D) ≈ tens of µs at A=6, D=512 |
| **Accuracy tradeoff** | Miss top-1 → fallback full search (adaptive K) | InAcc ~38.8-38.9 vs Always RAG 38.4 (LMC 1.0 vs 1.7-42.1) | Calibrated axis scores feed downstream, not direct retrieval |
| **Where in pipeline** | Pre-retrieval: query → RFF → top-K partitions → BM25/ANN inside | Pre-retrieval: query → features → classifier → decision (retrieval yes/no) | Pre-retrieval: query → axis scores → cross-filter partitions |
| **Failure mode** | Class-mean drift on skewed distributions | Feature extraction noise; pretraining domain mismatch | Per-axis classifier overconfident on cold user (no events yet) |
| **Source** | [Source: Zenodo 20737147 — Sivertsen & Eidheim 2026] <br>[Internal note: ai-agent-playbook/concepts/llm-research/Random Fourier Features для compact memory router в AI-агенте.md] | [Source: aclanthology.org/2025.emnlp-main.439 — Marina et al., 2025] <br>[Source: arXiv:2403.14403 — Jeong et al., 2024] | [Internal note: ai-agent-playbook/concepts/ai-agents/DLP-подобная архитектура профилирования для AI-агента.md] |

Routing — не самоцель. Главная метрика — **изменение итоговых `Recall@k` и `nDCG` retrieval**, а не accuracy маршрутизатора как самостоятельной задачи. Это принципиально: маршрутизатор — служебный компонент, а не самостоятельная ML-модель.

## §4. RFF-Density Math

RFF (Random Fourier Features) аппроксимирует shift-invariant ядро через конечное признаковое отображение.

> **Feature map (chosen convention):**
> ```
> z_i(x) = sqrt(2/D) cos(w_i^T x + b_i),   i = 1..D
> ```
>
> Each of the `D` components has its own random `w_i ∈ R^d` (drawn from a Gaussian for Gaussian kernel; from a Laplacian / multivariate Cauchy for other shift-invariant kernels) and a random phase `b_i ∈ [0, 2π]` (uniform).
>
> **Storage contract:**
> - Matrix `W ∈ R^{D×d}` stored as MDBX fixed-size column-major
> - Vector `b ∈ R^D` stored as MDBX fixed-size column-major
> - Class vector `m_c ∈ R^D` stored as MDBX fixed-size column-major
> - All three stored as `float32` (or `float16` for compactness at ~2% accuracy loss)
>
> **NOT used:** the cos/sin-paired form `z(x) = 1/sqrt(D)[cos, sin]` (which would double the storage to `2D` and remove `b`). The paired form is a valid alternative but not the chosen contract; the Sivertsen & Eidheim 2026 reference implements an adjacent variant and is described only as a porting reference below.

Класс-прототип в RFF-пространстве:

```text
m_c = (1/N_c) · Σ_{i: y_i = c} z(x_i)
```

Решение (one inner product per class):

```text
f_c(x) ≈ z(x)^T · m_c
```

[Source: Zenodo 20737147 — Sivertsen & Eidheim 2026, "Classification through Kernel Density Estimation and Random Fourier Features with Label-Based Weighting, Feature Pruning, and the Laplacian Kernel"]

> **Reference implementation note.** The C++23 reference implementation in `kde-rf-classification` (gitlab.com/eidheim/kde-rf-classification, MIT) constructs `z(x)` with both `cos` and `sin` components: `z(x) = (1/sqrt(D)) · [cos(w_i^T · x), sin(w_i^T · x)]` for i in 0..D-1. The class mean becomes a 2D-vector sum; prediction is `likelihood_z(z) = (1/N) · z^T · sum`. Adaptive decision in `ReducedBayesClassifier::adjust_zs(dataset, scale)` performs label-based weighting by iterating over misclassified examples and applying `add_z(true_class)` / `subtract_z(predicted_class)`. The reference implementation is not a reusable SDK — to integrate, port the `Model` and `ReducedBayesClassifier` classes to our dependency-free contract. The reference uses the cos/sin-paired convention; we are NOT adopting that convention (see feature-map note above), only the algorithmic structure (KDE + label-based adjustment + feature pruning).

Properties per Sivertsen & Eidheim 2026:

- **Compactness:** classifier storage is `O(C · D + D · dim)` instead of `O(N · dim)` for nearest-neighbor style classifiers.
- **Parallelisable:** all classes are evaluated independently.
- **Online update:** class mean is updated incrementally as `class_sum[c] += z(x); class_count[c] += 1; class_mean[c] = class_sum[c] / class_count[c]`. No retraining required. After label-based correction (§4.1), the online-update path is split into two state slots — see §4.5.
- **Deployment simplicity:** one matrix `W` + one phase vector `b` + `C` mean vectors, no GPU required.

### 4.1. Label-Based Weighting

The `ReducedBayesClassifier::adjust_zs` routine (verified in the kde-rf-classification reference at gitlab.com/eidheim/kde-rf-classification, figure2.cpp lines 569-593, default `iterations = 1000`, `scale = 1.0`) iteratively corrects class means by:

1. Predicting all rows.
2. For each misclassified row, `model[true_label].add_z(z, scale)` and `model[predicted_label].subtract_z(z, scale)`.
3. Snapshotting accuracy at iterations 250, 500, 750, 1000.

Snapshot pattern: `scale = 1.0` for the main run; a separate run with `scale = 1e4` validates sensitivity to the weighting magnitude.

### 4.2. Feature Pruning

The `ReducedBayesClassifier::remove_lowest_sum_absolute_frequencies(scale)` routine (lines 522-542 of figure2.cpp) drops weak RFF components whose absolute mean falls below a noise floor. Default `scale = 0.1` retains the top 90% of features per class.

Two variants:

- **Per-model pruning:** feature selection is independent per class (different feature sets per model).
- **All-models aggregated pruning:** feature selection is joint across all classes (`remove_lowest_sum_absolute_frequencies_all_models`, lines 497-520), then applied symmetrically — produces identical dimensionality across models.

Authors report pruning is **not uniformly beneficial**: in some configurations it improves accuracy, in others it degrades it. Validate before shipping in production.

### 4.3. Laplacian Kernel

`k(x, y) = exp(-||x - y||_1 / sigma)` — uses L1 norm. Less sensitive to kernel width selection than Gaussian, particularly on less smooth densities. In `kde-rf-classification`, the `MultivariateCauchyDistribution` class (lines 234-252) samples `w_i = normal(gen) / |chi(gen)|` — yielding **multivariate** (correlated) Cauchy, not diagonal. This preserves correlation structure for non-independent features.

### 4.4. Limitations

- **Classification, not retrieval.** The router picks partitions; the actual top-K fragments come from BM25 / dense ANN inside the chosen partition.
- **Needs labels.** Without labels, only nearest-centroid works over known clusters.
- **Loses per-document info.** Only the class mean is stored; outliers and within-class structure are averaged away.
- **Cosine / Euclidean mismatch.** Semantic embeddings (E5, BGE, GTE) are typically trained for cosine similarity; RFF here uses Euclidean / Laplacian kernels. **Direct transfer is not guaranteed** — empirical validation required.
- **Threshold calibration.** Without calibration, top-K vs full-search fallback is hard to decide. Raw `f_c(x)` after label-based weighting is not necessarily a calibrated probability.
- **Small classes.** When `N_c << D`, the class mean is noisy and unstable.
- **Drift.** Online update may gradually shift class means in undesirable directions on skewed data.

### 4.5. observe() After Label-Based Correction

The RFF router's online-update path is split into two state slots, NOT one. Without this separation, "online update" loses statistical meaning after label-based correction:

1. `class_raw_sum[c]` — sum of `z(x_i)` for examples labeled class `c` (immutable per example, monotonically grows).
2. `class_adjusted_weights[c]` — adjusted model weights from label-based correction (modified by `add_z` / `subtract_z` operations; updated by `observe` only AFTER explicit correction).

Semantics:

```text
observe(x, label):
    class_raw_sum[label] += z(x)              # always updated
    class_adjusted_weights[label] += z(x)     # ONLY if correction has been explicitly committed
    class_count[label] += 1
```

`recompute_adjusted_weights()` (or equivalent commit step) is what finalises the `add_z` / `subtract_z` operations queued during label-based weighting (§4.1). Until that commit step runs, `observe()` MUST NOT touch `class_adjusted_weights` — otherwise the correction state is silently overwritten and the label-based weighting becomes a no-op in production.

## §5. Adaptive RAG (No LLM)

Two papers establish the LLM-independent adaptive retrieval pattern:

1. **Jeong et al., 2024 — Adaptive-RAG** (arXiv:2403.14403): introduces the idea of routing between no-retrieval / single-hop / multi-hop retrieval based on question complexity.
2. **Marina et al., 2025 — LLM-Independent Adaptive RAG** (ACL Anthology 2025.emnlp-main.439, EMNLP 2025): demonstrates a 27-feature lightweight classifier that decides whether retrieval is needed, costing **<1% FLOPs** of full generation.

Marina et al. report InAcc 38.8-38.9 vs Always RAG 38.4 and Hybrid UE 39.3, while reducing LLM calls (LMC) to ~1.0 vs 1.7-42.1 for uncertainty-based baselines.

### 5.1. Seven Feature Groups

| # | Group | What it measures | Tool / Model | Quality |
|---|---|---|---|---|
| 1 | Graph features | Entity connectivity in Wikidata (min/max/mean in/out degree) | Wikidata API | — |
| 2 | Popularity features | Wikipedia pageviews per year (min/max/mean) | Wikipedia pageviews API | — |
| 3 | Frequency features | Entity frequency in corpus + rarest n-gram frequency | Corpus statistics | — |
| 4 | Knowledgability | How well the LLM "knows" the entity (precomputed) | Verbalized uncertainty (offline) | — |
| 5 | Question type | 9 types (ordinal, count, generic, superlative, difference, intersection, multihop, yes/no, comparative) | bert-base-uncased (Mintaka) | Accuracy 0.93 |
| 6 | Question complexity | One-hop vs multi-hop | DistilBERT (FreshQA N-hop) | F1 0.82 |
| 7 | Context relevance | Relevance of retrieved fragments (min/max/mean + length) | BERT cross-encoder | — |

Entity linking: BELA, DeepPavlov.

[Source: aclanthology.org/2025.emnlp-main.439 — Marina et al., 2025] <br>[Internal note: ai-agent-playbook/concepts/rag-knowledge/Adaptive RAG — lightweight routing без LLM.md]

### 5.2. Architecture

```text
Вопрос → Entity Linking (BELA / DeepPavlov) → Feature Extractor (7 групп)
                                                     ↓
                              [Knowledgability, Frequency, Popularity — предвычислены]
                                                     ↓
                                    Classifier (soft-voting ensemble)
                                                     ↓
                              Retrieval? → BM25 / Dense retriever → LLM с контекстом
                                    ↓ No
                              LLM без retrieval (direct answer)
```

### 5.3. When to Apply / When NOT to Apply

Apply when:

- RAG pipeline where most queries do NOT need retrieval (or retrieval returns noise).
- Need to reduce LLM-call count / inference cost.
- Data permits pre-computing features (Wikidata, Wikipedia pageviews, corpus statistics).
- Feature-extraction latency is acceptable (still << LLM call cost).

Do NOT apply when:

- Questions are too diverse and no corpus exists for frequency / knowledgability pre-computation.
- Retrieval is almost always needed (Always RAG is cheaper than router + selective retrieval).
- Maximum accuracy is required regardless of cost (DRAGIN / AdaptiveRAG reach ~40-41 InAcc but cost more).
- Real-time streaming without feature-extraction delay.

### 5.4. Caveats

- **"No LLM" applies only to the router.** The answer generator is still LLM (LLaMA 3.1-8B, Qwen2.5-7B in the reference). Knowledgability is also LLM-computed, but offline.
- **InAcc 38.9 is not absolute answer accuracy.** It's the fraction of questions where the correct answer appears in the model output; full answer-accuracy on datasets may differ.
- **Context relevance requires retrieval for its computation.** If the router uses group 7, retrieval is still called — but the result may be discarded if relevance is low. This changes the economics.
- **Hybrids with uncertainty-based methods rarely synergize** except on MuSiQue. If an uncertainty pipeline already exists, external features will likely replace it rather than augment it.

## §6. DLP-Style Axes

The DLP-bridge pattern (see [Internal note: ai-agent-playbook/concepts/ai-agents/DLP-подобная архитектура профилирования для AI-агента.md]) is a user-modeling layer that records per-axis scores for the conversation partner. Unlike RFF (which classifies queries into partitions) and Adaptive RAG (which decides whether retrieval is needed), DLP-style axes classify the **user** into axes like `technical_interest`, `current_project`, `long_term_preference`, `temporary_state`, `confidence_level`.

> **DLP-style axes are NOT a router by themselves.** User profile axes like `prefers_concise_answers`, `knowledge_of_C++`, `current_project`, `temporary_state` are metadata about the user, not direct filter rules. To use them for routing, an additional policy layer is required:
>
> ```
> user profile (axes, evidence, TTL)
>   → retrieval policy (allowed scopes, boosts, limits)
>   → routing decision (which partitions to include / exclude / boost)
> ```
>
> **Mapping examples:**
> - `current_project = MegaConnector` → boost `scope:project:MegaConnector`; suppress off-topic scopes
> - `knowledge_of_C++ = high` → influence explanation depth AFTER retrieval, NOT partition selection
> - `prefers_concise_answers = true` → influence response construction, NOT retrieval filtering
> - `temporary_state = active` → influence time-decay weights, not partition routing
>
> Without this policy layer, DLP profiles are user-model metadata, not a pre-retrieval router. Building this policy layer is itself a roadmap item (see §9 M0-M2).

### 6.1. Axis Categories

| Category | Examples in DLP | Examples in user-modeling |
|---|---|---|
| Motivation | fear of failure / achievement | prefers concise answers, likes detailed explanations |
| Needs | control, recognition, autonomy | code-quality control, expertise recognition, stack autonomy |
| Style | formal / informal, aggressive | technical, no fluff, with source references |
| Knowledge | n/a | knows C++17, does not know Rust, surface knowledge of ML |
| Goals | n/a | works on agent-memory MVP, researches Mem0 |
| Constraints | security policies | no unstable extensions, flat submodules |

### 6.2. Storage: Evidence Chain

The DLP-bridge pattern distinguishes itself from naive profiling through an **evidence chain**: every claim (`memory_claim`) or hypothesis (`mental_hypothesis`) references specific events (`event_log`). This allows:

- Reviewing claims when new data arrives.
- Rolling back stale hypotheses via TTL.
- Explaining to the agent "why it thinks this style fits the user."
- Avoiding "psychotyping" from a single observation.

Minimal tables (simplified, see [Internal note: ai-agent-playbook/concepts/ai-agents/DLP-подобная архитектура профилирования для AI-агента.md] for full schema):

```text
event_log:          id, timestamp, source, text, metadata.
memory_claim:       type (preference/constraint/goal/skill/project/style/correction),
                    key, value, confidence, evidence_event_ids, expires_at, status.
mental_hypothesis:  state_type (intent/emotion/knowledge_state/uncertainty/motivation),
                    hypothesis, confidence, evidence_event_ids, ttl.
interaction_baseline: topic_distribution, style_stats, preferred_depth, correction_patterns.
```

### 6.3. ToM Sidecar

A separate lightweight agent that **does not communicate with the user** but returns structured JSON to the main agent:

```json
{
  "likely_intent": "research architecture for user-modeling memory layer",
  "relevant_user_constraints": ["prefers C++17", "wants practical architecture", "accepts Python worker"],
  "avoid": ["unsupported psychodiagnostic labels", "vague motivational advice"],
  "recommended_response_style": "structured, technical, concise"
}
```

Sidecar advantages: separation of concerns, testability as a standalone component, replaceability.

### 6.4. Axis Update Mechanics

Axes are updated via online streaming:

```text
axis_sum[c]   += z(embedding)
axis_count[c] += 1
axis_mean[c]   = axis_sum[c] / axis_count[c]
```

Identical arithmetic to RFF class-mean update; the only difference is the **semantic interpretation** (user axes vs memory partitions). Cross-pollination of code is encouraged — see §"Cross-Paradigm Implementation Synergies" below.

### 6.5. Privacy / Ethics

Crossing into user-profiling raises consent / regulation concerns:

- **Consent:** DLP employees are notified via employment contract; consumer agents need explicit consent for profiling.
- **Data minimization:** do not store what is not used; do not extract psychotypes without explicit purpose.
- **Right to be forgotten:** user can erase profile and all evidence.
- **Transparency:** user sees what the agent "knows" and can correct it.
- **Bias:** psycholinguistic features can be demographically skewed; fairness checks needed.
- **Regulation:** GDPR (especially Art. 22 — automated decisions with legal effects), 152-ФЗ, EU AI Act.

These constraints are not "blocking" but require architectural support (consent flag, data export, profile correction UI).

## §7. Integration with MDBX Layer

How a routing layer could live on top of our MDBX substrate. The interface is **optional**: agents may plug in their own routing implementation, or skip routing entirely if they do not need pre-filter.

### 7.1. Interface Sketch

> **Proposed API sketch — not implemented.** The `IMemoryRouter` interface below is illustrative; no ADR has defined its members or fields. The retrieval contract is real and lives at `src/agent_memory/retrieval/IRetrievalEngine.hpp` (interface) and `src/agent_memory/retrieval/HybridRetrievalEngine.hpp` (default implementation); see API scope note in §7.3 for the exact signatures.

> **Proposed API sketch — not implemented.**

```cpp
class IMemoryRouter {
public:
    virtual ~IMemoryRouter() = default;

    /// Compute routing scores for a query across all known partitions.
    /// Returns per-partition score in [0, 1] (or uncalibrated raw scores
    /// if calibration is disabled).
    virtual std::vector<RouteScore> route(
        const Embedding& query_embedding,
        const RouteQueryOptions& options) = 0;

    /// Top-K partitions by score, with optional confidence-margin fallback
    /// to "all partitions" when top-1 score is below threshold.
    virtual std::vector<PartitionId> top_k_partitions(
        const Embedding& query_embedding,
        uint32_t k,
        double confidence_threshold) = 0;

    /// Online update: increment class mean by new example.
    virtual void observe(
        PartitionId partition_id,
        const Embedding& example_embedding) = 0;
};

struct RouteScore final {
    PartitionId partition_id;
    double raw_score;                                 // arbitrary scale, e.g., dot product
    std::optional<double> calibrated_probability;     // [0, 1] after Platt/Isotonic if available
    ScoreKind kind;                                    // RAW, CALIBRATED, or BAYESIAN
    double confidence_margin;                          // gap to next-best partition
};

struct RouteQueryOptions final {
    std::optional<PartitionFilter> partition_filter; // restrict to subset
    bool apply_label_weighting = true;
    bool apply_feature_pruning = false;
};
```

`IMemoryRouter` is intentionally thin: it does NOT replace `IRetrievalEngine` (which does BM25 + dense ANN + reranker). It only narrows the partition set before retrieval runs.

### 7.2. MDBX Backing State

For RFF KDE router, persistent state is small. Storage follows the chosen random-phase feature map `z_i(x) = sqrt(2/D) cos(w_i^T x + b_i)`:

```text
rff_projection_matrix:
    key   = (scope_id, "w")
    value = float[D × dim]                  # RFF frequencies (column-major)

rff_phase_vector:
    key   = (scope_id, "b")
    value = float[D]                        # RFF phases, uniform in [0, 2π]

rff_class_means:
    key   = (scope_id, PartitionId)
    value = float[D]                        # class mean over z(x) = cos(...), one vector of length D

rff_class_counts:
    key   = (scope_id, PartitionId)
    value = uint64                          # N_c

rff_class_label_weighting:
    key   = (scope_id, PartitionId, "adjust_iterations")
    value = uint32                          # number of label-weighting iterations

rff_label_weighting_scale:
    key   = (scope_id, "scale")
    value = float                           # scale = 1.0 default
```

All keys are scope-aware (ADR-012); the projection matrix and phase vector are fixed once `MemoryStack::open` loads them (deterministic by `(dim, D, kernel, seed)`). Class means grow with `observe()` and may be recomputed after label-based correction.

### 7.3. Optional vs Required

Routing is **opt-in** at the `MemoryStack` level via `MemoryProfileSpec`:

```text
MemoryProfileSpec.enable_memory_router = false   # default off
MemoryProfileSpec.router_class = "rff_density"   # only "rff_density" today
MemoryProfileSpec.router_rff_D = 512
MemoryProfileSpec.router_rff_kernel = "laplacian" # or "gaussian"
MemoryProfileSpec.router_rff_seed = 0
MemoryProfileSpec.router_label_weighting_iterations = 0  # 0 = no label weighting
```

> **Proposed fields — not yet in `MemoryProfileSpec`.** The `enable_memory_router`, `router_class`, `router_rff_D`, `router_rff_kernel`, `router_rff_seed`, and `router_label_weighting_iterations` fields above are proposed; the current `MemoryProfileSpec` (see `memory-stacks-roadmap.md` ADR-002 / §6 Capability Flags) does NOT define these fields. They are introduced here as part of the routing layer's spec extension and require a separate ADR. Do not code against them until the ADR is merged.

> **API scope note:**
> - `IRetrievalEngine` (`src/agent_memory/retrieval/IRetrievalEngine.hpp`) — real, exists; signature is `RetrievalResponse retrieve(const RetrievalRequest&) const`. The default implementation is `HybridRetrievalEngine`.
> - `IRetriever` (`src/agent_memory/retrieval/IRetriever.hpp`) — real, exists; signature is `RetrievalResult retrieve(const RetrievalQuery&) const`. Lower-level than `IRetrievalEngine`; both compose in the retrieval pipeline.
> - `IMemoryRouter` — proposed; not yet implemented. No file in `src/agent_memory/routing/` today (verified by `git ls-files 'src/agent_memory/routing/**'`). It only narrows the partition set before retrieval runs; it does NOT replace either `IRetrievalEngine` or `IRetriever`.
> - `MemoryProfileSpec.router_*` fields — proposed; not yet in `MemoryProfileSpec`. See proposed-fields note above.

When `enable_memory_router = false`, `IRetrievalEngine::retrieve` operates over all partitions (no pre-filter). When `true`, retrieval first calls `IMemoryRouter::top_k_partitions(...)` and restricts the per-retriever search to that subset.

## §8. Cross-Paradigm Implementation Synergies

These approaches are NOT built on a common arithmetic base. RFF is a specific implementation choice for the RFF-density paradigm only; Adaptive RAG (feature-based classifier) and DLP-axes (axis scoring) use different primitives. Honest breakdown:

```text
RFF-specific primitives (only for RFF-density paradigm):
  - random feature projection z(x) = sqrt(2/D) cos(w^T x + b) with storage W, b, class prototype m_c
  - per-class mean accumulator (online updateable)
  - label-based weighting (ReducedBayesClassifier::adjust_zs analogue)
  - feature pruning (remove_lowest_sum_absolute_frequencies analogue)

Adaptive-RAG-specific primitives:
  - entity linking (BELA / DeepPavlov or similar)
  - graph, popularity, frequency, knowledgability, question-type, complexity, context-relevance features
  - feature-based soft-voting classifier (no LLM at routing time)
  - offline pre-computation pipeline for knowledgability / frequency / popularity features

DLP-profile-specific primitives:
  - per-axis profile scores (technical_interest, current_project, ...) with TTL
  - evidence chain (event_log, memory_claim, mental_hypothesis)
  - correction history (label-based weight updates on user-modeling evidence)
  - privacy / isolation boundary (profiles are per-user, never shared)
  - ToM sidecar that returns structured JSON without communicating with the user

Potentially reusable across paradigms (M2+):
  - labeled online accumulator (RFF-density class sums, DLP-axis sums; same arithmetic, different semantics)
  - calibration layer (applies across these approaches, with different inputs)
  - confidence/fallback policy (route uncertainty → broader retrieval)
  - storage interface (MDBX Layer 1 primitives; not routing-specific)
```

A unified `CompactClassifier` library could back RFF-density and DLP-axes (sharing only the accumulator arithmetic, NOT the feature map), with paradigm-specific adapters for output semantics. Adaptive RAG is the least reusable across these approaches: its features are tied to English corpora, Wikidata, and an LLM-side knowledgability signal that does not generalize to agent-internal routing. This is M2+ architecture; M1 ships RFF-density only.

## §9. Implementation Ladder

### M0: In-Memory Routing Table for One Stack (prototype)

- Implement `IMemoryRouter` interface in `src/agent_memory/routing/` as **header-only dependency-free contract**.
- Reference `Model` / `ReducedBayesClassifier` from `kde-rf-classification` (figure2.cpp lines 254-373, 416-605), MIT-licensed — port, do NOT copy.
- Storage: in-memory class-mean table (no MDBX persistence yet). Reload from saved state at `MemoryStack::open`.
- Bench harness: RFF vs cosine-nearest-centroid vs logistic-regression baselines on AgentLTM synthetic dataset.
- **Out of scope:** MDBX persistence, multi-process consistency, online update (read-only at startup).

### M1: Cross-Stack Routing

- MDBX persistence per §7.2 (`rff_projection_matrix`, `rff_class_means`, `rff_class_counts`).
- Online update via `IMemoryRouter::observe` per write — class-mean accumulator updated atomically with unit write transaction. After label-based correction (§4.5), `observe()` updates both `class_raw_sum` and `class_adjusted_weights` only when the correction has been explicitly committed; do not collapse the two state slots.
- Label-based weighting (RFF adjust iterations, default 0) optional behind `MemoryProfileSpec.router_label_weighting_iterations` (proposed field; see §7.3).
- Multi-stack: `rff_class_means` keyed by `(scope_id, PartitionId)` — multiple stacks share projection matrix but keep independent class means.

### M2: Federated Routing

- Cross-scope routing: query routed across multiple `scope_id` partitions (e.g., user-specific vs project-specific vs shared).
- Adaptive-RAG-style "do we even need retrieval?" pre-filter on top of RFF partition narrowing.
- ToM-sidecar integration: DLP-axes feed partition-filter hints.
- Per-encoder migration: switching embedding model triggers router rebuild (re-derive projection matrix, recompute class means with new model).

## §10. Decision Tree

```text
Запрос на memory routing capability?

├─ Нужно сузить search space по разделам (проект / тип / домен)?
│    → RFF-density pre-filter
│       [Source: Zenodo 20737147 — Sivertsen & Eidheim 2026]
│
├─ Нужно решить, нужен ли retrieval вообще (Adaptive-RAG style)?
│    ├─ Есть английский корпус + Wikidata / Wikipedia API?
│    │    → LLM-Independent Adaptive RAG (Marina et al., 2025)
│    │       [Source: aclanthology.org/2025.emnlp-main.439 — Marina et al., EMNLP 2025]
│    ├─ Multi-hop / one-hop routing?
│    │    → Adaptive-RAG (Jeong et al., 2024)
│    │       [Source: arXiv:2403.14403 — Jeong et al., 2024]
│    └─ Русскоязычный домен без Wikidata coverage?
│         → Обогатить фичи или fallback на RFF-density
│
├─ Нужно понимать собеседника по осям (technical_interest / current_project)?
│    → DLP-style axes + ToM sidecar
│       [Internal note: ai-agent-playbook/concepts/ai-agents/DLP-подобная архитектура профилирования для AI-агента.md]
│
├─ Корпус слишком мал для labels?
│    → Skip routing, всегда full retrieval
│
└─ Корпус уже отлично работает на Always RAG (нет шума, retrieval always нужен)?
     → Skip routing — adaptive retrieval экономит < 1% FLOPs, не оправдывает classifier overhead
```

Дерево не охватывает corner cases. Часто в одной системе живут 2-3 routing подхода одновременно (например, RFF для partition narrowing + DLP-axes для cross-filter hints).

## §11. Open Questions

1. **Calibration of RFF raw scores.** RFF output after label-based weighting is not necessarily a calibrated probability. Is separate softmax-normalisation across classes required? Is Platt-scaling appropriate? The Sivertsen & Eidheim 2026 paper does not address calibration explicitly.
2. **Cross-lingual RFF.** RFF-density was demonstrated on MNIST; transfer to E5 / BGE / multilingual embeddings is not validated. What kernel (Gaussian vs Laplacian) works best for cosine-trained embeddings?
3. **Online update drift.** How does class-mean quality degrade over long streaming sessions? Is periodic batch re-fitting required? Sivertsen & Eidheim 2026 do not address online drift.
4. **Imbalanced class sizes.** Memory partitions may have 10⁵ vs 10² examples. Does label-based weighting (default `scale = 1.0`) compensate? Or does it amplify skew?
5. **Threshold for "unknown" detector.** When all `f_c(x)` are low, the query is out-of-distribution. Static threshold (computed on held-out set) vs per-class adaptive threshold?
6. **Adaptive K.** Top-1 vs top-2 vs top-3 vs adaptive-K must be evaluated downstream on Recall@k. What is the optimal K for typical AgentLTM workloads?
7. **DLP-axis choice.** Which DLP axes (technical_interest, current_project) are best routed via RFF, and which (temporary_state, confidence_level) require calibrated probabilities?
8. **GDPR / consent for DLP-axes.** How is user consent captured? What is the retention policy for `event_log` and `memory_claim` evidence? Right-to-be-forgotten requires event-level erasure.
9. **Multi-model router migration.** Switching embedding model (e.g., bge-m3 → openai-3-small) invalidates the projection matrix and class means. Migration cost vs retrieval benefit?
10. **Feature pruning in production.** Sivertsen & Eidheim 2026 report pruning is not uniformly beneficial. Should we ship per-class pruning or symmetric all-models pruning? Default `scale = 0.1` or aggressive `scale = 0.5`?

## §12. References

### 12.1. Public sources

- **Sivertsen, Emilie Kjeldsberg; Eidheim, Ole Christian (2026). "Classification through Kernel Density Estimation and Random Fourier Features with Label-Based Weighting, Feature Pruning, and the Laplacian Kernel."** Zenodo 20737147, DOI 10.5281/zenodo.20737147, CC BY 4.0. URL: <https://zenodo.org/records/20737147>. Reference implementation: <https://gitlab.com/eidheim/kde-rf-classification> (MIT).
- **Jeong, Soyeong; Baek, Jinheon; Cho, Sukmin; Park, Sung Ju; Park, Donghyun; Lee, Dongwook; Lee, Minjoon (2024). "Adaptive-RAG: Learning to Adapt Retrieval-Augmented Large Language Models through Question Complexity."** arXiv:2403.14403. URL: <https://arxiv.org/abs/2403.14403>.
- **Marina, Maria; Ivanov, Nikolay; Pletenev, Sergey; Salnikov, Mikhail; Galimzianova, Daria; Krayko, Nikita; Konovalov, Vasily; Panchenko, Alexander; Moskvoretskii, Viktor (2025). "LLM-Independent Adaptive RAG: Let the Question Speak for Itself."** ACL Anthology 2025.emnlp-main.439, EMNLP 2025, Suzhou, China. URL: <https://aclanthology.org/2025.emnlp-main.439/>.

### 12.2. Internal notes (ai-agent-playbook)

- `ai-agent-playbook/concepts/llm-research/Random Fourier Features для compact memory router в AI-агенте.md` (internal note) — concept summary: RFF math, use cases in agent memory, limitations, experimental plan.
- `ai-agent-playbook/tools/llm-research/kde-rf-classification — RFF-density классификатор (MIT).md` (internal note) — tool summary: `figure2.cpp` architecture, `Model`/`ReducedBayesClassifier` classes, OpenMP dispatch, integration scenarios A/B/C.
- `ai-agent-playbook/resources/llm-research/KDE-RF classification — препринт Sivertsen, Eidheim 2026 - разбор.md` (internal note) — paper analysis: full mathematical treatment, label-based weighting, feature pruning, Laplacian kernel details.
- `ai-agent-playbook/concepts/ai-agents/DLP-подобная архитектура профилирования для AI-агента.md` (internal note) — DLP-bridge pattern, ToM sidecar, evidence chain, ethical boundaries.
- `ai-agent-playbook/concepts/rag-knowledge/Adaptive RAG — lightweight routing без LLM.md` (internal note) — LLM-Independent Adaptive RAG summary, 7 feature groups, soft-voting ensemble, when to apply / not apply.

### 12.3. In-house (agent-memory-cpp/guides/)

- [`optimization-roadmap.md`](optimization-roadmap.md) — RFF KDE Semantic Router section (experimental, pre-retrieval) — реализационные слои RFF projection, class-mean accumulator, evaluation plan.
- [`policies-roadmap.md`](policies-roadmap.md) — `AdaptiveRetrievalConfig`, `RetrievalMode`, `HybridRetrievalConfig` контракты, на которых построен pre-retrieval routing layer.
- [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) — `MemoryProfileSpec` capability flags. `IMemoryRouter` integration point is proposed (see §7.3); no ADR has yet wired it into `MemoryProfileSpec` flags.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — `HybridRetrievalEngine` контракт (existing), `IQueryTransformer` (planned), routing sits "до" retrieval flow.
- [`retrieval-techniques-roadmap.md`](retrieval-techniques-roadmap.md) — typology of retrieval techniques; routing sits "до" them.
- [`memory-architectures-roadmap.md`](memory-architectures-roadmap.md) — broader memory architectures (Mem0, Zep, A-MEM); routing as pre-filter does not compete with these.
- [`code-intelligence-roadmap.md`](code-intelligence-roadmap.md) — RFF KDE pattern borrowed from codebase-memory-mcp; this guide extends that pattern for production agent use.