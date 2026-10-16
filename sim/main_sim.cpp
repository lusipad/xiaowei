// 小维模拟器入口:把真实的 pet.ino(状态机)+ pet_face/pet_anim(表情动画)+ WioKitCjk(汉字渲染)
// + WioKitLogic(L0)编译成宿主程序,硬件边缘由 sim/ 的垫片和仿真桩代替。
// 场景:开机 → 待机 → 摸头 → 三轮语音对话(不同情绪)→ 摇晃 → 犯困 → 睡觉 → 唤醒,
// 每个阶段导出一帧 PPM(sim/out/,ppm2png.py 转 PNG)。
//
// 编译:bash sim/build.sh
#include "Arduino.h"
#include "Seeed_GFX.h"
#include "sim_internals.h"
#include "../pet/pet.ino"  // 真实应用代码

#include <WioKitSense.h>  // SE_* 事件常量

static void runFor(unsigned long ms) {
  const unsigned long end = millis() + ms;
  while (millis() < end) {
    sim::advanceMs(33);
    loop();
  }
}

static void tap(int pin) {
  sim::setKey(pin, true);
  runFor(100);
  sim::setKey(pin, false);
  runFor(100);
}

// 提前结束 ST_SHOW 的展示(等过 400ms 宽限,再按任意键)
static void skipShow() {
  runFor(600);
  tap(WIO_KEY_A);
}

static void dump(const char* name) {
  printf("sim: frame %s (t=%lums)\n", name, millis());
  char path[128];
  snprintf(path, sizeof(path), "sim/out/%s.ppm", name);
  sim::dumpFrame(path);
}

int main() {
  // 三轮对话脚本(情绪标签由真实 parseEmotionTag 解析驱动表情)
  sim::pushDialog("现在几点了", "[兴奋]现在是模拟器时间呀!");
  sim::pushDialog("你是真的吗", "[害羞]嘿嘿,画面是真实代码画出来的哦");
  sim::pushDialog("明天会下雨吗", "[惊讶]哇,这个我可不知道!");

  printf("=== 小维模拟器 XiaoWei host sim ===\n");
  setup();
  dump("00_boot");

  runFor(3500);  // 睁眼动画结束 → 待机
  dump("01_idle");

  tap(WIO_5S_PRESS);  // 压下摇杆摸头(WIO_5S_LEFT/RIGHT 现为切页)
  runFor(300);
  dump("02_pet_happy");
  runFor(1200);  // 展示结束回待机

  tap(WIO_KEY_B);  // 第一轮:说话
  runFor(500);
  dump("03_listen");
  runFor(2300);  // 说话 1.5s + 截断静音 0.9s → 录完
  runFor(200);   // 识别 + 回答
  dump("04_reply_1");
  skipShow();

  tap(WIO_KEY_B);  // 第二轮
  runFor(2500);
  runFor(200);
  dump("05_reply_2");
  skipShow();

  tap(WIO_KEY_B);  // 第三轮
  runFor(2500);
  runFor(200);
  dump("06_reply_3");
  skipShow();

  sim::senseEventAt(millis() + 100, SE_SHAKE);  // 摇晃
  runFor(300);
  dump("07_dizzy");
  runFor(3200);  // 晕完回待机

  runFor(185000);  // 3 分钟没人理 → 犯困(半眯眼)
  dump("08_drowsy");
  // 等一次打哈欠(A_DROWSY 的 animStep==2 帧只持续 0.8s)
  while (!(petAnimCurrent() == A_DROWSY && animStep == 2)) {
    sim::advanceMs(33);
    loop();
  }
  dump("09_yawn");

  runFor(125000);  // 5 分钟没人理 → 睡觉
  runFor(2600);    // Zzz 长大
  dump("10_sleep");

  tap(WIO_KEY_B);  // 按键唤醒
  runFor(300);
  dump("11_wake");
  runFor(1200);  // 睁眼完毕回待机

  printf("=== 场景结束,共 12 帧 → sim/out/*.ppm → ppm2png.py 转 PNG ===\n");
  return 0;
}
