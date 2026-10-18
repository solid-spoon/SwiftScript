# SwiftScript

**Swift-inspired syntax that compiles to JavaScript.**

Write Swift-like code — `func`, `struct`, `class`, `enum`, `switch`, `if let` — and get plain ESM JavaScript out. No runtime library, no framework, no VM. Just a compiler.

```swift
enum Status {
    case ok
    case warn(String)
    case error(String)
}

func describe(_ s: Status) -> String {
    switch s {
    case .ok:             return "fine"
    case .warn(let msg):  return "warn: \(msg)"
    case .error(let msg): return "error: \(msg)"
    }
}

print(describe(.warn("slow")))
```

Compiles to:

```js
const Status = Object.freeze({
  ok: Object.freeze({ _case: "ok" }),
  warn: (_0) => Object.freeze({ _case: "warn", _0 }),
  error: (_0) => Object.freeze({ _case: "error", _0 }),
});
function describe(s) {
_sw1: {
  const _s1 = s;
  if (_s1 && _s1._case === "ok") {
    return "fine";
    break _sw1;
  }
  if (_s1 && _s1._case === "warn") {
    const msg = _s1._0;
    return `warn: ${String(msg)}`;
    break _sw1;
  }
  if (_s1 && _s1._case === "error") {
    const msg = _s1._0;
    return `error: ${String(msg)}`;
    break _sw1;
  }
}
}
print(describe(Status.warn("slow")));
```

## Why

JavaScript is a great runtime and a mediocre language to *think* in. SwiftScript gives you Swift's ergonomics — value types, pattern matching, optionals, labeled arguments, string interpolation — without giving up Node.js, npm, or the VSCode extension API.

It's a **source-to-source compiler**, not a transpiler framework. The generated code is what you'd write by hand if you were being careful. Readable, debuggable, no magic.

## Status

**v0.3 — polished core.** Usable for scripts, CLI tools, games, and VSCode extensions. The compiler core has passed a full security audit (no RCE, no memory unsafety) and is covered by 27 golden tests.

### Works today

- `func`, `let`, `var`, `struct`, `enum`, `class`, `switch`, `for`, `while`, `if/else`
- **Optionals**: `T?`, `T!`, `??`, `!`, `?.`, `if let`, `guard let`
- **Pattern matching**: enum cases with bindings, ranges, `where` guards, exhaustiveness checking
- **String interpolation**: `"Hello, \(name)!"`
- **Closures**: `nums.map { $0 * 2 }`, explicit params, `reduce` swap
- **Structs & Classes**: `extends`, `super.init`, `override`, `implements` (erased), auto-frozen structs
- **Computed Properties**: `var area: Double { return ... }` (compiles to `get` for classes, `Object.defineProperty` for structs)
- **Extensions**: `extension Foo { ... }` (merges methods at compile time)
- **Defer**: `defer { ... }` statements (compiles to `try/finally`)
- **Destructuring**: `let (a, b) = ...` and `for (k, v) in ...`
- **Block Scoping**: strict block-level variable lifetimes (like modern JS `let`/`const`)
- **Source Maps**: Debug runtime errors and set breakpoints directly in `.sws` files via Chrome DevTools / Node.js.
- **Smart Diagnostics**: Rust-style compiler errors with caret `^` pointing to the exact column.
- **Modules**: `import`/`export` — ESM out of the box.
- **VSCode Ecosystem**: A fully-featured [SwiftScript Toolkit](editors/swiftscript-toolkit/) extension (syntax, snippets, outline, folding, hover, diagnostics) written in SwiftScript itself.

### Doesn't work yet

- Type checking — **none**. Type annotations are parsed and erased. No errors on mismatches.
- `protocol` definitions — explicitly fails with a friendly "not yet implemented" error.
- `if case`, `guard case` — only `switch` does pattern matching.
- `Set<T>`, `Map<K, V>`.
- Multi-line strings (`"""..."""`), raw strings.

See [`docs/`](docs/) for the aspirational spec.

## Install & Build

Requires `gcc` (or `clang`) and `bash`. No Makefile, no CMake, no dependencies.

```bash
git clone https://github.com/solid-spoon/SwiftScript
cd SwiftScript

# Standard strict build (hardened, -Werror)
./build.sh

# Run tests (builds + runs 27 golden tests)
./test/run.sh

# Debug build with ASAN and UBSAN
./build.sh debug
```

The build script is a Rust-strict driver: warnings are errors, and release builds include `_FORTIFY_SOURCE`, PIE, RELRO, and stack canaries.

## Usage

```bash
# Compile a directory — mirrors src/ into build/, .sws → .js
./swsc build src/ -o build/

# Compile a single file — output goes next to the source
./swsc build hello.sws

# Check syntax without writing files (for CI and editor integration)
./swsc check src/

# Compile and run immediately
./swsc run hello.sws

# Pass arguments to the running program
./swsc run script.sws -- arg1 arg2
```

Then run with `node --enable-source-maps` (ESM mode) or include the `.js` in your HTML via `<script type="module">`.

### CLI

```
swsc build <path> [-o <out>] [--no-runtime]
swsc check <path>
swsc run   <file.sws> [-- args...]

  <path>       file or directory (.sws)
  -o <out>     output file or directory
```

## VSCode extension

The official VSCode extension is **[`editors/swiftscript-toolkit/`](editors/swiftscript-toolkit/)**. It is written entirely in SwiftScript and includes:

1. **TextMate grammar** for syntax highlighting.
2. **Snippets** for `func`, `class`, `iflet`, `defer`, `extension`, `vscmd`, etc.
3. **Language features (LSP-lite)**: Outline, Folding, Hover descriptions, and Completion.
4. **Diagnostics**: Hooks into `swsc check` to underline syntax errors in real-time.

To build and run the extension locally:
```bash
./swsc build editors/swiftscript-toolkit/src -o editors/swiftscript-toolkit/out
cd editors/swiftscript-toolkit/
code .
# press F5 → "Extension Development Host" opens with the SwiftScript panel
```

## Project layout

```
swsc                 compiled compiler binary
build.sh             Rust-strict build driver (ASAN/UBSAN, hardening)
tools/               C source for the compiler
  lexer.c            tokenizer
  parser.c           recursive descent → AST (with DoS protection)
  codegen.c          AST → JavaScript
  sourcemap.c        Source Map V3 generation (Base64 VLQ)
  ast.[ch]           AST node types
  util.[ch]          dynamic strings, memory helpers
  main.c             CLI, file walker, smart diagnostics, runtime prelude

test/
  run.sh             Golden test runner (positive and negative)
  golden/            Expected stdout for positive tests
  swiftscript/       Test cases (incl. neg/ for expected failures)

editors/
  swiftscript-toolkit/    The official VSCode extension

examples/
  bank.sws           classes, inheritance, throws, optionals
  todos/todo.sws     enums, structs, closures
  tetris/            A Tetris clone using HTML5 Canvas
  vscode-swiftscript-explorer/  example custom TreeView extension
```

## Design choices

**Tabs vs spaces?** Tabs, 4 spaces wide, for the source. Output JS is unformatted — pipe it through `prettier` if you care.

**Runtime in every file?** Yes. ~1.5 KB of `print`, `String`, `_force`, `_range` prepended to every output file. A bundler deduplicates in 10 ms.

**How do Source Maps work?** `swsc` tracks the original line/col of every AST statement and uses Base64 VLQ encoding to generate standard `.js.map` files. Node.js (`--enable-source-maps`) and Chrome DevTools will natively map your generated JS back to your `.sws` source.

**How does `extension` work?** The parser collects all `extension Type {}` blocks. Before codegen, a pass merges these methods directly into the original `struct` or `class` AST node.

**How does `defer` work?** `defer` blocks are extracted and the remaining statements are wrapped in a standard JS `try { ... } finally { ... }` block, ensuring execution on `return` or `throw`.

**How do Computed Properties work?** In classes, they become native `get` accessors. In structs, they are defined via `Object.defineProperty(_self, ..., { get: ... })` *before* calling `Object.freeze(_self)`.

**Why C?** The compiler is ~4500 lines and takes no dependencies. `gcc` is available everywhere. A single binary runs without a Node.js toolchain.

## Roadmap

Near-term:

- `if case` / `guard case` (pattern matching outside of `switch`)
- `protocol` definitions
- Multi-line strings (`"""..."""`)

Later:

- Type checker
- Full LSP server (goto-def, rename, semantic tokens)
- `Set<T>`, `Map<K, V>`

See [`docs/*.md`](docs/) for the full aspirational spec. The docs lie in the right direction — the code is what actually runs.

## Contributing

Small bugs are worth reporting in the issue tracker. Larger changes — open a discussion first, the language is still finding its shape.

Because `swsc` compiles itself as a test (the VSCode extension and the Tetris game), any change must pass `./test/run.sh`.

## License

MIT — see [`LICENSE`](LICENSE).