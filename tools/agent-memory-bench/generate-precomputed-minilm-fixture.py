#!/usr/bin/env python3
"""Generate a frozen MiniLM precomputed embedding benchmark fixture.

This script is intentionally not part of the default CI path: it downloads and
runs a third-party transformer model. CI consumes the committed JSON artifact
and verifies its hashes instead.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
import struct
import sys
from pathlib import Path
from typing import Any

sys.dont_write_bytecode = True


MODEL_ID = "sentence-transformers/all-MiniLM-L6-v2"
MODEL_REVISION = "1110a243fdf4706b3f48f1d95db1a4f5529b4d41"
TOKENIZER_REVISION = MODEL_REVISION
GENERATOR_ID = "agent-memory.tools.minilm-precomputed-embedding"
GENERATOR_VERSION = "v1"
GENERATOR_REVISION = "agent-memory-cpp:minilm-l6-v2-fixture-v1"
GENERATOR_COMMAND = (
    "python tools/agent-memory-bench/generate-precomputed-minilm-fixture.py "
    "--output tests/eval/fixtures/precomputed-embedding-minilm-l6-v2.json"
)
GENERATOR_REQUIREMENTS_LOCK = (
    "python==3.12.13; transformers==4.44.2; torch==2.8.0; "
    "numpy==2.5.1; tokenizers==0.19.1; safetensors==0.4.5; "
    "huggingface-hub==0.24.6"
)
DATASET_REVISION = "agent-memory-minilm-fixture:2026-07-22"
QRELS_REVISION = "agent-memory-minilm-qrels:2026-07-22"
DOCUMENT_PROMPT_ID = "title-plus-text-v1"
QUERY_PROMPT_ID = "query-text-v1"
PROJECTION_KIND = "minilm_l6_v2_mean_pool_normalized"


def contract_script_path() -> Path:
    return Path(__file__).with_name("generate-precomputed-external-hash-fixture.py")


def load_contract_module() -> Any:
    script = contract_script_path()
    spec = importlib.util.spec_from_file_location(
        "agent_memory_precomputed_external_hash_fixture",
        script,
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load shared fixture contract from {script}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_version(package: str, expected: str) -> None:
    actual = importlib.metadata.version(package)
    if actual != expected:
        raise RuntimeError(
            f"{package} version mismatch: expected {expected}, got {actual}"
        )


def f32_value(value: float) -> float:
    narrowed = struct.unpack("<f", struct.pack("<f", float(value)))[0]
    return 0.0 if narrowed == 0.0 else float(narrowed)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def encode_texts(
    texts: list[str],
    *,
    cache_dir: Path | None,
    local_files_only: bool,
) -> list[list[float]]:
    try:
        import torch
        import torch.nn.functional as torch_functional
        from transformers import AutoModel, AutoTokenizer
    except ImportError as exc:
        raise RuntimeError(
            "MiniLM fixture generation requires torch and transformers. "
            "Install the pinned packages from GENERATOR_REQUIREMENTS_LOCK."
        ) from exc

    require_version("transformers", "4.44.2")
    require_version("torch", "2.8.0")
    require_version("numpy", "2.5.1")
    require_version("tokenizers", "0.19.1")
    require_version("safetensors", "0.4.5")
    require_version("huggingface-hub", "0.24.6")

    torch.set_num_threads(1)
    model_kwargs: dict[str, Any] = {
        "revision": MODEL_REVISION,
        "local_files_only": local_files_only,
    }
    tokenizer_kwargs: dict[str, Any] = {
        "revision": TOKENIZER_REVISION,
        "local_files_only": local_files_only,
    }
    if cache_dir is not None:
        model_kwargs["cache_dir"] = str(cache_dir)
        tokenizer_kwargs["cache_dir"] = str(cache_dir)

    tokenizer = AutoTokenizer.from_pretrained(MODEL_ID, **tokenizer_kwargs)
    model = AutoModel.from_pretrained(MODEL_ID, **model_kwargs)
    model.eval()

    vectors: list[list[float]] = []
    with torch.no_grad():
        for text in texts:
            encoded = tokenizer(
                text,
                padding=True,
                truncation=True,
                max_length=256,
                return_tensors="pt",
            )
            outputs = model(**encoded)
            token_embeddings = outputs.last_hidden_state
            attention_mask = encoded["attention_mask"].unsqueeze(-1).to(
                token_embeddings.dtype
            )
            summed = (token_embeddings * attention_mask).sum(dim=1)
            counts = attention_mask.sum(dim=1).clamp(min=1.0e-9)
            pooled = summed / counts
            normalized = torch_functional.normalize(pooled, p=2, dim=1)
            vector = normalized[0].cpu().tolist()
            vectors.append([f32_value(value) for value in vector])
    return vectors


def build_fixture(
    *,
    cache_dir: Path | None,
    local_files_only: bool,
) -> dict[str, Any]:
    contract = load_contract_module()
    corpus = contract.CORPUS
    queries = contract.QUERIES
    judgments = contract.JUDGMENTS

    document_texts = [f"{item['title']}\n{item['text']}" for item in corpus]
    query_texts = [query["text"] for query in queries]
    vectors = encode_texts(
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
            "model_id": MODEL_ID,
            "dimension": dimension,
            "similarity_metric": "cosine",
            "pooling_mode": "mean",
            "normalized": True,
        },
        "embedding_artifact": {
            "generator_id": GENERATOR_ID,
            "generator_version": GENERATOR_VERSION,
            "dataset_revision": DATASET_REVISION,
            "generator_revision": GENERATOR_REVISION,
            "generator_source_hash": sha256_file(Path(__file__)),
            "generator_contract_source_hash": sha256_file(contract_script_path()),
            "generator_command": GENERATOR_COMMAND,
            "generator_requirements_lock": GENERATOR_REQUIREMENTS_LOCK,
            "model_revision": MODEL_REVISION,
            "tokenizer_revision": TOKENIZER_REVISION,
            "qrels_revision": QRELS_REVISION,
            "document_prompt_id": DOCUMENT_PROMPT_ID,
            "query_prompt_id": QUERY_PROMPT_ID,
            "projection_kind": PROJECTION_KIND,
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
