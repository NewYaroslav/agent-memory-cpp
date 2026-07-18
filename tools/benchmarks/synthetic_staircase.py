#!/usr/bin/env python3
"""Run a deterministic synthetic benchmark staircase and summarize it.

The script intentionally writes generated datasets, benchmark configs, and raw
JSON reports under an output directory such as `tmp/synthetic-staircase-v1/`.
Only the optional Markdown summary is meant to be committed when it informs a
repository decision.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_SIZES = (1000, 2500, 5000)
DEFAULT_QUERY_COUNT = 80
DEFAULT_SEED = 12648430
DEFAULT_LIMIT = 50
DEFAULT_BASELINES = ("bow_vector", "bm25_exact")


@dataclass(frozen=True)
class StaircaseCase:
    documents: int
    queries: int
    seed: int


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def parse_csv_ints(raw: str, field_name: str) -> list[int]:
    values: list[int] = []
    for part in raw.split(","):
        stripped = part.strip()
        if not stripped:
            continue
        try:
            value = int(stripped)
        except ValueError as exc:
            raise ValueError(f"{field_name} must contain only integers") from exc
        if value <= 0:
            raise ValueError(f"{field_name} entries must be positive")
        values.append(value)
    if not values:
        raise ValueError(f"{field_name} must contain at least one value")
    return values


def parse_baselines(raw: str) -> list[str]:
    baselines = [part.strip() for part in raw.split(",") if part.strip()]
    if not baselines:
        raise ValueError("baselines must contain at least one value")
    if len(set(baselines)) != len(baselines):
        raise ValueError("baselines must not contain duplicates")
    return baselines


def build_cases(args: argparse.Namespace) -> list[StaircaseCase]:
    sizes = parse_csv_ints(args.sizes, "sizes")
    query_counts = parse_csv_ints(args.queries, "queries")
    if len(query_counts) == 1:
        query_counts = query_counts * len(sizes)
    if len(query_counts) != len(sizes):
        raise ValueError(
            "queries must contain either one value or exactly one value per size"
        )
    return [
        StaircaseCase(documents=size, queries=query_count, seed=args.seed)
        for size, query_count in zip(sizes, query_counts)
    ]


def resolve_existing_file(path: Path, root: Path, description: str) -> Path:
    candidate = path if path.is_absolute() else root / path
    candidate = candidate.resolve()
    if not candidate.is_file():
        raise FileNotFoundError(f"{description} does not exist: {candidate}")
    return candidate


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def run_command(command: list[str], cwd: Path) -> None:
    print("$ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def generate_dataset(
    generator: Path,
    output_path: Path,
    case: StaircaseCase,
    limit: int,
    cwd: Path,
) -> None:
    run_command(
        [
            sys.executable,
            str(generator),
            "--documents",
            str(case.documents),
            "--queries",
            str(case.queries),
            "--seed",
            str(case.seed),
            "--limit",
            str(limit),
            "--output",
            str(output_path),
        ],
        cwd,
    )


def run_benchmark(
    bench_exe: Path,
    config_path: Path,
    output_path: Path,
    cwd: Path,
) -> dict[str, Any]:
    run_command([str(bench_exe), str(config_path), str(output_path)], cwd)
    return json.loads(output_path.read_text(encoding="utf-8"))


def require_mapping(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise TypeError(f"{context} must be a JSON object")
    return value


def require_list(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise TypeError(f"{context} must be a JSON array")
    return value


def require_field(mapping: dict[str, Any], field: str, context: str) -> Any:
    if field not in mapping:
        raise KeyError(f"{context} is missing required field '{field}'")
    return mapping[field]


def require_string_field(mapping: dict[str, Any], field: str, context: str) -> str:
    value = require_field(mapping, field, context)
    if not isinstance(value, str):
        raise TypeError(f"{context}.{field} must be a string")
    return value


def require_numeric_field(mapping: dict[str, Any], field: str, context: str) -> float:
    value = require_field(mapping, field, context)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TypeError(f"{context}.{field} must be numeric")
    return float(value)


def require_integer_field(mapping: dict[str, Any], field: str, context: str) -> int:
    value = require_field(mapping, field, context)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TypeError(f"{context}.{field} must be an integer")
    if isinstance(value, float) and not value.is_integer():
        raise TypeError(f"{context}.{field} must be an integer")
    return int(value)


def validate_sweep_report(
    report: dict[str, Any],
    case: StaircaseCase,
    baselines: list[str],
    report_path: Path,
) -> None:
    context = f"report {report_path}"
    if require_integer_field(report, "schema_version", context) != 1:
        raise ValueError(f"{context}.schema_version must be 1")
    if require_integer_field(report, "corpus_size", context) != case.documents:
        raise ValueError(
            f"{context}.corpus_size does not match expected {case.documents}"
        )
    if require_integer_field(report, "query_count", context) != case.queries:
        raise ValueError(
            f"{context}.query_count does not match expected {case.queries}"
        )

    report_rows = require_list(
        require_field(report, "reports", context),
        f"{context}.reports",
    )
    if len(report_rows) != len(baselines):
        raise ValueError(
            f"{context}.reports count {len(report_rows)} does not match "
            f"requested baselines count {len(baselines)}"
        )

    actual_baselines: list[str] = []
    for index, raw_baseline_report in enumerate(report_rows):
        row_context = f"{context}.reports[{index}]"
        baseline_report = require_mapping(raw_baseline_report, row_context)
        actual_baselines.append(
            require_string_field(baseline_report, "baseline_name", row_context)
        )
        quality = require_mapping(
            require_field(baseline_report, "quality", row_context),
            f"{row_context}.quality",
        )
        speed = require_mapping(
            require_field(baseline_report, "speed", row_context),
            f"{row_context}.speed",
        )
        index_metrics = require_mapping(
            require_field(baseline_report, "index", row_context),
            f"{row_context}.index",
        )
        for field in ("recall_at_10", "ndcg_at_10", "mrr"):
            require_numeric_field(quality, field, f"{row_context}.quality")
        for field in ("p95_latency_ms", "queries_per_second"):
            require_numeric_field(speed, field, f"{row_context}.speed")
        for field in ("index_build_time_ms", "vocabulary_size"):
            require_numeric_field(index_metrics, field, f"{row_context}.index")

    if actual_baselines != baselines:
        raise ValueError(
            f"{context}.reports baseline order {actual_baselines} does not "
            f"match requested order {baselines}"
        )

    if len(baselines) >= 2:
        comparison = require_mapping(
            require_field(report, "comparison", context),
            f"{context}.comparison",
        )
        first = require_string_field(
            comparison,
            "first_baseline",
            f"{context}.comparison",
        )
        second = require_string_field(
            comparison,
            "second_baseline",
            f"{context}.comparison",
        )
        if [first, second] != baselines[:2]:
            raise ValueError(
                f"{context}.comparison baseline pair {[first, second]} does "
                f"not match requested first baselines {baselines[:2]}"
            )
        require_numeric_field(
            comparison,
            "recall_at_10_delta_second_minus_first",
            f"{context}.comparison",
        )
        require_numeric_field(
            comparison,
            "p95_latency_ms_delta_second_minus_first",
            f"{context}.comparison",
        )


def number(value: Any) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    raise TypeError(f"expected numeric JSON value, got {type(value).__name__}")


def fmt(value: Any, digits: int = 3) -> str:
    return f"{number(value):.{digits}f}"


def display_path(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def make_summary(
    reports: list[dict[str, Any]],
    cases: list[StaircaseCase],
    baselines: list[str],
    limit: int,
    output_dir_label: str,
) -> str:
    lines: list[str] = [
        "# Synthetic staircase benchmark v1",
        "",
        "This report is generated by `tools/benchmarks/synthetic_staircase.py`.",
        "Generated datasets and raw JSON reports are temporary artifacts and are",
        "not intended to be committed.",
        "",
        "## Configuration",
        "",
        f"- Corpus sizes: {', '.join(str(case.documents) for case in cases)}",
        f"- Queries per size: {', '.join(str(case.queries) for case in cases)}",
        f"- Seed: {cases[0].seed if cases else DEFAULT_SEED}",
        f"- Result limit: {limit}",
        f"- Baselines: {', '.join(f'`{baseline}`' for baseline in baselines)}",
        f"- Raw artifact directory: `{output_dir_label}`",
        "",
        "## Quality and latency",
        "",
        "| Documents | Queries | Baseline | Recall@10 | nDCG@10 | MRR | p95 latency ms | QPS | Build ms | Vocabulary |",
        "| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    for report in reports:
        for baseline_report in report["reports"]:
            quality = baseline_report["quality"]
            speed = baseline_report["speed"]
            index = baseline_report["index"]
            lines.append(
                "| "
                f"{report['corpus_size']} | "
                f"{report['query_count']} | "
                f"`{baseline_report['baseline_name']}` | "
                f"{fmt(quality['recall_at_10'])} | "
                f"{fmt(quality['ndcg_at_10'])} | "
                f"{fmt(quality['mrr'])} | "
                f"{fmt(speed['p95_latency_ms'])} | "
                f"{fmt(speed['queries_per_second'], 1)} | "
                f"{fmt(index['index_build_time_ms'])} | "
                f"{int(number(index['vocabulary_size']))} |"
            )

    comparisons = [
        (report, comparison)
        for report in reports
        if isinstance((comparison := report.get("comparison")), dict)
    ]
    if comparisons:
        lines.extend(
            [
                "",
                "## Baseline deltas",
                "",
                "Deltas use the order recorded in each sweep report:",
                "`second_baseline - first_baseline`.",
                "",
                "| Documents | First baseline | Second baseline | Recall@10 delta | p95 latency delta ms |",
                "| ---: | --- | --- | ---: | ---: |",
            ]
        )
        for report, comparison in comparisons:
            lines.append(
                "| "
                f"{report['corpus_size']} | "
                f"`{comparison['first_baseline']}` | "
                f"`{comparison['second_baseline']}` | "
                f"{fmt(comparison['recall_at_10_delta_second_minus_first'])} | "
                f"{fmt(comparison['p95_latency_ms_delta_second_minus_first'])} |"
            )

    lines.extend(
        [
            "",
            "## Notes",
            "",
            "- This is a synthetic lexical sanity sweep, not a production-quality",
            "  benchmark or acceptance threshold.",
            "- Timing values come from one local run and are directional, not",
            "  statistically stable benchmark results.",
            "- `bow_vector` uses dense bag-of-words vectors with brute-force exact",
            "  top-K search, so growth in corpus size also increases vector",
            "  dimensionality for this generator.",
            "- Per-baseline peak RSS remains `0` for in-process synthetic sweeps;",
            "  memory attribution requires subprocess isolation or another",
            "  dedicated measurement path.",
            "",
        ]
    )
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bench-exe",
        type=Path,
        required=True,
        help="Path to the built agent-memory-bench executable.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("tmp/synthetic-staircase-v1"),
        help="Directory for generated datasets, configs, and raw JSON reports.",
    )
    parser.add_argument(
        "--summary",
        type=Path,
        default=None,
        help="Markdown summary path. Defaults to <output-dir>/synthetic-staircase-v1.md.",
    )
    parser.add_argument(
        "--sizes",
        default=",".join(str(size) for size in DEFAULT_SIZES),
        help="Comma-separated corpus sizes.",
    )
    parser.add_argument(
        "--queries",
        default=str(DEFAULT_QUERY_COUNT),
        help="One query count for every size, or comma-separated per-size counts.",
    )
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--limit", type=int, default=DEFAULT_LIMIT)
    parser.add_argument(
        "--baselines",
        default=",".join(DEFAULT_BASELINES),
        help="Comma-separated baseline names passed to synthetic_sweep.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = repo_root()
    cases = build_cases(args)
    baselines = parse_baselines(args.baselines)
    bench_exe = resolve_existing_file(args.bench_exe, root, "benchmark executable")
    generator = resolve_existing_file(
        Path("tools/benchmarks/synthetic_generator.py"),
        root,
        "synthetic generator",
    )
    output_dir = (
        args.output_dir if args.output_dir.is_absolute() else root / args.output_dir
    ).resolve()
    summary_path = args.summary or output_dir / "synthetic-staircase-v1.md"
    summary_path = (
        summary_path if summary_path.is_absolute() else root / summary_path
    ).resolve()

    reports: list[dict[str, Any]] = []
    for case in cases:
        stem = f"docs{case.documents}_queries{case.queries}_seed{case.seed}"
        dataset_path = output_dir / f"{stem}.dataset.json"
        config_path = output_dir / f"{stem}.config.json"
        report_path = output_dir / f"{stem}.report.json"
        generate_dataset(generator, dataset_path, case, args.limit, root)
        write_json(
            config_path,
            {
                "mode": "synthetic_sweep",
                "benchmark_name": f"synthetic_staircase_v1_docs{case.documents}",
                "dataset_path": str(dataset_path),
                "result_limit": args.limit,
                "baselines": baselines,
                "output_path": str(report_path),
            },
        )
        report = run_benchmark(bench_exe, config_path, report_path, root)
        validate_sweep_report(report, case, baselines, report_path)
        reports.append(report)

    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(
        make_summary(
            reports,
            cases,
            baselines,
            args.limit,
            display_path(output_dir, root),
        ),
        encoding="utf-8",
    )
    print(f"Summary output: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
