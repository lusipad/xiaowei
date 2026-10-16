#include "WioKitConfig.h"
#include <sfud.h>
#include <rpcWiFi.h>
#include <WebServer.h>

static const uint32_t WIO_CONFIG_MAGIC = 0x57494F32;       // "WIO2"
static const uint32_t WIO_CONFIG_ADDR  = 0x003FF000;       // 4MB Flash 最后一个 4KB 扇区

static WebServer* pWebServer = nullptr;
static bool portalSaved = false;
static bool portalExit = false;
static WioConfig portalResultCfg;
static WioConfig currentPresetCfg;

static uint32_t calcChecksum(const WioConfig& cfg) {
  const uint8_t* p = (const uint8_t*)&cfg;
  const size_t len = offsetof(WioConfig, crc);
  uint32_t sum = 0x12345678;
  for (size_t i = 0; i < len; i++) {
    sum = ((sum << 5) | (sum >> 27)) ^ p[i];
  }
  return sum;
}

// HTML 属性与正文转义:防止包含单引号(如 "Ming's iPhone")或特殊符号的 SSID 截断 HTML 属性
static String htmlEscape(const char* s) {
  if (!s) return String("");
  String out = "";
  while (*s) {
    const char c = *s++;
    if (c == '&') out += F("&amp;");
    else if (c == '\'') out += F("&#39;");
    else if (c == '"') out += F("&quot;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else out += c;
  }
  return out;
}

bool wioConfigInit() {
  static bool inited = false;
  if (inited) return true;
  if (sfud_init() == SFUD_SUCCESS) {
    inited = true;
    Serial.println("V: QSPI flash init ok");
    return true;
  }
  Serial.println("V: QSPI flash init FAIL");
  return false;
}

bool wioConfigLoad(WioConfig& cfg) {
  if (!wioConfigInit()) return false;
  const sfud_flash* flash = sfud_get_device(0);
  if (!flash) return false;

  WioConfig tmp;
  if (sfud_read(flash, WIO_CONFIG_ADDR, sizeof(tmp), (uint8_t*)&tmp) != SFUD_SUCCESS) {
    return false;
  }
  if (tmp.magic != WIO_CONFIG_MAGIC) return false;
  if (tmp.crc != calcChecksum(tmp)) return false;
  tmp.ssid[sizeof(tmp.ssid) - 1] = '\0';
  tmp.pass[sizeof(tmp.pass) - 1] = '\0';
  tmp.baiduApiKey[sizeof(tmp.baiduApiKey) - 1] = '\0';
  tmp.baiduSecret[sizeof(tmp.baiduSecret) - 1] = '\0';
  tmp.deepseekKey[sizeof(tmp.deepseekKey) - 1] = '\0';
  tmp.city[sizeof(tmp.city) - 1] = '\0';
  if (strlen(tmp.ssid) == 0) return false;

  cfg = tmp;
  Serial.print("V: loaded config from flash, ssid=");
  Serial.print(cfg.ssid);
  Serial.print(" city=");
  Serial.println(cfg.city);
  return true;
}

bool wioConfigSave(const WioConfig& cfg) {
  if (!wioConfigInit()) return false;
  const sfud_flash* flash = sfud_get_device(0);
  if (!flash) return false;

  WioConfig toWrite = cfg;
  toWrite.magic = WIO_CONFIG_MAGIC;
  toWrite.crc = calcChecksum(toWrite);

  if (sfud_erase_write(flash, WIO_CONFIG_ADDR, sizeof(toWrite), (uint8_t*)&toWrite) != SFUD_SUCCESS) {
    Serial.println("V: save config FAIL");
    return false;
  }
  Serial.println("V: save config ok");
  return true;
}

bool wioConfigClear() {
  if (!wioConfigInit()) return false;
  const sfud_flash* flash = sfud_get_device(0);
  if (!flash) return false;
  return sfud_erase(flash, WIO_CONFIG_ADDR, 4096) == SFUD_SUCCESS;
}

// ---- 配网 HTML 页面渲染 ----
static void handlePortalRoot() {
  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>"
                  "<title>小维桌宠 - 网络配置</title><style>"
                  "body{font-family:-apple-system,sans-serif;background:#fff5ea;color:#333;margin:0;padding:16px;display:flex;justify-content:center;}"
                  ".card{background:#fff;border-radius:18px;box-shadow:0 8px 24px rgba(0,0,0,0.08);max-width:380px;width:100%;padding:24px 20px;box-sizing:border-box;border:2px solid #ffcc80;}"
                  "h2{margin:0 0 4px;font-size:20px;color:#d85a00;text-align:center;}"
                  "p.sub{margin:0 0 18px;font-size:12px;color:#888;text-align:center;}"
                  "label{display:block;font-size:13px;font-weight:bold;margin:12px 0 4px;color:#555;}"
                  "input{width:100%;box-sizing:border-box;padding:10px 12px;border:1px solid #ddd;border-radius:10px;font-size:14px;outline:none;background:#fafafa;}"
                  "input:focus{border-color:#ff9800;background:#fff;}"
                  "details{margin-top:16px;background:#fdfdfd;border:1px dashed #ccc;border-radius:10px;padding:10px;}"
                  "summary{font-size:13px;color:#777;cursor:pointer;font-weight:bold;}"
                  "button{width:100%;margin-top:22px;padding:12px;background:linear-gradient(135deg,#ff9800,#f57c00);color:#fff;border:none;border-radius:12px;font-size:16px;font-weight:bold;cursor:pointer;box-shadow:0 4px 12px rgba(245,124,0,0.3);}"
                  "button:active{transform:scale(0.98);}"
                  ".exit-btn{display:block;text-align:center;margin-top:16px;color:#999;font-size:13px;text-decoration:none;padding:6px;}"
                  ".tip{font-size:11px;color:#aaa;margin-top:4px;}"
                  "</style></head><body><div class='card'>"
                  "<h2>🐱 小维桌宠配网</h2>"
                  "<p class='sub'>设置 WiFi 与常驻城市</p>"
                  "<form action='/save' method='POST'>");

  html += F("<label>WiFi 名称 (SSID)</label><input type='text' name='ssid' value='");
  html += htmlEscape(currentPresetCfg.ssid);
  html += F("' placeholder='输入要连接的 WiFi 名称' required>");

  html += F("<label>WiFi 密码</label><input type='password' name='pass' placeholder='");
  html += (strlen(currentPresetCfg.pass) > 0) ? F("留空保留原密码 (或输入新密码)") : F("无密码可留空");
  html += F("'>");

  html += F("<label>常驻城市 (查天气用)</label><input type='text' name='city' value='");
  html += htmlEscape(strlen(currentPresetCfg.city) ? currentPresetCfg.city : "上海");
  html += F("' placeholder='例如：上海 / 北京 / 深圳'>");

  html += F("<label>桌面皮肤形象</label><select name='skin' style='width:100%;padding:10px;margin-bottom:14px;border:1px solid #ccc;border-radius:8px;font-size:15px;'>"
            "<option value='0'");
  if (currentPresetCfg.activeSkin == 0) html += F(" selected");
  html += F(">🐱 小猫小维 (经典像素)</option><option value='1'");
  if (currentPresetCfg.activeSkin == 1) html += F(" selected");
  html += F(">🐋 DeepSeek 女仆大肥鱼</option></select>");

  html += F("<details><summary>🔑 自定义 API Key (可选)</summary>"
            "<label>DeepSeek API Key</label><input type='password' name='ds_key' placeholder='留空保留原配置'>"
            "<label>Baidu API Key</label><input type='password' name='bd_key' placeholder='留空保留原配置'>"
            "<label>Baidu Secret Key</label><input type='password' name='bd_sec' placeholder='留空保留原配置'>"
            "<div class='tip'>如未变更密钥，留空即可继续使用预置 Key</div></details>");

  html += F("<button type='submit'>💾 保存并连接</button>"
            "<a href='/exit' class='exit-btn'>❌ 退出配网模式</a>"
            "</form></div></body></html>");

  pWebServer->send(200, "text/html", html);
}

static void handlePortalExit() {
  portalExit = true;
  pWebServer->send(200, "text/html",
                   F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                     "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                     "<title>已退出</title><style>"
                     "body{font-family:sans-serif;background:#fff5ea;color:#d85a00;display:flex;justify-content:center;align-items:center;height:90vh;margin:0;}"
                     ".box{background:#fff;padding:30px;border-radius:16px;text-align:center;box-shadow:0 4px 20px rgba(0,0,0,0.1);max-width:320px;}"
                     "</style></head><body><div class='box'>"
                     "<h3>已退出配网模式</h3><p style='color:#666;'>小维已恢复正常待机，你可以关闭此网页啦~</p>"
                     "</div></body></html>"));
}

static void handlePortalSave() {
  String ssid = pWebServer->arg("ssid");
  ssid.trim();
  if (!ssid.length() && strlen(currentPresetCfg.ssid) > 0) {
    ssid = currentPresetCfg.ssid;
  }

  // WiFi 密码不做 trim()，以免破坏合法包含首尾空格的 WPA2 密码
  String pass = pWebServer->arg("pass");

  String city = pWebServer->arg("city");
  city.trim();
  if (!city.length()) city = "上海";

  String dsKey = pWebServer->arg("ds_key");
  dsKey.trim();
  String bdKey = pWebServer->arg("bd_key");
  bdKey.trim();
  String bdSec = pWebServer->arg("bd_sec");
  bdSec.trim();

  memset(&portalResultCfg, 0, sizeof(portalResultCfg));
  strncpy(portalResultCfg.ssid, ssid.c_str(), sizeof(portalResultCfg.ssid) - 1);

  // 若用户仅修改城市或 API Key 而未重输密码(且 SSID 未变)，自动保留原 WiFi 密码，防止密码被意外清空!
  if (pass.length() > 0) {
    strncpy(portalResultCfg.pass, pass.c_str(), sizeof(portalResultCfg.pass) - 1);
  } else if (strcmp(ssid.c_str(), currentPresetCfg.ssid) == 0 && strlen(currentPresetCfg.pass) > 0) {
    strncpy(portalResultCfg.pass, currentPresetCfg.pass, sizeof(portalResultCfg.pass) - 1);
  }

  strncpy(portalResultCfg.city, city.c_str(), sizeof(portalResultCfg.city) - 1);
  if (pWebServer->hasArg("skin")) {
    portalResultCfg.activeSkin = (uint8_t)pWebServer->arg("skin").toInt();
  } else {
    portalResultCfg.activeSkin = currentPresetCfg.activeSkin;
  }

  // 若填写了新 Key 则采用新 Key，否则沿用预置
  if (dsKey.length() > 5) {
    strncpy(portalResultCfg.deepseekKey, dsKey.c_str(), sizeof(portalResultCfg.deepseekKey) - 1);
  } else {
    strncpy(portalResultCfg.deepseekKey, currentPresetCfg.deepseekKey, sizeof(portalResultCfg.deepseekKey) - 1);
  }

  if (bdKey.length() > 5) {
    strncpy(portalResultCfg.baiduApiKey, bdKey.c_str(), sizeof(portalResultCfg.baiduApiKey) - 1);
  } else {
    strncpy(portalResultCfg.baiduApiKey, currentPresetCfg.baiduApiKey, sizeof(portalResultCfg.baiduApiKey) - 1);
  }

  if (bdSec.length() > 5) {
    strncpy(portalResultCfg.baiduSecret, bdSec.c_str(), sizeof(portalResultCfg.baiduSecret) - 1);
  } else {
    strncpy(portalResultCfg.baiduSecret, currentPresetCfg.baiduSecret, sizeof(portalResultCfg.baiduSecret) - 1);
  }

  wioConfigSave(portalResultCfg);
  portalSaved = true;

  String resp = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>保存成功</title><style>"
                  "body{font-family:sans-serif;background:#e8f5e9;color:#2e7d32;display:flex;justify-content:center;align-items:center;height:90vh;margin:0;}"
                  ".box{background:#fff;padding:30px;border-radius:16px;text-align:center;box-shadow:0 4px 20px rgba(0,0,0,0.1);max-width:320px;}"
                  "h2{margin-top:0;}p{color:#555;font-size:14px;}"
                  "</style></head><body><div class='box'>"
                  "<h2>✅ 配置已保存！</h2>"
                  "<p>小维正在连接 WiFi：<b>");
  resp += htmlEscape(ssid.c_str());
  resp += F("</b></p><p>设备将自动重启联网，你可以关闭此网页啦~</p></div></body></html>");

  pWebServer->send(200, "text/html", resp);
}

// Captive Portal 探测与未知路径统一 302 重定向到配网首页，确保 iOS/Android/Windows 自动唤起配网弹窗
static void handlePortalNotFound() {
  pWebServer->sendHeader("Location", "http://192.168.4.1/", true);
  pWebServer->send(302, "text/plain", "");
}

bool wioPortalBegin(const char* apSsid, const WioConfig& curCfg) {
  portalSaved = false;
  portalExit = false;
  currentPresetCfg = curCfg;

  if (pWebServer) {
    pWebServer->stop();
    delete pWebServer;
    pWebServer = nullptr;
  }

  WiFi.disconnect();
  delay(50);

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(apSsid)) {
    Serial.println("V: softAP failed");
    return false;
  }
  IPAddress ip = WiFi.softAPIP();
  Serial.print("V: softAP started, ip=");
  Serial.println(ip);

  pWebServer = new WebServer(80);
  pWebServer->on("/", HTTP_GET, handlePortalRoot);
  pWebServer->on("/save", HTTP_POST, handlePortalSave);
  pWebServer->on("/exit", HTTP_GET, handlePortalExit);
  pWebServer->on("/generate_204", HTTP_GET, handlePortalRoot);
  pWebServer->on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);
  pWebServer->onNotFound(handlePortalNotFound);
  pWebServer->begin();

  return true;
}

int wioPortalPoll(WioConfig& outCfg) {
  if (pWebServer) pWebServer->handleClient();
  if (portalSaved) {
    outCfg = portalResultCfg;
    return 1;
  }
  if (portalExit) {
    return 2;
  }
  return 0;
}

void wioPortalEnd() {
  if (pWebServer) {
    pWebServer->stop();
    delete pWebServer;
    pWebServer = nullptr;
  }
  WiFi.softAPdisconnect(true);
  delay(50);
  WiFi.mode(WIFI_STA);
  Serial.println("V: softAP stopped, memory released");
}
