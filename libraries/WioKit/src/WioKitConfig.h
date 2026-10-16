// ---- L1 配置持久化与端侧配网 ----
// 存储介质: 板载 4MB QSPI Flash 芯片 (W25Q32JV, 扇区 0x003FF000)
// 配网模式: 临时按需启动 SoftAP + WebServer + DNSServer (按需创建，退出即释放 100% 内存)
#pragma once
#include <Arduino.h>

struct WioConfig {
  uint32_t magic;          // 0x57494F32 ("WIO2")
  char ssid[33];           // WiFi SSID
  char pass[65];           // WiFi Password
  char baiduApiKey[65];    // Baidu API Key
  char baiduSecret[65];    // Baidu Secret Key
  char deepseekKey[80];    // DeepSeek Key
  char city[33];           // 城市名 (如 "上海")
  uint8_t activeSkin;      // 当前生效皮肤 (0=小猫小维, 1=DeepSeek大肥鱼)
  uint8_t reserved[3];
  uint32_t crc;
};

// 初始化板载 4MB QSPI Flash
bool wioConfigInit();

// 从板载 Flash 读取配置，成功且有效返回 true
bool wioConfigLoad(WioConfig& cfg);

// 将配置持久化写入板载 Flash (掉电不丢失)
bool wioConfigSave(const WioConfig& cfg);

// 清空板载 Flash 中的配置
bool wioConfigClear();

// 启动端侧 SoftAP 网页配网
// apSsid: 热点名 (例如 "Wio-Pet-Setup")
// curCfg: 当前配置 (用于在网页中预填)
bool wioPortalBegin(const char* apSsid, const WioConfig& curCfg);

// 轮询配网: 0=等待输入, 1=已保存并生成新配置, -1=错误
int wioPortalPoll(WioConfig& outCfg);

// 结束配网模式并销毁 WebServer / DNS，彻底释放内存
void wioPortalEnd();
