#pragma once
#include <Arduino.h>
#include "config.h"

// Terminal serial del nodo MAESTRO. Solo hace algo si IS_MASTER == 1.
// SIEMPRE activa (no depende de en que pantalla del OLED estes, ni de
// entrar a "Modo PC"): al arrancar imprime un banner y el prompt
// "meshnow:~$ " de una vez, con eco y backspace como una terminal de
// verdad, igual que en el proyecto camioneta. Comandos disponibles:
//
//   help                     esta ayuda
//   status                   metricas de la malla (tx/rx/relay/pps/rssi...)
//   info                     info de este nodo (id, canal, hardware...)
//   nodes                    tabla de nodos vivos (id, mac, rssi/saltos...)
//   ping                     lanza un barrido de activos
//   send <texto>             manda <texto> a TODOS
//   sendto <id_hex> <texto>  manda <texto> a un nodo especifico
//   canned                   lista el catalogo de mensajes prehechos
//   canned <idx> [id_hex]    manda un prehecho (a todos si no das id)
//   rgb/R/G/B                prueba de color en las neopixeles (3s)
//   buzzer                   prueba el buzzer
//   clear                    limpia la terminal
void serialMasterBegin();
void serialMasterLoop();
