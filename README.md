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

**v0.5 — protocol-driven core.** The language now supports strict static type checking and protocol conformance. If your class or struct claims to implement an interface, the compiler will verify that it actually does before generating any JavaScript. Covered by 34 golden tests.

### Works today

- `func`, `let`, `var`, `struct`, `enum`, `class`, `switch`, `for`, `while`, `if/else`
- **Static Type Checking**: `Int`, `Double`, `String`, `Bool`, `[T]`, `T?`. Compile-time errors on type mismatches.
- **Protocols**: `protocol P { ... }` and `implements P`. Validates required methods and properties at compile time.
- **Optionals**: `T?`, `T!`, `??`, `!`, `?.`, `if let`, `guard let`
- **Pattern matching**: `switch`, `if case`, `guard case`, enum payload extraction, ranges, `where` guards, exhaustiveness checking.
- **String interpolation**: `"Hello, \(name)!"`
- **Multi-line strings**: `"""..."""` (with automatic dedent).
- **Closures**: `nums.map { $0 * 2 }`, explicit params, `reduce` swap.
- **Structs & Classes**: `extends`, `super.init`, `override`, `implements` (erased), auto-frozen structs.
- **Computed Properties**: `var area: Double { return ... }` (compiles to `get` for classes, `Object.defineProperty` for structs).
- **Extensions**: `extension Foo { ... }` (merges methods at compile time).
- **Defer**: `defer { ... }` statements (compiles to `try/finally`).
- **Destructuring**: `let (a, b) = ...` and `for (k, v) in ...`
- **Block Scoping**: strict block-level variable lifetimes.
- **Source Maps**: Debug runtime errors and set breakpoints directly in `.sws` files via Chrome DevTools / Node.js.
- **Smart Diagnostics**: Rust-style compiler errors with caret `^` pointing to the exact column.
- **Recursive `run`**: `swsc run` resolves and compiles the entire import tree dynamically.
- **Modules**: `import`/`export` — ESM out of the box.
- **VSCode Ecosystem**: A fully-featured [SwiftScript Toolkit](editors/swiftscript-toolkit/) extension.

### Doesn't work yet

- Advanced generics (`struct Box<T>`) — parsed and erased, but not type-checked.
- `Set<T>`, `Map<K, V>` (as standard library types).
- Raw strings (`#"..."#`).

See [`docs/`](docs/) for the aspirational spec.

## Install & Build

Requires `gcc` (or `clang`) and `bash`. No Makefile, no CMake, no dependencies.

```bash
git clone https://github.com/solid-spoon/SwiftScript
cd SwiftScript

# Standard strict build (hardened, -Werror)
./build.sh

# Run tests (builds + runs 34 golden tests)
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

# Check syntax and types without writing files
./swsc check src/

# Compile and run immediately (resolves dependencies automatically)
./swsc run hello.sws

# Pass arguments to the running program
./swsc run script.sws -- arg1 arg2
```

Then run with `node --enable-source-maps` (ESM mode) or include the `.js` in your HTML via `<script type="module">`.

## VSCode extension

The official VSCode extension is **[`editors/swiftscript-toolkit/`](editors/swiftscript-toolkit/)**. It is written entirely in SwiftScript and includes:

1. **TextMate grammar** for syntax highlighting (including multi-line strings).
2. **Snippets** for `func`, `class`, `iflet`, `defer`, `extension`, `vscmd`, etc.
3. **Language features (LSP-lite)**: Outline, Folding, Hover descriptions, and Completion.
4. **Diagnostics**: Hooks into `swsc check` to underline syntax *and type* errors in real-time.

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
  parser.c           recursive descent → AST
  typecheck.c        symbol table + static type validation + protocol checks
  codegen.c          AST → JavaScript
  sourcemap.c        Source Map V3 generation
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
  tetris/            A Tetris using HTML5 Canvas
```

## How it compiles

Five stages, no shared state:

1. **Lex** — text → tokens (`lexer.c`)
2. **Parse** — tokens → AST (`parser.c`)
3. **Type Check** — AST → validated AST (`typecheck.c`)
4. **Emit** — AST → JS string (`codegen.c`)
5. **Wrap** — prepend runtime; wrap non-module code in IIFE

The compiler takes ~2ms for a 100-line file.

## Design choices

**How does the Type Checker work?** It builds a lexical scope tree (symbol table) and resolves types. Literals, arrays, optionals, and function returns are inferred. If a type annotation is present, strict compatibility is enforced (e.g., you cannot assign `String` to `Int`). `Any` acts as an escape hatch for dynamic JavaScript interop.

**How do Protocols work?** Protocols are parsed into the AST and registered in the type checker's global scope. When a `class` or `struct` declares `implements P`, the type checker compares the declared methods and properties against the protocol's requirements. Protocols generate zero JavaScript code (they are erased), acting as pure compile-time contracts.

**How do Source Maps work?** `swsc` tracks the original line/col of every AST statement and uses Base64 VLQ encoding to generate standard `.js.map` files. Node.js (`--enable-source-maps`) and Chrome DevTools will natively map your generated JS back to your `.sws` source.

**How does `extension` work?** The parser collects all `extension Type {}` blocks. Before codegen, a pass merges these methods directly into the original `struct` or `class` AST node.

**How does `defer` work?** `defer` blocks are extracted and the remaining statements are wrapped in a standard JS `try { ... } finally { ... }` block, ensuring execution on `return` or `throw`.

**How do Computed Properties work?** In classes, they become native `get` accessors. In structs, they are defined via `Object.defineProperty(_self, ..., { get: ... })` *before* calling `Object.freeze(_self)`.

**Why C?** The compiler is ~5000 lines and takes no dependencies. `gcc` is available everywhere. A single binary runs without a Node.js toolchain.

## Roadmap

Near-term:

- Generics (`Box<T>`) preservation and checking
- Full LSP server (goto-def, rename, semantic tokens)

Later:

- `swsc fmt` (auto-formatter)
- `Set<T>`, `Map<K, V>`
- Raw strings

See [`docs/*.md`](docs/) for the full aspirational spec. The docs lie in the right direction — the code is what actually runs.

## Experimental: C backend

> ⚠️ **This is a research artifact, not a supported target.**
>
> As a proof-of-concept, `swsc` can also emit portable C11. It shares
> 100% of the frontend (lexer, parser, typechecker) with the JS backend.
>
> **Status**: ~65% feature parity. Missing: classes with inheritance,
> closures with captures, multi-file imports, computed properties,
> protocol conformance. Not tested in CI. Do not use in production.
>
> The C backend exists to demonstrate that the frontend is reusable
> and to keep the door open for future native/WASM work.
>

## Contributing

Small bugs are worth reporting in the issue tracker. Larger changes — open a discussion first, the language is still finding its shape.

Because `swsc` compiles itself as a test (the VSCode extension and the Tetris game), any change must pass `./test/run.sh`.

## License

MIT — see [`LICENSE`](LICENSE).