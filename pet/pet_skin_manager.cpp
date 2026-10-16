#include "pet_skin_manager.h"
#include <WioKitConfig.h>

int currentSkinId = 0; // 0=Cat, 1=DeepSeek
SkinHeader dsSkinHeader;
uint16_t dsPalette[256];
bool hasDsSkin = false;

static bool checkAndLoadDsFromQspi() {
  SkinHeader hdr;
  if (!petQspiRead(QSPI_SKIN1_ADDR, (uint8_t*)&hdr, sizeof(hdr))) {
    return false;
  }
  if (hdr.magic != QSPI_SKIN_MAGIC) {
    Serial.println("P: QSPI skin slot 1 not programmed or magic mismatch");
    return false;
  }
  dsSkinHeader = hdr;
  // Load palette into RAM
  if (!petQspiRead(QSPI_SKIN1_ADDR + hdr.paletteOffset, (uint8_t*)dsPalette, 512)) {
    return false;
  }
  hasDsSkin = true;
  Serial.print("P: QSPI skin slot 1 loaded: ");
  Serial.print(hdr.skinId);
  Serial.print(" (");
  Serial.print(hdr.charName);
  Serial.println(")");
  return true;
}


bool petSkinInit(int preferredSkin) {
  hasDsSkin = checkAndLoadDsFromQspi();
  if (preferredSkin == 1 && hasDsSkin) {
    currentSkinId = 1;
  } else {
    currentSkinId = 0;
  }
  return true;
}

bool petSkinHasDs() {
  return hasDsSkin;
}

bool petSkinSet(int skinId) {
  if (skinId == 1) {
    hasDsSkin = checkAndLoadDsFromQspi();
    if (hasDsSkin) {
      currentSkinId = 1;
      return true;
    }
    return false;
  }
  currentSkinId = 0;
  return true;
}

bool petSkinToggle() {
  if (currentSkinId == 0) {
    return petSkinSet(1);
  } else {
    return petSkinSet(0);
  }
}

const char* petSkinGetName() {
  return (currentSkinId == 1 && hasDsSkin) ? dsSkinHeader.charName : "小维";
}

const char* petSkinGetPrompt() {
  if (currentSkinId == 1 && hasDsSkin) {
    return dsSkinHeader.prompt;
  }
  return "你是电子桌宠\"小维\",性格活泼元气,说话简短、口语化、爱用感叹号。"
         "回答必须以情绪标签开头,格式如 [开心],情绪只能从 开心/兴奋/惊讶/害羞/疑惑/难过 里选一个;"
         "标签后直接是回答正文,用简体中文,45字以内,不要使用Emoji表情,不要使用Markdown符号(如*、#),直接说纯文本。";
}

uint16_t petSkinGetBottomBg() {
  return (currentSkinId == 1 && hasDsSkin) ? dsSkinHeader.bottomBg : 0x0862;
}

int petSkinGetFaceX() {
  return (currentSkinId == 1 && hasDsSkin) ? dsSkinHeader.faceX : PET_FACE_X;
}
int petSkinGetFaceY() {
  return (currentSkinId == 1 && hasDsSkin) ? dsSkinHeader.faceY : PET_FACE_Y;
}
int petSkinGetFaceW() {
  return (currentSkinId == 1 && hasDsSkin) ? dsSkinHeader.faceW : PET_FACE_W;
}
int petSkinGetFaceH() {
  return (currentSkinId == 1 && hasDsSkin) ? dsSkinHeader.faceH : PET_FACE_H;
}
