# RUNTIME_SPEC.md

Specification of the SwiftScript runtime. This document describes the exact behavior of the JavaScript code prepended to every compiled file in `tools/main.c`. If the code and this document disagree, the code is the source of truth.

## Overview

The runtime is a hardcoded string literal (`SWIFTS_RUNTIME`) in `main.c`. It is prepended to the generated JavaScript output for every single `.sws` file. 

It provides basic standard library functions (`print`, `String`, `_force`, etc.) that the generated code relies on.

> ⚠️ **Warning:** The runtime is copied into every compiled file. For a 10-file project, the runtime is duplicated 10 times. Bundlers (like rollup or esbuild) can deduplicate this later.

## Functions

### `_force(x)`

Force-unwraps an optional. Used by the code generator for the `!` operator.

```js
const _force = (x) => { 
    if (x === null || x === undefined) throw new Error("Force unwrap of nil"); 
    return x; 
};
```

- **Throws:** `Error("Force unwrap of nil")` if `x` is `null` or `undefined`.

### `print(...args)`

Prints to the console. Used by the `print()` function.

```js
const print = (...args) => console.log(...args);
```

### `String(x)`

A custom string conversion function that handles `null`, arrays, and objects. Used by string interpolation (`"\(x)"`).

```js
const String = (x) => {
  if (x === null || x === undefined) return "nil";
  if (Array.isArray(x)) return "[" + x.map(String).join(", ") + "]";
  if (typeof x === "object") {
    const pairs = Object.entries(x).map(([k, v]) => k + ": " + String(v)).join(", ");
    return "{" + pairs + "}";
  }
  return "" + x;
};
```

- `String(null)` -> `"nil"`
- `String([1, 2])` -> `"[1, 2]"`
- `String({ a: 1 })` -> `"{a: 1}"`

> ⚠️ **Warning:** This function shadows the global `String` constructor. If a user writes `new String("x")`, it will fail, because `String` is now an arrow function.

### Type Conversion

```js
const Int = (x) => { const n = parseInt(x, 10); return Number.isNaN(n) ? null : n; };
const Double = (x) => { const n = parseFloat(x); return Number.isNaN(n) ? null : n; };
const Bool = (x) => Boolean(x);
```

- `Int("42")` -> `42`
- `Int("abc")` -> `null`
- `Double("3.14")` -> `3.14`
- `Bool("")` -> `false`

### Math

```js
const abs = Math.abs, min = Math.min, max = Math.max;
const round = Math.round, floor = Math.floor, ceil = Math.ceil;
```

These are direct references to JavaScript's `Math` object.

### Ranges

Used by the `..<` and `...` operators.

```js
const _range   = (a, b) => { const r = []; for (let i = a; i <  b; i++) r.push(i); return r; };
const _rangeIn = (a, b) => { const r = []; for (let i = a; i <= b; i++) r.push(i); return r; };
```

- `_range(0, 3)` -> `[0, 1, 2]`
- `_rangeIn(0, 3)` -> `[0, 1, 2, 3]`

## Module vs. Script Wrapping

After prepending the runtime, `main.c` checks if the generated user code contains `import` or `export` syntax.

**If it does (ES Module):**
```javascript
[RUNTIME]
[USER CODE]
```

**If it does not (Script):**
```javascript
[RUNTIME]
(() => {
[USER CODE]
})();
```

This ensures that global variables in simple scripts don't pollute the global namespace.

## Reserved Names

The following identifiers are defined by the runtime and **must not be used** by the user:

```
_force  print  String  Int  Double  Bool
abs  min  max  round  floor  ceil
_range  _rangeIn
```

Using these names will shadow the runtime and cause cryptic runtime errors (e.g., `ReferenceError: String is not a function`).