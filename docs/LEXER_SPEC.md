# LEXER_SPEC.md

Specification of the SwiftScript lexer. This document describes the exact behavior of `tools/lexer.c`. If the code and this document disagree, the code is the source of truth.

## Overview

The lexer transforms the `.sws` source text into a flat array of tokens. Each token contains a type, a value, and a position. The lexer does not decode escape sequences in strings and does not parse interpolation; that is the parser's job.

**Input:** A UTF-8 encoded string.
**Output:** A `TokenList` array. The last element is always `T_EOF`.

**Contract:** For any input, the lexer either returns a `TokenList` or reports an error via `SSError` with a position. There are no intermediate states.

## Token Types

Defined in `lexer.h` as `TokType`:

| Enum | Description |
|------|-------------|
| `T_NUM` | Numeric literal |
| `T_STR` | String literal (quotes stripped, escapes preserved) |
| `T_ID` | Identifier |
| `T_KW` | Keyword |
| `T_OP` | Operator or punctuation |
| `T_EOF` | End of file |

Token structure (`Token` struct):
```c
typedef struct {
    TokType type;
    char   *value;   // for STR / KW / ID / OP; NULL for NUM/EOF
    double  num;     // for NUM
    int     line, col;
} Token;
```

## Keywords

Recognized by the lexer and emitted as `T_KW`:

```
let  var  func  return  if  else  for  in  while  break  continue
struct  enum  class  protocol  extension  import  true  false  nil
self  init  guard  defer  throws  try  catch  switch  case  default
where  inout  async  await  public  private  internal  static  lazy
implements  extends  override  new  super  throw  rethrows  export  from  as
```

> ⚠️ **Warning:** Some keywords (e.g., `protocol`, `extension`) are reserved for Swift compatibility but are not yet processed by the parser. Using them will result in a parser error.

## Tokenization Rules

### Whitespace and BOM

Whitespace is skipped. We rely on the C standard `isspace()` function.

If the file starts with a UTF-8 BOM (`EF BB BF`), it is explicitly skipped before tokenization begins.

### Comments

| Syntax | Behavior |
|--------|----------|
| `// ...` to `\n` | Skipped to the end of the line |
| `/* ... */` | Skipped to the first `*/` |

Nested block comments are **not** supported.

> ⚠️ **Warning:** An unterminated `/*` comment throws an `SSError` (`"Unterminated block comment"`). It does not silently consume the rest of the file.

### Numeric Literals

We support standard decimal, hexadecimal, binary, and octal literals.

**Decimal:**
- Digits `0-9`.
- Underscores `_` are allowed as separators, but strict: no leading, trailing, or double underscores (`1__2` is an error).
- Decimals: `3.14`.
- Exponents: `1e10`, `1.5e-3`.

**Hex / Binary / Octal:**
- Hex: `0xFF`, `0XFF`.
- Binary: `0b1010`, `0B1010`.
- Octal: `0o777`, `0O777`.
- Underscores are allowed in the digits (e.g., `0xFF_FF`).

> ℹ️ **Note:** `strtoull` is used for hex/oct/bin parsing to avoid overflow. If the number exceeds `long long`, it falls back to `strtod` for a floating-point approximation.

### String Literals

Only double quotes (`"..."`) are supported. Single quotes are rejected (they are not valid strings in Swift).

The lexer does **not** decode escape sequences. It preserves pairs like `\n`, `\t`, `\\`, and `\"` in the buffer as-is for the parser to handle.

Strings **cannot** span multiple lines. A `\n` inside a string will cause an "Unterminated string" error when the EOF is reached.

An unterminated string throws an `SSError` with the position of the opening quote.

### Identifiers

Start with `[A-Za-z_$]`, followed by `[A-Za-z0-9_$]`.

```
foo       → T_ID "foo"
_foo      → T_ID "_foo"
$0        → T_ID "$0"
$1        → T_ID "$1"
```

- `$` is allowed for implicit closure parameters (`$0`, `$1`).
- `_` is a valid identifier. It is used in patterns as a wildcard and in function parameters as an "unlabeled" marker.
- `self` is a keyword (`T_KW`).

**Unicode identifiers are not supported.** `имя` or `变量` will trigger an "Unknown character" error.

> ⚠️ **Warning:** `is_id_start` and `is_id_part` in `lexer.c` are strictly ASCII to avoid locale-dependent bugs.

### Operators

Three-character operators (checked first):
```
...    ..<
```

Two-character operators:
```
->  ==  !=  <=  >=  &&  ||  ??  ?.  +=  -=  *=  /=  %=
```

Single-character operators:
```
+  -  *  /  %  <  >  (  )  [  ]  {  }  ,  ;  :  .  =  !  ?  @  _  &  |  ~  ^
```

> ℹ️ **Note:** `..` is not a token. It is parsed as two `T_OP "."` tokens.
> ℹ️ **Note:** `a?.b` uses the two-character `?.` token. `a? .b` uses `?` and `.`.

### EOF

Always appended at the end. Has `type: T_EOF`, `value: NULL`, and the position is the end of the file.

## Positions

Each token stores `line` and `col`. Both are 1-based.

`line` increments on `\n`. `col` resets to 1.

For string literals, the position is the **opening quote**.

## Errors

The lexer throws an `SSError` in the following cases:

| Message | Condition |
|---------|-----------|
| `Unterminated string` | `"` without a closing quote before EOF |
| `Unknown character '<c>'` | Character does not match any lexical class |
| `Unterminated block comment` | `/*` without a closing `*/` before EOF |

All errors contain `line` and `col` of the problematic character.

## Invariants

For any successful `tokenize(src)` call:

1. The result is an array where the last element is `T_EOF`.
2. Positions are monotonically non-decreasing.
3. `T_NUM` values are finite doubles.
4. `T_KW` values are in the keyword list.
5. Concatenating the token values (with appropriate whitespace stripping) reconstructs the input.

## Example

Input:
```swift
let x = 0xFF + 3.14
```

Tokens:

| # | Type | Value | Line | Col |
|---|------|-------|------|-----|
| 0 | `T_KW` | `let` | 1 | 1 |
| 1 | `T_ID` | `x` | 1 | 5 |
| 2 | `T_OP` | `=` | 1 | 7 |
| 3 | `T_NUM` | `255.0` | 1 | 9 |
| 4 | `T_OP` | `+` | 1 | 14 |
| 5 | `T_NUM` | `3.14` | 1 | 16 |
| 6 | `T_EOF` | `null` | 1 | 20 |

## See Also

- [GRAMMAR.md](GRAMMAR.md) — how tokens are parsed into an AST.
- [C_IMPLEMENTATION.md](C_IMPLEMENTATION.md) — how the lexer is structured in C.