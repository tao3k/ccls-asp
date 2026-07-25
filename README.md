# ccls-asp

`ccls-asp` is the compiler-native C-family provider for Agent Semantic
Protocols. It is derived from ccls' Clang indexing lineage, but it is not an
LSP server.

The fork intentionally removes JSON-RPC, editor lifecycle state, completion,
hover, rename, formatting, diagnostic pushes, semantic-token pushes, and
document synchronization. The retained product boundary is:

- compilation database fidelity;
- Clang AST declarations, definitions, calls, types, inheritance, and
  Objective-C entities;
- ASP `search`, `query`, `check`, and `guide` commands;
- schema-owned JSON packets and compact agent-facing projections.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Commands

```sh
build/ccls-asp --language c search prime --workspace . --view seeds
build/ccls-asp --language cpp search owner src/widget.cc items --workspace . --json
build/ccls-asp --language objective-c search lexical Controller owner tests --workspace . --json
build/ccls-asp --language cpp query --selector src/widget.cc:10:40 --workspace . --code
```

Supported public language identities are `c`, `cpp`, and `objective-c`.

## Schema ownership

The shared packet schemas in `schemas/` are synchronized from
`agent-semantic-protocols`; this repository does not evolve them independently.
From the protocol repository, run:

```sh
python -m tools schema profiles validate c-family
python -m tools schema profiles sync c-family
```

The root profile gate rejects missing, extra, or drifted provider-local copies.
