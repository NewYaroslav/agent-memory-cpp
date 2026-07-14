# Retrieval Techniques Roadmap

> C++17 compliance: кодовые сниппеты используют `const std::vector<T>&` вместо `std::span` и явные конструкторы вместо designated initializers. BM25 и BM25F-формулы скопированы **verbatim** из источника `ai-agent-playbook/concepts/rag-knowledge/BM25 и BM25F — формулы и reference implementation.md` для traceability; мы не «улучшаем» математику, а документируем контракт поверх нашего pipeline.

## Source attribution policy

Этот гайд синтезирует материал из нескольких источников. Цитаты следуют двухуровневому паттерну:

- `[Source: <public citation>]` — основная цитата на опубликованную работу, документацию проекта, ревизию репозитория или статью. При цитировании внешнего источника публичная ссылка авторитетна.
- `[Source: internal note — no public source available. Path: <path>]` — утверждение взято только из внутренних исследовательских заметок без эквивалентного публичного источника на момент написания. Требует дополнительной проверки.

Внутренние заметки — это discovery aids, а не авторитетные цитаты. Публичные roadmap-заявления цитируют публичный источник первым; внутренние заметки служат лишь маркером частного происхождения.

Этот гайд покрывает спектр retrieval-техник, выходящих за рамки lexical+vector pair, описанных в [`lexical-search-roadmap.md`](lexical-search-roadmap.md) и [`optimization-roadmap.md`](optimization-roadmap.md). Цель — дать обзор того, что архитектурно ложится на наш `HybridRetriever` (см. `knowledge-base-roadmap.md` §7.3) и `IQueryTransformer` (см. `memory-stacks-roadmap.md` §13), и какие техники требуют дополнительной инфраструктуры.

Гайд **синтезирует** материалы из `ai-agent-playbook` (приведены с цитатами в каждой секции). Не копирует исходники дословно и не выдумывает архитектурные детали, которых нет в первоисточниках.

Related roadmaps:

- [`lexical-search-roadmap.md`](lexical-search-roadmap.md) — BM25F первая линия, postings, tokenization, projection-aware indexing.
- [`optimization-roadmap.md`](optimization-roadmap.md) — vector/binary/ANN optimization layer.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — `HybridRetriever` контракт, RRF fusion, decay-aware scoring, evaluation pipeline.
- [`memory-architectures-roadmap.md`](memory-architectures-roadmap.md) — broader memory architectures (Mem0, Zep, A-MEM, RLM).
- [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §13 — Runtime Services и `IQueryTransformer` контракт.

## §1. Purpose

> **Three tiers of API references in this doc.** (1) **Real** (есть в `src/agent_memory/`): `HybridRetrievalEngine`, `IRetriever`. (2) **Planned API — not yet implemented.** Определены как roadmap-структуры в `memory-stacks-roadmap.md` §13 / `policies-roadmap.md` / `lexical-search-roadmap.md`, но не в actual code: `HybridRetrievalConfig`, `IQueryTransformer`, `IAsyncIndexer`, `RetrievalPlan`. (3) **Proposed API sketch — not implemented.** Иллюстративные имена/методы/поля, не зафиксированные ни в каком ADR: `HybridRetriever` (≠ `HybridRetrievalEngine`), `RetrievalPlan.retrievers[]`, `WeightedMax`/`Learned` fusion names. Не полагайтесь на эти имена до утверждения ADR.

Этот документ предназначен для трёх целей:

1. **Typology of retrieval techniques.** Свести в одну карту 8+ классов retrieval: Naive / Advanced / Hybrid / Contextual / Graph / Fusion / Adaptive / Agentic / RLM — какие техники в каждом классе, когда включать, какая сложность.
2. **Implementation mapping.** Для каждой техники пометить, что из неё `agent-memory-cpp` может воспроизвести через наши MDBX primitives (envelope/components/projections, posting DBI, embedding DBI) и что требует нового контракта.
3. **Open questions, not coverage.** Этот гайд явно говорит «не знаем» о вещах, по которым в playbook недостаточно эмпирики (per-domain α для RRF/PRF, реальная экономия Contextual Retrieval для русского).

Non-goals:

- Не повторять детальную спецификацию BM25F/BM25 (это в `lexical-search-roadmap.md`).
- Не задавать `MemoryProfileSpec` defaults. Техники выбираются через `RetrievalPlan.retrievers[]` + `HybridRetrievalConfig`.
- Не описывать infra (vector DB выбор, GPU/CPU trade-offs) — это в `optimization-roadmap.md`.

## §2. Typology

8 классов retrieval techniques по `ai-agent-playbook/concepts/rag-knowledge/11 RAG стратегий — спектр и комбинации.md` и `ai-agent-playbook/concepts/rag-knowledge/Типы RAG - от Naive до Agentic.md`:

| Класс | Что это | Когда помогает | Сложность |
|---|---|---|---|
| **Naive** | chunk → embedding → vector DB → top-K по cosine | Factoid Q&A с known structure | trivial |
| **Advanced** | Naive + rerank (cross-encoder) + query rewriting (HyDE) | Когда Naive recall недостаточен | low |
| **Hybrid** | sparse + dense + fusion (RRF или linear) | Heterogeneous corpora, large scale [Source: internal note — no public source available. Path: ai-agent-playbook/concepts/rag-knowledge/11 RAG стратегий — спектр и комбинации.md] | medium |
| **Contextual** | LLM-обогащение chunks контекстом документа перед индексацией | Long docs с похожими параграфами | medium-high |
| **Graph** | entity/relation graph + spreading activation + temporal edges | Multi-hop, structural memory, customer history | high |
| **Fusion** | linear interpolation / RRF / learned reranker на multi-signal | Multi-signal scoring (semantic + BM25 + entity) | medium |
| **Adaptive** | non-LLM router решает, нужен ли retrieval | Production с many no-retrieval запросами | medium |
| **Agentic** | LM сам решает, какой retriever вызвать, когда остановиться | Multi-source research, неопределённый scope | high |
| **RLM** | REPL-based root LM с sub-LM вызовами | Completeness-critical, code-base, multi-hop | high |

Эти классы не исключают друг друга. Production-система по `ai-agent-playbook/concepts/rag-knowledge/11 RAG стратегий — спектр и комбинации.md` рекомендует базовую комбинацию **Context-Aware Chunking + Hybrid Search + Reranking**, к которой добавляются другие классы по необходимости.

> **Hybrid — not normative by default.** Hybrid (sparse + dense) — strong default candidate for heterogeneous corpora, но должен эмпирически побеждать lexical и dense baselines на target evaluation set до того, как выбирается как default. Hybrid может ухудшать latency, memory footprint, operational complexity и relevance при плохой fusion calibration либо на маленьком однородном корпусе. См. также §3.2 про per-field weights и §6 про fusion timing.

## §3. Sparse Retrieval

Семейство sparse retrievers: explicit термин-веса в inverted index. Общий pipeline: tokenize → normalise → postings → scoring. Различаются механизмом весов.

### 3.1. BM25 (single field)

BM25 — Okapi BM25, канонический baseline retrieval. Вероятностная модель relevance с TF-сатурацией и length normalization. Формула скопирована из `ai-agent-playbook/concepts/rag-knowledge/BM25 и BM25F — формулы и reference implementation.md`:

```text
score_BM25(q, d) = Σ_{t ∈ q} IDF(t) · ( f(t,d) · (k₁ + 1) ) / ( f(t,d) + k₁ · (1 - b + b · |d|/avgdl) )

IDF(t) = log( (N - n(t) + 0.5) / (n(t) + 0.5) + 1 )

где:
- f(t,d)        — term frequency of t in d
- |d|           — document length (number of tokens)
- avgdl         — average document length over collection
- k₁ ≈ 2        — term frequency saturation (parameter)
- b ≈ 0.75      — document length normalization (parameter)
- N             — total documents in collection
- n(t)          — number of documents containing t
```

Интуиция (тот же источник):

- TF-сатурация: `k₁` ограничивает влияние частоты термина (двойное вхождение не даёт двойной вес).
- Length normalization: `b` контролирует, насколько короткие документы предпочтительнее длинных.
- IDF: редкие термины дают больший вес.

Детальная implementation strategy для нашего стека — в `lexical-search-roadmap.md` §BM25 Baseline; проекционный-aware вариант — в `lexical-search-roadmap.md` §Lexical Projections And BM25F.

### 3.2. BM25F (multi-field)

BM25F — multi-field extension BM25 (Pérez-Iglesias et al. 2009; Zaragoza et al. 2009, "The BM25F"). В нашем стеке `SearchProjection` заполняет несколько полей (title/heading/body/code/tag/symbol/meta/qa_q/qa_a/summary), и BM25F применяет per-field weights.

**Канонический BM25F контракт и формула** — в `lexical-search-roadmap.md` §Projection-Weighted BM25F Scoring (там же `LexicalFieldWeights` struct). Наш pipeline не воспроизводит пользовательскую нестандартную BM25F-формулу здесь; см. также Thakur et al. BEIR (arXiv:2104.08663) для бенчмарк-номенклатуры.

Per-field параметры `k_i`, `b_i`, boost определяются в `LexicalFieldWeights`.

Контраст BM25 vs BM25F vs learned sparse приведён в источнике (`ai-agent-playbook/concepts/rag-knowledge/BM25 и BM25F — формулы и reference implementation.md`); точные численные показатели на BEIR сильно зависят от набора данных, версии модели, fine-tuning, retrieval depth и reranker'а — point estimates не приводятся:

| Approach | BEIR nDCG@10 (variability) |
|----------|---------------------------|
| BM25 | Highly variable across datasets, often good baseline |
| SPLADE | Often improves over BM25, particularly on technical text |
| Dense (bi-encoder) | Strong on semantic similarity, weaker on exact term matching |
| ColBERT (late interaction) | Often top-quality but ~100-1000× more expensive than BM25 |

Exact values depend on specific BEIR datasets, model versions, fine-tuning, retrieval depth, and reranking. See Thakur et al. arXiv:2104.08663 and the BEIR leaderboard for ranges, not point estimates.

### 3.3. Learned sparse (SPLADE family)

Learned sparse retrievers сохраняют sparse inverted-index структуру BM25, но заменяют ручные term-weights на learned через BERT MLM head. Канонический представитель — SPLADE (Formal et al., SIGIR 2021).

Архитектурный паттерн SPLADE (из `ai-agent-playbook/concepts/rag-knowledge/Learned sparse retrieval — SPLADE семейство и гибридный sparse+dense.md`):

```text
input_tokens → BERT → per-token contextual embeddings
                                      ↓
importance(t_input, t_vocab) = linear(emb(t_input))^T · embed(t_vocab) + bias
                                      ↓
weight(t_vocab) = log(1 + ReLU( Σ importance по input_tokens ))
                                      ↓
sparse vector в WordPiece vocabulary
```

Ключевые трюки:

- **Log-saturation** вместо чистого ReLU — естественная sparsity, предотвращение доминирования.
- **FLOPS regularization** — явный контроль среднего числа ненулевых dims ≈ retrieval time.
- **End-to-end обучение** — совмещённый loss (ranking + regularization), без двухэтапного pipeline.

Семейство (тот же источник):

| Модель | Год | Особенности |
|---|---|---|
| DeepCT | 2019 | BERT предсказывает BM25-like term weights; без expansion |
| doc2query / docT5query | 2019 / 2020 | seq2seq/T5 генерирует expansion terms для документа |
| SPARTA | 2020 | dot-product между query embedding и document token embeddings |
| uniCOIL | 2021 | impact scores через MLM, только lexical |
| SPLADE v1 | 2021 | log-saturation + FLOPS regularization + end-to-end |
| SPLADE v2 | 2022 | residual compression + FLOPS regularization + distillation |
| SPLADE++ | 2023 | further distillation + fine-tuning recipe |
| **BGE-M3 (sparse branch)** | 2024 | multi-functionality dense+sparse+ColBERT в одной модели |
| **Nomic Embed Text v2** | 2025 | sparse + dense + multi-vector |
| **Qwen3 Embedding (sparse)** | 2026 | sparse branch в decoder-based модели |

Преимущества learned sparse:

- **Exact term matching** — каждый dim = конкретный WordPiece токен; интерпретируемо.
- **Inverted index efficiency** — та же скорость и инфраструктура, что у BM25.
- **Expansion** — vocabulary mismatch решается: модель добавляет релевантные термины.
- **Re-weighting** — важные термины получают больший вес.
- **Interpretability** — можно посмотреть, какие термины добавились.
- **OOD generalization** — strong zero-shot на BEIR (SPLADE v2 — один из SOTA).

### 3.4. BGE-M3 sparse branch

BGE-M3 — multi-functionality embedding (dense + sparse + ColBERT) в одной модели. Sparse branch наследует learned-sparse pattern. Соответствующий конвейер описан в `ai-agent-playbook/resources/rag-knowledge/ЮMoney Юджин корпоративный RAG на FRIDA и Milvus - разбор статьи.md` и `ai-agent-playbook/concepts/rag-knowledge/Decoder-based retrievers — BGE и миграция от encoder.md`.

### 3.5. Nomic Embed v2 sparse

Nomic Embed Text v2 — sparse + dense + multi-vector. Источник `ai-agent-playbook/concepts/rag-knowledge/Learned sparse retrieval — SPLADE семейство и гибридный sparse+dense.md` (см. таблицу семейства).

## §4. Dense Retrieval

Dense retrievers — bi-encoder архитектура с одним вектором на документ (или на токен, см. §5 Late Interaction). Cosine similarity — primary ranking score.

### 4.1. Bi-encoder baseline

Bi-encoder pattern: query и document кодируются независимо (две отдельные encoder-операции или shared encoder с [Q]/[D] маркером). На выходе — single vector `q` и `d`. Score = `cosine(q, d)` или dot product на L2-normalized vectors.

Детальное описание dense layer для нашего стека — в `optimization-roadmap.md`. CRITICAL контракт: `Embedding` остаётся `std::vector<float>`, и ANN-выбор (HNSW/FAISS/DiskANN/ScaNN) — отдельная decision точка, не API surface.

### 4.2. Decoder-based retrievers (BGE, Qwen-Embed, NV-Embed, E5-Mistral)

Decoder-based retrievers — переход от encoder-only (BERT) к decoder-only (LLM-style) архитектурам. Преимущества (из `ai-agent-playbook/concepts/rag-knowledge/Decoder-based retrievers — BGE и миграция от encoder.md`):

- Унаследованный instruction-tuning pattern от LLM.
- Многоязычность из коробки (decoder-семейства обучены на multilingual data).
- SOTA на многих BEIR-доменах.

Примеры:

- **BGE-M3** — multi-functionality (dense + sparse + ColBERT) [Source: arXiv:2402.03216 — BGE M3-Embedding paper (Chen et al., 2024)] <br>[Internal note: ai-agent-playbook/concepts/rag-knowledge/Decoder-based retrievers — BGE и миграция от encoder.md].
- **Qwen3 Embedding** — sparse branch для decoder-based модели (2026).
- **NV-Embed** — NVIDIA decoder-based retriever.
- **E5-Mistral** — encoder-decoder hybrid.

В контексте нашего стека — внешний адаптер; конкретная модель выбирается за пределами C++-core, реализуется через `IEmbedder` контракт.

### 4.3. Hard-negative mining

Hard-negative mining — на этапе обучения bi-encoder учится отличать relevant documents от **похожих, но нерелевантных**. Источник: `ai-agent-playbook/concepts/rag-knowledge/Foundational RAG papers — BEIR, REALM, DPR, RAG.md` — DPR оригинал использует BM25 hard negatives.

Практический приём (DPR + ANCE):

1. Encode corpus.
2. BM25 top-K hard negatives — кандидаты, отобранные BM25 для каждого query.
3. Mine false negatives через ANN с periodically refreshed encoder.
4. Train triplet loss.

У нас в C++-стеке — не реализуется; hard-negative mining — offline training concern. Но качество обученной модели зависит от этого шага.

### 4.4. EOS pooling

Bi-encoder pooling strategy — где взять document-уровневое представление из token embeddings. EOS pooling (использовать embedding `[EOS]`-токена) — стандарт для decoder-based моделей (BGE, E5-Mistral). Альтернативы: mean-pool, [CLS]-pool (encoder-based), max-pool.

EOS pooling победил на BEIR-бенчмарках для decoder-based архитектур [Source: arXiv:2402.03216 — BGE M3-Embedding paper (Chen et al., 2024)] <br>[Internal note: ai-agent-playbook/concepts/rag-knowledge/Decoder-based retrievers — BGE и миграция от encoder.md].

## §5. Late Interaction (ColBERT)

ColBERT — late-interaction архитектура, где matching между query и document идёт на уровне **token-vs-all-tokens**, а не на уровне двух single-векторов.

### 5.1. Архитектура ColBERT

Из `ai-agent-playbook/concepts/rag-knowledge/Late-interaction retrieval — ColBERT архитектура и паттерн.md`:

```text
- Query encoder E_q: query → bag of token-level embeddings {q₁, ..., q_n}.
- Document encoder E_d: document → bag of token-level embeddings {d₁, ..., d_m}.
- Оба encoder обычно делят веса (BERT), отличаются только [Q]/[D] маркером.
- Interaction — после encoding, через дешёвую MaxSum операцию.
```

Ключевая формула (тот же источник):

```text
S(q, d) = Σ_{i=1..|q|} max_{j=1..|d|} (q_i · d_j)
```

после L2-нормализации embeddings (cosine = dot product).

### 5.2. Семейство и реализации

| Модель | Год | Особенности |
|---|---|---|
| **ColBERT** | 2020 | Оригинал. BERT-base, 128-dim, [Q]/[D] markers, query augmentation [mask] |
| **ColBERTv2** | 2022 | Residual compression, декоррелированные embeddings, CEDR-style training |
| **ColBERTv3 / Jina ColBERT** | 2024 | Backbone заменён на современный LM (XLM-RoBERTa, Jina), multi-vector с linear projection |
| **PLAID** | 2022 | Эффективный ANN-индекс специально для ColBERT |
| **RAGatouille** | 2023+ | Python-обёртка от Answer.AI для ColBERTv2 |
| **Vespa native** | 2021+ | Vespa ColBERT integration для production-grade |

Тот же источник упоминает **170× speedup vs cross-encoder, FLOPs 13,900× меньше** в качественном сравнении (cross-encoder ~10-30 sec, ColBERT 60-500 ms). Это основа для trade-off: bi-encoder быстро но слабо, cross-encoder точно но медленно; ColBERT закрывает разрыв.

### 5.3. Storage trade-offs

Из `lexical-search-roadmap.md` §SPLADE-v2 / ColBERT разделов (paraphrasing):

- Bi-encoder: одно vector per document (e.g., 384-1024 dim float32).
- ColBERT: token-level embeddings (~32 токенов × 128 dim float32 для chunk).
- ColBERTv2 compression: residual compression уменьшает footprint в 6-10×.

Storage estimate (illustrative, не из playbook): 1M units × 32 tokens × 128 dim float32 raw ≈ 16 GB; with ColBERTv2 compression ≈ 1.6-2.7 GB. Это много — ColBERT/late-interaction adapters в `agent-memory-cpp` остаются M2+ optional external backend.

### 5.4. Когда выбирать late-interaction

Из `ai-agent-playbook/concepts/rag-knowledge/BM25 и BM25F — формулы и reference implementation.md` trade-off таблицы: ColBERT даёт top-quality на BEIR (см. таблицу в §3.2 для variability) при storage cost high.

У нас в стеке late-interaction — **M2+ optional backend**, не входит в M0/M1 budget. Если нужна late-interaction quality, она ставится через `IAdapter` контракт (`memory-stacks-roadmap.md` §13 Runtime Services).

## §6. Hybrid + PRF Interpolation Timing

После `§3` sparse и `§4` dense возникает вопрос: в какой момент смешивать sparse + dense в retrieval pipeline? Это decision matrix — Pre-PRF / Post-PRF / Both-PRF.

### 6.1. Три схемы интерполяции

Из `ai-agent-playbook/concepts/rag-knowledge/PRF interpolation timing - Pre-PRF vs Post-PRF vs Both-PRF.md`, источник: Li et al. SIGIR 2022 ("To Interpolate or not to Interpolate", arXiv 2205.00235):

```text
Pre-PRF:
    Sparse+dense интерполируются в первом раунде retrieval.
    Затем VPRF берёт top-K кандидатов и обогащает query их векторами.
    Второй раунд retrieval уже на обогащённом query.
    Самый слабый из трёх вариантов по MAP/nDCG@10 в эмпирике.

Post-PRF:
    Оба раунда VPRF работают в dense-only.
    Sparse-interpolation применяется к итоговой выдаче второго раунда.
    Близок к Both-PRF по MAP/nDCG@10, дешевле — рекомендуется по умолчанию для production.

Both-PRF:
    Sparse-interpolation применяется и в первом раунде, и к финальной выдаче.
    Максимальная эффективность на большинстве dense×sparse комбинаций.
    Дороже Post-PRF; разница с Post-PRF статистически незначима (Li et al. 2022).
```

### 6.2. Decision matrix

Из того же источника:

| Цель | Рекомендация |
|---|---|
| Production RAG, дефолт | Post-PRF с BM25 (Recall-ориентирован) |
| Top-10 точность важнее всего, есть бюджет | Both-PRF с uniCOIL (MAP/nDCG) |
| Hybrid для индексации без PRF | No-PRF интерполяция (BM25 + dense), простой RRF или linear |
| Recall критичен (legal, compliance, edge cases) | Post-PRF или Both-PRF с **BM25** (Recall@1000 выше) |
| nDCG@10/MAP критичен (precision-first) | Both-PRF с **uniCOIL** |

### 6.3. Применимость к нашему стеку

> **Real** (есть в code): `HybridRetrievalEngine` (`src/agent_memory/retrieval/HybridRetrievalEngine.hpp`) — orchestrator для fusion. **Proposed API sketch — not implemented.** `HybridRetriever` (без `Engine`) и fusion-strategy names `WeightedMax`/`Learned` — placeholder'ы для будущего API, не зафиксированы в ADR.

RRF (Reciprocal Rank Fusion) — у нас по умолчанию для `HybridRetriever` (см. `knowledge-base-roadmap.md` §7.3). Linear interpolation с α — `WeightedMax` или `Learned` fusion варианты в M2+.

PRF как отдельный шаг в нашем стеке **пока не предусмотрен**. Если потребуется, он подключается через `IQueryTransformer` контракт (`memory-stacks-roadmap.md` §13) — runtime-сервис, обогащающий query через VPRF.

## §7. Contextual Retrieval (Anthropic)

Anthropic-техника обогащения chunks контекстом документа перед индексацией. Из `ai-agent-playbook/concepts/rag-knowledge/Contextual Retrieval — LLM-обогащение чанков контекстом документа.md`:

```text
- 50–100-токеновый LLM-сгенерированный префикс, описывающий место chunk в документе.
- Префикс добавляется в оба индекса — dense-embedding и BM25.
- Anthropic-бенчмарк: 5.7% → 3.7% (Contextual Embeddings) → 2.9% (+ Contextual BM25) → 1.9% (+ Reranking).
- 49% retrieval failure rate reduction (Contextual Embeddings + Contextual BM25).
- 67% reduction с reranker.
- Стоимость: ~$0.50 на 1000 chunks (Claude Haiku 4.5 + prompt caching + batch API).
```

Префикс описывает: тип документа, главную тему, раздел/подтему, конкретные сущности (компания, продукт, версия, период). НЕ пересказывает chunk, НЕ делает выводов.

### 7.1. Pipeline

Из того же источника:

```text
1. Chunking (sliding window 500/100, semantic, structural).
2. Per chunk: LLM-call с cache_control: ephemeral на документе, max_tokens=200.
3. Склейка context + "\n\n" + chunk → embedding и BM25.
4. Original_chunk и context хранятся отдельно от индексируемого текста.
5. На query: hybrid retrieval → RRF → optional reranker → LLM с original_chunk без префикса.
```

### 7.2. Когда применять

Тот же источник:

- Длинные документы с похожими параграфами (финансы, legal, tech specs).
- Большие однородные корпуса (>1K chunks).
- Готовность к reranking.

Когда НЕ применять:

- Корпус <1K chunks — затраты не окупаются.
- Документы уже структурированы (JSON, markdown frontmatter).
- Высоко-динамичные корпуса — reindex постоянный расход.
- Однотипные документы — префикс получается одинаковым.
- Общие запросы без привязки к документу.

### 7.3. Экономика

Из `ai-agent-playbook/concepts/rag-knowledge/Contextual Retrieval — LLM-обогащение чанков контекстом документа.md`:

- Claude Haiku 4.5 для генерации контекстов (дёшево).
- Batch API для разовой индексации корпуса.
- Prompt caching обязателен для документов >50 chunks.
- Итог: ~$200-300 на корпус 10K документов × 50 chunks.

### 7.4. Anti-patterns

5 явных anti-patterns (тот же источник):

1. Малые корпуса (<1K chunks).
2. Структурированные документы (entity уже в полях).
3. Высоко-динамичные корпуса.
4. Однотипные документы.
5. Общие запросы без привязки к документу.

### 7.5. В нашем стеке

Contextual Retrieval не реализовано в M0/M1. Может быть добавлено через `IResourceAdapter` pre-indexing hook:

```text
Resource → read content
       ↓
chunked_resource = chunker.chunk(resource)
       ↓
for each chunk:
    contextualized = llm.augment(chunk, full_document_context)
       ↓
SearchProjection(text=contextualized, original_chunk=chunk, ...)
```

Это **M2+ optional**, реализуемо через `IAsyncIndexer` контракт. Storage: `original_chunk` хранится отдельно от индексируемого текста (как в playbook).

## §8. Adaptive Routing (no-LLM)

EMNLP 2025 paper: «LLM-Independent Adaptive RAG» (Maria Marina et al., 2025). Источник: `ai-agent-playbook/concepts/rag-knowledge/Adaptive RAG — lightweight routing без LLM.md`.

### 8.1. Что предлагается

```text
Решение «нужен ли retrieval» выносится из дорогого слоя LLM в лёгкий классификатор,
обученный на 27 внешних признаках вопроса и сущностей в нём.
```

Стоимость: <1% FLOPs от полной генерации ответа. Сопоставимая точность (`InAcc` ~38.8-38.9 vs 38.4 у Always RAG и 39.3 у Hybrid UE). Сокращение LLM-вызовов до ~1.0 vs 1.7-42.1 у uncertainty-based baseline.

### 8.2. 7 групп признаков

Из того же источника:

| # | Группа | Суть | Инструмент |
|---|---|---|---|
| 1 | Graph features | Связи сущностей в Wikidata (min/max/mean in/out degree) | Wikidata API |
| 2 | Popularity features | Просмотры Wikipedia за год (min/max/mean) | Wikipedia pageviews API |
| 3 | Frequency features | Частота сущностей в корпусе + частота редчайшего n-грамма | Корпусная статистика |
| 4 | Knowledgability | Насколько LLM «знает» сущность (предвычислено) | Verbalized uncertainty (офлайн) |
| 5 | Question type | 9 типов вопроса | bert-base-uncased (Mintaka), accuracy 0.93 |
| 6 | Question complexity | One-hop vs multi-hop | DistilBERT (FreshQA N-hop), F1 0.82 |
| 7 | Context relevance | Релевантность найденных фрагментов | BERT cross-encoder |

Entity linking: BELA, DeepPavlov.

### 8.3. В нашем стеке

Уже есть `ILightweightIntentRouter` контракт (`knowledge-base-roadmap.md` §7.6) — non-LLM классификатор. Развитие в сторону EMNLP-2025 paper включает добавление:

- Popularity / frequency features per corpus.
- Question type classifier.
- Soft-voting ensemble.

Это потенциально M2+ расширение, не в M0/M1 scope.

### 8.4. Чего НЕ делает

Тот же источник предупреждает:

- **«Не нужна LLM» касается только роутера.** Генератор ответа всё равно LLM.
- **Context relevance требует retrieval** для своего вычисления — это меняет экономику.
- **Гибриды с uncertainty-based методами почти не дают синергии** — внешние признаки скорее заменяют, чем дополняют.

## §9. Multimodal RAG

Из `ai-agent-playbook/concepts/rag-knowledge/11 RAG стратегий — спектр и комбинации.md` (производственный конвейер):

VLM (vision-language model) анализирует графики, диаграммы, таблицы напрямую — без OCR, без отдельного числового извлечения. Модель «видит» изображение как человек:

- Вопрос «какая лояльность Ozon в июле 2023?» — VLM смотрит на пузырьковую диаграмму, находит нужный месяц и маркетплейс, выдаёт «65%».
- Вопрос «какой маркетплейс популярнее у женщин?» — VLM считывает подписи на осях, находит пузырь с наибольшей долей женской аудитории.
- Арифметические операции (сумма за 3 года, среднее арифметическое) — модель делает на лету.

Когда применять: документы с визуальной аналитикой (маркетинговые отчёты, финансовые дашборды, научные статьи с графиками), когда OCR + числовое извлечение — это хрупкий конвейер.

Из `ai-agent-playbook/concepts/rag-knowledge/Multimodal RAG - VLM для анализа картинок и графиков.md`:

- Hybrid text+image retrieval.
- VLM понимает графики, не OCR.
- Арифметика — без отдельного калькулятора.

В нашем стеке multimodal RAG — **M2+** extension. Docling-адаптер как `IResourceAdapter` может извлекать и текст, и embedded images; VLM — external model [Source: github.com/docling-project/docling — Docling (MIT)].

## §10. Graph-Based Retrieval

Графовые retrieval-техники полагаются на структурные связи между units / entities / events.

### 10.1. GraphRAG

Из `ai-agent-playbook/concepts/llm-research/Сравнение подходов организации памяти и контекста LLM.md` (секция 4):

```text
- Извлекает entities и relations, строит граф, retrieval по связям и по embedding.
- Лучше для multi-hop reasoning.
- Структурные связи видны явно.
- Лучше temporal reasoning.
- Сложная инфраструктура (Neo4j, Neptune, Kuzu).
- Entity extraction может ошибаться.
- Дороже индексация.
- Не все задачи — графовые.
```

В нашем стеке ADR-006 фиксирует GraphStorage как DBI внутри одного MDBX env (не внешний graph DB). Это позволяет HybridRetrieval смешивать vector + graph без внешней инфраструктуры.

### 10.2. Event-based RAG / Dynamic Graph (DyG-RAG)

Из `ai-agent-playbook/concepts/rag-knowledge/Event-based RAG и динамический граф событий.md`:

```text
- Текст → события с temporal anchors + entities → dynamic graph.
- Retrieval по событиям и связям.
- Лучше для новостей, диалогов, изменяющихся данных.
- Temporal reasoning first-class.
- Multi-hop с временной упорядоченностью.
- Event extraction — нетривиально.
- Dynamic graph требует updates.
```

### 10.3. Spreading Activation

Из `ai-agent-playbook/concepts/ai-agents/Dual-layer memory retrieval LanceDB и spreading activation graph.md`:

```text
- От начального узла активация распространяется по соседним рёбрам с убывающим весом.
- Релевантный подграф «активируется» как контекст.
- Применимо к любому graph-based memory layer.
- Преимущество перед чистым vector search: vector находит семантически близкое, но не учитывает структурные связи.
- Spreading activation находит «что ещё связано с этим» — критично для контекстно-зависимого retrieval.
```

### 10.4. Hard + soft links with decay

`GraphExpansionOptions` в `knowledge-base-roadmap.md` §7.5:

```cpp
struct GraphExpansionOptions {
    uint32_t max_depth = 2;
    uint32_t max_edges = 64;
    uint32_t budget_tokens = 1024;
    std::vector<EdgeKind> allowed_edge_kinds;
    double min_weight = 0.0;
};
```

Hard links: конкретные edge_kinds (allowed_edge_kinds filter). Soft links: weighted (`min_weight` pruning). Decay: через `DecayPolicy` per-edge.

### 10.5. В нашем стеке

`GraphRetriever` через `IUnitRetriever` контракт (`knowledge-base-roadmap.md` §7.2). Bounded BFS expansion с `GraphExpansionOptions`. Determinism: ordering `(edge_weight desc, edge_kind, from_id, to_id)`.

## §11. Agentic vs Deterministic RAG

Когда LLM в retrieval pipeline — оркестратор (autonomous) vs калькулятор (deterministic).

### 11.1. Autonomous (ReAct, LangChain AgentExecutor)

Из `ai-agent-playbook/concepts/rag-knowledge/Deterministic RAG vs autonomous RAG-агенты.md`:

```text
- LLM сама решает, какие инструменты вызвать, в каком порядке, когда остановиться.
- Может делать несколько вызовов подряд (multi-step).
- Может пересматривать решение на основе промежуточных результатов.
- Чёрный ящик: trace сложно восстановить.
- Непредсказуемая стоимость: один вопрос может запускать 5-10 LLM-вызовов.
- Нестабильность поведения при смене версии модели.
- Таймауты при глубокой рекурсии инструментов.
```

Где работает: прототипы, низкая нагрузка, некритичные сценарии, research. Где ломается: production с высокими SLA, регулируемые отрасли, финансовая/медицинская чувствительность.

### 11.2. Deterministic (LangGraph state machines)

Тот же источник:

```text
- Логика шагов в коде (граф нод, рёбра переходов).
- LLM только как вычислитель в конкретных узлах.
- Каждый шаг логируется.
- Решения принимает код (на основе порогов, классификаторов, бизнес-правил).
```

Где работает: production с высокими требованиями к надёжности, регулируемые отрасли, боты с ограниченной областью знаний, системы с жёсткими SLA по стоимости.

### 11.3. APTUS-критика

Из того же источника:

```text
«Загрузить всё в контекст» — антипаттерн: распространённая ошибка — вместо Naive RAG просто
скормить LLM всю базу знаний как один большой system prompt или user message. Это чаще
выглядит как «гибридное расширение Long context», но в production проваливается:
- Линейная стоимость от объёма базы.
- Loss-in-middle — модель помнит начало и конец контекста, середину выбрасывает.
- Задержка пропорциональна размеру контекста.
- Нет контроля над тем, какие фрагменты попали в ответ.
- Деградация качества при росте базы.
```

Корректное решение — для больших баз использовать Naive RAG с `top-K=3-5`, для больших — Advanced RAG с rephraser и Corrective RAG валидатором.

### 11.4. В нашем стеке

`RetrievalPlan` (см. `memory-stacks-roadmap.md` §7.3) реализует deterministic approach: фиксированные retrievers, fixed fusion strategy, fixed decay. LLM вызывается только в `IQueryTransformer` контракте (опционально) и в `IRetrievalEvaluator` контракте (eval-time). Agentic retrieval может быть подключен как отдельный `IAgenticRetriever` (M2+ optional), но не default для production paths.

## §12. RLM (Recursive Language Models)

RLM — root LM в Python REPL, которая пишет код для исследования контекста и вызывает sub-RLM для фрагментов. Из `ai-agent-playbook/concepts/llm-research/Сравнение подходов организации памяти и контекста LLM.md` (секция 11):

```text
- Near-infinite context (10M+ токенов, 1000+ документов).
- Решает все 3 gap RAG: relevance, aggregation, completeness.
- Программный доступ к контексту (regex, math, code).
- Comparable cost к long-context call.
- 100% accuracy на BrowseComp-Plus 1000 docs.
```

Минусы:

- Blocking sub-calls, нет async.
- Непредсказуемая стоимость (LM сама решает, сколько вызовов).
- Медленнее naive RAG (multi-step).
- Требует code-capable модели.

Когда выбирать RLM:

- Multi-hop reasoning.
- Completeness-critical задачи.
- Кодовые базы.
- Длинные документы с O(n)/O(n²) обработкой.

### 12.1. RLM vs RAG failure modes

Из `ai-agent-playbook/concepts/llm-research/Сравнение подходов организации памяти и контекста LLM.md`:

| Failure mode | RAG | RLM |
|---|---|---|
| Relevance gap | Multi-hop, где чанки не похожи на query | Нет — model сама ищет |
| Aggregation gap | Bag of snippets в одном forward pass | Iterative через sub-calls |
| Completeness gap | top-K не гарантирует coverage | Iterative exhaustive processing |
| Cost (simple retrieval) | Cheap | Дороже (multiple LLM calls) |
| Cost (frontier models) | Variable | Comparable to long-context |
| Latency | <1s | Секунды-минуты |
| Infrastructure | Vector DB + embedding model | Python REPL + LLM |

### 12.2. В нашем стеке

> **Proposed API sketch — not implemented.** RLM-integration через `HybridRetriever` — illustrative; в текущем коде RLM не интегрирован.

RLM — **альтернатива**, а не расширение. RLM живёт в Python REPL с sub-RLM вызовами; это другой execution model. Hybrid RAG → RLM (RAG top-100 → RLM deep processing) — потенциальный M2+ extension, если потребуется completeness-critical задача поверх нашего `HybridRetriever`.

Конкретные implementation choices для RLM-стиля — за пределами C++ core, живут в Python worker.

## §13. Implementation Ladder

Маппинг техник на наши maturity levels (M0/M1/M2). См. также `memory-stacks-roadmap.md` §16 (Recommended Implementation Order).

### 13.1. M0 (MVP smoke tests)

Готово из коробки:

- **Naive BM25** (flat body, projection_kind=Original, field_id=body) — `lexical-search-roadmap.md` L1.
- **Bi-encoder dense через ONNX** (опциональный) — если адаптер включён.
- **Hybrid RRF** — `lexical-search-roadmap.md` L1 step 9.
- **IntentRouter** (lightweight classification) — `knowledge-base-roadmap.md` §7.6.

### 13.2. M1 (Production)

Дополнительно:

- **BM25F** (projection-aware, multi-field) — `lexical-search-roadmap.md` L2 steps 10-16.
- **Hybrid + Sparse + Dense fusion** через RRF + per-stack weights — `knowledge-base-roadmap.md` §7.3.
- **Graph retrieval** — bounded BFS expansion, `Knowledge-base-roadmap.md` §7.5.
- **Decay-aware scoring** — exponential decay + log use_boost + cooldown factor — `Knowledge-base-roadmap.md` §7.4.
- **Hybrid lift target** — Recall@10(hybrid) >= 1.20 * Recall@10(BM25-only) — `knowledge-base-roadmap.md` §9.5.
- **Anti-loop cooldown** — UsageStatsComponent.cooldown_until_ms, runtime state, не content-bearing — `policies-roadmap.md`.

### 13.3. M2+ (Advanced, optional)

Дополнительно через `IAdapter` / `IQueryTransformer` / `IAgenticRetriever` контракты:

- **Learned sparse (SPLADE v2, BGE-M3 sparse, Nomic Embed v2 sparse)** — внешний inference backend.
- **Late interaction (ColBERTv2/v3)** — multi-vector per unit, M2+ optional.
- **Contextual Retrieval** — Haiku-class context generator через `AsyncIndexer` pre-indexing hook.
- **Docling multimodal** — `IResourceStore` adapter для PDF/DOCX/PPTX/XLSX/images/audio/HTML [Source: github.com/docling-project/docling — Docling (MIT)].
- **VLM augmentation** — multimodal RAG для графиков, диаграмм, таблиц.
- **Event-based graph** (temporal anchors) — `TBD`, см. open questions.
- **Adaptive routing** с EMNLP-2025 features — soft-voting ensemble.
- **RLM-style hybrid** — RAG top-100 → Python REPL deep processing (если потребуется).
- **ToM sidecar** — optional IPC к Python worker для user-modeling (DLP-bridge pattern).
- **Cross-encoder reranker** через `IRerankerAdapter` — post-fusion rerank.
- **Self-reflective RAG** — Corrective RAG pattern, validate retrieved context quality before LLM.

## §14. Open Questions

1. **Optimal RRF k per stack.** Значение k=60 — common default, но для разных workloads возможно нужны per-stack tuning. Нужны benchmarks на `AgentLTM`, `BasicRag`, `QAKB`.
2. **α для linear interpolation vs RRF.** RRF не требует cross-backend score normalization, но linear interpolation может дать лучший top-K [Source: arXiv:2205.00235 — "To Interpolate or not to Interpolate" (Li et al., SIGIR 2022)] <br>[Internal note: ai-agent-playbook/concepts/rag-knowledge/PRF interpolation timing - Pre-PRF vs Post-PRF vs Both-PRF.md]. Нужны head-to-head benchmarks.
3. **Both-PRF в нашем HybridRetriever.** PRF не реализовано в текущем контракте; Both-PRF даёт 1-3% nDCG@10 vs Post-PRF, но стоимость двойной работы [Source: arXiv:2205.00235 — "To Interpolate or not to Interpolate" (Li et al., SIGIR 2022)] <br>[Internal note: ai-agent-playbook/concepts/rag-knowledge/PRF interpolation timing - Pre-PRF vs Post-PRF vs Both-PRF.md]. Стоит ли реализовывать через `IQueryTransformer`? (см. §1: `HybridRetriever`/`IQueryTransformer` — proposed API sketch).
4. **Anthropic 49% reduction на русском корпусе.** Не воспроизведено [Source: Anthropic blog "Introducing Contextual Retrieval" (2024-09-19, https://www.anthropic.com/news/contextual-retrieval)] <br>[Internal note: ai-agent-playbook/concepts/rag-knowledge/Contextual Retrieval — LLM-обогащение чанков контекстом документа.md]. Наша документация преимущественно русскоязычная — нужен ли пилот?
5. **Anti-loop formula для не-live workloads.** Cooldown+decay по СВИНОПАС разработан для voice-streamed chat [Source: internal note — no public source available. Path: ai-agent-playbook/concepts/rag-knowledge/Внешняя память LLM-агентов — система СВИНОПАС.md]. Какой decay curve оптимален для batch retrieval?
6. **Spreading activation depth & decay-per-hop.** lifemodel не публикует конкретные параметры [Source: internal note — no public source available. Path: ai-agent-playbook/concepts/ai-agents/Dual-layer memory retrieval LanceDB и spreading activation graph.md]. Нужны работы на наших корпусах.
7. **Embedding anisotropy problem.** NOUZ документирует: сырой cosine обманчив из-за анизотропии embeddings [Source: internal note — no public source available. Path: ai-agent-playbook/concepts/NOUZ — структурированная память для ИИ-агентов в Obsidian.md]. Нужна ли calibration layer? Mem0 обходит через multi-signal scoring — возможно, наш `HybridRetriever` автоматически выигрывает.
8. **Bi-temporal validation.** Zep использует bi-temporal; наш `TemporalComponent.valid_from_ms`/`valid_until_ms` одномерный [Source: github.com/getzep/graphiti — Zep Graphiti] <br>[Internal note: ai-agent-playbook/tools/ai-agents/Zep Graphiti — temporal knowledge graph для AI-агентов (документация).md]. Нужно ли расширять для legal/compliance workloads?
9. **Deterministic RAG gate.** Когда добавлять Corrective RAG-валидатор (валидирует retrieved context перед передачей в LLM) — на M1 или M2+?
10. **Multimodal RAG readiness.** Docling — M2+ candidate. Стоит ли включать в M1 как optional adapter?
11. **Learned sparse в M2 backend.** BGE-M3 sparse + dense + ColBERT даёт единый inference pipeline. Это сильный кандидат для production-grade M2+ retrieval.

## §15. References

### 15.1. External (ai-agent-playbook)

**Sparse retrieval:**

- **Robertson et al. (1995, "Okapi at TREC-3"); Pérez-Iglesias et al. (2009); Zaragoza et al. (2009, "The BM25F") — BM25/BM25F canon.** Private provenance: `ai-agent-playbook/concepts/rag-knowledge/BM25 и BM25F — формулы и reference implementation.md` (internal note) — канонические формулы, Robertson & Zaragoza 2009, Pérez-Iglesias Lucene reference.
- **arXiv:2107.05720 — SPLADE paper (Formal et al., SIGIR 2021).** Private provenance: `ai-agent-playbook/concepts/rag-knowledge/Learned sparse retrieval — SPLADE семейство и гибридный sparse+dense.md` (internal note) — SPLADE v1/v2/v2, BGE-M3 sparse branch, Nomic Embed v2 sparse.
- **arXiv:2205.00235 — "To Interpolate or not to Interpolate" (Li et al., SIGIR 2022).** Private provenance: `ai-agent-playbook/concepts/rag-knowledge/PRF interpolation timing - Pre-PRF vs Post-PRF vs Both-PRF.md` (internal note) — SIGIR 2022 Li et al., interpolation + PRF decision matrix.

**Dense retrieval:**

- **arXiv:2104.08663 — BEIR benchmark (Thakur et al., 2021).** Private provenance: `ai-agent-playbook/concepts/rag-knowledge/Foundational RAG papers — BEIR, REALM, DPR, RAG.md` (internal note) — DPR, REALM, BEIR, hard-negative mining.
- **arXiv:2402.03216 — BGE M3-Embedding paper (Chen et al., 2024).** Private provenance: `ai-agent-playbook/concepts/rag-knowledge/Decoder-based retrievers — BGE и миграция от encoder.md` (internal note) — BGE-M3, Qwen-Embed, EOS pooling, multi-functionality.
- `ai-agent-playbook/concepts/rag-knowledge/Vector DB для RAG - выбор и алгоритмы ANN.md` (internal note — no public source available) — ANN algorithms (HNSW/FAISS/DiskANN/ScaNN).
- `ai-agent-playbook/concepts/llm-research/ANN-алгоритмы для векторного поиска — HNSW, FAISS, DiskANN, ScaNN, фильтрация.md` (internal note — no public source available) — детали ANN-выбора.

**Late interaction:**

- **ColBERT paper (Khattab & Zaharia, SIGIR 2020) — ColBERT late interaction.** Private provenance: `ai-agent-playbook/concepts/rag-knowledge/Late-interaction retrieval — ColBERT архитектура и паттерн.md` (internal note) — ColBERT v1/v2/v3, PLAID, RAGatouille, Vespa multi-vector.

**Contextual Retrieval:**

- **Anthropic blog "Introducing Contextual Retrieval" (2024-09-19, https://www.anthropic.com/news/contextual-retrieval).** Private provenance: `ai-agent-playbook/concepts/rag-knowledge/Contextual Retrieval — LLM-обогащение чанков контекстом документа.md` (internal note) — Anthropic техника, 49-67% reduction, $0.50/1000 chunks.
- `ai-agent-playbook/playbooks/rag/Contextual Retrieval — индексация production-RAG с Haiku + prompt caching.md` (internal note — no public source available) — playbook с Haiku + prompt caching.
- `ai-agent-playbook/resources/n8n-automation/Метод улучшения RAG от Anthropic - Контекстуализация в n8n - расшифровка.md` (internal note — no public source available) — реализация через n8n + Qdrant.

**Multimodal RAG:**

- `ai-agent-playbook/concepts/rag-knowledge/Multimodal RAG - VLM для анализа картинок и графиков.md` (internal note — no public source available) — VLM для графиков, диаграмм, таблиц.
- `ai-agent-playbook/concepts/rag-knowledge/11 RAG стратегий — спектр и комбинации.md` (internal note — no public source available) — production-stack по devclubspb с VLM упоминанием.

**Graph retrieval:**

- `ai-agent-playbook/concepts/rag-knowledge/Event-based RAG и динамический граф событий.md` (internal note — no public source available) — DyG-RAG style, temporal anchors.
- `ai-agent-playbook/concepts/ai-agents/Dual-layer memory retrieval LanceDB и spreading activation graph.md` (internal note — no public source available) — spreading activation pattern.
- **arXiv:2502.12110 — A-MEM paper (NeurIPS 2025).** Private provenance: `ai-agent-playbook/resources/llm-research/A-MEM - Agentic Memory for LLM Agents (arXiv 2502.12110) - разбор статьи.md` (internal note) — A-MEM link generation, memory evolution.

**Adaptive routing:**

- **arXiv:2403.14403 — Adaptive-RAG (Jeong et al., 2024).** Private provenance: `ai-agent-playbook/concepts/rag-knowledge/Adaptive RAG — lightweight routing без LLM.md` (internal note) — EMNLP 2025 LLM-Independent Adaptive RAG, 27 features.
- `ai-agent-playbook/concepts/rag-knowledge/Deterministic RAG vs autonomous RAG-агенты.md` (internal note — no public source available) — APTUS critique, deterministic LangGraph.

**RLM:**

- `ai-agent-playbook/concepts/llm-research/Сравнение подходов организации памяти и контекста LLM.md` (internal note — no public source available) — раздел RLM, RLM vs RAG failure modes.
- `ai-agent-playbook/concepts/rag-knowledge/Агентный RAG vs традиционный RAG - эволюция подхода к поиску.md` (internal note — no public source available) — Agentic RAG spectrum.

**Memory frameworks (cross-ref):**

- **github.com/mem0ai/mem0 — Mem0 open-source memory layer.** Private provenance: `ai-agent-playbook/tools/ai-agents/Mem0 — open-source memory layer для LLM-приложений (документация).md` (internal note) — multi-signal scoring (semantic + BM25 + entity).
- **github.com/getzep/graphiti — Zep Graphiti.** Private provenance: `ai-agent-playbook/tools/ai-agents/Zep Graphiti — temporal knowledge graph для AI-агентов (документация).md` (internal note) — bi-temporal architecture.
- `ai-agent-playbook/concepts/rag-knowledge/Внешняя память LLM-агентов — система СВИНОПАС.md` — anti-loop formulas, floating subgraph.

**Misc:**

- `ai-agent-playbook/concepts/rag-knowledge/RAG без эмбеддингов — LLM-классификация страниц вместо similarity search.md` — alternative page classification.

### 15.2. In-house (agent-memory-cpp/guides/)

- [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §13 — Runtime Services, `IQueryTransformer`, `IRetrievalEvaluator` контракты.
- [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §16 — Recommended Implementation Order (M0 → M1 → M2).
- [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) §17 — Open questions.
- [`lexical-search-roadmap.md`](lexical-search-roadmap.md) — BM25F implementation details, projection-aware indexing, Cyrillic morphology.
- [`optimization-roadmap.md`](optimization-roadmap.md) — vector/binary/ANN optimization.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — `HybridRetriever`, RRF fusion, decay-aware scoring, evaluation pipeline.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) §7.5 — `GraphRetriever` + `GraphExpansionOptions`.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) §7.6 — `ILightweightIntentRouter`, `QueryType` enum.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) §9 — Evaluation, golden dataset, hybrid lift target.
- [`policies-roadmap.md`](policies-roadmap.md) — DecayPolicy, cooldown_factor, self_echo.
- [`runtime-services-roadmap.md`](runtime-services-roadmap.md) — PromptCache, AsyncIndexer, WriteGate.
- [`memory-architectures-roadmap.md`](memory-architectures-roadmap.md) — broader memory architectures (Mem0, Zep, A-MEM, RLM), adoption ladder.
- [`research-reading-map.md`](research-reading-map.md) — research references backing this project.
