#include "WioKitAsrBaidu.h"
#include <WioKitMic.h>
#include <WioKitNet.h>
#include <WioKitTiming.h>
#include <rpcWiFi.h>
#include <rpcWiFiClientSecure.h>
#include <ArduinoJson.h>

static String asrApiKey = "";
static String asrSecretKey = "";
static String baiduToken = "";

void wioAsrBaiduBegin(const char* apiKey, const char* secretKey) {
  const String newKey = apiKey ? apiKey : "";
  const String newSec = secretKey ? secretKey : "";
  if (asrApiKey != newKey || asrSecretKey != newSec) {
    baiduToken = "";  // 密钥内容变更时清空旧缓存令牌(避免同地址覆盖时不更新)
  }
  asrApiKey = newKey;
  asrSecretKey = newSec;
}

void wioAsrBaiduDebugToken(const char* token) { baiduToken = token ? token : ""; }

static bool fetchBaiduToken() {
  if (wioNetAborted()) return false;
  if (baiduToken.length() > 10) return true;
  if (asrApiKey.length() < 4 || asrSecretKey.length() < 4) return false;

  WiFiClientSecure client;
  if (!client.connect("openapi.baidu.com", 443, 10000)) {
    client.stop();
    return false;
  }
  if (wioNetAborted()) {
    client.stop();
    return false;
  }
  const String req = String("GET /oauth/2.0/token?grant_type=client_credentials&client_id=") +
                     asrApiKey + "&client_secret=" + asrSecretKey +
                     " HTTP/1.1\r\nHost: openapi.baidu.com\r\nConnection: close\r\n\r\n";
  if (!wioWriteStr(client, req.c_str(), 6000)) {
    client.stop();
    return false;
  }
  HttpBuf http;
  wioBindSharedHttpBuf(http);
  if (!wioReadHttp(client, http, 12000, "token")) return false;

  JsonDocument filter;
  filter["access_token"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, http.body(), http.len, DeserializationOption::Filter(filter))) return false;
  baiduToken = String(doc["access_token"] | "");
  return baiduToken.length() > 10;
}

bool wioAsrBaiduWarmup() { return fetchBaiduToken(); }

// 上传一次识别请求。返回百度 err_no;传输层失败返回 -1(note 填原因);响应缺 err_no 返回 -2
static int baiduAsrOnce(const int16_t* pcm, uint32_t n, String& transcript, String& note) {
  if (wioNetAborted()) {
    note = "cancelled";
    return -1;
  }
  if (!pcm || n == 0) {
    note = "asr no speech";
    return -1;
  }
  // ASR 走 HTTP 明文(80 端口):板载 TLS 写入只有 ~5KB/s,上传大音频会被服务器掐线;
  // 明文 TCP 快数倍。令牌仍走 HTTPS,音频本身在家庭网络内明文传输(玩具可接受)。
  WiFiClient client;
  if (!client.connect("vop.baidu.com", 80, 10000)) {
    client.stop();
    note = wioNetAborted() ? "cancelled" : "asr connect failed";
    return -1;
  }
  if (wioNetAborted()) {
    client.stop();
    note = "cancelled";
    return -1;
  }
  const uint32_t wavLen = 44 + 2 * n;
  const uint32_t b64Len = ((wavLen + 2) / 3) * 4;
  const String head = String("{\"format\":\"wav\",\"rate\":16000,\"dev_pid\":1537,") +
                      "\"channel\":1,\"cuid\":\"wioterminal001\",\"token\":\"" + baiduToken +
                      "\",\"len\":" + String(wavLen) + ",\"speech\":\"";
  const char* tail = "\"}";
  String req = "POST /server_api HTTP/1.1\r\n"
               "Host: vop.baidu.com\r\n"
               "Content-Type: application/json\r\n"
               "Connection: close\r\nContent-Length: ";
  req += String(head.length() + b64Len + strlen(tail));
  req += "\r\n\r\n";

  const uint32_t upT = millis();
  if (!wioWriteStr(client, req.c_str(), 6000) || !wioWriteStr(client, head.c_str(), 6000)) {
    client.stop();
    note = wioNetAborted() ? "cancelled" : "asr upload failed";
    return -1;
  }
  B64Stream<WiFiClient> b64(client);
  // WAV 头 + int16 小端 PCM,必须经 wioWriteAll 完整发送并 flush,确保实发字节数与 Content-Length 100% 一致
  uint8_t wavHead[44];
  wioMakeWavHeader(wavHead, 2 * n, WIO_MIC_SAMPLE_RATE);
  b64.write(wavHead, 44);
  b64.write((const uint8_t*)pcm, 2 * n);
  if (!b64.flush() || !wioWriteStr(client, tail, 6000)) {
    client.stop();
    note = wioNetAborted() ? "cancelled" : "asr upload failed";
    return -1;
  }
  wioTiming().upload = millis() - upT;
  Serial.print("V: wav uploaded ms=");
  Serial.println(wioTiming().upload);

  const uint32_t tAsr = millis();
  HttpBuf http;
  wioBindSharedHttpBuf(http);
  const bool ok = wioReadHttp(client, http, 15000, "asr");
  wioTiming().asr = millis() - tAsr;
  if (!ok) {
    note = wioNetAborted() ? "cancelled" : "asr http error";
    return -1;
  }
  JsonDocument adoc;
  if (deserializeJson(adoc, http.body(), http.len)) {
    note = "asr bad json";
    return -1;
  }
  const int errNo = adoc["err_no"] | -2;
  transcript = String(adoc["result"][0] | "");
  Serial.print("V: baidu err_no=");
  Serial.print(errNo);
  Serial.print(" transcript=");
  Serial.println(transcript);
  return errNo;
}

bool wioAsrBaidu(const int16_t* pcm, uint32_t n, String& transcript, String& note) {
  transcript = "";
  note = "";
  if (wioNetAborted()) {
    note = "cancelled";
    return false;
  }
  if (asrApiKey.length() < 4 || asrSecretKey.length() < 4) {
    note = "baidu key missing";
    return false;
  }
  const uint32_t tNet = millis();
  if (!wioNetConnected(2, 8000)) {
    note = wioNetAborted() ? "cancelled" : "wifi failed";
    return false;
  }
  if (!fetchBaiduToken()) {
    note = wioNetAborted() ? "cancelled" : "baidu token failed";
    return false;
  }
  wioTiming().net = millis() - tNet;

  int errNo = baiduAsrOnce(pcm, n, transcript, note);
  if (errNo == 110 || errNo == 111 || errNo == 3302) {  // token 失效/鉴权失败:重取一次再试
    Serial.print("V: token rejected err_no=");
    Serial.println(errNo);
    baiduToken = "";
    if (!fetchBaiduToken()) {
      note = wioNetAborted() ? "cancelled" : "baidu token failed";
      return false;
    }
    errNo = baiduAsrOnce(pcm, n, transcript, note);
  }
  if (errNo == -1) return false;
  if (errNo != 0 || !transcript.length()) {
    note = "asr rejected";
    return false;
  }
  return true;
}
