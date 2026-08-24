#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

//    IS_MASTER = 1  -> flashea UNA 
//    IS_MASTER = 0  -> flashea el resto de placas (nodos).
#define IS_MASTER      1

// ==================== PINES (mismo pinout camioneta) ====================
#define PIN_SDA         6
#define PIN_SCL         7
#define PIN_SELECT      1
#define PIN_UP          15
#define PIN_DOWN        23
#define PIN_BACK        22
#define PIN_NEOPIXEL    11
#define PIN_BUZZER      2
#define PIN_LED1        3
#define PIN_GPSON       8
#define PIN_CD          40

// ==================== PANTALLA ====================
#define SCREEN_W        128
#define SCREEN_H        64

// ==================== NEOPIXELES ====================
#define NUM_PIXELS      9

// ==================== PARAMETROS DE MALLA ====================
// Todos los nodos DEBEN estar en el mismo canal WiFi.
#define MESH_CHANNEL    1

// Saltos maximos que puede recorrer un paquete antes de morir.
// Con 15 nodos en un salon, 5-6 sobra de bienvenida.
#define MESH_TTL        6

// Cada cuanto un nodo anuncia que sigue vivo (ms).
#define HELLO_INTERVAL  2000

// Tras cuanto sin oir a un nodo se considera "caido" (ms).
#define NODE_TIMEOUT    8000

// Capacidad de la tabla de nodos conocidos.
#define MAX_NODES       24

// Tamano maximo de payload de texto (para mensajes multi-salto).
#define MESH_MAX_PAYLOAD  64

// Nombre corto opcional del nodo. Si se deja "" se usa el id
// hexadecimal derivado de la MAC (unico por placa, no hay que
// tocar el codigo por unidad).
#define NODE_NAME       ""

// ==================== ESTADOS DE PANTALLA ====================
enum Screen {
  SCR_SPLASH,
  SCR_MENU,
  SCR_METRICS,     // metricas (tx/rx/relay/pps...)
  SCR_NEIGHBORS,   // red / vecinos (directos + por salto)
  SCR_PING,        // ping activos + quienes respondieron
  SCR_SCANNER,     // scanner ESP-NOW (RSSI en vivo)
  SCR_MESSAGES,    // historial + envio de mensajes
  SCR_SETTINGS,    // ajustes (placeholder)
  SCR_HELP,        // ayuda (placeholder)
  SCR_PCMODE,      // solo maestro: modo PC / LabVIEW
  SCR_EASTER       // easter egg (pixel art)
};

extern Screen currentScreen;

// ==================== VARIABLES RGB NEOPIXEL ====================
extern uint8_t neoR;
extern uint8_t neoG;
extern uint8_t neoB;

#endif
