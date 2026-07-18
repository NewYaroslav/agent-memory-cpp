#!/usr/bin/env python3
"""Generate deterministic retrieval-eval synthetic datasets.

The output matches docs/eval/dataset-schema.md v1 and is intentionally small
enough to inspect by hand. Larger sweeps should be generated per seed instead
of committed to the repository.
"""

from __future__ import annotations

import argparse
import json
import random
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


QUERY_CLASSES = (
    "exact",
    "partial",
    "graded",
    "distractor_heavy",
    "oov",
    "no_answer",
    "length_bias",
    "repeated_term",
)

TOPICS = (
    ("aurora", ("ion", "plasma", "magnet", "solar")),
    ("harbor", ("dock", "cargo", "tide", "anchor")),
    ("orchard", ("apple", "pear", "branch", "harvest")),
    ("kernel", ("mutex", "thread", "syscall", "scheduler")),
    ("library", ("catalog", "shelf", "index", "archive")),
    ("desert", ("dune", "cactus", "oasis", "mirage")),
    ("studio", ("camera", "light", "scene", "audio")),
    ("forest", ("moss", "river", "canopy", "trail")),
)


@dataclass(frozen=True)
class Doc:
    id: str
    title: str
    text: str
    topic: str
    facet: str
    unique: str


def token(prefix: str, index: int) -> str:
    return f"{prefix}{index:04d}"


def make_docs(count: int) -> list[Doc]:
    docs: list[Doc] = []
    for i in range(count):
        topic, facets = TOPICS[i % len(TOPICS)]
        facet = facets[(i // len(TOPICS)) % len(facets)]
        unique = token("uniq", i)
        # Shared terms deliberately create partial and graded matches; unique
        # terms keep exact queries unambiguous.
        terms = [
            topic,
            facet,
            unique,
            "memory",
            "retrieval",
            token("group", i % 5),
        ]
        if i % 7 == 0:
            terms.extend(["common"] * 12)
        if i % 11 == 0:
            terms.extend([topic] * 5)
        docs.append(
            Doc(
                id=f"doc:{i:04d}",
                title=f"Synthetic {topic} document {i:04d}",
                text=" ".join(terms),
                topic=topic,
                facet=facet,
                unique=unique,
            )
        )
    return docs


def relevant_by_topic(docs: Iterable[Doc], topic: str, limit: int) -> list[Doc]:
    out = [doc for doc in docs if doc.topic == topic]
    return out[:limit]


def add_qrel(judgments: list[dict], qid: str, doc: Doc, grade: int) -> None:
    judgments.append(
        {"query_id": qid, "item_id": doc.id, "relevance_grade": grade}
    )


def make_query(
    docs: list[Doc],
    query_index: int,
    query_class: str,
    rng: random.Random,
    limit: int,
) -> tuple[dict, list[dict]]:
    target = docs[(query_index * 7 + 3) % len(docs)]
    qid = f"q:{query_index:04d}"
    judgments: list[dict] = []
    answer_mode = "JudgedRetrieval"

    if query_class == "exact":
        text = f"{target.topic} {target.facet} {target.unique}"
        add_qrel(judgments, qid, target, 3)
    elif query_class == "partial":
        text = f"{target.topic} {target.facet}"
        for doc in relevant_by_topic(docs, target.topic, 3):
            add_qrel(judgments, qid, doc, 1 if doc.id != target.id else 2)
    elif query_class == "graded":
        text = f"{target.topic} {target.facet} retrieval"
        add_qrel(judgments, qid, target, 3)
        for doc in relevant_by_topic(docs, target.topic, 4):
            if doc.id != target.id:
                add_qrel(judgments, qid, doc, 1)
    elif query_class == "distractor_heavy":
        distractors = rng.sample([topic for topic, _ in TOPICS], k=3)
        text = " ".join([target.unique, target.facet, *distractors, "common"])
        add_qrel(judgments, qid, target, 3)
    elif query_class == "oov":
        text = f"zzzoov{query_index} phantomterm {target.topic} {target.unique}"
        add_qrel(judgments, qid, target, 2)
    elif query_class == "no_answer":
        text = f"zzznothing{query_index} absentconcept{query_index}"
        answer_mode = "NoAnswer"
    elif query_class == "length_bias":
        text = f"{target.unique} common common common {target.topic}"
        add_qrel(judgments, qid, target, 3)
    elif query_class == "repeated_term":
        text = " ".join([target.topic] * 4 + [target.unique])
        add_qrel(judgments, qid, target, 3)
    else:
        raise ValueError(f"unknown query class: {query_class}")

    query = {
        "id": qid,
        "text": text,
        "query_type": f"Synthetic/{query_class}",
        "limit": limit,
        "answer_mode": answer_mode,
    }
    return query, judgments


def make_dataset(documents: int, queries: int, seed: int, limit: int) -> dict:
    if documents < len(QUERY_CLASSES):
        raise ValueError("documents must be at least 8")
    if queries < len(QUERY_CLASSES):
        raise ValueError("queries must be at least 8")

    rng = random.Random(seed)
    docs = make_docs(documents)
    corpus = [
        {
            "id": doc.id,
            "title": doc.title,
            "text": doc.text,
            "metadata": {"topic": doc.topic, "facet": doc.facet},
        }
        for doc in docs
    ]
    query_rows: list[dict] = []
    judgments: list[dict] = []
    for i in range(queries):
        query_class = QUERY_CLASSES[i % len(QUERY_CLASSES)]
        query, qrels = make_query(docs, i, query_class, rng, limit)
        query_rows.append(query)
        judgments.extend(qrels)
    return {
        "name": f"synthetic_v1_docs{documents}_queries{queries}_seed{seed}",
        "corpus": corpus,
        "queries": query_rows,
        "judgments": judgments,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--documents", type=int, default=50)
    parser.add_argument("--queries", type=int, default=40)
    parser.add_argument("--seed", type=int, default=12648430)
    parser.add_argument("--limit", type=int, default=50)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    dataset = make_dataset(args.documents, args.queries, args.seed, args.limit)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(dataset, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
