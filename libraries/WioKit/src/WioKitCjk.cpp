#include "WioKitCjk.h"
#include "WioKitFontHz16.h"

static Seeed_GFX* cjkDisp = nullptr;

void wioCjkBegin(Seeed_GFX& d) { cjkDisp = &d; }

// 二分查找 UTF-8(3 字节) -> GB2312(2 字节)
static bool utf8ToGb(const uint8_t* u, uint8_t gbOut[2]) {
  int lo = 0, hi = HZ16_INDEX_COUNT - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    const uint8_t* e = &HZ16_INDEX[mid * 5];
    const int cmp = memcmp(u, e, 3);
    if (cmp == 0) { gbOut[0] = e[3]; gbOut[1] = e[4]; return true; }
    if (cmp < 0) hi = mid - 1; else lo = mid + 1;
  }
  return false;
}

// 二分查找高频补充二级字模 (已按 UTF-8 排序, 共 205 个常用二级字如 嗯, 聆, 喵, 咪, 圳 等)
static const uint8_t* findExtraGlyph(const uint8_t* u8) {
  int lo = 0, hi = (int)HZ16_EXTRA_COUNT - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    const int cmp = memcmp(u8, HZ16_EXTRA[mid].utf8, 3);
    if (cmp == 0) return HZ16_EXTRA[mid].glyph;
    if (cmp < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return nullptr;
}

// 性能:16x16 字形拼进行缓冲(512B 栈数组),pushImage 按行批量推送。
static void drawGlyph32(const uint8_t* g, int x, int y, uint16_t color, uint16_t bg) {
  uint16_t line[256];
  for (int row = 0; row < 16; row++) {
    uint16_t* out = &line[row * 16];
    const uint8_t hi = g[row * 2], lo = g[row * 2 + 1];
    for (int col = 0; col < 8; col++) {
      out[col] = ((hi << col) & 0x80) ? color : bg;
      out[col + 8] = ((lo << col) & 0x80) ? color : bg;
    }
  }
  cjkDisp->pushImage(x, y, 16, 16, line);
}

static void drawHZ16ByQuWei(int qu, int wei, int x, int y, uint16_t color, uint16_t bg) {
  uint32_t idx;
  if (qu >= 1 && qu <= 3 && wei >= 1 && wei <= 94) idx = (uint32_t)(qu - 1) * 94 + (wei - 1);
  else if (qu >= 16 && qu <= 55 && wei >= 1 && wei <= 94) idx = 94 * 3 + (uint32_t)(qu - 16) * 94 + (wei - 1);
  else { cjkDisp->drawRect(x, y, 15, 15, color); return; }

  drawGlyph32(&HZ16_FONT[idx * 32], x, y, color, bg);
}

static bool drawHZ16IfFound(const uint8_t* u, int x, int y, uint16_t color, uint16_t bg) {
  uint8_t gb[2];
  if (utf8ToGb(u, gb)) {
    drawHZ16ByQuWei(gb[0] - 0xA0, gb[1] - 0xA0, x, y, color, bg);
    return true;
  }
  const uint8_t* extra = findExtraGlyph(u);
  if (extra) {
    drawGlyph32(extra, x, y, color, bg);
    return true;
  }
  return false;
}

// 中英混排绘制(ASCII 走 16 像素高 Font 2, CJK 16x16, 常用2字节数学符号与高频二级字), 自动折行
int drawTextCJK(const String& s, int x, int y, int maxX, uint16_t color, uint16_t bg) {
  if (!cjkDisp) return y;
  cjkDisp->setTextSize(1);  // 严格强制 1:1 缩放，杜绝 ASCII 字符被外部 textSize 放大
  int cx = x, cy = y;
  const uint8_t* p = (const uint8_t*)s.c_str();
  while (*p) {
    if (*p == '\n') { cx = x; cy += 18; p++; continue; }

    // ---- 4 字节 UTF-8 (Emoji 表情，U+10000 及以上) ----
    if (p[0] >= 0xF0) {
      int step = 1;
      while (step < 4 && p[step] != 0 && (p[step] & 0xC0) == 0x80) step++;
      p += step;  // 安全跳过 Emoji，绝不越过字符串末尾 '\0'
      continue;
    }

    // ---- 1 字节 ASCII 字符 (< 0x80) ----
    if (*p < 0x80) {
      if (*p == ' ') {
        if (cx + 6 > maxX) { cx = x; cy += 18; }
        cjkDisp->fillRect(cx, cy, 6, 16, bg);
        cx += 6;
      } else if (*p >= 32 && *p <= 126) {
        cjkDisp->setTextSize(1);
        char buf[2] = {(char)*p, 0};
        int w = cjkDisp->textWidth(buf, 2);
        if (w <= 0) w = 8;
        if (cx + w > maxX) { cx = x; cy += 18; }
        cjkDisp->setTextColor(color, bg);
        cjkDisp->drawChar(*p, cx, cy, 2);  // Font 2 = 16 像素高字符 (x, /, 数字, 字母等高度严格与中文对齐)
        cx += w;
      } else {
        cx += 6;
      }
      p++;
    }
    // ---- 3 字节 UTF-8 汉字 / 全角标点 / 3字节符号 ----
    else if (p[0] >= 0xE0 && p[0] < 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      const uint16_t u16 = (uint16_t)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
      // 跳过零宽控制符(U+200B..U+200F, 如 ZWJ)、Emoji 变体选择符(U+FE00..U+FE0F)与 BOM(U+FEFF)
      if ((u16 >= 0x200B && u16 <= 0x200F) || (u16 >= 0xFE00 && u16 <= 0xFE0F) || u16 == 0xFEFF) {
        p += 3;
        continue;
      }
      if (cx + 16 > maxX) { cx = x; cy += 18; }
      if (drawHZ16IfFound(p, cx, cy, color, bg)) {
        cx += 16;
      } else if (u16 >= 0x2600 && u16 <= 0x27BF) {
        // 未收录的 3 字节杂项 Emoji/符号(如 ✨ ❤️ ☀️ ☁️ 等)静默跳过, 避免画成刺眼的缺字方框
      } else {
        cjkDisp->drawRect(cx, cy, 15, 15, color);  // 缺字占位框
        cx += 16;
      }
      p += 3;
    }
    // ---- 2 字节 UTF-8 常用数学/标点符号 (×, ÷, °, ·, ±, § 等) ----
    else if (p[0] >= 0xC0 && p[0] < 0xE0 && (p[1] & 0xC0) == 0x80) {
      uint16_t u16 = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      int qu = 0, wei = 0;
      if (u16 == 0x00D7) { qu = 1; wei = 33; }       // × 乘号 (U+00D7)
      else if (u16 == 0x00F7) { qu = 1; wei = 34; }  // ÷ 除号 (U+00F7)
      else if (u16 == 0x00B0) { qu = 1; wei = 67; }  // ° 度数 (U+00B0)
      else if (u16 == 0x00B7) { qu = 1; wei = 4; }   // · 间隔号 (U+00B7)
      else if (u16 == 0x00B1) { qu = 1; wei = 32; }  // ± 正负号 (U+00B1)
      else if (u16 == 0x00A7) { qu = 1; wei = 76; }  // § 章节号 (U+00A7)

      if (qu != 0) {
        if (cx + 16 > maxX) { cx = x; cy += 18; }
        drawHZ16ByQuWei(qu, wei, cx, cy, color, bg);
        cx += 16;
      } else {
        char buf[2] = {'?', 0};
        int w = cjkDisp->textWidth(buf, 2);
        if (cx + w > maxX) { cx = x; cy += 18; }
        cjkDisp->setTextColor(color, bg);
        cjkDisp->drawChar('?', cx, cy, 2);
        cx += (w > 0 ? w : 8);
      }
      p += 2;
    }
    else {
      p++;  // 畸形字节跳过
    }
  }
  return cy + 18;
}
