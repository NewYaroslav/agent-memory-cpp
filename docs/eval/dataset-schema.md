# Retrieval Evaluation Dataset Schema (v1)

Pins the in-memory shape of `agent_memory::RetrievalEvalDataset` so the JSON /
file loader in PR #27 cannot reshape the contract that `evaluate_retrieval`
already depends on. This is the same shape `StubDataset` produces and
`run_retriever` consumes today.

## Top-level record

```text
RetrievalEvalDataset {
    string                name
    EvalCorpusItem[]      corpus
    EvalQuery[]           queries
    RelevanceJudgment[]   judgments
}
```

## `EvalCorpusItem`

```text
EvalCorpusItem {
    string   id          // unique corpus item id
    string   title
    string   text
    Metadata metadata    // optional; may be empty
}
```

`id` is the value `RetrievedChunk.chunk.id.value()` returns on the retrieval
side and `RelevanceJudgment.item_id` references. Title and text are
informational; the eval pipeline does not parse them today.

## `EvalQuery`

```text
EvalQuery {
    string                 id
    string                 text
    string                 query_type
    size_t                 limit              // default 10
    MetadataFilter[]       metadata_filters   // optional
    EvalQueryAnswerMode    answer_mode        // default JudgedRetrieval
}
```

```cpp
enum class EvalQueryAnswerMode { JudgedRetrieval, NoAnswer, Ignore };
```

- `JudgedRetrieval` — must have at least one positive qrel; participates in
  Recall@K, nDCG@K, MRR.
- `NoAnswer` — must carry zero positive qrels, but zero-grade qrels
  (explicit non-relevant markings) are allowed; participates only in
  `no_answer_accuracy`.
- `Ignore` — counted in `ignored_query_count`, otherwise skipped.

## `RelevanceJudgment`

```text
RelevanceJudgment {
    string    query_id
    string    item_id
    int32_t   relevance_grade
}
```

`relevance_grade > 0` is relevant. `0` is judged non-relevant. Negative grades
are invalid. `JudgedRetrieval` queries must have at least one positive grade.
Duplicate `(query_id, item_id)` pairs and duplicate query ids are rejected.

## Worked example

The synthetic fixture produced by `StubDataset` is the canonical example
(corpus_size=64, query_count=24, seed=0xC0FFEE):
- `dataset.queries[i].text == "id:doc:<j>"` for ~75% of queries.
- `dataset.queries[i].text == "noise:<i>"` for the remaining ~25%.
- `dataset.judgments[i].item_id` matches the `id:` text where applicable.

## Versioning

- **v1** (frozen by PR #26) — in-memory shape only.
- **v2** (PR #27+) — file/JSON loader mirroring this shape byte-for-byte. The
  on-disk schema is intentionally out of scope for v1.