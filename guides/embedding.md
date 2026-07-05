# Embeddings

## Direction

Embedding generation is a first-class project boundary. Keep the public API
backend-independent and implement concrete providers as optional adapters.

Do not fork `cpp-llamalib` to retrofit embedding support. It can be useful as a
reference for small RAII wrappers around `llama.cpp`, but chat/generation
wrappers are not the right public abstraction for embeddings. Embedding models
need explicit handling of batch inputs, pooling, normalization, vector
dimensions, model metadata, and query-vs-document usage.

## Contract First

Add dependency-free embedding contracts under `src/agent_memory/embedding/`
before adding any concrete backend. The contracts should be usable by storage,
indexing, and retrieval code without including ONNX Runtime, `llama.cpp`, HTTP
clients, or Python headers.

The contract layer should model:

- embedding vector values and dimensions;
- model identifier and limits;
- similarity metric expected by the model;
- whether returned vectors are normalized;
- embedding purpose, such as query, document, or symmetric text embedding.

## Backends

Concrete backends should live behind adapter boundaries, for example:

- `LlamaCppEmbedder` for local `llama.cpp` embedding models;
- `OnnxRuntimeEmbedder` for local ONNX Runtime models;
- `OpenAICompatibleEmbedder` for HTTP-compatible embedding APIs;
- user-provided implementations of the embedding interface.

Each backend must keep its dependency wiring optional and must not leak backend
types into dependency-free contracts.
