#!/usr/bin/env python3
"""Generate a frozen MiniLM precomputed embedding benchmark fixture.

This script is intentionally not part of the default CI path: it downloads and
runs a third-party transformer model. CI consumes the committed JSON artifact
and verifies its hashes instead.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

sys.dont_write_bytecode = True

import minilm_fixture_generator_common as minilm_common
import precomputed_fixture_contract as contract


GENERATOR_REVISION = "agent-memory-cpp:minilm-l6-v2-fixture-v1"
GENERATOR_COMMAND = (
    "python tools/agent-memory-bench/generate-precomputed-minilm-fixture.py "
    "--output tests/eval/fixtures/precomputed-embedding-minilm-l6-v2.json"
)
DATASET_REVISION = "agent-memory-minilm-fixture:2026-07-22"
QRELS_REVISION = "agent-memory-minilm-qrels:2026-07-22"


def contract_script_path() -> Path:
    return Path(__file__).with_name("precomputed_fixture_contract.py")


def build_fixture(
    *,
    cache_dir: Path | None,
    local_files_only: bool,
) -> dict[str, Any]:
    corpus = contract.CORPUS
    queries = contract.QUERIES
    judgments = contract.JUDGMENTS

    document_texts = [f"{item['title']}\n{item['text']}" for item in corpus]
    query_texts = [query["text"] for query in queries]
    vectors = minilm_common.encode_texts(
        document_texts + query_texts,
        cache_dir=cache_dir,
        local_files_only=local_files_only,
    )
    document_vectors = vectors[: len(document_texts)]
    query_vectors = vectors[len(document_texts):]
    dimension = len(document_vectors[0])

    root: dict[str, Any] = {
        "schema_version": 1,
        "name": "precomputed-embedding-minilm-l6-v2",
        "embedding_model": {
            "model_id": minilm_common.MODEL_ID,
            "dimension": dimension,
            "similarity_metric": "cosine",
            "pooling_mode": "mean",
            "normalized": True,
        },
        "embedding_artifact": {
            "generator_id": minilm_common.GENERATOR_ID,
            "generator_version": minilm_common.GENERATOR_VERSION,
            "dataset_revision": DATASET_REVISION,
            "generator_revision": GENERATOR_REVISION,
            "generator_source_hash": minilm_common.generator_source_hash(Path(__file__)),
            "generator_contract_source_hash": minilm_common.sha256_file(
                contract_script_path()
            ),
            "generator_command": GENERATOR_COMMAND,
            "generator_requirements_lock": minilm_common.requirements_lock_identity(),
            "model_revision": minilm_common.MODEL_REVISION,
            "tokenizer_revision": minilm_common.TOKENIZER_REVISION,
            "qrels_revision": QRELS_REVISION,
            "document_prompt_id": minilm_common.DOCUMENT_PROMPT_ID,
            "query_prompt_id": minilm_common.QUERY_PROMPT_ID,
            "projection_kind": minilm_common.PROJECTION_KIND,
            "normalization": "l2",
            "dtype": "float32",
            "hash_algorithm": "sha256",
            "config_hash": "",
            "dataset_hash": "",
            "qrels_hash": "",
            "artifact_hash": "",
        },
        "corpus": corpus,
        "queries": queries,
        "judgments": judgments,
        "document_embeddings": [
            {"id": item["id"], "vector": vector}
            for item, vector in zip(corpus, document_vectors)
        ],
        "query_embeddings": [
            {"id": query["id"], "vector": vector}
            for query, vector in zip(queries, query_vectors)
        ],
    }
    artifact = root["embedding_artifact"]
    artifact["config_hash"] = contract.sha256_hex(contract.canonical_config_text(root))
    artifact["dataset_hash"] = contract.sha256_hex(
        contract.canonical_dataset_payload(root)
    )
    artifact["qrels_hash"] = contract.sha256_hex(contract.canonical_qrels_payload(root))
    artifact["artifact_hash"] = contract.sha256_hex(
        contract.canonical_artifact_payload(root)
    )
    return root


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--local-files-only", action="store_true")
    args = parser.parse_args()

    fixture = build_fixture(
        cache_dir=args.cache_dir,
        local_files_only=args.local_files_only,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(fixture, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()
