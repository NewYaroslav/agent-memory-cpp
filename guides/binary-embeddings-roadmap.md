# Binary Embeddings Roadmap

> C++17 compliance: кодовые сниппеты используют `const std::vector<T>&` вместо `std::span` и явные конструкторы вместо designated initializers. Binary signatures (`BinarySignature`, `IBinarySignatureEncoder`, `BinarySignatureEncoderRegistry`, `RandomHyperplaneLSH`, `binary_bucket_index`, `DenseIndexMode::BinaryCandidateFilter`) — это cache-friendly fingerprints для coarse filter (bucket index); binary embeddings — это semantic-preserving quantizers общего назначения. Этот гайд расширяет roadmap binary signatures до общего compression layer; cross-link с [`optimization-roadmap.md`](optimization-roadmap.md) §"Binary Signature Index Tasks" обязателен.

> **Scope note.** All symbols listed in this guide (`BinarySignature`, `BinarySignatureInfo`, `IBinarySignatureEncoder`, `BinarySignatureEncoderRegistry`, `RandomHyperplaneLSH`, `binary_bucket_index`, `DenseIndexMode::BinaryCandidateFilter`, `DenseIndexMode::BinaryOnly`, `DenseIndexMode::ApproximateVector`, `BinaryOnlyIndex`, `ApproximateVectorIndex`, `AutoencoderBinarySignatureEncoder`, `IAutoencoderEncoder`, `IAutoencoderDecoder`, `BinarySignature::hamming_distance`) are roadmap-only. None are implemented in `src/agent_memory/` yet (verified by `git ls-files 'src/agent_memory/**/*.hpp'`). Implementation requires separate PRs; M0 here means "roadmap-only, intended for the first PR lane", NOT "already shipped".

## Source attribution policy

Этот гайд синтезирует материал из нескольких источников. Цитаты следуют двухуровневому паттерну:

- `[Source: <public citation>]` — основная цитата на опубликованную работу, документацию проекта, ревизию репозитория или статью. При цитировании внешнего источника публичная ссылка авторитетна.
- `[Source: internal note — no public source available. Path: <path>]` — утверждение взято только из внутренних исследовательских заметок без эквивалентного публичного источника на момент написания. Требует дополнительной проверки.

Внутренние заметки — это discovery aids, а не авторитетные цитаты. Публичные roadmap-заявления цитируют публичный источник первым; внутренние заметки служат лишь маркером частного происхождения.

Этот гайд покрывает **binary embeddings** как общий compression layer поверх continuous эмбеддингов. Цель — дать набор решений «когда какой метод», включая relationship к roadmap binary signatures (Planned API, not yet implemented), SIMD-accelerated distance (XOR + POPCNT), и quality vs storage tradeoff на 64/128/256/512 bit.

Related roadmaps:

- [`optimization-roadmap.md`](optimization-roadmap.md) — основной документ: binary signature index, encoder registry, dense index modes, SIMD path.
- [`code-intelligence-roadmap.md`](code-intelligence-roadmap.md) — MinHash + LSH (Pattern 1) и RotSQ codec (Pattern 2), заимствованные из `codebase-memory-mcp`.
- [`retrieval-techniques-roadmap.md`](retrieval-techniques-roadmap.md) — typology of retrieval techniques; binary embeddings фильтруют candidate set, retrieval остаётся.
- [`memory-routing-roadmap.md`](memory-routing-roadmap.md) — pre-retrieval routing layer, sits "до" binary embedding filter.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — `HybridRetrievalEngine` контракт, на котором binary embeddings используются как candidate filter.

## §1. Purpose

Этот гайд существует для трёх целей:

1. **Extend binary-signatures roadmap to general compression layer.** Binary signatures (cache-friendly fingerprints для coarse bucket filter) — roadmap-only, not yet implemented. Binary embeddings — semantic-preserving quantizers общего назначения, расширяющие signature-only подход до storage tier (binary + optional decoder) и hybrid modes (binary + dense rerank).
2. **Comparison framework.** Свести в одну карту методы binarization (sign-based, autoencoder, LSH, PQ), по единому набору осей: compression ratio, accuracy retention, training cost, inference cost, hardware-friendliness.
3. **Adoption guidance.** Для каждого метода пометить, что из него `agent-memory-cpp` может воспроизвести через наши planned primitives (`BinarySignature`, `BinarySignatureEncoderRegistry`, `IBinarySignatureEncoder`, `DenseIndexMode`) и что требует нового контракта.

Non-goals:

- Не повторять детальную спецификацию `BinarySignatureEncoderRegistry` (это в `optimization-roadmap.md` §"Encoder Registry и Versioning"). `BinarySignatureEncoderRegistry` сам по себе roadmap-only.
- Не описывать float32 / float16 / int8 codecs (это в `optimization-roadmap.md` §"Future Encodings").
- Не каталогизировать ANN-методы (HNSW, IVF, DiskANN — это в `optimization-roadmap.md` §"Dense Index Modes").

Binary embeddings — **не замена** full vectors. Это tradeoff между accuracy и storage/query cost. Quality-sensitive rerank остаётся обязательным; binary embeddings могут быть **only candidate filter** (planned `DenseIndexMode::BinaryCandidateFilter`) или **primary** storage tier (experimental modes `BinaryOnly`, `ApproximateVector`).

## §2. Binarization Landscape

| Метод | Размер на вектор | Compression vs float32 | Качество (illustrative hypothesis) | Когда применять |
|---|---|---|---|---|
| **Float32 (baseline)** | d × 4 bytes | 1× | 100% | Ground truth, quality-sensitive rerank |
| **Float16** | d × 2 bytes | 2× | ~99.5% | GPU inference, mixed precision |
| **INT8 scalar quantization** | d × 1 byte | 4× | ~98% | pgvector, Weaviate default |
| **Matryoshka truncation (MRL)** | N × 4 bytes (N < d) | d/N× | varies | Adaptive serving, two-stage retrieval |
| **Product Quantization (PQ)** | m bytes (typ. 8) | 96× (768-dim) | ~95% | FAISS, Milvus, billion-scale ANN |
| **Binary (1 bit/dim)** | d / 8 bytes | 32× (768-dim) | ~85% | Coarse filter, edge, in-memory cache |
| **Binary learned (Tissier 2018)** | 8-64 B per vector (see table below) | 300d: 18.75-150×; 768d: 48-384× (see table below) | ~98% (256 bit) | Coarse-to-fine retrieval, dense storage tier |
| **LSH (random hyperplanes)** | 8-64 B per vector (see table below) | 300d: 18.75-150×; 768d: 48-384× (see table below) | ~85% (256 bit) | Cache-friendly candidate filter (planned; `RandomHyperplaneLSH` not yet implemented) |
| **RotSQ** | ~6 bytes/dim + 12 B metadata | ~6× | ~95% | Sibling codec alongside Matryoshka, PQ |

> All values above are **illustrative starting hypotheses, not validated cross-codec quality targets**. Recall@10 numbers depend on:
> - embedding model (BERT vs E5 vs BGE behave differently under quantization);
> - corpus (technical text, conversational, structured vs unstructured);
> - retrieval metric (Recall@10 vs nDCG@10 vs MRR);
> - quantization training data (or lack thereof).
>
> Tissier et al. 2018 reported near-float results for some 256–512-bit word-embedding tasks; **transfer to sentence-transformer retrieval is unvalidated**. Do NOT quote these numbers as production acceptance contracts.

Binary embeddings занимают **наименьший размер на вектор** при сохранении семантики. Compression ratio strongly depends on dimensionality; см. per-dimension table ниже. Quality retention при увеличении bit budget — illustrative hypothesis, см. caveats.

Compression ratios for `Binary learned` and `LSH` не универсальны — они зависят от embedding dimensionality:

| Dimensionality | float32 size | Compression at 64 bits | Compression at 512 bits |
|---------------|--------------|------------------------|--------------------------|
| 300-dim (e.g., GloVe) | 1,200 B  | ~150× (≈8 B vs 1,200 B) | ~18.75× (≈64 B vs 1,200 B) |
| 768-dim (e.g., BERT/E5/BGE) | 3,072 B | ~384× (≈8 B vs 3,072 B) | ~48×  (≈64 B vs 3,072 B) |

Note: actual ratios depend on dimension. Numbers above are upper and lower bounds; intermediate bit counts (128, 256 bits) yield intermediate ratios.

> Note: compression ratio for a `bit_count`-bit code on a `dim`-dimensional float32 embedding:
> 
> ```text
> compression_ratio = (dim × 4) / (bit_count / 8)
> 
> Example for 768d float32 with 256-bit code:
>   compression_ratio = (768 × 4) / (256 / 8) = 3072 / 32 = 96×
> ```
> 
> The code is a single `bit_count`-bit value stored once per vector, NOT `bit_count / dim` bits per dimension.

## §3. Binarization Methods

### 3.1. Threshold-Based Sign of Embedding

```text
b_i = H(x_i)
```

`H` — Heaviside step function. Naive approach: один бит на компоненту embedding'а.

- **Pros:** zero training, dependency-free, deterministic.
- **Cons:** keeps same dimension; no CPU-register alignment; loses magnitude info (only sign); NOT invertible without a learned decoder or external magnitude metadata.
- **Use case:** experimental baseline; production-grade needs learned quantization.

> Zero-training and deterministic: `b_i = sign(x_i)` is one bit per component, requiring no model. Preserves only the sign of each component; the magnitude and ordering among positive values are NOT recoverable. Not invertible without a learned decoder or external magnitude metadata.

[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

### 3.2. Autoencoder-Binarization (Tissier 2018)

The Tissier et al. 2018 approach uses a learned autoencoder with weight tying:

```text
Encoder:  b = H(W · x),   W ∈ R^{m × d}
Decoder:  x_hat = tanh(W^T · b + c)
Loss:     L = MSE(x, x_hat) + lambda · Σ_{i<j} corr(b_i, b_j)^2
```

Weight tying is forced: `H` is non-differentiable, so the encoder does not receive a direct gradient. The gradient flows only through the decoder; `W` is updated by the decoder signal.

> **Gradient flow (tied weights):** With weight tying `W = W_decoder^T`, encoder weights `W` are updated indirectly through the decoder path during training. The binary encoder `H(Wx)` is non-differentiable; gradient does NOT pass through `H`. This is NOT a straight-through estimator (STE) — STE would require an artificial non-zero derivative at `H` during backward pass, which is not used here. The autoencoder instead learns useful `W` representations through decoder-side updates.

**Decorrelation regulariser** penalises pairwise correlations between code bits. Without it, the autoencoder learns to reconstruct numbers well (decoder MSE), but poorly preserves semantics — the encoder gradient is absent, and `W` discards semantic information in favour of numerical accuracy. The regulariser forces code bits to carry independent semantic information.

[Source: arXiv:1803.09065 — Tissier, Gravier, Habrard 2018] <br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

> **Reference implementation note.** Code (MIT-licensed) is available at <https://github.com/tca19/near-lossless-binarization>. The reference is a small CLI/training pipeline, not a reusable SDK — port the encoder/decoder classes to our dependency-free contract; do not copy verbatim.

Results on word-level embeddings (2018, GloVe 300-dim):

| Метрика | 64-bit | 128-bit | 256-bit | 512-bit | Float baseline |
|---|---|---|---|---|---|
| Spearman на WS353 (GloVe) | 30.1 | 44.9 | 56.6 | 60.3 | 60.9 |
| Accuracy на AG-News (dict2vec) | 85.3 | 85.9 | 87.7 | 87.8 | 89.0 |
| Top-10 query latency (ms, GloVe) | 2.72 | 2.89 | 3.25 | 4.29 | 98.08 |
| Load + Top-10 latency (ms) | 160 | 213 | 310 | 500 | 23 500 |

Key empirical findings:

- 256-bit: ~2% loss on semantic similarity and classification, 30× retrieval speedup.
- 64-bit: stronger compression (150×), but up to 16% loss on large datasets.
- On GloVe (400k words), 512-bit code approaches float baseline on average semantic correlation.

**Regularising effect.** Paradoxically, binary code sometimes **exceeds** float embeddings on downstream tasks. 512-bit fasttext on WS353 yields 70.3 vs 69.7 for float; 256-bit GloVe on SimVerb yields 22.9 vs 22.7. Authors interpret this as regularisation: noisy, weakly-informative dimensions of float vectors are discarded during 1-bit quantisation, leaving a "cleaner" representation.

### 3.3. Locality-Sensitive Hashing (LSH)

LSH for binarization uses random hyperplanes (planned: `RandomHyperplaneLSH` encoder, **not yet implemented**):

```text
bit_i = sign(dot(embedding, random_hyperplane_i))
```

- **Pros:** zero training; deterministic by `(dim, bit_count, seed)`; planned as the baseline encoder in the roadmap.
- **Cons:** no semantic preservation beyond the random hyperplane approximation; ~85-90% Recall@10 of float baseline at 128 bits (hypothesis, not contract).
- **Use case:** cache-friendly candidate filter; bucket prefiltering.

[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

This is the **same approach** as the planned baseline encoder in the binary-signatures roadmap; cross-link to `optimization-roadmap.md` §"Baseline Encoder" and §"Binary Signature Index Tasks". This guide treats LSH as the first member of the binarization family (zero-training baseline); autoencoder-binarization is the learned upgrade.

### 3.4. Product Quantization (PQ) for Hybrid Binary + FP

PQ decomposes the embedding into sub-vectors and quantises each independently via k-means:

```text
1. Split x ∈ R^d into m sub-vectors of length d/m each.
2. K-means cluster each sub-vector on training data → dictionary of k centroids (typ. k=256, log2(k)=8 bits per sub-vector).
3. Replace each sub-vector with the index of its nearest centroid.
4. Storage: m bytes per vector (k=256).
```

Compression ratios (Joulin et al. 2016):

| Размерность | float32 | PQ (m=8) | Коэффициент |
|---|---|---|---|
| d=300 | 1200 B | 8 B | 150× |
| d=100 | 400 B | 8 B | 50× |
| d=300, m=4 | 1200 B | 4 B | 300× |

Asymmetric ADC (approximate distance computation) via lookup tables.

[Source: arXiv:1612.03651 — Joulin, Grave, Bojanowski, Mikolov 2016, FastText.zip] <br>[Source: arXiv:1702.08734 — Jégou et al., FAISS] <br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

PQ is **not strictly binary** — it produces byte codes, not bit codes. But it composes naturally with binary embeddings: the same dense index can mix a binary candidate filter (XOR + POPCNT) with a PQ byte code (ADC distance) for finer rerank. This is the **hybrid binary + FP** mode referenced in §"Implementation Ladder" below.

## §4. SIMD-Accelerated Distance

The killer feature of binary embeddings is **SIMD-accelerated distance via XOR + POPCNT**. Hamming distance between two binarized vectors reduces to:

```text
hamming_distance(b1, b2) = popcount(b1 XOR b2)
```

This is **cheap per operation** on modern x86 (POPCNT instructions on `uint64_t` words), but the wall-clock cost is NOT "one instruction per vector". Hamming distance between two 256-bit signatures decomposes into `4 × XOR64` + `4 × POPCNT64` + integer adds, plus loads, loop overhead, and top-K maintenance. AVX2 does NOT have native vector popcount (`_mm256_popcnt_u64` does not exist); a vectorized implementation typically uses a 16-bit nibble LUT + PSHUFB lookup (Bradford-Larson or similar). Wall-clock latency for 1M vectors depends on CPU, memory bandwidth, cache hit rate, and the top-K algorithm; do not quote absolute numbers without a benchmark setup. Compare with cosine similarity, which requires multiplications and additions per dimension.

### 4.1. CPU Register Alignment

Bit sizes, register widths, and cache-line occupancy are **independent properties**. A 256-bit signature fits comfortably in one AVX2 register (32 bytes) but spans only **half** a 64-byte cache line. The table below separates the two:

| Bit size | Bytes | AVX2 register (32 B) fit | Cache line (64 B) occupancy | Use case |
|---|---|---|---|---|
| 32 bit | 4 B | one eighth | one sixteenth | experimental, super-fast coarse filter (NOT default) |
| 64 bit | 8 B | one quarter | one eighth | BasicRag default, minimum practical |
| 128 bit | 16 B | one half | one quarter | AgentLTM default, normal candidate filter |
| 256 bit | 32 B | full register | one half | CompiledWiki, quality-oriented |
| 512 bit | 64 B | spans 2 registers | full cache line | M2+ experimental, near-float quality |

Rationale: 64/128/256 bits align with CPU register sizes (`uint64_t`, SSE, AVX2) on the register side; on the cache-line side, the same bit counts occupy 1/8, 1/4, 1/2 of a 64-byte cache line respectively. 32-bit is not alignment-friendly for popcount/Hamming through `__builtin_popcount`. A 512-bit signature occupies a full cache line, which is desirable for sequential scans and SIMD loads but increases cache pressure when many signatures are processed together.

### 4.2. SIMD Dispatch

| ISA | Path | Speedup vs scalar | Notes |
|---|---|---|---|
| Scalar | `popcount64` intrinsic | 1× (baseline) | Cross-platform wrapper |
| SSE4.2 | `_mm_popcnt_u64` (64-bit only) | 2× per register | Available on most x86_64 |
| AVX2 | `_mm256_xor_si256` + nibble LUT (`_mm256_shuffle_epi8` + `_mm256_sad_epu8`) | 2-4× | `_mm256_popcnt_u64` does NOT exist (AVX-512 only) |
| AVX-512 + VPOPCNTDQ | `_mm512_xor_si512` + `_mm512_popcnt_epi64` | 8-16× | Skylake-X+ required |
| NEON | ARM popcount | varies | cross-platform via runtime detection |

[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

The `popcount64` wrapper:

```cpp
namespace simd {

inline uint32_t popcount64(uint64_t x) {
#if defined(_MSC_VER)
    return static_cast<uint32_t>(__popcnt64(x));
#elif defined(__GNUC__) || defined(__clang__)
    return static_cast<uint32_t>(__builtin_popcountll(x));
#else
    return fallback_popcount64_sw_ar(x);
#endif
}

uint32_t hamming_distance_u64(
    const uint64_t* a,
    const uint64_t* b,
    size_t word_count);

}  // namespace simd
```

This is the same SIMD layer defined in [`optimization-roadmap.md`](optimization-roadmap.md) §"Eigen и SIMD стратегия". Binary embeddings are planned to reuse it via `BinarySignature::hamming_distance(lhs, rhs)` (Planned API, **not yet implemented**) and the `HammingTopK` kernel (see `optimization-roadmap.md` §"HammingTopK kernel" for the full design).

## §5. Relationship to the Binary-Signatures Roadmap

Planned binary signatures vs planned binary embeddings — two sides of the same coin (all symbols roadmap-only):

| Aspect | Planned Binary Signatures | Planned Binary Embeddings |
|---|---|---|
| **Purpose** | Cache-friendly fingerprints (bucket prefilter) | Semantic-preserving quantizers (general compression) |
| **Storage** | `(scope_id, projection_kind, short_key)` bucket index | Optional embedding storage tier (binary only / binary + decoder) |
| **Encoder** | `RandomHyperplaneLSH` baseline + `AutoencoderBinarizer` M2+ | Multiple encoders (sign, LSH, autoencoder, PQ, RotSQ) |
| **Reconstruction** | None (fingerprint only) | Optional decoder for `ApproximateVector` mode |
| **Quality hypothesis** | Coarse filter: Recall@10 ≈ 0.95 (binary + float rerank, hypothesis) | Standalone: Recall@10 ≈ 0.95 (256+ bits, hypothesis); binary-only: ≈ 0.85 (hypothesis) |
| **Use in `DenseIndexMode`** | `BinaryCandidateFilter` (planned default) | `BinaryCandidateFilter` / `BinaryOnly` / `ApproximateVector` |

The binary-signatures roadmap is **the first slice** of the binary embeddings roadmap. It plans to establish the encoder registry, signature value types, and bucket index. Binary embeddings roadmap **extends** that into:

- Standalone binary embeddings as a storage tier (drop float, keep binary).
- Hybrid binary + dense indexes (binary for candidate filter + dense rerank).
- Multiple encoder backends (sign-based, LSH, autoencoder, future RotSQ).
- Optional decoder for approximate vector reconstruction.
- Per-bit-size quality vs storage tradeoff tables.

Cross-link to [`optimization-roadmap.md`](optimization-roadmap.md) §"Dense Index Modes (Backend Selection)" and §"Binary Signature Index Tasks" is mandatory.

## §6. Quality vs Storage Tradeoff

Per-bit-size quality targets for binary embeddings. **The numbers below are starting hypotheses for benchmarking, not acceptance contracts.** Full quality targets across `DenseIndexMode` × encoder × bit_size live in [`optimization-roadmap.md`](optimization-roadmap.md) §"Quality Targets Per Mode" and §"Per-Bit-Size Recall Targets"; that file carries the same hypothesis caveat.

> **Hypothesis, not contract.** The numbers above are starting hypotheses for benchmarking, derived from Tissier et al. 2018 results on word-level GloVe embeddings. They MUST be re-validated separately for every target embedding model (BERT, E5, BGE) and dataset. Failure to reach these numbers does NOT indicate an implementation bug — it indicates the hypothesis needs recalibration. Recall@10 of modern sentence-transformer embeddings CANNOT be derived from Spearman correlation on WS353 or AG-News classification accuracy from 2018.

```text
autoencoder_binarizer_128bit (hypothesis):
  Recall@10(binary_only) ≈ 0.95 x Recall@10(float_baseline)    # Tissier-style extrapolation
  candidate_count: ~1.5x float top-K при Recall@10 = 0.95      # rough heuristic

autoencoder_binarizer_64bit (hypothesis):
  Recall@10(binary_only) ≈ 0.90 x Recall@10(float_baseline)

random_hyperplane_lsh_64bit (baseline, hypothesis):
  Recall@10(binary_only) ≈ 0.85 x Recall@10(float_baseline)

random_hyperplane_lsh_128bit (baseline, hypothesis):
  Recall@10(binary_only) ≈ 0.90 x Recall@10(float_baseline)

Tissier 2018 results (word-level GloVe 300-dim, 2018) — observation only:
  64-bit:  ~50% of float quality on WS353 Spearman
  128-bit: ~74% of float quality
  256-bit: ~93% of float quality, 30x retrieval speedup
  512-bit: ~99% of float quality, 24x retrieval speedup
```

[Source: arXiv:1803.09065 — Tissier, Gravier, Habrard 2018] <br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

Storage estimates per 1M units × 768-dim (hypotheses; expected compression ratios, not validated quality targets):

| Encoding | Storage | Compression vs float | Quality hypothesis |
|---|---|---|---|
| Float32 (baseline) | ~3.0 GB | 1× | 1.00 (ground truth) |
| Float16 | ~1.5 GB | 2× | ~0.99 |
| INT8 | ~768 MB | 4× | ~0.98 |
| Binary 64-bit (RH-LSH) | ~8 MB | ~384× | ~0.85 |
| Binary 128-bit (RH-LSH) | ~16 MB | ~192× | ~0.90 |
| Binary 256-bit (autoencoder) | ~32 MB | ~96× | ~0.95 |
| Binary 512-bit (autoencoder) | ~64 MB | ~48× | ~0.97 |
| Binary + decoder (AE-128, Compact) | ~16 MB + ~384 KiB decoder | ~192× | ~0.93 |
| Binary + decoder (AE-128, Safe) | ~3 GB + ~384 KiB decoder | ~1× | ~0.97 |
| Binary + decoder (AE-256, Compact) | ~32 MB + ~768 KiB decoder | ~96× | ~0.95 |
| PQ (m=8, k=256) | ~8 MB | ~384× | ~0.95 |

Decoder matrix shared across stack (one per encoder registry, roadmap): ~384 KiB (`W_decoder` float32, 128-bit × 768-dim) + ~3 KiB `b_decoder` ≈ ~387 KiB per encoder registry.

## §7. Compatibility with `optimization-roadmap.md`

Binary embeddings slot into the planned `optimization-roadmap.md` binary-signatures bucket and the broader quantizer section.

### 7.1. Planned Bucket

Planned binary-signatures bucket (roadmap, **not yet implemented**): `binary_bucket_index` keyed by `(scope_id, projection_kind, short_key)`, posting lists of `BinaryBucketPosting { unit_id, full_signature, unit_revision, resource_generation }`. Planned encoder registry: `BinarySignatureEncoderRegistry` with `.bse` file format.

### 7.2. Quantizer Section

`optimization-roadmap.md` §"Future Encodings" already defines the `VectorEncoding` enum (`Float32`, `Float16`, `Int8`, `BinarySignature`, `ProductQuantized`). Binary embeddings **extend** the `BinarySignature` entry with:

- `DenseIndexMode::BinaryOnly` — standalone binary (no float rerank).
- `DenseIndexMode::ApproximateVector` — binary + decoder → approximate float → rerank.

### 7.3. DenseIndexConfig Extensions

> **Proposed API sketch — not implemented.** The `BinaryOnlyConfig` and `ApproximateVectorConfig` additions below are illustrative — no ADR has defined them. Existing `DenseIndexConfig` lives in `memory-stacks-roadmap.md`; the binary embedding extensions below would slot alongside it.

> **Proposed API sketch — not implemented.**

```cpp
struct DenseIndexConfig {
    DenseIndexMode mode = DenseIndexMode::Exact;

    // Binary modes (BinaryCandidateFilter / BinaryOnly / ApproximateVector):
    uint32_t bit_count = 128;
    std::string encoder_id = "random_hyperplane_lsh_v1";

    // ApproximateVector mode only:
    bool store_decoder = true;
    bool store_float_fallback = false;

    // BinaryOnly mode only:
    bool drop_float_after_migration = false;

    // Hnsw mode only:
    std::optional<HnswConfig> hnsw_config;
};
```

`store_float_fallback = true` corresponds to `ApproximateVector (Safe)` mode (binary + float + decoder); `store_float_fallback = false` corresponds to `ApproximateVector (Compact)` (binary + decoder only).

## §8. Implementation Ladder

### M0: Binary Signatures Only (roadmap-only, not yet implemented)

> **Planned API — not yet implemented.** Every symbol in the M0 list below is a roadmap item, not a shipped feature. None of `BinarySignature`, `BinarySignatureInfo`, `IBinarySignatureEncoder`, `BinarySignatureEncoderRegistry`, `RandomHyperplaneLSH`, `binary_bucket_index`, or `DenseIndexMode::BinaryCandidateFilter` are in `src/agent_memory/` (verified by `git ls-files 'src/agent_memory/**/*.hpp'`). Each requires its own PR; M0 here denotes the **first** PR lane, not a shipped milestone.

M0 (planned scope):

- `BinarySignature` value type with `std::vector<std::uint64_t> words` (Planned API).
- `BinarySignatureInfo` with encoder id, model id, source projection kind (Planned API).
- `IBinarySignatureEncoder` interface (Planned API).
- `BinarySignatureEncoderRegistry` with `.bse` file format (Planned API).
- `RandomHyperplaneLSH` baseline encoder (Planned API).
- `binary_bucket_index` MDBX layout (Planned API).
- `DenseIndexMode::BinaryCandidateFilter` default candidate-filter mode (Planned API).

This guide treats M0 as the starting point of the roadmap.

### M1: Binary Embeddings Standalone

> **Planned API — not yet implemented.** M1 features below are roadmap-level intentions, not in any ADR. The `BinaryOnlyIndex`, `ApproximateVectorIndex`, `AutoencoderBinarySignatureEncoder` are referenced in `optimization-roadmap.md` §"Dense Index Modes (Backend Selection)" but not yet implemented.

> **Planned API — not yet implemented.**

- `DenseIndexMode::BinaryOnly` (`BinaryOnlyIndex`) — Hamming bucket → Hamming rank, no float rerank. Float embeddings can be dropped after confirmation; storage savings ~190× on 1M units.
- `DenseIndexMode::ApproximateVector` (`ApproximateVectorIndex`) — Hamming bucket → decoder → approximate float → cosine rerank. Two variants: Safe (binary + float + decoder) vs Compact (binary + decoder only).
- `IAutoencoderEncoder` interface (dependency-free contract).
- `AutoencoderBinarySignatureEncoder` implementation (matmul + sign). Eigen adapter behind `AGENT_MEMORY_ENABLE_EIGEN` (default OFF).
- `IAutoencoderDecoder` interface (optional, for `ApproximateVector` mode).
- `.bse` file format extension: optional `weights_WT_decoder` matrix.
- Migration flow (write-new → read-both → bg-reindex → delete-old), as in `optimization-roadmap.md` §"Multi-encoder migration flow".

### M2: Hybrid Binary + Dense Indexes

- `DenseIndexMode::Hnsw` (`HnswVectorIndex`) — mainline M2+ backend, optionally combined with `BinaryCandidateFilter` for hybrid.
- Hybrid retrieval: binary filter produces candidate set (Recall@10 ≈ 0.95 — hypothesis, see §6), HNSW ranks within candidate set, dense rerank on top.
- Adaptive per-query mode selection via `RetrievalPlan::dense_index_mode_override`.
- Multi-encoder hybrid: binary embeddings + PQ codes for finer rerank (asymmetric ADC distance).

## §9. Open Questions

1. **Calibration of binary quality targets.** The Tissier 2018 results are on word-level GloVe; transfer to sentence-transformer embeddings (BERT, E5, BGE, M3-Embedding) is not validated. Same open question as `memory-routing-roadmap.md` §11: does the autoencoder objective generalise from 300-dim GloVe to 768-1024 dim modern embeddings?
2. **Comparison with ANN methods (HNSW, IVF, ScaNN).** On corpora >10M vectors, does binary embedding remain competitive, or does HNSW dominate? Benchmark-driven choice is required.
3. **Multi-bit quantization (2-4 bits/dim).** Active research direction since 2020. Promises intermediate trade-off between binary (1 bit) and float8 (8 bits). Integration with the planned binary-signatures encoder registry: new `BinarySignatureEncoderId` value, new `.bse` encoder format, new bit-count handling in `BinarySignature`.
4. **Multilingual and code-model embeddings.** Structure of semantic space differs from GloVe/fasttext. Does Tissier 2018's `MSE + decorrelation penalty` objective still work for multilingual and code embeddings?
5. **LLM-based hashing.** Frozen LLM + hash head vs joint fine-tuning. Trade-off: training cost vs code quality. No public benchmark comparing both approaches on retrieval metrics (mAP, NDCG).
6. **Adversarial robustness.** Small text edit (paraphrase, synonym swap) can radically change binary code. How robust is binary coarse-filter for production RAG with paraphrased queries?
7. **PQ on LLM hidden states.** Joulin 2016 PQ for text classification is on Word2Vec; does it transfer to 4096-dim LLM hidden states with residual compensation?
8. **Adaptive dimension selection per query.** Can we dynamically select embedding dimension by query complexity, so simple queries don't pay for full D dimensions?
9. **MRL + binary composition.** 2048-dim embedding → MRL-truncate to 256 → binarize to 256 bits → 16× additional compression. Does this nested compression retain usable semantic quality?
10. **Decoder training cost.** Autoencoder training requires labelled corpus (10k-1M embeddings). What is the minimum viable training set size for our specific AgentLTM workload? Workload-specific, requires empirical measurement.

## §10. Composite compression (MRL + PQ / INT8 / binary)

See [`compression-is-intelligence-roadmap.md`](compression-is-intelligence-roadmap.md) for the conceptual framing (cross-entropy, prefix-free codes, "what good compression preserves") and the §10 Composite compression table for MRL+INT8 / MRL+PQ / MRL+binary multipliers.

Одна техника сжатия (MRL truncate, INT8, PQ, binary) обычно **не** достигает жёстких storage / latency targets в одиночку. Memory savings are configuration-dependent; the examples in this section range from 12× to 384× depending on embedding dimension, code width, and composition. Этот раздел систематизирует совместимость и реалистичные multipliers.

[Source: arXiv:2205.13147 — Kusupati et al. 2022, Matryoshka Representation Learning]
<br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Адаптивные embeddings - variable-length representations и Matryoshka.md]

### §10.1. Why composite compression

- **MRL truncate** уменьшает размерность, но каждое измерение остаётся float32 → по умолчанию 3× compression для 768→256.
- **INT8 scalar quantization** уменьшает bytes-per-dim (4 → 1) без зависимости от размерности → 4× compression.
- **PQ (m=8, K=256)** заменяет sub-vector на 1 байт индекс кластера → ~96× на 768-dim, ~12× на 256-dim (per-segment basis).
- **Binary binarisation** (Tissier 2018 autoencoder или RH-LSH) — 1 bit per dim или фиксированный bit_count → 32× для 768-dim, 8× для 256-dim.

Если storage budget жёстче, чем даёт одна техника, композиция stack'ов даёт multiplicative effect.

### §10.2. Composite multiplier table

Baseline — 768-dim float32 embedding (3,072 bytes per vector).

| Stage | Output size per vector | Stage multiplier | Cumulative |
|---|---|---|---|
| float32 baseline (768-dim) | 3,072 B | 1× | 1× |
| MRL truncate 768→256 | 1,024 B | 3× | 3× |
| MRL 256 + INT8 | 256 B | 4× vs float32, 12× cumulative | 12× |
| MRL 256 + PQ m=8 | 8 B | 128× vs 256-float, 384× vs 768-float baseline | 384× |
| MRL 256 + binary (256-bit) | 32 B | 32× vs float32 | 96× |

Примеры комбинаций (rough — для оценки storage budget):

- **MRL 256 + INT8** ≈ 12× compression (3× truncation × 4× INT8). Quality loss minimal; default для moderate-budget workloads.
- **MRL 256 + PQ m=8** ≈ 384× compression (3× truncation × 128× PQ per 256-dim segment basis, 1024 B → 8 B). Quality loss noticeable, требует calibration set.
- **MRL 256 + binary** ≈ 96× compression (3× truncation × 32× binarisation). Самый агрессивный; biggest quality risk; используется как candidate filter, не standalone retrieval.

> **All multipliers above are illustrative starting hypotheses, not validated cross-codec quality targets.** Actual ratios depend on:
> - embedding model (BERT vs E5 vs BGE behave differently under quantisation);
> - corpus (technical text, conversational, structured vs unstructured);
> - retrieval metric (Recall@10 vs nDCG@10 vs MRR);
> - quantisation training data (or lack thereof).
>
> Эти числа полезны для sizing storage budget, не как acceptance contracts.

### §10.3. Compatibility matrix

Что stack'уется с чем, и где нужна осторожность:

| Combination | Status | Notes |
|---|---|---|
| MRL + INT8 | clearly composable | MRL truncation is bit-independent of INT8; INT8 quantises each retained dimension. |
| MRL + PQ | clearly composable | Standard pipeline: MRL truncate to 256, then PQ m=8 on 256-dim sub-vectors; K-means clusters per segment. Requires K-means calibration set on MRL-truncated embeddings. |
| MRL + separately-trained binary encoder | clearly composable | Tissier 2018-style: MRL truncate, then learn a binary encoder on the MRL-truncated embedding. См. §10.4 — open question о transfer to sentence-transformers. |
| PQ + quantized centroids / residuals | requires defined codec | PQ code + INT8/quantised-centroid residuals is a defined codec schema (FAISS PQ+rz), not a simple per-stage multiplier; specify residual scheme explicitly. |
| INT8 → binary | requires defined codec | binary is typically derived from float / from a learned encoder, not from INT8→binarize pipeline. INT8-then-binarize loses the per-dim scaling that INT8 preserved. |
| MRL + SPLADE | NOT a codec — orthogonal retrieval channels | MRL dense + SPLADE sparse are orthogonal retrieval channels. They do not compose into one codec, but can coexist in hybrid fusion (different heads, different representations). |

[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Адаптивные embeddings - variable-length representations и Matryoshka.md]

### §10.4. Open question: MRL + binary composition

Из §9 question 9: «2048-dim embedding → MRL-truncate to 256 → binarize to 256 bits → ~256× additional compression. Does this nested compression retain usable semantic quality?»

Промоутируем в dedicated section, потому что эта комбинация наиболее перспективна для million-vector corpora с жёсткими memory budget'ами:

- Tissier 2018 binarisation тестировалась на word-embedding scale (GloVe 300-dim); sentence-transformer transfer невалидирован.
- MRL truncate 2048→256 — coarse-to-fine semantic; первые 256 dims обычно несут основную семантику (per Kusupati et al. 2022 ImageNet results, 14× speedup с 14× reduction при comparable accuracy).
- **Composite arithmetic (MRL 256 + 256-bit binary), per code:**
  - 768-dim float32 + MRL 256 + 256-bit code: 3072 B → 1024 B → 32 B → ~96× compression (1M vectors: ~3 GB → ~32 MB).
  - 2048-dim float32 + MRL 256 + 256-bit code: 8192 B → 1024 B → 32 B → ~256× compression (1M vectors: ~8 GB → ~32 MB).
  Оба варианта влезают в single-process memory budget на million-vector corpora.
- **Открытый вопрос:** сохраняет ли nested MRL+binary качество на современных sentence-transformer embeddings (BGE, E5, M3-Embedding), или теряет tail of semantic distribution?

Рекомендация: **treat as experimental until benchmarked on target corpus.** Использовать как `DenseIndexMode::BinaryCandidateFilter` (planned) — coarse filter перед float rerank, не standalone storage.

[Source: arXiv:2205.13147 — Kusupati et al. 2022]
<br>[Source: arXiv:1803.09065 — Tissier, Gravier, Habrard 2018]
<br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Адаптивные embeddings - variable-length representations и Matryoshka.md]

## §11. References

### 11.1. Public sources

- **Tissier, Julien; Gravier, Christophe; Habrard, Amaury (2018). "Near-lossless Binarization of Word Embeddings."** AAAI 2019 (arXiv:1803.09065). URL: <https://arxiv.org/abs/1803.09065>. Code: <https://github.com/tca19/near-lossless-binarization>.
- **Chen, Jianlv; Xiao, Shitao; Zhang, Peitian; Luo, Kun; Lian, Defu; Liu, Zheng (2024). "M3-Embedding: Multi-Linguality, Multi-Functionality, Multi-Granularity Text Embeddings Through Self-Knowledge Distillation."** arXiv:2402.03216, BAAI (BGE-M3 in open-source release). URL: <https://arxiv.org/abs/2402.03216>.
- **Joulin, Armand; Grave, Edouard; Bojanowski, Piotr; Mikolov, Tomas (2016). "FastText.zip: Compressing text classification models."** arXiv:1612.03651. URL: <https://arxiv.org/abs/1612.03651>.
- **Jégou, Hervé; Douze, Matthijs; Schmid, Cordelia (2011); subsequent FAISS line including Johnson, Jeff; Douze, Matthijs; Jégou, Hervé (2017). "Billion-scale similarity search with GPUs."** arXiv:1702.08734 (FAISS). URL: <https://arxiv.org/abs/1702.08734>.
- **Kusupati, Aditya; et al. (2022). "Matryoshka Representation Learning."** arXiv:2205.13147. URL: <https://arxiv.org/abs/2205.13147>.
- **He, et al. (2025). "A Survey on Deep Text Hashing."** arXiv:2510.27232. URL: <https://arxiv.org/abs/2510.27232>.

### 11.2. Internal notes (ai-agent-playbook)

- `ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md` (internal note) — comprehensive concept summary: Tissier 2018 autoencoder approach, deep text hashing survey, PQ, MRL, regularising effect of binarisation.
- `ai-agent-playbook/resources/llm-research/M3-Embedding BGE-M3 - Multi-Linguality Multi-Functionality Multi-Granularity - arXiv 2402.03216 - разбор статьи.md` (internal note) — BGE-M3 paper analysis: 3M properties (multi-linguality, multi-functionality, multi-granularity), self-knowledge distillation.

### 11.3. In-house (agent-memory-cpp/guides/)

- [`optimization-roadmap.md`](optimization-roadmap.md) — primary technical anchor:
  - §"Eigen и SIMD стратегия" — SIMD abstraction layer, `popcount64`, `HammingTopK` kernel design.
  - §"Binary Signature Index Tasks" — `BinarySignature` value type, `IBinarySignatureEncoder`, baseline encoder, learned autoencoder encoder (M2 experimental), short-key generation, neighbour bucket masks.
  - §"Encoder Registry и Versioning" — `BinarySignatureEncoderRegistry`, `.bse` file format, `BinarySignatureEncoderId` taxonomy.
  - §"Binary Bucket Index Tasks" — `binary_bucket_index` MDBX layout, mutable bucket updates, query pipeline, encoder registry versioning.
  - §"Dense Index Modes (Backend Selection)" — 4-mode `IDenseIndex` interface (Exact / BinaryCandidateFilter / BinaryOnly / ApproximateVector), `DenseIndexConfig` shape, storage estimates, quality targets per mode, per-stack defaults.
  - §"Future Encodings" — `VectorEncoding` enum, `BinarySignatureEncoderId` taxonomy.
  - §"Per-Bit-Size Recall Targets" and §"Per-Mode Recall Targets" — quality targets for binary encoders across modes.
  - §"Recommended Implementation Order" — Steps 7-9 (binary signature, bucket), 22 (encoder registry), 23-25 (multi-mode IDenseIndex), 26-28 (SIMD abstraction and HammingTopK kernel).
- [`code-intelligence-roadmap.md`](code-intelligence-roadmap.md) — MinHash + LSH (Pattern 1) augments binary bucket index; RotSQ codec (Pattern 2) slots alongside Matryoshka, PQ codecs.
- [`retrieval-techniques-roadmap.md`](retrieval-techniques-roadmap.md) — typology of retrieval techniques; binary embeddings filter candidates, retrieval still ranks.
- [`memory-routing-roadmap.md`](memory-routing-roadmap.md) — pre-retrieval routing sits "до" binary embedding filter; both layers are cheap pre-filters in series.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — `HybridRetrievalEngine` (existing), `IQueryTransformer` (planned), `IRetrievalEvaluator` (planned) — binary embeddings integrate as candidate filter inside retrieval.
- [`memory-stacks-roadmap.md`](memory-stacks-roadmap.md) — `MemoryProfileSpec`, `DenseIndexConfig`, `DenseIndexMode`, MDBX layout, capability flags, embedding migration workflow (`EmbeddingRecomputeJob`).