#pragma once
#include <Arduino.h>
#include "config.h"

// Inicializa estado de la UI (no toca el hardware de pantalla).
void uiBegin();

// Llamar en cada iteracion de loop(): lee botones, dibuja la
// pantalla actual y refresca los neopixeles con la salud de la malla.
void uiLoop();
