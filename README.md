# Zinc

Zinc is a compiled, statically-typed language (`.zn`), influenced by JavaScript, Zig, and C++.
The full grammar lives in `zinc.ebnf`. Known limitations and deferred design decisions are
tracked in `FIXME.md`.

## Building

```
mkdir build && cd build
cmake ..
make
```

This produces four binaries:

- `src/zincc` — the compiler driver. Lexes, parses, type-checks, and (on success) prints the
  AST for a `.zn` file: `./src/zincc ../examples/hello.zn`
- `tests/lexer_tests`, `tests/parser_tests`, `tests/checker_tests` — manual test runners
  (no framework, PASS/FAIL per check).

## Status

- [x] Grammar (`zinc.ebnf`)
- [x] Lexer (`src/lexer/`)
- [x] Parser / AST (`src/ast/`, `src/parser/`)
- [x] Semantic analysis / type checking (`src/sema/`)
- [ ] Bytecode / codegen
- [ ] VM / runtime

