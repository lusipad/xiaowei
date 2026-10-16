// WioConsole — 桌面 AI 控制台固件
// 板子 = 脸和按钮(菜单/状态屏/蜂鸣器反馈);PC 端脚本 = 大脑(host/wio_console.py)
//
// 串口协议(115200,行分隔):
//   设备→PC: HELLO <ver> / PING / ACT <id>
//   PC→设备: PONG / BUSY / TEXT|<UTF-8行> / DONE|<ASCII备注> / FAIL|<ASCII原因>
// v1 屏幕只渲染 ASCII;中文结果在 PC 剪贴板里,后续换 SD 字体可升级中文上屏。

#include <Arduino.h>
#include <Seeed_GFX.h>

// 直连 DeepSeek 需要在本目录建 wifi_secrets.h(从 wifi_secrets.h.example 复制)。
// 没有该文件时固件照常工作,仅 "DeepSeek WiFi" 动作会提示缺少配置。
#if __has_include("wifi_secrets.h")
#define WIO_HAS_WIFI 1
#include <rpcWiFi.h>
#include <rpcWiFiClientSecure.h>
#include <ArduinoJson.h>
#include "wifi_secrets.h"
#else
#define WIO_HAS_WIFI 0
#endif

Seeed_GFX display(Seeed_Product::Wio_Terminal);

#include "cjk.h"  // 中文渲染(需要 display 与 Seeed_GFX 的颜色宏,须在两者之后)

// ---------- 输入 ----------
const int PIN_UP    = WIO_5S_UP;
const int PIN_DOWN  = WIO_5S_DOWN;
const int PIN_PRESS = WIO_5S_PRESS;
const int PIN_KEY_A = WIO_KEY_A;
const int PIN_KEY_B = WIO_KEY_B;
const int PIN_KEY_C = WIO_KEY_C;

// ---------- 动作表(id 必须与 PC 端一致) ----------
struct Action {
  const char* id;
  const char* label;
};
const Action ACTIONS[] = {
  {"translate", "1 Translate  [clipboard]"},
  {"summarize", "2 Summarize  [clipboard]"},
  {"commit",    "3 Commit Msg [clipboard]"},
  {"inspire",   "4 Inspire"},
  {"hello",     "5 Hello Test"},
  {"voice",     "6 Voice Ask (Baidu)"},
  {"deepseek",  "7 DeepSeek Cloud"},
};
const int ACTION_COUNT = sizeof(ACTIONS) / sizeof(ACTIONS[0]);

int actionIndexOf(const char* id) {
  for (int i = 0; i < ACTION_COUNT; i++) {
    if (strcmp(ACTIONS[i].id, id) == 0) return i;
  }
  return 0;
}

// ---------- 状态 ----------
enum Screen { SCR_MENU, SCR_WAIT, SCR_RESULT };
Screen screen = SCR_MENU;
int  selected = 0;
bool linkOk = false;
bool actRunning = false;
bool selfTestSent = false;
unsigned long actStartMs = 0;
unsigned long lastPingMs = 0;
unsigned long lastSpinnerMs = 0;
int  spinnerPhase = 0;
const unsigned long ACT_TIMEOUT_MS = 120000;

// ---------- 结果缓存(UTF-8 原文,绘制时由 cjk.h 折行) ----------
const int MAX_LINES = 12;
const int LINE_LEN  = 96;
char resultLines[MAX_LINES][LINE_LEN + 1];
int  resultCount = 0;
int  resultTranscriptLines = 0;  // 前若干行是语音识别原文(灰色显示)
char resultNote[56] = "";
char resultAction[16] = "";
bool resultFailed = false;

// ---------- 串口行缓冲 ----------
char rxBuf[256];
int  rxLen = 0;


// ============================================================
// 绘制
// ============================================================

void drawStatusBar() {
  display.fillRect(0, 222, 320, 18, TFT_BLACK);
  display.drawFastHLine(0, 221, 320, TFT_DARKGREY);
  display.setTextSize(1);
  display.setTextColor(linkOk ? TFT_GREEN : TFT_DARKGREY);
  display.drawString(linkOk ? "PC: linked" : "PC: --", 6, 225);
  display.setTextColor(TFT_DARKGREY);
  display.drawString("WIO CONSOLE", 240, 225);
}

void drawTitle(const char* text, uint16_t color) {
  display.fillRect(0, 0, 320, 24, TFT_BLACK);
  display.drawFastHLine(0, 24, 320, TFT_DARKGREY);
  display.setTextSize(2);
  display.setTextColor(color);
  display.drawString(text, 6, 4);
}

void drawMenu() {
  display.fillScreen(TFT_BLACK);
  drawTitle("WIO CONSOLE", TFT_CYAN);
  display.setTextSize(2);
  for (int i = 0; i < ACTION_COUNT; i++) {
    bool sel = (i == selected);
    display.setTextColor(sel ? TFT_RED : TFT_WHITE);
    char row[32];
    snprintf(row, sizeof(row), "%s%s", sel ? "> " : "  ", ACTIONS[i].label);
    display.drawString(row, 16, 30 + i * 24);
  }
  display.setTextSize(1);
  display.setTextColor(TFT_DARKGREY);
  display.drawString("Joystick: select+run   A: run   B: link test", 16, 200);
  drawStatusBar();
}

void drawWait() {
  display.fillScreen(TFT_BLACK);
  drawTitle("RUNNING", TFT_YELLOW);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE);
  display.drawString(ACTIONS[selected].label, 20, 80);
  display.setTextSize(1);
  display.setTextColor(TFT_DARKGREY);
  display.drawString("processing ...", 20, 110);
  display.setTextColor(TFT_DARKGREY);
  display.drawString("C: hide", 16, 200);
  drawStatusBar();
}

void drawSpinner() {
  display.fillRect(250, 100, 60, 24, TFT_BLACK);
  display.setTextSize(2);
  display.setTextColor(TFT_YELLOW);
  char dots[8];
  int n = spinnerPhase % 4;
  for (int i = 0; i < n; i++) dots[i] = '.';
  dots[n] = 0;
  display.drawString(dots, 250, 100);
}

void drawResult() {
  display.fillScreen(TFT_BLACK);
  if (resultFailed) {
    drawTitle("FAIL", TFT_RED);
  } else {
    drawTitle("DONE", TFT_GREEN);
  }
  int y = 30;
  bool separatorDrawn = false;
  for (int i = 0; i < resultCount && y < 190; i++) {
    if (resultLines[i][0] == 0) continue;
    const bool isReply = (i >= resultTranscriptLines);
    if (isReply && !separatorDrawn && resultTranscriptLines > 0) {
      y += 3;
      display.drawFastHLine(0, y, 320, TFT_DARKGREEN);
      y += 7;
      separatorDrawn = true;
    }
    const uint16_t color = isReply ? TFT_YELLOW : TFT_LIGHTGREY;
    y = drawTextCJK(resultLines[i], 8, y, 312, color);
  }
  if (y == 30) {
    display.setTextSize(1);
    display.setTextColor(TFT_DARKGREY);
    display.drawString("(empty)", 8, 32);
  }
  display.drawFastHLine(0, 196, 320, TFT_DARKGREY);
  display.setTextSize(1);
  display.setTextColor(resultFailed ? TFT_RED : TFT_LIGHTGREY);
  display.drawString(resultNote, 8, 200);
  display.setTextColor(TFT_DARKGREY);
  display.drawString("any key: menu", 8, 210);
  drawStatusBar();
}

// ============================================================
// 行为
// ============================================================

void beep(int freq, int dur);
void finishAction(bool failed, const char* note);
#if WIO_HAS_WIFI
void runDeepSeek();
void runVoiceAsk();
#endif

void beep(int freq, int dur) {
  tone(WIO_BUZZER, freq, dur);
}

// 存储原始 UTF-8 文本(按 \n 分行,超长按 UTF-8 边界截断);绘制交给 drawTextCJK
void storeText(const char* src) {
  const char* p = src;
  while (*p && resultCount < MAX_LINES) {
    char* dst = resultLines[resultCount];
    int c = 0;
    while (*p && *p != '\n' && c < LINE_LEN) dst[c++] = *p++;
    if (*p && *p != '\n') {  // 行超长截断,不切断多字节字符
      while (c > 0 && ((unsigned char)dst[c - 1] & 0xC0) == 0x80) c--;
      if (c > 0 && ((unsigned char)dst[c - 1] & 0x80)) c--;
      while (*p && *p != '\n') p++;
    }
    dst[c] = 0;
    resultCount++;
    if (*p == '\n') p++;
  }
}

void startAction(int idx) {
  selected = idx;
  resultCount = 0;
  resultTranscriptLines = 0;
  resultNote[0] = 0;
  strncpy(resultAction, ACTIONS[idx].id, sizeof(resultAction) - 1);
  actRunning = true;
  actStartMs = millis();
  screen = SCR_WAIT;
  beep(880, 50);
  drawWait();

  if (strcmp(ACTIONS[idx].id, "deepseek") == 0) {
#if WIO_HAS_WIFI
    runDeepSeek();  // 本地执行,不走 PC
#else
    finishAction(true, "missing wifi_secrets.h - see README");
#endif
    return;
  }

  if (strcmp(ACTIONS[idx].id, "voice") == 0) {
#if WIO_HAS_WIFI
    runVoiceAsk();  // 录音 -> 百度 ASR -> DeepSeek LLM -> 屏幕
#else
    finishAction(true, "missing wifi_secrets.h - see README");
#endif
    return;
  }

  Serial.print("ACT ");
  Serial.println(ACTIONS[idx].id);
}

void finishAction(bool failed, const char* note) {
  actRunning = false;
  resultFailed = failed;
  strncpy(resultNote, note, sizeof(resultNote) - 1);
  screen = SCR_RESULT;
  if (failed) beep(196, 250);
  else beep(1319, 80);
  drawResult();
}

#if WIO_HAS_WIFI
// ---- 直连 DeepSeek(不经过 PC)----
// 注意:未配置 CA 证书,使用的是跳过证书校验的 TLS(玩具用途可接受)。

bool wifiEnsureConnected() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("V: scanning networks...");
  const int n = WiFi.scanNetworks();
  for (int i = 0; i < n && i < 12; i++) {
    Serial.print("V: net \"");
    Serial.print(WiFi.SSID(i));
    Serial.print("\" ch=");
    Serial.print(WiFi.channel(i));
    if (WiFi.channel(i) <= 14) Serial.print(" (2.4G)"); else Serial.print(" (5G)");
    Serial.print(" rssi=");
    Serial.println(WiFi.RSSI(i));
  }
  Serial.print("V: scan done, ");
  Serial.print(n);
  Serial.println(" networks");

  Serial.print("V: wifi begin ssid=\"");
  Serial.print(WIFI_SSID);
  Serial.println("\"");

  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.print("V: wifi attempt ");
    Serial.println(attempt);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - start > 15000) break;
      delay(500);
      Serial.print("V: wifi status=");
      Serial.print(WiFi.status());
      Serial.print(" t=");
      Serial.println(millis() - start);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("V: wifi OK ip=");
      Serial.println(WiFi.localIP());
      return true;
    }
    Serial.println("V: wifi attempt failed, retrying...");
    WiFi.disconnect();
    delay(2000);
  }
  Serial.println("V: wifi TIMEOUT (3 attempts)");
  return false;
}

#include "voice.h"  // 语音链路:录音/ASR/LLM(放在这里确保 readHttpBody 等可被复用)

void runDeepSeek() {
  if (!wifiEnsureConnected()) {
    finishAction(true, "wifi connect failed");
    return;
  }

  const char* prompt =
      "Reply with exactly two lines and nothing else. "
      "Line 1: one witty one-liner for programmers, simple English, max 15 words. "
      "Line 2: its Simplified Chinese translation.";

  JsonDocument req;
  req["model"] = "deepseek-chat";
  req["messages"][0]["role"] = "user";
  req["messages"][0]["content"] = prompt;
  req["max_tokens"] = 120;
  req["temperature"] = 1.0;
  String body;
  serializeJson(req, body);

  WiFiClientSecure client;
  if (!client.connect("api.deepseek.com", 443, 15000)) {
    finishAction(true, "https connect failed");
    return;
  }

  String request = "POST /v1/chat/completions HTTP/1.1\r\n"
                   "Host: api.deepseek.com\r\n"
                   "Authorization: Bearer " DEEPSEEK_KEY "\r\n"
                   "Content-Type: application/json\r\n"
                   "Connection: close\r\n"
                   "Content-Length: ";
  request += String(body.length());
  request += "\r\n\r\n";
  request += body;
  client.print(request);

  String httpBody;
  if (!readHttpBody(client, httpBody, 30000, "deepseek")) {
    finishAction(true, "http error - check key/ssid");
    return;
  }

  JsonDocument resp;
  if (deserializeJson(resp, httpBody)) {
    finishAction(true, "bad json response");
    return;
  }
  const char* content = resp["choices"][0]["message"]["content"] | "";
  if (!content[0]) {
    finishAction(true, "empty reply");
    return;
  }
  storeText(content);
  finishAction(false, "direct deepseek ok");
}
#endif

void handleHostLine(const char* line) {
  if (strcmp(line, "PONG") == 0) {
    bool was = linkOk;
    linkOk = true;
    if (!was) drawStatusBar();
    if (!selfTestSent) {
      selfTestSent = true;  // 首次连上 PC,自动跑一次 Hello 自检
      startAction(actionIndexOf("hello"));
    }
    return;
  }
  if (actRunning) {
    if (strncmp(line, "TEXT|", 5) == 0) {
      storeText(line + 5);
    } else if (strncmp(line, "DONE|", 5) == 0) {
      finishAction(false, line + 5);
    } else if (strncmp(line, "FAIL|", 5) == 0) {
      finishAction(true, line + 5);
    }
    // BUSY:无需处理
  }
}

// ============================================================
// 输入
// ============================================================

unsigned long lastKeyMs = 0;
const unsigned long KEY_DEBOUNCE_MS = 180;

bool pressed(int pin) {
  if (digitalRead(pin) != LOW) return false;
  if (millis() - lastKeyMs < KEY_DEBOUNCE_MS) return false;
  lastKeyMs = millis();
  return true;
}

void pollInput() {
  if (pressed(PIN_UP) && screen == SCR_MENU) {
    selected = (selected + ACTION_COUNT - 1) % ACTION_COUNT;
    drawMenu();
  }
  if (pressed(PIN_DOWN) && screen == SCR_MENU) {
    selected = (selected + 1) % ACTION_COUNT;
    drawMenu();
  }
  if (pressed(PIN_PRESS) || pressed(PIN_KEY_A)) {
    if (screen == SCR_MENU) startAction(selected);
    else if (screen == SCR_RESULT) { screen = SCR_MENU; drawMenu(); }
  }
  if (pressed(PIN_KEY_B)) {
    if (screen == SCR_MENU) {
      Serial.println("PING");
      lastPingMs = millis();
    } else if (screen == SCR_RESULT) { screen = SCR_MENU; drawMenu(); }
  }
  if (pressed(PIN_KEY_C)) {
    if (screen == SCR_WAIT) { screen = SCR_MENU; drawMenu(); }
    else if (screen == SCR_RESULT) { screen = SCR_MENU; drawMenu(); }
  }
}

// ============================================================
// 串口
// ============================================================

void pollSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n') {
      rxBuf[rxLen] = 0;
      if (rxLen > 0) handleHostLine(rxBuf);
      rxLen = 0;
    } else if (ch != '\r' && rxLen < (int)sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = ch;
    }
  }
}

// ============================================================
// 主程序
// ============================================================

void setup() {
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_PRESS, INPUT_PULLUP);
  pinMode(PIN_KEY_A, INPUT_PULLUP);
  pinMode(PIN_KEY_B, INPUT_PULLUP);
  pinMode(PIN_KEY_C, INPUT_PULLUP);

  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2500) {
  }

  if (!display.begin()) {
    Serial.print("ERR display: ");
    Serial.println(display.lastResult().message);
    while (true) delay(1000);
  }

  drawMenu();
  Serial.println("HELLO wio-console 1.0");
  lastPingMs = millis();
}

void loop() {
  pollSerial();
  pollInput();

  if (!actRunning && millis() - lastPingMs > 5000) {
    Serial.println("PING");
    lastPingMs = millis();
  }

  if (actRunning) {
    if (screen == SCR_WAIT && millis() - lastSpinnerMs > 350) {
      spinnerPhase++;
      drawSpinner();
      lastSpinnerMs = millis();
    }
    if (millis() - actStartMs > ACT_TIMEOUT_MS) {
      finishAction(true, "timeout - PC busy/offline?");
    }
  }
}
