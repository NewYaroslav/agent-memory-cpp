"""Shared precomputed embedding fixture contract helpers.

This module is intentionally dependency-free. It owns the tiny text/qrels
fixture used by the precomputed embedding smoke tests and the canonical byte
encoding used to derive dataset, qrels, config, and embedding payload hashes.
Concrete generators remain responsible for producing vectors.
"""

from __future__ import annotations

import hashlib
import struct
from typing import Any


CORPUS = [
    {
        "id": "doc:binary-candidate-filter",
        "title": "Binary candidate filter",
        "text": (
            "Binary signatures scan a compact Hamming space first and then "
            "over-fetch candidates for exact vector reranking."
        ),
    },
    {
        "id": "doc:itq-pca-rotation",
        "title": "ITQ and PCA rotation",
        "text": (
            "PCA chooses a real-valued projection subspace while ITQ rotates "
            "that space to reduce quantization error before taking signs."
        ),
    },
    {
        "id": "doc:artifact-provenance",
        "title": "Precomputed embedding artifact provenance",
        "text": (
            "Frozen embedding fixtures record generator, model, dataset, qrels, "
            "prompt identities, normalization, dtype, and canonical hashes."
        ),
    },
    {
        "id": "doc:dataset-qrels-hash",
        "title": "Dataset and qrels hashes",
        "text": (
            "Dataset hashes protect corpus and query text. Qrels hashes protect "
            "graded relevance judgments used by retrieval evaluation."
        ),
    },
    {
        "id": "doc:affective-security",
        "title": "Affective memory security",
        "text": (
            "Affective memory should separate consent, sensitivity policy, "
            "emotion labels, encryption, and audit-friendly provenance."
        ),
    },
    {
        "id": "doc:urgency-routing",
        "title": "Urgency based memory routing",
        "text": (
            "Urgency-aware planners gate short, medium, long, and base memory "
            "retrieval to avoid unnecessary latency during live chat."
        ),
    },
    {
        "id": "doc:encrypted-storage",
        "title": "Encrypted local storage",
        "text": (
            "AEAD encryption with a strong key derivation function protects "
            "local memory files and summaries stored on disk."
        ),
    },
    {
        "id": "doc:mdbx-storage",
        "title": "MDBX storage primitives",
        "text": (
            "MDBX-backed adapters persist documents and resource manifests while "
            "keeping indexing, retrieval, and storage concerns separate."
        ),
    },
    {
        "id": "doc:bm25-lexical",
        "title": "BM25 lexical retrieval",
        "text": (
            "BM25 and BM25F lexical indexes use term frequency, inverse document "
            "frequency, and length normalization for sparse retrieval."
        ),
    },
    {
        "id": "doc:hybrid-retrieval",
        "title": "Hybrid retrieval",
        "text": (
            "Hybrid retrieval combines lexical matches, dense vector similarity, "
            "binary candidate generation, and exact reranking."
        ),
    },
    {
        "id": "doc:header-hygiene",
        "title": "Header include hygiene",
        "text": (
            "Public headers should compile independently while aggregate domain "
            "headers remain the preferred user-facing include surface."
        ),
    },
    {
        "id": "doc:experiment-notes",
        "title": "Experiment notes",
        "text": (
            "Each experiment note records the date, hypothesis, expected result, "
            "actual benchmark table, caveats, and follow-up checks."
        ),
    },
]


QUERIES = [
    {
        "id": "q:binary-rerank",
        "text": "binary hamming candidates with exact rerank",
        "query_type": "semantic",
        "limit": 5,
    },
    {
        "id": "q:artifact-integrity",
        "text": "precomputed embedding artifact dataset qrels hashes",
        "query_type": "semantic",
        "limit": 5,
    },
    {
        "id": "q:emotional-memory",
        "text": "affective memory security encryption consent",
        "query_type": "semantic",
        "limit": 5,
    },
    {
        "id": "q:prompt-routing",
        "text": "urgency based memory planner tiers live chat latency",
        "query_type": "semantic",
        "limit": 5,
    },
    {
        "id": "q:lexical-storage",
        "text": "mdbx storage bm25 lexical retrieval",
        "query_type": "semantic",
        "limit": 5,
    },
]


JUDGMENTS = [
    {"query_id": "q:binary-rerank", "item_id": "doc:binary-candidate-filter", "relevance_grade": 3},
    {"query_id": "q:binary-rerank", "item_id": "doc:itq-pca-rotation", "relevance_grade": 2},
    {"query_id": "q:binary-rerank", "item_id": "doc:hybrid-retrieval", "relevance_grade": 2},
    {"query_id": "q:artifact-integrity", "item_id": "doc:artifact-provenance", "relevance_grade": 3},
    {"query_id": "q:artifact-integrity", "item_id": "doc:dataset-qrels-hash", "relevance_grade": 3},
    {"query_id": "q:artifact-integrity", "item_id": "doc:experiment-notes", "relevance_grade": 1},
    {"query_id": "q:emotional-memory", "item_id": "doc:affective-security", "relevance_grade": 3},
    {"query_id": "q:emotional-memory", "item_id": "doc:encrypted-storage", "relevance_grade": 2},
    {"query_id": "q:prompt-routing", "item_id": "doc:urgency-routing", "relevance_grade": 3},
    {"query_id": "q:prompt-routing", "item_id": "doc:hybrid-retrieval", "relevance_grade": 1},
    {"query_id": "q:lexical-storage", "item_id": "doc:mdbx-storage", "relevance_grade": 3},
    {"query_id": "q:lexical-storage", "item_id": "doc:bm25-lexical", "relevance_grade": 3},
    {"query_id": "q:lexical-storage", "item_id": "doc:hybrid-retrieval", "relevance_grade": 1},
]


def u32(value: int) -> bytes:
    return struct.pack("<I", value)


def i32(value: int) -> bytes:
    return struct.pack("<i", value)


def f32(value: float) -> bytes:
    narrowed = struct.unpack("<f", struct.pack("<f", float(value)))[0]
    if narrowed == 0.0:
        narrowed = 0.0
    return struct.pack("<f", narrowed)


def string_bytes(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return u32(len(encoded)) + encoded


def canonical_config_text(root: dict[str, Any]) -> bytes:
    model = root["embedding_model"]
    artifact = root["embedding_artifact"]
    lines = [
        f"dataset_revision={artifact['dataset_revision']}",
        f"document_prompt_id={artifact['document_prompt_id']}",
        f"dtype={artifact['dtype']}",
        f"embedding_dimension={model['dimension']}",
        f"embedding_model_id={model['model_id']}",
        f"embedding_normalized={'true' if model['normalized'] else 'false'}",
        f"generator_id={artifact['generator_id']}",
        f"generator_revision={artifact['generator_revision']}",
    ]
    for field in (
        "generator_command",
        "generator_requirements_lock",
        "generator_contract_source_hash",
        "generator_source_hash",
    ):
        if artifact.get(field):
            lines.append(f"{field}={artifact[field]}")
    lines.extend(
        [
            f"generator_version={artifact['generator_version']}",
            f"model_revision={artifact['model_revision']}",
            f"normalization={artifact['normalization']}",
            f"pooling_mode={model['pooling_mode']}",
            f"projection_kind={artifact['projection_kind']}",
            f"qrels_revision={artifact['qrels_revision']}",
            f"query_prompt_id={artifact['query_prompt_id']}",
            f"similarity_metric={model['similarity_metric']}",
        ]
    )
    if artifact.get("tokenizer_revision"):
        lines.append(f"tokenizer_revision={artifact['tokenizer_revision']}")
    return ("\n".join(lines) + "\n").encode("utf-8")


def canonical_dataset_payload(root: dict[str, Any]) -> bytes:
    payload = bytearray(b"agent-memory-precomputed-embedding-dataset-v1")
    payload += u32(len(root["corpus"]))
    for item in root["corpus"]:
        payload += string_bytes(item["id"])
        payload += string_bytes(item["title"])
        payload += string_bytes(item["text"])
        metadata = item.get("metadata", {})
        payload += u32(len(metadata))
        for key in sorted(metadata):
            payload += string_bytes(key)
            payload += string_bytes(metadata[key])
    payload += u32(len(root["queries"]))
    for query in root["queries"]:
        payload += string_bytes(query["id"])
        payload += string_bytes(query["text"])
        payload += string_bytes(query["query_type"])
        payload += u32(query["limit"])
        filters = query.get("metadata_filters", [])
        payload += u32(len(filters))
        for metadata_filter in filters:
            payload += string_bytes(metadata_filter["key"])
            payload += string_bytes(metadata_filter["value"])
        payload += string_bytes(query.get("answer_mode", "JudgedRetrieval"))
    return bytes(payload)


def canonical_qrels_payload(root: dict[str, Any]) -> bytes:
    payload = bytearray(b"agent-memory-precomputed-embedding-qrels-v1")
    payload += u32(len(root["judgments"]))
    for judgment in root["judgments"]:
        payload += string_bytes(judgment["query_id"])
        payload += string_bytes(judgment["item_id"])
        payload += i32(judgment["relevance_grade"])
    return bytes(payload)


def canonical_artifact_payload(root: dict[str, Any]) -> bytes:
    payload = bytearray(b"agent-memory-precomputed-embedding-payload-v1")
    for field, record_type in (("document_embeddings", 1), ("query_embeddings", 2)):
        for record in root[field]:
            payload += struct.pack("B", record_type)
            payload += string_bytes(record["id"])
            payload += u32(len(record["vector"]))
            for value in record["vector"]:
                payload += f32(value)
    return bytes(payload)


def sha256_hex(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()
