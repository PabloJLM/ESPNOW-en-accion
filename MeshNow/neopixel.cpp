#include "neopixel.h"

#define NEO_BRIGHTNESS   20      // bajo a proposito: menos consumo
#define SCANNER_SCALE    0.35f   // el scanner brilla bajito, no a full color

static uint32_t msgFlashUntil = 0;

void neopixelInit() {
  strip.begin();
  strip.setBrightness(NEO_BRIGHTNESS);
  strip.clear();
  strip.show();

  // Flash morado UNA sola vez al arrancar; despues quedan apagadas.
  for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(120, 0, 160));
  strip.show();
  delay(350);
  strip.clear();
  strip.show();
}

void neopixelClear() {
  strip.clear();
  strip.show();
}

void neopixelFlashMessage() {
  msgFlashUntil = millis() + 900;
}

static uint32_t wheelColor(uint8_t pos, float scale) {
  pos = 255 - pos;
  uint8_t r, g, b;
  if (pos < 85)       { r = 255 - pos * 3; g = 0;              b = pos * 3; }
  else if (pos < 170) { pos -= 85;  r = 0;              g = pos * 3; b = 255 - pos * 3; }
  else                { pos -= 170; r = pos * 3;        g = 255 - pos * 3; b = 0; }
  return strip.Color((uint8_t)(r * scale), (uint8_t)(g * scale), (uint8_t)(b * scale));
}

void neopixelTick(NeoMode mode) {
  static uint32_t lastUpdate = 0;
  static NeoMode  lastMode   = (NeoMode)-1;   // fuerza el primer refresco
  uint32_t now = millis();

  if (msgFlashUntil) {
    if (now < msgFlashUntil) mode = NEO_MSG;
    else msgFlashUntil = 0;
  }

  if (mode == NEO_IDLE) {
    if (lastMode != NEO_IDLE) {   // apaga una sola vez al entrar, no reescribe en cada loop
      strip.clear();
      strip.show();
    }
    lastMode = mode;
    return;
  }

  if (now - lastUpdate < 60) { lastMode = mode; return; }   // refresco espaciado = menos consumo
  lastUpdate = now;
  lastMode   = mode;

  if (mode == NEO_MSG) {
    for (int i = 0; i < NUM_PIXELS; i++) strip.setPixelColor(i, strip.Color(0, 140, 0));
    strip.show();
    return;
  }

  // NEO_SCANNER: wheel arcoiris atenuado
  static uint8_t phase = 0;
  phase += 4;
  for (int i = 0; i < NUM_PIXELS; i++) {
    uint8_t pos = (uint8_t)((i * 256 / NUM_PIXELS) + phase);
    strip.setPixelColor(i, wheelColor(pos, SCANNER_SCALE));
  }
  strip.show();
}
