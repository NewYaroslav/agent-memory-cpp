# Experiment Notes

This directory stores human-readable notes for exploratory work that can shape
architecture, benchmarks, or roadmap decisions.

Use this directory when a task is more than a routine benchmark run:

- it tests a hypothesis;
- it compares implementation strategies;
- it produces directional performance or quality evidence;
- it changes the roadmap or the next PR order.

## Note format

Create one Markdown note per experiment line, not per command invocation. If a
later PR continues the same research question, append a new dated section to the
existing note instead of overwriting previous results.

Each note should record:

- date and PR/commit context;
- what is being checked;
- why the question matters;
- expected result before running the experiment;
- setup, dataset, configuration, and relevant code paths;
- actual results, including tables when useful;
- interpretation and limitations;
- possible improvements;
- what should be checked next.

Generated JSON reports and raw logs are useful evidence, but the experiment note
should contain the short human interpretation that future maintainers need.

