#ifndef MESH_H
#define MESH_H

#include <Arduino.h>
#include "config.h"

// Tipos de paquete.
enum MeshType : uint8_t {
  MT_HELLO  = 1,   // "sigo vivo" (mantiene la tabla de nodos)
  MT_PING   = 2,   // barrido: pide a todos que respondan
  MT_PONG   = 3,   // respuesta a un ping
  MT_TEXT   = 4,   // mensaje custom (lo manda el maestro)
  MT_SENSOR = 5    // telemetria simulada de un nodo (slave): rssi + random
};

// Estructura del paquete que viaja por el aire. __packed__ para
// que ocupe exactamente lo mismo en todas las placas.
typedef struct __attribute__((packed)) {
  uint8_t  magic;                       // 0xA5, identifica nuestros paquetes
  uint8_t  version;                     // version de protocolo
  uint8_t  type;                        // MeshType
  uint8_t  ttl;                         // saltos que le quedan
  uint8_t  hops;                        // saltos ya recorridos
  uint16_t srcId;                       // id del origen (16 bits, deriva de MAC)
  uint16_t dstId;                       // 0xFFFF = todos
  uint16_t seq;                         // secuencia por origen (anti-duplicado)
  uint8_t  srcMac[6];                   // MAC del origen
  uint8_t  payloadLen;                  // bytes utiles en payload
  uint8_t  payload[MESH_MAX_PAYLOAD];   // datos (texto, etc.)
} MeshPacket;

// Entrada de la tabla de nodos conocidos.
typedef struct {
  bool     used;
  uint16_t id;
  uint8_t  mac[6];
  int8_t   rssi;       // RSSI del ultimo salto directo (valido si direct)
  uint8_t  hops;       // saltos hasta ese nodo (1 = vecino directo)
  bool     direct;     // true si lo oimos directamente (y reciente)
  uint32_t lastSeen;   // millis() de cualquier noticia suya (directa o por salto)
  uint32_t lastDirect; // millis() de la ultima vez que lo oimos DIRECTO
  bool     hasSensor;  // ya reporto telemetria simulada al menos una vez
  uint16_t sensorVal;  // ultimo valor "sensor" (random) recibido
  uint32_t sensorAt;   // millis() del ultimo reporte de sensor
} MeshNode;

// Item de historial de mensajes (custom o sensor) para pantalla Mensajes.
typedef struct {
  uint16_t from;
  uint8_t  type;                         // MT_TEXT o MT_SENSOR
  char     text[MESH_MAX_PAYLOAD + 1];
  uint32_t at;
} MeshMsgItem;
#define MSG_HIST_SIZE 8

// Metricas en vivo.
typedef struct {
  uint32_t tx;         // paquetes que originamos + reenviamos
  uint32_t rx;         // paquetes validos recibidos
  uint32_t relay;      // paquetes reenviados (multi-salto)
  uint32_t dup;        // duplicados descartados
  uint16_t pps;        // paquetes por segundo (rx)
  int8_t   lastRssi;   // RSSI del ultimo paquete oido
} MeshMetrics;

// ---- API publica ----
void        meshBegin();                        // init ESP-NOW
void        meshLoop();                          // procesar cola + housekeeping
uint16_t    meshMyId();                          // id propio (16 bits)
const char* meshMyName();                        // nombre corto propio
void        meshFormatId(uint16_t id, char* out, size_t n); // id -> texto

void        meshSendText(const char* text);      // difundir texto custom (maestro)
void        meshSendPing();                       // lanzar barrido de activos
void        meshSendSensorReport();               // slave: rssi+random simulado

// Consultas para la UI / serial:
MeshMetrics meshGetMetrics();
int         meshNodeCount();                      // nodos vivos conocidos (total)
int         meshDirectCount();                    // vecinos directos vivos
const MeshNode* meshNodeAt(int idx);              // acceso a la tabla (puede ser inactivo)
bool        meshNodeAlive(const MeshNode* n);     // vivo segun NODE_TIMEOUT

// Resultado del ultimo ping:
int         meshPingResponders();                 // cuantos respondieron al ultimo ping
uint16_t    meshPingResponderId(int idx);          // id del respondedor idx
bool        meshPingInProgress();
uint16_t    meshLastPingFrom();                    // quien nos hizo ping a nosotros
uint32_t    meshLastPingFromAt();                  // millis() de ese ping

// Ultimo texto recibido (para el toast en pantalla):
bool        meshHasNewText();                     // hay texto sin leer
const char* meshLastText();                       // contenido
uint16_t    meshLastTextFrom();                    // id del emisor
void        meshClearNewText();

// Historial (mensajes custom + reportes de sensor), mas reciente primero.
int              meshMsgHistCount();
const MeshMsgItem* meshMsgHistAt(int idx);

#endif
