#!/usr/bin/env python3
"""Generate the external-hash precomputed embedding benchmark fixture.

This script is intentionally dependency-free. It models the "external
precomputed artifact" workflow without requiring CI or contributors to download
an embedding model: vectors are generated outside the C++ benchmark, frozen into
JSON, and then validated by the C++ artifact verifier.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
from collections import Counter
from pathlib import Path
from typing import Any


DIMENSION = 32
GENERATOR_ID = "agent-memory.tools.external-hash-embedding"
GENERATOR_VERSION = "v1"
GENERATOR_REVISION = "agent-memory-cpp:external-hash-fixture-v1"
MODEL_ID = "agent-memory.external-hash-embedding-32d-v1"
MODEL_REVISION = "external-hash-embedding-32d-v1"
DATASET_REVISION = "agent-memory-external-hash-fixture:2026-07-22"
QRELS_REVISION = "agent-memory-external-hash-qrels:2026-07-22"
PROJECTION_KIND = "external_hash_text_ngrams_32d"


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


TOKEN_RE = re.compile(r"[a-z0-9]+")


def tokenize(text: str) -> list[str]:
    return TOKEN_RE.findall(text.lower())


def features(text: str) -> Counter[str]:
    tokens = tokenize(text)
    counts: Counter[str] = Counter()
    for token in tokens:
        counts[f"tok:{token}"] += 1
        if len(token) >= 4:
            for index in range(len(token) - 2):
                counts[f"tri:{token[index:index + 3]}"] += 1
    for left, right in zip(tokens, tokens[1:]):
        counts[f"bigram:{left}_{right}"] += 1
    return counts


def hash_bytes(payload: bytes) -> bytes:
    return hashlib.sha256(payload).digest()


def embedding_for_text(text: str) -> list[float]:
    values = [0.0] * DIMENSION
    for feature, count in features(text).items():
        digest = hash_bytes(f"{MODEL_REVISION}|{feature}".encode("utf-8"))
        bucket = int.from_bytes(digest[:4], "little") % DIMENSION
        sign = -1.0 if digest[4] & 1 else 1.0
        # Damp repeated character n-grams without making term frequency irrelevant.
        values[bucket] += sign * (1.0 + math.log(count))
    norm = math.sqrt(sum(value * value for value in values))
    if norm == 0.0:
        raise RuntimeError(f"text produced a zero embedding: {text!r}")
    return [float(f"{value / norm:.8g}") for value in values]


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
    text = (
        f"dataset_revision={artifact['dataset_revision']}\n"
        f"document_prompt_id={artifact['document_prompt_id']}\n"
        f"dtype={artifact['dtype']}\n"
        f"embedding_dimension={model['dimension']}\n"
        f"embedding_model_id={model['model_id']}\n"
        f"embedding_normalized={'true' if model['normalized'] else 'false'}\n"
        f"generator_id={artifact['generator_id']}\n"
        f"generator_revision={artifact['generator_revision']}\n"
        f"generator_version={artifact['generator_version']}\n"
        f"model_revision={artifact['model_revision']}\n"
        f"normalization={artifact['normalization']}\n"
        f"pooling_mode={model['pooling_mode']}\n"
        f"projection_kind={artifact['projection_kind']}\n"
        f"qrels_revision={artifact['qrels_revision']}\n"
        f"query_prompt_id={artifact['query_prompt_id']}\n"
        f"similarity_metric={model['similarity_metric']}\n"
    )
    return text.encode("utf-8")


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


def build_fixture() -> dict[str, Any]:
    document_embeddings = [
        {
            "id": item["id"],
            "vector": embedding_for_text(f"{item['title']}\n{item['text']}"),
        }
        for item in CORPUS
    ]
    query_embeddings = [
        {
            "id": query["id"],
            "vector": embedding_for_text(query["text"]),
        }
        for query in QUERIES
    ]
    root: dict[str, Any] = {
        "schema_version": 1,
        "name": "precomputed-embedding-external-hash",
        "embedding_model": {
            "model_id": MODEL_ID,
            "dimension": DIMENSION,
            "similarity_metric": "cosine",
            "pooling_mode": "mean",
            "normalized": True,
        },
        "embedding_artifact": {
            "generator_id": GENERATOR_ID,
            "generator_version": GENERATOR_VERSION,
            "dataset_revision": DATASET_REVISION,
            "generator_revision": GENERATOR_REVISION,
            "model_revision": MODEL_REVISION,
            "qrels_revision": QRELS_REVISION,
            "document_prompt_id": "title-plus-text-v1",
            "query_prompt_id": "query-text-v1",
            "projection_kind": PROJECTION_KIND,
            "normalization": "l2",
            "dtype": "float32",
            "hash_algorithm": "sha256",
            "config_hash": "",
            "dataset_hash": "",
            "qrels_hash": "",
            "artifact_hash": "",
        },
        "corpus": CORPUS,
        "queries": QUERIES,
        "judgments": JUDGMENTS,
        "document_embeddings": document_embeddings,
        "query_embeddings": query_embeddings,
    }
    artifact = root["embedding_artifact"]
    artifact["config_hash"] = sha256_hex(canonical_config_text(root))
    artifact["dataset_hash"] = sha256_hex(canonical_dataset_payload(root))
    artifact["qrels_hash"] = sha256_hex(canonical_qrels_payload(root))
    artifact["artifact_hash"] = sha256_hex(canonical_artifact_payload(root))
    return root


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    fixture = build_fixture()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(fixture, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
