// 小维(Wio)—— AI 电子桌宠
// 开机即宠物:待机会眨眼、张望、哼歌;按一下 B 开始听,说完自动发送;
// 语音 → 百度ASR → DeepSeek → 对话气泡(汉字);摇杆左右 = 切页,下压 = 摸头。
// 硬件能力(录音/渲染/网络/传感器)在 libraries/WioKit,这里只保留应用层:
// 状态机、表情人设(pet_face/pet_anim)、系统提示词、密钥。

#include <Arduino.h>
#if defined(_MSC_VER)
#include "unistd.h"
#else
#include <unistd.h>
#endif
#include <Seeed_GFX.h>
#include "pet_skin_manager.h"

enum PetState { ST_IDLE, ST_RECORD, ST_THINK, ST_SHOW, ST_SLEEP, ST_CONFIG };
PetState state = ST_IDLE;

#include <WioKitLogic.h>   // L0 纯逻辑(HTTP 判停/chunked/情绪标签/探测器)
#include <WioKitCjk.h>     // L1 中文渲染(字库在库里,~150KB Flash)
#include <WioKitMic.h>     // L1 麦克风 DMA 录音 + VAD
#include <WioKitSense.h>   // L1 光线 + IMU(判定逻辑在 WioKitLogic)
#include <WioKitNet.h>     // L1 WiFi + HTTP + base64
#include <WioKitTiming.h>  // 各阶段计时账本("T:" 行)

#include <WioKitConfig.h>      // L1 配置持久化 + SoftAP 网页配网
#include <WioKitAsrBaidu.h>    // L2 百度 ASR
#include <WioKitLlmDeepSeek.h> // L2 DeepSeek
#if defined(ARDUINO)
#include <sfud.h>
#endif

// ---- 默认密钥/回退:若本地有 wifi_secrets.h 且未指定发布模式则用作开发调试缺省值 ----
#if __has_include("wifi_secrets.h") && !defined(PET_RELEASE_BUILD)
#define HAS_COMPILED_SECRETS 1
#include "wifi_secrets.h"
#else
#define HAS_COMPILED_SECRETS 0
#endif

#ifndef PET_TEST_OFFLINE
#define PET_TEST_OFFLINE 0   // 1 = 开机预连直接失败(测试离线开机)
#endif
#ifndef PET_TEST_BAD_TOKEN
#define PET_TEST_BAD_TOKEN 0 // 1 = 预连后把 token 改坏(测试失效自动重取)
#endif

Seeed_GFX display(Seeed_Product::Wio_Terminal);

WioConfig activeConfig;
bool hasActiveConfig = false;
static String petSysPrompt;

#include "pet_pages.h"
#include "pet_face.h"
#include "pet_anim.h"

// 桌面多功能看板状态定义
int curPage = PAGE_PET;
int lastPageBeforeVoice = PAGE_PET;
uint32_t netEpochBase = 0;
uint32_t netEpochMillis = 0;
bool hasSyncedTime = false;
uint32_t nextTimeSyncMs = 0;

uint32_t pomoRemainingSec = POMO_DURATION_SEC;
bool pomoRunning = false;
uint32_t pomoLastTickMs = 0;
int pomoCompletedTotal = 0;
bool pomoJustFinished = false;
unsigned long lastInteract = 0;  // 最近一次有人互动(按键/摸头/拿起/专注)

void startPomodoro() {
  // 若上一轮番茄钟已归零结束，再次启动时自动重置为 25 分钟满时长
  if (pomoRemainingSec == 0) {
    pomoRemainingSec = POMO_DURATION_SEC;
  }
  if (!pomoRunning) {
    pomoRunning = true;
    pomoLastTickMs = millis();
  }
  lastInteract = millis();
}

void pausePomodoro() {
  pomoRunning = false;
  lastInteract = millis();
}

void resetPomodoro() {
  pomoRunning = false;
  pomoRemainingSec = POMO_DURATION_SEC;
  pomoJustFinished = false;
  lastInteract = millis();
}

void tickPomodoroLogic() {
  if (!pomoRunning) return;
  const unsigned long now = millis();
  lastInteract = now;  // 专注进行期间持续刷新互动时间，防止 25 分钟结束瞬间因 idleFor>5min 立即误入沉睡!
  if (now - pomoLastTickMs >= 1000) {
    const uint32_t secPassed = (now - pomoLastTickMs) / 1000;
    pomoLastTickMs += secPassed * 1000;
    if (pomoRemainingSec > secPassed) {
      pomoRemainingSec -= secPassed;
    } else {
      pomoRemainingSec = 0;
      pomoRunning = false;
      pomoCompletedTotal++;
      pomoJustFinished = true;
    }
  }
}

const int PIN_KEY_A = WIO_KEY_A;
const int PIN_KEY_B = WIO_KEY_B;
const int PIN_KEY_C = WIO_KEY_C;
const int PIN_JOY_L = WIO_5S_LEFT;
const int PIN_JOY_R = WIO_5S_RIGHT;
// 全部 8 个按键输入(唤醒/提前结束显示时任意键都算)
const int ALL_KEYS[8] = {WIO_KEY_A, WIO_KEY_B, WIO_KEY_C, WIO_5S_UP,
                         WIO_5S_DOWN, WIO_5S_LEFT, WIO_5S_RIGHT, WIO_5S_PRESS};

void beep(int freq, int dur) {
  tone(WIO_BUZZER, freq, dur);
}

const uint32_t PET_TRAILING_MS = 1200;  // 说完静音多久自动停(放宽到1.2s,避免换气断句)

// ============================================================
// 按键边沿检测与注入(彻底杜绝长按连续重入导致按 B 后 0.2s 秒进"让我想想"!)
// ============================================================

static int injectedKey = -1;
static bool keyWasDown[8] = {false};
static unsigned long keyLastChangeMs[8] = {0};

static int findKeyIndex(int pin) {
  for (int i = 0; i < 8; i++) {
    if (ALL_KEYS[i] == pin) return i;
  }
  return -1;
}

void syncKeyStates() {
  const unsigned long now = millis();
  for (int i = 0; i < 8; i++) {
    keyWasDown[i] = (digitalRead(ALL_KEYS[i]) == LOW);
    keyLastChangeMs[i] = now;
  }
}

void petInjectKey(int pin) {
  injectedKey = pin;
}

bool pressed(int pin) {
  if (injectedKey == pin) {
    injectedKey = -1;
    return true;
  }
  const int idx = findKeyIndex(pin);
  const bool isDown = (digitalRead(pin) == LOW);
  const unsigned long now = millis();
  if (idx < 0) return isDown;

  // 先检测按键释放(必须先松开再按下才算新一次点击，彻底根治同一次按键跨状态重入)
  if (!isDown) {
    if (keyWasDown[idx] && (now - keyLastChangeMs[idx] >= 25)) {
      keyWasDown[idx] = false;
      keyLastChangeMs[idx] = now;
    }
    return false;
  }
  if (!keyWasDown[idx] && (now - keyLastChangeMs[idx] >= 80)) {
    keyWasDown[idx] = true;
    keyLastChangeMs[idx] = now;
    return true;
  }
  return false;
}

bool anyKeyDown() {
  for (int i = 0; i < 8; i++) {
    if (digitalRead(ALL_KEYS[i]) == LOW) return true;
  }
  return false;
}

// ============================================================
// 语音链路编排(应用层):裁剪 → 识别 → 回答 → 情绪标签
// ============================================================

unsigned long stateStart = 0;
unsigned long showDurMs = 0;
static bool thinkUserCancelled = false;
static bool thinkWatchdogTimeout = false;

const char* getPetSystemPrompt() {
  petSysPrompt = petSkinGetPrompt();
  if (hasActiveConfig && strlen(activeConfig.city) > 0) {
    petSysPrompt += " 主人常驻城市是: ";
    petSysPrompt += activeConfig.city;
    petSysPrompt += "。被问及天气时据此作答。";
  }
  return petSysPrompt.c_str();
}

void handleTestCommands();
void petYieldLoop() {
  handleTestCommands();
  petAnimTick();
}

bool petNetAbortCheck() {
  handleTestCommands();
  if (thinkUserCancelled || thinkWatchdogTimeout) return true;
  if (state == ST_THINK && (millis() - stateStart > 250)) {
    if (pressed(PIN_KEY_C)) {
      Serial.println("V: user cancelled ST_THINK via KEY_C");
      thinkUserCancelled = true;
      return true;
    }
    // 思考总时长硬看门狗(28 秒)，绝不允许永久卡死在"让我想想"
    if (millis() - stateStart > 28000) {
      Serial.println("V: ST_THINK hard watchdog timeout");
      thinkWatchdogTimeout = true;
      return true;
    }
  }
  return false;
}

void applyActiveConfig() {
  wioNetSetYield(petYieldLoop);
  wioNetSetAbort(petNetAbortCheck);
  if (hasActiveConfig) {
    petSkinSet(activeConfig.activeSkin);
  }
  if (hasActiveConfig && strlen(activeConfig.ssid) > 0) {
    wioNetBegin(activeConfig.ssid, activeConfig.pass);
    wioAsrBaiduBegin(activeConfig.baiduApiKey, activeConfig.baiduSecret);
    wioLlmDeepSeekBegin(activeConfig.deepseekKey, getPetSystemPrompt());
  }
}

// ---- 多轮对话上下文 (保存最近 2 轮对答 = 4 条消息: user, assistant, user, assistant) ----
const size_t MAX_HISTORY_MSGS = 4;
static WioLlmMsg chatHistory[MAX_HISTORY_MSGS];
static size_t chatHistoryCount = 0;
static uint32_t lastChatTime = 0;
const uint32_t CHAT_CONTEXT_EXPIRE_MS = 90000;  // 90秒不说话则上下文自动重置

void clearChatHistory() {
  chatHistoryCount = 0;
}

void addChatHistory(const String& userText, const String& assistantText) {
  if (chatHistoryCount + 2 <= MAX_HISTORY_MSGS) {
    chatHistory[chatHistoryCount++] = {"user", userText};
    chatHistory[chatHistoryCount++] = {"assistant", assistantText};
  } else {
    for (size_t i = 2; i < chatHistoryCount; i++) {
      chatHistory[i - 2] = chatHistory[i];
    }
    chatHistory[chatHistoryCount - 2] = {"user", userText};
    chatHistory[chatHistoryCount - 1] = {"assistant", assistantText};
  }
  lastChatTime = millis();
}

// 每轮结束打印 "T:" 行(格式不变,性能基线可对比)
void petPrintTiming(uint32_t processMs) {
  Serial.print("T: trail=");
  Serial.print(PET_TRAILING_MS);
  Serial.print(" net=");
  Serial.print(wioTiming().net);
  Serial.print(" upload=");
  Serial.print(wioTiming().upload);
  Serial.print(" asr=");
  Serial.print(wioTiming().asr);
  Serial.print(" llm_conn=");
  Serial.print(wioTiming().llmConn);
  Serial.print(" llm=");
  Serial.print(wioTiming().llm);
  Serial.print(" total=");
  Serial.println(PET_TRAILING_MS + processMs);
}

// 开机预连:WiFi(只试 1 次,≤10s)+ 百度 token。失败不阻止开机:离线照常做宠物,按 B 时再连
void petNetWarmup() {
  if (!hasActiveConfig || strlen(activeConfig.ssid) == 0) {
    Serial.println("V: no wifi config, skipping warmup");
    return;
  }
  wioNetResetAbort();
  const uint32_t t0 = millis();
#if PET_TEST_OFFLINE
  const bool ok = false;  // 测试:模拟开机时无网
#else
  const bool ok = wioNetConnected(1, 10000) && wioAsrBaiduWarmup();
#endif
  Serial.print("V: warmup ");
  Serial.print(ok ? "ok" : "failed");
  Serial.print(" ms=");
  Serial.println(millis() - t0);
}

// 识别 + 回答(阻塞,数秒):成功 true;transcript/reply/note 填充。
// 连网/令牌/失效重取在 wioAsrBaidu 内部;情绪标签解析在这里做(标签属于小维人设,不在库)
bool petProcessVoice(uint32_t samples, bool bufferFull, String& transcript, String& reply, int& emotion, String& note) {
  transcript = "";
  reply = "";
  note = "";
  emotion = EMO_HAPPY;
  wioTimingReset();
  wioNetResetAbort();
  thinkUserCancelled = false;
  thinkWatchdogTimeout = false;

  if (!hasActiveConfig || strlen(activeConfig.ssid) == 0) {
    note = "wifi not configured";
    return false;
  }

  if (samples == 0) {
    note = "asr no speech";
    return false;
  }

  // 静音裁剪:去掉首尾低于阈值的段,只留有效语音(+100ms 余量)
  uint32_t lo = 0, hi = samples;
  wioRecTrim(lo, hi);
  if (hi <= lo) {
    note = "asr no speech";
    return false;
  }
  if (hi - lo < 3200) {
    // 有效语音不足 0.2s 时向两侧适当扩充至 3200 样本，绝不扩回整段 48000 静音
    if (samples <= 3200) {
      lo = 0;
      hi = samples;
    } else if (lo + 3200 <= samples) {
      hi = lo + 3200;
    } else {
      lo = samples - 3200;
      hi = samples;
    }
  }
  const uint32_t n = hi - lo;
  Serial.print("V: trimmed ");
  Serial.print(samples);
  Serial.print(" -> ");
  Serial.println(n);

  if (!wioAsrBaidu(wioRecBuffer() + lo, n, transcript, note)) {
    if (bufferFull && note == "asr rejected") {
      note = "asr buffer full";
    }
    return false;
  }
  if (!hasActiveConfig || strlen(activeConfig.deepseekKey) < 5) {
    return true;  // 只识别,无回答
  }

  // 识别出文字后立即刷新气泡第一行，让用户即时看到识别结果，消除"卡死感"
  drawBubbleText("你:" + transcript, 0x8410, String(petSkinGetName()) + "正在思考… (按 C 取消)", 0x7BEF);

  // 90 秒无互动则清除过期历史
  if (chatHistoryCount > 0 && millis() - lastChatTime > CHAT_CONTEXT_EXPIRE_MS) {
    clearChatHistory();
    Serial.println("P: chat context expired");
  }

  String raw;
  if (!wioLlmAskWithHistory(chatHistory, chatHistoryCount, transcript, raw, note)) return false;
  size_t textStart = 0;
  emotion = parseEmotionTag(raw.c_str(), raw.length(), &textStart);
  reply = raw.substring((unsigned int)textStart);  // 标签不上屏
  if (!reply.length()) {  // 空回复,或只给了标签没有正文
    note = "llm empty reply";
    return false;
  }

  // 记录本轮对话进多轮上下文(保留情绪标签前缀,防止模型在后续轮次模仿无标签格式)
  addChatHistory(transcript, raw);
  return true;
}

// ============================================================
// 状态机
// ============================================================

const char* petStateStr(PetState s) {
  switch (s) {
    case ST_IDLE: return "ST_IDLE";
    case ST_RECORD: return "ST_RECORD";
    case ST_THINK: return "ST_THINK";
    case ST_SHOW: return "ST_SHOW";
    case ST_SLEEP: return "ST_SLEEP";
    case ST_CONFIG: return "ST_CONFIG";
    default: return "UNKNOWN";
  }
}

void petSetState(PetState s) {
  if (state != s) {
    state = s;
    Serial.print("!STATE ");
    Serial.println(petStateStr(s));
  }
}
uint32_t recSamples = 0;
bool recBufferFull = false;
static bool recKeyBReleased = false;
const unsigned long PET_DROWSY_MS = 180000;  // 3 分钟没人理 → 犯困
const unsigned long PET_SLEEP_MS = 300000;   // 5 分钟 → 睡觉
const int PET_DIM_LEVEL = 40;                // 睡觉时背光 PWM(0-255)

// 背光:LCD_BACKLIGHT 实测不支持 PWM,analogWrite 会直接全灭且无法调暗 —— 睡觉时保持亮度,只显示睡觉画面
void setBacklight(bool dim) {
  (void)dim;
  (void)PET_DIM_LEVEL;
}

void setBacklightPower(bool on) {
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, on ? HIGH : LOW);
}

// Emotion(WioKitLogic.h)→ 表情;顺序必须与 enum Emotion 一致:开心 兴奋 惊讶 害羞 疑惑 难过
const int EMO_FACE[6] = {F_HAPPY, F_EXCITED, F_SURPRISED, F_SHY, F_CONFUSED, F_SAD};

// 把技术性 note 翻成小维口吻(原 note 仍显示在第二行,方便排查)
String friendlyNote(const String& note) {
  if (note.startsWith("asr buffer full")) return "一句话太长啦~";
  if (note.startsWith("asr no speech")) return "没听到声音呀~";
  if (note.startsWith("wifi")) return "我连不上 WiFi 啦…";
  if (note.startsWith("asr rejected")) return "没听清,再说一遍嘛~";
  if (note.startsWith("asr")) return "耳朵(语音识别)出故障了";
  if (note.startsWith("baidu")) return "语音服务登录失败了";
  if (note.startsWith("llm auth")) return "DeepSeek 密钥无效哦";
  if (note.startsWith("llm balance")) return "DeepSeek 余额不足啦";
  if (note.startsWith("llm rate")) return "提问太快啦,歇一会~";
  if (note.startsWith("llm")) return "脑袋(DeepSeek)卡住了";
  return "出了点小状况";
}

void drawBottomBar(const String& text, int x = -1, uint16_t textColor = TFT_LIGHTGREY) {
  const uint16_t bg = petSkinGetBottomBg();
  const int bx = 6, by = 208, bw = 308, bh = 29;

  if (currentSkinId == 1 && hasDsSkin) {
    // DeepSeek 大肥鱼主题：动态绘制高质感双层科技边框与圆角底框
    display.fillRoundRect(bx, by, bw, bh, 4, bg);
    display.drawRoundRect(bx, by, bw, bh, 4, 0x0000);              // 黑色外描边
    display.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 3, 0x4478); // DeepSeek 标志性海洋蓝内描边高光
  } else {
    // 小猫主题：原画 PET_SCENE_BG 已内嵌复古像素黑框，清空内部文本区防止字符残影
    display.fillRect(10, 210, 300, 26, bg);
  }

  // 自动计算水平居中位置 (若未显式指定 x >= 0)
  if (x < 0) {
    int w = 0;
    const uint8_t* p = (const uint8_t*)text.c_str();
    while (*p) {
      if (*p >= 0xE0 && *p <= 0xEF) { w += 16; p += 3; }
      else if (*p >= 0x80) { w += 16; p += 2; }
      else { w += 8; p++; }
    }
    x = (320 - w) / 2;
    if (x < 12) x = 12;
  }

  drawTextCJK(text.c_str(), x, 215, 320, textColor, bg);
}

void drawIdleHint() {
  drawBottomBar("按 B 说话 | 摇杆切页 | 压下互动", 34, TFT_LIGHTGREY);
}

void switchPage(int newPage) {
  if (newPage < 0) newPage = 2;
  if (newPage > 2) newPage = 0;
  curPage = newPage;
  lastPageBeforeVoice = curPage;
  lastInteract = millis();
  if (state == ST_IDLE) {
    if (curPage == PAGE_PET) {
      drawPet(F_NORMAL);
      drawIdleHint();
      petAnimSet(A_IDLE);
    } else if (curPage == PAGE_CLOCK) {
      drawClockPage(true);
    } else if (curPage == PAGE_FOCUS) {
      drawFocusPage(true);
    }
  }
}

void enterIdle();

void startVoiceRecord() {
  lastInteract = millis();
  setBacklightPower(true);
  beep(988, 50);
  delay(55);  // 等蜂鸣音彻底播完再开麦,杜绝麦克风采集到蜂鸣声造成误触发
  wioRecStart();
  petSetState(ST_RECORD);
  stateStart = millis();
  recKeyBReleased = (digitalRead(PIN_KEY_B) != LOW);
  syncKeyStates();
  drawPet(F_LISTEN);
  drawBubbleText("在听…说完我会自己停", TFT_BLACK, "也可再按 B 立即发送 / C 取消", TFT_BLACK);
  drawBottomBar("正在聆听… | 按 B 立即发送 | 按 C 取消", 18, TFT_LIGHTGREY);
  petAnimSet(A_LISTEN);
}

void enterConfig() {
  wioRecStop();
  petSetState(ST_CONFIG);
  stateStart = millis();
  syncKeyStates();
  beep(1200, 80);
  drawPet(F_LISTEN);
  drawBubbleText("手机连接热点: Wio-Pet", TFT_BLACK, "打开网页 192.168.4.1", TFT_BLACK);
  drawBottomBar("手机完成配置 | 按 B 退出", 64, TFT_LIGHTGREY);
  petAnimSet(A_LISTEN);
  if (!wioPortalBegin("Wio-Pet", activeConfig)) {
    Serial.println("V: portal start failed");
    wioPortalEnd();
    enterIdle();
  }
}

void enterIdle() {
  wioRecStop();
  petSetState(ST_IDLE);
  stateStart = millis();
  syncKeyStates();
  curPage = lastPageBeforeVoice;
  if (curPage == PAGE_PET) {
    drawPet(F_NORMAL);
    drawIdleHint();
    petAnimSet(A_IDLE);
  } else if (curPage == PAGE_CLOCK) {
    drawClockPage(true);
  } else if (curPage == PAGE_FOCUS) {
    drawFocusPage(true);
  }
}

void enterThink(uint32_t samples, bool bufFull) {
  recSamples = samples;
  recBufferFull = bufFull;
  beep(880, 50);
  petSetState(ST_THINK);
  stateStart = millis();
  syncKeyStates();
  drawPet(F_THINK);
  drawBubbleText("让我想想…", TFT_BLACK, "正在识别语音 (按 C 取消)", 0x7BEF);
  drawBottomBar("思考中… | 按 C 键可立即取消", 52, TFT_LIGHTGREY);
  petAnimSet(A_THINK);
}

// 显示一段表情 + 气泡,durMs 后自动回待机(期间按键也可提前返回)
void enterShow(int face, const String& t1, uint16_t c1, const String& t2, uint32_t durMs) {
  petSetState(ST_SHOW);
  stateStart = millis();
  showDurMs = durMs;
  syncKeyStates();
  drawPet(face);
  if (t1.length()) {
    drawBubbleText(t1, c1, t2, TFT_BLACK);
    drawBottomBar("按 B 继续对话 | 其他键返回", 56, TFT_LIGHTGREY);
  } else {
    drawIdleHint();
  }
  petAnimSet(A_EMOTE, face);
}

void enterSleep() {
  Serial.println("P: sleep");
  clearChatHistory();
  petSetState(ST_SLEEP);
  stateStart = millis();
  syncKeyStates();
  drawPet(F_SLEEP);
  drawBottomBar("沉睡中 zzz | 按任意键唤醒", 60, 0x7BEF);
  petAnimSet(A_SLEEP);
  setBacklight(true);
}

void wakeUp() {
  Serial.println("P: wake");
  setBacklight(false);
  setBacklightPower(true);
  lastInteract = millis();
  petSetState(ST_IDLE);
  stateStart = millis();
  syncKeyStates();
  curPage = lastPageBeforeVoice;
  if (curPage == PAGE_CLOCK) {
    drawClockPage(true);
  } else if (curPage == PAGE_FOCUS) {
    drawFocusPage(true);
  } else {
    curPage = PAGE_PET;
    drawPet(F_SLEEP);
    drawIdleHint();
    petAnimSet(A_WAKE);  // 睁眼动画结束后自动转 A_IDLE
  }
}

void startDizzy() {
  lastInteract = millis();
  petSetState(ST_SHOW);
  stateStart = millis();
  showDurMs = 3000;
  drawPet(F_DIZZY);
  drawBottomBar("天旋地转呜…别摇我啦~", 72, TFT_LIGHTGREY);
  petAnimSet(A_DIZZY);
}

// 传感器事件:只在 IDLE / SHOW / SLEEP 处理;录音、思考时不采样
void handleSense() {
  if (state != ST_IDLE && state != ST_SHOW && state != ST_SLEEP) return;
  const int ev = wioSensePoll();
  if (ev == SE_NONE) return;
  Serial.print("S: event=");
  Serial.println(ev);

  // 翻转事件检测 (Flip-to-Focus 扣下即专注)
  if (ev == SE_FACEDOWN) {
    Serial.println("P: flip facedown -> focus mode");
    lastInteract = millis();
    curPage = PAGE_FOCUS;
    lastPageBeforeVoice = PAGE_FOCUS;
    petSetState(ST_IDLE);
    stateStart = millis();
    startPomodoro();
    drawFocusPage(true);  // 先初始化专注看板帧缓冲，防止后续倒计时局部重绘叠在小猫背景上
    setBacklightPower(false);
    beep(880, 40);
    return;
  }
  if (ev == SE_FACEUP) {
    Serial.println("P: flip faceup -> restore display");
    lastInteract = millis();
    setBacklightPower(true);
    beep(1319, 60);
    petSetState(ST_IDLE);
    stateStart = millis();
    if (curPage == PAGE_FOCUS) {
      drawFocusPage(true);
    } else if (curPage == PAGE_CLOCK) {
      drawClockPage(true);
    } else {
      drawPet(F_NORMAL);
      drawIdleHint();
      petAnimSet(A_IDLE);
    }
    return;
  }

  if (state == ST_SLEEP) {
    if (ev == SE_LIGHT || ev == SE_PICKUP || ev == SE_SHAKE) wakeUp();
    return;
  }
  if (ev == SE_DARK) {
    enterSleep();
    return;
  }
  if (ev == SE_SHAKE) {
    beep(523, 60);
    startDizzy();
    return;
  }
  if (ev == SE_PICKUP) {
    lastInteract = millis();
    if (petAnimCurrent() == A_DROWSY) petAnimSet(A_IDLE);
  }
}

void performSkinToggleWithFeedback() {
  if (!petSkinHasDs()) {
    beep(440, 150);
    drawBubbleText("尚未同步大肥鱼皮肤", TFT_BLACK, "请在电脑运行 sync_qspi_skin.py", TFT_BLACK);
    delay(1800);
    enterIdle();
    return;
  }
  petSkinToggle();
  if (hasActiveConfig) {
    activeConfig.activeSkin = currentSkinId;
    wioConfigSave(activeConfig);
    if (strlen(activeConfig.deepseekKey) > 0) {
      wioLlmDeepSeekBegin(activeConfig.deepseekKey, getPetSystemPrompt());
    }
  }
  beep(1568, 80);
  delay(60);
  beep(1760, 120);
  drawPet(F_HAPPY);
  drawBubbleText("换装成功啦~", TFT_BLACK, String("我是") + petSkinGetName() + "!", TFT_BLACK);
  drawBottomBar(String(petSkinGetName()) + " 准备好啦!", -1, TFT_LIGHTGREY);
  delay(1500);
  enterIdle();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(WIO_BUZZER, OUTPUT);
  tone(WIO_BUZZER, 1200, 60);

  for (int i = 0; i < 8; i++) pinMode(ALL_KEYS[i], INPUT_PULLUP);
  syncKeyStates();
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, LOW);  // 先保持背光关闭，彻底杜绝开机白屏！

  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1000) {
  }
  Serial.println("INIT: start");

  display.begin();
  Serial.println("INIT: display ok");

  wioCjkBegin(display);  // 注入 display(解耦点①),之后 drawTextCJK 全局可用
  randomSeed(analogRead(A0) ^ micros());
  wioMicBegin();
  Serial.println("INIT: mic ok");
  wioMicConfig(400, PET_TRAILING_MS, 4500);  // 静音阈值 / 截断静音时长 / 没说话超时(4.5s)
  wioSenseBegin();
  Serial.println("INIT: sense ok");
  wioConfigInit();
  hasActiveConfig = wioConfigLoad(activeConfig);
  Serial.println("INIT: config ok");
  petSkinInit(hasActiveConfig ? activeConfig.activeSkin : 0);
  if (!hasActiveConfig) {
#if HAS_COMPILED_SECRETS
    memset(&activeConfig, 0, sizeof(activeConfig));
    strncpy(activeConfig.ssid, WIFI_SSID, sizeof(activeConfig.ssid) - 1);
    strncpy(activeConfig.pass, WIFI_PASS, sizeof(activeConfig.pass) - 1);
    strncpy(activeConfig.baiduApiKey, BAIDU_API_KEY, sizeof(activeConfig.baiduApiKey) - 1);
    strncpy(activeConfig.baiduSecret, BAIDU_SECRET_KEY, sizeof(activeConfig.baiduSecret) - 1);
    strncpy(activeConfig.deepseekKey, DEEPSEEK_KEY, sizeof(activeConfig.deepseekKey) - 1);
    strncpy(activeConfig.city, "上海", sizeof(activeConfig.city) - 1);
    hasActiveConfig = true;
    Serial.println("V: using default compiled secrets");
#endif
  } else {
    // 若 Flash 存储的是旧默认值“深圳”或为空，则平滑迁移为“上海”并持久化
    if (strcmp(activeConfig.city, "深圳") == 0 || strlen(activeConfig.city) == 0) {
      strncpy(activeConfig.city, "上海", sizeof(activeConfig.city) - 1);
      wioConfigSave(activeConfig);
      Serial.println("V: migrated default city to 上海 in Flash");
    }
  }
  applyActiveConfig();
  Serial.println("HELLO pet 1.4");
  drawPet(F_SLEEP);
  digitalWrite(LCD_BACKLIGHT, HIGH);  // 首帧就绪后再亮屏，从深黑优雅平滑醒来！
  if (hasActiveConfig && strlen(activeConfig.ssid) > 0) {
    drawBottomBar(String(petSkinGetName()) + " 醒来中…正在连网", -1, TFT_LIGHTGREY);
    petAnimSet(A_WAKE);
    petNetWarmup();
    if (WiFi.status() == WL_CONNECTED) {
      syncNetworkTime();
    }
  } else {
    drawBottomBar("初次见面! 按 A 键配网", 80, TFT_LIGHTGREY);
    petAnimSet(A_WAKE);
  }
#if PET_TEST_BAD_TOKEN
  wioAsrBaiduDebugToken("invalid_token_for_selftest_0000");
#endif
  lastInteract = millis();
  enterIdle();
  Serial.println("!READY");
}

uint32_t petFreeRam() {
  char top = 't';
  return (uint32_t)(&top - reinterpret_cast<char*>(sbrk((ptrdiff_t)0)));
}

void handleTestCommands() {
  while (Serial.available()) {
    static String cmdBuf = "";
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '!' && cmdBuf.length() > 0 && !cmdBuf.startsWith("!")) {
      cmdBuf = "";  // 丢弃前面由于端口打开产生的脏字符
    }
    if (c == '\n') {
      cmdBuf.trim();
      if (cmdBuf.length() > 0) {
        if (cmdBuf == "!PING") {
          Serial.println("!PONG");
        } else if (cmdBuf == "!STATUS") {
          Serial.print("!STATUS state=");
          Serial.print(petStateStr(state));
          Serial.print(" anim=");
          Serial.print(petAnimCurrent());
          Serial.print(" page=");
          Serial.print(curPage);
          Serial.print(" pomo=");
          Serial.print(pomoRemainingSec);
          Serial.print(" pomo_run=");
          Serial.print(pomoRunning ? 1 : 0);
          Serial.print(" ram=");
          Serial.print(petFreeRam());
          Serial.print(" uptime=");
          Serial.println(millis());
        } else if (cmdBuf == "!KEY_A" || cmdBuf == "!KEY A") {
          petInjectKey(PIN_KEY_A);
          Serial.println("!OK KEY_A");
        } else if (cmdBuf == "!KEY_B" || cmdBuf == "!KEY B") {
          petInjectKey(PIN_KEY_B);
          Serial.println("!OK KEY_B");
        } else if (cmdBuf == "!KEY_C" || cmdBuf == "!KEY C") {
          petInjectKey(PIN_KEY_C);
          Serial.println("!OK KEY_C");
        } else if (cmdBuf == "!KEY_JOY_L" || cmdBuf == "!KEY_LEFT") {
          petInjectKey(PIN_JOY_L);
          Serial.println("!OK KEY_JOY_L");
        } else if (cmdBuf == "!KEY_JOY_R" || cmdBuf == "!KEY_RIGHT") {
          petInjectKey(PIN_JOY_R);
          Serial.println("!OK KEY_JOY_R");
        } else if (cmdBuf == "!KEY_JOY_PRESS" || cmdBuf == "!KEY_PRESS") {
          petInjectKey(WIO_5S_PRESS);
          Serial.println("!OK KEY_JOY_PRESS");
        } else if (cmdBuf.startsWith("!PAGE ")) {
          int pg = cmdBuf.substring(6).toInt();
          switchPage(pg);
          Serial.print("!OK PAGE ");
          Serial.println(pg);
          Serial.flush();
        } else if (cmdBuf == "!POMO_START") {
          startPomodoro();
          Serial.println("!OK POMO_START");
        } else if (cmdBuf == "!POMO_PAUSE") {
          pausePomodoro();
          Serial.println("!OK POMO_PAUSE");
        } else if (cmdBuf == "!POMO_RESET") {
          resetPomodoro();
          Serial.println("!OK POMO_RESET");
        } else if (cmdBuf == "!CFG_DUMP") {
          Serial.print("!CFG ssid=");
          Serial.print(activeConfig.ssid);
          Serial.print(" city=");
          Serial.println(activeConfig.city);
        } else if (cmdBuf == "!CFG_CLEAR") {
          wioConfigClear();
          Serial.println("!OK CFG_CLEAR");
        } else if (cmdBuf.startsWith("!ANIM ")) {
          int a = cmdBuf.substring(6).toInt();
          petAnimSet(a);
          Serial.print("!OK ANIM ");
          Serial.println(a);
        } else if (cmdBuf.startsWith("!LOOK ")) {
          int lk = cmdBuf.substring(6).toInt();
          drawFeatures(F_NORMAL, lk);
          Serial.print("!OK LOOK ");
          Serial.println(lk);
#if defined(ARDUINO)
        } else if (cmdBuf.startsWith("!QSPI_ERASE ")) {
          int sp = cmdBuf.indexOf(' ', 12);
          uint32_t addr = strtoul(cmdBuf.substring(12, sp > 0 ? sp : cmdBuf.length()).c_str(), NULL, 16);
          uint32_t len = (sp > 0) ? strtoul(cmdBuf.substring(sp + 1).c_str(), NULL, 16) : 4096;
          wioConfigInit();
          const sfud_flash* flash = sfud_get_device(0);
          if (flash && sfud_erase(flash, addr, len) == SFUD_SUCCESS) {
            Serial.print("!OK QSPI_ERASE ");
            Serial.println(len, HEX);
          } else {
            Serial.println("!ERR QSPI_ERASE");
          }
        } else if (cmdBuf.startsWith("!QSPI_WRITE ")) {
          int sp = cmdBuf.indexOf(' ', 12);
          if (sp < 0) {
            Serial.println("!ERR QSPI_WRITE_SYNTAX");
          } else {
            uint32_t addr = strtoul(cmdBuf.substring(12, sp).c_str(), NULL, 16);
            String hexStr = cmdBuf.substring(sp + 1);
            hexStr.trim();
            size_t byteLen = hexStr.length() / 2;
            if (byteLen > 0 && byteLen <= 512) {
              uint8_t buf[512];
              for (size_t i = 0; i < byteLen; i++) {
                char h1 = hexStr[i * 2];
                char h2 = hexStr[i * 2 + 1];
                auto unhex = [](char c) -> uint8_t {
                  if (c >= '0' && c <= '9') return c - '0';
                  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                  return 0;
                };
                buf[i] = (unhex(h1) << 4) | unhex(h2);
              }
              const sfud_flash* flash = sfud_get_device(0);
              if (flash && sfud_write(flash, addr, byteLen, buf) == SFUD_SUCCESS) {
                Serial.print("!OK QSPI_WRITE ");
                Serial.println(byteLen);
              } else {
                Serial.println("!ERR QSPI_WRITE_FAIL");
              }
            } else {
              Serial.println("!ERR QSPI_WRITE_LEN");
            }
          }
        } else if (cmdBuf.startsWith("!QSPI_DIAG")) {
          Serial.print("!DIAG skinId="); Serial.print(currentSkinId);
          Serial.print(" hasDs="); Serial.print(hasDsSkin);
          Serial.print(" hdrMagic="); Serial.print(dsSkinHeader.magic, HEX);
          Serial.print(" bgOff="); Serial.print(dsSkinHeader.bgOffset);
          Serial.print(" palOff="); Serial.print(dsSkinHeader.paletteOffset);
          Serial.print(" face="); Serial.print(dsSkinHeader.faceW); Serial.print("x"); Serial.println(dsSkinHeader.faceH);

          int palZeroCount = 0;
          for (int i = 0; i < 256; i++) {
            if (dsPalette[i] == 0) palZeroCount++;
          }
          Serial.print("!PAL_ZERO_COUNT="); Serial.println(palZeroCount);
          Serial.print("!PAL_FIRST8=");
          for (int i = 0; i < 8; i++) {
            Serial.print(dsPalette[i], HEX); Serial.print(" ");
          }
          Serial.println();

          uint8_t sfReadBuf[16] = {0};
          uint32_t bgAddr = QSPI_SKIN1_ADDR + dsSkinHeader.bgOffset;
          petQspiRead(bgAddr, sfReadBuf, 16);

          Serial.print("!BG_READ=");
          for (int i = 0; i < 16; i++) {
            if (sfReadBuf[i] < 0x10) Serial.print('0');
            Serial.print(sfReadBuf[i], HEX);
          }
          Serial.println();
        } else if (cmdBuf.startsWith("!QSPI_READ ")) {
          int sp = cmdBuf.indexOf(' ', 11);
          uint32_t addr = strtoul(cmdBuf.substring(11, sp > 0 ? sp : cmdBuf.length()).c_str(), NULL, 16);
          size_t len = (sp > 0) ? strtoul(cmdBuf.substring(sp + 1).c_str(), NULL, 10) : 16;
          if (len > 256) len = 256;
          const sfud_flash* flash = sfud_get_device(0);
          uint8_t rBuf[256];
          if (flash && sfud_read(flash, addr, len, rBuf) == SFUD_SUCCESS) {
            Serial.print("!OK QSPI_READ ");
            for (size_t i = 0; i < len; i++) {
              if (rBuf[i] < 0x10) Serial.print('0');
              Serial.print(rBuf[i], HEX);
            }
            Serial.println();
          } else {
            Serial.println("!ERR QSPI_READ");
          }
        } else if (cmdBuf.startsWith("!QSPI_CHECK ")) {
          uint32_t addr = strtoul(cmdBuf.substring(12).c_str(), NULL, 16);
          const sfud_flash* flash = sfud_get_device(0);
          uint32_t magic = 0;
          if (flash) sfud_read(flash, addr, 4, (uint8_t*)&magic);
          Serial.print("!MAGIC ");
          Serial.println(magic, HEX);
#endif
        } else if (cmdBuf.startsWith("!SKIN ")) {
          int s = cmdBuf.substring(6).toInt();
          if (petSkinSet(s)) {
            if (hasActiveConfig) {
              activeConfig.activeSkin = currentSkinId;
              wioConfigSave(activeConfig);
              if (strlen(activeConfig.deepseekKey) > 0) {
                wioLlmDeepSeekBegin(activeConfig.deepseekKey, getPetSystemPrompt());
              }
            }
            drawPet(F_HAPPY);
            drawIdleHint();
            Serial.print("!OK SKIN ");
            Serial.println(s);
          } else {
            Serial.println("!ERR SKIN");
          }
        }
      }
      cmdBuf = "";
    } else {
      if (cmdBuf.length() < 1200) cmdBuf += c;
    }
  }
}

void loop() {
  digitalWrite(LED_BUILTIN, ((millis() / 500) % 2) ? HIGH : LOW);
  handleTestCommands();
  petAnimTick();
  handleSense();
  tickPomodoroLogic();
  if (pomoJustFinished) {
    pomoJustFinished = false;
    lastInteract = millis();
    setBacklightPower(true);
    beep(1568, 150);
    delay(150);
    beep(1760, 250);
    if (state == ST_IDLE && curPage == PAGE_FOCUS) {
      drawFocusPage(false);
    }
  }
  // 定期网络对时:仅在 ST_IDLE 待机下触发,且 syncNetworkTime 内部已保证无论成败都推进 nextTimeSyncMs
  if (state == ST_IDLE && hasSyncedTime && (int32_t)(millis() - nextTimeSyncMs) >= 0) {
    syncNetworkTime();
  }
  switch (state) {
    case ST_IDLE: {
      const unsigned long idleFor = millis() - lastInteract;
      if (curPage == PAGE_PET && !pomoRunning) {
        if (idleFor > PET_SLEEP_MS) {
          enterSleep();
          break;
        }
        if (idleFor > PET_DROWSY_MS && petAnimCurrent() == A_IDLE) petAnimSet(A_DROWSY);
      }

      // 实时看板帧刷新
      if (curPage == PAGE_CLOCK) {
        drawClockPage(false);
      } else if (curPage == PAGE_FOCUS) {
        drawFocusPage(false);
      }

      if (pressed(PIN_KEY_A)) {  // A 键:进入端侧热点网页配网
        lastInteract = millis();
        setBacklightPower(true);
        enterConfig();
        break;
      }

      if (pressed(PIN_KEY_B)) {  // B 键:全局对讲问答
        lastPageBeforeVoice = curPage;
        startVoiceRecord();
        break;
      }

      if (pressed(PIN_JOY_R)) {  // 摇杆右: 下一页
        setBacklightPower(true);
        switchPage((curPage + 1) % 3);
        beep(1200, 30);
        break;
      }
      if (pressed(PIN_JOY_L)) {  // 摇杆左: 上一页
        setBacklightPower(true);
        switchPage((curPage + 2) % 3);
        beep(1200, 30);
        break;
      }

      if (curPage == PAGE_PET) {
        if (pressed(PIN_KEY_C)) {  // C 键:叫醒犯困的小维
          lastInteract = millis();
          petAnimSet(A_IDLE);
          break;
        }
        if (pressed(WIO_5S_PRESS)) {  // 摇杆下压: 换装 / 切换形象
          lastInteract = millis();
          Serial.println("K: joy press -> toggle skin!");
          performSkinToggleWithFeedback();
          break;
        }
        if (pressed(WIO_5S_UP) || pressed(WIO_5S_DOWN)) {  // 摇杆上下: 摸头/互动
          lastInteract = millis();
          beep(1568, 80);
          enterShow(F_HAPPY, "", TFT_BLACK, "", 1200);
          break;
        }
      } else if (curPage == PAGE_CLOCK) {
        if (pressed(WIO_5S_PRESS)) {  // 压下对时
          lastInteract = millis();
          beep(1200, 50);
          drawTextCJK("正在网络对时...", 100, 160, 240, C_TEXT_GOLD, C_BG_CLOCK);
          if (syncNetworkTime()) {
            beep(1568, 80);
          } else {
            beep(440, 150);
          }
          drawClockPage(true);
          break;
        }
      } else if (curPage == PAGE_FOCUS) {
        if (pressed(WIO_5S_PRESS)) {  // 启停番茄钟
          lastInteract = millis();
          if (pomoRunning) {
            pausePomodoro();
            beep(600, 80);
          } else {
            startPomodoro();
            beep(1200, 80);
          }
          drawFocusPage(true);
          break;
        }
        if (pressed(PIN_KEY_C)) {  // 重置番茄钟
          lastInteract = millis();
          resetPomodoro();
          beep(880, 80);
          drawFocusPage(true);
          break;
        }
      }
      break;
    }

    case ST_RECORD: {
      uint32_t samples = 0, hint = 0;

      // 跟踪初始按下的 B 键是否已经松开:
      // - 若用户习惯"按住 B 说话、松开即发"(按住超过 700ms 且已检测到人声后松开)，松手时立即送入思考
      // - 若是普通点按(<700ms 松开)，则标记 recKeyBReleased=true，后续再按一次 B 才触发手动立即发送
      if (!recKeyBReleased && digitalRead(PIN_KEY_B) != LOW) {
        recKeyBReleased = true;
        if (millis() - stateStart > 700 && wioRecHadSpeech()) {
          const uint32_t stoppedSamples = wioRecStop();
          if (stoppedSamples > 1600) {
            enterThink(stoppedSamples, false);
            break;
          }
        }
      }

      // 1. 按键干预：
      // - 按 C 键：取消本次录音，真正关闭 DMA 录音并快速返回待机
      // - 已松开过 B 键后再按 B 键：提前结束说话，真正停止 DMA 并取实际样本数送入思考
      if (millis() - stateStart > 200) {
        if (pressed(PIN_KEY_C)) {
          wioRecStop();  // 真正关闭麦克风 DMA
          beep(440, 60);
          enterIdle();
          break;
        }
        if (recKeyBReleased && pressed(PIN_KEY_B)) {
          const uint32_t stoppedSamples = wioRecStop();
          if (!wioRecHadSpeech() && stoppedSamples < 3200) {
            beep(196, 200);
            enterShow(F_SAD, "你还没说话呢~", TFT_BLACK, "按一下 B 再开口就好", 2500);
          } else {
            enterThink((stoppedSamples > 1600) ? stoppedSamples : 1600, false);
          }
          break;
        }
      }

      // 2. 底层 VAD 自动判停
      const int st = wioRecPoll(samples, hint);
      if (st == 2) {  // 超时没说话
        wioRecStop();
        beep(196, 250);
        enterShow(F_SAD, "你还没说话呢~", TFT_BLACK, "按一下 B 再开口就好", 3000);
        break;
      }
      if (st == 1 || st == 3) {  // 录完: 1=正常说完截断, 3=录满3秒强行截断
        wioRecStop();
        if (st == 3) {
          Serial.println("V: buffer full (hit 3s limit)");
        }
        enterThink(samples, (st == 3));
        break;
      }

      // 3. 硬件/环境硬看门狗：录音最长决不允许超过 8.5 秒(4.5s开口等待+2.8s说话+1.2s尾静音)，杜绝任何卡死
      if (millis() - stateStart > 8500) {
        const bool hadSpeech = wioRecHadSpeech();
        const uint32_t stoppedSamples = wioRecStop();
        if (hadSpeech && stoppedSamples > 3200) {
          enterThink(stoppedSamples, stoppedSamples >= WIO_MIC_MAX_SAMPLES);
        } else {
          beep(196, 250);
          enterShow(F_SAD, "你还没说话呢~", TFT_BLACK, "按一下 B 再开口就好", 3000);
        }
        break;
      }
      break;
    }

    case ST_THINK: {
      // 识别 + 回答(等待期间 petAnimTick 与取消键检测由 wioNetYield 驱动)
      String t, r, note;
      int emo = EMO_HAPPY;
      const uint32_t tProc = millis();
      const bool ok = petProcessVoice(recSamples, recBufferFull, t, r, emo, note);
      petPrintTiming(millis() - tProc);
      lastInteract = millis();
      if (ok) {
        beep(1319, 90);
        const int face = (emo >= 0 && emo < 6) ? EMO_FACE[emo] : F_HAPPY;
        String cleanR = "";
        for (unsigned int i = 0; i < r.length(); i++) {
          char c = r[i];
          if (c != '*' && c != '#' && c != '`') cleanR += c;
        }
        enterShow(face, "你:" + t, 0x8410,
                  cleanR.length() ? (String(petSkinGetName()) + ":" + cleanR) : String("(还没配置 DeepSeek Key,我不会说话呀)"), 15000);
      } else if (thinkUserCancelled) {
        // 用户按 C 键主动取消思考:直接回待机,不弹错误框
        beep(440, 60);
        enterIdle();
      } else {
        beep(196, 250);
        if (note == "asr buffer full") {
          enterShow(F_CONFUSED, "呜…一句话太长啦~", 0x8410, "单次说话请在3秒内哦", 5000);
        } else if (thinkWatchdogTimeout) {
          enterShow(F_SAD, "呜…网络响应超时啦~", 0x8410, "请检查 WiFi 信号后再试", 5000);
        } else {
          enterShow(F_SAD, "呜…" + friendlyNote(note), 0x8410, note, 5000);
        }
      }
      break;
    }

    case ST_SHOW: {
      // 在展示对话结果期间(400ms后)，若直接按 B 键则无缝开启下一轮多轮对话录音!
      if (millis() - stateStart > 400 && pressed(PIN_KEY_B)) {
        startVoiceRecord();
        break;
      }
      const bool otherKeyAfterGrace = millis() - stateStart > 400 && (anyKeyDown() || injectedKey != -1);
      if (millis() - stateStart >= showDurMs || otherKeyAfterGrace) {
        injectedKey = -1;
        while (anyKeyDown()) petAnimTick();  // 等松手,避免同一次按键在待机里又触发
        enterIdle();
      }
      break;
    }

    case ST_SLEEP: {
      if (anyKeyDown() || injectedKey != -1) {
        injectedKey = -1;
        wakeUp();
        while (anyKeyDown()) petAnimTick();  // 等松手,避免叫醒的那一下又触发录音
        syncKeyStates();
      }
      break;
    }

    case ST_CONFIG: {
      // 退出配网:不在此处同步调阻塞的 WiFi.begin()，确保 <50ms 秒退回 ST_IDLE(下次按 B 说话时 wioNetConnected 自动按需重连)
      if (millis() - stateStart > 300 && (pressed(PIN_KEY_B) || pressed(PIN_KEY_C) || pressed(PIN_KEY_A))) {
        Serial.println("V: exit config by button");
        beep(880, 80);
        wioPortalEnd();
        enterIdle();
        break;
      }
      WioConfig newCfg;
      const int st = wioPortalPoll(newCfg);
      if (st == 1) {  // 手机配网已保存
        Serial.println("V: config saved from web");
        beep(1568, 150);
        activeConfig = newCfg;
        hasActiveConfig = true;
        wioPortalEnd();
        drawPet(F_HAPPY);
        drawBubbleText("配置保存成功！", TFT_BLACK, "正在重新连网中…", TFT_BLACK);
        petAnimSet(A_EMOTE, F_HAPPY);
        delay(800);
        applyActiveConfig();
        petNetWarmup();
        if (WiFi.status() == WL_CONNECTED) {
          syncNetworkTime();
        }
        enterIdle();
        break;
      }
      if (st == 2) {  // 手机网页点击退出
        Serial.println("V: exit config by web");
        beep(880, 80);
        wioPortalEnd();
        enterIdle();
        break;
      }
      break;
    }
  }
}
