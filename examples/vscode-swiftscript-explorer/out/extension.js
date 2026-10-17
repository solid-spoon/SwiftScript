const _force = (x) => { if (x === null || x === undefined) throw new Error("Force unwrap of nil"); return x; };
const print = (...args) => console.log(...args);
const String = (x) => {
  if (x === null || x === undefined) return "nil";
  if (Array.isArray(x)) return "[" + x.map(String).join(", ") + "]";
  if (typeof x === "object") {
    const pairs = Object.entries(x).map(([k, v]) => k + ": " + String(v)).join(", ");
    return "{" + pairs + "}";
  }
  return "" + x;
};
const Int = (x) => { const n = parseInt(x, 10); return Number.isNaN(n) ? null : n; };
const Double = (x) => { const n = parseFloat(x); return Number.isNaN(n) ? null : n; };
const Bool = (x) => Boolean(x);
const abs = Math.abs, min = Math.min, max = Math.max;
const round = Math.round, floor = Math.floor, ceil = Math.ceil;
const _range   = (a, b) => { const r = []; for (let i = a; i <  b; i++) r.push(i); return r; };
const _rangeIn = (a, b) => { const r = []; for (let i = a; i <= b; i++) r.push(i); return r; };

import * as vscode from "vscode";
const SymbolKind = Object.freeze({
  enumDecl: Object.freeze({ _case: "enumDecl" }),
  structDecl: Object.freeze({ _case: "structDecl" }),
  classDecl: Object.freeze({ _case: "classDecl" }),
  funcDecl: Object.freeze({ _case: "funcDecl" }),
  letDecl: Object.freeze({ _case: "letDecl" }),
});
const NodeKind = Object.freeze({
  file: Object.freeze({ _case: "file" }),
  symbol: Object.freeze({ _case: "symbol" }),
});
class TreeNode {
  constructor(label, nodeKind) {
    (this.label = label);
    (this.nodeKind = nodeKind);
  }
}
class SymbolNode extends TreeNode {
  constructor(label, symbolKind, line, uri, isBlock) {
    super(label, NodeKind.symbol);
    (this.symbolKind = symbolKind);
    (this.line = line);
    (this.uri = uri);
    (this.isBlock = isBlock);
    (this.depth = 0);
    (this.children = []);
  }
}
class FileNode extends TreeNode {
  constructor(label, uri, symbols) {
    super(label, NodeKind.file);
    (this.uri = uri);
    (this.symbols = symbols);
  }
}
function iconFor(kind) {
_sw1: {
  const _s1 = kind;
  if (_s1 && _s1._case === "enumDecl") {
    return "symbol-enum";
    break _sw1;
  }
  if (_s1 && _s1._case === "structDecl") {
    return "symbol-struct";
    break _sw1;
  }
  if (_s1 && _s1._case === "classDecl") {
    return "symbol-class";
    break _sw1;
  }
  if (_s1 && _s1._case === "funcDecl") {
    return "symbol-method";
    break _sw1;
  }
  if (_s1 && _s1._case === "letDecl") {
    return "symbol-variable";
    break _sw1;
  }
}
return "symbol-misc";
}
function stripComment(line) {
const pos = line.indexOf("//");
if ((pos >= 0)) { return line.substring(0, pos); }
return line;
}
function countBraces(line, brace) {
const parts = line.split(brace);
return (parts.length - 1);
}
function parseDecl(t, originalLine, uri) {
const rawWords = t.split(" ");
let words = [];
for (const w of rawWords) { if ((w.length > 0)) { words.push(w); } }
let kind = null;
let idx = 0;
for (const w of words) { if ((w == "enum")) { (kind = SymbolKind.enumDecl); break; } if ((w == "struct")) { (kind = SymbolKind.structDecl); break; } if ((w == "class")) { (kind = SymbolKind.classDecl); break; } if ((w == "func")) { (kind = SymbolKind.funcDecl); break; } if (((w == "let") || (w == "var"))) { (kind = SymbolKind.letDecl); break; } if (((((((((w == "export") || (w == "private")) || (w == "public")) || (w == "internal")) || (w == "static")) || (w == "async")) || (w == "override")) || (w == "lazy"))) { (idx += 1); continue; } return null; }
if ((kind == null)) { return null; }
if (((idx + 1) >= words.length)) { return null; }
let name = words[(idx + 1)];
(name = name.split("(")[0]);
(name = name.split("{")[0]);
(name = name.split(":")[0]);
(name = name.trim());
const kindStr = words[idx];
const label = `${String(kindStr)} ${String(name)}`;
const k = _force(kind);
const isBlock = ((((k == SymbolKind.enumDecl) || (k == SymbolKind.structDecl)) || (k == SymbolKind.classDecl)) || (k == SymbolKind.funcDecl));
return new SymbolNode(label, k, originalLine, uri, isBlock);
}
function parseNestedSymbols(text, uri) {
let result = [];
let stack = [];
const lines = text.split("\n");
let lineNo = 1;
let currentDepth = 0;
for (const line of lines) { const code = stripComment(line); const t = code.trim(); if ((t.length > 0)) { const opens = countBraces(t, "{"); const closes = countBraces(t, "}"); {
  const _t1_0 = parseDecl(t, lineNo, uri);
  if (_t1_0 != null) {
    const n = _t1_0;
    while (((stack.length > 0) && (stack[(stack.length - 1)].depth >= currentDepth))) { stack.pop(); }
    if ((stack.length > 0)) { stack[(stack.length - 1)].children.push(n); } else { result.push(n); }
    if ((n.isBlock && (opens > closes))) { (n.depth = currentDepth); stack.push(n); }
  }
} (currentDepth += (opens - closes)); } (lineNo += 1); }
return result;
}
class SwiftScriptTreeProvider {
  constructor() {
    this.files = [];
    this.loaded = false;
    (this._onDidChangeTreeData = new vscode.EventEmitter());
    (this.onDidChangeTreeData = this._onDidChangeTreeData.event);
  }
  refresh() {
    (this.loaded = false);
    return this._onDidChangeTreeData.fire();
  }
  async load() {
    if (this.loaded) { return; }
    const uris = (await vscode.workspace.findFiles("**/*.sws"));
    let result = [];
    for (const uri of uris) { try { const doc = (await vscode.workspace.openTextDocument(uri)); const text = doc.getText(); const symbols = parseNestedSymbols(text, uri); const rel = vscode.workspace.asRelativePath(uri); result.push(new FileNode(rel, uri, symbols)); } catch (e) {  } }
    (this.files = result);
    return (this.loaded = true);
  }
  getTreeItem(element) {
    let hasChildren = false;
    if (((element.nodeKind == NodeKind.symbol) && (element.children.length > 0))) { (hasChildren = true); }
    const collapsible = (((element.nodeKind == NodeKind.file) || hasChildren) ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None);
    const item = new vscode.TreeItem(element.label, collapsible);
    if ((element.nodeKind == NodeKind.file)) { (item.iconPath = new vscode.ThemeIcon("file-code")); (item.contextValue = "swiftscriptFile"); (item.tooltip = element.label); } else { (item.iconPath = new vscode.ThemeIcon(iconFor(element.symbolKind))); (item.contextValue = "swiftscriptSymbol"); (item.command = ({["command"]: "swiftscriptExplorer.openSymbol", ["title"]: "Open", ["arguments"]: [element]})); }
    return item;
  }
  async getChildren(element) {
    if ((element == null)) { (await this.load()); return this.files; }
    if ((element.nodeKind == NodeKind.file)) { return element.symbols; }
    if (((element.nodeKind == NodeKind.symbol) && (element.children.length > 0))) { return element.children; }
    return [];
  }
}
export function activate(context) {
const provider = new SwiftScriptTreeProvider();
const treeView = vscode.window.createTreeView("swiftscriptExplorer", ({["treeDataProvider"]: provider, ["showCollapseAll"]: true}));
context.subscriptions.push(treeView);
const refreshCmd = vscode.commands.registerCommand("swiftscriptExplorer.refresh", (() => { return provider.refresh(); }));
context.subscriptions.push(refreshCmd);
const openCmd = vscode.commands.registerCommand("swiftscriptExplorer.openSymbol", (async (node) => { if ((node.nodeKind == NodeKind.symbol)) { const line = (node.line - 1); const pos = new vscode.Position(line, 0); const range = new vscode.Range(pos, pos); (await vscode.window.showTextDocument(node.uri, ({["selection"]: range}))); } return null; }));
context.subscriptions.push(openCmd);
const watcher = vscode.workspace.createFileSystemWatcher("**/*.sws");
watcher.onDidChange(((_) => { return provider.refresh(); }));
watcher.onDidCreate(((_) => { return provider.refresh(); }));
watcher.onDidDelete(((_) => { return provider.refresh(); }));
context.subscriptions.push(watcher);
}
export function deactivate() {

}
