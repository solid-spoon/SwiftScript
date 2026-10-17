# C Implementation Guide

This document describes how the SwiftScript compiler is structured in C. 
If the code and this document disagree, the code is the source of truth.

## The AST (`tools/ast.h`)

Unlike the original JS prototype (which used plain objects with a `k: "Kind"` field), 
the C compiler uses a `Node` struct with a `NodeKind` enum.

```c
typedef struct Node {
    NodeKind kind;
    int line, col;
    // ... various flags (is_async, is_static, etc.)
    
    const char *name;
    const char *sval;
    const char *op;
    
    // ... primitive values (num, boolean, isnull)
    
    Node *a, *b, *c; // child nodes
    List *items;     // generic list for children (stmts, params, etc.)
    // ...
} Node;
```
Because C doesn't have variants, the `Node` struct contains all possible fields for 
all node types. This is a "fat struct" — slightly memory-inefficient but very simple 
to write and debug.

## The Parser (`tools/parser.c`)

The parser is a recursive descent parser. It uses `setjmp`/`longjmp` for error handling 
(equivalent to `throw`/`catch` in JS). When a syntax error occurs, `p_err` is called, 
which `longjmp`s back to the top-level `parse()` function, aborting the parse.

It also uses `setjmp` for backtracking in `parse_closure` to distinguish between 
`{ (a, b) in ... }` and `{ (expression) }`.

## Codegen (`tools/codegen.c`)

The codegen is a direct AST walk. It maintains a `Ctx` struct to track:
- `struct_names` / `class_names`: to auto-insert `new` for user classes.
- `enum_cases`: to resolve `.ok` to `Status.ok`.
- `locals` / `fields` / `self_prefix`: to resolve `self.x` in classes vs structs.

Instead of throwing errors, codegen uses `codegen_error()`. If an error occurs, 
`gen_program` returns `NULL` and the caller prints the diagnostic. This allows `swsc check` 
to continue to the next file instead of crashing the process.
