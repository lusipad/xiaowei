#!/usr/bin/env python3
"""一键皮肤切换工具: 在 'cat' (小维小猫) 与 'deepseek' (大肥鱼鲸鱼娘) 之间自由切换并自动烧录"""
import os
import sys
import subprocess
import argparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "pet", "pet_scene_data.h")
INO_PATH = os.path.join(ROOT, "pet", "pet.ino")
FACE_H_PATH = os.path.join(ROOT, "pet", "pet_face.h")

def export_assets(skin_type):
    from PIL import Image
    if skin_type == "cat":
        import tools.build_pixel_pet as bpp
        fx, fy, fw, fh = bpp.FX0, bpp.FY0, bpp.FW, bpp.FH
    else:
        import tools.build_deepseek_pet as bpp
        fx, fy, fw, fh = bpp.FX0, bpp.FY0, bpp.FW, bpp.FH

    base_img = Image.open(bpp.SRC_BASE).convert("RGB")
    
    # 联合生成 256 色全局调色板
    comp_w = base_img.width + 100
    comp_h = max(base_img.height, len(bpp.face_names) * (fh + 4))
    composite = Image.new("RGB", (comp_w, comp_h))
    composite.paste(base_img, (0, 0))
    for idx, k in enumerate(bpp.face_names):
        composite.paste(bpp.faces[k], (base_img.width + 5, idx * (fh + 4)))

    q_comp = composite.quantize(256, method=Image.Quantize.MEDIANCUT)
    pal_rgb = q_comp.getpalette()[:768]

    pal_565 = []
    for i in range(256):
        r, g, b = pal_rgb[i*3 : i*3+3]
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        pal_565.append(rgb565)

    pal_template = Image.new("P", (1, 1))
    pal_template.putpalette(pal_rgb + [0]*(768 - len(pal_rgb)))

    q_base = base_img.quantize(palette=pal_template, dither=Image.Dither.NONE)
    base_indices = list(q_base.getdata())

    face_indices = {}
    for k in bpp.face_names:
        f_img = bpp.faces[k].convert("RGB")
        f_q = f_img.quantize(palette=pal_template, dither=Image.Dither.NONE)
        face_indices[k] = list(f_q.getdata())

    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write(f"// 自动生成的像素宠物场景与表情数据 ({skin_type})\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write(f"const int PET_FACE_X = {fx};\n")
        f.write(f"const int PET_FACE_Y = {fy};\n")
        f.write(f"const int PET_FACE_W = {fw};\n")
        f.write(f"const int PET_FACE_H = {fh};\n\n")

        f.write(f'const char* const PET_SKIN_NAME = "{skin_type}";\n')
        if skin_type == "cat":
            f.write('const char* const PET_CHARACTER_NAME = "小维";\n')
            f.write('const uint16_t C_BOTTOM_BG = 0x0862;\n')
            f.write('const char* const PET_PROMPT =\n'
                    '    "你是电子桌宠\\"小维\\",性格活泼元气,说话简短、口语化、爱用感叹号。"\n'
                    '    "回答必须以情绪标签开头,格式如 [开心],情绪只能从 开心/兴奋/惊讶/害羞/疑惑/难过 里选一个;"\n'
                    '    "标签后直接是回答正文,用简体中文,45字以内,不要使用Emoji表情,不要使用Markdown符号(如*、#),直接说纯文本。";\n\n')
        else:
            f.write('const char* const PET_CHARACTER_NAME = "大肥鱼";\n')
            f.write('const uint16_t C_BOTTOM_BG = 0x2906;\n')
            f.write('const char* const PET_PROMPT =\n'
                    '    "你是DeepSeek女仆鲸鱼娘\\"大肥鱼\\",头上顶着呆毛与鲸鱼鳍,穿着印着小鲸鱼围裙的女仆装。"\n'
                    '    "性格傲娇可爱,爱摸鱼偷吃Token算力,经典口癖是\\"你不诚实!\\"。说话简短活泼口语化。"\n'
                    '    "回答必须以情绪标签开头,格式如 [开心],情绪只能从 开心/兴奋/惊讶/害羞/疑惑/难过 里选一个;"\n'
                    '    "标签后直接是回答正文,用简体中文,45字以内,不要使用Emoji表情,不要使用Markdown符号(如*、#),直接说纯文本。";\n\n')

        f.write("const uint16_t PET_PALETTE[256] = {\n  ")
        for i, c in enumerate(pal_565):
            f.write(f"0x{c:04X}, ")
            if (i + 1) % 12 == 0: f.write("\n  ")
        f.write("\n};\n\n")

        f.write("const uint8_t PET_SCENE_BG[76800] = {\n  ")
        for i, v in enumerate(base_indices):
            f.write(f"{v}, ")
            if (i + 1) % 32 == 0: f.write("\n  ")
        f.write("\n};\n\n")

        f.write(f"const uint8_t PET_FACES[14][{fw * fh}] = {{\n")
        for name in bpp.face_names:
            f.write(f"  // {name}\n  {{\n    ")
            for i, v in enumerate(face_indices[name]):
                f.write(f"{v}, ")
                if (i + 1) % 32 == 0: f.write("\n    ")
            f.write("\n  },\n")
        f.write("};\n\n")
    print(f"[OK] 皮肤素材已导出: {skin_type} ({fw}x{fh} 面部切片)")

def main():
    parser = argparse.ArgumentParser(description="Switch pet skin between cat and deepseek")
    parser.add_argument("skin", choices=["cat", "deepseek"], help="Skin name: 'cat' or 'deepseek'")
    parser.add_argument("--upload", action="store_true", help="Compile and upload to COM3 automatically")
    parser.add_argument("-p", "--port", default="COM3", help="Serial port for upload (default: COM3)")
    args = parser.parse_args()

    sys.path.insert(0, ROOT)
    export_assets(args.skin)


    if args.upload:
        print("[..] 正在编译固件...")
        cmd_compile = [
            "arduino-cli", "compile",
            "--fqbn", "Seeeduino:samd:seeed_wio_terminal",
            "--libraries", "libraries", "pet"
        ]
        ret = subprocess.run(cmd_compile, cwd=ROOT)
        if ret.returncode != 0:
            print("[FAIL] 编译失败")
            sys.exit(1)
        print("[..] 正在烧录到设备", args.port, "...")
        cmd_upload = [
            "arduino-cli", "upload",
            "-p", args.port,
            "--fqbn", "Seeeduino:samd:seeed_wio_terminal", "pet"
        ]
        ret = subprocess.run(cmd_upload, cwd=ROOT)
        if ret.returncode == 0:
            print(f"[SUCCESS] 皮肤已成功切换为 {args.skin} 并烧录到设备！")
        else:
            print("[FAIL] 烧录失败")
            sys.exit(1)

if __name__ == "__main__":
    main()
