# ZINC - FIXME / Known Limitations

This file tracks unresolved design decisions, known limitations, and temporary implementation choices.

---

## 1. String Concatenation

`+` supports both numeric addition and string concatenation.

```zinc
"a" + "b"
```

produces `"ab"`. Mixed operands are now supported too: if *either* side of `+` is a `str`, the
other side is converted via the same stringification used by template interpolation, and the
result is `str`.

```zinc
let score = 420;
let message = "Score: " + score; // "Score: 420"
```

Both sides must still be non-nullable at this point (the existing null-safety narrowing rules
apply) - a `str?`/`T?` needs a null check before it can be used here.

Template strings remain supported and are still the preferred way to build formatted strings
from mixed values:

```zinc
`Hello {name}!`
```

**Status:** Implemented (`Checker::checkExpr`/`Interpreter::evalExpr`, `ExprKind::Binary`, op `"+"`).

---

## 2. Null-Safety Narrowing

Null-safety narrowing is currently **pattern-based rather than full flow analysis**.

The checker should recognize these patterns:

### Pattern 1 — Null check

```zinc
if x != null {
    // x is treated as non-null here
}
```

### Pattern 2 — Early exit

```zinc
if x == null {
    return;
}

// x is treated as non-null here
```

The checker does **not** currently need to track narrowing through complex `&&` chains, loops, or arbitrary control-flow paths.

This is an intentional known limitation rather than something the checker should pretend to handle completely.

**Status:** Implemented, and this is the intended scope (not a stopgap to be expanded by default — widening it to full flow analysis would be a separate decision).

---

## 3. Object Construction

Object construction syntax has not been decided.

There is currently no `new` keyword or constructor-call syntax defined in the grammar.

For the initial type checker implementation, calling a class name will be treated as invoking that class's constructor:

```zinc
Bubble("Example", 100)
```

This is similar to Python/Kotlin-style construction. There is also no `super()`/constructor-chaining
syntax decided, so only the most-derived class's own constructor (if any) runs; base-class fields
are still initialized first (see `Interpreter::instantiateClass`), just not via an explicit call.

This is a temporary/load-bearing assumption and should be revisited when Zinc's object-construction syntax is formally designed.

**Status:** Implemented as a load-bearing temporary assumption. Still open for revisit when object-construction syntax is formally designed.

---

## 4. Heterogeneous Map Values

Map literals should potentially allow values of different types:

```zinc
let config = {
    "name": "bubble-factory",
    "version": 1
};
```

Currently the type checker requires all map values to have the same type (unified via `Checker::checkExpr`'s `ExprKind::MapLiteral` case).

Need to decide whether heterogeneous maps should:
- infer a common union/Any type,
- use a dedicated dynamic/Any type,
- or remain homogeneous.

**Status:** Not decided; not implemented. Deliberately left open rather than guessed at.

Extension points for whichever mechanism gets chosen (so this doesn't require a rewrite):
- `TypeKind` (`src/sema/Type.h`) would gain a new kind (e.g. `Any` or `Union`), with matching cases in `isAssignable`, `unify`, `sameBaseType`, and `typeToString`.
- The homogeneity check itself lives in exactly one place — `Checker::checkExpr`'s `ExprKind::MapLiteral` case in `src/sema/Checker.cpp` — and is already isolated from the rest of the checker, so relaxing it won't touch unrelated logic.

---

## 5. `if` / `try` Used as Expressions — Block Value Semantics

`if` and `try` can be used in expression position (e.g. `let x = if cond { ... } else { ... };`),
but what value the block actually *yields* has never been decided. Options on the table, not yet
chosen between:

- the block's final expression is its value (implicit return, like Rust),
- an explicit `return` inside the block supplies the value,
- some other mechanism,
- or `if`/`try` aren't allowed to be value-producing at all, and today's grammar allowance for
  using them in expression position should be walked back.

**Current behavior (NOT a language rule):** the checker types `If`/`Try` expressions as `Unknown`,
and the interpreter evaluates the block for its side effects and yields `null`. This is strictly
an internal placeholder so the rest of the checker/interpreter has *something* to do with the
syntax that's already accepted - it must not be read as "if-expressions evaluate to null" being
the intended design. Both spots are marked with `FIXME (see FIXME.md #5)` comments in
`Checker.cpp` and `Interpreter.cpp`.

**Status:** Not decided; not implemented beyond the placeholder above.