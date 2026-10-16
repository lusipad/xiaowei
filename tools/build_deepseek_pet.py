#!/usr/bin/env python3
"""生成 DeepSeek 鲨鱼娘 (鲸鱼娘 / 大肥鱼) 的 14 种原画级动漫表情切片与预览图"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_IMG_PATH = os.path.join(ROOT, "art", "deepseek_maid_room_base.png")
SRC_BASE = BASE_IMG_PATH

base = Image.open(BASE_IMG_PATH).convert("RGB")

FX0, FY0 = 138, 112
FW, FH = 64, 32
FX1, FY1 = FX0 + FW, FY0 + FH

default_patch = base.crop((FX0, FY0, FX1, FY1))

alignments = {
    'happy': (111, 56),
    'tsundere_pout': (113, 56),
    'listen': (113, 56),
    'excited': (110, 56),
    'surprised': (112, 56),
    'shy': (112, 56),
    'wink': (113, 56),
    'yawn': (113, 56),
    'dizzy': (113, 56)
}

scale = 0.3243

def get_face_patch(name, dx, dy):
    exp_path = os.path.join(ROOT, "art", f"exp_{name}.png")
    exp = Image.open(exp_path).convert("RGB")
    nw = int(round(exp.width * scale))
    nh = int(round(exp.height * scale))
    scaled = exp.resize((nw, nh), Image.Resampling.LANCZOS)
    px = 138 - dx
    py = 112 - dy
    patch = scaled.crop((px, py, px + FW, py + FH))
    return patch

def clean_patch(p):
    """精准锁定发丝、额头刘海与右下角下颌头发边缘，彻底杜绝切片直角撕裂与崩坏"""
    res = p.copy()
    for y in range(FH):
        for x in range(FW):
            r0, g0, b0 = default_patch.getpixel((x, y))
            is_hair = False
            # 1. 左侧头发与鬓角
            if x < 13: is_hair = True
            # 2. 右侧头发与鬓角
            if x > 56: is_hair = True
            # 3. 额头顶部刘海 (y < 7)
            if y < 7 and (b0 > 60 and r0 < 130): is_hair = True
            # 4. 额头中分刘海发丝 (y <= 11)
            if y <= 11 and (26 <= x <= 40):
                if (b0 > 60 and r0 < 130): is_hair = True
            # 5. 右下角下颌/后颈头发 (x >= 45 and y >= 22) —— 彻底杜绝右下角方块肉色崩坏
            if x >= 45 and y >= 22:
                if (b0 > 60 and r0 < 130) or (r0 < 60 and g0 < 60 and b0 < 80):
                    is_hair = True
            # 6. 下巴与衣领边缘 (y >= 30 且两侧)
            if y >= 30 and (x < 24 or x > 40):
                is_hair = True
                
            if is_hair:
                res.putpixel((x, y), (r0, g0, b0))
    return res

raw_patches = {}
for k, (dx, dy) in alignments.items():
    raw_patches[k] = clean_patch(get_face_patch(k, dx, dy))

faces = {}

# 0. F_NORMAL: 原版基础萌态
faces['normal'] = default_patch.copy()

# 1. F_BLINK: 眨眼 (自然弧形闭目睫毛 + 常态猫猫嘴)
f_blink = raw_patches['happy'].copy()
for y in range(21, 32):
    for x in range(26, 46):
        f_blink.putpixel((x, y), default_patch.getpixel((x, y)))
faces['blink'] = f_blink

# 2. F_HAPPY: 开心 (笑眼弯弯 + 可爱猫猫嘴 :3 + 腮红)
faces['happy'] = raw_patches['happy'].copy()

# 3. F_SAD: 委屈/傲娇生气 (经典原画“你不诚实！”鼓腮帮皱眉表情)
faces['sad'] = raw_patches['tsundere_pout'].copy()

# 4. F_LISTEN: 认真倾听 (大眼睛好奇注视)
faces['listen'] = raw_patches['listen'].copy()

# 5. F_THINK: 思考 (向上专注眼神)
faces['think'] = raw_patches['listen'].copy()

# 6. F_EXCITED: 兴奋超燃 (晶莹四角星钻石眼 + 欢呼大张嘴)
faces['excited'] = raw_patches['excited'].copy()

# 7. F_SURPRISED: 震惊惊讶 (动漫呆萌圆溜溜眼 + 小张嘴)
faces['surprised'] = raw_patches['surprised'].copy()

# 8. F_SHY: 害羞侧首 (水灵蓝眸向下看 + 满脸粉红红晕)
faces['shy'] = raw_patches['shy'].copy()

# 9. F_CONFUSED: 困惑/调皮 (单眼眨眼 + 吐小舌头 :P)
faces['confused'] = raw_patches['wink'].copy()

# 10. F_SLEEPY: 困倦半眯 (耷拉沉重眼皮 + 迷糊嘴)
faces['sleepy'] = raw_patches['yawn'].copy()

# 11. F_SLEEP: 安详熟睡 (温柔闭眼睫毛弧度 + 恬静微笑)
f_sleep = raw_patches['happy'].copy()
for y in range(22, 32):
    for x in range(26, 46):
        f_sleep.putpixel((x, y), default_patch.getpixel((x, y)))
faces['sleep'] = f_sleep

# 12. F_DIZZY: 蚊香眼 (经典螺旋圈圈眼 @ @ + 飘忽波浪嘴)
faces['dizzy'] = raw_patches['dizzy'].copy()

# 13. F_YAWN: 打哈欠 (眯眼大哈欠嘴)
faces['yawn'] = raw_patches['yawn'].copy()

face_names = [
    'normal', 'blink', 'happy', 'sad', 'listen', 'think',
    'excited', 'surprised', 'shy', 'confused', 'sleepy', 'sleep', 'dizzy', 'yawn'
]

# 保存 14 个表情对比拼图
preview_w = FW * 2 + 15
preview_h = (FH + 4) * 7 + 10
preview = Image.new('RGB', (preview_w, preview_h), (230, 235, 245))
for i, name in enumerate(face_names):
    col = i % 2
    row = i // 2
    px = 5 + col * (FW + 5)
    py = 5 + row * (FH + 4)
    preview.paste(faces[name], (px, py))

preview_path = os.path.join(ROOT, "art", "deepseek_faces_preview.png")
preview.save(preview_path)
print(f"成功生成 14 个动漫级表情，拼图已保存至: {preview_path}")

# 保存场景合成图
for name in ['happy', 'sad', 'excited', 'confused', 'dizzy']:
    sc = base.copy()
    sc.paste(faces[name], (FX0, FY0))
    sc.save(os.path.join(ROOT, "art", f"preview_scene_{name}.png"))
print("已保存 5 种场景合成预览图")
