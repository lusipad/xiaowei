// ---- 语音助理:按住 B 说话 -> 百度 ASR -> DeepSeek LLM -> 屏幕 ----
// 板载模拟麦克风,16kHz 采样;录音存 8bit(省 RAM),上传时流式转 16bit WAV
// (百度 ASR 要求 16bit,走明文 80 端口避开板载 TLS 上传限速)。

const uint32_t VOICE_SAMPLE_RATE = 16000;
const uint32_t VOICE_MAX_SAMPLES = 48000;  // 3 秒
uint8_t voicePcm[VOICE_MAX_SAMPLES];

const char B64TAB[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
struct B64State { uint8_t carry[2]; uint8_t ncarry; };

// Base64 输出缓冲:攒够 1KB 再写底层,循环确保全部字节写出
char b64Out[1024];
size_t b64OutLen = 0;
bool b64WriteOk = true;

template <typename ClientT>
bool consoleWriteAll(ClientT& client, const uint8_t* data, size_t len, uint32_t timeoutMs = 8000) {
  size_t sent = 0;
  unsigned long lastProgress = millis();
  while (sent < len) {
    const size_t chunk = (len - sent > 1024) ? 1024 : (len - sent);
    const int n = (int)client.write(data + sent, chunk);
    if (n > 0) {
      sent += (size_t)n;
      lastProgress = millis();
    } else {
      if (!client.connected()) return false;
      if (millis() - lastProgress > timeoutMs) return false;
      delay(5);
    }
  }
  return true;
}

template <typename ClientT>
void b64OutPush(ClientT& client, const char* g) {
  if (!b64WriteOk) return;
  if (b64OutLen + 4 > sizeof(b64Out)) {
    if (!consoleWriteAll(client, (const uint8_t*)b64Out, b64OutLen)) {
      b64WriteOk = false;
      b64OutLen = 0;
      return;
    }
    b64OutLen = 0;
  }
  memcpy(b64Out + b64OutLen, g, 4);
  b64OutLen += 4;
}

template <typename ClientT>
bool b64OutFlush(ClientT& client) {
  if (b64WriteOk && b64OutLen) {
    if (!consoleWriteAll(client, (const uint8_t*)b64Out, b64OutLen)) {
      b64WriteOk = false;
    }
    b64OutLen = 0;
  }
  return b64WriteOk;
}

template <typename ClientT>
void b64Write(ClientT& client, B64State& st, const uint8_t* data, size_t len) {
  uint8_t grp[3];
  size_t i = 0;
  while (b64WriteOk) {
    uint8_t n = st.ncarry;
    if (n > 0) { grp[0] = st.carry[0]; if (n > 1) grp[1] = st.carry[1]; }
    while (n < 3 && i < len) grp[n++] = data[i++];
    if (n == 0) break;
    if (n == 3) {
      const uint32_t v = ((uint32_t)grp[0] << 16) | ((uint32_t)grp[1] << 8) | grp[2];
      const char out[4] = {B64TAB[(v >> 18) & 63], B64TAB[(v >> 12) & 63], B64TAB[(v >> 6) & 63], B64TAB[v & 63]};
      b64OutPush(client, out);
      st.ncarry = 0;
      if (i >= len) break;
    } else {
      st.carry[0] = grp[0]; st.carry[1] = grp[1]; st.ncarry = n;
      break;
    }
  }
}

template <typename ClientT>
bool b64Flush(ClientT& client, B64State& st) {
  if (st.ncarry) {
    uint32_t v = (uint32_t)st.carry[0] << 16;
    if (st.ncarry > 1) v |= (uint32_t)st.carry[1] << 8;
    const char out[4] = {B64TAB[(v >> 18) & 63], B64TAB[(v >> 12) & 63], st.ncarry > 1 ? B64TAB[(v >> 6) & 63] : '=', '='};
    b64OutPush(client, out);
    st.ncarry = 0;
  }
  return b64OutFlush(client);
}

void makeWavHeader(uint8_t h[44], uint32_t dataLen, uint16_t bits) {
  memcpy(h, "RIFF", 4);
  uint32_t v = 36 + dataLen; memcpy(h + 4, &v, 4);
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  v = 16; memcpy(h + 16, &v, 4);          // fmt 块长
  h[20] = 1; h[21] = 0;                    // PCM
  h[22] = 1; h[23] = 0;                    // 单声道
  v = VOICE_SAMPLE_RATE; memcpy(h + 24, &v, 4);
  v = VOICE_SAMPLE_RATE * (bits / 8); memcpy(h + 28, &v, 4);  // byteRate
  h[32] = bits / 8; h[33] = 0;             // blockAlign
  h[34] = bits & 0xFF; h[35] = 0;          // 位深
  memcpy(h + 36, "data", 4);
  v = dataLen; memcpy(h + 40, &v, 4);
}

template <typename ClientT>
bool readHttpBody(ClientT& client, String& body, uint32_t maxWaitMs, const char* label) {
  String payload;
  const unsigned long start = millis();
  size_t lastLen = 0;
  unsigned long lastGrowth = millis();
  while ((client.connected() || client.available()) && payload.length() < 16000) {
    while (client.available()) payload += (char)client.read();
    if (payload.length() != lastLen) {
      lastLen = payload.length();
      lastGrowth = millis();
    }
    // 响应头已到且 2 秒无新数据 = 响应结束(Connection:close 时个别栈不复位 connected 标志)
    if (payload.length() > 0 && millis() - lastGrowth > 2000 && payload.indexOf("\r\n\r\n") >= 0) break;
    if (millis() - start > maxWaitMs) break;
    delay(5);
  }
  client.stop();
  Serial.print("V: http[");
  Serial.print(label);
  Serial.print("] wait=");
  Serial.print(millis() - start);
  Serial.print("ms len=");
  Serial.println(payload.length());
  if (payload.length()) {
    Serial.print("V: http head=");
    Serial.println(payload.substring(0, 40));
  }
  const int headerEnd = payload.indexOf("\r\n\r\n");
  if (headerEnd < 0) return false;
  if (!payload.startsWith("HTTP/1.1 200") && !payload.startsWith("HTTP/1.0 200")) return false;
  body = payload.substring(headerEnd + 4);
  if (payload.indexOf("chunked") >= 0) {
    String decoded;
    int pos = 0;
    while (pos < (int)body.length()) {
      const int lineEnd = body.indexOf("\r\n", pos);
      if (lineEnd < 0) break;
      const int chunkLen = (int)strtol(body.substring(pos, lineEnd).c_str(), NULL, 16);
      if (chunkLen <= 0) break;
      decoded += body.substring(lineEnd + 2, lineEnd + 2 + chunkLen);
      pos = lineEnd + 2 + chunkLen + 2;
    }
    body = decoded;
  }
  return true;
}

String extractContent(const String& jsonBody) {
  JsonDocument doc;
  if (deserializeJson(doc, jsonBody)) return "";
  const char* content = doc["choices"][0]["message"]["content"] | "";
  return String(content);
}

void recordVoice(uint32_t& outLen) {
  analogReadResolution(8);
  outLen = 0;
  const unsigned long t0 = millis();
  while (digitalRead(PIN_KEY_B) != LOW) {   // 等待按住 B (或按 C 取消)
    if (digitalRead(PIN_KEY_C) == LOW) return;
    if (millis() - t0 > 15000) return;
    delay(10);
  }
  delay(30);
  beep(1200, 60);
  const uint32_t period = 1000000UL / VOICE_SAMPLE_RATE;
  uint32_t next = micros();
  while (outLen < VOICE_MAX_SAMPLES) {
    if (digitalRead(PIN_KEY_B) != LOW) break;  // 松开即停
    next += period;
    voicePcm[outLen++] = (uint8_t)analogRead(WIO_MIC);
    while ((int32_t)(micros() - next) < 0) {}
  }
  beep(880, 50);
}

// ---- 百度令牌(RAM 内缓存,过期前复用) ----
String baiduToken = "";

bool fetchBaiduToken() {
  if (baiduToken.length() > 10) return true;
  WiFiClientSecure client;
  Serial.println("V: token fetch...");
  if (!client.connect("openapi.baidu.com", 443, 12000)) {
    client.stop();
    Serial.println("V: token tls FAIL");
    return false;
  }
  const String req = String("GET /oauth/2.0/token?grant_type=client_credentials&client_id=") +
                     BAIDU_API_KEY + "&client_secret=" + BAIDU_SECRET_KEY +
                     " HTTP/1.1\r\nHost: openapi.baidu.com\r\nConnection: close\r\n\r\n";
  client.print(req);
  String body;
  if (!readHttpBody(client, body, 15000, "token")) {
    Serial.println("V: token failed");
    return false;
  }
  JsonDocument filter;
  filter["access_token"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    Serial.println("V: token json error");
    return false;
  }
  baiduToken = String(doc["access_token"] | "");
  Serial.print("V: token ok len=");
  Serial.println(baiduToken.length());
  return baiduToken.length() > 10;
}

// 8bit 录音 -> 16bit LE WAV,流式 Base64 上传(不物化完整字符串)
template <typename ClientT>
bool streamWav16(ClientT& client, B64State& st, uint32_t pcmLen) {
  b64OutLen = 0;
  b64WriteOk = true;
  uint8_t h[44];
  makeWavHeader(h, 2 * pcmLen, 16);
  b64Write(client, st, h, 44);
  uint8_t conv[512];
  for (uint32_t i = 0; i < pcmLen && b64WriteOk; i += 256) {
    const uint32_t n = (pcmLen - i < 256) ? (pcmLen - i) : 256;
    for (uint32_t j = 0; j < n; j++) {
      const int16_t v = (int16_t)(((int)voicePcm[i + j] - 128) * 256);
      conv[2 * j] = (uint8_t)(v & 0xFF);
      conv[2 * j + 1] = (uint8_t)((uint16_t)v >> 8);
    }
    b64Write(client, st, conv, 2 * n);
  }
  return b64Flush(client, st);
}

void runVoiceAsk() {
  if (!wifiEnsureConnected()) {
    finishAction(true, "wifi connect failed");
    return;
  }

  drawWait();
  display.setTextSize(1);
  display.setTextColor(TFT_YELLOW);
  display.drawString("HOLD B and talk, release to send", 16, 140);
  Serial.println("V: waiting B press");

  uint32_t pcmLen = 0;
  recordVoice(pcmLen);
  Serial.print("V: recorded bytes=");
  Serial.println(pcmLen);
  if (pcmLen < 4000) {
    finishAction(true, "no speech recorded (hold B)");
    return;
  }
  // 录音结束:换成识别中提示
  display.fillRect(0, 130, 320, 20, TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_YELLOW);
  display.drawString("recognizing ... (a few seconds)", 16, 140);

  // ---- 第一步:百度短语音识别(走明文 80 端口,避免板载 TLS 写限速 5KB/s 被服务端掐线) ----
  if (!fetchBaiduToken()) {
    finishAction(true, "baidu token failed");
    return;
  }

  Serial.println("V: asr tcp connecting...");
  WiFiClient client;
  if (!client.connect("vop.baidu.com", 80, 12000)) {
    client.stop();
    Serial.println("V: asr connect FAIL");
    finishAction(true, "asr connect failed");
    return;
  }
  const uint32_t wavLen = 44 + 2 * pcmLen;
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
  client.print(req);
  client.print(head);
  B64State st = {{0, 0}, 0};
  const uint32_t upT = millis();
  if (!streamWav16(client, st, pcmLen)) {
    client.stop();
    finishAction(true, "asr upload failed");
    return;
  }
  client.print(tail);
  Serial.print("V: wav uploaded ms=");
  Serial.println(millis() - upT);

  String asrBody;
  if (!readHttpBody(client, asrBody, 15000, "asr")) {
    Serial.println("V: asr http error");
    finishAction(true, "asr http error");
    return;
  }
  JsonDocument adoc;
  if (deserializeJson(adoc, asrBody)) {
    finishAction(true, "asr bad json");
    return;
  }
  const int errNo = adoc["err_no"] | -1;
  const char* text = adoc["result"][0] | "";
  Serial.print("V: baidu err_no=");
  Serial.print(errNo);
  Serial.print(" transcript=");
  Serial.println(text);  // 串口日志可读中文
  if (errNo == 110 || errNo == 111 || errNo == 3302) {
    baiduToken = "";
  }
  if (errNo != 0 || !text[0]) {
    finishAction(true, "asr rejected (speak louder?)");
    return;
  }
  const String transcript = String(text);

  // ---- 第二步:DeepSeek 回答(未填 Key 时只显示识别原文) ----
  storeText(transcript.c_str());
  resultTranscriptLines = resultCount;

  if (strlen(DEEPSEEK_KEY) < 5) {
    Serial.println("V: no deepseek key, asr-only");
    finishAction(false, "asr ok (add deepseek key for AI reply)");
    return;
  }

  Serial.println("V: llm tls connecting...");
  WiFiClientSecure client2;
  if (!client2.connect("api.deepseek.com", 443, 15000)) {
    client2.stop();
    Serial.println("V: llm tls FAIL");
    finishAction(true, "llm connect failed");
    return;
  }

  JsonDocument req2;
  req2["model"] = "deepseek-chat";
  JsonArray msgs = req2["messages"].to<JsonArray>();
  JsonObject sys = msgs.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] =
      "用户的话已由语音识别转成文字。请直接用简体中文回答(40字以内),"
      "不要任何前缀、标记或解释。";
  JsonObject user = msgs.add<JsonObject>();
  user["role"] = "user";
  user["content"] = transcript;
  req2["max_tokens"] = 150;

  String body2;
  serializeJson(req2, body2);
  String req2s = "POST /v1/chat/completions HTTP/1.1\r\n"
                 "Host: api.deepseek.com\r\n"
                 "Authorization: Bearer " DEEPSEEK_KEY "\r\n"
                 "Content-Type: application/json\r\n"
                 "Connection: close\r\nContent-Length: ";
  req2s += String(body2.length());
  req2s += "\r\n\r\n";
  req2s += body2;
  client2.print(req2s);

  String llmBody;
  if (!readHttpBody(client2, llmBody, 20000, "llm")) {
    Serial.println("V: llm http error");
    finishAction(true, "llm http error");
    return;
  }
  const String reply = extractContent(llmBody);
  Serial.print("V: llm reply bytes=");
  Serial.println(reply.length());
  if (!reply.length()) {
    finishAction(true, "llm empty reply");
    return;
  }
  storeText(reply.c_str());
  finishAction(false, "voice ask ok");
}
