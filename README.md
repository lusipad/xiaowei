# 小维 XiaoWei

> 给 Wio Terminal(SAMD51 + 2.4" 彩屏 + WiFi + 板载麦克风)装上性格,再给它一台 3D 打印的复古小电脑当家:
> **按一下 B 说话,说完自动停 → 百度 ASR 识别 → DeepSeek 回答 → 汉字气泡 + 匹配的表情。**
> 它还会眨眼、张望、哼歌;被摇晃会晕,摸头会开心,没人理会打哈欠、睡着。

<p align="center">
  <img src="cad/preview_render.jpg" width="560" alt="小维复古桌面显示器(STL 直接渲染)"/>
</p>

| 待机 | 开心摸头 | 聆听 | 对话展示 | 沉睡 |
| :---: | :---: | :---: | :---: | :---: |
| ![待机](sim/out/01_idle.png) | ![开心](sim/out/02_pet_happy.png) | ![聆听](sim/out/03_listen.png) | ![对话](sim/out/04_reply_1.png) | ![睡觉](sim/out/10_sleep.png) |

> 外壳图由 `tools/render_preview.py` 直接从 STL 渲染(屏幕内容为示意);屏幕截图为**宿主模拟器**渲染(`bash sim/build.sh` 或 `sim/build.bat`, Windows/MSVC),与固件运行完全同一份表情、动画、汉字渲染及像素场景代码。欢迎补充实机照片/GIF 到 `docs/img/`。

## 玩法

| 动作 | 反应 |
|------|------|
| 按一下 B (或长按 B) | 全局对讲: 点按自动 VAD 截断(1.2s 静音自动发送,最长 3s)或长按说话松手即发; 思考中按 C 随时取消 |
| 按一下 A | 进入**端侧配网模式**(手机连热点 `Wio-Pet`,网页直接配 WiFi、城市与密钥) |
| 摇杆左 / 右 | **三合一多功能看板切换** (像素桌宠房间 ↔ 75px 赛博时钟 ↔ 25分钟番茄钟) |
| 摇杆下压 | 桌宠页**一键切换宠物形象**(小猫「小维」↔ DeepSeek 娘「大肥鱼」); 时钟页网络对时; 番茄钟页启停计时 |
| 摇杆上 / 下 | 桌宠页摸头互动 |
| 桌面平扣 (Flip-to-Focus) | 屏幕自动熄灭进入 25 分钟专注番茄钟; 翻起自动亮屏恢复显示 |
| 摇晃 | 晕 3 秒(蚊香眼),之后冷却 5 秒 |
| 拿起 | 从犯困里打起精神 |
| 3 分钟没人理 / 5 分钟 | 犯困(打哈欠)/ 睡觉(Zzz) |
| 任意按键 | 睡醒 |

- **双主题动态换肤（「小维」像素猫 ↔ 「大肥鱼」DeepSeek 娘）**:
  - 桌宠页下压五向摇杆即可实时换装，自动切换角色视觉、底栏定制双层圆角高光框与专属 AI 人设 System Prompt（元气小猫「小维」 vs 傲娇偷吃算力的女仆鲸鱼娘「大肥鱼」），偏好持久保存在 QSPI Flash 中。
- **三合一桌面多功能看板**:
  - **看板 0（像素桌宠）**: 概念图 100% 还原的复古像素小屋、实时待机动画、摸头互动、换装与 AI 语音对话。
  - **看板 1（赛博时钟）**: 75 像素巨大数码管粗体（Font 8，宽 250px），两米外清晰可读；零闪烁局部直写机制 + 3px 平滑线性秒数进度条，彻底杜绝刷屏与背光频闪；支持 HTTP 自动对齐精确北京时间。
  - **看板 2（25 分钟番茄钟）**: 75 像素大倒计时，专注计时结束自动蜂鸣提示并动态刷新今日专注次数。
- **扣下即专注 (Flip-to-Focus)**: 基于板载 LIS3DHTR 三轴加速度计精密重力标定。将设备正面扣在桌面上自动关闭 LCD 背光并启动番茄钟；翻起即刻亮屏恢复，省电且免打扰。
- **端侧独立配网与持久化**: 随时按顶部 **A 键** 进入配网模式，Wio Terminal 开启 `Wio-Pet` 热点与 Captive Portal 网页。手机直连访问 `192.168.4.1` 图形化选择 WiFi、设置城市与 API 密钥。配置永久存储在板载 **4MB QSPI Flash**（W25Q32JV）中，平时运行 0 额外 RAM 占用。
- **连续多轮对答与高可靠语音链路**: 自动记忆最近 2 轮问答历史（超过 90 秒无互动自动重置）；具备滑动预卷人声检测、首尾 100ms 余量静音裁剪、百度 ASR 识别结果即时上屏预览、C 键秒级中断与 28 秒硬件看门狗兜底。
- **回答与像素动画**: 回答以情绪标签(`[开心]` `[兴奋]` `[惊讶]` `[害羞]` `[疑惑]` `[难过]`)开头,由系统提示词约定、DeepSeek 生成;标签驱动 14 种高精度像素表情,正文以气泡上屏。录音为 16bit@16kHz DMA 采样 + 高通滤波。

## 3D 打印外壳:复古桌面显示器

一台复古 CRT 小显示器坐在"主机盒"上:Wio Terminal 装进显示器,显示器绕背后的铰链 0–45° 后仰,主机盒里预留功放、喇叭、电池和 Grove 模块的位置,正面接口面板可换。全部模型由 `tools/generate_3d_models.py` 参数化生成。

| 正面 | 背面(后仰 25°) | 嘉立创免费版 |
| :---: | :---: | :---: |
| ![正面](cad/preview_render.jpg) | ![背面](cad/preview_render_back.jpg) | ![免费版](cad/preview_render_jlc.jpg) |

| | 完整版 | 嘉立创免费打印版 |
| :--- | :--- | :--- |
| 文件 | `cad/stl/wio_tilt_tv_*.stl`(6 件) | `cad/stl/jlc_free/`(2 件) |
| 零件 | 显示器前壳、CRT 后盖、主机盒、底盖、正面面板、阻尼旋钮 | 显示器一体件、主机盒一体件 |
| 打印体积 | 86.1 cm³(≈99 g,9600 树脂) | 63.99 cm³(≤ 70,满足免费打印限制) |
| Wio 装入 | 从背后插入,后盖压住 | 从顶部插入,背后导轨夹住 |
| 扩展 | 可换正面面板;底盖带电池仓、6 根模块柱、防滑垫槽 | 接口直接开在前壁;底部敞开,模块用胶固定 |
| 阻尼旋钮 | 打印件(内嵌 M3 螺母) | 外购 M3 滚花手拧螺母(外径 ≤ 12mm) |

- **走线**:Wio 底边的 USB-C 和两个 Grove 口正下方开槽,40-Pin 杜邦线从 Wio 背后两侧的孔下去,线缆全部直通主机盒;
- **主机盒**:左侧喇叭格栅 + 3520 腔体喇叭卡槽,右侧散热槽,正面 USB-C / 2×Grove / 拨动开关 / LED 孔位;
- **校验**:`tools/test_cad_models.py` 检查每个零件水密、单壳体、≤ 100mm,并像在线检查器那样按 STL 坐标重读、按 0.001/0.01mm 容差合并顶点后不得出现退化面或坏边;`tools/generate_svg_preview.py` 生成 STL 实测图纸,并对 0–45° 每 5° 做干涉求交。

详细说明:[cad/README.md](cad/README.md)(结构、组装顺序)· [cad/JLC_FREE_GUIDE.md](cad/JLC_FREE_GUIDE.md)(免费打印下单)· [cad/tilt_tv_product_preview.svg](cad/tilt_tv_product_preview.svg)(实测图纸 + 校核)

重新生成:`python tools/generate_3d_models.py` → `python tools/generate_svg_preview.py` → `python tools/render_preview.py` → `python tools/test_cad_models.py`

## BOM 物料清单

**核心**

| 物料 | 规格 | 数量 | 说明 |
| :--- | :--- | :---: | :--- |
| Wio Terminal | Seeed Studio,ATSAMD51 + RTL8720DN,2.4" 320×240 | 1 | 主机,固件见下方"快速上手" |
| USB-C 数据线 | 能传数据的线 | 1 | 供电 + 烧录 |
| 3D 打印外壳 | 完整版 6 件或嘉立创免费版 2 件,推荐 9600 光敏树脂 | 1 套 | 见上一节 |

**外壳五金**

| 物料 | 规格 | 数量 | 说明 |
| :--- | :--- | :---: | :--- |
| 铰链螺栓 | M3×25 内六角 | 1 | 螺栓头沉入左叉臂沉孔 |
| 螺母 | M3 螺母(完整版,压进打印旋钮)或 M3 滚花手拧螺母 ≤ Ø12(免费版) | 1 | 拧紧即可调俯仰阻尼 |
| 底盖螺丝 | M3×8 沉头自攻 | 4 | 仅完整版 |
| 模块螺丝 | M2×5 自攻 | 若干 | 固定 Grove 模块 / 功放板(完整版模块柱) |
| 防滑脚垫 | Ø10 硅胶脚垫 | 4 | 完整版贴底盖垫槽;免费版贴主机盒底边 |

**扩展电子(可选,装进主机盒)**

| 物料 | 规格 | 参考价 | 说明 |
| :--- | :--- | :---: | :--- |
| I2S 功放 | MAX98357A(已焊排针) | 5–8 元 | 接 40-Pin,接线见 [cad/README.md](cad/README.md#-max98357a-到-wio-terminal-40-pin-极简免焊接线表) |
| 腔体喇叭 | 8Ω 2W 3520 | 4–6 元 | 插入主机盒左侧卡槽 |
| 杜邦线 | 20cm 母对母 × 5–7 | 2–3 元 | 40-Pin → 功放 |
| USB-C 延长线 | 面板式公转母(弯头更好走线) | 6–10 元 | 把 Wio 底边 USB-C 引到主机盒正面 |
| Grove 线 | 4pin 20cm | 2–4 元 | 接 Grove 模块或从正面过线口引出 |
| 拨动开关 / LED | 小型拨动开关、Ø3 LED | 1–2 元 | 正面面板默认孔位 |
| 锂电池 | 3.7V 603040 / 503040(带保护板) | 10–15 元 | 需另配充电 + 升压 5V 模块才能给 Wio 供电 |

> 参考价为淘宝 / 拼多多常见价位的估计,以实际购买为准。

## 像素美术与双皮肤资源架构

- **双皮肤架构（片内 Flash + 片外 4MB QSPI Flash 流式直驱）**:
  - **主题 0 — 小维（复古像素小猫）**: 内置在 MCU Flash (`pet/pet_scene_data.h`)，320x240 复古像素小屋 + 14 种 `86x32` 面部表情切片，约 113 KB Flash。
  - **主题 1 — 大肥鱼（DeepSeek 女仆鲸鱼娘）**: 打包为 `art/skin_deepseek.bin` (~104.5 KB) 存储在板载 **4MB QSPI Flash** (`0x00040000`)，运行时仅常驻 512 字节 RGB565 调色板，背景与 14 组 `58x35` 面部切片均通过 QSPI 按行流式直推屏幕，搭配专属藏青底色 (`0x2906`) 与海洋蓝 (`0x4478`) 双层圆角底栏外框。
- **14 种情绪像素面部**: 正常、眨眼、开心、兴奋、难过、听、想、惊、害羞、疑惑、犯困、沉睡、眩晕、打哈欠。
- **低内存高帧率渲染**:
  - 256 色全局共享调色板 (512 字节 RGB565)
  - 320x240 字节索引背景图 (75.0 KB) + 14 个面部差分切片
  - **RAM 堆零占用**（仅使用 640 字节栈上行缓冲，为麦克风 96KB DMA 缓冲区留足裕量）。
- **极速局部刷新**: 面部微表情切换只刷新脏矩形，SPI 传输仅需 **~1.5ms**，完全零闪烁；气泡与装饰擦除根据索引数组或 QSPI 偏移按行恢复，无需整屏重绘。
- **资源管线与皮肤工具**:
  - `art/src/concept_pixel_art.jpg` (原始概念艺术图)
  - `tools/build_pixel_pet.py` / `tools/export_pet_assets.py` (小猫主题表情生成与导出)
  - `tools/build_deepseek_pet.py` / `tools/pack_qspi_skin.py` (DeepSeek 娘皮肤生成与二进制打包 `art/skin_deepseek.bin`)
  - `tools/sync_qspi_skin.py` (通过串口将 `skin_deepseek.bin` 烧写并校验至板载 QSPI Flash)
  - `tools/switch_skin.py` (编译期/命令行皮肤切换工具)

## 快速上手

硬件:Wio Terminal + USB-C 数据线 + WiFi(2.4G 信道 12/13 连不上,优先 5G,见 HANDOFF 踩坑史)。

1. 安装 [arduino-cli](https://arduino.github.io/arduino-cli/)、板卡包
   `Seeeduino:samd`(板卡源 `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`)
   及依赖库:`Seeed_GFX2`、`Seeed Arduino rpcWiFi`、`ArduinoJson`、`Seeed Arduino Mic`、
   `Grove-3-Axis-Digital-Accelerometer-2g-to-16g-LIS3DHTR`、`Seeed Arduino SFUD`
2. 配密钥:可按顶部 **A 键** 进入 `Wio-Pet` 手机网页热点配网，或 `cp pet/wifi_secrets.h.example pet/wifi_secrets.h` 填入默认 WiFi、百度语音 Key、DeepSeek Key
   (该文件已被 gitignore;不填则得到一只"离线宠物"——动画表情照常,说话会提示没网)
3. 编译烧录(一键脚本,Windows/Git Bash,自动按 VID 找串口 + 挂串口日志;其他平台用下面的手动命令):

   ```bash
   bash tools/flash_and_log.sh pet 600
   ```

   ```bash
   arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal --libraries libraries pet
   arduino-cli upload  -p <PORT> --fqbn Seeeduino:samd:seeed_wio_terminal --libraries libraries
   ```

   *(可选) 首次烧写 DeepSeek 娘扩展皮肤包至 QSPI Flash:*
   ```bash
   python tools/sync_qspi_skin.py -p <PORT>
   ```

4. 按 B 跟它说话；在桌宠页下压摇杆切换「小维」与「大肥鱼」主题。

## WioKit —— 它脚下的库

pet 的全部硬件能力沉淀在 [libraries/WioKit/](libraries/WioKit/)(标准 Arduino 库,
编译时 `--libraries libraries` 即可被任何 sketch 复用):

| 层 | 模块 | 内容 |
|----|------|------|
| L0 | `WioKitLogic.h` | 纯逻辑(无 Arduino 依赖):HTTP 判停与状态码提取 / chunked 解码 / 情绪标签 / 光线动作探测器,60 项自检全覆盖 |
| L1 | `WioKitCjk` | 中文渲染:UTF-8→GB2312→HZK16(含扩展字库与 Emoji 变体过滤),行缓冲 + pushImage 批量推送 |
| L1 | `WioKitMic` | DMA 录音 + 滑动预卷 VAD 自动截断 + 原子停止 + 静音裁剪(96KB PCM 零拷贝暴露) |
| L1 | `WioKitNet` | WiFi 连接 / 共享 6KB 静态 HttpBuf / 完整重试流式上传 (`wioWriteAll`) / C 键秒级中断与 yield 回调 |
| L1 | `WioKitSense` | 光线(与麦克风共用 ADC1 的安全共享读法)+ LIS3DH 摇晃/拿起/平扣检测 |
| L1 | `WioKitConfig` | 板载 4MB QSPI Flash 持久化存储 + SoftAP Captive Portal 手机网页配网 |
| L2 | `WioKitAsrBaidu` | 百度 ASR:OAuth token 缓存与失效重取、WAV 封装、明文 80 端口流式上传 |
| L2 | `WioKitLlmDeepSeek` | DeepSeek 多轮对话客户端 + 401/402/429 精细状态码诊断 |

模块独立 include,不用的不进固件(纯渲染示例固件比 pet 小 40KB+)。独立示例:
`examples/CjkHello`(只画中文)、`examples/VoiceEcho`(录音→识别→回答全链路)。

## 其他项目

- **`console/` + `host/`** — 菜单版桌面控制台(PC 中转 / 板端语音助理 / 直连 DeepSeek 三种形态),
  动作经摇杆选择,串口协议对接 `host/wio_console.py`(配置见 `host/config.json.example`)
- **`diag/tlsdiag/`** — 板子到各 AI 端点的 TLS 可达性诊断,排查网络问题用
- **`sketches/HelloWio/`** — 点灯小品
- **`tests/pet_selftest/`** — WioKit L0 纯逻辑的板上自检(`bash tools/flash_and_log.sh tests/pet_selftest 60`)

## 文档

- [cad/README.md](cad/README.md) — 3D 打印外壳:结构、组装、扩展预留、接线;[cad/JLC_FREE_GUIDE.md](cad/JLC_FREE_GUIDE.md) — 嘉立创免费打印版
- [HANDOFF.md](HANDOFF.md) — 开发笔记:软件架构细节、踩坑史(9 条真金白银)、真机验收清单。
  写给下一个接手的人,多半是未来的自己
- [docs/superpowers/specs/](docs/superpowers/specs/) — 两份设计文档:情绪+提速、WioKit 库化
- [docs/superpowers/plans/](docs/superpowers/plans/) — 情绪+提速一轮改造的开发记事(原始过程日志见 git 历史)
- [docs/perf-log.md](docs/perf-log.md) — 语音链路计时基线
