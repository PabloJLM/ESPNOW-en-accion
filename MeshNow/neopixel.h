#pragma once
#include <Adafruit_NeoPixel.h>
#include "config.h"

extern Adafruit_NeoPixel strip;

enum NeoMode {
  NEO_IDLE,     
  NEO_SCANNER,  
  NEO_MSG       
};

void neopixelInit();
void neopixelClear();

void neopixelTick(NeoMode mode);

void neopixelFlashMessage();
