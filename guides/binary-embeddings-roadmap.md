# Binary Embeddings Roadmap

> C++17 compliance: кодовые сниппеты используют `const std::vector<T>&` вместо `std::span` и явные конструкторы вместо designated initializers. PR #29 binary signatures — это cache-friendly fingerprints для coarse filter (bucket index); binary embeddings — это semantic-preserving quantizers общего назначения. Этот гайд расширяет PR #29 до общего compression layer; cross-link с [`optimization-roadmap.md`](optimization-roadmap.md) §"Binary Signature Index Tasks" обязателен.

## Source attribution policy

Этот гайд синтезирует материал из нескольких источников. Цитаты следуют двухуровневому паттерну:

- `[Source: <public citation>]` — основная цитата на опубликованную работу, документацию проекта, ревизию репозитория или статью. При цитировании внешнего источника публичная ссылка авторитетна.
- `[Source: internal note — no public source available. Path: <path>]` — утверждение взято только из внутренних исследовательских заметок без эквивалентного публичного источника на момент написания. Требует дополнительной проверки.

Внутренние заметки — это discovery aids, а не авторитетные цитаты. Публичные roadmap-заявления цитируют публичный источник первым; внутренние заметки служат лишь маркером частного происхождения.

Этот гайд покрывает **binary embeddings** как общий compression layer поверх continuous эмбеддингов. Цель — дать набор решений «когда какой метод», включая relationship к PR #29 binary signatures, SIMD-accelerated distance (XOR + POPCNT), и quality vs storage tradeoff на 64/128/256/512 bit.

Related roadmaps:

- [`optimization-roadmap.md`](optimization-roadmap.md) — основной документ: binary signature index, encoder registry, dense index modes, SIMD path.
- [`code-intelligence-roadmap.md`](code-intelligence-roadmap.md) — MinHash + LSH (Pattern 1) и RotSQ codec (Pattern 2), заимствованные из `codebase-memory-mcp`.
- [`retrieval-techniques-roadmap.md`](retrieval-techniques-roadmap.md) — typology of retrieval techniques; binary embeddings фильтруют candidate set, retrieval остаётся.
- [`memory-routing-roadmap.md`](memory-routing-roadmap.md) — pre-retrieval routing layer, sits "до" binary embedding filter.
- [`knowledge-base-roadmap.md`](knowledge-base-roadmap.md) — `HybridRetrievalEngine` контракт, на котором binary embeddings используются как candidate filter.

## §1. Purpose

Этот гайд существует для трёх целей:

1. **Extend PR #29 to general compression layer.** PR #29 binary signatures — cache-friendly fingerprints для coarse bucket filter; binary embeddings — semantic-preserving quantizers общего назначения, расширяющие signature-only подход до storage tier (binary + optional decoder) и hybrid modes (binary + dense rerank).
2. **Comparison framework.** Свести в одну карту методы binarization (sign-based, autoencoder, LSH, PQ), по единому набору осей: compression ratio, accuracy retention, training cost, inference cost, hardware-friendliness.
3. **Adoption guidance.** Для каждого метода пометить, что из него `agent-memory-cpp` может воспроизвести через наши существующие примитивы (`BinarySignature`, `BinarySignatureEncoderRegistry`, `IBinarySignatureEncoder`, `DenseIndexMode`) и что требует нового контракта.

Non-goals:

- Не повторять детальную спецификацию `BinarySignatureEncoderRegistry` (это в `optimization-roadmap.md` §"Encoder Registry и Versioning").
- Не описывать float32 / float16 / int8 codecs (это в `optimization-roadmap.md` §"Future Encodings").
- Не каталогизировать ANN-методы (HNSW, IVF, DiskANN — это в `optimization-roadmap.md` §"Dense Index Modes").

Binary embeddings — **не замена** full vectors. Это tradeoff между accuracy и storage/query cost. Quality-sensitive rerank остаётся обязательным; binary embeddings могут быть **only candidate filter** (PR #29 mode `BinaryCandidateFilter`) или **primary** storage tier (experimental modes `BinaryOnly`, `ApproximateVector`).

## §2. Binarization Landscape

| Метод | Размер на вектор | Compression vs float32 | Качество | Когда применять |
|---|---|---|---|---|
| **Float32 (baseline)** | d × 4 bytes | 1× | 100% | Ground truth, quality-sensitive rerank |
| **Float16** | d × 2 bytes | 2× | ~99.5% | GPU inference, mixed precision |
| **INT8 scalar quantization** | d × 1 byte | 4× | ~98% | pgvector, Weaviate default |
| **Matryoshka truncation (MRL)** | N × 4 bytes (N < d) | d/N× | varies | Adaptive serving, two-stage retrieval |
| **Product Quantization (PQ)** | m bytes (typ. 8) | 96× (768-dim) | ~95% | FAISS, Milvus, billion-scale ANN |
| **Binary (1 bit/dim)** | d / 8 bytes | 32× (768-dim) | ~85% | Coarse filter, edge, in-memory cache |
| **Binary learned (Tissier 2018)** | 64-512 bits total | 6-48× | ~98% (256 bit) | Coarse-to-fine retrieval, dense storage tier |
| **LSH (random hyperplanes)** | 64-512 bits total | 6-48× | ~85% (256 bit) | Cache-friendly candidate filter (PR #29) |
| **RotSQ** | ~6 bytes/dim + 12 B metadata | ~6× | ~95% | Sibling codec alongside Matryoshka, PQ |

Binary embeddings занимают **наименьший размер на вектор** при сохранении семантики; tradeoff — потеря ~2% на retrieval при 256+ битах для learned encoders. На 64 бит compression выше (150×) но потери до 16% на больших датасетах.

## §3. Binarization Methods

### 3.1. Threshold-Based Sign of Embedding

```text
b_i = H(x_i)
```

`H` — Heaviside step function. Naive approach: один бит на компоненту embedding'а.

- **Pros:** zero training, dependency-free, trivially invertible (sign preserves order).
- **Cons:** keeps same dimension; no CPU-register alignment; loses magnitude info (only sign).
- **Use case:** experimental baseline; production-grade needs learned quantization.

[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

### 3.2. Autoencoder-Binarization (Tissier 2018)

The Tissier et al. 2018 approach uses a learned autoencoder with weight tying:

```text
Encoder:  b = H(W · x),   W ∈ R^{m × d}
Decoder:  x_hat = tanh(W^T · b + c)
Loss:     L = MSE(x, x_hat) + lambda · Σ_{i<j} corr(b_i, b_j)^2
```

Weight tying is forced: `H` is non-differentiable, so the encoder does not receive a direct gradient. The gradient flows only through the decoder; `W` is updated by the decoder signal. This is functionally a straight-through estimator: `dL/dW ≈ dL/dx_hat · dx_hat/dW` (assuming `dH/dz ≈ 1`).

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

LSH for binarization uses random hyperplanes (this is the `RandomHyperplaneLSH` encoder from PR #29):

```text
bit_i = sign(dot(embedding, random_hyperplane_i))
```

- **Pros:** zero training; deterministic by `(dim, bit_count, seed)`; perfect for bucket indexing; PR #29 baseline.
- **Cons:** no semantic preservation beyond the random hyperplane approximation; ~85-90% Recall@10 of float baseline at 128 bits.
- **Use case:** cache-friendly candidate filter; bucket prefiltering.

[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

This is the **same approach** as PR #29 baseline encoder; cross-link to `optimization-roadmap.md` §"Baseline Encoder" and §"Binary Signature Index Tasks". This guide treats LSH as the first member of the binarization family (zero-training baseline); autoencoder-binarization is the learned upgrade.

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

This is **one assembly instruction** on modern x86 (POPCNT). Linear scan of 1M 256-bit vectors = 1M × 1 instruction = a few milliseconds. Compare with cosine similarity, which requires multiplications and additions per dimension.

### 4.1. CPU Register Alignment

Bit sizes match CPU register widths and cache-line sizes:

| Bit size | Register alignment | Cache line | Use case |
|---|---|---|---|
| 32 bit | partial register | partial | experimental, super-fast coarse filter (NOT default) |
| 64 bit | `uint64_t` | partial | BasicRag default, minimum practical |
| 128 bit | SSE | half line | AgentLTM default, normal candidate filter |
| 256 bit | AVX2 | full line | CompiledWiki, quality-oriented |
| 512 bit | AVX-512 | two lines | M2+ experimental, near-float quality |

Rationale: 64/128/256 bits align with CPU register sizes (`uint64_t`, SSE, AVX2); 32-bit is not alignment-friendly for popcount/Hamming through `__builtin_popcount`.

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

This is the same SIMD layer defined in [`optimization-roadmap.md`](optimization-roadmap.md) §"Eigen и SIMD стратегия". Binary embeddings reuse it via `BinarySignature::hamming_distance(lhs, rhs)` and the `HammingTopK` kernel (see `optimization-roadmap.md` §"HammingTopK kernel" for the full design).

## §5. Relationship to PR #29

PR #29 binary signatures vs binary embeddings — two sides of the same coin:

| Aspect | PR #29 Binary Signatures | Binary Embeddings |
|---|---|---|
| **Purpose** | Cache-friendly fingerprints (bucket prefilter) | Semantic-preserving quantizers (general compression) |
| **Storage** | `(scope_id, projection_kind, short_key)` bucket index | Optional embedding storage tier (binary only / binary + decoder) |
| **Encoder** | `RandomHyperplaneLSH` baseline + `AutoencoderBinarizer` M2+ | Multiple encoders (sign, LSH, autoencoder, PQ, RotSQ) |
| **Reconstruction** | None (fingerprint only) | Optional decoder for `ApproximateVector` mode |
| **Quality target** | Coarse filter: Recall@10 ≥ 0.95 (binary + float rerank) | Standalone: Recall@10 ≥ 0.95 (256+ bits); binary-only: ≥ 0.85 (experimental) |
| **Use in `DenseIndexMode`** | `BinaryCandidateFilter` (mandatory with float) | `BinaryCandidateFilter` / `BinaryOnly` / `ApproximateVector` |

PR #29 = **the first slice** of the binary embeddings roadmap. It establishes the encoder registry, signature value types, and bucket index. Binary embeddings roadmap **extends** that into:

- Standalone binary embeddings as a storage tier (drop float, keep binary).
- Hybrid binary + dense indexes (binary for candidate filter + dense rerank).
- Multiple encoder backends (sign-based, LSH, autoencoder, future RotSQ).
- Optional decoder for approximate vector reconstruction.
- Per-bit-size quality vs storage tradeoff tables.

Cross-link to [`optimization-roadmap.md`](optimization-roadmap.md) §"Dense Index Modes (Backend Selection)" and §"Binary Signature Index Tasks" is mandatory.

## §6. Quality vs Storage Tradeoff

Per-bit-size quality targets for binary embeddings. Values are `Recall@10(binary_only)` normalised against `Recall@10(float_baseline)`. Full quality targets across `DenseIndexMode` × encoder × bit_size live in [`optimization-roadmap.md`](optimization-roadmap.md) §"Quality Targets Per Mode" and §"Per-Bit-Size Recall Targets".

```text
autoencoder_binarizer_128bit:
  Recall@10(binary_only) >= 0.95 x Recall@10(float_baseline)
  candidate_count: ~1.5x float top-K при Recall@10 = 0.95

autoencoder_binarizer_64bit:
  Recall@10(binary_only) >= 0.90 x Recall@10(float_baseline)

random_hyperplane_lsh_64bit (baseline):
  Recall@10(binary_only) >= 0.85 x Recall@10(float_baseline)

random_hyperplane_lsh_128bit (baseline):
  Recall@10(binary_only) >= 0.90 x Recall@10(float_baseline)

Tissier 2018 results (word-level GloVe 300-dim, 2018):
  64-bit:  ~50% of float quality on WS353 Spearman
  128-bit: ~74% of float quality
  256-bit: ~93% of float quality, 30x retrieval speedup
  512-bit: ~99% of float quality, 24x retrieval speedup
```

[Source: arXiv:1803.09065 — Tissier, Gravier, Habrard 2018] <br>[Source: internal note — no public source available. Path: ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md]

Storage estimates per 1M units × 768-dim:

| Encoding | Storage | Compression vs float | Quality target |
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

Decoder matrix shared across stack (one per encoder registry): ~384 KiB (`W_decoder` float32, 128-bit × 768-dim) + ~3 KiB `b_decoder` ≈ ~387 KiB per encoder registry.

## §7. Compatibility with `optimization-roadmap.md`

Binary embeddings slot into the existing `optimization-roadmap.md` binary-signatures bucket and the broader quantizer section.

### 7.1. Existing Bucket

PR #29 bucket (existing): `binary_bucket_index` keyed by `(scope_id, projection_kind, short_key)`, posting lists of `BinaryBucketPosting { unit_id, full_signature, unit_revision, resource_generation }`. Encoder registry: `BinarySignatureEncoderRegistry` with `.bse` file format.

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

### M0: Binary Signatures Only (PR #29 — already shipped)

PR #29 ships:

- `BinarySignature` value type with `std::vector<std::uint64_t> words`.
- `BinarySignatureInfo` with encoder id, model id, source projection kind.
- `IBinarySignatureEncoder` interface.
- `BinarySignatureEncoderRegistry` with `.bse` file format.
- `RandomHyperplaneLSH` baseline encoder.
- `binary_bucket_index` MDBX layout.
- `DenseIndexMode::BinaryCandidateFilter` default production mode.

This guide treats M0 as the starting point.

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
- Hybrid retrieval: binary filter produces candidate set (Recall@10 ≥ 0.95), HNSW ranks within candidate set, dense rerank on top.
- Adaptive per-query mode selection via `RetrievalPlan::dense_index_mode_override`.
- Multi-encoder hybrid: binary embeddings + PQ codes for finer rerank (asymmetric ADC distance).

## §9. Open Questions

1. **Calibration of binary quality targets.** The Tissier 2018 results are on word-level GloVe; transfer to sentence-transformer embeddings (BERT, E5, BGE, M3-Embedding) is not validated. Same open question as `memory-routing-roadmap.md` §11: does the autoencoder objective generalise from 300-dim GloVe to 768-1024 dim modern embeddings?
2. **Comparison with ANN methods (HNSW, IVF, ScaNN).** On corpora >10M vectors, does binary embedding remain competitive, or does HNSW dominate? Benchmark-driven choice is required.
3. **Multi-bit quantization (2-4 bits/dim).** Active research direction since 2020. Promises intermediate trade-off between binary (1 bit) and float8 (8 bits). Integration with PR #29 encoder registry: new `BinarySignatureEncoderId` value, new `.bse` encoder format, new bit-count handling in `BinarySignature`.
4. **Multilingual and code-model embeddings.** Structure of semantic space differs from GloVe/fasttext. Does Tissier 2018's `MSE + decorrelation penalty` objective still work for multilingual and code embeddings?
5. **LLM-based hashing.** Frozen LLM + hash head vs joint fine-tuning. Trade-off: training cost vs code quality. No public benchmark comparing both approaches on retrieval metrics (mAP, NDCG).
6. **Adversarial robustness.** Small text edit (paraphrase, synonym swap) can radically change binary code. How robust is binary coarse-filter for production RAG with paraphrased queries?
7. **PQ on LLM hidden states.** Joulin 2016 PQ for text classification is on Word2Vec; does it transfer to 4096-dim LLM hidden states with residual compensation?
8. **Adaptive dimension selection per query.** Can we dynamically select embedding dimension by query complexity, so simple queries don't pay for full D dimensions?
9. **MRL + binary composition.** 2048-dim embedding → MRL-truncate to 256 → binarize to 256 bits → 16× additional compression. Does this nested compression retain usable semantic quality?
10. **Decoder training cost.** Autoencoder training requires labelled corpus (10k-1M embeddings). What is the minimum viable training set size for our specific AgentLTM workload? Workload-specific, requires empirical measurement.

## §10. References

### 10.1. Public sources

- **Tissier, Julien; Gravier, Christophe; Habrard, Amaury (2018). "Near-lossless Binarization of Word Embeddings."** AAAI 2019 (arXiv:1803.09065). URL: <https://arxiv.org/abs/1803.09065>. Code: <https://github.com/tca19/near-lossless-binarization>.
- **Chen, Jianlv; Xiao, Shitao; Zhang, Peitian; Luo, Kun; Lian, Defu; Liu, Zheng (2024). "M3-Embedding: Multi-Linguality, Multi-Functionality, Multi-Granularity Text Embeddings Through Self-Knowledge Distillation."** arXiv:2402.03216, BAAI (BGE-M3 in open-source release). URL: <https://arxiv.org/abs/2402.03216>.
- **Joulin, Armand; Grave, Edouard; Bojanowski, Piotr; Mikolov, Tomas (2016). "FastText.zip: Compressing text classification models."** arXiv:1612.03651. URL: <https://arxiv.org/abs/1612.03651>.
- **Jégou, Hervé; Douze, Matthijs; Schmid, Cordelia (2011); subsequent FAISS line including Johnson, Jeff; Douze, Matthijs; Jégou, Hervé (2017). "Billion-scale similarity search with GPUs."** arXiv:1702.08734 (FAISS). URL: <https://arxiv.org/abs/1702.08734>.
- **Kusupati, Aditya; et al. (2022). "Matryoshka Representation Learning."** arXiv:2205.13147. URL: <https://arxiv.org/abs/2205.13147>.
- **He, et al. (2025). "A Survey on Deep Text Hashing."** arXiv:2510.27232. URL: <https://arxiv.org/abs/2510.27232>.

### 10.2. Internal notes (ai-agent-playbook)

- `ai-agent-playbook/concepts/llm-research/Бинаризация эмбеддингов для экономии памяти и ускорения retrieval.md` (internal note) — comprehensive concept summary: Tissier 2018 autoencoder approach, deep text hashing survey, PQ, MRL, regularising effect of binarisation.
- `ai-agent-playbook/resources/llm-research/M3-Embedding BGE-M3 - Multi-Linguality Multi-Functionality Multi-Granularity - arXiv 2402.03216 - разбор статьи.md` (internal note) — BGE-M3 paper analysis: 3M properties (multi-linguality, multi-functionality, multi-granularity), self-knowledge distillation.

### 10.3. In-house (agent-memory-cpp/guides/)

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