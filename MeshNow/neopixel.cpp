#include "neopixel.h"

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

void neopixelMeshStatus(int activeNodes, int bestRssi) {
  // Color por calidad de senal del mejor vecino directo.
  // RSSI tipico: -30 (excelente) .. -90 (pesimo).
  uint8_t r, g;
  if (bestRssi >= -55)      { r = 0;   g = 255; }   // verde
  else if (bestRssi >= -70) { r = 180; g = 180; }   // ambar
  else                      { r = 255; g = 0;   }   // rojo

  if (activeNodes > NUM_PIXELS) activeNodes = NUM_PIXELS;
  strip.clear();
  for (int i = 0; i < activeNodes; i++)
    strip.setPixelColor(i, strip.Color(r, g, 0));
  strip.show();
}
