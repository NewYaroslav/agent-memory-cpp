# Architecture

## Baseline

Use a DDD-like architecture with small domain slices and explicit dependency
boundaries. The project is a static C++17 library first; optional infrastructure
should not leak into core APIs.

## Dependency Direction

Dependencies should point inward:

```text
infrastructure/adapters -> storage/index/embedding implementations
retrieval/memory/context -> domain contracts and value objects
domain/core -> standard library and stable internal primitives only
```

Rules:

- Core and domain code must not depend on MDBX, ONNX Runtime, HTTP clients,
  Python, or other optional infrastructure.
- Storage adapters implement storage contracts; they do not define memory
  strategy semantics.
- Embedding adapters implement embedding contracts; they do not own retrieval
  ranking policy.
- Context assembly consumes retrieval results and memory policies; it should not
  perform storage-specific queries directly.

## Static Library Bias

The main target is `agent_memory`, built as a static library. Prefer `.cpp`
implementation files for behavior that may depend on external libraries or
change ABI/compile-time cost. Header-only code is acceptable for small value
types, templates, and trivial inline helpers.

## Boundary Guidelines

- Put stable value objects and contracts near the domain they describe.
- Put dependency-owning implementations under `infrastructure/` or a
  dependency-specific subarea.
- Keep public APIs small and easy to inspect.
- Avoid one large facade that hides all behavior.
- Avoid speculative plugin systems before at least two real implementations
  require the same extension point.

## Planned Storage Direction

Storage contracts live under `src/agent_memory/storage/` and stay dependency
free. MDBX-backed storage is expected to implement those contracts with
`mdbx-containers` as an adapter detail. The upper project should keep
dependencies flat: parent projects should be able to provide dependency targets
when possible.

MDBX support is opt-in through `AGENT_MEMORY_ENABLE_MDBX`. The build must reuse
an existing `mdbx_containers::mdbx_containers` target before adding a local
source tree or falling back to `find_package(mdbx_containers)`.
