#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 HZK16.bin 生成板载中文字库头文件(单文件,点阵 + 索引):
   - libraries/WioKit/src/WioKitFontHz16.h
     · HZ16_FONT : 点阵数据(GB2312 区1-3 符号/全角标点 + 区16-55 一级汉字)
     · HZ16_INDEX: UTF-8(3字节) -> GB2312(2字节) 索引,按 UTF-8 排序供二分查找
   解码用 GBK 超集(让 — 等映射进区1),但只收录 HZK16 有字形的区。
   消费方:WioKitCjk.cpp(WioKit 库)。console/ 仍持有旧拷贝,不随本脚本再生。
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "HZK16.bin")                       # 源字库
REPO = os.path.dirname(HERE)                                # 仓库根
OUT_PATH = os.path.join(REPO, "libraries", "WioKit", "src", "WioKitFontHz16.h")

data = open(SRC, "rb").read()
assert len(data) == 261696, f"unexpected HZK16 size {len(data)}"

# 嵌入的区:1(标点) 2(带圈符号) 3(全角ASCII/全角标点) 16-55(一级汉字)
ZONES = [1, 2, 3] + list(range(16, 56))
entries = []  # (utf8_bytes, gb_bytes)
glyphs = bytearray()


def blob_index(qu, wei):
    """glyph 在生成 blob 中的序号(单位:32字节)"""
    if qu in (1, 2, 3):
        return (qu - 1) * 94 + (wei - 1)
    return 94 * 3 + (qu - 16) * 94 + (wei - 1)


for qu in ZONES:
    for wei in range(1, 95):
        gb = bytes([0xA0 + qu, 0xA0 + wei])
        off = ((gb[0] - 0xA0 - 1) * 94 + (gb[1] - 0xA0 - 1)) * 32
        # 始终写入真实的 32 字节点阵(包括 ×, ÷, °, · 等 2 字节符号)
        glyphs.extend(data[off:off + 32])
        try:
            ch = gb.decode("gbk")
            u8 = ch.encode("utf-8")
        except UnicodeDecodeError:
            continue
        if len(u8) == 3:  # 3 字节 UTF-8 加入定长二分索引
            entries.append((u8, gb))

entries.sort(key=lambda e: e[0])

# ---- WioKitFontHz16.h(点阵 + 索引) ----
with open(OUT_PATH, "w", newline="\n") as f:
    f.write("// 自动生成:HZK16 子集字库 + UTF-8->GB2312 索引(tools/gen_font.py 生成,勿手改)\n")
    f.write("// 点阵:GB2312 区1-3 符号/全角标点 + 区16-55 一级汉字(共 %d 字形 x 32B)\n" % (len(glyphs) // 32))
    f.write("// 索引:按 UTF-8 排序供二分查找,每项 5 字节 utf8[0..2] + gb[0] + gb[1]\n")
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write(f"#define HZ16_GLYPH_COUNT {len(glyphs) // 32}\n")
    f.write(f"#define HZ16_INDEX_COUNT {len(entries)}\n\n")
    f.write("// 索引: qu 1-3 -> (qu-1)*94 + (wei-1); 16<=qu<=55 -> 282 + (qu-16)*94 + (wei-1) (单位:32字节)\n")
    f.write("const uint8_t HZ16_FONT[] = {\n")
    for i in range(0, len(glyphs), 16):
        f.write("  " + ",".join(f"0x{b:02X}" for b in glyphs[i:i + 16]) + ",\n")
    f.write("};\n\n")
    f.write("// 每项 5 字节: utf8[0..2] + gb[0] + gb[1]\n")
    f.write("const uint8_t HZ16_INDEX[] = {\n")
    flat = bytearray()
    for u8, gb in entries:
        flat.extend(u8)
        flat.extend(gb)
    for i in range(0, len(flat), 15):
        f.write("  " + ",".join(f"0x{b:02X}" for b in flat[i:i + 15]) + ",\n")
    f.write("};\n\n")

    # 常用高频现代汉语二级字补充表 (涵盖拟声、口语、情绪、动作、城市、美食、动植物，按 UTF-8 排序供二分查找)
    EXTRA_CHARS_STR = (
        "嗯咦咧啵啾唔喽噢呗唷咚嗦叽嘀嗒喵咪哒嗨嗷噜尴尬啧喔喏咻嚓哐啷淅咔哧噗嗤"
        "聆瞰瞌盹慵惬馨潇逍璀璨斓皎朦胧冽煦嘈漪徜徉蜿岖妩窈窕珑诙腼炫飒懵愣忐忑"
        "憧憬唠叨嘟囔哽啜嗝蜷挲偎舐腌涮炖煲焖饪瘠圳莞衢濮漯泸莓枇杷猕柚橘葚橄榄"
        "槟淇豉噌荞蛳馍馄饨粽糍粑喱暹玳瑁豚猬貂鼯鹦鹉鹭隼鳄蜥蜴蟒蟾蜍蟆蝌蚪蝾螈"
        "鲨鱿螃蛎蚝蜊蛞蝓蚯蚓蟥蜈蚣蜻蜓螳螂蟋蟀蝈蚱蜢虻蟑茉栀薰罂荟藓蕨榕桦楠枸"
        "杞芪茯苓藿睑龈踝痦痣痱荨翡瑙琥珀玺曜岫嵩鄱俑滕鹳婺湄撸黏怂怼霾"
    )
    EXTRA_WORDS = list(dict.fromkeys(EXTRA_CHARS_STR))
    extra_entries = []
    for w in EXTRA_WORDS:
        gb = w.encode('gb2312')
        qu = gb[0] - 0xa0
        wei = gb[1] - 0xa0
        if qu < 56:
            continue
        u8 = w.encode('utf-8')
        off = ((qu - 1) * 94 + (wei - 1)) * 32
        glyph = data[off:off+32]
        extra_entries.append((u8, glyph, w))

    extra_entries.sort(key=lambda e: e[0])

    f.write(f"#define HZ16_EXTRA_COUNT {len(extra_entries)}\n")
    f.write("struct Hz16ExtraGlyph {\n  uint8_t utf8[3];\n  uint8_t glyph[32];\n};\n\n")
    f.write("const Hz16ExtraGlyph HZ16_EXTRA[HZ16_EXTRA_COUNT] = {\n")
    for u8, glyph, w in extra_entries:
        u8_hex = ", ".join(f"0x{b:02X}" for b in u8)
        glyph_hex = ", ".join(f"0x{b:02X}" for b in glyph)
        f.write(f"  {{ {{{u8_hex}}}, {{{glyph_hex}}} }}, // {w}\n")
    f.write("};\n")

print(f"WioKitFontHz16.h: {len(glyphs) // 32} glyphs, {len(entries)} entries, {len(extra_entries)} extra pet glyphs")
