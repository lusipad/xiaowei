// ---- 动画调度器:非阻塞,按 millis() 推进,每次 tick 最多画一帧 ----
// loop() 与网络等待循环(readHttpBody / 上传 / 连 WiFi)都调用 petAnimTick(),阻塞请求期间画面照样动
#pragma once

enum Anim { A_NONE, A_IDLE, A_LISTEN, A_THINK, A_EMOTE, A_WAKE, A_DROWSY, A_SLEEP, A_DIZZY };

int animCur = A_NONE;
int animFace = F_NORMAL;
int animStep = 0;
unsigned long animNext = 0;
bool animImmediate = true;

// A_IDLE 内部:眨眼 + 随机小动作(1=张望 2=哼歌 3=歪头)
bool idleBlinking = false;
unsigned long idleBlinkAt = 0;
int idleAct = 0;
int idleStep = 0;
unsigned long idleActAt = 0;

void petAnimSet(int anim, int face = F_NORMAL) {
  petFaceResetLook();
  animCur = anim;
  animFace = face;
  animStep = 0;
  animImmediate = true;  // 下一次 tick 立即画第一帧(不依赖 millis()==0 比较)
  const unsigned long now = millis();
  idleBlinking = false;
  idleBlinkAt = now + random(2500, 5000);
  idleAct = 0;
  idleStep = 0;
  idleActAt = now + random(8000, 20000);
}

int petAnimCurrent() { return animCur; }

void endIdleAct(unsigned long now) {
  petFaceResetLook();
  clearDeco();
  idleAct = 0;
  idleStep = 0;
  idleActAt = now + random(8000, 20000);
  animNext = now + 100;
}

void tickIdle(unsigned long now) {
  if (animStep == 0) {  // 进入待机:复位五官与装饰
    clearDeco();
    drawFeatures(F_NORMAL);
    animStep = 1;
  }
  if (idleAct == 1) {  // 张望:生动顾盼(转睛看侧面 → 正视 → 好奇专注 → 恢复常态)
    static const int SEQ[4] = {F_THINK, F_NORMAL, F_LISTEN, F_NORMAL};
    drawFeatures(SEQ[idleStep]);
    if (++idleStep >= 4) endIdleAct(now);
    else animNext = now + 400;
    return;
  }
  if (idleAct == 2) {  // 哼歌:开心嘴 + ♪ 从右侧往上飘
    if (idleStep == 0) drawFeatures(F_HAPPY);
    clearDeco();
    if (idleStep < 4) drawNote(DECO_RX + 10 + 4 * idleStep, DECO_Y + 44 - 10 * idleStep, C_GOLD);
    if (++idleStep >= 5) {
      clearDeco();
      drawFeatures(F_NORMAL);
      endIdleAct(now);
    } else {
      animNext = now + 300;
    }
    return;
  }
  if (idleAct == 3) {  // 歪头:疑惑表情停 1 秒
    if (idleStep == 0) {
      drawFeatures(F_CONFUSED);
      idleStep = 1;
      animNext = now + 1000;
      return;
    }
    drawFeatures(F_NORMAL);
    endIdleAct(now);
    return;
  }
  if (idleBlinking) {  // 眨眼的第二帧:睁眼
    drawEyesOnly(F_NORMAL, 0);
    idleBlinking = false;
    idleBlinkAt = now + random(2500, 5000);
  } else if ((long)(now - idleActAt) >= 0) {
    idleAct = random(1, 4);
    idleStep = 0;
    animNext = now;
    return;
  } else if ((long)(now - idleBlinkAt) >= 0) {
    drawEyesOnly(F_BLINK, 0);
    idleBlinking = true;
    animNext = now + 160;
    return;
  }
  animNext = ((long)(idleBlinkAt - idleActAt) < 0) ? idleBlinkAt : idleActAt;
}

extern PetState state;
extern int curPage;

void petAnimTick() {
  // 当处于非宠物页面的待机状态时，不画待机小猫动画，以免覆盖时钟或番茄钟看板
  if (state == ST_IDLE && curPage != PAGE_PET) return;
  const unsigned long now = millis();
  if (animCur == A_NONE) return;
  if (!animImmediate && (long)(now - animNext) < 0) return;
  animImmediate = false;
  switch (animCur) {
    case A_IDLE:
      tickIdle(now);
      break;
    case A_LISTEN:
      if (animStep == 0) drawFeatures(F_LISTEN);
      drawSoundBars(animStep);
      animStep++;
      animNext = now + 150;
      break;
    case A_THINK: {  // 思考:专注看右上方 + 思考气泡点循环
      if (animStep == 0) {
        clearDeco();
        drawFeatures(F_THINK);
      }
      drawThinkDots(animStep % 3 + 1);
      animStep++;
      animNext = now + 300;
      break;
    }
    case A_EMOTE:  // 表情 + 装饰;兴奋时星光闪烁,其余静止
      if (animStep == 0) drawFeatures(animFace);
      drawEmoteDeco(animFace, animStep);
      animStep++;
      animNext = now + (animFace == F_EXCITED ? 400 : 60000);
      break;
    case A_WAKE: {  // 慢慢睁眼,结束后自动转待机
      static const int SEQ[4] = {F_SLEEP, F_SLEEPY, F_SLEEPY, F_NORMAL};
      if (animStep == 0) clearDeco();
      drawEyesOnly(SEQ[animStep], 0);
      if (++animStep >= 4) {
        petAnimSet(A_IDLE);
        return;
      }
      animNext = now + 250;
      break;
    }
    case A_DROWSY:  // 半眯眼;每 6 秒打一次哈欠(0.8 秒)
      if (animStep == 0) {
        clearDeco();
        drawFeatures(F_SLEEPY);
        animStep = 1;
        animNext = now + 6000;
      } else if (animStep == 1) {
        drawFeatures(F_YAWN);
        animStep = 2;
        animNext = now + 800;
      } else {
        drawFeatures(F_SLEEPY);
        animStep = 1;
        animNext = now + 6000;
      }
      break;
    case A_SLEEP:
      if (animStep == 0) drawFeatures(F_SLEEP);
      drawZzz(animStep);
      animStep++;
      animNext = now + 500;
      break;
    case A_DIZZY:  // 眩晕:XX 蚊香眼
      if (animStep == 0) {
        clearDeco();
        drawFeatures(F_DIZZY);
      }
      animStep++;
      animNext = now + 100;
      break;
    default:
      break;
  }
}
