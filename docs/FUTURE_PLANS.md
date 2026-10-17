# Future Plans

This document collects features, bugs, and open questions that are **not** implemented in v1.

## Language Features

- **Type Checker**: None exists. Type annotations are parsed and erased. We need a pass 
  that builds a symbol table and checks type compatibility (inspired by the original 
  `TYPE_SYSTEM.md`).
- **Exhaustiveness Checking**: `switch` over enums does not check if all cases are covered. 
  We should add a warning or error.
- **`try?` and `try!`**: Currently, only `try { } catch { }` works. We need `try?` (returns 
  optional) and `try!` (force unwrap) for concise error handling.
- **Destructuring**: `let (a, b) = tuple` and `for (k, v) in dict` are not supported.
- **`Set<T>` and `Map<K, V>`**: Currently, dictionaries compile to JS objects. We should 
  add native Map/Set support if needed.

## Standard Library

- **String methods**: `split`, `join`, `replace`, `trim`, `upper`, `lower`. 
  Currently, users must use JS `String.prototype`.
- **Array methods**: `sum`, `avg`, `unique`, `chunk`.
- **Math module**: `sin`, `cos`, `sqrt`, `pow`, `log`.
- **JSON**: `JSON.parse` and `JSON.stringify` are available via JS globals, but it would 
  be better to have a standard `json` module.

## Infrastructure

- **LSP / Language Server**: We have a custom TreeView extension and basic hover/completion, 
  but no full LSP server (`vscode-languageserver`).
- **Source Maps**: The compiler emits plain JS. Stack traces point to the generated JS, 
  not the original `.sws` file.
- **Golden-file tests**: A test suite that compiles `.sws` files and diffs the output 
  against expected `.js` files is needed to prevent regressions.
- **`swsc watch`**: A mode that watches for file changes and recompiles.
- **`swsc fmt`**: A code formatter for `.sws` files.
- **Cross-platform**: Improve Windows support (currently relies on `system()` for `swsc run`).

## Open Questions

- **Nominal typing**: If two modules export `struct Point`, are they the same type?
- **Implicit conversions**: Should `Int + Float` coerce to `Float`, or be an error?
- **Multiple `default` cases**: We reject this, but is it standard?
- **`if` / `switch` as expressions**: Currently they generate IIFEs. Is there a better way?
