#include "WioKitMic.h"
#include <mic.h>
#include "processing/filters.h"

static int16_t recPcm[WIO_MIC_MAX_SAMPLES];

static volatile uint32_t micIdx = 0;
static volatile uint8_t micRecording = 0;
static volatile bool micDone = false;
static FilterBuHp micFilter;

static int micSilenceAvg = 400;
static uint32_t micTrailingMs = 900;
static uint32_t micNoSpeechMs = 8000;

// 轮询状态(文件内静态:上一轮的 VAD 记忆)
static bool speechSeen = false;
static uint32_t lastVoice = 0;
static uint32_t recStart = 0;
static uint32_t lastPoll = 0;
static uint32_t voiceEndIdx = 0;  // 最近一次有声帧结束时的样本位置

static mic_config_t mic_config{
  .channel_cnt = 1,
  .sampling_rate = WIO_MIC_SAMPLE_RATE,
  .buf_size = 320,
  .debug_pin = 1,
};
static DMA_ADC_Class Mic(&mic_config);

static void audio_rec_callback(uint16_t* buf, uint32_t buf_len) {
  if (!micRecording) return;
  for (uint32_t i = 0; i < buf_len; i++) {
    // 12bit 无偏 ADC(中点 1024)转 16bit PCM,过高通去直流
    const int16_t sample = micFilter.step((int16_t)(buf[i] - 1024) * 16);
    if (micIdx < WIO_MIC_MAX_SAMPLES) {
      recPcm[micIdx++] = sample;
    }
    if (micIdx >= WIO_MIC_MAX_SAMPLES) {
      micRecording = 0;
      micDone = true;
      break;
    }
  }
}

bool wioMicBegin() {
  Mic.set_callback(audio_rec_callback);
  if (!Mic.begin()) {
    Serial.println("V: mic init FAIL");
    return false;
  }
  Serial.println("V: mic init ok");
  return true;
}

void wioMicConfig(int silenceAvg, uint32_t trailingMs, uint32_t noSpeechMs) {
  micSilenceAvg = silenceAvg;
  micTrailingMs = trailingMs;
  micNoSpeechMs = noSpeechMs;
}

void wioRecStart() {
  micRecording = 0;
  micFilter = FilterBuHp();  // 重置 IIR 高通滤波器内部状态，消除上次录音残留的直流阶跃响应
  micIdx = 0;
  micDone = false;
  speechSeen = false;
  voiceEndIdx = 0;
  const uint32_t now = millis();
  recStart = now;
  lastVoice = now;
  lastPoll = 0;
  micRecording = 1;
}

uint32_t wioRecStop() {
  micRecording = 0;
  micDone = false;
  const uint32_t n = micIdx;
  return (n <= WIO_MIC_MAX_SAMPLES) ? n : WIO_MIC_MAX_SAMPLES;
}

bool wioRecHadSpeech() {
  return speechSeen;
}

// 轮询:返回 0=还在录 1=录完(结束) 2=超时没说话 3=缓冲录满(达到 3 秒上限强行截断)
int wioRecPoll(uint32_t& samples, uint32_t& speechHint) {
  const uint32_t now = millis();
  uint32_t curIdx = micIdx;
  samples = (curIdx <= WIO_MIC_MAX_SAMPLES) ? curIdx : WIO_MIC_MAX_SAMPLES;

  if (!micDone && (now - lastPoll < 80)) return 0;
  lastPoll = now;

  const int window = (samples < 1600) ? (int)samples : 1600;
  uint32_t sum = 0;
  for (int i = 0; i < window; i++) {
    const int32_t v = recPcm[samples - 1 - i];
    sum += (uint32_t)((v < 0) ? -v : v);
  }
  const uint32_t avg = window ? (sum / (uint32_t)window) : 0;
  const bool voiced = (avg > (uint32_t)micSilenceAvg);

  if (voiced) {
    speechHint += (uint32_t)window;
    speechSeen = true;
    lastVoice = now;
    voiceEndIdx = samples;
  }

  // 1. 尚未检测到说话时:
  //    - 若已超过没说话超时阈值(默认最大 4.5s 或 micNoSpeechMs)，立即返回 2(超时没说话)
  //    - 平时每当静音累积超过 3200 样本(200ms)即滑动保留最近 1600 样本(100ms 预卷)，
  //      把 96% 的缓冲区(2.9 秒)全部留给开口后的真实语音!
  const uint32_t noSpeechLimit = (micNoSpeechMs < 4500) ? micNoSpeechMs : 4500;
  if (!speechSeen) {
    if (now - recStart > noSpeechLimit) {
      micRecording = 0;
      micDone = false;
      samples = 0;
      return 2;
    }
    if (samples > 3200) {
      micRecording = 0;
      const uint32_t cur = micIdx;
      const uint32_t keep = 1600;
      if (cur > keep && cur <= WIO_MIC_MAX_SAMPLES) {
        memmove(recPcm, recPcm + (cur - keep), keep * sizeof(int16_t));
        micIdx = keep;
        samples = keep;
      } else {
        micIdx = 0;
        samples = 0;
      }
      micDone = false;
      micRecording = 1;
    }
    return 0;
  }

  // 2. 正常说话后，静音时长超过 trailing 阈值自动截断
  if (now - lastVoice > micTrailingMs) {
    micRecording = 0;
    return 1;
  }

  // 3. 正在等待尾音静音判停期间(!voiced):
  //    若人声结束后已录下超过 3200 样本(200ms)的尾音静音，回退多余的静音采样点，
  //    防止 1.2s 的静音等待期把 48000 样本缓冲区撑爆导致误报"录满3秒(st=3)"!
  const uint32_t maxPostRoll = voiceEndIdx + 3200;
  if (!voiced && maxPostRoll < WIO_MIC_MAX_SAMPLES && samples > maxPostRoll) {
    micRecording = 0;
    micIdx = maxPostRoll;
    samples = maxPostRoll;
    micDone = false;
    micRecording = 1;
    return 0;
  }

  // 4. 硬件 DMA 真正录满 48000 样本的有效语音(达到 3 秒上限)
  if (micDone || samples >= WIO_MIC_MAX_SAMPLES) {
    micRecording = 0;
    samples = WIO_MIC_MAX_SAMPLES;
    // 若最后阶段其实已经静音超过 400ms，说明刚好说完，按正常录完(1)处理而非超长截断(3)
    if (!voiced && (now - lastVoice >= 400)) {
      return 1;
    }
    return 3;
  }

  return 0;
}

const int16_t* wioRecBuffer() { return recPcm; }

void wioRecTrim(uint32_t& lo, uint32_t& hi) {
  const uint32_t origLo = lo;
  const uint32_t origHi = (hi <= WIO_MIC_MAX_SAMPLES) ? hi : WIO_MIC_MAX_SAMPLES;
  const int trimTh = (micSilenceAvg > 250) ? (micSilenceAvg - 80) : 300;
  while (lo < origHi && abs((int)recPcm[lo]) < trimTh) lo++;
  while (hi > lo && abs((int)recPcm[hi - 1]) < trimTh) hi--;
  if (lo >= hi) {
    lo = origLo;
    hi = origLo;
    return;
  }
  // 首尾各保留 100ms(1600 样本)余量，防止吞掉轻辅音与尾音
  lo = (lo > origLo + 1600) ? (lo - 1600) : origLo;
  hi = (hi + 1600 < origHi) ? (hi + 1600) : origHi;
}
