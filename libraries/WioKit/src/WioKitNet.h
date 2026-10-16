// ---- L1 网络:WiFi 连接 + HTTP 收发 + base64 流式上传 + WAV 头(header-only 模板) ----
// 解耦点②:库的等待循环统一调用 wioNetYield();应用经 wioNetSetYield() 注册回调
// (如桌宠动画帧),不注册则等待期间什么都不做。函数指针而非虚函数:零开销、无堆分配。
// 判停语义:按 Content-Length / chunked 终止块收满立即返回;无长度信息退回
// "无新数据超时"兜底(RTL8720 栈在 Connection:close 后 connected() 不会及时变 false)。
#pragma once
#include <Arduino.h>
#include <rpcWiFi.h>
#include "WioKitLogic.h"

// 连接参数与回调经内联函数的函数级 static 保存:内置定长拷贝缓冲,杜绝外部临时 String 悬空指针
struct WioNetState {
  char ssid[36];
  char pass[68];
  void (*yieldFn)();
  bool (*abortFn)();
  bool aborted;
};
inline WioNetState& wioNetState() {
  static WioNetState s = {{0}, {0}, nullptr, nullptr, false};
  return s;
}

// 应用在 setup() 调用:注入密钥(内部安全拷贝,不怕调用方传临时变量)
inline void wioNetBegin(const char* ssid, const char* pass) {
  memset(wioNetState().ssid, 0, sizeof(wioNetState().ssid));
  memset(wioNetState().pass, 0, sizeof(wioNetState().pass));
  if (ssid) strncpy(wioNetState().ssid, ssid, sizeof(wioNetState().ssid) - 1);
  if (pass) strncpy(wioNetState().pass, pass, sizeof(wioNetState().pass) - 1);
}

// 注册等待期回调(如 petAnimTick);不注册则等待期间什么都不做
inline void wioNetSetYield(void (*fn)()) { wioNetState().yieldFn = fn; }

// 注册用户取消/中断检测回调(如按 C 键立即取消网络等待,避免卡死在"让我想想")
inline void wioNetSetAbort(bool (*fn)()) { wioNetState().abortFn = fn; }
inline void wioNetResetAbort() { wioNetState().aborted = false; }
inline bool wioNetAborted() { return wioNetState().aborted; }

inline void wioNetYield() {
  if (wioNetState().yieldFn) wioNetState().yieldFn();
  if (!wioNetState().aborted && wioNetState().abortFn && wioNetState().abortFn()) {
    wioNetState().aborted = true;
  }
}

// 连 WiFi(默认试 2 次,每次 ≤10s);已连上立即返回 true;支持中途按取消键立即中断
inline bool wioNetConnected(int attempts = 2, uint32_t perAttemptMs = 10000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!wioNetState().ssid[0]) return false;
  Serial.print("V: wifi begin ssid=\"");
  Serial.print(wioNetState().ssid);
  Serial.println("\"");
  for (int attempt = 1; attempt <= attempts; attempt++) {
    if (wioNetAborted()) return false;
    WiFi.begin(wioNetState().ssid, wioNetState().pass);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (wioNetAborted()) {
        WiFi.disconnect();
        return false;
      }
      if (millis() - start > perAttemptMs) break;
      for (int k = 0; k < 20; k++) {  // 等 200ms,期间应用回调与取消检测照常
        wioNetYield();
        if (wioNetAborted()) {
          WiFi.disconnect();
          return false;
        }
        delay(10);
      }
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("V: wifi OK ip=");
      Serial.println(WiFi.localIP());
      return true;
    }
    WiFi.disconnect();
    for (int k = 0; k < 50; k++) {
      wioNetYield();
      if (wioNetAborted()) return false;
      delay(10);
    }
  }
  return false;
}

// 可靠的全量发送辅助函数:处理 RTL8720DN/lwIP 发送缓冲区满时的部分写(partial write)或返回 0,
// 确保声明的 Content-Length 字节数 100% 完整发出,彻底根治"丢字节导致百度服务器死等 60 秒"!
template <typename ClientT>
bool wioWriteAll(ClientT& client, const uint8_t* data, size_t len, uint32_t timeoutMs = 8000) {
  size_t sent = 0;
  unsigned long lastProgress = millis();
  while (sent < len) {
    if (wioNetAborted()) return false;
    const size_t chunk = (len - sent > 1024) ? 1024 : (len - sent);
    const int n = (int)client.write(data + sent, chunk);
    if (n > 0) {
      sent += (size_t)n;
      lastProgress = millis();
      wioNetYield();
    } else {
      if (!client.connected()) return false;
      if (millis() - lastProgress > timeoutMs) return false;
      wioNetYield();
      delay(5);
    }
  }
  return true;
}

template <typename ClientT>
bool wioWriteStr(ClientT& client, const char* s, uint32_t timeoutMs = 8000) {
  if (!s || !s[0]) return true;
  return wioWriteAll(client, (const uint8_t*)s, strlen(s), timeoutMs);
}

static const char WIO_B64TAB[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
struct B64State { uint8_t carry[2]; uint8_t ncarry; };

// 流式 base64 编码:内部 1KB 批量写出,经 wioWriteAll 保证零丢包且支持取消中断。
// 用法:B64Stream<WiFiClient> s(client); s.write(wavHead, 44); s.write(pcm, n); if (!s.flush()) ...
template <typename ClientT>
struct B64Stream {
  ClientT* client;
  B64State st;
  char out[1024];
  size_t outLen;
  bool ok;

  explicit B64Stream(ClientT& c) : client(&c), st{{0, 0}, 0}, outLen(0), ok(true) {}

  void push(const char* g) {
    if (!ok) return;
    if (outLen + 4 > sizeof(out)) {
      if (!wioWriteAll(*client, (const uint8_t*)out, outLen)) {
        ok = false;
        outLen = 0;
        return;
      }
      outLen = 0;
    }
    memcpy(out + outLen, g, 4);
    outLen += 4;
  }

  void write(const uint8_t* data, size_t len) {
    if (!ok) return;
    uint8_t grp[3];
    size_t i = 0;
    while (ok) {
      uint8_t n = st.ncarry;
      if (n > 0) { grp[0] = st.carry[0]; if (n > 1) grp[1] = st.carry[1]; }
      while (n < 3 && i < len) grp[n++] = data[i++];
      if (n == 0) break;
      if (n == 3) {
        const uint32_t v = ((uint32_t)grp[0] << 16) | ((uint32_t)grp[1] << 8) | grp[2];
        const char g[4] = {WIO_B64TAB[(v >> 18) & 63], WIO_B64TAB[(v >> 12) & 63], WIO_B64TAB[(v >> 6) & 63], WIO_B64TAB[v & 63]};
        push(g);
        st.ncarry = 0;
        if (i >= len) break;
      } else {
        st.carry[0] = grp[0]; st.carry[1] = grp[1]; st.ncarry = n;
        break;
      }
    }
  }

  bool flush() {
    if (!ok) return false;
    if (st.ncarry) {
      uint32_t v = (uint32_t)st.carry[0] << 16;
      if (st.ncarry > 1) v |= (uint32_t)st.carry[1] << 8;
      const char g[4] = {WIO_B64TAB[(v >> 18) & 63], WIO_B64TAB[(v >> 12) & 63], st.ncarry > 1 ? WIO_B64TAB[(v >> 6) & 63] : '=', '='};
      push(g);
      st.ncarry = 0;
    }
    if (ok && outLen) {
      if (!wioWriteAll(*client, (const uint8_t*)out, outLen)) {
        ok = false;
      }
      outLen = 0;
    }
    return ok;
  }
};

// 16bit 单声道 PCM 的 WAV 头(44 字节)
inline void wioMakeWavHeader(uint8_t h[44], uint32_t dataLen, uint32_t sampleRate) {
  memcpy(h, "RIFF", 4);
  uint32_t v = 36 + dataLen; memcpy(h + 4, &v, 4);
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  v = 16; memcpy(h + 16, &v, 4);
  h[20] = 1; h[21] = 0;
  h[22] = 1; h[23] = 0;
  v = sampleRate; memcpy(h + 24, &v, 4);
  v = sampleRate * 2; memcpy(h + 28, &v, 4);
  h[32] = 2; h[33] = 0;
  h[34] = 16; h[35] = 0;
  memcpy(h + 36, "data", 4);
  v = dataLen; memcpy(h + 40, &v, 4);
}

// 预分配 HTTP 响应缓冲(性能铁律:收响应用预分配缓冲,库内不 malloc)。
// 收满后正文被原地去头/chunked 解码,放在 buf 起始处,以 '\0' 结尾;status 保存 HTTP 状态码。
struct HttpBuf {
  char* buf;
  size_t cap;
  size_t len;
  int status;
  void bind(char* storage, size_t capacity) { buf = storage; cap = capacity; len = 0; status = 0; }
  const char* body() const { return buf; }
};

// 跨模块共享的 6KB HTTP 响应缓冲区(Token / ASR / LLM 严格串行调用,共享同一块静态内存:
// 比原本 2K+2K+4K=8K 节省 2KB RAM,同时把单次响应容量提升到 6144B,彻底消除百度 OAuth 长 scope 截断!)
inline void wioBindSharedHttpBuf(HttpBuf& out) {
  static char sharedStorage[6144];
  out.bind(sharedStorage, sizeof(sharedStorage));
}

// 读一个 HTTP 响应:按 Content-Length / chunked 终止块判断收完立即返回;
// 无长度信息或异常时退回"无新数据超时"兜底。成功(200 且头完整且正文未截断)返回 true,
// 正文解码到 out(原地去头部:写指针永远不超过读指针)。
template <typename ClientT>
bool wioReadHttp(ClientT& client, HttpBuf& out, uint32_t maxWaitMs, const char* label, bool keepOpen = false) {
  out.len = 0;
  out.status = 0;
  if (!out.buf || out.cap < 16) return false;
  out.buf[0] = '\0';
  const unsigned long start = millis();
  unsigned long lastGrowth = start;
  int hs = HTTP_NEED_MORE;
  while ((client.connected() || client.available()) && out.len < out.cap - 1) {
    if (wioNetAborted()) {
      client.stop();
      return false;
    }
    bool got = false;
    while (client.available() && out.len < out.cap - 1) {
      const int c = client.read();
      if (c < 0) break;
      out.buf[out.len++] = (char)c;
      got = true;
    }
    out.buf[out.len] = '\0';
    if (got) {
      lastGrowth = millis();
      // 若收到前置 HTTP/1.1 100 Continue 响应头，原地剥离以节省缓冲并简化后续解析
      const long firstHe = httpHeaderEnd(out.buf, out.len);
      if (firstHe >= 12 && memcmp(out.buf, "HTTP/1.1 100", 12) == 0) {
        const size_t stripLen = (size_t)firstHe + 4;
        const size_t remain = out.len - stripLen;
        if (remain > 0) memmove(out.buf, out.buf + stripLen, remain);
        out.len = remain;
        out.buf[out.len] = '\0';
      }
      hs = httpResponseState(out.buf, out.len);
      if (hs == HTTP_DONE) break;  // 按协议收满,立即收工
    }
    // 已收到部分数据后停止增长超过 2.5s = 响应流结束或断流兜底
    if (out.len > 0 && millis() - lastGrowth > 2500) break;
    if (millis() - start > maxWaitMs) break;
    wioNetYield();
    delay(5);
  }
  if (!keepOpen || hs != HTTP_DONE) client.stop();
  const long headerEnd = httpHeaderEnd(out.buf, out.len);
  out.status = httpStatusCode(out.buf, out.len);
  if (out.len && headerEnd < 0) {
    Serial.print("V: http raw=");
    Serial.write((const uint8_t*)out.buf, out.len < 170 ? out.len : 170);  // 不完整响应:原样打印排查
    Serial.println();
  }
  Serial.print("V: http[");
  Serial.print(label);
  Serial.print("] status=");
  Serial.print(out.status);
  Serial.print(" wait=");
  Serial.print(millis() - start);
  Serial.print("ms len=");
  Serial.print(out.len);
  Serial.print(" done=");
  Serial.println(hs);
  if (headerEnd < 0) return false;
  if (out.status != 200) return false;
  // 若声明了 Content-Length 或 chunked 但连接提前中断导致 hs 仍为 HTTP_NEED_MORE，说明正文被截断
  if (hs == HTTP_NEED_MORE) return false;
  const size_t bodyStart = (size_t)headerEnd + 4;
  size_t bodyLen = out.len - bodyStart;
  const long cl = ciFind(out.buf, (size_t)headerEnd, "\ncontent-length:");
  if (cl >= 0) {
    const long want = strtol(out.buf + cl + 16, NULL, 10);
    if (want >= 0 && bodyLen > (size_t)want) bodyLen = (size_t)want;
  }
  if (httpIsChunked(out.buf, (size_t)headerEnd)) {
    size_t decoded = 0;
    if (!bodyLen || !chunkedWalk(out.buf + bodyStart, bodyLen, out.buf, &decoded)) return false;
    out.len = decoded;
  } else {
    if (bodyStart) memmove(out.buf, out.buf + bodyStart, bodyLen);
    out.len = bodyLen;
  }
  out.buf[out.len] = '\0';
  return true;
}
