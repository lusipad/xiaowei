#include "WioKitSense.h"
#include <WioKitLogic.h>
#include <LIS3DHTR.h>
#include "wiring_private.h"  // pinPeripheral

// 默认阈值与 pet 迁移前一致;wioSenseConfig() 可调
static const int DARK_TH = 60;
static const int LIGHT_TH = 120;
static const int SHAKE_MG = 800;
static const int PICKUP_MG = 300;
static const uint32_t STILL_MS = 3000;

static LIS3DHTR<TwoWire> lis;
static bool imuOk = false;
static LightDetector lightDet(DARK_TH, LIGHT_TH, 10000, 2000);
static MotionDetector motionDet(SHAKE_MG, PICKUP_MG, STILL_MS);
static unsigned long lightNext = 0, imuNext = 0, senseDbgNext = 0;
static long lightAvg = -1;
static int lastAx = 0, lastAy = 0, lastAz = 0;
static bool isFaceDown = false;

void wioSenseConfig(int darkTh, int lightTh, int shakeMg, int pickupMg, uint32_t stillMs) {
  lightDet.darkTh = darkTh;
  lightDet.lightTh = lightTh;
  motionDet.shakeMg = shakeMg;
  motionDet.pickupMg = pickupMg;
  motionDet.stillMs = stillMs;
}

// 光线传感器(PD01/AIN15)与麦克风(AIN12)共用 ADC1:麦克风让 ADC1 自由运行 + DMA 持续搬运。
// 这里临时把通道切到光线(并加长采样时间)、等几次转换、读结果,再恢复麦克风的设置;ADC1 全程不停。
static int readLightShared() {
  if (!ADC1->CTRLA.bit.ENABLE) return 0;
  const uint8_t micCh = g_APinDescription[WIO_MIC].ulADCChannelNumber;
  const uint8_t micSamplen = ADC1->SAMPCTRL.bit.SAMPLEN;
  ADC1->SAMPCTRL.bit.SAMPLEN = 63;  // 光敏电路内阻高:麦克风设的最短采样时间充不满采样电容,读数会接近 0
  uint16_t to = 1000;
  while (ADC1->SYNCBUSY.bit.SAMPCTRL && --to) {}
  ADC1->INPUTCTRL.bit.MUXPOS = g_APinDescription[WIO_LIGHT].ulADCChannelNumber;
  to = 1000;
  while (ADC1->SYNCBUSY.bit.INPUTCTRL && --to) {}
  delay(2);  // 等若干次转换,丢掉切换前旧通道/旧采样时间的结果
  const int v = ADC1->RESULT.reg;  // 12 bit
  ADC1->INPUTCTRL.bit.MUXPOS = micCh;
  to = 1000;
  while (ADC1->SYNCBUSY.bit.INPUTCTRL && --to) {}
  ADC1->SAMPCTRL.bit.SAMPLEN = micSamplen;
  to = 1000;
  while (ADC1->SYNCBUSY.bit.SAMPCTRL && --to) {}
  return v >> 2;  // 折算成 0-1023,与阈值量程一致
}

bool wioSenseBegin() {
  pinPeripheral(WIO_LIGHT, PIO_ANALOG);
  lis.begin(Wire1);
  imuOk = lis.isConnection();
  if (imuOk) {
    lis.setOutputDataRate(LIS3DHTR_DATARATE_50HZ);
    lis.setFullScaleRange(LIS3DHTR_RANGE_2G);
  }
  Serial.println(imuOk ? "S: imu ok" : "S: imu FAIL (shake/pickup disabled)");
  return imuOk;
}

// 非阻塞:到采样时间才读;每次最多返回一个事件(使用带符号差值防止 millis() 49.7天溢出)
int wioSensePoll() {
  const unsigned long now = millis();
  int ev = SE_NONE;
  if (lightNext == 0 || (long)(now - lightNext) >= 0) {
    lightNext = now + 500;
    const int raw = readLightShared();
    lightAvg = (lightAvg < 0) ? raw : (lightAvg * 3 + raw) / 4;
    const int r = lightDet.update((int)lightAvg, (uint32_t)now);
#if WIO_SENSE_LIGHT_ENABLED
    if (!isFaceDown) {
      if (r == 1) ev = SE_DARK;
      else if (r == 2) ev = SE_LIGHT;
    }
#else
    (void)r;
#endif
  }
  if (imuOk && (imuNext == 0 || (long)(now - imuNext) >= 0)) {
    imuNext = now + 50;
    lastAx = (int)(lis.getAccelerationX() * 1000);
    lastAy = (int)(lis.getAccelerationY() * 1000);
    lastAz = (int)(lis.getAccelerationZ() * 1000);

    // 翻转检测: 正常平放屏幕朝上时 Z 轴约为 -1000mg; 扣在桌面上屏幕朝下时 Z 轴翻转为 > +650mg
    if (lastAz > 650) {
      if (!isFaceDown) {
        isFaceDown = true;
        ev = SE_FACEDOWN;
      }
    } else if (lastAz < -150) {
      if (isFaceDown) {
        isFaceDown = false;
        ev = SE_FACEUP;
      }
    }

    const int m = motionDet.update(lastAx, lastAy, lastAz, (uint32_t)now);
    // 扣放专注模式下屏蔽摇晃/拿起误触发，避免桌面震动导致亮屏眩晕
    if (ev == SE_NONE && !isFaceDown) {
      if (m == MOTION_SHAKE) ev = SE_SHAKE;
      else if (m == MOTION_PICKUP) ev = SE_PICKUP;
    }
  }
#if WIO_SENSE_DEBUG
  if (senseDbgNext == 0 || (long)(now - senseDbgNext) >= 0) {
    senseDbgNext = now + 2000;
    Serial.print("S: light=");
    Serial.print(lightAvg);
    Serial.print(" a=");
    Serial.print(lastAx);
    Serial.print(",");
    Serial.print(lastAy);
    Serial.print(",");
    Serial.print(lastAz);
    Serial.print(" facedown=");
    Serial.print(isFaceDown ? 1 : 0);
    Serial.print(" adc1_en=");  // 麦克风要求 ADC1 保持使能且通道停在 AIN12
    Serial.print(ADC1->CTRLA.bit.ENABLE);
    Serial.print(" mux=");
    Serial.println(ADC1->INPUTCTRL.bit.MUXPOS);
  }
#endif
  return ev;
}

bool wioSenseIsFaceDown() {
  return isFaceDown;
}
