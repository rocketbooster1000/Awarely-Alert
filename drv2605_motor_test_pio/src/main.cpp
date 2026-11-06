/*
 * DRV2605L motor test for Seeed Studio XIAO ESP32-C3
 *
 * Confirms the Adafruit DRV2605L is on I2C, runs the chip's actuator
 * diagnostic for an Adafruit #1201 ERM disc, then plays a click, a buzz,
 * and a full-scale real-time pulse.
 *
 * Library: this folder's platformio.ini
 *          lib_deps = adafruit/Adafruit DRV2605 Library
 *
 * Board: seeed_xiao_esp32c3
 *        USB CDC On Boot: already enabled by the board manifest
 *        Serial Monitor: 115200
 *
 * Build from this folder:
 *   pio run
 *   pio run -t upload
 *   pio device monitor
 *
 * Wiring (3.3 V only — ESP32-C3 pins are not 5 V tolerant):
 *   DRV2605L VIN    -> XIAO 3V3
 *   DRV2605L GND    -> XIAO GND
 *   DRV2605L SDA    -> XIAO D4 (GPIO6)
 *   DRV2605L SCL    -> XIAO D5 (GPIO7)
 *   DRV2605L Motor+ -> disc red wire
 *   DRV2605L Motor- -> disc blue/black wire
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_DRV2605.h>

static const uint8_t ERM_OPEN_LOOP_BIT = 0x20;
static const unsigned long DIAG_TIMEOUT_MS = 1500;

Adafruit_DRV2605 drv;

static const char *deviceName(uint8_t id) {
  switch (id) {
    case 3:
      return "DRV2605";
    case 4:
      return "DRV2604";
    case 6:
      return "DRV2604L";
    case 7:
      return "DRV2605L";
    default:
      return "unknown";
  }
}

static void setErmOpenLoop(bool openLoop) {
  uint8_t control3 = drv.readRegister8(DRV2605_REG_CONTROL3);
  if (openLoop) {
    control3 |= ERM_OPEN_LOOP_BIT;
  } else {
    control3 &= ~ERM_OPEN_LOOP_BIT;
  }
  drv.writeRegister8(DRV2605_REG_CONTROL3, control3);
}

// Closed-loop diagnostic. DIAG_RESULT is status bit 3 and clears on read:
// 0 = actuator looks connected, 1 = open, short, or out-of-range back-EMF.
static void runDiagnostic() {
  drv.useERM();
  setErmOpenLoop(false);
  drv.setMode(DRV2605_MODE_DIAGNOS);
  drv.go();

  unsigned long start = millis();
  while (drv.readRegister8(DRV2605_REG_GO) & 0x01) {
    if (millis() - start > DIAG_TIMEOUT_MS) {
      Serial.println("Diagnostic: TIMEOUT (GO did not clear)");
      return;
    }
    delay(10);
  }

  uint8_t status = drv.readRegister8(DRV2605_REG_STATUS);
  bool failed = status & 0x08;
  if (failed) {
    Serial.println("Diagnostic: FAIL (open or short on Motor+ / Motor-)");
  } else {
    Serial.println("Diagnostic: PASS");
  }
  Serial.printf("OC_DETECT: %u\n", (status >> 0) & 0x01);
  Serial.printf("OVER_TEMP: %u\n", (status >> 1) & 0x01);
}

static void playEffect(uint8_t effect, unsigned long pauseMs) {
  drv.setWaveform(0, effect);
  drv.setWaveform(1, 0);
  drv.go();
  delay(pauseMs);
}

static void playVibration() {
  // ROM effects 1-5 are ERM libraries and were built for open-loop drive.
  setErmOpenLoop(true);
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG);

  Serial.println("Playing strong click");
  playEffect(1, 400);

  Serial.println("Playing strong buzz");
  playEffect(14, 800);

  Serial.println("Playing 750 ms alert");
  playEffect(15, 1200);

  // Signed open-loop RTP: 0x7F is full drive and 0x00 is off.
  // 0xFF is full scale only in unsigned RTP, where 0x00 is full reverse.
  Serial.println("Playing full buzz");
  drv.setMode(DRV2605_MODE_REALTIME);
  drv.setRealtimeValue(0x7F);
  delay(500);
  drv.setRealtimeValue(0x00);
}

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 2000) {
    delay(10);
  }

  Serial.println("DRV2605L motor test");

  Wire.begin(D4, D5);
  if (!drv.begin()) {
    Serial.println("DRV2605L not found on I2C.");
    Serial.println("Check: VIN->3V3, GND->GND, SDA->D4, SCL->D5");
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("I2C: found at 0x%02X\n", DRV2605_ADDR);

  uint8_t id = (drv.readRegister8(DRV2605_REG_STATUS) >> 5) & 0x07;
  Serial.printf("Device ID: %u (%s)\n", id, deviceName(id));
}

void loop() {
  runDiagnostic();
  playVibration();
  Serial.println("Repeating in 3 seconds.");
  delay(3000);
}
