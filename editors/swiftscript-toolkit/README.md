# SwiftScript for VSCode

The full VSCode extension for **SwiftScript** — syntax, snippets, outline, folding, hover, completion. Written in SwiftScript itself.

## Features

| Feature | How |
|---|---|
| **Syntax highlighting** | TextMate grammar (`syntaxes/swiftscript.tmLanguage.json`) |
| **Snippets** | `func`, `struct`, `class`, `enum`, `switch`, `iflet`, `guardlet`, `trycatch`, `vscmd`, ... |
| **Outline** (Ctrl+Shift+O) | `DocumentSymbolProvider` — nested tree of classes, structs, enums, methods, fields |
| **Folding** | `FoldingRangeProvider` — `{...}` blocks collapse |
| **Hover** | `HoverProvider` — hover `guard`, `let`, `async`, etc. for docs |
| **Completion** | `CompletionProvider` — keywords, types, builtins; triggers on `.` |

## Build

```bash
./swsc build editors/swiftscript/src -o editors/swiftscript/out
```

## Run locally

1. Open `editors/swiftscript/` in VSCode.
2. Press `F5` → opens a new VSCode window ("Extension Development Host").
3. Open any `.sws` file → all features active.