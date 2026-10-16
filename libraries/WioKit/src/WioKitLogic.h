// ---- L0 纯逻辑(无 Arduino 依赖,PC/板上均可测):HTTP 响应判定、chunked 解码、
// ---- 情绪标签解析、光线/动作探测器 ----
// 唯一有自动化测试(板上自检 tests/pet_selftest)的层;上层 WioKitNet/WioKitSense 复用。
#pragma once
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

enum HttpState { HTTP_NEED_MORE = 0, HTTP_DONE = 1, HTTP_NO_LENGTH = 2 };

// 在 [h, h+n) 中不区分大小写地查找 needle,返回偏移;找不到返回 -1
inline long ciFind(const char* h, size_t n, const char* needle) {
  const size_t m = strlen(needle);
  if (m == 0 || m > n) return -1;
  for (size_t i = 0; i + m <= n; i++) {
    size_t j = 0;
    while (j < m && tolower((unsigned char)h[i + j]) == tolower((unsigned char)needle[j])) j++;
    if (j == m) return (long)i;
  }
  return -1;
}

inline long httpHeaderEnd(const char* buf, size_t n) { return ciFind(buf, n, "\r\n\r\n"); }

// 提取 HTTP 响应状态码(如 200 / 401 / 402 / 429 / 500);头部不完整或格式非法返回 0
inline int httpStatusCode(const char* buf, size_t n) {
  if (!buf || n < 12) return 0;
  if (memcmp(buf, "HTTP/1.", 7) != 0) return 0;
  const char* p = buf + 8;
  while (p < buf + n && *p == ' ') p++;
  if (p + 3 > buf + n) return 0;
  if (p[0] < '1' || p[0] > '5' || p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9') return 0;
  return (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
}

inline bool httpIsChunked(const char* buf, size_t headerLen) {
  const long te = ciFind(buf, headerLen, "\ntransfer-encoding:");
  if (te < 0) return false;
  const size_t start = (size_t)te + 1;
  const long lineEnd = ciFind(buf + start, headerLen - start, "\r\n");
  const size_t searchLen = (lineEnd >= 0) ? (size_t)lineEnd : (headerLen - start);
  return ciFind(buf + start, searchLen, "chunked") >= 0;
}

// 逐块走一遍 chunked 正文:遇到 0 长度块及其后的 CRLF 才算完整(数据里出现 "0\r\n\r\n" 不会误判)。
// out 非空时把数据依次写入 out(允许 out == body 原地解码,写指针永远不超过读指针)。
inline bool chunkedWalk(const char* body, size_t n, char* out, size_t* decodedLen) {
  size_t pos = 0, w = 0;
  while (true) {
    const long le = ciFind(body + pos, n - pos, "\r\n");
    if (le < 0) return false;
    size_t sz = 0;
    bool any = false;
    for (size_t k = pos; k < pos + (size_t)le; k++) {  // 十六进制长度,忽略 ";扩展"
      const char c = body[k];
      int v;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else break;
      if (sz > (SIZE_MAX >> 4)) return false;  // 防止畸形超大块号整数溢出
      sz = sz * 16 + (size_t)v;
      any = true;
    }
    if (!any) return false;
    pos += (size_t)le + 2;
    if (sz == 0) {
      if (n - pos < 2) return false;  // 末尾 CRLF 未到
      if (decodedLen) *decodedLen = w;
      return true;
    }
    if (sz > n - pos || n - pos - sz < 2) return false;
    if (out) memmove(out + w, body + pos, sz);
    w += sz;
    pos += sz + 2;
  }
}

// 判断已收到的响应是否完整:按 Content-Length 或 chunked 终止块;两者都没有返回 HTTP_NO_LENGTH(调用方兜底)
inline int httpResponseState(const char* buf, size_t n) {
  const long he = httpHeaderEnd(buf, n);
  if (he < 0) return HTTP_NEED_MORE;
  // 若头部是 HTTP/1.1 100 Continue，则真正响应头还在后面，继续等待
  if (he >= 12 && memcmp(buf, "HTTP/1.1 100", 12) == 0) {
    const size_t restStart = (size_t)he + 4;
    return httpResponseState(buf + restStart, n - restStart);
  }
  const size_t bodyStart = (size_t)he + 4;
  const size_t bodyLen = n - bodyStart;
  const long cl = ciFind(buf, (size_t)he, "\ncontent-length:");
  if (cl >= 0) {
    const long want = strtol(buf + cl + 16, NULL, 10);  // 数字后面紧跟 \r,strtol 会停下
    if (want < 0) return HTTP_NO_LENGTH;
    return (long)bodyLen >= want ? HTTP_DONE : HTTP_NEED_MORE;
  }
  if (httpIsChunked(buf, (size_t)he)) {
    return chunkedWalk(buf + bodyStart, bodyLen, NULL, NULL) ? HTTP_DONE : HTTP_NEED_MORE;
  }
  return HTTP_NO_LENGTH;
}

enum Emotion { EMO_HAPPY = 0, EMO_EXCITED, EMO_SURPRISED, EMO_SHY, EMO_CONFUSED, EMO_SAD };

// 解析回答开头的情绪标签:支持 [情绪] 与全角【情绪】,允许前导空白,剥掉标签及其后空白。
// 无标签 / 不在白名单 / 缺右括号 → EMO_HAPPY,*textStart = 0(原文完整显示)
inline int parseEmotionTag(const char* s, size_t n, size_t* textStart) {
  *textStart = 0;
  size_t b = 0;
  while (b < n && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
  size_t open = 0;
  if (b < n && s[b] == '[') open = 1;
  else if (b + 3 <= n && memcmp(s + b, "\xE3\x80\x90", 3) == 0) open = 3;  // 【
  if (!open) return EMO_HAPPY;
  const size_t tagStart = b + open;
  size_t close = 0, closeLen = 0;
  for (size_t i = tagStart; i < n && i <= tagStart + 16; i++) {
    if (s[i] == ']') { close = i; closeLen = 1; break; }
    if (i + 3 <= n && memcmp(s + i, "\xE3\x80\x91", 3) == 0) { close = i; closeLen = 3; break; }  // 】
  }
  if (!closeLen) return EMO_HAPPY;
  static const char* const NAMES[6] = {"开心", "兴奋", "惊讶", "害羞", "疑惑", "难过"};
  const size_t tagLen = close - tagStart;
  for (int e = 0; e < 6; e++) {
    if (strlen(NAMES[e]) == tagLen && memcmp(s + tagStart, NAMES[e], tagLen) == 0) {
      size_t p = close + closeLen;
      while (p < n && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) p++;
      *textStart = p;
      return e;
    }
  }
  return EMO_HAPPY;
}

// 光线判定:双阈值迟滞 + 持续时间。暗于 darkTh 持续 darkHoldMs → 1(变暗);亮于等于 lightTh 持续 lightHoldMs → 2(变亮)
struct LightDetector {
  int darkTh, lightTh;
  uint32_t darkHoldMs, lightHoldMs;
  bool isDark, pending;
  uint32_t since;
  LightDetector(int darkTh_, int lightTh_, uint32_t darkHoldMs_, uint32_t lightHoldMs_)
      : darkTh(darkTh_), lightTh(lightTh_), darkHoldMs(darkHoldMs_), lightHoldMs(lightHoldMs_),
        isDark(false), pending(false), since(0) {}
  int update(int v, uint32_t now) {
    const bool wantDark = isDark ? (v < lightTh) : (v < darkTh);
    if (wantDark == isDark) {  // 回到当前状态:取消待定(防闪烁)
      pending = false;
      return 0;
    }
    if (!pending) {
      pending = true;
      since = now;
      return 0;
    }
    if (now - since >= (wantDark ? darkHoldMs : lightHoldMs)) {
      isDark = wantDark;
      pending = false;
      return isDark ? 1 : 2;
    }
    return 0;
  }
};

enum MotionEvent { MOTION_NONE = 0, MOTION_SHAKE = 1, MOTION_PICKUP = 2 };

// 动作判定(输入三轴加速度,单位 mg):
// 摇晃:|a| 偏离 1g 超过 shakeMg 记一次,1 秒内 3 次触发;触发后 8 秒冷却(晕 3s + 冷却 5s)
// 拿起:先静止 stillMs,之后三轴变化之和超过 pickupMg
struct MotionDetector {
  int shakeMg, pickupMg;
  uint32_t stillMs;
  int hits;
  uint32_t hitWindowStart, cooldownUntil;
  int refX, refY, refZ;
  uint32_t stillSince;
  bool still;
  MotionDetector(int shakeMg_, int pickupMg_, uint32_t stillMs_)
      : shakeMg(shakeMg_), pickupMg(pickupMg_), stillMs(stillMs_), hits(0), hitWindowStart(0),
        cooldownUntil(0), refX(0), refY(0), refZ(1000), stillSince(0), still(false) {}
  int update(int x, int y, int z, uint32_t now) {
    const long mag2 = (long)x * x + (long)y * y + (long)z * z;
    const long hiMag = 1000L + shakeMg;
    const long loMag = (1000L - shakeMg) > 0 ? (1000L - shakeMg) : 0;
    if (mag2 > hiMag * hiMag || mag2 < loMag * loMag) {
      if (hits == 0 || now - hitWindowStart > 1000) {
        hits = 0;
        hitWindowStart = now;
      }
      hits++;
      still = false;
      stillSince = now;
      refX = x; refY = y; refZ = z;
      if (hits >= 3 && (cooldownUntil == 0 || (int32_t)(now - cooldownUntil) >= 0)) {
        hits = 0;
        cooldownUntil = now + 8000;
        return MOTION_SHAKE;
      }
      return MOTION_NONE;
    }
    const long d = labs((long)x - refX) + labs((long)y - refY) + labs((long)z - refZ);
    if (d < pickupMg / 3) {  // 基本没动:累计静止
      if (!still && now - stillSince >= stillMs) still = true;
      return MOTION_NONE;
    }
    const bool fire = still && d > pickupMg;
    still = false;
    stillSince = now;
    refX = x; refY = y; refZ = z;
    return fire ? MOTION_PICKUP : MOTION_NONE;
  }
};
