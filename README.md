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

## Layout

```
zinc/
├── CMakeLists.txt
├── zinc.ebnf              the language grammar
├── FIXME.md                known limitations & deferred design decisions
├── examples/
│   └── hello.zn             sample program exercising most of the grammar
├── src/
│   ├── main.cpp              zincc driver
│   ├── lexer/                 source text -> token stream
│   ├── ast/                   AST node definitions + printer
│   ├── parser/                 token stream -> AST (recursive descent, panic-mode recovery)
│   └── sema/                   type checker (Type, Checker)
└── tests/
    ├── lexer_tests.cpp
    ├── parser_tests.cpp
    └── checker_tests.cpp
```
