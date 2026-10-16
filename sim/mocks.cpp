// 硬件仿真桩:WioKit 的 Mic/Sense/Config/AsrBaidu/LlmDeepSeek 模块在宿主上的替身。
// 桩保持与真模块相同的 API 和计时记账;麦克风给出"说话 1.5s + 静音"的合成波形,
// 云端返回脚本台词(台词里的情绪标签仍由真实 L0 parseEmotionTag 解析)。
#include "Arduino.h"
#include "sim_internals.h"
#include <WioKitAsrBaidu.h>
#include <WioKitConfig.h>
#include <WioKitLlmDeepSeek.h>
#include <WioKitMic.h>
#include <WioKitSense.h>
#include <WioKitTiming.h>

#include <cstring>
#include <vector>

// ---- 麦克风:开始录音后 1.5s 合成"说话"(1kHz 正弦,幅度 2000),之后静音;
// ---- 2.4s(VAD 截断等待)后返回完成。裁剪逻辑与真模块一致(400 阈值)。
static int16_t g_pcm[48000];
static bool g_recording = false;
static uint32_t g_recStart = 0;
static uint32_t g_samples = 0;
static bool g_hadSpeech = false;

bool wioMicBegin() {
  Serial.println("V: mic init ok (sim)");
  return true;
}
void wioMicConfig(int, uint32_t, uint32_t) {}
void wioRecStart() {
  g_recording = true;
  g_recStart = millis();
  g_samples = 0;
  g_hadSpeech = false;
}
uint32_t wioRecStop() {
  g_recording = false;
  return g_samples;
}
bool wioRecHadSpeech() {
  return g_hadSpeech;
}
int wioRecPoll(uint32_t& samples, uint32_t& hint) {
  if (g_recording) {
    const uint32_t elapsed = millis() - g_recStart;
    const uint32_t target = elapsed > 3000 ? 48000 : elapsed * 16;  // 16kHz
    while (g_samples < target && g_samples < 48000) {
      const bool speaking = elapsed < 1500;
      if (speaking) g_hadSpeech = true;
      // 说话段:1kHz 方波幅度 1800(让真实裁剪逻辑有东西可裁);静音段:小幅噪声
      g_pcm[g_samples++] = speaking ? (int16_t)((g_samples % 16 < 8) ? 1800 : -1800)
                                    : (int16_t)((int)(g_samples % 7) - 3);
    }
    samples = g_samples;
    hint = g_samples;
    if (elapsed > 2400) {  // 说话 1.5s + 截断静音 0.9s
      g_recording = false;
      return 1;
    }
  }
  return 0;
}
const int16_t* wioRecBuffer() { return g_pcm; }
void wioRecTrim(uint32_t& lo, uint32_t& hi) {
  while (lo < hi && abs(g_pcm[lo]) < 400) lo++;
  while (hi > lo + 1600 && abs(g_pcm[hi - 1]) < 400) hi--;
}

// ---- 传感器:回放 sim::senseEventAt 预约的事件(摇晃/拿起等),平时无事件 ----
bool wioSenseBegin() {
  Serial.println("S: imu ok (sim)");
  return true;
}
void wioSenseConfig(int, int, int, int, uint32_t) {}
int wioSensePoll() {
  int ev = 0;
  if (sim::nextSenseEvent(ev)) return ev;
  return SE_NONE;
}
bool wioSenseIsFaceDown() { return false; }

// ---- 配置持久化与配网桩 ----
bool wioConfigInit() { return true; }
bool wioConfigLoad(WioConfig& cfg) {
  std::memset(&cfg, 0, sizeof(cfg));
  std::strncpy(cfg.ssid, "SimWiFi", sizeof(cfg.ssid) - 1);
  std::strncpy(cfg.pass, "simpass", sizeof(cfg.pass) - 1);
  std::strncpy(cfg.baiduApiKey, "sim_baidu_key", sizeof(cfg.baiduApiKey) - 1);
  std::strncpy(cfg.baiduSecret, "sim_baidu_secret", sizeof(cfg.baiduSecret) - 1);
  std::strncpy(cfg.deepseekKey, "sim_deepseek_key", sizeof(cfg.deepseekKey) - 1);
  std::strncpy(cfg.city, "上海", sizeof(cfg.city) - 1);
  return true;
}
bool wioConfigSave(const WioConfig&) { return true; }
bool wioConfigClear() { return true; }
bool wioPortalBegin(const char*, const WioConfig&) { return true; }
int wioPortalPoll(WioConfig&) { return 0; }
void wioPortalEnd() {}

// ---- 百度 ASR 桩:net 阶段记账,返回脚本转写 ----
void wioAsrBaiduBegin(const char*, const char*) {}
bool wioAsrBaiduWarmup() {
  Serial.println("V: warmup ok (sim)");
  return true;
}
void wioAsrBaiduDebugToken(const char*) {}
bool wioAsrBaidu(const int16_t* pcm, uint32_t n, String& transcript, String& note) {
  (void)pcm;
  wioTiming().net = 2;
  wioTiming().upload = 830;
  wioTiming().asr = 460;
  Serial.print("V: trimmed(sim samples)=");
  Serial.println(n);
  transcript = sim::nextTranscript();
  Serial.print("V: baidu err_no=0 transcript=");
  Serial.println(transcript);
  note = "";
  return true;
}

// ---- DeepSeek 桩:返回脚本台词(含情绪标签) ----
void wioLlmDeepSeekBegin(const char*, const char*) {}
bool wioLlmAskWithHistory(const WioLlmMsg*, size_t, const String& question, String& reply, String& note) {
  wioTiming().llmConn = 980;
  wioTiming().llm = 1900;
  reply = sim::nextReply();
  Serial.print("V: reply=");
  Serial.println(reply);
  (void)question;
  note = "";
  return reply.length() > 0;
}
