"""Shared runtime helpers for frozen MiniLM embedding fixture generators.

The committed MiniLM fixtures intentionally do not regenerate vectors in CI.
This module keeps the offline generator runtime in one place so the small and
large fixtures do not drift in environment validation, model loading, pooling,
normalization, or float32 narrowing.
"""

from __future__ import annotations

import hashlib
import importlib.metadata
import platform
import struct
from pathlib import Path
from typing import Any


MODEL_ID = "sentence-transformers/all-MiniLM-L6-v2"
MODEL_REVISION = "1110a243fdf4706b3f48f1d95db1a4f5529b4d41"
TOKENIZER_REVISION = MODEL_REVISION
GENERATOR_ID = "agent-memory.tools.minilm-precomputed-embedding"
GENERATOR_VERSION = "v1"
REQUIREMENTS_LOCK_FILE = "requirements-minilm-fixture.txt"
REQUIRED_PACKAGE_PINS = (
    "transformers",
    "torch",
    "numpy",
    "tokenizers",
    "safetensors",
    "huggingface-hub",
)
DOCUMENT_PROMPT_ID = "title-plus-text-v1"
QUERY_PROMPT_ID = "query-text-v1"
PROJECTION_KIND = "minilm_l6_v2_mean_pool_normalized"


def script_dir() -> Path:
    return Path(__file__).resolve().parent


def requirements_lock_path() -> Path:
    return script_dir() / REQUIREMENTS_LOCK_FILE


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sha256_source_files(paths: list[Path]) -> str:
    """Return a stable source identity for a list of generator source files."""

    payload = "".join(f"{sha256_file(path)}\n" for path in paths).encode("ascii")
    return hashlib.sha256(payload).hexdigest()


def generator_source_hash(driver_script: Path) -> str:
    """Hash the thin generator driver plus this shared runtime module."""

    return sha256_source_files([driver_script, Path(__file__)])


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
            f"Python version mismatch: expected {expected_python}, got {actual_python}"
        )
    for package in REQUIRED_PACKAGE_PINS:
        if package not in packages:
            raise RuntimeError(
                f"{REQUIREMENTS_LOCK_FILE}: missing required package pin {package}"
            )
    for package, expected in packages.items():
        try:
            actual = importlib.metadata.version(package)
        except importlib.metadata.PackageNotFoundError as exc:
            raise RuntimeError(f"required package is not installed: {package}") from exc
        if actual != expected:
            raise RuntimeError(
                f"{package} version mismatch: expected {expected}, got {actual}"
            )


def f32_value(value: float) -> float:
    narrowed = struct.unpack("<f", struct.pack("<f", float(value)))[0]
    return 0.0 if narrowed == 0.0 else float(narrowed)


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
