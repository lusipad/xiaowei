#pragma once
#include <Arduino.h>
#if defined(ARDUINO) && __has_include(<sfud.h>)
#include <sfud.h>
inline bool petQspiRead(uint32_t addr, uint8_t* buf, size_t len) {
  const sfud_flash* flash = sfud_get_device(0);
  if (!flash) return false;
  return sfud_read(flash, addr, len, buf) == SFUD_SUCCESS;
}
#else
inline bool petQspiRead(uint32_t, uint8_t*, size_t) { return false; }
#endif

#include "pet_scene_data.h"

const uint32_t QSPI_SKIN1_ADDR = 0x00040000;  // Slot 1: DeepSeek Maid
const uint32_t QSPI_SKIN_MAGIC = 0x534B494E;  // "SKIN"

struct SkinHeader {
  uint32_t magic;           // 0x534B494E
  uint32_t version;         // 1
  char skinId[16];          // "deepseek"
  char charName[16];        // "大肥鱼"
  uint16_t bottomBg;        // 0x2906
  int16_t faceX;            // 124
  int16_t faceY;            // 92
  int16_t faceW;            // 64
  int16_t faceH;            // 32
  uint16_t reserved;        // 2 bytes explicit padding to offset 52
  uint32_t paletteOffset;   // 1024
  uint32_t facesOffset;
  uint32_t faceSize;
  uint32_t bgOffset;
  uint32_t totalSize;
  char prompt[512];
};

extern int currentSkinId;
extern SkinHeader dsSkinHeader;
extern uint16_t dsPalette[256];
extern bool hasDsSkin;

bool petSkinInit(int preferredSkin);
bool petSkinHasDs();
bool petSkinSet(int skinId);
bool petSkinToggle();
const char* petSkinGetName();
const char* petSkinGetPrompt();
uint16_t petSkinGetBottomBg();
int petSkinGetFaceX();
int petSkinGetFaceY();
int petSkinGetFaceW();
int petSkinGetFaceH();
