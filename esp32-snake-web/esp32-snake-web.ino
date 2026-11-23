/*
 * ESP32 网页贪吃蛇（零外设版）
 *
 * 硬件：只需要一块 ESP32（DevKit 即可），USB 供电。
 * 屏幕和手柄：手机或电脑的浏览器。
 *
 * 用法：
 *   1. 上传本程序，打开串口监视器（115200）看到 "Ready"。
 *   2. 手机/电脑连接 WiFi：ESP32-Snake / 密码 snake1234
 *   3. 浏览器打开 http://192.168.4.1
 *   4. 方向键 / 滑动屏幕 / 屏幕方向键控制，空格暂停。
 *
 * 依赖库（库管理器搜索安装）：
 *   WebSockets  by Markus Sattler
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

const char *AP_SSID = "ESP32-Snake";
const char *AP_PASS = "snake1234";

WebServer server(80);
WebSocketsServer ws(81);

// ---------- 游戏参数 ----------
const int COLS    = 24;
const int ROWS    = 18;
const int MAX_LEN = COLS * ROWS;

struct Point {
  int8_t x;
  int8_t y;
};

Point snake[MAX_LEN];
int snakeLen = 0;
Point food = {0, 0};
int8_t dirX = 1, dirY = 0;
int8_t nextDirX = 1, nextDirY = 0;
int score = 0;
bool gameOver = false;
bool paused = false;
unsigned long stepMs = 180;
unsigned long lastStep = 0;

// 网页（保存在 Flash 里，不占内存）
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>ESP32 贪吃蛇</title>
<style>
  :root {
    color-scheme: dark;
    --bg: #08080c;
    --panel: #14141c;
    --line: rgba(255,255,255,.08);
    --text: #ECECF1;
    --muted: #8B8B98;
    --brand: #6054F1;
    --head: #CFD8FF;
    --food: #F87454;
  }
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
  html, body { margin: 0; height: 100%; background: var(--bg); }
  body {
    color: var(--text);
    font-family: system-ui, -apple-system, "PingFang SC", "Segoe UI", sans-serif;
    display: grid; place-items: center;
    padding: 16px; overscroll-behavior: none;
  }
  .app { width: min(92vw, 640px); display: flex; flex-direction: column; gap: 12px; }
  .hud { display: flex; align-items: baseline; justify-content: space-between; }
  h1 { margin: 0; font-size: 18px; font-weight: 600; letter-spacing: .02em; }
  .score { color: var(--muted); font-size: 14px; }
  #score { color: var(--text); font-weight: 600; font-variant-numeric: tabular-nums; }
  .stage {
    position: relative; aspect-ratio: 4 / 3;
    border-radius: 16px; overflow: hidden;
    background: #0E0E14;
    box-shadow: 0 0 0 1px var(--line);
  }
  canvas { display: block; width: 100%; height: 100%; touch-action: none; }
  .overlay {
    position: absolute; inset: 0;
    display: flex; flex-direction: column; align-items: center; justify-content: center;
    gap: 8px; background: rgba(8,8,12,.74);
    text-align: center; padding: 16px;
  }
  .overlay.hidden { display: none; }
  .ov-title { font-size: 22px; font-weight: 600; }
  .ov-sub { color: var(--muted); font-size: 14px; }
  #ovBtn {
    margin-top: 8px; padding: 10px 22px; border: 0; border-radius: 999px;
    background: var(--brand); color: #fff; font-size: 15px; font-weight: 600;
  }
  .pad {
    display: none;
    grid-template-columns: repeat(3, 1fr);
    grid-template-rows: repeat(3, 52px);
    gap: 8px; width: 100%; max-width: 280px; margin: 0 auto;
  }
  @media (hover: none), (max-width: 720px) { .pad { display: grid; } }
  .pad button {
    border: 1px solid var(--line); border-radius: 12px;
    background: var(--panel); color: var(--text);
    font-size: 18px; touch-action: none;
  }
  .pad button:active { background: #1d1d28; }
  .b-up { grid-column: 2; grid-row: 1; }
  .b-left { grid-column: 1; grid-row: 2; }
  .b-mid { grid-column: 2; grid-row: 2; }
  .b-right { grid-column: 3; grid-row: 2; }
  .b-down { grid-column: 2; grid-row: 3; }
  .hint { margin: 0; text-align: center; color: var(--muted); font-size: 12px; }
</style>
</head>
<body>
<main class="app">
  <header class="hud">
    <h1>ESP32 贪吃蛇</h1>
    <div class="score">得分 <span id="score">0</span></div>
  </header>

  <div class="stage">
    <canvas id="game" width="480" height="360"></canvas>
    <div class="overlay hidden" id="overlay">
      <div class="ov-title" id="ovTitle">连接中…</div>
      <div class="ov-sub" id="ovSub">正在连接 ESP32</div>
      <button id="ovBtn" hidden>重新开始</button>
    </div>
  </div>

  <div class="pad" id="pad">
    <button class="b-up" data-cmd="up">▲</button>
    <button class="b-left" data-cmd="left">◀</button>
    <button class="b-mid" data-cmd="pause">Ⅱ</button>
    <button class="b-right" data-cmd="right">▶</button>
    <button class="b-down" data-cmd="down">▼</button>
  </div>

  <p class="hint">方向键 / 滑动屏幕控制 · 空格暂停</p>
</main>

<script>
(function () {
  var COLS = 24, ROWS = 18;
  var canvas = document.getElementById('game');
  var ctx = canvas.getContext('2d');
  var CW = canvas.width / COLS;
  var CH = canvas.height / ROWS;
  var scoreEl = document.getElementById('score');
  var overlay = document.getElementById('overlay');
  var ovTitle = document.getElementById('ovTitle');
  var ovSub = document.getElementById('ovSub');
  var ovBtn = document.getElementById('ovBtn');
  var state = null;
  var ws = null;
  var ovCmd = null;

  function send(cmd) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(cmd);
  }

  function roundRect(x, y, w, h, r) {
    ctx.beginPath();
    if (ctx.roundRect) {
      ctx.roundRect(x, y, w, h, r);
    } else {
      ctx.rect(x, y, w, h);
    }
  }

  function drawGrid() {
    ctx.strokeStyle = 'rgba(255,255,255,.035)';
    ctx.lineWidth = 1;
    for (var c = 1; c < COLS; c++) {
      ctx.beginPath(); ctx.moveTo(c * CW, 0); ctx.lineTo(c * CW, canvas.height); ctx.stroke();
    }
    for (var r = 1; r < ROWS; r++) {
      ctx.beginPath(); ctx.moveTo(0, r * CH); ctx.lineTo(canvas.width, r * CH); ctx.stroke();
    }
  }

  function draw() {
    ctx.fillStyle = '#0E0E14';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    drawGrid();
    if (!state) return;

    // 食物
    ctx.fillStyle = '#F87454';
    ctx.beginPath();
    ctx.arc(state.food[0] * CW + CW / 2, state.food[1] * CH + CH / 2, Math.min(CW, CH) * 0.3, 0, Math.PI * 2);
    ctx.fill();

    // 蛇
    for (var i = 0; i < state.snake.length; i++) {
      var p = state.snake[i];
      ctx.fillStyle = i === 0 ? '#CFD8FF' : '#6054F1';
      roundRect(p[0] * CW + 2, p[1] * CH + 2, CW - 4, CH - 4, 4);
      ctx.fill();
    }

    scoreEl.textContent = state.score;
  }

  function showOverlay(title, sub, label, cmd) {
    ovTitle.textContent = title;
    ovSub.textContent = sub;
    ovCmd = cmd;
    if (cmd) {
      ovBtn.textContent = label;
      ovBtn.hidden = false;
    } else {
      ovBtn.hidden = true;
    }
    overlay.classList.remove('hidden');
  }

  function updateOverlay() {
    if (!state) {
      showOverlay('连接中…', '正在连接 ESP32', null, null);
    } else if (state.over) {
      showOverlay('游戏结束', '本局得分 ' + state.score, '重新开始', 'restart');
    } else if (state.paused) {
      showOverlay('已暂停', '按空格或点击继续', '继续', 'pause');
    } else {
      overlay.classList.add('hidden');
    }
  }

  function render() {
    draw();
    updateOverlay();
  }

  function connect() {
    ws = new WebSocket('ws://' + location.hostname + ':81');
    ws.onmessage = function (ev) {
      try {
        state = JSON.parse(ev.data);
      } catch (e) {
        return;
      }
      render();
    };
    ws.onclose = function () {
      state = null;
      showOverlay('连接断开', '1 秒后自动重连…', null, null);
      draw();
      setTimeout(connect, 1000);
    };
  }

  ovBtn.addEventListener('click', function () {
    if (ovCmd) send(ovCmd);
  });

  // 屏幕方向键
  document.getElementById('pad').addEventListener('pointerdown', function (e) {
    var btn = e.target.closest('button[data-cmd]');
    if (!btn) return;
    e.preventDefault();
    send(btn.getAttribute('data-cmd'));
  });

  // 滑动
  var sx = 0, sy = 0;
  canvas.addEventListener('touchstart', function (e) {
    var t = e.touches[0]; sx = t.clientX; sy = t.clientY;
  }, { passive: true });
  canvas.addEventListener('touchend', function (e) {
    var t = e.changedTouches[0];
    var dx = t.clientX - sx, dy = t.clientY - sy;
    if (Math.abs(dx) < 24 && Math.abs(dy) < 24) return;
    if (Math.abs(dx) > Math.abs(dy)) send(dx > 0 ? 'right' : 'left');
    else send(dy > 0 ? 'down' : 'up');
  }, { passive: true });

  // 键盘
  var keyMap = {
    ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right',
    w: 'up', s: 'down', a: 'left', d: 'right'
  };
  window.addEventListener('keydown', function (e) {
    var key = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (keyMap[key]) {
      e.preventDefault();
      send(keyMap[key]);
    } else if (e.key === ' ') {
      e.preventDefault();
      send('pause');
    } else if (e.key === 'Enter') {
      send('restart');
    }
  });

  draw();
  updateOverlay();
  connect();
})();
</script>
</body>
</html>
)rawliteral";

// ---------- 游戏逻辑 ----------
bool onSnake(int x, int y) {
  for (int i = 0; i < snakeLen; i++) {
    if (snake[i].x == x && snake[i].y == y) return true;
  }
  return false;
}

void spawnFood() {
  if (snakeLen >= MAX_LEN) {  // 占满全屏
    gameOver = true;
    return;
  }
  do {
    food.x = random(COLS);
    food.y = random(ROWS);
  } while (onSnake(food.x, food.y));
}

void resetGame() {
  snakeLen = 3;
  int cx = COLS / 2;
  int cy = ROWS / 2;
  snake[0].x = cx;     snake[0].y = cy;
  snake[1].x = cx - 1; snake[1].y = cy;
  snake[2].x = cx - 2; snake[2].y = cy;

  dirX = 1; dirY = 0;
  nextDirX = 1; nextDirY = 0;

  score    = 0;
  stepMs   = 180;
  paused   = false;
  gameOver = false;

  spawnFood();
  lastStep = millis();
}

void setDir(int8_t dx, int8_t dy) {
  if (gameOver) return;
  if (dx == -dirX && dy == -dirY) return;  // 不能 180 度掉头
  nextDirX = dx;
  nextDirY = dy;
}

void step() {
  dirX = nextDirX;
  dirY = nextDirY;

  int nx = snake[0].x + dirX;
  int ny = snake[0].y + dirY;

  if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
    gameOver = true;
    return;
  }

  for (int i = 0; i < snakeLen - 1; i++) {
    if (snake[i].x == nx && snake[i].y == ny) {
      gameOver = true;
      return;
    }
  }

  bool ate = (nx == food.x && ny == food.y);
  int newLen = ate ? snakeLen + 1 : snakeLen;

  for (int i = newLen - 1; i > 0; i--) {
    snake[i] = snake[i - 1];
  }
  snake[0].x = nx;
  snake[0].y = ny;
  snakeLen = newLen;

  if (ate) {
    score++;
    stepMs = max(70UL, 180UL - (unsigned long)score * 4);
    spawnFood();
  }
}

// ---------- 网络 ----------
String stateJson() {
  String s;
  s.reserve(32 + snakeLen * 8);
  s += "{\"snake\":[";
  for (int i = 0; i < snakeLen; i++) {
    if (i) s += ',';
    s += '[';
    s += snake[i].x;
    s += ',';
    s += snake[i].y;
    s += ']';
  }
  s += "],\"food\":[";
  s += food.x;
  s += ',';
  s += food.y;
  s += "],\"score\":";
  s += score;
  s += ",\"over\":";
  s += (gameOver ? 1 : 0);
  s += ",\"paused\":";
  s += (paused ? 1 : 0);
  s += '}';
  return s;
}

void broadcastState() {
  ws.broadcastTXT(stateJson());
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd == "up") {
    setDir(0, -1);
  } else if (cmd == "down") {
    setDir(0, 1);
  } else if (cmd == "left") {
    setDir(-1, 0);
  } else if (cmd == "right") {
    setDir(1, 0);
  } else if (cmd == "pause") {
    if (!gameOver) {
      paused = !paused;
      lastStep = millis();
      broadcastState();
    }
  } else if (cmd == "restart") {
    resetGame();
    broadcastState();
  }
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  if (type == WStype_CONNECTED) {
    ws.sendTXT(num, stateJson());
  } else if (type == WStype_TEXT) {
    handleCommand(String((char *)payload, len));
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("WiFi: ");
  Serial.print(AP_SSID);
  Serial.print(" / ");
  Serial.println(AP_PASS);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());

  randomSeed(micros());
  resetGame();

  server.on("/", []() {
    server.send_P(200, "text/html; charset=utf-8", PAGE);
  });
  server.onNotFound([]() {
    server.send(404, "text/plain", "not found");
  });
  server.begin();

  ws.begin();
  ws.onEvent(onWsEvent);

  Serial.println("Ready. Join WiFi and open the page.");
}

void loop() {
  server.handleClient();
  ws.loop();

  if (!gameOver && !paused && millis() - lastStep >= stepMs) {
    lastStep = millis();
    step();
    broadcastState();
  }
}
