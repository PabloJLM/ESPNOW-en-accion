#include "buzzer.h"

void buzzerClick() { tone(PIN_BUZZER, 2000, 30); }
void buzzerBeep()  { tone(PIN_BUZZER, 1000, 100); }

void buzzerNote(unsigned int freq, unsigned int durationMs) {
  if (freq == 0) { delay(durationMs); return; }
  tone(PIN_BUZZER, freq, durationMs);
  delay(durationMs);
  noTone(PIN_BUZZER);
}
