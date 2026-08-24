#pragma once
#include <Arduino.h>
#include "config.h"

// Terminal serial del nodo MAESTRO ("Modo PC" en el menu, igual que
// camioneta). Solo hace algo si IS_MASTER == 1, y solo mientras
// currentScreen == SCR_PCMODE (entrar a esa pantalla la prende, salir
// la apaga). Nota: si el serial no responde en nada, revisa en el IDE
// Tools > "USB CDC On Boot" -> Enabled (si esta en Disabled, el
// Serial de Arduino no sale por el puerto USB que monitoreas).
//
// Al entrar imprime un banner y el prompt "meshnow:~$ ", con eco y
// backspace como una terminal de verdad. Comandos disponibles:
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
