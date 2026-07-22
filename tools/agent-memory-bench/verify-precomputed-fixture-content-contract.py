#!/usr/bin/env python3
"""Verify that a frozen fixture still matches its source content contract."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path
from types import ModuleType

sys.dont_write_bytecode = True


def load_contract(path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location("fixture_content_contract", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load content contract: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_equal(name: str, fixture_value: object, contract_value: object) -> None:
    if fixture_value != contract_value:
        raise RuntimeError(f"fixture {name} does not match content contract")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--contract", required=True, type=Path)
    args = parser.parse_args()

    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    contract = load_contract(args.contract)

    require_equal("corpus", fixture.get("corpus"), contract.CORPUS)
    require_equal("queries", fixture.get("queries"), contract.QUERIES)
    require_equal("judgments", fixture.get("judgments"), contract.JUDGMENTS)


if __name__ == "__main__":
    main()
