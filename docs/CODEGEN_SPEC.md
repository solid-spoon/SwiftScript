# CODEGEN_SPEC.md

Specification of the SwiftScript code generator. This document describes the exact behavior of `tools/codegen.c`. If the code and this document disagree, the code is the source of truth.

## Overview

The code generator takes the AST produced by the parser and emits a JavaScript string. No type checking occurs here; the AST is assumed to be correct. Any error thrown by the code generator is a compiler bug, not a user error.

**Input:** A `N_PROGRAM` AST node.
**Output:** A string containing the user's JavaScript code, or `NULL` on error.
**Contract:** For any valid AST, it either returns a string or sets a codegen error (via `codegen_error()`) and returns `NULL`.

## Output Structure and Wrapping

The code generator emits **only the user's code**. The runtime and IIFE wrapper are added later in `main.c`.

If `user_js` contains `import` or `export` at the start of a line, it is treated as an ES Module. The final output is:
```
[RUNTIME]
[user_js]
```

Otherwise, it is wrapped in an IIFE to prevent global scope pollution:
```javascript
[RUNTIME]
(() => {
[user_js]
})();
```

## Strategy and Context (`Ctx`)

The code generator does not build a full symbol table. It uses a `Ctx` struct to track the current scope:

- `struct_names`: Set of known struct names (used for constructor syntax).
- `class_names`: Set of known class names (used to auto-insert `new`).
- `enum_cases`: Map of `.case` to `EnumName` for shorthand resolution.
- `locals`: Set of local variables and parameters in the current scope.
- `fields`: Set of fields in the current struct/class.
- `self_prefix`: `"_self"` inside a struct, `"this"` inside a class, `NULL` elsewhere.
- `break_stack`: Tracks if `break` should apply to a loop or a switch.

### Error Handling
Instead of throwing exceptions, the code generator calls `codegen_error(line, col, "message")`. The first error sets a global flag. At the end of `gen_program`, if the flag is set, the generated string is freed and `NULL` is returned.

## Identifiers and Scope Resolution

Inside a struct or class method, identifier resolution follows this rule:
1. If the name is a local variable or parameter → use as-is.
2. If the name is a field → prefix with `_self.` (struct) or `this.` (class).
3. Otherwise → use as-is.

## Statements

### `Let`
| SwiftScript | JavaScript |
|-------------|------------|
| `let x = 5` | `const x = 5;` |
| `var y = 5` | `let y = 5;` |
| `let z: Int` | `const z = undefined;` |

### `Func`
```swift
func fib(_ n: Int) -> Int { ... }
```
```javascript
function fib(n) { ... }
```
- Types and labels are erased. `async` is preserved.
- Bodies are emitted without extra indentation.

### `Struct`
```swift
struct Point { let x: Int; let y: Int }
```
```javascript
function Point(_args) {
  const { x, y } = _args;
  const _self = { x, y };
  // ... methods attached to _self ...
  return Object.freeze(_self);
}
```
- Constructors take a single object `_args`.
- If **all** fields are `let`, the object is frozen. If any field is `var`, it is not frozen.
- `static` methods are attached to the constructor itself: `Point.create = function() { ... }`.

### `Class`
```swift
class Dog extends Animal { ... }
```
```javascript
class Dog extends Animal {
  constructor(name) {
    this.name = name;
  }
  speak() {
    return "Woof";
  }
}
```
- `init` becomes `constructor`.
- `super.init(...)` becomes `super(...)`.
- Fields with default values (e.g., `let x: Int = 5`) are emitted at the top of the constructor.
- `static` and `async` modifiers are preserved.

### `Enum`
```swift
enum Status { case ok; case warn(String) }
```
```javascript
const Status = Object.freeze({
  ok: Object.freeze({ _case: "ok" }),
  warn: (_0) => Object.freeze({ _case: "warn", _0 }),
});
```

### Loops and Control Flow
- `while cond { ... }` → `while (cond) { ... }`
- `for x in xs { ... }` → `for (const x of xs) { ... }`
- `break` inside a `switch` becomes `break _swN;`. Elsewhere, it is `break;`.
- `try { ... } catch e { ... }` → `try { ... } catch (e) { ... }`

## Expressions

### `Lit`
- `nil` → `null`
- Strings are JSON-escaped.
- Numbers use `%.17g` to preserve double precision.

### `Id`
- `self` → `_self` or `this` depending on context.

### `Call` and `new`
- Struct constructor: `Point(x: 1, y: 2)` → `Point({x: 1, y: 2})`.
- Class constructor: `Dog(name: "Rex")` → `new Dog({name: "Rex"})` (auto-inserted if `Dog` is in `class_names`).
- External classes (like `vscode.TreeItem`) require explicit `new`.
- Argument labels are stripped: `f(x: 1)` → `f(1)`.

### `Dict`
```swift
["a": 1]
```
```javascript
({["a"]: 1})
```
> ⚠️ **Warning:** Dictionaries are plain JS objects. Keys are coerced to strings.

### `Tuple`
```swift
(1, "a")
```
```javascript
[1, "a"]
```
Tuples are arrays.

### `Template` (String Interpolation)
```swift
"Hello, \(name)!"
```
```javascript
`Hello, ${String(name)}!`
```
- `\`, `` ` ``, and `$` are escaped in text parts.
- Expression parts are wrapped in `String(...)`.

### Operators
- `a ..< b` → `_range(a, b)`
- `a ... b` → `_rangeIn(a, b)`
- `a ?? b` → `(a ?? b)`
- Everything else: `(a op b)`

### `If` as an expression
- If both branches are single expressions → ternary `(cond ? a : b)`.
- If branches contain statements → IIFE `(() => { ... })()`.
> ℹ️ **Note:** The code generator detects if the `if` has no `else` or is used as an action-only statement (e.g., inside an async closure) and avoids wrapping it in an IIFE, preventing `await` from being trapped inside a synchronous function.

### `Switch`
- `switch` as a statement uses a labeled block `_swN: { ... }`.
- `switch` as an expression uses an IIFE.

### `Closure`
```swift
nums.map { $0 * 2 }
```
```javascript
nums.map((($0) => { return ($0 * 2); }))
```
- If no explicit parameters are provided, `$0`, `$1`, etc., are collected. The highest index determines the parameter count.
- `async` closures become `async (...) => { ... }`.

## Patterns (Switch Cases)

| Pattern | Condition | Bindings |
|---------|-----------|----------|
| `.ok` | `_s && _s._case === "ok"` | — |
| `.warn(let m)` | `_s && _s._case === "warn"` | `const m = _s._0;` |
| `42` | `_s === 42` | — |
| `400...499` | `(_s >= 400) && (_s <= 499)` | — |
| `let x` | `true` | `const x = _s;` |

> ℹ️ **Note:** `where` guards wrap the condition and bindings in an IIFE to evaluate them together. Bindings are re-emitted inside the `if` block to be visible in the case body.

## Runtime

The code generator relies on the following runtime functions prepended by `main.c`:
`_force`, `_range`, `_rangeIn`, `String`, `print`, `Int`, `Double`, `Bool`, `abs`, `min`, `max`, `round`, `floor`, `ceil`.

## Invariants

1. Output is valid JavaScript.
2. All type annotations and argument labels are erased.
3. `_self` and `this` are used correctly for structs and classes.
4. Internal names (`_swN`, `_sN`, `_self`, `_0`) are used for bookkeeping.

## Known Limitations

- Nested closures with `$0` shadow each other (e.g., `nums.map { $0.map { $0 * 2 } }`).
- User variables named `_self`, `_sw1`, etc., will cause collisions.
- `case let x where cond` bindings are correctly visible in the body, but complex multi-pattern cases with guards may have edge cases.