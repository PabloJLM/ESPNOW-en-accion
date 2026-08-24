#include "neopixel.h"
#include <math.h>

static uint32_t msgFlashUntil = 0;

void neopixelInit() {
  strip.begin();
  strip.setBrightness(60);
  strip.clear();
  strip.show();
}

void neopixelClear() {
  strip.clear();
  strip.show();
}

void neopixelFlashMessage() {
  msgFlashUntil = millis() + 1200;
}

static uint32_t wheelColor(uint8_t pos) {
  pos = 255 - pos;
  if (pos < 85)  return strip.Color(255 - pos * 3, 0, pos * 3);
  if (pos < 170) { pos -= 85; return strip.Color(0, pos * 3, 255 - pos * 3); }
  pos -= 170;    return strip.Color(pos * 3, 255 - pos * 3, 0);
}

void neopixelTick(NeoMode mode) {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (now - lastUpdate < 25) return;
  lastUpdate = now;

  if (msgFlashUntil) {
    if (now < msgFlashUntil) mode = NEO_MSG;
    else msgFlashUntil = 0;
  }

  if (mode == NEO_MSG) {
    for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(0, 255, 0));
    strip.show();
    return;
  }

  if (mode == NEO_SCANNER) {
    static uint8_t phase = 0;
    phase += 5;
    for (int i = 0; i < NUM_PIXELS; i++) {
      uint8_t pos = (uint8_t)((i * 256 / NUM_PIXELS) + phase);
      strip.setPixelColor(i, wheelColor(pos));
    }
    strip.show();
    return;
  }

  // NEO_IDLE: respiracion morada
  static float phase2 = 0.0f;
  phase2 += 0.05f;
  if (phase2 > 6.283185f) phase2 -= 6.283185f;
  float b = (sinf(phase2) + 1.0f) * 0.5f;      // 0..1
  uint8_t v = (uint8_t)(25 + b * 200);         // nunca del todo apagado
  uint8_t r = (uint8_t)(((uint16_t)v * 160) / 255);
  uint8_t bl = (uint8_t)(((uint16_t)v * 220) / 255);
  for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(r, 0, bl));
  strip.show();
}
