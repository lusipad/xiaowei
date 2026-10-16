#!/usr/bin/env python3
"""打包皮肤二进制文件供写入 Wio Terminal 板载 4MB QSPI Flash"""
import os
import sys
import struct
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import tools.build_deepseek_pet as bdp

def build_deepseek_binary():
    base_img = Image.open(bdp.SRC_BASE).convert("RGB")
    comp_w = base_img.width + 100
    comp_h = max(base_img.height, len(bdp.face_names) * (bdp.FH + 4))
    comp = Image.new("RGB", (comp_w, comp_h))
    comp.paste(base_img, (0, 0))
    for idx, k in enumerate(bdp.face_names):
        comp.paste(bdp.faces[k], (base_img.width + 5, idx * (bdp.FH + 4)))

    q_comp = comp.quantize(256, method=Image.Quantize.MEDIANCUT)
    pal_rgb = q_comp.getpalette()[:768]
    pal_565 = bytearray()
    for i in range(256):
        r, g, b = pal_rgb[i*3:i*3+3]
        c565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        pal_565.extend(struct.pack("<H", c565))

    pal_template = Image.new("P", (1, 1))
    pal_template.putpalette(pal_rgb + [0]*(768 - len(pal_rgb)))

    q_base = base_img.quantize(palette=pal_template, dither=Image.Dither.NONE)
    bg_bytes = bytes(q_base.getdata())

    face_bytes = bytearray()
    for k in bdp.face_names:
        f_q = bdp.faces[k].convert("RGB").quantize(palette=pal_template, dither=Image.Dither.NONE)
        face_bytes.extend(bytes(f_q.getdata()))

    prompt = (
        '你是DeepSeek女仆鲸鱼娘"大肥鱼",头上顶着呆毛与鲸鱼鳍,穿着印着小鲸鱼围裙的女仆装。'
        '性格傲娇可爱,爱摸鱼偷吃Token算力,经典口癖是"你不诚实!"。说话简短活泼口语化。'
        '回答必须以情绪标签开头,格式如 [开心],情绪只能从 开心/兴奋/惊讶/害羞/疑惑/难过 里选一个;'
        '标签后直接是回答正文,用简体中文,45字以内,不要使用Emoji表情,不要使用Markdown符号(如*、#),直接说纯文本。'
    ).encode("utf-8")

    # Header layout (1024 bytes aligned):
    # offset 0: magic (0x534B494E)
    # offset 4: version (1)
    # offset 8: skinId (16 bytes)
    # offset 24: charName (16 bytes)
    # offset 40: bottomBg (uint16_t)
    # offset 42: faceX (int16_t)
    # offset 44: faceY (int16_t)
    # offset 46: faceW (int16_t)
    # offset 48: faceH (int16_t)
    # offset 52: paletteOffset (uint32_t)
    # offset 56: facesOffset (uint32_t)
    # offset 60: faceSize (uint32_t)
    # offset 64: bgOffset (uint32_t)
    # offset 68: totalSize (uint32_t)
    # offset 72: prompt (512 bytes)
    hdr = bytearray(1024)
    struct.pack_into("<II", hdr, 0, 0x534B494E, 1)
    hdr[8:8+len("deepseek")] = b"deepseek"
    name_bytes = "大肥鱼".encode("utf-8")
    hdr[24:24+len(name_bytes)] = name_bytes
    struct.pack_into("<Hhhhh", hdr, 40, 0x2906, bdp.FX0, bdp.FY0, bdp.FW, bdp.FH)

    pal_off = 1024
    faces_off = pal_off + len(pal_565)
    face_sz = bdp.FW * bdp.FH
    bg_off = faces_off + len(face_bytes)
    total_sz = bg_off + len(bg_bytes)
    struct.pack_into("<IIIII", hdr, 52, pal_off, faces_off, face_sz, bg_off, total_sz)
    hdr[72:72+len(prompt)] = prompt

    full_pkg = bytes(hdr) + bytes(pal_565) + bytes(face_bytes) + bg_bytes
    out_path = os.path.join(ROOT, "art", "skin_deepseek.bin")
    with open(out_path, "wb") as f:
        f.write(full_pkg)
    print(f"[OK] DeepSeek 皮肤包生成成功: {len(full_pkg)} 字节 ({len(full_pkg)/1024:.1f} KB) -> {out_path}")
    return full_pkg

if __name__ == "__main__":
    build_deepseek_binary()
