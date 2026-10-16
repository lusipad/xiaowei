// ---- 宠物外观:像素艺术小房间 + 概念图小猫全套表情 ----
// 100% 还原概念图 2 的小房间场景、坐垫与小猫形象
#pragma once
#include <Arduino.h>
#include <Seeed_GFX.h>
#include <WioKitCjk.h>
#include "pet_scene_data.h"
#include "pet_skin_manager.h"

enum Face {
  F_NORMAL, F_BLINK, F_HAPPY, F_SAD, F_LISTEN, F_THINK,
  F_EXCITED, F_SURPRISED, F_SHY, F_CONFUSED, F_SLEEPY, F_SLEEP, F_DIZZY, F_YAWN
};

const int CX = 160, CY = 110;
const uint16_t C_BUBBLE = 0xFFFF;
const uint16_t C_EDGE = 0x2124;       // 像素深色描边
const uint16_t C_WATER = 0x5DFF;      // 泪滴/声波
const uint16_t C_GOLD = 0xFEA0;       // 星光/音符
const uint16_t C_BOTTOM_BG = 0x0862;  // 底部黑框内胆颜色

// 装饰层区域 (左右两侧墙壁空旷区域, 必须位于 y >= 96, 绝不能侵入气泡 y=4..93)
const int DECO_LX = 16, DECO_RX = 254, DECO_Y = 96, DECO_W = 46, DECO_H = 56;

static int petLastLook = 0;

// 还原背景指定矩形区域 (用于擦除气泡或擦除装饰, 毫秒级无闪烁)
void restoreSceneRect(int rx, int ry, int rw, int rh) {
  if (rx < 0) { rw += rx; rx = 0; }
  if (ry < 0) { rh += ry; ry = 0; }
  if (rx + rw > 320) rw = 320 - rx;
  if (ry + rh > 240) rh = 240 - ry;
  if (rw <= 0 || rh <= 0) return;

  uint16_t line[320];
  if (currentSkinId == 0 || !hasDsSkin) {
    for (int y = ry; y < ry + rh; y++) {
      const uint8_t* src = &PET_SCENE_BG[y * 320 + rx];
      for (int x = 0; x < rw; x++) {
        line[x] = PET_PALETTE[src[x]];
      }
      display.pushImage(rx, y, rw, 1, line);
    }
  } else {
    uint8_t idxBuf[320];
    const uint32_t bgBase = QSPI_SKIN1_ADDR + dsSkinHeader.bgOffset;
    for (int y = ry; y < ry + rh; y++) {
      petQspiRead(bgBase + y * 320 + rx, idxBuf, rw);
      for (int x = 0; x < rw; x++) {
        line[x] = dsPalette[idxBuf[x]];
      }
      display.pushImage(rx, y, rw, 1, line);
    }
  }
}

// 强制复位面部偏移边缘 (兼容保留)
void petFaceResetLook() {
  petLastLook = 0;
}

// 绘制面部表情切片 (固定在 112, 94 的 86x32 区域, 极速无闪烁, 零位移零残影)
void drawFeatures(int f, int look = 0) {
  if (f < 0 || f >= 14) f = F_NORMAL;
  (void)look;  // 兼容旧接口，面部切片固定原位，彻底杜绝切片错位撕裂与重影
  if (currentSkinId == 0 || !hasDsSkin) {
    const uint8_t* patch = PET_FACES[f];
    uint16_t line[PET_FACE_W];
    for (int y = 0; y < PET_FACE_H; y++) {
      const uint8_t* src = &patch[y * PET_FACE_W];
      for (int x = 0; x < PET_FACE_W; x++) {
        line[x] = PET_PALETTE[src[x]];
      }
      display.pushImage(PET_FACE_X, PET_FACE_Y + y, PET_FACE_W, 1, line);
    }
  } else {
    const int fw = dsSkinHeader.faceW;
    const int fh = dsSkinHeader.faceH;
    const uint32_t faceBase = QSPI_SKIN1_ADDR + dsSkinHeader.facesOffset + (uint32_t)f * dsSkinHeader.faceSize;
    uint8_t faceLineIdx[86];
    uint16_t line[86];
    for (int y = 0; y < fh; y++) {
      petQspiRead(faceBase + y * fw, faceLineIdx, fw);
      for (int x = 0; x < fw; x++) {
        line[x] = dsPalette[faceLineIdx[x]];
      }
      display.pushImage(dsSkinHeader.faceX, dsSkinHeader.faceY + y, fw, 1, line);
    }
  }
}

// 只重画眼部区域 (兼容旧接口, 直接映射到 drawFeatures)
void drawEyesOnly(int f, int look = 0) {
  drawFeatures(f, look);
}

// 整屏绘制: 绘制概念图基础场景 + 应用指定表情 (仅在切换状态时调用)
void drawPet(int f) {
  petLastLook = 0;
  restoreSceneRect(0, 0, 320, 240);
  if (currentSkinId == 1 && hasDsSkin) {
    const uint16_t bg = petSkinGetBottomBg();
    display.fillRoundRect(6, 208, 308, 29, 4, bg);
    display.drawRoundRect(6, 208, 308, 29, 4, 0x0000);
    display.drawRoundRect(7, 209, 306, 27, 3, 0x4478);
  }
  if (f != F_NORMAL || currentSkinId != 0) {
    drawFeatures(f, 0);
  }
}

// ---- 装饰层 ----
void clearDeco() {
  restoreSceneRect(DECO_LX, DECO_Y, DECO_W, DECO_H);
  restoreSceneRect(DECO_RX, DECO_Y, DECO_W, DECO_H);
}

void drawSparkle(int x, int y, int r, uint16_t c) {
  display.drawFastHLine(x - r, y, 2 * r + 1, c);
  display.drawFastVLine(x, y - r, 2 * r + 1, c);
  display.drawLine(x - r / 2, y - r / 2, x + r / 2, y + r / 2, c);
  display.drawLine(x - r / 2, y + r / 2, x + r / 2, y - r / 2, c);
}

void drawNote(int x, int y, uint16_t c) {  // ♪
  display.fillCircle(x, y, 3, c);
  display.drawFastVLine(x + 3, y - 12, 12, c);
  display.drawLine(x + 3, y - 12, x + 8, y - 8, c);
}

void drawGlyph(const char* s, int x, int y, int size, uint16_t c) {
  display.setTextSize(size);
  display.setTextColor(c);
  display.drawString(s, x, y);
  display.setTextSize(1);  // 立即恢复 1:1 缩放，防止污染中英文混排渲染
}

void drawEmoteDeco(int f, int phase) {
  clearDeco();
  switch (f) {
    case F_EXCITED:  // 星光闪烁(两组位置交替)
      if (phase % 2 == 0) {
        drawSparkle(DECO_RX + 16, DECO_Y + 12, 5, C_GOLD);
        drawSparkle(DECO_LX + 24, DECO_Y + 24, 4, C_GOLD);
      } else {
        drawSparkle(DECO_RX + 26, DECO_Y + 24, 4, C_GOLD);
        drawSparkle(DECO_LX + 14, DECO_Y + 12, 5, C_GOLD);
      }
      break;
    case F_SURPRISED:
      drawGlyph("!", DECO_RX + 16, DECO_Y + 10, 3, TFT_YELLOW);
      break;
    case F_CONFUSED:
      drawGlyph("?", DECO_RX + 16, DECO_Y + 10, 3, C_WATER);
      break;
    default:
      break;
  }
}

void drawSoundBars(int phase) {  // 聆听:两侧声波条轮流起伏
  clearDeco();
  static const int H[3][3] = {{8, 14, 20}, {14, 20, 8}, {20, 8, 14}};
  for (int k = 0; k < 3; k++) {
    const int h = H[phase % 3][k];
    const int y = DECO_Y + 20 - h / 2;
    display.fillRect(DECO_RX + 4 + 10 * k, y, 5, h, C_WATER);
    display.fillRect(DECO_LX + 26 - 10 * k, y, 5, h, C_WATER);
  }
}

void drawThinkDots(int count) {  // 思考:右侧 1~3 个点
  restoreSceneRect(DECO_RX, DECO_Y + 12, DECO_W, 16);
  for (int k = 0; k < count; k++) display.fillCircle(DECO_RX + 8 + 10 * k, DECO_Y + 20, 3, TFT_WHITE);
}

void drawZzz(int phase) {  // 睡觉:右侧 z 依次变大
  clearDeco();
  const int n = phase % 4;
  if (n >= 1) drawGlyph("z", DECO_RX + 4, DECO_Y + 24, 1, TFT_LIGHTGREY);
  if (n >= 2) drawGlyph("z", DECO_RX + 12, DECO_Y + 14, 2, TFT_LIGHTGREY);
  if (n >= 3) drawGlyph("Z", DECO_RX + 20, DECO_Y + 4, 3, TFT_LIGHTGREY);
}

void drawBubbleFrame(int x, int y, int w, int h) {
  // 实体纯色填充 (阶梯圆角无缝填充, 绝无缺失孔洞)
  display.fillRect(x + 3, y + 1, w - 6, h - 2, C_BUBBLE);
  display.fillRect(x + 1, y + 3, 2, h - 6, C_BUBBLE);
  display.fillRect(x + w - 3, y + 3, 2, h - 6, C_BUBBLE);
  display.drawPixel(x + 2, y + 2, C_BUBBLE);
  display.drawPixel(x + w - 3, y + 2, C_BUBBLE);
  display.drawPixel(x + 2, y + h - 3, C_BUBBLE);
  display.drawPixel(x + w - 3, y + h - 3, C_BUBBLE);

  // 像素描边 (C_EDGE)
  display.drawFastHLine(x + 3, y, w - 6, C_EDGE);
  display.drawFastHLine(x + 3, y + h - 1, w - 6, C_EDGE);
  display.drawFastVLine(x, y + 3, h - 6, C_EDGE);
  display.drawFastVLine(x + w - 1, y + 3, h - 6, C_EDGE);

  // 四角阶梯像素
  display.drawPixel(x + 2, y + 1, C_EDGE);
  display.drawPixel(x + 1, y + 2, C_EDGE);
  display.drawPixel(x + w - 3, y + 1, C_EDGE);
  display.drawPixel(x + w - 2, y + 2, C_EDGE);
  display.drawPixel(x + 2, y + h - 2, C_EDGE);
  display.drawPixel(x + 1, y + h - 3, C_EDGE);
  display.drawPixel(x + w - 3, y + h - 2, C_EDGE);
  display.drawPixel(x + w - 2, y + h - 3, C_EDGE);

  // 气泡尾巴 (指向猫咪额头中心 160, 尖端严格落在 y+h+6 <= 93, 绝不侵入 PET_FACE_Y=94 面部切片区!)
  const int tx = 160, ty = y + h - 1;
  display.fillTriangle(tx - 7, ty, tx + 7, ty, tx, ty + 7, C_BUBBLE);
  display.drawLine(tx - 7, ty, tx, ty + 7, C_EDGE);
  display.drawLine(tx + 7, ty, tx, ty + 7, C_EDGE);
  display.drawFastHLine(tx - 6, ty, 13, C_BUBBLE);  // 抹平气泡底部与尾巴连接处的线
}

// 按最大视觉宽度(像素)安全截断 UTF-8 字符串，防止长文本冲破气泡底边
static String fitBubbleText(const String& s, int maxLines, int linePx = 264) {
  const uint8_t* p = (const uint8_t*)s.c_str();
  int curLine = 1;
  int cx = 0;
  size_t pos = 0;
  while (p[pos]) {
    if (p[pos] == '\n') {
      if (curLine >= maxLines) break;
      curLine++;
      cx = 0;
      pos++;
      continue;
    }
    int charW = 8;
    size_t step = 1;
    if (p[pos] >= 0xF0) {
      while (step < 4 && p[pos + step] != 0 && (p[pos + step] & 0xC0) == 0x80) step++;
      charW = 0;
    } else if (p[pos] >= 0xE0) {
      if ((p[pos + 1] & 0xC0) == 0x80 && (p[pos + 2] & 0xC0) == 0x80) step = 3;
      charW = 16;
    } else if (p[pos] >= 0xC0) {
      if ((p[pos + 1] & 0xC0) == 0x80) step = 2;
      charW = 16;
    } else {
      charW = 8;
    }
    if (cx + charW > linePx) {
      if (curLine >= maxLines) break;
      curLine++;
      cx = 0;
    }
    cx += charW;
    pos += step;
  }
  if (!p[pos]) return s;
  String out = "";
  for (size_t i = 0; i < pos; i++) out += (char)p[i];
  return out;
}

void drawBubbleText(const String& t1, uint16_t c1, const String& t2, uint16_t c2) {
  // 气泡主体 y=4..86 (h=83), 尾巴尖端 ty+7 = 86+7 = 93 (刚好在 PET_FACE_Y=94 上方 1px, 永不被 drawFeatures 截断!)
  drawBubbleFrame(14, 4, 292, 83);
  if (!t2.length()) {
    const String safe1 = fitBubbleText(t1, 4);
    drawTextCJK(safe1, 26, 12, 290, c1, C_BUBBLE);
    return;
  }
  // 当 t2 较长(>2行, >96字节)时采用紧凑排布, 容纳 1行问题 + 3行回答 且不溢出底边(y=86)
  if (t2.length() > 96) {
    const String safe1 = fitBubbleText(t1, 1);
    drawTextCJK(safe1, 26, 8, 290, c1, C_BUBBLE);
    display.drawFastHLine(26, 26, 268, 0xD6B8);
    const String safe2 = fitBubbleText(t2, 3);
    drawTextCJK(safe2, 26, 29, 290, c2, C_BUBBLE);
  } else {
    const String safe1 = fitBubbleText(t1, 2);
    const int y = drawTextCJK(safe1, 26, 12, 290, c1, C_BUBBLE);
    if (y < 42) {
      display.drawFastHLine(26, 38, 268, 0xD6B8);
      const String safe2 = fitBubbleText(t2, 2);
      drawTextCJK(safe2, 26, 44, 290, c2, C_BUBBLE);
    } else {
      const String safe2 = fitBubbleText(t2, 1);
      drawTextCJK(safe2, 26, y + 2, 290, c2, C_BUBBLE);
    }
  }
}
