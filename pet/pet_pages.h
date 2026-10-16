// ---- 桌面多功能看板系统: 赛博时钟/天气 + 翻转25分钟专注番茄钟 ----
#pragma once
#include <Arduino.h>
#include <rpcWiFi.h>
#include <Seeed_GFX.h>
#include <WioKitCjk.h>
#include <WioKitConfig.h>

extern Seeed_GFX display;
extern WioConfig activeConfig;
extern bool hasActiveConfig;

enum PageMode {
  PAGE_PET = 0,    // 0: 像素小猫房间 (默认)
  PAGE_CLOCK = 1,  // 1: 赛博大字号时钟与上海天气
  PAGE_FOCUS = 2   // 2: 翻转25分钟番茄钟
};

// 当前页面
extern int curPage;
extern int lastPageBeforeVoice;

// ---- 网络时间同步管理 ----
extern uint32_t netEpochBase;
extern uint32_t netEpochMillis;
extern bool hasSyncedTime;
extern uint32_t nextTimeSyncMs;

// 将 HTTP 日期解析并转换为格林威治时间戳
static bool parseHttpDate(const char* str, int& yr, int& mo, int& d, int& h, int& m, int& s) {
  const char* p = strchr(str, ',');
  if (!p) p = str;
  else p++;
  while (*p == ' ') p++;
  d = atoi(p);
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  static const char* const MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  mo = 1;
  for (int i = 0; i < 12; i++) {
    if (strncmp(p, MONTHS[i], 3) == 0) { mo = i + 1; break; }
  }
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  yr = atoi(p);
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  h = atoi(p);
  p = strchr(p, ':');
  if (p) {
    m = atoi(p + 1);
    p = strchr(p + 1, ':');
    if (p) s = atoi(p + 1);
  }
  return (yr >= 2020 && d >= 1 && d <= 31 && mo >= 1 && mo <= 12);
}

static uint32_t toUnixEpoch(int y, int m, int d, int h, int min, int s) {
  if (m <= 2) y -= 1;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int days = era * 146097 + (int)doe - 719468;
  return (uint32_t)(days * 86400LL + h * 3600 + min * 60 + s);
}

static void fromUnixEpoch(uint32_t epoch, int& yr, int& mo, int& d, int& h, int& m, int& s, int& wday) {
  // 北京/上海时间: UTC+8
  epoch += 8 * 3600;
  const uint32_t days = epoch / 86400;
  const uint32_t rem = epoch % 86400;
  h = rem / 3600;
  m = (rem % 3600) / 60;
  s = rem % 60;
  wday = (days + 4) % 7;  // 0=周日, 1=周一, ..., 6=周六
  const int era = (days + 719468) / 146097;
  const unsigned doe = (days + 719468) - era * 146097;
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int y = yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  mo = mp < 10 ? mp + 3 : mp - 9;
  yr = y + (mo <= 2 ? 1 : 0);
}

// 快速通过百度 HTTP 标头对时
bool syncNetworkTime() {
  // 无论成功或失败均先推进 nextTimeSyncMs(失败后退避 5 分钟再试)，彻底防止 loop() 陷入无限重连死循环!
  nextTimeSyncMs = millis() + 300000UL;
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  if (!client.connect("www.baidu.com", 80, 2000)) {
    client.stop();
    return false;
  }
  client.setTimeout(400);
  client.print("HEAD / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n");
  const unsigned long t0 = millis();
  bool ok = false;
  while (millis() - t0 < 1500) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (line.startsWith("Date: ") || line.startsWith("date: ")) {
        int yr = 0, mo = 0, d = 0, h = 0, m = 0, s = 0;
        if (parseHttpDate(line.c_str() + 6, yr, mo, d, h, m, s)) {
          netEpochBase = toUnixEpoch(yr, mo, d, h, m, s);
          netEpochMillis = millis();
          hasSyncedTime = true;
          nextTimeSyncMs = millis() + 3600000UL;  // 成功后每小时同步一次
          ok = true;
          Serial.print("V: time synced from HTTP: ");
          Serial.print(yr); Serial.print("-"); Serial.print(mo); Serial.print("-"); Serial.print(d);
          Serial.print(" "); Serial.print(h); Serial.print(":"); Serial.print(m); Serial.print(":"); Serial.println(s);
          break;
        }
      }
    }
    if (ok) break;
    delay(10);
  }
  client.stop();
  return ok;
}

// 获取当前上海时间
void getCurrentShanghaiTime(int& yr, int& mo, int& d, int& h, int& m, int& s, int& wday) {
  if (hasSyncedTime) {
    const uint32_t elapsed = (millis() - netEpochMillis) / 1000;
    fromUnixEpoch(netEpochBase + elapsed, yr, mo, d, h, m, s, wday);
  } else {
    // 兜底时间: 从开机 12:00:00 累加
    const uint32_t totalSec = (millis() / 1000) + 12 * 3600;
    s = totalSec % 60;
    m = (totalSec / 60) % 60;
    h = (totalSec / 3600) % 24;
    yr = 2026; mo = 10; d = 5; wday = 1;
  }
}

// ---- 番茄钟专注管理 (25 分钟) ----
const uint32_t POMO_DURATION_SEC = 25 * 60;
extern uint32_t pomoRemainingSec;
extern bool pomoRunning;
extern uint32_t pomoLastTickMs;
extern int pomoCompletedTotal;
extern bool pomoJustFinished;

void startPomodoro();
void pausePomodoro();
void resetPomodoro();
void tickPomodoroLogic();

// ---- 页面 UI 渲染实现 ----

// 颜色定义 (OLED 极致纯黑底 + 霓虹高对比)
const uint16_t C_BG_PAGE   = TFT_BLACK;  // 0x0000 纯黑底，彻底消除液晶边框漏光
const uint16_t C_BG_CLOCK  = C_BG_PAGE;
const uint16_t C_BG_FOCUS  = C_BG_PAGE;
const uint16_t C_TIME_CYAN = 0x07FF;     // 霓虹青 (极醒目大数字)
const uint16_t C_TEXT_CYAN = C_TIME_CYAN;
const uint16_t C_TIME_GOLD = 0xFEA0;     // 暖金黄 (秒数与小标签)
const uint16_t C_TEXT_GOLD = C_TIME_GOLD;
const uint16_t C_POMO_RED  = 0xFA40;     // 番茄红
const uint16_t C_POMO_DONE = 0x07E0;     // 完成亮绿
const uint16_t C_DIVIDER   = 0x2145;     // 科技暗蓝分割线

static int lastClockSec = -1;
static int lastClockMin = -1;
static int lastClockHour = -1;
static int lastClockDay = -1;
static int lastClockWifi = -1;

// 限制城市名最多显示 3 个汉字(9字节 UTF-8)，防止长城市名(如乌鲁木齐)侵入右侧日期区域(x=74)
static void formatShortCity(char out[16]) {
  const char* src = (hasActiveConfig && strlen(activeConfig.city) > 0) ? activeConfig.city : "上海";
  size_t pos = 0;
  int chars = 0;
  while (src[pos] && chars < 3 && pos < 12) {
    unsigned char b = (unsigned char)src[pos];
    size_t step = (b >= 0xF0) ? 4 : (b >= 0xE0 ? 3 : (b >= 0xC0 ? 2 : 1));
    if (pos + step > 12) break;
    for (size_t k = 0; k < step && src[pos]; k++) {
      out[pos] = src[pos];
      pos++;
    }
    chars++;
  }
  out[pos] = '\0';
}

void drawClockPage(bool fullRedraw = true) {
  int yr, mo, d, h, m, s, wday;
  getCurrentShanghaiTime(yr, mo, d, h, m, s, wday);

  if (fullRedraw) {
    display.fillScreen(C_BG_PAGE);

    // 1. 顶部状态栏: 城市 + 日期与星期 + WiFi 状态 (单次绘制，无闪烁)
    char locBuf[16];
    formatShortCity(locBuf);
    drawTextCJK(locBuf, 16, 8, 72, C_TIME_CYAN, C_BG_PAGE);

    display.drawFastHLine(14, 32, 292, C_DIVIDER);

    // 2. 秒数副标
    display.setTextColor(0x632C, C_BG_PAGE);
    display.drawString("SEC", 274, 90, 2);

    // 3. 底部操作栏提示
    display.drawFastHLine(14, 198, 292, C_DIVIDER);
    drawTextCJK("按 B 对讲  |  摇杆切页  |  压下对时", 42, 212, 320, 0x7BEF, C_BG_PAGE);

    lastClockSec = -1;
    lastClockMin = -1;
    lastClockHour = -1;
    lastClockDay = -1;
    lastClockWifi = -1;
  }

  // 动态刷新右上角 WiFi 在线/离线状态
  const int curWifi = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  if (curWifi != lastClockWifi) {
    lastClockWifi = curWifi;
    display.fillRect(272, 8, 40, 20, C_BG_PAGE);
    if (curWifi) {
      drawTextCJK("WiFi", 274, 8, 312, 0x07E0, C_BG_PAGE);
    } else {
      drawTextCJK("离线", 274, 8, 312, 0x7BEF, C_BG_PAGE);
    }
  }

  // 跨天或全量重绘时刷新顶部日期
  if (d != lastClockDay) {
    lastClockDay = d;
    display.fillRect(74, 8, 196, 20, C_BG_PAGE);
    static const char* const W_NAMES[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    char dateBuf[48];
    snprintf(dateBuf, sizeof(dateBuf), "%04d年%02d月%02d日 %s", yr, mo, d, W_NAMES[wday]);
    drawTextCJK(dateBuf, 74, 8, 268, 0xDEFB, C_BG_PAGE);
  }

  // 核心时间: HH:MM 仅在分钟改变时刷新 (Font 8: 75 像素巨大字体，直写无 fillRect，杜绝闪烁!)
  if (m != lastClockMin || fullRedraw) {
    lastClockMin = m;
    char hmStr[8];
    snprintf(hmStr, sizeof(hmStr), "%02d:%02d", h, m);
    display.setTextColor(C_TIME_CYAN, C_BG_PAGE);
    display.drawString(hmStr, 16, 46, 8);  // 75px 超大字号!
  }

  // 秒数: 仅在秒数改变时局部直写 (Font 4: 32 像素，无 fillRect)
  if (s != lastClockSec || fullRedraw) {
    lastClockSec = s;
    char secStr[4];
    snprintf(secStr, sizeof(secStr), "%02d", s);
    display.setTextColor(C_TIME_GOLD, C_BG_PAGE);
    display.drawString(secStr, 274, 54, 4);  // 32px 动态秒数

    // 60 秒科技感线性进度条 (仅 3 像素高，快速写入无闪烁)
    const int totalW = 288;
    const int curW = (s * totalW) / 59;
    if (curW > 0) display.fillRect(16, 132, curW, 3, C_TIME_CYAN);
    if (curW < totalW) display.fillRect(16 + curW, 132, totalW - curW, 3, 0x18E3);
  }

  // 时段关怀寄语: 仅在小时改变时更新 (单次局部擦写，避免每秒重画闪烁)
  if (h != lastClockHour || fullRedraw) {
    lastClockHour = h;
    display.fillRect(16, 150, 288, 24, C_BG_PAGE);
    if (h >= 6 && h < 12) {
      drawTextCJK("[ 早上好! 阳光正好，元气满满开始新一天~ ]", 18, 154, 302, TFT_WHITE, C_BG_PAGE);
    } else if (h >= 12 && h < 14) {
      drawTextCJK("[ 中午啦! 记得好好吃饭休息，补充能量哦~ ]", 18, 154, 302, TFT_WHITE, C_BG_PAGE);
    } else if (h >= 14 && h < 18) {
      drawTextCJK("[ 下午好! 工作辛苦啦，喝杯咖啡提提神吧~ ]", 18, 154, 302, TFT_WHITE, C_BG_PAGE);
    } else if (h >= 18 && h < 22) {
      drawTextCJK("[ 傍晚啦! 今日任务搞定了吗? 按时下班哦~ ]", 18, 154, 302, TFT_WHITE, C_BG_PAGE);
    } else {
      drawTextCJK("[ 夜深了! 注意早点休息，明天也会很棒呢~ ]", 18, 154, 302, TFT_WHITE, C_BG_PAGE);
    }
  }
}

static int lastPomoSec = -1;
static int lastPomoRunState = -1;
static int lastPomoCompleted = -1;

void drawFocusPage(bool fullRedraw = true) {
  if (fullRedraw) {
    display.fillScreen(C_BG_PAGE);

    // 顶部状态栏
    drawTextCJK("番茄钟专注", 16, 8, 120, C_POMO_RED, C_BG_PAGE);
    display.drawFastHLine(14, 32, 292, C_DIVIDER);

    // 底部操作栏提示
    display.drawFastHLine(14, 198, 292, C_DIVIDER);
    drawTextCJK("扣下桌面即专注 | 摇杆启停 | C 键重置", 24, 212, 320, 0x7BEF, C_BG_PAGE);

    lastPomoSec = -1;
    lastPomoRunState = -1;
    lastPomoCompleted = -1;
  }

  // 动态更新右上角"今日专注: N 次"计数(番茄钟倒计时归零完成时实时刷新!)
  if (pomoCompletedTotal != lastPomoCompleted) {
    lastPomoCompleted = pomoCompletedTotal;
    display.fillRect(190, 8, 120, 20, C_BG_PAGE);
    char totalBuf[32];
    snprintf(totalBuf, sizeof(totalBuf), "今日专注: %d 次", pomoCompletedTotal);
    drawTextCJK(totalBuf, 195, 8, 312, C_TIME_GOLD, C_BG_PAGE);
  }

  // 剩余秒数
  const uint32_t rem = pomoRemainingSec;
  if ((int)rem != lastPomoSec || fullRedraw) {
    lastPomoSec = rem;
    const int minPart = rem / 60;
    const int secPart = rem % 60;

    // 75 像素巨大倒计时数字 (MM:SS 直写，杜绝全框 fillRect 闪烁!)
    char pomoStr[16];
    snprintf(pomoStr, sizeof(pomoStr), "%02d:%02d", minPart, secPart);

    uint16_t pomoColor = pomoRunning ? C_POMO_RED : (rem == 0 ? C_POMO_DONE : TFT_LIGHTGREY);
    display.setTextColor(pomoColor, C_BG_PAGE);
    display.drawString(pomoStr, 35, 46, 8);  // 75px 超大字号居中倒计时!

    // 专注进度条 (4 像素平滑推进)
    const int barW = 288;
    const int progressW = (int)((POMO_DURATION_SEC - rem) * barW / POMO_DURATION_SEC);
    if (progressW > 0) display.fillRect(16, 132, progressW, 4, C_POMO_RED);
    if (progressW < barW) display.fillRect(16 + progressW, 132, barW - progressW, 4, C_DIVIDER);
  }

  // 状态标识文字: 仅在运行状态改变时刷新 (避免每秒擦除闪烁)
  const int curRunState = pomoRunning ? 1 : (rem == 0 ? 2 : 0);
  if (curRunState != lastPomoRunState || fullRedraw) {
    lastPomoRunState = curRunState;
    display.fillRect(16, 150, 288, 24, C_BG_PAGE);
    if (pomoRunning) {
      drawTextCJK("[ 专注进行中: 屏幕朝下扣放进入全黑防打扰 ]", 16, 154, 304, C_POMO_RED, C_BG_PAGE);
    } else if (rem == 0) {
      drawTextCJK("[ 太棒啦! 25分钟专注达成, 休息5分钟 ]", 26, 154, 296, C_POMO_DONE, C_BG_PAGE);
    } else if (rem < POMO_DURATION_SEC) {
      drawTextCJK("[ 已暂停: 摇杆下压继续 / 扣下继续 ]", 34, 154, 296, C_TIME_GOLD, C_BG_PAGE);
    } else {
      drawTextCJK("[ 准备就绪: 摇杆下压开始 / 扣在桌面开启 ]", 24, 154, 296, TFT_WHITE, C_BG_PAGE);
    }
  }
}
