#ifndef MESH_H
#define MESH_H

#include <Arduino.h>
#include "config.h"

// ============================================================
//  Nucleo de la malla ESP-NOW (flooding controlado con TTL)
//
//  Idea: no hay tabla de rutas. Cada paquete lleva un TTL y un
//  numero de secuencia unico por origen. Cuando un nodo recibe
//  un paquete que NO ha visto, lo procesa y, si TTL>0, lo vuelve
//  a emitir en broadcast (reenvio). Un cache anti-duplicados
//  evita que los paquetes den vueltas para siempre.
//
//  Resultado: aunque D no oiga a A directamente, el mensaje de A
//  llega a D saltando por B y C. Eso es el "multi-salto".
// ============================================================

// Tipos de paquete.
enum MeshType : uint8_t {
  MT_HELLO = 1,   // "sigo vivo" (mantiene la tabla de nodos)
  MT_PING  = 2,   // barrido: pide a todos que respondan
  MT_PONG  = 3,   // respuesta a un ping
  MT_TEXT  = 4    // mensaje de texto multi-salto (demo en vivo)
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
} MeshNode;

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

void        meshSendText(const char* text);      // difundir texto a toda la malla
void        meshSendPing();                       // lanzar barrido de activos

// Consultas para la UI / serial:
MeshMetrics meshGetMetrics();
int         meshNodeCount();                      // nodos vivos conocidos (total)
int         meshDirectCount();                    // vecinos directos vivos
const MeshNode* meshNodeAt(int idx);              // acceso a la tabla (puede ser inactivo)
bool        meshNodeAlive(const MeshNode* n);     // vivo segun NODE_TIMEOUT

// Resultado del ultimo ping:
int         meshPingResponders();                 // cuantos respondieron al ultimo ping
bool        meshPingInProgress();

// Ultimo texto recibido (para mostrar toast en pantalla):
bool        meshHasNewText();                     // hay texto sin leer
const char* meshLastText();                       // contenido
uint16_t    meshLastTextFrom();                   // id del emisor
void        meshClearNewText();

#endif
