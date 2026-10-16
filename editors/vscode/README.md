# SwiftScript for VSCode

Syntax highlighting for `.sws` files.

## What it does

- TextMate grammar for `.sws` (comments, strings with `\(...)` interpolation,
  numbers with `_`, keywords, operators, ranges, optionals, enum cases,
  user-defined types)
- Language configuration: brackets, comments, auto-close pairs

## Run locally

1. Open `vscode-swiftscript/` in VSCode.
2. Press `F5` → opens a new VSCode window ("Extension Development Host").
3. Open any `.sws` file → highlighting works.

## What's missing

- Semantic tokens (needs `class` / `new` in the compiler)
- LSP integration (goto-def, hover, diagnostics) — future work
- Snippets for `func`, `struct`, `enum`, `switch` — nice-to-have

## Publishing

Bump version in `package.json`, run `vsce package`, upload `.vsix` to the
Marketplace. No compilation step — it's pure JSON.