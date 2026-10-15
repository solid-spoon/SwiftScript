# AST_SCHEMA.md

Схема абстрактного синтаксического дерева SwiftScript. Документ описывает узлы, которые производит `js/parser.js` и потребляет `js/codegen.js`. Если код и документ расходятся — это баг.

## Обзор

AST — это дерево объектов JavaScript. Каждый узел — обычный объект с полем `k` (kind). Поля различаются по типу узла. Никаких классов, никаких прототипов, никаких скрытых свойств.

**Инвариант:** для любого узла `k` — строка, входящая в один из перечисленных ниже типов. Никаких других полей, кроме описанных в схеме узла и `_lastLine`, не появляется.

**Инвариант:** AST не содержит ссылок на токены. Все строковые значения — копии, не `StringView`.

**Инвариант:** AST не содержит циклов. Это дерево, не граф.

## Общие поля

Поле `_lastLine: number | undefined` может присутствовать на любом узле, построенном через `withLine`. Служебное. Используется парсером для остановки postfix-цепочек. **Codegen не должен на него полагаться.**

## Список типов узлов

| `k` | Категория | Описание |
|-----|-----------|----------|
| `Program` | корень | Программа |
| `Let` | statement | Объявление `let`/`var` |
| `Return` | statement | Возврат |
| `Break` | statement | Выход из цикла |
| `Continue` | statement | Продолжение цикла |
| `While` | statement | Цикл `while` |
| `For` | statement | Цикл `for-in` |
| `Block` | statement | Блок `{ ... }` |
| `Expr` | statement | Выражение как statement |
| `Func` | declaration | Функция |
| `Struct` | declaration | Структура |
| `Enum` | declaration | Перечисление |
| `Lit` | expression | Литерал |
| `Id` | expression | Идентификатор |
| `EnumCase` | expression | Короткая форма `.name` |
| `Array` | expression | Массив |
| `Dict` | expression | Словарь |
| `Tuple` | expression | Кортеж |
| `Template` | expression | Интерполированная строка |
| `Member` | expression | Доступ к полю |
| `Index` | expression | Доступ по индексу |
| `Call` | expression | Вызов |
| `Force` | expression | Принудительная развёртка `!` |
| `Unary` | expression | Унарный оператор |
| `Bin` | expression | Бинарный оператор |
| `Ternary` | expression | Тернарный оператор |
| `Assign` | expression | Присваивание |
| `If` | expression | Условное выражение |
| `Switch` | expression | Множественный выбор |
| `Closure` | expression | Замыкание или анонимная функция |
| `Try` | expression | Обёртка `try` |
| `Await` | expression | Обёртка `await` |

## Statements

### `Program`

Корень дерева. Единственный экземпляр на файл.

```js
{
    k: "Program",
    stmts: [Stmt, ...]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `stmts` | `Stmt[]` | Top-level statements |

### `Let`

```js
{
    k: "Let",
    kind: "let" | "var",
    name: string,
    type: Type | null,
    value: Expr | null
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `kind` | `string` | Исходный вид объявления |
| `name` | `string` | Имя переменной |
| `type` | `Type \| null` | Аннотация типа, если указана |
| `value` | `Expr \| null` | Значение, если указано |

`type` **сохраняется** в AST, но codegen его не использует. Это сделано для будущего type checker'а.

### `Return`

```js
{
    k: "Return",
    expr: Expr | null
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `expr` | `Expr \| null` | Возвращаемое выражение |

`expr === null` — возврат без значения (`return` или `return;`).

### `Break`, `Continue`

```js
{ k: "Break" }
{ k: "Continue" }
```

Без полей.

### `While`

```js
{
    k: "While",
    cond: Expr,
    body: Block
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `cond` | `Expr` | Условие цикла |
| `body` | `Block` | Тело цикла |

### `For`

```js
{
    k: "For",
    name: string,
    src: Expr,
    body: Block
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `name` | `string` | Имя переменной цикла |
| `src` | `Expr` | Источник итерации |
| `body` | `Block` | Тело цикла |

Поддерживается только один идентификатор в роли переменной. Деструктуризация `for (a, b) in ...` не реализована.

### `Block`

```js
{
    k: "Block",
    stmts: [Stmt, ...]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `stmts` | `Stmt[]` | Statements блока |

### `Expr` (statement)

Обёртка для выражения, используемого как statement.

```js
{
    k: "Expr",
    expr: Expr
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `expr` | `Expr` | Обёрнутое выражение |

> ℹ️ **Примечание:** `if` и `switch` на top-level оборачиваются в `Expr`. Внутри выражений они используются напрямую.

## Declarations

### `Func`

```js
{
    k: "Func",
    name: string,
    params: Param[],
    retType: Type | null,
    body: Stmt[]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `name` | `string` | Имя функции |
| `params` | `Param[]` | Параметры |
| `retType` | `Type \| null` | Возвращаемый тип, если указан |
| `body` | `Stmt[]` | Тело функции |

**`Param`:**

```js
{
    label: string | null,
    name: string,
    type: Type | null
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `label` | `string \| null` | Метка аргумента (`_` даёт `null`, `name` даёт `name`) |
| `name` | `string` | Имя параметра внутри функции |
| `type` | `Type \| null` | Аннотация типа |

В codegen используется только `name`. `label` сохраняется для будущей реализации меток.

> ⚠️ **Внимание:** `body` — массив statements, не `Block`. Это единственное место, где тело функции — плоский массив. `Block` как узел существует отдельно.

### `Struct`

```js
{
    k: "Struct",
    name: string,
    fields: Field[],
    methods: Func[]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `name` | `string` | Имя структуры |
| `fields` | `Field[]` | Поля |
| `methods` | `Func[]` | Методы |

**`Field`:**

```js
{
    kind: "let" | "var",
    name: string,
    type: Type
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `kind` | `string` | Вид поля |
| `name` | `string` | Имя поля |
| `type` | `Type` | Тип поля (обязателен) |

### `Enum`

```js
{
    k: "Enum",
    name: string,
    cases: Case[]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `name` | `string` | Имя перечисления |
| `cases` | `Case[]` | Случаи |

**`Case`:**

```js
{
    name: string,
    params: Param[]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `name` | `string` | Имя случая |
| `params` | `Param[]` | Параметры payload'а |

`params` использует ту же структуру, что у функции.

## Expressions

### `Lit`

```js
{
    k: "Lit",
    v: number | string | boolean | null
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `v` | `number \| string \| boolean \| null` | Значение литерала |

`v === null` — литерал `nil`. `v === true`/`false` — булевы. `v` типа `number` — числовой. `v` типа `string` — строковый без интерполяции.

### `Id`

```js
{
    k: "Id",
    name: string
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `name` | `string` | Имя идентификатора |

`self` разбирается как `Id` с `name: "self"`.

### `EnumCase`

```js
{
    k: "EnumCase",
    name: string
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `name` | `string` | Имя случая без точки |

Возникает только из primary-разбора `.name`. Внутри postfix-цепочки `.name` превращается в `Member`.

### `Array`

```js
{
    k: "Array",
    items: Expr[]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `items` | `Expr[]` | Элементы |

### `Dict`

```js
{
    k: "Dict",
    pairs: Pair[]
}
```

**`Pair`:**

```js
{
    key: Expr,
    value: Expr
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `key` | `Expr` | Ключ |
| `value` | `Expr` | Значение |

### `Tuple`

```js
{
    k: "Tuple",
    items: Expr[]
}
```

Возникает при разборе `(a, b, ...)` с двумя и более элементами. `(x)` — это не `Tuple`, а `x`. `()` — это `Tuple` с пустым `items`.

### `Template`

```js
{
    k: "Template",
    parts: Part[]
}
```

**`Part`:**

```js
{
    type: "text",
    value: string
}
```

или

```js
{
    type: "expr",
    value: Expr
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `type` | `"text" \| "expr"` | Вид части |
| `value` | `string \| Expr` | Содержимое |

Text-части содержат уже декодированные escape-последовательности (`\n`, `\t`, `\\` и другие). Expr-части содержат разобранные выражения интерполяции.

### `Member`

```js
{
    k: "Member",
    obj: Expr,
    name: string,
    optional: boolean | undefined
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `obj` | `Expr` | Объект |
| `name` | `string` | Имя поля |
| `optional` | `boolean \| undefined` | `true` для `?.` |

`optional === true` — опциональная цепочка `obj?.name`. Отсутствие поля — `obj.name`.

### `Index`

```js
{
    k: "Index",
    obj: Expr,
    idx: Expr,
    optional: boolean | undefined
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `obj` | `Expr` | Объект |
| `idx` | `Expr` | Индекс |
| `optional` | `boolean \| undefined` | `true` для `?.[]` |

### `Call`

```js
{
    k: "Call",
    callee: Expr,
    args: Arg[],
    optional: boolean | undefined
}
```

**`Arg`:**

```js
{
    name: string | null,
    value: Expr
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `callee` | `Expr` | Вызываемое выражение |
| `args` | `Arg[]` | Аргументы |
| `optional` | `boolean \| undefined` | `true` для `callee?.()` |

`Arg.name` — метка аргумента или `null` для безымянного.

### `Force`

```js
{
    k: "Force",
    arg: Expr
}
```

Принудительная развёртка опционала `arg!`. В codegen превращается в `_force(arg)`.

### `Unary`

```js
{
    k: "Unary",
    op: string,
    arg: Expr
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `op` | `string` | Один из `-`, `!`, `+` |
| `arg` | `Expr` | Операнд |

### `Bin`

```js
{
    k: "Bin",
    op: string,
    l: Expr,
    r: Expr
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `op` | `string` | Оператор |
| `l` | `Expr` | Левый операнд |
| `r` | `Expr` | Правый операнд |

Операторы: `??`, `||`, `&&`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `+`, `-`, `*`, `/`, `%`, `..<`, `...`, `|>`, `->`. Последние два не должны появляться в `Bin` — `->` парсится только в контексте замыканий, `|>` — только если реализован. В текущем парсере `|>` разбирается как `Bin`.

### `Ternary`

```js
{
    k: "Ternary",
    cond: Expr,
    then: Expr,
    else: Expr
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `cond` | `Expr` | Условие |
| `then` | `Expr` | Значение при истине |
| `else` | `Expr` | Значение при лжи |

### `Assign`

```js
{
    k: "Assign",
    target: Expr,
    op: string,
    value: Expr
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `target` | `Expr` | Целевое выражение |
| `op` | `string` | Один из `=`, `+=`, `-=`, `*=`, `/=`, `%=` |
| `value` | `Expr` | Присваиваемое значение |

### `If`

```js
{
    k: "If",
    cond: Expr,
    then: Block,
    else: Block | If | null
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `cond` | `Expr` | Условие |
| `then` | `Block` | Ветка истины |
| `else` | `Block \| If \| null` | Ветка лжи или `else if` |

Если `else` — `If`, это цепочка `else if`. Если `null` — ветки нет.

### `Switch`

```js
{
    k: "Switch",
    subject: Expr,
    cases: Case[],
    defaultBody: Stmt[] | null
}
```

**`Case`:**

```js
{
    patterns: Pattern[],
    guard: Expr | null,
    body: Stmt[]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `subject` | `Expr` | Проверяемое выражение |
| `cases` | `Case[]` | Ветки `case` |
| `defaultBody` | `Stmt[] \| null` | Тело `default` или `null` |

### Patterns

Общий вид: объект с полем `k`.

**`PatEnum`:**

```js
{
    k: "PatEnum",
    name: string,
    bindings: Binding[]
}
```

**`Binding`:**

```js
{
    name: string | null
}
```

`name === null` — позиция проигнорирована через `_`.

**`PatBind`:**

```js
{
    k: "PatBind",
    name: string
}
```

**`PatWild`:**

```js
{
    k: "PatWild"
}
```

**`PatLit`:**

```js
{
    k: "PatLit",
    v: number | string | boolean | null
}
```

**`PatRange`:**

```js
{
    k: "PatRange",
    lo: Expr,
    hi: Expr,
    inclusive: boolean
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `lo` | `Expr` | Нижняя граница |
| `hi` | `Expr` | Верхняя граница |
| `inclusive` | `boolean` | `true` для `...`, `false` для `..<` |

**`PatExpr`:**

```js
{
    k: "PatExpr",
    value: Expr
}
```

### `Closure`

```js
{
    k: "Closure",
    params: Param[] | null,
    body: Stmt[]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `params` | `Param[] \| null` | Параметры или `null`, если не указаны |
| `body` | `Stmt[]` | Тело |

`params === null` — замыкание без явных параметров. В codegen используются `$0`, `$1`, ... в зависимости от того, какие из них упомянуты в теле.

`Param` в замыкании имеет ту же структуру, что у функции, но `label` всегда `null`.

### `Try`

```js
{
    k: "Try",
    arg: Expr
}
```

Обёртка `try expr`. В codegen разворачивается прозрачно: `genExpr(n.arg)`.

### `Await`

```js
{
    k: "Await",
    arg: Expr
}
```

Обёртка `await expr`. В codegen превращается в `(await arg)`.

## Types

Типы сохраняются в AST, но не используются codegen'ом. Их структура минимальна.

**`Named`:**

```js
{
    k: "Named",
    name: string
}
```

**`Array`:**

```js
{
    k: "Array",
    inner: Type
}
```

**`Tuple`:**

```js
{
    k: "Tuple",
    items: Type[]
}
```

**`Opt`:**

```js
{
    k: "Opt",
    inner: Type
}
```

## Инварианты AST

Для любого корректного AST:

1. **Только перечисленные типы.** `k` любого узла входит в список выше. Никаких других.
2. **Нет `null` в позициях `Expr[]`.** Массивы не содержат `null` или `undefined`.
3. **`Block` содержит `Stmt[]`, не `Expr[]`.** Тело блока — statements.
4. **`Func.body` — `Stmt[]`, не `Block`.** Особый случай, отличается от `If.then`.
5. **`Call.args` содержит `Arg`, не `Expr`.** Каждый аргумент — объект с `name` и `value`.
6. **`Template.parts` непуст, если `Template` существует.** Парсер не создаёт пустых шаблонов — `""` это `Lit`.
7. **`Pattern` всегда один из семи типов.** Никаких `null` в `patterns`.
8. **`Type` не используется в codegen.** Но его структура валидна.

## Что AST не содержит

- **Позиции строк/колонок.** Только `_lastLine` на некоторых узлах. Полные позиции не сохраняются.
- **Ссылки на объявления.** `Id` хранит имя, а не указатель на `Func` или `Let`.
- **Информацию о типах.** Типы — только синтаксические аннотации.
- **Область видимости.** Разрешение имён — работа будущего checker'а.
- **Комментарии.** Лексер их пропускает, AST их не видит.

## Пример

Исходник:

```swift
func add(_ a: Int, _ b: Int) -> Int {
    return a + b
}

let result = add(3, 4)
```

AST:

```js
{
    k: "Program",
    stmts: [
        {
            k: "Func",
            name: "add",
            params: [
                { label: null, name: "a", type: { k: "Named", name: "Int" } },
                { label: null, name: "b", type: { k: "Named", name: "Int" } }
            ],
            retType: { k: "Named", name: "Int" },
            body: [
                {
                    k: "Return",
                    expr: {
                        k: "Bin",
                        op: "+",
                        l: { k: "Id", name: "a" },
                        r: { k: "Id", name: "b" }
                    }
                }
            ]
        },
        {
            k: "Let",
            kind: "let",
            name: "result",
            type: null,
            value: {
                k: "Call",
                callee: { k: "Id", name: "add" },
                args: [
                    { name: null, value: { k: "Lit", v: 3 } },
                    { name: null, value: { k: "Lit", v: 4 } }
                ]
            }
        }
    ]
}
```

## Смотрите также

- [GRAMMAR.md](GRAMMAR.md) — какие правила строят эти узлы.
- [CODEGEN_SPEC.md](CODEGEN_SPEC.md) — как узлы превращаются в JavaScript.
- [DIAGNOSTICS.md](DIAGNOSTICS.md) — какие ошибки бросает парсер при построении AST.

---

## Открытые вопросы

❓ **`_lastLine` в AST.** Это служебное поле парсера, но оно попадает в дерево. Должен ли codegen его игнорировать явно, или это подразумевается? Стоит ли вынести его в отдельную структуру рядом с узлом?

❓ **`If` как statement vs expression.** На top-level `If` оборачивается в `Expr`. Внутри — используется напрямую. Codegen различает их через `if (n.expr.k === "If")`. Стоит ли унифицировать, всегда оборачивая в `Expr`?

❓ **`Func.body` — `Stmt[]`, не `Block`.** Единственное место с такой асимметрией. Стоит ли привести к `Block`, чтобы единообразие?

❓ **Отсутствие позиций.** Диагностики указывают на токены, а не на узлы AST. Если checker найдёт ошибку типа, ему нечего показать пользователю. Нужны ли позиции в узлах AST?

❓ **`Try` и `Await` как узлы.** Сейчас это просто обёртки. Если в будущем появится настоящая обработка ошибок, `Try` может стать отдельным statement. Стоит ли закладывать это сейчас?

❓ **`Type` в AST.** Сохраняется, но не используется. Нужен ли он в AST, или должен быть отдельным слоем информации (например, `WeakMap<Node, Type>`)?
