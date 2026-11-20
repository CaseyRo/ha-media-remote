#include "hardware/motion.h"

#include "board/board.h"

#if defined(BOARD_HAS_QMI8658)

#include <Arduino.h>
#include <Wire.h>

#include <cmath>

#include "log.h"

/**
 * The QMI8658, read over the touch controller's I2C bus with the same Wire,
 * as the AXP2101 is (battery.cpp). Accelerometer only, in its low-power mode
 * at 21 Hz and +-2 g, polled from the loop: no interrupt pin, no gyro.
 * Registers as the QMI8658A datasheet and SensorLib (MIT) give them.
 */
namespace hw {
namespace {

constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kWhoAmI = 0x05;
/** Address auto-increment on, little endian, the oscillator running. */
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kCtrl1 = 0x40;
/** +-2 g (bits 6:4 000), 21 Hz low power (bits 3:0 1101). */
constexpr uint8_t kRegCtrl2 = 0x03;
constexpr uint8_t kCtrl2 = 0x0D;
/** Bit 0: the accelerometer on; bit 1, the gyro, left off. */
constexpr uint8_t kRegCtrl7 = 0x08;
constexpr uint8_t kCtrl7 = 0x01;
/** AX_L, AX_H, AY_L ... AZ_H. */
constexpr uint8_t kRegAccel = 0x35;
constexpr float kLsbPerG = 16384.0f;

constexpr unsigned long kPollMs = 100;
/** Still means no axis further than this from its running average. */
constexpr float kStillG = 0.04f;
/** A lift: an axis this far from its running average, once armed. */
constexpr float kLiftG = 0.15f;
/** Armed by lying still this long, so a lift is a start of motion, not more
 *  of the same. */
constexpr unsigned long kArmMs = 1500;
/** Face down: screen-out z below this, held kFaceDownMs; turned up again once
 *  above kFaceUpZ. */
constexpr float kFaceDownZ = -0.8f;
constexpr float kFaceUpZ = -0.5f;
constexpr unsigned long kFaceDownMs = 1000;

uint8_t s_address = 0;
float s_a[3] = {0, 0, 0};
float s_base[3] = {0, 0, 0};
bool s_have = false;
unsigned long s_still_since = 0;
bool s_armed = false;
unsigned long s_down_since = 0;
bool s_down_told = false;
bool s_lift = false;
bool s_face_down = false;

int readReg(uint8_t reg) {
  Wire.beginTransmission(s_address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(static_cast<int>(s_address), 1) != 1) {
    return -1;
  }
  return Wire.read();
}

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(s_address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readAccel(float out[3]) {
  Wire.beginTransmission(s_address);
  Wire.write(kRegAccel);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(static_cast<int>(s_address), 6) != 6) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    out[i] = static_cast<int16_t>(lo | hi << 8) / kLsbPerG;
  }
  out[2] *= board::kImuScreenOutZ;
  return true;
}

void poll() {
  static unsigned long s_polled_ms = 0;
  if (s_address == 0 || millis() - s_polled_ms < kPollMs) {
    return;
  }
  s_polled_ms = millis();
  if (!readAccel(s_a)) {
    return;
  }
  const unsigned long now = millis();
  if (!s_have) {
    memcpy(s_base, s_a, sizeof(s_base));
    s_have = true;
    s_still_since = now;
  }
  float dev = 0.0f;
  for (int i = 0; i < 3; ++i) {
    dev = fmaxf(dev, fabsf(s_a[i] - s_base[i]));
    s_base[i] += (s_a[i] - s_base[i]) * 0.2f;  // about half a second
  }

  const bool face_down = s_a[2] < kFaceDownZ;
  if (face_down) {
    if (s_down_since == 0) {
      s_down_since = now;
    }
    if (!s_down_told && now - s_down_since >= kFaceDownMs) {
      s_down_told = true;
      s_face_down = true;
      LOG_INFO("Motion: face down");
    }
  } else if (s_a[2] > kFaceUpZ) {
    s_down_since = 0;
    s_down_told = false;
  }

  if (dev <= kStillG) {
    if (now - s_still_since >= kArmMs) {
      s_armed = true;
    }
    return;
  }
  s_still_since = now;
  // Lying face down arms it as well, so turning it up is a lift; the flip
  // onto its face is not one.
  if (s_armed && dev >= kLiftG && !face_down && s_down_since == 0) {
    s_armed = false;
    s_lift = true;
    LOG_DEBUG("Motion: lift (%.2f g)", dev);
  }
}

}  // namespace

bool motionInit() {
  if (s_address != 0) {
    return true;
  }
  // SA0 high on Waveshare's boards (0x6B); the other address in case not.
  for (const uint8_t addr : {board::kImuI2cAddress, uint8_t{0x6A}}) {
    s_address = addr;
    if (readReg(kRegWhoAmI) == kWhoAmI) {
      break;
    }
    s_address = 0;
  }
  if (s_address == 0) {
    LOG_WARN("Motion: no QMI8658");
    return false;
  }
  if (!writeReg(kRegCtrl1, kCtrl1) || !writeReg(kRegCtrl2, kCtrl2) ||
      !writeReg(kRegCtrl7, kCtrl7)) {
    LOG_WARN("Motion: QMI8658 at 0x%02X would not start", s_address);
    s_address = 0;
    return false;
  }
  LOG_INFO("Motion: QMI8658 at 0x%02X", s_address);
  return true;
}

bool motionTakeLift() {
  poll();
  const bool lift = s_lift;
  s_lift = false;
  return lift;
}

bool motionTakeFaceDown() {
  poll();
  const bool down = s_face_down;
  s_face_down = false;
  return down;
}

bool motionLast(float& x, float& y, float& z) {
  if (!s_have) {
    return false;
  }
  x = s_a[0];
  y = s_a[1];
  z = s_a[2];
  return true;
}

}  // namespace hw

#else  // no IMU on this board

namespace hw {

bool motionInit() { return false; }
bool motionTakeLift() { return false; }
bool motionTakeFaceDown() { return false; }
bool motionLast(float&, float&, float&) { return false; }

}  // namespace hw

#endif
