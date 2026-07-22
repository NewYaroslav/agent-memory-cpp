"""Larger text/qrels contract for the MiniLM precomputed embedding benchmark.

The canonical byte encodings remain in `precomputed_fixture_contract.py`.
This module owns only the larger evaluation content so changes to this fixture
do not perturb the small MiniLM smoke fixture.
"""

from __future__ import annotations


CORPUS = [
    {
        "id": "doc:binary-candidate-filter",
        "title": "Binary candidate filter",
        "text": (
            "Binary signatures scan compact Hamming codes first, over-fetch a "
            "candidate set, and then rerank selected documents with exact "
            "float-vector similarity."
        ),
    },
    {
        "id": "doc:hamming-hot-path",
        "title": "Hamming hot path",
        "text": (
            "Packed binary signatures use word-wise xor plus popcount. Hardware "
            "popcount, lookup fallbacks, and deterministic top-k selection keep "
            "the flat Hamming scan reproducible."
        ),
    },
    {
        "id": "doc:multi-probe-binary-index",
        "title": "Multi-probe binary index",
        "text": (
            "A production binary backend should avoid full scans by probing "
            "nearby buckets, MIH partitions, HNSW-Hamming, or another sublinear "
            "candidate-generation structure."
        ),
    },
    {
        "id": "doc:pca-projection",
        "title": "PCA projection",
        "text": (
            "PCA learns a global real-valued projection from document vectors. "
            "It can be useful before binarization, but training and evaluation "
            "records must be separated."
        ),
    },
    {
        "id": "doc:itq-rotation",
        "title": "ITQ rotation",
        "text": (
            "ITQ rotates a PCA subspace with an orthogonal matrix to reduce "
            "quantization error before taking the sign of each projected "
            "coordinate."
        ),
    },
    {
        "id": "doc:learned-projection",
        "title": "Learned projection baseline",
        "text": (
            "A global learned projection can train on pair differences or other "
            "document-only statistics. It must not leak evaluation queries into "
            "the encoder artifact."
        ),
    },
    {
        "id": "doc:artifact-provenance",
        "title": "Precomputed embedding artifact provenance",
        "text": (
            "Frozen embedding artifacts record model, tokenizer, generator, "
            "dataset, qrels, prompt identities, normalization, dtype, and hash "
            "scopes."
        ),
    },
    {
        "id": "doc:canonical-hash",
        "title": "Canonical artifact hash",
        "text": (
            "Canonical artifact hashes should depend on parsed float32 payload "
            "bytes and deterministic metadata, not incidental JSON number "
            "spelling."
        ),
    },
    {
        "id": "doc:requirements-manifest",
        "title": "Generator requirements manifest",
        "text": (
            "A fixture generator should parse the same requirements manifest "
            "whose SHA-256 appears in provenance, so the recorded environment "
            "matches runtime checks."
        ),
    },
    {
        "id": "doc:affective-security",
        "title": "Affective memory security",
        "text": (
            "Affective memory stores sensitive emotion-related signals and "
            "therefore needs consent policy, sensitivity labels, encryption, and "
            "audit-friendly provenance."
        ),
    },
    {
        "id": "doc:encrypted-storage",
        "title": "Encrypted local storage",
        "text": (
            "Local memory files can use AEAD encryption with a strong key "
            "derivation function and atomic replace semantics to protect data "
            "at rest."
        ),
    },
    {
        "id": "doc:aead-kdf",
        "title": "AEAD and KDF contract",
        "text": (
            "AES-GCM or another AEAD mode protects confidentiality and "
            "integrity, while scrypt or another KDF turns a password or machine "
            "secret into an encryption key."
        ),
    },
    {
        "id": "doc:urgency-routing",
        "title": "Urgency based memory routing",
        "text": (
            "Urgency-aware retrieval gates short, medium, long, and base memory "
            "tiers so live chat does not wait for unnecessary deep retrieval."
        ),
    },
    {
        "id": "doc:memory-aware-prompt-builder",
        "title": "Memory aware prompt builder",
        "text": (
            "A prompt builder can inspect trigger keywords, urgency, and "
            "dialogue context to choose which memory layers should be queried "
            "before constructing the final prompt."
        ),
    },
    {
        "id": "doc:memory-tiers",
        "title": "Memory tiers",
        "text": (
            "Short, medium, long, and base memory tiers trade off latency, "
            "recency, durability, cross-agent reuse, and prompt budget."
        ),
    },
    {
        "id": "doc:mdbx-storage",
        "title": "MDBX storage primitives",
        "text": (
            "MDBX-backed adapters persist documents and resource manifests while "
            "keeping storage, indexing, retrieval, and ingestion boundaries "
            "separate."
        ),
    },
    {
        "id": "doc:bm25-lexical",
        "title": "BM25 lexical retrieval",
        "text": (
            "BM25 and BM25F lexical indexes use term frequency, inverse document "
            "frequency, and length normalization to rank sparse text matches."
        ),
    },
    {
        "id": "doc:hybrid-retrieval",
        "title": "Hybrid retrieval",
        "text": (
            "Hybrid retrieval can combine lexical matches, dense vector "
            "similarity, binary candidate generation, and exact reranking."
        ),
    },
    {
        "id": "doc:knowledge-base",
        "title": "Knowledge base capability",
        "text": (
            "A knowledge-base capability retrieves trusted facts, project notes, "
            "and durable reference material without turning the memory library "
            "into a general agent framework."
        ),
    },
    {
        "id": "doc:wiki-synthesis",
        "title": "Wiki style long-term synthesis",
        "text": (
            "Long-term synthesis can consolidate repeated memories into wiki-like "
            "articles that are portable across agents and easier to audit."
        ),
    },
    {
        "id": "doc:cross-agent-memory",
        "title": "Cross-agent memory",
        "text": (
            "Cross-agent memory needs explicit source provenance, ownership, "
            "schema compatibility, and policies for sharing durable knowledge "
            "between agents."
        ),
    },
    {
        "id": "doc:chunker-layout",
        "title": "Chunker layout",
        "text": (
            "Chunkers split resources into stable retrievable units while "
            "preserving resource identifiers, revisions, and text offsets for "
            "targeted reindexing."
        ),
    },
    {
        "id": "doc:resource-indexer",
        "title": "Resource indexer",
        "text": (
            "A resource indexer coordinates document snapshots, embeddings, "
            "lexical postings, and vector indexes without collapsing those "
            "concerns into one storage facade."
        ),
    },
    {
        "id": "doc:header-hygiene",
        "title": "Header include hygiene",
        "text": (
            "Public headers should compile independently, while aggregate domain "
            "headers remain the preferred user-facing include surface."
        ),
    },
    {
        "id": "doc:qrels-grading",
        "title": "Graded qrels",
        "text": (
            "Graded relevance judgments distinguish highly relevant, partially "
            "relevant, and weakly relevant documents so nDCG can evaluate "
            "ranking quality."
        ),
    },
    {
        "id": "doc:no-answer-coverage",
        "title": "No-answer and ignored query coverage",
        "text": (
            "Evaluation reports must track judged, no-answer, and ignored query "
            "coverage by query identity, not just by count."
        ),
    },
    {
        "id": "doc:experiment-notes",
        "title": "Experiment notes",
        "text": (
            "Experiment notes record date, hypothesis, expected result, actual "
            "benchmark table, caveats, and follow-up checks for each research "
            "line."
        ),
    },
    {
        "id": "doc:appraisal-model",
        "title": "Appraisal model",
        "text": (
            "Affective memory can represent appraisal dimensions such as novelty, "
            "pleasantness, goal relevance, coping potential, and social meaning."
        ),
    },
    {
        "id": "doc:affective-tags",
        "title": "Affective tags",
        "text": (
            "Emotion tags should be explicit derived projections with confidence, "
            "source, consent, and sensitivity metadata rather than hidden prompt "
            "state."
        ),
    },
    {
        "id": "doc:consent-policy",
        "title": "Consent policy",
        "text": (
            "Storing personal or emotional memories requires policy hooks for "
            "consent, retention, erasure, visibility, and redaction."
        ),
    },
    {
        "id": "doc:hnsw-hamming",
        "title": "HNSW Hamming backend",
        "text": (
            "HNSW over binary codes can provide approximate nearest-neighbor "
            "candidate generation with Hamming distance instead of scanning every "
            "signature."
        ),
    },
    {
        "id": "doc:ivf-pq",
        "title": "IVF and PQ hybrid",
        "text": (
            "IVF and product quantization partition vector space and compress "
            "payloads so candidate generation can scale beyond flat scans."
        ),
    },
    {
        "id": "doc:mih-index",
        "title": "Multi-index hashing",
        "text": (
            "Multi-index hashing splits binary codes into substrings, probes "
            "matching buckets, and joins partial matches to recover nearby "
            "Hamming neighbors."
        ),
    },
    {
        "id": "doc:minilm-embedding",
        "title": "MiniLM embedding fixture",
        "text": (
            "The MiniLM fixture uses a pinned transformer revision, mean pooling, "
            "L2 normalization, explicit float32 narrowing, and frozen JSON "
            "vectors."
        ),
    },
    {
        "id": "doc:bge-e5-embeddings",
        "title": "BGE and E5 embeddings",
        "text": (
            "Additional embedding adapters can support BGE, E5, or other popular "
            "models through precomputed fixtures before adding production "
            "runtime dependencies."
        ),
    },
    {
        "id": "doc:precomputed-fixture",
        "title": "Precomputed fixture workflow",
        "text": (
            "Precomputed embedding fixtures decouple benchmark verification from "
            "network access, model downloads, and provider-specific embedding "
            "runtime code."
        ),
    },
]


QUERIES = [
    {"id": "q:binary-prefilter", "text": "binary hamming candidate prefilter with exact rerank", "query_type": "semantic", "limit": 10},
    {"id": "q:learned-binary-projection", "text": "PCA ITQ learned projection without evaluation query leakage", "query_type": "semantic", "limit": 10},
    {"id": "q:artifact-provenance", "text": "precomputed embedding artifact canonical hash generator requirements manifest", "query_type": "semantic", "limit": 10},
    {"id": "q:encrypted-affective-memory", "text": "affective memory encryption consent sensitivity policy", "query_type": "semantic", "limit": 10},
    {"id": "q:urgency-prompt-routing", "text": "urgency aware prompt builder memory tiers live chat latency", "query_type": "semantic", "limit": 10},
    {"id": "q:lexical-storage-hybrid", "text": "MDBX storage BM25 lexical dense hybrid retrieval", "query_type": "semantic", "limit": 10},
    {"id": "q:cross-agent-knowledge", "text": "knowledge base wiki synthesis cross agent durable memory", "query_type": "semantic", "limit": 10},
    {"id": "q:indexing-hygiene", "text": "chunker resource indexer public header include hygiene", "query_type": "semantic", "limit": 10},
    {"id": "q:evaluation-contract", "text": "graded qrels no answer ignored query coverage experiment notes", "query_type": "semantic", "limit": 10},
    {"id": "q:emotion-projections", "text": "appraisal model affective tags consent policy emotional memory", "query_type": "semantic", "limit": 10},
    {"id": "q:production-indexes", "text": "HNSW Hamming IVF PQ multi index hashing production binary backend", "query_type": "semantic", "limit": 10},
    {"id": "q:embedding-fixtures", "text": "MiniLM BGE E5 precomputed embedding fixture workflow", "query_type": "semantic", "limit": 10},
]


JUDGMENTS = [
    {"query_id": "q:binary-prefilter", "item_id": "doc:binary-candidate-filter", "relevance_grade": 3},
    {"query_id": "q:binary-prefilter", "item_id": "doc:hamming-hot-path", "relevance_grade": 2},
    {"query_id": "q:binary-prefilter", "item_id": "doc:multi-probe-binary-index", "relevance_grade": 2},
    {"query_id": "q:learned-binary-projection", "item_id": "doc:pca-projection", "relevance_grade": 3},
    {"query_id": "q:learned-binary-projection", "item_id": "doc:itq-rotation", "relevance_grade": 3},
    {"query_id": "q:learned-binary-projection", "item_id": "doc:learned-projection", "relevance_grade": 2},
    {"query_id": "q:artifact-provenance", "item_id": "doc:artifact-provenance", "relevance_grade": 3},
    {"query_id": "q:artifact-provenance", "item_id": "doc:canonical-hash", "relevance_grade": 3},
    {"query_id": "q:artifact-provenance", "item_id": "doc:requirements-manifest", "relevance_grade": 2},
    {"query_id": "q:encrypted-affective-memory", "item_id": "doc:affective-security", "relevance_grade": 3},
    {"query_id": "q:encrypted-affective-memory", "item_id": "doc:encrypted-storage", "relevance_grade": 3},
    {"query_id": "q:encrypted-affective-memory", "item_id": "doc:aead-kdf", "relevance_grade": 2},
    {"query_id": "q:urgency-prompt-routing", "item_id": "doc:urgency-routing", "relevance_grade": 3},
    {"query_id": "q:urgency-prompt-routing", "item_id": "doc:memory-aware-prompt-builder", "relevance_grade": 3},
    {"query_id": "q:urgency-prompt-routing", "item_id": "doc:memory-tiers", "relevance_grade": 2},
    {"query_id": "q:lexical-storage-hybrid", "item_id": "doc:mdbx-storage", "relevance_grade": 3},
    {"query_id": "q:lexical-storage-hybrid", "item_id": "doc:bm25-lexical", "relevance_grade": 3},
    {"query_id": "q:lexical-storage-hybrid", "item_id": "doc:hybrid-retrieval", "relevance_grade": 2},
    {"query_id": "q:cross-agent-knowledge", "item_id": "doc:knowledge-base", "relevance_grade": 3},
    {"query_id": "q:cross-agent-knowledge", "item_id": "doc:wiki-synthesis", "relevance_grade": 3},
    {"query_id": "q:cross-agent-knowledge", "item_id": "doc:cross-agent-memory", "relevance_grade": 2},
    {"query_id": "q:indexing-hygiene", "item_id": "doc:chunker-layout", "relevance_grade": 3},
    {"query_id": "q:indexing-hygiene", "item_id": "doc:resource-indexer", "relevance_grade": 3},
    {"query_id": "q:indexing-hygiene", "item_id": "doc:header-hygiene", "relevance_grade": 2},
    {"query_id": "q:evaluation-contract", "item_id": "doc:qrels-grading", "relevance_grade": 3},
    {"query_id": "q:evaluation-contract", "item_id": "doc:no-answer-coverage", "relevance_grade": 3},
    {"query_id": "q:evaluation-contract", "item_id": "doc:experiment-notes", "relevance_grade": 2},
    {"query_id": "q:emotion-projections", "item_id": "doc:appraisal-model", "relevance_grade": 3},
    {"query_id": "q:emotion-projections", "item_id": "doc:affective-tags", "relevance_grade": 3},
    {"query_id": "q:emotion-projections", "item_id": "doc:consent-policy", "relevance_grade": 2},
    {"query_id": "q:production-indexes", "item_id": "doc:hnsw-hamming", "relevance_grade": 3},
    {"query_id": "q:production-indexes", "item_id": "doc:ivf-pq", "relevance_grade": 3},
    {"query_id": "q:production-indexes", "item_id": "doc:mih-index", "relevance_grade": 2},
    {"query_id": "q:embedding-fixtures", "item_id": "doc:minilm-embedding", "relevance_grade": 3},
    {"query_id": "q:embedding-fixtures", "item_id": "doc:bge-e5-embeddings", "relevance_grade": 2},
    {"query_id": "q:embedding-fixtures", "item_id": "doc:precomputed-fixture", "relevance_grade": 2},
]
