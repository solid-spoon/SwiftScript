# SwiftScript

**Swift-inspired syntax that compiles to JavaScript.**

Write Swift-like code — `func`, `struct`, `enum`, `switch`, `if let` — and get plain ESM JavaScript out. No runtime library, no framework, no VM. Just a compiler.

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

**v0.1 — early.** Usable for small projects and VSCode extensions. Not ready for production where correctness matters more than ergonomics.

### Works today

- `func`, `let`, `var`, `struct`, `enum`, `switch`, `for`, `while`, `if/else`
- **Optionals**: `T?`, `??`, `!`, `?.`, `if let`, `guard let`
- **Pattern matching**: enum cases with bindings, `case 400...499`, `case 301, 302`, `where` guards
- **String interpolation**: `"Hello, \(name)!"`
- **Closures**: `nums.map { $0 * 2 }`, explicit params, `reduce` swap
- **Ranges**: `0..<5`, `0...5` (eager arrays)
- **Modules**: `import`/`export` — ESM out of the box
- **Structs with value semantics**: auto-frozen when all fields are `let`
- **Enums with payloads**: `case warn(String)` → tagged unions

### Doesn't work yet

- `async` / `await` — parsed but not usable (`async func` not wired)
- `class`, `new`, `protocol`, `extension` — recognized as keywords, not parsed
- Type checking — **none**. Type annotations are parsed and erased. No errors on mismatches
- `if case`, `guard case`, destructuring (`let (a, b) = ...`)
- `Set<T>`, `Map<K, V>`
- Full Swift regex-free pattern matching on structs

See [`docs/`](docs/) for the aspirational spec — including everything **not** in v1.

## Install

Requires `gcc` and `bash`. No Makefile, no CMake, no dependencies.

```bash
git clone https://github.com/solid-spoon/SwiftScript
cd SwiftScript
./build.sh
# → ./swsc
```

## Usage

```bash
# Compile a directory — mirrors src/ into build/, .sws → .js
./swsc build src/ -o build/

# Compile a single file — output goes next to the source
./swsc build hello.sws

# Compile a single file to a specific path
./swsc build hello.sws -o dist/hello.js
```

Then run with `node` (ESM mode — either `.mjs` extension, or `"type": "module"` in `package.json`):

```bash
node build/main.js
```

### CLI

```
swsc build <path> [-o <out>]

  <path>       file or directory (.sws)
  -o <out>     output file or directory

  Without -o:
    directory → .js files written next to .sws files
    file      → <name>.js written next to source
```

## VSCode extension

[`vscode swiftscript extension`](editors/vscode/) — TextMate grammar for `.sws`. Syntax highlighting, bracket matching, comment toggling. No LSP yet.

Install locally:

```bash
cd editors/vscode/
code .
# press F5 → "Extension Development Host" opens with highlighting active
```

## Project layout

```
swsc                 compiled compiler binary
build.sh             gcc invocation, no dependencies
tools/               C source for the compiler
  lexer.c            tokenizer
  parser.c           recursive descent → AST
  codegen.c          AST → JavaScript
  ast.[ch]           AST node types
  util.[ch]          dynamic strings, memory helpers
  main.c             CLI, file walker, runtime prelude

test/
  example_sws/       .sws sources
  output_js/         compiled output

editors/
  vscode/            VSCode extension (TextMate grammar)

docs/                specification documents (aspirational, v2+)
```

## How it compiles

Four stages, no shared state:

1. **Lex** — text → tokens (`lexer.c`)
2. **Parse** — tokens → AST (`parser.c`)
3. **Emit** — AST → JS string (`codegen.c`)
4. **Wrap** — prepend runtime; wrap non-module code in IIFE

No type checker yet. No optimizations. The compiler takes ~2ms for a 100-line file.

## Design choices

**Tabs vs spaces?** Tabs, 4 spaces wide, for the source. Output JS is unformatted — pipe it through `prettier` if you care.

**Runtime in every file?** Yes. ~1.5 KB of `print`, `String`, `_force`, `_range` prepended to every output file. For 10-file projects that's 15 KB of duplication; a bundler (rollup, esbuild) deduplicates in 10 ms.

**Why not TypeScript?** TS is JavaScript with types bolted on. SwiftScript is *Swift with JS as the runtime*. Different ergonomics: value semantics, pattern matching, labeled arguments, no `this`.

**Why C?** The compiler is ~3000 lines and takes no dependencies. `gcc` is available everywhere. A single binary runs without a Node.js toolchain.

## Roadmap

Near-term:

- `async func` / `async { ... }` closures
- `swsc check` — compile without writing files
- Golden-file test suite
- `case let x where cond` — fix bindings leak (see `CODEGEN_SPEC.md`)

Later:

- `class` / `new` / `implements`
- Type checker
- LSP server for VSCode (goto-def, hover, diagnostics)
- `Set<T>`, `Map<K, V>`

See [`docs/*.md`](docs/) for the full aspirational spec. The docs lie in the right direction — the code is what actually runs.

## Contributing

Small bugs are worth reporting in the issue tracker. Larger changes — open a discussion first, the language is still finding its shape.

The compiler is deliberately boring: no AST visitors, no passes, no error recovery. Read `parser.c` top to bottom in 15 minutes. Same for `codegen.c`.

## License

MIT — see [`LICENSE`](LICENSE).
