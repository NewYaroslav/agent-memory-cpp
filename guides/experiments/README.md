# Experiment Notes

`guides/experiments/` stores human-readable experiment records. These notes are
not raw benchmark dumps; they are compact research logs that explain why a run
was performed, what was expected, what happened, and what should be checked
next.

## When to write or update a note

Create or update an experiment note when a PR:

- tests a hypothesis;
- compares algorithms, encoders, indexes, storage layouts, or benchmark
  methodology;
- produces benchmark numbers that influence the roadmap;
- changes the interpretation of earlier benchmark results.

Create one note per research line, not per command invocation. If a later PR
continues the same question, append a new dated section instead of overwriting
earlier results.

## Required contents

Each note should include:

- date and PR/commit context;
- question or hypothesis;
- setup and command/config references;
- expected result;
- actual result, preferably with compact tables;
- interpretation;
- limitations and threats to validity;
- possible improvements;
- follow-up checks.

## Raw artifact policy

Do not commit large generated JSON reports by default. Commit only:

- small, stable smoke fixtures;
- example configs;
- manually curated tables or short excerpts needed to support the note.

When raw reports matter, store the command, config path, output path, git head,
and enough identifying metadata for reproduction. If a future PR needs
long-term raw artifact retention, add an explicit policy for artifact location,
size budget, and cleanup before committing dumps.

## Timing methodology

Experiment notes must distinguish:

- data generation;
- exact baseline build and query timing;
- encoder training/cold-start timing;
- binary materialization/build timing;
- query encoding;
- candidate search;
- exact rerank;
- process-wide memory high-water marks.

Timing values from a single local run are directional. Treat them as stable
benchmark evidence only after the harness uses repeated runs, warm-up rules,
fixed environment notes, and preserved raw outputs.
