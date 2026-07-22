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
from collections import Counter
from pathlib import Path
from typing import Any

import precomputed_fixture_contract as contract


DIMENSION = 32
GENERATOR_ID = "agent-memory.tools.external-hash-embedding"
GENERATOR_VERSION = "v1"
GENERATOR_REVISION = "agent-memory-cpp:external-hash-fixture-v1"
MODEL_ID = "agent-memory.external-hash-embedding-32d-v1"
MODEL_REVISION = "external-hash-embedding-32d-v1"
DATASET_REVISION = "agent-memory-external-hash-fixture:2026-07-22"
QRELS_REVISION = "agent-memory-external-hash-qrels:2026-07-22"
PROJECTION_KIND = "external_hash_text_ngrams_32d"


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


def build_fixture() -> dict[str, Any]:
    document_embeddings = [
        {
            "id": item["id"],
            "vector": embedding_for_text(f"{item['title']}\n{item['text']}"),
        }
        for item in contract.CORPUS
    ]
    query_embeddings = [
        {
            "id": query["id"],
            "vector": embedding_for_text(query["text"]),
        }
        for query in contract.QUERIES
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
        "corpus": contract.CORPUS,
        "queries": contract.QUERIES,
        "judgments": contract.JUDGMENTS,
        "document_embeddings": document_embeddings,
        "query_embeddings": query_embeddings,
    }
    artifact = root["embedding_artifact"]
    artifact["config_hash"] = contract.sha256_hex(contract.canonical_config_text(root))
    artifact["dataset_hash"] = contract.sha256_hex(contract.canonical_dataset_payload(root))
    artifact["qrels_hash"] = contract.sha256_hex(contract.canonical_qrels_payload(root))
    artifact["artifact_hash"] = contract.sha256_hex(contract.canonical_artifact_payload(root))
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
        newline="\n",
    )


if __name__ == "__main__":
    main()
