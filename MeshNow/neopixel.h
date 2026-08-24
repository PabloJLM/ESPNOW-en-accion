#pragma once
#include <Adafruit_NeoPixel.h>
#include "config.h"

extern Adafruit_NeoPixel strip;

void neopixelInit();
void neopixelClear();

// Muestra la salud de la malla en la tira:
//   enciende 'active' pixeles (nodos vivos, tope NUM_PIXELS) y
//   colorea segun el mejor RSSI directo (verde=fuerte, rojo=debil).
void neopixelMeshStatus(int activeNodes, int bestRssi);
