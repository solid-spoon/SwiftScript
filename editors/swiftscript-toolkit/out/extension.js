const _force = (x) => {
  if (x === null || x === undefined) throw new Error("Force unwrap of nil");
  return x;
};
const print = (...args) => console.log(...args);
const String = (x) => {
  if (x === null || x === undefined) return "nil";
  if (Array.isArray(x)) return "[" + x.map(String).join(", ") + "]";
  if (typeof x === "object") {
    const pairs = Object.entries(x)
      .map(([k, v]) => k + ": " + String(v))
      .join(", ");
    return "{" + pairs + "}";
  }
  return "" + x;
};
const Int = (x) => {
  const n = parseInt(x, 10);
  return Number.isNaN(n) ? null : n;
};
const Double = (x) => {
  const n = parseFloat(x);
  return Number.isNaN(n) ? null : n;
};
const Bool = (x) => Boolean(x);
const abs = Math.abs,
  min = Math.min,
  max = Math.max;
const round = Math.round,
  floor = Math.floor,
  ceil = Math.ceil;
const _range = (a, b) => {
  const r = [];
  for (let i = a; i < b; i++) r.push(i);
  return r;
};
const _rangeIn = (a, b) => {
  const r = [];
  for (let i = a; i <= b; i++) r.push(i);
  return r;
};

import * as vscode from "vscode";
const DeclKind = Object.freeze({
  classDecl: Object.freeze({ _case: "classDecl" }),
  structDecl: Object.freeze({ _case: "structDecl" }),
  enumDecl: Object.freeze({ _case: "enumDecl" }),
  funcDecl: Object.freeze({ _case: "funcDecl" }),
  letDecl: Object.freeze({ _case: "letDecl" }),
});
class Decl {
  constructor(name, kind, line, endLine, signature) {
    this.name = name;
    this.kind = kind;
    this.line = line;
    this.endLine = endLine;
    this.signature = signature;
    this.children = [];
  }
}
function stripComment(line) {
  const pos = line.indexOf("//");
  if (pos >= 0) {
    return line.substring(0, pos);
  }
  return line;
}
function countOccurrences(s, sub) {
  if (sub.length == 0) {
    return 0;
  }
  return s.split(sub).length - 1;
}
function charAt(s, i) {
  if (i < 0 || i >= s.length) {
    return "";
  }
  return s.substring(i, i + 1);
}
function isIdentPart(c) {
  if (c.length == 0) {
    return false;
  }
  if (c == "_" || c == "$") {
    return true;
  }
  if (c >= "a" && c <= "z") {
    return true;
  }
  if (c >= "A" && c <= "Z") {
    return true;
  }
  if (c >= "0" && c <= "9") {
    return true;
  }
  return false;
}
function findBlockEnd(lines, start) {
  let depth = 0;
  let sawOpen = false;
  let i = start;
  while (i < lines.length) {
    const code = stripComment(lines[i]);
    const opens = countOccurrences(code, "{");
    const closes = countOccurrences(code, "}");
    if (opens > 0) {
      sawOpen = true;
    }
    depth += opens - closes;
    if (sawOpen && depth <= 0) {
      return i;
    }
    i += 1;
  }
  if (lines.length == 0) {
    return 0;
  }
  return lines.length - 1;
}
function detectDecl(raw, lineNo) {
  const code = stripComment(raw);
  const t = code.trim();
  if (t.length == 0) {
    return null;
  }
  const words = t.split(" ");
  let idx = 0;
  let kind = null;
  for (const w of words) {
    if (w.length == 0) {
      continue;
    }
    if (w == "class") {
      kind = DeclKind.classDecl;
      break;
    }
    if (w == "struct") {
      kind = DeclKind.structDecl;
      break;
    }
    if (w == "enum") {
      kind = DeclKind.enumDecl;
      break;
    }
    if (w == "func") {
      kind = DeclKind.funcDecl;
      break;
    }
    if (w == "let" || w == "var") {
      kind = DeclKind.letDecl;
      break;
    }
    if (
      w == "private" ||
      w == "public" ||
      w == "internal" ||
      w == "static" ||
      w == "async" ||
      w == "override" ||
      w == "lazy" ||
      w == "export"
    ) {
      idx += 1;
      continue;
    }
    return null;
  }
  if (kind == null) {
    return null;
  }
  if (idx + 1 >= words.length) {
    return null;
  }
  let rawName = words[idx + 1];
  rawName = rawName.split("(")[0];
  rawName = rawName.split("{")[0];
  rawName = rawName.split(":")[0];
  const name = rawName.trim();
  if (name.length == 0) {
    return null;
  }
  return new Decl(name, _force(kind), lineNo, lineNo, t);
}
function parseDecls(lines) {
  let result = [];
  let stack = [];
  let i = 0;
  while (i < lines.length) {
    const d = detectDecl(lines[i], i);
    if (d != null) {
      const decl = _force(d);
      decl.endLine = findBlockEnd(lines, i);
      const code = stripComment(lines[i]);
      const opens = countOccurrences(code, "{");
      const closes = countOccurrences(code, "}");
      while (stack.length > 0 && stack[stack.length - 1].endLine < i) {
        stack.pop();
      }
      if (stack.length > 0) {
        stack[stack.length - 1].children.push(decl);
      } else {
        result.push(decl);
      }
      if (opens > closes) {
        stack.push(decl);
      }
    }
    i += 1;
  }
  return result;
}
function kindToVscode(kind) {
  _sw1: {
    const _s1 = kind;
    if (_s1 && _s1._case === "classDecl") {
      return vscode.SymbolKind.Class;
      break _sw1;
    }
    if (_s1 && _s1._case === "structDecl") {
      return vscode.SymbolKind.Struct;
      break _sw1;
    }
    if (_s1 && _s1._case === "enumDecl") {
      return vscode.SymbolKind.Enum;
      break _sw1;
    }
    if (_s1 && _s1._case === "funcDecl") {
      return vscode.SymbolKind.Function;
      break _sw1;
    }
    if (_s1 && _s1._case === "letDecl") {
      return vscode.SymbolKind.Variable;
      break _sw1;
    }
  }
  return vscode.SymbolKind.Variable;
}
function toVscodeSymbol(d) {
  const startPos = new vscode.Position(d.line, 0);
  const endPos = new vscode.Position(d.endLine, 1000);
  const range = new vscode.Range(startPos, endPos);
  const sel = new vscode.Range(startPos, startPos);
  const sym = new vscode.DocumentSymbol(
    d.name,
    d.signature,
    kindToVscode(d.kind),
    range,
    sel,
  );
  let ch = [];
  for (const c of d.children) {
    ch.push(toVscodeSymbol(c));
  }
  sym.children = ch;
  return sym;
}
function wordAt(line, col) {
  if (col >= line.length) {
    return "";
  }
  let start = col;
  while (start > 0) {
    if (!isIdentPart(charAt(line, start - 1))) {
      break;
    }
    start -= 1;
  }
  let end = col;
  while (end < line.length) {
    if (!isIdentPart(charAt(line, end))) {
      break;
    }
    end += 1;
  }
  return line.substring(start, end);
}
class SymbolProvider {
  provideDocumentSymbols(doc, token) {
    const text = doc.getText();
    const lines = text.split("\n");
    const decls = parseDecls(lines);
    let result = [];
    for (const d of decls) {
      result.push(toVscodeSymbol(d));
    }
    return result;
  }
}
class FoldingProvider {
  provideFoldingRanges(doc, context, token) {
    const text = doc.getText();
    const lines = text.split("\n");
    let ranges = [];
    let stack = [];
    let i = 0;
    while (i < lines.length) {
      const code = stripComment(lines[i]);
      const opens = countOccurrences(code, "{");
      const closes = countOccurrences(code, "}");
      let j = 0;
      while (j < opens) {
        stack.push(i);
        j += 1;
      }
      j = 0;
      while (j < closes && stack.length > 0) {
        const start = _force(stack.pop());
        if (i - start > 1) {
          ranges.push(
            new vscode.FoldingRange(start, i, vscode.FoldingRangeKind.Region),
          );
        }
        j += 1;
      }
      i += 1;
    }
    return ranges;
  }
}
class HoverProvider {
  constructor() {
    this.descriptions = {
      ["let"]: "Immutable binding. Compiles to JS `const`.",
      ["var"]: "Mutable binding. Compiles to JS `let`.",
      ["func"]: "Function declaration.",
      ["struct"]:
        "Value type. Auto-frozen with `Object.freeze` if all fields are `let`.",
      ["class"]:
        "Reference type. Supports `extends`, `super.init`, `override`.",
      ["enum"]: "Tagged union. Cases may carry a payload.",
      ["guard"]:
        "Early exit. `else` must terminate (return / throw / break / continue).",
      ["switch"]:
        "Pattern matching. Supports enum cases, ranges (`400...499`), `where`.",
      ["async"]: "Marks a function as async (returns Promise).",
      ["await"]: "Suspends until the Promise resolves.",
      ["try"]: "Wraps code that may throw. Paired with `catch`.",
      ["throw"]: "Throws an error. Caught by `try/catch`.",
      ["new"]:
        "Constructs a class. Auto-added for classes declared in the same module.",
      ["nil"]: "Null-ish. Compiles to JS `null`.",
      ["self"]: "Current instance. `this` in classes, `_self` in structs.",
      ["super"]: "Parent class. Used as `super.init(...)`.",
      ["import"]:
        "ESM import. Bare specifiers (`vscode`, `node:fs`) stay as-is; `.sws` paths become `.js`.",
      ["export"]: "ESM export. Makes declaration visible to importers.",
      ["implements"]:
        "Interface contract. Erased at codegen (JS is duck-typed).",
      ["extends"]: "Inheritance. Only for `class`.",
    };
  }
  provideHover(doc, position, token) {
    const line = doc.lineAt(position.line).text;
    const word = wordAt(line, position.character);
    if (word.length == 0) {
      return null;
    }
    const desc = this.descriptions[word];
    if (desc == null) {
      return null;
    }
    return new vscode.Hover(`**${String(word)}** — ${String(desc)}`);
  }
}
class CompletionProvider {
  constructor() {
    this.keywords = [
      "let",
      "var",
      "func",
      "return",
      "if",
      "else",
      "for",
      "in",
      "while",
      "break",
      "continue",
      "struct",
      "enum",
      "class",
      "import",
      "export",
      "try",
      "throw",
      "catch",
      "async",
      "await",
      "true",
      "false",
      "nil",
      "self",
      "super",
      "new",
      "static",
      "private",
      "public",
      "internal",
      "override",
      "lazy",
      "throws",
      "guard",
      "implements",
      "extends",
    ];
    this.types = ["Int", "Double", "Float", "String", "Bool", "Any", "Void"];
    this.builtins = [
      "print",
      "String",
      "Int",
      "Double",
      "Bool",
      "abs",
      "min",
      "max",
      "round",
      "floor",
      "ceil",
    ];
  }
  provideCompletionItems(doc, position, token, context) {
    let items = [];
    for (const k of this.keywords) {
      items.push(
        new vscode.CompletionItem(k, vscode.CompletionItemKind.Keyword),
      );
    }
    for (const t of this.types) {
      items.push(new vscode.CompletionItem(t, vscode.CompletionItemKind.Class));
    }
    for (const b of this.builtins) {
      items.push(
        new vscode.CompletionItem(b, vscode.CompletionItemKind.Function),
      );
    }
    return items;
  }
}
export function activate(context) {
  const sel = "swiftscript";
  const sym = new SymbolProvider();
  context.subscriptions.push(
    vscode.languages.registerDocumentSymbolProvider(sel, sym),
  );
  const fold = new FoldingProvider();
  context.subscriptions.push(
    vscode.languages.registerFoldingRangeProvider(sel, fold),
  );
  const hov = new HoverProvider();
  context.subscriptions.push(vscode.languages.registerHoverProvider(sel, hov));
  const comp = new CompletionProvider();
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(sel, comp, "."),
  );
}
export function deactivate() {}
