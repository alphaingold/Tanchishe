/*
 * ESP32 贪吃蛇 —— 0.96" SSD1306 OLED + 摇杆
 *
 * 接线（ESP32 DevKit v1）:
 *   OLED  : VCC -> 3V3 | GND -> GND | SDA -> GPIO21 | SCL -> GPIO22
 *   摇杆  : VCC -> 3V3 | GND -> GND | VRx -> GPIO34 | VRy -> GPIO35 | SW -> GPIO27
 *
 * 依赖库（Arduino IDE -> 库管理器）:
 *   Adafruit SSD1306        by Adafruit
 *   Adafruit GFX Library    by Adafruit
 *   Adafruit BusIO          （安装上面两个时会自动提示）
 *
 * 玩法:
 *   摇杆控制方向，按下摇杆 = 暂停/继续；游戏结束后按一下重新开始。
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- 引脚 ----------
const int PIN_JOY_X  = 34;   // 只读引脚，无需上拉
const int PIN_JOY_Y  = 35;
const int PIN_JOY_SW = 27;

// ---------- 屏幕布局 ----------
const int SCREEN_W = 128;
const int SCREEN_H = 64;
const int CELL     = 4;                        // 每格 4x4 像素
const int TOP      = 10;                       // 顶部留 10px 给分数
const int COLS     = SCREEN_W / CELL;          // 32 列
const int ROWS     = (SCREEN_H - TOP) / CELL;  // 13 行
const int MAX_LEN  = COLS * ROWS;

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ---------- 游戏状态 ----------
struct Point {
  int8_t x;
  int8_t y;
};

Point snake[MAX_LEN];
int snakeLen;
Point food;
int8_t dirX, dirY;          // 当前方向
int8_t nextDirX, nextDirY;  // 下一步方向
int score;
unsigned long lastStep;
unsigned long stepMs;
bool gameOver;
bool paused;

bool onSnake(int x, int y) {
  for (int i = 0; i < snakeLen; i++) {
    if (snake[i].x == x && snake[i].y == y) return true;
  }
  return false;
}

void spawnFood() {
  if (snakeLen >= MAX_LEN) {  // 蛇占满全屏，算通关
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

  score   = 0;
  stepMs  = 180;
  paused  = false;
  gameOver = false;

  spawnFood();
  lastStep = millis();
}

void step() {
  dirX = nextDirX;
  dirY = nextDirY;

  int nx = snake[0].x + dirX;
  int ny = snake[0].y + dirY;

  // 撞墙
  if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
    gameOver = true;
    return;
  }

  // 撞自己（尾巴这一格即将移开，不算）
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
    stepMs = max(70UL, 180UL - (unsigned long)score * 4);  // 越快越难
    spawnFood();
  }
}

void readJoystick() {
  if (gameOver || paused) return;

  int vx = analogRead(PIN_JOY_X);
  int vy = analogRead(PIN_JOY_Y);
  int dx = vx - 2048;
  int dy = vy - 2048;
  int ax = abs(dx);
  int ay = abs(dy);
  const int TH = 1200;

  int8_t ndx = 0, ndy = 0;
  if (ax > ay && ax > TH) {
    ndx = (dx > 0) ? 1 : -1;
  } else if (ay > TH) {
    ndy = (dy > 0) ? 1 : -1;
  } else {
    return;
  }

  // 不能 180 度掉头
  if (ndx == -dirX && ndy == -dirY) return;

  nextDirX = ndx;
  nextDirY = ndy;
}

void handleButton() {
  static bool lastPressed = false;
  static unsigned long lastChange = 0;

  bool pressed = (digitalRead(PIN_JOY_SW) == LOW);
  if (pressed != lastPressed && millis() - lastChange > 30) {
    lastChange = millis();
    lastPressed = pressed;

    if (pressed) {
      if (gameOver) {
        resetGame();
      } else {
        paused = !paused;
        lastStep = millis();  // 避免恢复的瞬间就走一步
      }
    }
  }
}

void drawOverlay(const char *title, const char *hint);

void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // 顶部：分数
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("SCORE ");
  display.print(score);
  if (paused && !gameOver) {
    display.setCursor(86, 0);
    display.print("PAUSED");
  }
  display.drawFastHLine(0, TOP - 1, SCREEN_W, SSD1306_WHITE);

  // 蛇身
  for (int i = 1; i < snakeLen; i++) {
    display.fillRect(snake[i].x * CELL + 1, TOP + snake[i].y * CELL + 1,
                     CELL - 2, CELL - 2, SSD1306_WHITE);
  }
  // 蛇头（画成整格，便于分辨）
  display.fillRect(snake[0].x * CELL, TOP + snake[0].y * CELL, CELL, CELL, SSD1306_WHITE);

  // 食物
  display.fillRect(food.x * CELL, TOP + food.y * CELL, CELL, CELL, SSD1306_WHITE);

  // 覆盖层
  if (gameOver) {
    drawOverlay("GAME OVER", "PRESS TO RESTART");
  } else if (paused) {
    drawOverlay("PAUSED", "PRESS TO RESUME");
  }

  display.display();
}

void drawOverlay(const char *title, const char *hint) {
  display.fillRect(0, 18, SCREEN_W, 30, SSD1306_BLACK);
  display.drawRect(0, 18, SCREEN_W, 30, SSD1306_WHITE);

  display.setTextSize(2);
  int tw = strlen(title) * 12;
  display.setCursor((SCREEN_W - tw) / 2, 22);
  display.print(title);

  display.setTextSize(1);
  int hw = strlen(hint) * 6;
  display.setCursor((SCREEN_W - hw) / 2, 39);
  display.print(hint);
}

void setup() {
  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  analogReadResolution(12);

  Wire.begin(21, 22);  // ESP32 默认 I2C 引脚
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextWrap(false);

  randomSeed(micros());
  resetGame();
}

void loop() {
  handleButton();
  readJoystick();

  if (!gameOver && !paused && millis() - lastStep >= stepMs) {
    lastStep = millis();
    step();
  }

  draw();
}
