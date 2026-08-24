#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

//    IS_MASTER = 1  -> flashea UNA 
//    IS_MASTER = 0  -> flashea el resto de placas (nodos).
#define IS_MASTER      0


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


#define SCREEN_W        128
#define SCREEN_H        64


#define NUM_PIXELS      9

#define MESH_CHANNEL    1

#define MESH_TTL        6

#define HELLO_INTERVAL  2000

#define NODE_TIMEOUT    8000

#define MAX_NODES       24

#define MESH_MAX_PAYLOAD  64

#define NODE_NAME       ""

#define SENSOR_INTERVAL 12000

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

extern uint8_t neoR;
extern uint8_t neoG;
extern uint8_t neoB;

#endif
