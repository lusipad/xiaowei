// rpcWiFi 替身:WiFi 恒为已连接(模拟器不模拟网络,云端由 sim/mocks.cpp 扮演)
#pragma once
#include "Arduino.h"

#define WL_CONNECTED 3
#define WL_IDLE_STATUS 0
#define WIFI_STA 1
#define WIFI_AP  2

class IPAddress {
 public:
  const char* c_str() const { return "192.168.7.7 (sim)"; }
};

class SimWiFiClass {
 public:
  int status() const { return WL_CONNECTED; }
  void mode(int) {}
  void begin(const char*, const char*) {}
  void disconnect() {}
  bool softAP(const char*, const char* = nullptr) { return true; }
  bool softAPdisconnect(bool = false) { return true; }
  IPAddress softAPIP() const { return IPAddress(); }
  IPAddress localIP() const { return IPAddress(); }
};

class WiFiClient {
 public:
  bool connect(const char*, uint16_t, int32_t = 0) { return true; }
  void setTimeout(uint32_t) {}
  size_t print(const char*) { return 0; }
  size_t write(const uint8_t*, size_t n) { return n; }
  int available() { return 0; }
  int read() { return -1; }
  bool connected() { return false; }
  String readStringUntil(char) { return String(); }
  void stop() {}
};

extern SimWiFiClass WiFi;
