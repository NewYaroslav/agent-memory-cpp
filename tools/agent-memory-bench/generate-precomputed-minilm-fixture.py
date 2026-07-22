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
import json
import platform
import struct
import sys
from pathlib import Path
from typing import Any

sys.dont_write_bytecode = True

import precomputed_fixture_contract as contract


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
REQUIREMENTS_LOCK_FILE = "requirements-minilm-fixture.txt"
REQUIRED_PACKAGE_PINS = (
    "transformers",
    "torch",
    "numpy",
    "tokenizers",
    "safetensors",
    "huggingface-hub",
)
DATASET_REVISION = "agent-memory-minilm-fixture:2026-07-22"
QRELS_REVISION = "agent-memory-minilm-qrels:2026-07-22"
DOCUMENT_PROMPT_ID = "title-plus-text-v1"
QUERY_PROMPT_ID = "query-text-v1"
PROJECTION_KIND = "minilm_l6_v2_mean_pool_normalized"


def contract_script_path() -> Path:
    return Path(__file__).with_name("precomputed_fixture_contract.py")


def requirements_lock_path() -> Path:
    return Path(__file__).with_name(REQUIREMENTS_LOCK_FILE)


def requirements_lock_identity() -> str:
    return (
        f"tools/agent-memory-bench/{REQUIREMENTS_LOCK_FILE};"
        f"sha256={sha256_file(requirements_lock_path())}"
    )


def parse_requirements_lock() -> tuple[str, dict[str, str]]:
    python_version = ""
    packages: dict[str, str] = {}
    for line_number, raw_line in enumerate(
        requirements_lock_path().read_text(encoding="utf-8").splitlines(),
        start=1,
    ):
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("#"):
            marker = "# python-version:"
            if line.startswith(marker):
                python_version = line[len(marker):].strip()
            continue
        if "==" not in line:
            raise RuntimeError(
                f"{REQUIREMENTS_LOCK_FILE}:{line_number}: expected package==version"
            )
        package, version = line.split("==", 1)
        package = package.strip()
        version = version.strip()
        if not package or not version:
            raise RuntimeError(
                f"{REQUIREMENTS_LOCK_FILE}:{line_number}: expected package==version"
            )
        if package in packages:
            raise RuntimeError(
                f"{REQUIREMENTS_LOCK_FILE}:{line_number}: duplicate package {package}"
            )
        packages[package] = version
    if not python_version:
        raise RuntimeError(f"{REQUIREMENTS_LOCK_FILE}: missing # python-version")
    if not packages:
        raise RuntimeError(f"{REQUIREMENTS_LOCK_FILE}: missing package pins")
    return python_version, packages


def verify_requirements_lock_environment() -> None:
    expected_python, packages = parse_requirements_lock()
    actual_python = platform.python_version()
    if actual_python != expected_python:
        raise RuntimeError(
            f"Python version mismatch: expected {expected_python}, "
            f"got {actual_python}"
        )
    for package in REQUIRED_PACKAGE_PINS:
        if package not in packages:
            raise RuntimeError(
                f"{REQUIREMENTS_LOCK_FILE}: missing required package pin {package}"
            )
    for package, expected in packages.items():
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
    verify_requirements_lock_environment()
    try:
        import torch
        import torch.nn.functional as torch_functional
        from transformers import AutoModel, AutoTokenizer
    except ImportError as exc:
        raise RuntimeError(
            "MiniLM fixture generation requires torch and transformers. "
            f"Install the pinned packages from {REQUIREMENTS_LOCK_FILE}."
        ) from exc

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
            "generator_requirements_lock": requirements_lock_identity(),
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
