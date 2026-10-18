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

(() => {
const COLS = 10;
const ROWS = 20;
const BLOCK_SIZE = 30;
const COLORS = ["#00f0f0", "#f0f000", "#a000f0", "#00f000", "#f00000", "#0000f0", "#f0a000"];
const SHAPES = [[[[0, 0, 0, 0], [1, 1, 1, 1], [0, 0, 0, 0], [0, 0, 0, 0]], [[0, 0, 1, 0], [0, 0, 1, 0], [0, 0, 1, 0], [0, 0, 1, 0]], [[0, 0, 0, 0], [0, 0, 0, 0], [1, 1, 1, 1], [0, 0, 0, 0]], [[0, 1, 0, 0], [0, 1, 0, 0], [0, 1, 0, 0], [0, 1, 0, 0]]], [[[1, 1], [1, 1]], [[1, 1], [1, 1]], [[1, 1], [1, 1]], [[1, 1], [1, 1]]], [[[0, 1, 0], [1, 1, 1], [0, 0, 0]], [[0, 1, 0], [0, 1, 1], [0, 1, 0]], [[0, 0, 0], [1, 1, 1], [0, 1, 0]], [[0, 1, 0], [1, 1, 0], [0, 1, 0]]], [[[0, 1, 1], [1, 1, 0], [0, 0, 0]], [[0, 1, 0], [0, 1, 1], [0, 0, 1]], [[0, 0, 0], [0, 1, 1], [1, 1, 0]], [[1, 0, 0], [1, 1, 0], [0, 1, 0]]], [[[1, 1, 0], [0, 1, 1], [0, 0, 0]], [[0, 0, 1], [0, 1, 1], [0, 1, 0]], [[0, 0, 0], [1, 1, 0], [0, 1, 1]], [[0, 1, 0], [1, 1, 0], [1, 0, 0]]], [[[1, 0, 0], [1, 1, 1], [0, 0, 0]], [[0, 1, 1], [0, 1, 0], [0, 1, 0]], [[0, 0, 0], [1, 1, 1], [0, 0, 1]], [[0, 1, 0], [0, 1, 0], [1, 1, 0]]], [[[0, 0, 1], [1, 1, 1], [0, 0, 0]], [[0, 1, 0], [0, 1, 0], [0, 1, 1]], [[0, 0, 0], [1, 1, 1], [1, 0, 0]], [[1, 1, 0], [0, 1, 0], [0, 1, 0]]]];
class Randomizer {
  constructor() {
    (this.bag = []);
  }
  next() {
    if ((this.bag.length == 0)) {
  (this.bag = [0, 1, 2, 3, 4, 5, 6]);
  let i = (this.bag.length - 1);
  while ((i > 0)) {
  const j = Int(Math.floor((Math.random() * (i + 1))));
  const temp = this.bag[i];
  (this.bag[i] = this.bag[j]);
  (this.bag[j] = temp);
  (i -= 1);
}
}
    return this.bag.pop();
  }
}
class Piece {
  constructor(type) {
    (this.type = type);
    (this.rotation = 0);
    (this.x = 3);
    (this.y = 0);
  }
  getMatrix() {
    return SHAPES[this.type][this.rotation];
  }
  clone() {
    const p = new Piece(this.type);
    (p.rotation = this.rotation);
    (p.x = this.x);
    (p.y = this.y);
    return p;
  }
}
class Game {
  constructor() {
    const boardCanvas = document.getElementById("board");
    const nextCanvas = document.getElementById("next");
    const holdCanvas = document.getElementById("hold");
    (this.boardCtx = boardCanvas.getContext("2d"));
    (this.nextCtx = nextCanvas.getContext("2d"));
    (this.holdCtx = holdCanvas.getContext("2d"));
    (this.scoreEl = document.getElementById("score"));
    (this.linesEl = document.getElementById("lines"));
    (this.levelEl = document.getElementById("level"));
    (this.board = []);
    let r = 0;
    while ((r < ROWS)) {
  let row = [];
  let c = 0;
  while ((c < COLS)) {
  row.push((-1));
  (c += 1);
}
  this.board.push(row);
  (r += 1);
}
    (this.randomizer = new Randomizer());
    (this.nextQueue = []);
    (this.holdPieceType = null);
    (this.canHold = true);
    (this.currentPiece = null);
    (this.score = 0);
    (this.lines = 0);
    (this.level = 1);
    (this.dropCounter = 0);
    (this.dropInterval = 1000);
    (this.lastTime = 0);
    (this.isGameOver = false);
    let i = 0;
    while ((i < 3)) {
  this.nextQueue.push(this.randomizer.next());
  (i += 1);
}
  }
  start() {
    this.spawnPiece();
    this.bindInput();
    return window.requestAnimationFrame(((time) => {
return this.update(time);
}));
  }
  spawnPiece() {
    const type = this.nextQueue.shift();
    this.nextQueue.push(this.randomizer.next());
    (this.currentPiece = new Piece(type));
    (this.canHold = true);
    if (this.collides(_force(this.currentPiece), 0, 0)) {
  (this.isGameOver = true);
  print("Game Over!");
}
    return null;
  }
  bindInput() {
    return document.addEventListener("keydown", ((event) => {
if (this.isGameOver) {
  return;
}
const key = event.key;
return (() => { if ((key == "ArrowLeft")) { return this.movePiece((-1), 0);
 } return (() => { if ((key == "ArrowRight")) { return this.movePiece(1, 0);
 } return (() => { if ((key == "ArrowDown")) { this.movePiece(0, 1);
(this.dropCounter = 0);
return (this.score += 1);
 } return (() => { if (((key == "ArrowUp") || (key == "x"))) { return this.rotatePiece(1);
 } return (() => { if ((key == "z")) { return this.rotatePiece((-1));
 } return (() => { if ((key == " ")) { return this.hardDrop();
 } return (() => { if (((key == "c") || (key == "Shift"))) { return this.hold();
 } return null; })(); })(); })(); })(); })(); })(); })();
}));
  }
  movePiece(dx, dy) {
    if ((this.currentPiece == null)) {
  return false;
}
    if ((!this.collides(_force(this.currentPiece), dx, dy))) {
  (_force(this.currentPiece).x += dx);
  (_force(this.currentPiece).y += dy);
  if ((dx != 0)) {
  (this.lockDelayStart = this.lastTime);
}
  return true;
}
    return false;
  }
  rotatePiece(dir) {
    if ((this.currentPiece == null)) {
  return;
}
    const p = _force(this.currentPiece);
    const prevRot = p.rotation;
    (p.rotation = (((p.rotation + dir) + 4) % 4));
    const kicks = [0, (-1), 1, (-2), 2];
    let kicked = false;
    for (const kick of kicks) {
  if ((!this.collides(p, kick, 0))) {
  (p.x += kick);
  (kicked = true);
  break;
}
}
    if ((!kicked)) {
  (p.rotation = prevRot);
}
    return null;
  }
  hardDrop() {
    if ((this.currentPiece == null)) {
  return;
}
    let dropDist = 0;
    while ((!this.collides(_force(this.currentPiece), 0, 1))) {
  (_force(this.currentPiece).y += 1);
  (dropDist += 1);
}
    (this.score += (dropDist * 2));
    return this.lockPiece();
  }
  hold() {
    if (((this.currentPiece == null) || (!this.canHold))) {
  return;
}
    const currentType = _force(this.currentPiece).type;
    if ((this.holdPieceType == null)) {
  (this.holdPieceType = currentType);
  this.spawnPiece();
} else {
  const holdType = _force(this.holdPieceType);
  (this.holdPieceType = currentType);
  (this.currentPiece = new Piece(holdType));
}
    return (this.canHold = false);
  }
  lockPiece() {
    if ((this.currentPiece == null)) {
  return;
}
    const p = _force(this.currentPiece);
    const matrix = p.getMatrix();
    let r = 0;
    while ((r < matrix.length)) {
  let c = 0;
  while ((c < matrix[r].length)) {
  if ((matrix[r][c] != 0)) {
  const boardY = (p.y + r);
  const boardX = (p.x + c);
  if (((((boardY >= 0) && (boardY < ROWS)) && (boardX >= 0)) && (boardX < COLS))) {
  (this.board[boardY][boardX] = p.type);
}
}
  (c += 1);
}
  (r += 1);
}
    this.clearLines();
    return this.spawnPiece();
  }
  clearLines() {
    let linesCleared = 0;
    let y = (ROWS - 1);
    while ((y >= 0)) {
  let isFull = true;
  let x = 0;
  while ((x < COLS)) {
  if ((this.board[y][x] == (-1))) {
  (isFull = false);
  break;
}
  (x += 1);
}
  if (isFull) {
  this.board.splice(y, 1);
  let newRow = [];
  let c = 0;
  while ((c < COLS)) {
  newRow.push((-1));
  (c += 1);
}
  this.board.unshift(newRow);
  (linesCleared += 1);
  (y += 1);
}
  (y -= 1);
}
    if ((linesCleared > 0)) {
  (this.lines += linesCleared);
  const points = [0, 100, 300, 500, 800];
  (this.score += (points[linesCleared] * this.level));
  (this.level = (Math.floor((this.lines / 10)) + 1));
  (this.dropInterval = Math.max(100, (1000 - ((this.level - 1) * 100))));
}
    return null;
  }
  collides(p, dx, dy) {
    const matrix = p.getMatrix();
    let r = 0;
    while ((r < matrix.length)) {
  let c = 0;
  while ((c < matrix[r].length)) {
  if ((matrix[r][c] != 0)) {
  const boardY = ((p.y + r) + dy);
  const boardX = ((p.x + c) + dx);
  if ((((boardX < 0) || (boardX >= COLS)) || (boardY >= ROWS))) {
  return true;
}
  if (((boardY >= 0) && (this.board[boardY][boardX] != (-1)))) {
  return true;
}
}
  (c += 1);
}
  (r += 1);
}
    return false;
  }
  update(time) {
    if (this.isGameOver) {
  return;
}
    const deltaTime = (time - this.lastTime);
    (this.lastTime = time);
    if ((this.currentPiece != null)) {
  (this.dropCounter += deltaTime);
  if ((this.dropCounter > this.dropInterval)) {
  if ((!this.movePiece(0, 1))) {
  if ((this.lockDelayStart == 0)) {
  (this.lockDelayStart = time);
}
  if (((time - this.lockDelayStart) > 500)) {
  this.lockPiece();
  (this.lockDelayStart = 0);
  (this.dropCounter = 0);
}
} else {
  (this.lockDelayStart = 0);
  (this.dropCounter = 0);
}
}
}
    this.draw();
    return window.requestAnimationFrame(((t) => {
return this.update(t);
}));
  }
  drawBlock(ctx, x, y, color, alpha) {
    (ctx.globalAlpha = (alpha / 100));
    (ctx.fillStyle = color);
    ctx.fillRect((x * BLOCK_SIZE), (y * BLOCK_SIZE), (BLOCK_SIZE - 1), (BLOCK_SIZE - 1));
    return (ctx.globalAlpha = 1);
  }
  draw() {
    (this.boardCtx.fillStyle = "#000");
    this.boardCtx.fillRect(0, 0, (COLS * BLOCK_SIZE), (ROWS * BLOCK_SIZE));
    (this.boardCtx.strokeStyle = "#222");
    let i = 0;
    while ((i <= COLS)) {
  this.boardCtx.beginPath();
  this.boardCtx.moveTo((i * BLOCK_SIZE), 0);
  this.boardCtx.lineTo((i * BLOCK_SIZE), (ROWS * BLOCK_SIZE));
  this.boardCtx.stroke();
  (i += 1);
}
    (i = 0);
    while ((i <= ROWS)) {
  this.boardCtx.beginPath();
  this.boardCtx.moveTo(0, (i * BLOCK_SIZE));
  this.boardCtx.lineTo((COLS * BLOCK_SIZE), (i * BLOCK_SIZE));
  this.boardCtx.stroke();
  (i += 1);
}
    let r = 0;
    let c = 0;
    while ((r < ROWS)) {
  (c = 0);
  while ((c < COLS)) {
  if ((this.board[r][c] != (-1))) {
  this.drawBlock(this.boardCtx, c, r, COLORS[this.board[r][c]], 100);
}
  (c += 1);
}
  (r += 1);
}
    if ((this.currentPiece != null)) {
  const p = _force(this.currentPiece);
  const pieceMatrix = p.getMatrix();
  const pieceColor = COLORS[p.type];
  let ghostY = p.y;
  while ((!this.collides(p, 0, ((ghostY - p.y) + 1)))) {
  (ghostY += 1);
}
  (r = 0);
  while ((r < pieceMatrix.length)) {
  (c = 0);
  while ((c < pieceMatrix[r].length)) {
  if ((pieceMatrix[r][c] != 0)) {
  if (((ghostY + r) != (p.y + r))) {
  this.drawBlock(this.boardCtx, (p.x + c), (ghostY + r), pieceColor, 20);
}
  this.drawBlock(this.boardCtx, (p.x + c), (p.y + r), pieceColor, 100);
}
  (c += 1);
}
  (r += 1);
}
}
    this.holdCtx.clearRect(0, 0, 120, 120);
    if ((this.holdPieceType != null)) {
  const holdType = _force(this.holdPieceType);
  const holdMatrix = SHAPES[holdType][0];
  const holdOffsetX = ((4 - holdMatrix[0].length) / 2);
  const holdOffsetY = ((4 - holdMatrix.length) / 2);
  (r = 0);
  while ((r < holdMatrix.length)) {
  (c = 0);
  while ((c < holdMatrix[r].length)) {
  if ((holdMatrix[r][c] != 0)) {
  (this.holdCtx.fillStyle = COLORS[holdType]);
  this.holdCtx.fillRect(((c + holdOffsetX) * 30), ((r + holdOffsetY) * 30), 29, 29);
}
  (c += 1);
}
  (r += 1);
}
}
    this.nextCtx.clearRect(0, 0, 120, 360);
    let q = 0;
    while ((q < this.nextQueue.length)) {
  const nextType = this.nextQueue[q];
  const nextMatrix = SHAPES[nextType][0];
  const nextOffsetX = ((4 - nextMatrix[0].length) / 2);
  const nextOffsetY = ((q * 4) + ((4 - nextMatrix.length) / 2));
  (r = 0);
  while ((r < nextMatrix.length)) {
  (c = 0);
  while ((c < nextMatrix[r].length)) {
  if ((nextMatrix[r][c] != 0)) {
  (this.nextCtx.fillStyle = COLORS[nextType]);
  this.nextCtx.fillRect(((c + nextOffsetX) * 30), ((r + nextOffsetY) * 30), 29, 29);
}
  (c += 1);
}
  (r += 1);
}
  (q += 1);
}
    (this.scoreEl.innerText = String(this.score));
    (this.linesEl.innerText = String(this.lines));
    return (this.levelEl.innerText = String(this.level));
  }
}
const game = new Game();
game.start();
})();

//# sourceURL=examples/tetris/src//main.sws

//# sourceMappingURL=main.js.map
