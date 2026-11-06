#include "hardware/battery.h"

#include "board/board.h"

#if defined(BOARD_HAS_AXP2101)

#include <Arduino.h>
#include <Wire.h>

#include "log.h"

/**
 * The AXP2101, read over the touch controller's I2C bus with the same Wire,
 * which touchRawInit() starts. Registers as XPowersLib (MIT) reads them, the
 * library Waveshare's examples drive this board's power manager with.
 */
namespace hw {
namespace {

/** Bit 3: a battery is present. */
constexpr uint8_t kRegStatus1 = 0x00;
/** Bits 6:5: 01 charging, 10 discharging, 00 standby. */
constexpr uint8_t kRegStatus2 = 0x01;
/** Bit 3: the fuel gauge, which fills kRegPercent, is on. */
constexpr uint8_t kRegGaugeCtrl = 0x18;
constexpr uint8_t kGaugeOn = 0x08;
constexpr uint8_t kRegPercent = 0xA4;
/** Bit 3 of each: the PWR key pressed briefly. Enabled, it is latched in the
 *  status register until written back. */
constexpr uint8_t kRegIrqEnable1 = 0x40;
constexpr uint8_t kRegIrqEnable2 = 0x41;
constexpr uint8_t kRegIrqEnable3 = 0x42;
constexpr uint8_t kRegIrqStatus1 = 0x48;
constexpr uint8_t kRegIrqStatus2 = 0x49;
constexpr uint8_t kRegIrqStatus3 = 0x4A;
constexpr uint8_t kPowerKeyShort = 0x08;
/** Bits 1 and 0: PWRON's falling edge -- the key pressed -- and its rising
 *  edge, released. Together they say whether it is held. */
constexpr uint8_t kPowerKeyDown = 0x02;
constexpr uint8_t kPowerKeyUp = 0x01;
constexpr uint8_t kPowerKeyBits = kPowerKeyShort | kPowerKeyDown | kPowerKeyUp;
constexpr unsigned long kPowerKeyPollMs = 100;

bool s_ready = false;
/** The key as the last read left it, and a short press not yet taken. */
bool s_key_down = false;
bool s_key_press = false;

/** One register, or -1 when the power manager does not answer. */
int readReg(uint8_t reg) {
  Wire.beginTransmission(board::kPmuI2cAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(static_cast<int>(board::kPmuI2cAddress), 1) != 1) {
    return -1;
  }
  return Wire.read();
}

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(board::kPmuI2cAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

/** Every latched interrupt cleared, each status register written back. */
void clearIrqs() {
  writeReg(kRegIrqStatus1, 0xFF);
  writeReg(kRegIrqStatus2, 0xFF);
  writeReg(kRegIrqStatus3, 0xFF);
}

}  // namespace

bool batteryInit() {
  if (s_ready) {
    return true;
  }
  const int gauge = readReg(kRegGaugeCtrl);
  if (gauge < 0) {
    LOG_WARN("Battery: no AXP2101 at 0x%02X", board::kPmuI2cAddress);
    return false;
  }
  if ((gauge & kGaugeOn) == 0 &&
      !writeReg(kRegGaugeCtrl, static_cast<uint8_t>(gauge | kGaugeOn))) {
    LOG_WARN("Battery: could not turn the fuel gauge on");
  }
  // The PWR key -- its short press and both edges -- armed as Waveshare's
  // AXP2101 example for this board arms it: every other interrupt off, and
  // all of them cleared.
  writeReg(kRegIrqEnable1, 0x00);
  writeReg(kRegIrqEnable2, kPowerKeyBits);
  writeReg(kRegIrqEnable3, 0x00);
  clearIrqs();
  LOG_INFO("Battery: PWR key armed (INTEN2 0x%02X)", readReg(kRegIrqEnable2));
  s_ready = true;
  const int pct = batteryPercent();
  if (pct < 0) {
    LOG_INFO("Battery: none reported (status 0x%02X)", readReg(kRegStatus1));
  } else {
    LOG_INFO("Battery: %d%%%s", pct, batteryCharging() ? ", charging" : "");
  }
  return true;
}

int batteryPercent() {
  if (!s_ready) {
    return -1;
  }
  const int status = readReg(kRegStatus1);
  if (status < 0 || (status & 0x08) == 0) {
    return -1;
  }
  const int pct = readReg(kRegPercent);
  return pct >= 0 && pct <= 100 ? pct : -1;
}

bool batteryCharging() {
  const int status = s_ready ? readReg(kRegStatus2) : -1;
  return status >= 0 && (status >> 5 & 0x03) == 0x01;
}

namespace {

/** Read the key's events, at most every kPowerKeyPollMs: a short press is
 *  latched until taken, and the edges say whether it is held. */
void pollPowerKey() {
  static unsigned long s_polled_ms = 0;
  if (!s_ready || millis() - s_polled_ms < kPowerKeyPollMs) {
    return;
  }
  s_polled_ms = millis();
  // All three read and all three cleared, as the example does.
  const int s1 = readReg(kRegIrqStatus1);
  const int status = readReg(kRegIrqStatus2);
  const int s3 = readReg(kRegIrqStatus3);
  if (status < 0 || (s1 <= 0 && status == 0 && s3 <= 0)) {
    return;
  }
  clearIrqs();
  LOG_DEBUG("PMU irq %02X %02X %02X", s1 & 0xFF, status, s3 & 0xFF);
  if ((status & kPowerKeyShort) != 0) {
    s_key_press = true;
  }
  const bool down = (status & kPowerKeyDown) != 0;
  const bool up = (status & kPowerKeyUp) != 0;
  if (down && !up) {
    s_key_down = true;
    LOG_DEBUG("PWR down");
  } else if (up) {
    // Released -- or pressed and let go between two reads.
    s_key_down = false;
    LOG_DEBUG("PWR up");
  }
}

}  // namespace

bool powerKeyTakePress() {
  pollPowerKey();
  const bool pressed = s_key_press;
  s_key_press = false;
  return pressed;
}

bool powerKeyHeld() {
  pollPowerKey();
  return s_key_down;
}

}  // namespace hw

#else  // no power manager on this board

namespace hw {

bool batteryInit() { return false; }
int batteryPercent() { return -1; }
bool batteryCharging() { return false; }
bool powerKeyTakePress() { return false; }
bool powerKeyHeld() { return false; }

}  // namespace hw

#endif
