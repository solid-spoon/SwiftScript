# GRAMMAR.md

Specification of the SwiftScript parser. This document describes the exact behavior of `tools/parser.c`. If the code and this document disagree, the code is the source of truth.

## Overview

The parser transforms a token stream into an Abstract Syntax Tree (AST). It uses a recursive descent approach. The intermediate state is the current position in the token array. Parsing errors are handled via `setjmp`/`longjmp` (equivalent to throwing a `SyntaxError`).

**Input:** A `TokenList` from the lexer, the last token is `T_EOF`.
**Output:** A `N_PROGRAM` AST node containing a list of top-level statements.
**Contract:** For any input, the parser either returns a valid AST or reports an error via `SSError`. The AST never contains partially built nodes.

## Notation

```
NAME         — Identifier (T_ID)
KW           — Keyword (T_KW)
OP           — Operator (T_OP)
[ x ]        — Optional
{ x }        — Zero or more repetitions
x | y        — Alternative
( x )        — Grouping
"x"          — Literal token
```

## Program

```
Program     ::= { TopLevel } EOF
TopLevel    ::= Decl | Stmt
Decl        ::= FuncDecl | StructDecl | EnumDecl | ClassDecl | ImportDecl | ExportDecl
```

Top-level allows function, struct, enum, and class declarations, as well as imports, exports, and any statements (including `let`, calls, loops). Top-level expressions are allowed:

```swift
let x = 5
func f() { }
print("hello")
```

## Statements

```
Stmt        ::= LetStmt
              | ReturnStmt
              | BreakStmt
              | ContinueStmt
              | WhileStmt
              | ForStmt
              | Block
              | TryStmt
              | ThrowStmt
              | ExprStmt
```

Dispatch by first token:

| First Token | Parsed As |
|-------------|-----------|
| `let`, `var` | `LetStmt` |
| `func` | `FuncDecl` |
| `struct` | `StructDecl` |
| `enum` | `EnumDecl` |
| `class` | `ClassDecl` |
| `import` | `ImportDecl` |
| `export` | `ExportDecl` |
| `try` | `TryStmt` |
| `throw` | `ThrowStmt` |
| `switch` | `Expr(parseSwitch)` |
| `return` | `ReturnStmt` |
| `if` | `Expr(parseIf)` |
| `guard` | `GuardStmt` |
| `while` | `WhileStmt` |
| `for` | `ForStmt` |
| `break` | `BreakStmt` |
| `continue` | `ContinueStmt` |
| `{` | `Block` |
| anything else | `ExprStmt` = `Expr(parseExpr(0))` |

### `LetStmt`

```
LetStmt     ::= ( "let" | "var" ) NAME [ ":" Type ] [ "=" Expr ] [ ";" ]
```

### `ReturnStmt`

```
ReturnStmt  ::= "return" [ Expr ] [ ";" ]
```

### `BreakStmt`, `ContinueStmt`

```
BreakStmt    ::= "break" [ ";" ]
ContinueStmt ::= "continue" [ ";" ]
```

### `WhileStmt`

```
WhileStmt   ::= "while" Expr Block
```
The condition is parsed with `noTrailing: true` so that `{` is treated as the loop body, not a trailing closure.

### `ForStmt`

```
ForStmt     ::= "for" NAME "in" Expr Block
```

### `IfStmt` / `IfExpr`

```
IfStmt      ::= "if" Expr Block [ "else" ( IfStmt | Block ) ]
```

### `GuardStmt`

```
GuardStmt   ::= "guard" CondList "else" Block
CondList    ::= ( LetBinding | Expr ) { "," ( LetBinding | Expr ) }
```
The `else` block must explicitly terminate (via `return`, `throw`, `break`, or `continue`). This is enforced by the codegen.

### `TryStmt` / `ThrowStmt`

```
TryStmt     ::= "try" Block "catch" [ NAME ] Block
ThrowStmt   ::= "throw" Expr [ ";" ]
```

### `Block`

```
Block       ::= "{" { Stmt } "}"
```

## Declarations

### Modifiers

```
Modifier    ::= "private" | "public" | "internal" | "static" | "async" | "override" | "lazy"
```
Modifiers can precede `let`, `var`, `func`, and `init` inside classes and structs.

### `FuncDecl`

```
FuncDecl    ::= "func" NAME "(" [ Params ] ")" [ "throws" ] [ "->" Type ] Block
Params      ::= Param { "," Param } [ "," ]
Param       ::= "_" NAME [ ":" Type ]
              | NAME NAME [ ":" Type ]
              | NAME [ ":" Type ]
```

### `StructDecl`

```
StructDecl  ::= "struct" NAME "{" { Field | Method | Init } "}"
Field       ::= ( "let" | "var" ) NAME ":" Type [ "=" Expr ] [ ";" ]
Method      ::= FuncDecl
Init        ::= "init" "(" [ Params ] ")" [ "throws" ] Block
```

### `ClassDecl`

```
ClassDecl   ::= "class" NAME [ "extends" NAME ] [ "implements" QName { "," QName } ] "{" { Member } "}"
QName       ::= NAME { "." NAME }
Member      ::= Field | Method | Init
```

### `EnumDecl`

```
EnumDecl    ::= "enum" NAME "{" { CaseDecl } "}"
CaseDecl    ::= "case" NAME [ "(" [ Params ] ")" ] [ ";" ]
```

### `ImportDecl` / `ExportDecl`

```
ImportDecl  ::= "import" ImportSpec "from" STR [ ";" ]
ImportSpec  ::= "*" "as" NAME
              | "{" NAME [ "as" NAME ] { "," NAME [ "as" NAME ] } "}"
              | NAME

ExportDecl  ::= "export" ( LetStmt | FuncDecl | StructDecl | EnumDecl | ClassDecl )
```
> ⚠️ **Warning:** In the C implementation, the `from` path string is transformed at codegen (`.sws` -> `.js`) only if it starts with `./` or `../`.

## Expressions

### Precedence

From lowest to highest:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 | `??` | Right |
| 2 | `\|\|` | Left |
| 3 | `&&` | Left |
| 4 | `==`, `!=` | Left |
| 5 | `<`, `<=`, `>`, `>=` | Left |
| 5.5 | `..<`, `...` | Left |
| 6 | `+`, `-` | Left |
| 7 | `*`, `/`, `%` | Left |

### Ternary

```
Ternary     ::= Expr "?" Expr ":" Expr
```
Right-associative. Parsed only at `minPrec == 0`.

### Assignment

```
Assign      ::= Expr ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" ) Expr
```
Parsed only at `minPrec == 0`. Lvalue checking is deferred to codegen/checker.

### Unary

```
Unary       ::= ( "-" | "!" | "+" ) Unary
              | "try" Unary
              | "await" Unary
              | Postfix
```

### Postfix Chain

```
Postfix     ::= ( "new" ) Primary { PostfixOp }
PostfixOp   ::= "." NAME
              | "?." NAME
              | "(" [ Args ] ")"
              | "[" Expr "]"
              | "!"
              | "{" ClosureBody "}"
```

> ℹ️ **Note:** `new` is consumed at the beginning of the postfix chain. It binds to the first `Call` in the chain.

### Arguments

```
Args        ::= Arg { "," Arg } [ "," ]
Arg         ::= [ NAME ":" ] Expr
```

### Primary Expressions

```
Primary     ::= NUM | STR | "true" | "false" | "nil"
              | "if" ...
              | "switch" ...
              | "." NAME                             — EnumCase
              | "[" [ ArrayItems | DictItems ] "]"   — Array or Dict
              | "(" [ Expr { "," Expr } ] ")"        — Grouping or Tuple
              | "{" ClosureBody "}"                  — Closure
              | "func" ... "{" ... "}"               — Anonymous Func
              | NAME
              | "self"
              | "super"
```

### Trailing Closures

If `{` follows an expression and `noTrailing` is not set, the parser parses it as a closure and attaches it to the preceding `Call`.

```swift
nums.map { $0 * 2 } // Parses as Call(Member(nums, map), [Closure])
```

### Closures

```
ClosureBody ::= [ "async" ] [ Params "in" ] { Stmt }
Params      ::= "(" [ ClosureParams ] ")" [ "->" Type ]
              | NAME { "," NAME }
```

The parser uses `setjmp`/`longjmp` for backtracking to distinguish between explicit parameter lists `(a, b) in` and body expressions `(x)`.

## Patterns (for `switch`)

```
Pattern     ::= "." NAME [ "(" [ Bindings ] ")" ]     — PatEnum
              | "let" NAME                            — PatBind
              | "_"                                   — PatWild
              | "true" | "false" | "nil"              — PatLit
              | Expr                                  — PatLit | PatRange | PatExpr
Bindings    ::= Binding { "," Binding }
Binding     ::= "let" NAME | "_" | NAME
```

## Types

Types are parsed and stored in the AST but erased by codegen.

```
Type        ::= "[" Type "]" [ "?" ]              — Array
              | "(" [ Type { "," Type } ] ")" [ "?" ]  — Tuple
              | NAME [ "<" ... ">" ] [ "?" ]      — Named
```

> ℹ️ **Note:** Generic arguments in `<...>` are parsed using depth counting and then discarded.

## Statements and Separators

Semicolons are **optional** everywhere but are allowed. Statements are separated by the first token that cannot continue the current expression.

## Known Limitations (v1)

- No `do { } while`.
- No labeled statements (`label: for ...`).
- No destructuring (`let (a, b) = ...`).
- No `if case`, `guard case`, `while case`.
- No pattern matching on structs.
- No variadic parameters.
- No `inout` parameters.

## See Also

- [LEXER_SPEC.md](LEXER_SPEC.md) — tokens that feed into the parser.
- [C_IMPLEMENTATION.md](C_IMPLEMENTATION.md) — how the parser is structured in C.
- [CODEGEN_SPEC.md](CODEGEN_SPEC.md) — how the AST is turned into JS.