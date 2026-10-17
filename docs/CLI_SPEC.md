# CLI_SPEC.md

Specification of the SwiftScript command-line interface. This document describes the exact behavior of `tools/main.c`. If the code and this document disagree, the code is the source of truth.

## Overview

`swsc` is a compiled C binary that transforms `.sws` files into `.js` files. It has no state, no configuration file, and no package manager. It works on a single file or recursively on a directory.

```
swsc <command> [arguments]
```

## Commands

### `swsc build <path> [-o <out>]`

Compiles a file or directory.

```bash
swsc build src/main.sws -o build/main.js
swsc build src/ -o build/
```

| Argument | Description |
|----------|-------------|
| `<path>` | `.sws` file or directory |
| `-o <out>` | Output file or directory |

**Behavior:**

- If `<path>` is a file, it compiles it to a single `.js` file.
- If `<path>` is a directory, it recursively walks it, finds all `.sws` files, and compiles them preserving the relative directory structure in `<out>`.
- If `-o` is omitted, the output is written next to the input (`.sws` -> `.js`).
- If `-o` points to an existing directory (or ends with `/`), the output is placed inside it.

**Returns:** `0` on success, `1` on compilation error, `2` on I/O error or invalid arguments.

### `swsc check <path>`

Parses and runs codegen in memory to catch errors, without writing any files.

```bash
swsc check src/
swsc check src/main.sws
```

| Argument | Description |
|----------|-------------|
| `<path>` | File or directory |

**Behavior:**

- Runs the parser and code generator.
- No `.js` files are written.
- Diagnostics are printed to `stderr`.
- If `<path>` is a directory, it continues checking other files after an error.

**Returns:** `0` if no errors, `1` if any errors.

> ℹ️ **Note:** `check` currently performs syntax and basic semantic checks (e.g., unclosed blocks, invalid struct arguments). It does not perform full type checking.

### `swsc run <file.sws> [-- args...]`

Compiles and immediately executes a file.

```bash
swsc run examples/hello.sws
swsc run script.sws -- --port 8080
```

| Argument | Description |
|----------|-------------|
| `<file.sws>` | The file to compile and run |
| `--` | Separator. Everything after it is passed to the program |
| `args` | Arguments passed to the Node.js process |

**Behavior:**

1. Compiles the file to a temporary `.js` file.
2. Spawns `node <tmp.js> <args>`.
3. Inherits `stdin`, `stdout`, and `stderr`.
4. Deletes the temporary file.
5. Returns the exit code of `node`.

> ⚠️ **Warning:** If `node` is not in `PATH`, the command fails with exit code `127` (POSIX) or `2`.

## Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | Compilation error (syntax error, codegen error) |
| `2` | Usage error, invalid arguments, or I/O error (file not found) |

## Directory Traversal

When `<path>` is a directory, the following rules apply:

1. Only files ending in `.sws` are processed.
2. Subdirectories are traversed recursively.
3. Hidden files and directories (starting with `.`) are **not** skipped by default in the current C implementation (unlike the old JS spec), but only `.sws` files are compiled.

## Output Format

Diagnostics are printed to `stderr` in the following format:

```
<path>:<line>:<col>: <message>
```

Example:
```
src/main.sws:12:5: Expected op '{', got 'foo'
```

## Platform Notes

- **POSIX:** `swsc run` uses `system()` to spawn Node.js and parses the exit code via `WIFEXITED`/`WEXITSTATUS`.
- **Windows:** `swsc run` uses `system()` natively, and relies on `TEMP` or `TMP` environment variables for the temporary file.
- **No Makefile:** The compiler is built via `build.sh` (or `gcc` directly).