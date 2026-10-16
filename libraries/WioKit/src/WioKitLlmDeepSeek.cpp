#include "WioKitLlmDeepSeek.h"
#include <WioKitNet.h>
#include <WioKitTiming.h>
#include <rpcWiFi.h>
#include <rpcWiFiClientSecure.h>
#include <ArduinoJson.h>

static String llmKey = "";
static String llmSystemPrompt = "";

void wioLlmDeepSeekBegin(const char* key, const char* systemPrompt) {
  llmKey = key ? key : "";
  llmSystemPrompt = systemPrompt ? systemPrompt : "";
}

bool wioLlmAskWithHistory(const WioLlmMsg* history, size_t historyCount, const String& question, String& reply, String& note) {
  reply = "";
  note = "";
  if (wioNetAborted()) {
    note = "cancelled";
    return false;
  }
  if (llmKey.length() < 5) {
    note = "llm key missing";
    return false;
  }
  const uint32_t tConn = millis();
  WiFiClientSecure client;
  if (!client.connect("api.deepseek.com", 443, 12000)) {
    client.stop();
    note = wioNetAborted() ? "cancelled" : "llm connect failed";
    return false;
  }
  if (wioNetAborted()) {
    client.stop();
    note = "cancelled";
    return false;
  }
  wioTiming().llmConn = millis() - tConn;
  const uint32_t tLlm = millis();
  JsonDocument req;
  req["model"] = "deepseek-chat";
  JsonArray msgs = req["messages"].to<JsonArray>();
  JsonObject sys = msgs.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] = llmSystemPrompt.length() ? llmSystemPrompt.c_str() : "你是语音助手,用简体中文简短回答。";

  // 注入历史对话上下文
  if (history && historyCount > 0) {
    for (size_t i = 0; i < historyCount; i++) {
      if (history[i].content.length() > 0) {
        JsonObject hmsg = msgs.add<JsonObject>();
        hmsg["role"] = history[i].role;
        hmsg["content"] = history[i].content;
      }
    }
  }

  JsonObject user = msgs.add<JsonObject>();
  user["role"] = "user";
  user["content"] = question;
  req["max_tokens"] = 120;  // 回答 45 字以内,够用且限制最坏耗时

  String body;
  serializeJson(req, body);
  String reqStr = "POST /v1/chat/completions HTTP/1.1\r\n"
                  "Host: api.deepseek.com\r\n"
                  "Authorization: Bearer ";
  reqStr += llmKey;
  reqStr += "\r\nContent-Type: application/json\r\n"
            "Connection: close\r\nContent-Length: ";
  reqStr += String(body.length());
  reqStr += "\r\n\r\n";
  reqStr += body;
  if (!wioWriteStr(client, reqStr.c_str(), 8000)) {
    client.stop();
    note = wioNetAborted() ? "cancelled" : "llm write failed";
    return false;
  }

  HttpBuf http;
  wioBindSharedHttpBuf(http);
  const bool ok = wioReadHttp(client, http, 18000, "llm");
  wioTiming().llm = millis() - tLlm;
  if (!ok) {
    if (wioNetAborted()) {
      note = "cancelled";
    } else if (http.status == 401) {
      note = "llm auth 401";
    } else if (http.status == 402) {
      note = "llm balance 402";
    } else if (http.status == 429) {
      note = "llm rate 429";
    } else if (http.status >= 500) {
      note = String("llm server ") + String(http.status);
    } else {
      note = "llm http error";
    }
    return false;
  }
  JsonDocument ldoc;
  if (deserializeJson(ldoc, http.body(), http.len)) {
    note = "llm bad json";
    return false;
  }
  reply = String(ldoc["choices"][0]["message"]["content"] | "");
  Serial.print("V: reply=");
  Serial.println(reply);
  if (!reply.length()) {
    note = "llm empty reply";
    return false;
  }
  return true;
}
