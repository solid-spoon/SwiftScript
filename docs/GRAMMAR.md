# GRAMMAR.md

Спецификация парсера SwiftScript. Документ описывает точное поведение `js/parser.js`. Если код и документ расходятся — это баг.

## Обзор

Парсер превращает поток токенов в AST. Работает методом рекурсивного спуска. Промежуточное состояние — позиция в массиве токенов. Ошибка парсинга — исключение `SSError` с позицией.

**Вход:** массив токенов из лексера, последний — `eof`.
**Выход:** узел `Program` со списком top-level statements.
**Контракт:** для любого входа парсер либо возвращает AST, либо бросает `SSError`. AST не содержит частично построенных узлов.

## Обозначения

```
NAME         — идентификатор (id)
KW           — ключевое слово (kw)
OP           — оператор (op)
[ x ]        — опционально
{ x }        — ноль или более повторений
x | y        — альтернатива
( x )        — группировка
"x"          — буквальный токен
```

Терминалы `let`, `func`, `(` и так далее — конкретные токены из LEXER_SPEC.

## Программа

```
Program     ::= { TopLevel } EOF
TopLevel    ::= Decl | Stmt
Decl        ::= FuncDecl | StructDecl | EnumDecl
```

Top-level допускает объявления функций, структур, перечислений и любые statements (включая `let`, вызовы, циклы). Top-level expressions разрешены:

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
              | ExprStmt
```

Dispatch по первому токену:

| Первый токен | Разбор |
|--------------|--------|
| `let`, `var` | `LetStmt` |
| `func` | `FuncDecl` |
| `struct` | `StructDecl` |
| `enum` | `EnumDecl` |
| `switch` | `Expr(parseSwitch)` |
| `return` | `ReturnStmt` |
| `if` | `Expr(parseIf)` |
| `while` | `WhileStmt` |
| `for` | `ForStmt` |
| `break` | `BreakStmt` |
| `continue` | `ContinueStmt` |
| `{` | `Block` |
| любой другой | `ExprStmt` = `Expr(parseExpr(0))` |

> ℹ️ **Примечание:** `if` и `switch` на top-level оборачиваются в `Expr`. Это значит, что в AST они выглядят как statement с выражением внутри. При этом внутри выражений они разбираются как выражения напрямую.

### `LetStmt`

```
LetStmt     ::= ( "let" | "var" ) NAME [ ":" Type ] [ "=" Expr ] [ ";" ]
```

Поле `kind` в AST — `"let"` или `"var"`.

```swift
let x = 5
var y: Int
let z: String = "hello"
```

> ⚠️ **Внимание:** `let y: Int` без значения допустимо на уровне парсера. Проверка «все `let` инициализированы» — работа checker'а.

### `ReturnStmt`

```
ReturnStmt  ::= "return" [ Expr ] [ ";" ]
```

Разделение:

- Если после `return` идёт `}`, `;` или `eof` — возврат без значения.
- Иначе — разбор выражения.

```swift
return
return 5
return x + y
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

Условие парсится с `noTrailing: true`. Это значит, что `{` после условия — это **тело**, а не trailing closure.

```swift
while x > 0 { x -= 1 }
while isValid() { step() }
```

Антипример:

```swift
while f { x } { y }    // не поддерживается: `f { x }` — не вызов с trailing closure,
                       // а `f` и блок тела `{ x }`, затем второй `{ y }` — ошибка
```

### `ForStmt`

```
ForStmt     ::= "for" NAME "in" Expr Block
```

Паттерн цикла — только один идентификатор. Деструктуризация (`for (a, b) in ...`) не поддерживается.

```swift
for n in nums { print(n) }
for i in 0..<10 { print(i) }
```

### `Block`

```
Block       ::= "{" { Stmt } "}"
```

Блок — это последовательность statements, не выражений. Значение блока не возвращается.

## Объявления

### `FuncDecl`

```
FuncDecl    ::= "func" NAME "(" [ Params ] ")" [ "->" Type ] Block
Params      ::= Param { "," Param } [ "," ]
Param       ::= "_" NAME [ ":" Type ]
              | NAME NAME [ ":" Type ]
              | NAME [ ":" Type ]
```

Три формы параметра:

| Форма | `label` | `name` |
|-------|---------|--------|
| `_ name` | `null` | `name` |
| `label name` | `label` | `name` |
| `name` | `name` | `name` |

```swift
func greet(_ name: String) -> String { return "Hello, \(name)!" }
func greet(from city: String) -> String { return "From \(city)" }
func greet(name: String) -> String { return "Hello, \(name)!" }
```

> ℹ️ **Примечание:** в кодогенераторе используется только `name`. `label` сохраняется в AST, но не влияет на выходной JS. Семантика меток аргументов в v1 не реализована.

Trailing comma в списке параметров разрешён:

```swift
func f(a: Int, b: Int,) { }
```

### `StructDecl`

```
StructDecl  ::= "struct" NAME "{" { Field | Method } "}"
Field       ::= ( "let" | "var" ) NAME ":" Type [ ";" ]
Method      ::= FuncDecl
```

Тело структуры содержит только поля и методы. Никакие другие statements не разрешены.

```swift
struct Point {
    let x: Int
    let y: Int

    func manhattan() -> Int {
        return abs(self.x) + abs(self.y)
    }
}
```

Антипример:

```swift
struct Foo {
    let x = 5       // ошибка: поле требует тип после `:`
    print("hi")     // ошибка: statement не разрешён в структуре
}
```

> ❓ **Требуется уточнение:** почему в структуре **обязательна** аннотация типа, а в локальных `let` — опциональна. Возможно, стоит разрешить `let x = 5` внутри struct.

### `EnumDecl`

```
EnumDecl    ::= "enum" NAME "{" { CaseDecl } "}"
CaseDecl    ::= "case" NAME [ "(" [ Params ] ")" ] [ ";" ]
```

Параметры случая имеют ту же форму, что у функции.

```swift
enum Status {
    case ok
    case warn(String)
    case error(code: Int, message: String)
}
```

## Выражения

### Приоритеты

От низшего к высшему:

| Приоритет | Операторы | Ассоциативность |
|-----------|-----------|-----------------|
| 1 | `??` | Левая |
| 2 | `\|\|` | Левая |
| 3 | `&&` | Левая |
| 4 | `==`, `!=` | Левая |
| 5 | `<`, `<=`, `>`, `>=` | Левая |
| 5.5 | `..<`, `...` | Левая |
| 6 | `+`, `-` | Левая |
| 7 | `*`, `/`, `%` | Левая |

Ассоциативность — **левая** для всех бинарных операторов. Это значит:

```
a ?? b ?? c     → (a ?? b) ?? c
a - b - c       → (a - b) - c
```

> ⚠️ **Внимание:** в Swift `??` право-ассоциативен. В SwiftScript — лево-ассоциативен. Разница проявляется только при смешивании `??` с побочными эффектами — например, `f() ?? g() ?? h()`.

### Тернарный оператор

```
Ternary     ::= Expr "?" Expr ":" Expr
```

Право-ассоциативен. Разбирается только при `minPrec === 0`, то есть на самом верхнем уровне выражения.

```swift
let label = code >= 500 ? "ERROR" : "OK"
let x = a ? b : c ? d : e     // → a ? b : (c ? d : e)
```

> ⚠️ **Внимание:** тернарный оператор **не может быть вложен в другой оператор без скобок**. `a + (b ? c : d)` — работает, `a + b ? c : d` — ошибка или неожиданный разбор.

### Присваивание

```
Assign      ::= Expr ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" ) Expr
```

Разбирается при `minPrec === 0`. Левая часть — любое выражение (проверка на lvalue — работа checker'а).

```swift
x = 5
x += 1
obj.field = 10
arr[0] = 42
```

### Унарные операторы

```
Unary       ::= ( "-" | "!" | "+" ) Unary
              | "try" Unary
              | "await" Unary
              | Postfix
```

Все три арифметических унарных оператора и два ключевых слова имеют одинаковый приоритет и право-ассоциативны.

```swift
-5
!!x
-!x
try await f()
await try f()
```

> ℹ️ **Примечание:** `try` и `await` в AST — обёртки `Try` и `Await`. Кодогенератор разворачивает `Try` прозрачно (`genExpr(n.arg)`), а `Await` — в `(await x)`.

### Postfix-цепочка

```
Postfix     ::= Primary { PostfixOp }
PostfixOp   ::= "." NAME
              | "?." NAME
              | "(" [ Args ] ")"
              | "[" Expr "]"
              | "!"
              | "{" ClosureBody "}"       — trailing closure, если !flags.noTrailing
```

Postfix-операции применяются слева направо, пока не встретится токен, не входящий в набор.

```swift
a.b.c
a?.b?.c
f(x)(y)(z)
arr[0][1]
obj!.field
obj.method { $0 * 2 }
```

> ⚠️ **Внимание:** postfix-цепочка **не переносится через строки**. Парсер не использует информацию о позиции токенов, но разбор останавливается на первом неподходящем токене. Единственная причина остановки — не в наборе `PostfixOp`.

Антипример:

```swift
let x = foo
    .bar()
    .baz()
```

Это **не** разберётся как `foo.bar().baz()`. На строке 2 встретится `.bar` — но это не postfix от `foo`, потому что `foo` уже стал отдельным выражением на строке 1. На самом деле `.bar` — это `EnumCase`, и в лучшем случае получится ошибка, в худшем — неожиданное поведение.

> ❓ **Требуется уточнение:** нужна ли поддержка переноса postfix-цепочки через строки. Если да — потребуется отслеживать позиции.

### Аргументы вызова

```
Args        ::= Arg { "," Arg } [ "," ]
Arg         ::= [ NAME ":" ] Expr
```

Метка аргумента опциональна. Trailing comma разрешён.

```swift
f()
f(1, 2, 3)
f(name: "Alice", age: 30)
f(1, 2,)
```

> ℹ️ **Примечание:** `name:` разбирается только если `NAME` — идентификатор и следующий токен `:`. В противном случае разбор идёт как обычное выражение. Это означает, что `f(x: 5)` интерпретируется как именованный аргумент, а `f((x): 5)` — как ошибка или неожиданный разбор.

### Trailing closure

Если после postfix-выражения идёт `{`, и `flags.noTrailing` не установлен, парсер разбирает `{` как замыкание и **прикрепляет** его к последнему вызову.

Правила:

1. Если узел — `Call`, замыкание **добавляется в конец** `args`.
2. Если узел — не `Call`, замыкание **оборачивается в новый `Call`** с одним аргументом.

```swift
nums.map { $0 * 2 }              // → Call(Member(nums, map), [Closure])
nums.filter { $0 > 0 }.count     // → Member(Call(Member(...), [Closure]), count)
let f = { 42 }                   // это не trailing closure, а просто замыкание
```

Антипример:

```swift
f(a) { x } { y }    // ошибка: две trailing closure подряд
```

Trailing closure **не применяется**:

- В условии `if`, `while`, `for`, `switch` (там установлен `noTrailing: true`).
- В аргументах вызова (`parseArgs` вызывает `parseExpr(0)` без флага, но `parsePostfix` в теле аргумента всё ещё может встретить `{`).
- В левой части присваивания.

> ⚠️ **Внимание:** `flags.noTrailing` действует на **весь** подвыражения ниже. Это значит, что `if f(a) { b } { c }` разберётся как `if (f(a)) { b } else ...` — нет, как `if (f(a)) { b }` и второй `{ c }` останется. Он попадёт в тело `if`, но не как trailing closure.

### Primary-выражения

```
Primary     ::= NUM | STR | "true" | "false" | "nil"
              | "if" Expr Block [ "else" ... ]
              | "switch" Expr "{" ... "}"
              | "." NAME                             — EnumCase
              | "[" [ Args ] "]"                     — Array или Dict
              | "(" [ Expr ] ")"                     — группировка или tuple
              | "{" ClosureBody "}"                  — замыкание
              | "func" ... "{" ... "}"               — анонимная функция
              | NAME
              | "self"
```

#### Массивы и словари

```
Array       ::= "[" Expr { "," Expr } [ "," ] "]"
Dict        ::= "[" Expr ":" Expr { "," Expr ":" Expr } [ "," ] "]"
```

Различие — по первому `:`. Если после первого элемента идёт `:`, всё выражение — словарь. Иначе — массив.

```swift
[1, 2, 3]
["a": 1, "b": 2]
[]                          // пустой массив
["a": 1, "b": 2,]           // trailing comma
```

> ⚠️ **Внимание:** смешанный случай `[1, "a": 2]` — ошибка. Первый элемент `1`, после него `,`, не `:`. Начинается массив, второй элемент `"a": 2` — ошибка синтаксиса.

#### Группировка и tuple

```
Paren       ::= "(" ")"                     — пустой tuple
              | "(" Expr ")"                — группировка (возвращает Expr)
              | "(" Expr "," Expr { "," Expr } ")"  — tuple
```

```swift
(5)                 // не tuple, а группировка → Lit 5
()                  // Tuple с пустым items
(1, 2)              // Tuple
(1, 2, 3)           // Tuple
(a: 1, b: 2)        // именованные элементы: `a:` и `b:` игнорируются парсером
```

> ⚠️ **Внимание:** `(a: 1, b: 2)` разбирается как tuple `[Lit 1, Lit 2]`. Метки `a:` и `b:` **отбрасываются** парсером. Если нужны именованные поля — используйте структуры.

#### `.name` как EnumCase

```swift
Status.warn("slow")     // Member(Call(...)) — обычный путь
.warn("slow")           // EnumCase(warn) с последующим Call
.ok                     // EnumCase(ok)
```

Разбор `.name` в primary работает только если `.` — первый токен выражения. Внутри postfix-цепочки `.name` — это `Member`.

### Замыкания

```
ClosureBody ::= [ Params "in" ] { Stmt }
Params      ::= "(" [ ClosureParams ] ")" [ "->" Type ]
              | NAME { "," NAME }
```

Два синтаксиса параметров:

1. `(a, b) in` или `(a: Int, b: String) -> Int in`
2. `a, b in`

Парсер использует **backtracking** через try/catch. Сначала пытается разобрать `(...)` как список параметров, откатывается при неудаче. Затем пытается разобрать `a, b in`. Если оба варианта не подходят — замыкание без параметров.

```swift
{ $0 * 2 }
{ (x: Int) -> Int in x + 1 }
{ x, y in x + y }
{ print("hi") }
```

> ⚠️ **Внимание:** backtracking через try/catch **может давать неожиданные результаты** на неоднозначных входах. Например, `{ (x) }` — это замыкание без параметров с телом `(x)` (группировка), а не замыкание с параметром `x`.

### `parseString` — интерполяция

Строковый литерал без `\(` — это `Lit`. С `\(` — `Template` с массивом частей.

```
Template    ::= { TextPart | ExprPart }
TextPart    ::= — текст без `\(`
ExprPart    ::= "\(" Expr ")"
```

Внутри `\(...)` рекурсивно вызывается лексер и парсер. Результат — выражение.

```swift
"Hello, \(name)!"
"Result: \(x + y)"
""
"\(a)\(b)\(c)"
```

Антипример:

```swift
"\(let x = 5)"      // ошибка: интерполяция должна быть выражением
"\(a; b)"           // ошибка: `;` внутри \()
```

## Pattern (для `switch`)

```
Pattern     ::= "." NAME [ "(" [ Bindings ] ")" ]     — PatEnum
              | "let" NAME                            — PatBind
              | "_"                                   — PatWild
              | "true" | "false" | "nil"              — PatLit
              | Expr                                  — PatLit | PatRange | PatExpr
Bindings    ::= Binding { "," Binding }
Binding     ::= "let" NAME | "_" | NAME
```

Разбор:

1. Если первый токен `.` — `PatEnum` с опциональными bindings.
2. Если `let` — `PatBind`.
3. Если `_` — `PatWild`.
4. Если `true`/`false`/`nil` — `PatLit`.
5. Иначе — разбор как выражения, классификация:
   - `Lit` → `PatLit`
   - `Bin` с `...` или `..<` → `PatRange`
   - всё остальное → `PatExpr`

```swift
case .ok:
case .warn(let msg):
case .error(let code, let msg):
case .error(_, let msg):
case let x:
case _:
case 200:
case 400...499:
case 500..<600:
case SOME_CONST:
```

> ⚠️ **Внимание:** bindings в `case .error(code, msg)` разбираются как **прямые имена**, не как паттерны. `let` перед именем опционален. Это значит, что `case .error(_, let x)` и `case .error(_, x)` работают одинаково.

## Типы (синтаксис)

Типы разбираются, сохраняются в AST, но **не используются** кодогенератором. Их разбор нужен только чтобы не спотыкаться на аннотациях.

```
Type        ::= "[" Type "]" [ "?" ]              — Array
              | "(" [ Type { "," Type } ] ")" [ "?" ]  — Tuple
              | NAME [ "<" ... ">" ] [ "?" ]      — Named
```

Generics разбираются как `NAME<...>`, но **внутренность отбрасывается**. `Array<Int>` в AST — это `Named(Array)`, а `Int` теряется.

```swift
Int
String?
[Int]
[String]?
(a: Int, b: String)     // метки отбрасываются
Dictionary<String, Int>
Array<Array<Int>>
```

> ⚠️ **Внимание:** вложенные generics разбираются с учётом глубины скобок. `<` и `>` считаются. Но `a<b>` без пробелов может быть спутано с сравнением в **выражениях**, не в типах.

## Разделители и точки с запятой

Точка с запятой **опциональна** везде, где встречается `match("op", ";")`:

- После `let`/`var`.
- После `return`.
- После `break`/`continue`.
- После поля структуры.
- После случая enum.
- После expression statement.

Точка с запятой **не обязательна** нигде, но **разрешается** везде.

> ⚠️ **Внимание:** отсутствие точки с запятой означает, что **разделение statements — по первому токену**. `let x = 5 let y = 6` не разберётся: после `5` парсер ждёт либо `;`, либо следующий токен statement. `let` как продолжение выражения не подходит — `parseExpr` остановится на `let`. Но `parseStmt` для `let x = 5` завершится и вернётся в `parseProgram`, который увидит `let` и начнёт новый statement. **Это работает**, потому что `parseExpr` останавливается на `kw`.

## Проблемные случаи

### Неоднозначность `{` после выражения

```swift
let f = { 42 }              // замыкание
let x = f { 42 }            // вызов с trailing closure
if cond { body }            // блок
if cond { body } { other }  // ошибка или неожиданный разбор
```

Правило: `{` после выражения — trailing closure, если `noTrailing` не установлен. `if`/`while`/`for`/`switch` устанавливают флаг для условий.

### `switch` как выражение и statement

`switch` в statement position оборачивается в `Expr`:

```swift
switch x { case 1: return "one" }
```

В expression position — разбирается напрямую:

```swift
let s = switch x { case 1: return "one"; default: return "other" }
```

> ❓ **Требуется уточнение:** возвращает ли `switch` как выражение значение. В кодогенераторе он оборачивается в IIFE, но внутри используются `return`, которые выходят из IIFE, а не из окружающей функции.

### Вложенные trailing closure

```swift
f(a) { $0 }.g { $1 }     // разбирается: f(a, closure).g(closure)
```

Работает, потому что trailing closure прикрепляется к ближайшему `Call`.

```swift
f { g { h } }            // f(closure внутри которой g(closure внутри которой h))
```

Работает, потому что каждый `{` разбирается рекурсивно.

### Точка после числа

`1.5` — число. `1.` — число `1` и точка. `1.foo` — число `1`, точка, `foo`.

Парсер не делает специальных проверок на `1.foo` — разбор пойдёт как `Member(Lit 1, foo)`. Это позволит получить ошибку только на этапе type checker'а, если `Int` не имеет поля `foo`.

## Ограничения v1

- Нет `do { } while`.
- Нет `repeat { } while`.
- Нет labeled statements (`label: for ...`).
- Нет `continue label` / `break label`.
- Нет destructuring в `for` (только один NAME).
- Нет destructuring в `let` (`let (a, b) = ...`).
- Нет `if case`, `guard case`, `while case`.
- Нет `switch` на tuple.
- Нет pattern matching на структурах.
- Нет default parameters с выражениями.
- Нет variadic parameters.
- Нет `inout` parameters.
- Нет `throws` в сигнатуре функции.
- Нет `catch` без `try`.

Каждое из этих расширений можно добавить, но это повлияет на существующий код.

## Смотрите также

- [LEXER_SPEC.md](LEXER_SPEC.md) — какие токены приходят на вход.
- [AST_SCHEMA.md](AST_SCHEMA.md) — какие узлы строит парсер.
- [DIAGNOSTICS.md](DIAGNOSTICS.md) — какие ошибки бросает парсер.

---

## Открытые вопросы

❓ **Обязательность типов в полях структуры.** `let x: Int` — обязательна, `let x = 5` — ошибка. В локальных `let` наоборот. Стоит ли унифицировать?

❓ **Возврат значения из `switch` и `if` как выражений.** Сейчас генерируется IIFE с `return` внутри. Значит `let x = if cond { 1 } else { 2 }` работает, но `let x = if cond { return 1 } else { return 2 }` — нет. Является ли `return` внутри expression-`if` ошибкой или особенностью?

❓ **Trailing closure и тернарник.** `f(a) { $0 } ? b : c` — разбирается ли это как `(f(a, closure)) ? b : c` или как `f(a, closure ? b : c)`? В текущем парсере — первое, потому что ternary разбирается на уровне `minPrec === 0`, а trailing closure — в postfix.

❓ **Порядок разрешения `foo.bar` — Member или EnumCase.** Сейчас `.name` в primary — EnumCase. Но `foo .bar` (с пробелом) — это `foo` + EnumCase? Нет — `foo` (primary), затем postfix видит `.`, разбирает `.bar` как Member. Пробел между `foo` и `.` не важен. Стоит ли **запретить** пробел перед `.` для ясности?

❓ **Backtracking в `parseClosure`.** Использование try/catch может маскировать ошибки. Например, `{ (x: Foo<bar>) in x }` — если `Foo<bar>` не разберётся, замыкание откатится к варианту без параметров, и `(x: Foo<bar>)` станет телом. Это **тихая ошибка**, не диагностика. Стоит ли ужесточить разбор?

❓ **`parseType` для generic'ов.** Внутренность `<...>` отбрасывается. Есть ли случай, когда это ломает разбор? Например, `Array<(Int) -> String>` — `>` внутри `->` может быть спутан.
