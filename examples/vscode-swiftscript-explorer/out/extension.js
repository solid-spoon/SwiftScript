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
  constructor(label, symbolKind, line, uri) {
    super(label, NodeKind.symbol);
    (this.symbolKind = symbolKind);
    (this.line = line);
    (this.uri = uri);
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
function extractName(text, prefix) {
const rest = text.substring(prefix.length);
let end = rest.length;
const stops = [" ", "{", ":", "<"];
for (const s of stops) { const pos = rest.indexOf(s); if (((pos >= 0) && (pos < end))) { (end = pos); } }
return rest.substring(0, end);
}
function parseSymbols(text, uri) {
let result = [];
const lines = text.split("\n");
let index = 0;
for (const line of lines) { const t = line.trim(); let kind = undefined; let prefix = ""; if (t.startsWith("enum ")) { (kind = SymbolKind.enumDecl); (prefix = "enum "); } else if (t.startsWith("struct ")) { (kind = SymbolKind.structDecl); (prefix = "struct "); } else if (t.startsWith("class ")) { (kind = SymbolKind.classDecl); (prefix = "class "); } else if (t.startsWith("func ")) { (kind = SymbolKind.funcDecl); (prefix = "func "); } else if ((t.startsWith("let ") || t.startsWith("var "))) { (kind = SymbolKind.letDecl); (prefix = (t.startsWith("let ") ? "let " : "var ")); } {
  const _t1_0 = kind;
  if (_t1_0 != null) {
    const k = _t1_0;
    const name = extractName(t, prefix);
    const label = ((prefix.trim() + " ") + name);
    result.push(new SymbolNode(label, k, (index + 1), uri));
  }
} (index += 1); }
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
    for (const uri of uris) { try { const doc = (await vscode.workspace.openTextDocument(uri)); const text = doc.getText(); const symbols = parseSymbols(text, uri); const rel = vscode.workspace.asRelativePath(uri); result.push(new FileNode(rel, uri, symbols)); } catch (e) {  } }
    (this.files = result);
    return (this.loaded = true);
  }
  getTreeItem(element) {
    const collapsible = ((element.nodeKind == NodeKind.file) ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None);
    const item = new vscode.TreeItem(element.label, collapsible);
    if ((element.nodeKind == NodeKind.file)) { (item.iconPath = new vscode.ThemeIcon("file-code")); (item.contextValue = "swiftscriptFile"); (item.tooltip = element.label); } else { (item.iconPath = new vscode.ThemeIcon(iconFor(element.symbolKind))); (item.contextValue = "swiftscriptSymbol"); (item.command = ({["command"]: "swiftscriptExplorer.openSymbol", ["title"]: "Open", ["arguments"]: [element]})); }
    return item;
  }
  async getChildren(element) {
    if ((element == null)) { (await this.load()); return this.files; }
    if ((element.nodeKind == NodeKind.file)) { return element.symbols; }
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
