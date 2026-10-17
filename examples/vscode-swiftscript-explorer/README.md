# SwiftScript Explorer

A VSCode extension written in SwiftScript that shows a tree of top-level
declarations (`enum`, `struct`, `class`, `func`, `let`/`var`) from all
`.sws` files in the workspace.

## Build

```bash
./swsc build examples/vscode-swiftscript-explorer/src \
  -o examples/vscode-swiftscript-explorer/out
```

This creates `out/extension.js`.

## Run

1. Open `examples/vscode-swiftscript-explorer/` in VSCode.
2. Press `F5` → Extension Development Host opens.
3. In the Explorer sidebar you'll see a **SwiftScript** view.
4. Click a file to expand its declarations.
5. Click a declaration to jump to its line.
6. The view auto-refreshes on `.sws` file changes; use the refresh icon
   in the view title to force it.

## What it demonstrates

- `class` with inheritance (`TreeNode` → `FileNode`/`SymbolNode`)
- `enum` with payload and pattern matching in `switch`
- `async func` + `await` (`findFiles`, `openTextDocument`, `getChildren`)
- `try/catch` around file reads
- Optionals (`SymbolKind?`, `if let k = kind`)
- `new` for external classes (`EventEmitter`, `TreeItem`, `ThemeIcon`,
  `Position`, `Range`)
- `implements vscode.TreeDataProvider` (erased at codegen; duck typing
  does the actual work)
- `vscode.EventEmitter` for tree refresh
- `FileSystemWatcher` for live updates

## Note on `new`

Classes from the same `.sws` project are auto-`new`'d (Swift-style
construction). External classes from `vscode` are not tracked, so write
`new` explicitly: `new vscode.TreeItem(...)`, `new vscode.EventEmitter()`,
`new vscode.Range(...)`.