#pragma once
#include <Adafruit_NeoPixel.h>
#include "config.h"

extern Adafruit_NeoPixel strip;

enum NeoMode {
  NEO_IDLE,     // apagadas (ahorro de bateria)
  NEO_SCANNER,  // wheel arcoiris tenue (pantalla scanner)
  NEO_MSG       // verde (mensaje recibido)
};

// Enciende un flash morado breve al arrancar y despues las deja
// apagadas. Brillo base bajo a proposito (consumo).
void neopixelInit();
void neopixelClear();

// Llamar en cada uiLoop() con el modo actual. Se auto-throttlea y en
// idle ni siquiera reescribe la tira (consumo minimo).
void neopixelTick(NeoMode mode);

// Arma el flash verde por MSG_FLASH_MS (llamar cuando llega un mensaje).
void neopixelFlashMessage();

// Fuerza un color solido por 'ms' (prueba rgb desde PC-mode/terminal).
// Mientras dura, neopixelTick() no toca la tira.
void neopixelManual(uint8_t r, uint8_t g, uint8_t b, uint32_t ms);
