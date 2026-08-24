#pragma once
#include <Arduino.h>
#include "config.h"

void buzzerClick();
void buzzerBeep();
void buzzerNote(unsigned int freq, unsigned int durationMs);
