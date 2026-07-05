# Commit Conventions

Operational rule for AI agents that create commits in this repository.

## When To Commit

- Create commits only when the user asks for committed work or a PR.
- Check `git status --short` and inspect the diff before committing.
- Include only files related to the requested change.

## Format

- Commit messages must be in English and use Conventional Commits.
- Use the form `type(scope): short summary` when a scope is useful.
- Use an imperative, concise summary.
- Include a descriptive body for non-trivial commits.

Example:

```bash
git commit -m "docs(guides): add agent workflow baseline" -m "Split project-local agent instructions into topic guides and keep AGENTS.md as the compact entry point."
```

## Allowed Types

- `feat` - new functionality.
- `fix` - bug fix.
- `refactor` - refactoring without behavior changes.
- `perf` - performance improvements.
- `test` - add or modify tests.
- `docs` - documentation changes.
- `build` - build system or dependency updates.
- `ci` - CI/CD configuration changes.
- `chore` - maintenance tasks that do not affect production code behavior.
