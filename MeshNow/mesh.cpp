#include "mesh.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

// ============================================================
//  Constantes internas
// ============================================================
#define MESH_MAGIC    0xA5
#define MESH_VERSION  1

static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

#define HEADER_SIZE   (sizeof(MeshPacket) - MESH_MAX_PAYLOAD)

#define PING_WINDOW   1500   // ms que esperamos respuestas de un ping

// ============================================================
//  Identidad propia
// ============================================================
static uint8_t  myMac[6];
static uint16_t myId  = 0;
static char     myName[8];

static uint16_t idFromMac(const uint8_t* mac) {
  return ((uint16_t)mac[4] << 8) | mac[5];
}

uint16_t meshMyId() { return myId; }

const char* meshMyName() { return myName; }

void meshFormatId(uint16_t id, char* out, size_t n) {
  snprintf(out, n, "%04X", id);
}

// ============================================================
//  Cola de recepcion (el callback de ESP-NOW solo copia aqui;
//  todo el trabajo pesado se hace en meshLoop(), fuera del
//  contexto de la tarea de WiFi)
// ============================================================
#define RXQ_SIZE 16
typedef struct {
  MeshPacket pkt;
  uint8_t    senderMac[6];
  int8_t     rssi;
  uint8_t    len;
} RxItem;

static volatile RxItem   rxq[RXQ_SIZE];
static volatile uint8_t  rxHead = 0;   // escribe el callback
static volatile uint8_t  rxTail = 0;   // lee meshLoop

// ============================================================
//  Cache anti-duplicados (origen, secuencia)
// ============================================================
#define SEEN_SIZE 64
typedef struct { uint16_t id; uint16_t seq; } SeenEntry;
static SeenEntry seen[SEEN_SIZE];
static uint8_t   seenPos = 0;

static bool seenBefore(uint16_t id, uint16_t seq) {
  for (int i = 0; i < SEEN_SIZE; i++) {
    if (seen[i].id == id && seen[i].seq == seq) return true;
  }
  return false;
}
static void markSeen(uint16_t id, uint16_t seq) {
  seen[seenPos].id  = id;
  seen[seenPos].seq = seq;
  seenPos = (seenPos + 1) % SEEN_SIZE;
}

// ============================================================
//  Tabla de nodos
// ============================================================
static MeshNode nodes[MAX_NODES];

static MeshNode* findNode(uint16_t id) {
  for (int i = 0; i < MAX_NODES; i++)
    if (nodes[i].used && nodes[i].id == id) return &nodes[i];
  return nullptr;
}
static MeshNode* newNode() {
  // primero un hueco libre
  for (int i = 0; i < MAX_NODES; i++)
    if (!nodes[i].used) return &nodes[i];
  // si no hay, recicla el mas viejo
  MeshNode* oldest = &nodes[0];
  for (int i = 1; i < MAX_NODES; i++)
    if (nodes[i].lastSeen < oldest->lastSeen) oldest = &nodes[i];
  return oldest;
}

static void updateNode(uint16_t id, const uint8_t* mac,
                       bool direct, int8_t rssi, uint8_t hops) {
  uint32_t now = millis();
  MeshNode* n = findNode(id);
  if (!n) {
    n = newNode();
    memset(n, 0, sizeof(MeshNode));
    n->used = true;
    n->id   = id;
    n->hops = hops;
  }
  memcpy(n->mac, mac, 6);
  n->lastSeen = now;

  if (direct) {
    n->direct     = true;
    n->rssi       = rssi;
    n->hops       = 1;
    n->lastDirect = now;
  } else {
    // Si hace rato que no lo oimos directo, deja de ser "directo".
    if (now - n->lastDirect > NODE_TIMEOUT) n->direct = false;
    if (!n->direct) {
      // guarda la ruta mas corta conocida
      if (hops < n->hops || n->hops == 0) n->hops = hops;
    }
  }
}

// ============================================================
//  Metricas
// ============================================================
static MeshMetrics metrics;
static uint32_t ppsWindowStart = 0;
static uint16_t ppsCounter     = 0;

// ============================================================
//  Estado de envio / secuencia
// ============================================================
static uint16_t txSeq = 0;
static uint32_t lastHello = 0;

// ---- ping ----
static bool     pingActive     = false;
static uint32_t pingStart      = 0;
static uint16_t pingNonce      = 0;
static uint16_t pingResponders[MAX_NODES];
static int      pingRespCount  = 0;

// ---- pong pendiente (respuesta con jitter para evitar colisiones) ----
static bool     pongPending    = false;
static uint32_t pongDue        = 0;
static uint16_t pongNonce      = 0;

// ---- texto recibido ----
static bool     newText        = false;
static char     lastText[MESH_MAX_PAYLOAD + 1];
static uint16_t lastTextFrom   = 0;

// ============================================================
//  Envio de bajo nivel
// ============================================================
static void rawSend(MeshPacket* p, uint8_t payloadLen) {
  uint8_t total = HEADER_SIZE + payloadLen;
  if (total > sizeof(MeshPacket)) total = sizeof(MeshPacket);
  esp_now_send(BROADCAST_MAC, (uint8_t*)p, total);
  metrics.tx++;
}

// Origina un paquete nuevo desde este nodo.
static void originate(uint8_t type, uint16_t dstId,
                      const uint8_t* payload, uint8_t payloadLen) {
  MeshPacket p;
  memset(&p, 0, sizeof(p));
  p.magic   = MESH_MAGIC;
  p.version = MESH_VERSION;
  p.type    = type;
  p.ttl     = MESH_TTL;
  p.hops    = 0;
  p.srcId   = myId;
  p.dstId   = dstId;
  p.seq     = ++txSeq;
  memcpy(p.srcMac, myMac, 6);
  if (payloadLen > MESH_MAX_PAYLOAD) payloadLen = MESH_MAX_PAYLOAD;
  p.payloadLen = payloadLen;
  if (payload && payloadLen) memcpy(p.payload, payload, payloadLen);

  markSeen(p.srcId, p.seq);   // no reenviar lo nuestro
  rawSend(&p, payloadLen);
}

// ============================================================
//  Callback de recepcion (contexto WiFi: solo copiar y salir)
// ============================================================
static void onRecv(const esp_now_recv_info_t* info,
                   const uint8_t* data, int len) {
  if (len < (int)HEADER_SIZE || len > (int)sizeof(MeshPacket)) return;
  if (data[0] != MESH_MAGIC) return;

  uint8_t next = (rxHead + 1) % RXQ_SIZE;
  if (next == rxTail) return;   // cola llena, descarta

  RxItem* it = (RxItem*)&rxq[rxHead];
  memcpy(&it->pkt, data, len);
  memcpy(it->senderMac, info->src_addr, 6);
  it->rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  it->len  = len;
  rxHead = next;
}

// ============================================================
//  Procesamiento de un paquete (en meshLoop)
// ============================================================
static void handleApp(const MeshPacket* p);

static void processPacket(RxItem* it) {
  MeshPacket* p = &it->pkt;

  // Ignora lo que originamos nosotros (por si vuelve rebotado).
  if (p->srcId == myId) return;

  // Anti-duplicado.
  if (seenBefore(p->srcId, p->seq)) { metrics.dup++; return; }
  markSeen(p->srcId, p->seq);

  metrics.rx++;
  ppsCounter++;
  metrics.lastRssi = it->rssi;

  // ---- actualizar tabla de nodos ----
  uint16_t senderId = idFromMac(it->senderMac);
  // El emisor inmediato siempre es un vecino directo.
  updateNode(senderId, it->senderMac, true, it->rssi, 1);
  // El origen puede estar a varios saltos.
  if (p->srcId != senderId) {
    updateNode(p->srcId, p->srcMac, false, 0, p->hops + 1);
  }

  // ---- logica de aplicacion ----
  handleApp(p);

  // ---- reenvio (multi-salto) ----
  if (p->ttl > 0) {
    MeshPacket fwd = *p;
    fwd.ttl--;
    fwd.hops++;
    rawSend(&fwd, fwd.payloadLen);
    metrics.relay++;
  }
}

static void handleApp(const MeshPacket* p) {
  switch (p->type) {
    case MT_HELLO:
      // La tabla ya se actualizo; nada mas que hacer.
      break;

    case MT_PING: {
      // Nos piden reportarnos: programa un PONG con jitter.
      uint16_t nonce = (p->payloadLen >= 2)
                       ? ((uint16_t)p->payload[0] | ((uint16_t)p->payload[1] << 8))
                       : 0;
      pongNonce   = nonce;
      pongDue     = millis() + random(20, 220);
      pongPending = true;
      break;
    }

    case MT_PONG: {
      if (pingActive) {
        uint16_t nonce = (p->payloadLen >= 2)
                         ? ((uint16_t)p->payload[0] | ((uint16_t)p->payload[1] << 8))
                         : 0;
        if (nonce == pingNonce) {
          // registra respondedor unico
          bool known = false;
          for (int i = 0; i < pingRespCount; i++)
            if (pingResponders[i] == p->srcId) { known = true; break; }
          if (!known && pingRespCount < MAX_NODES)
            pingResponders[pingRespCount++] = p->srcId;
        }
      }
      break;
    }

    case MT_TEXT: {
      uint8_t n = p->payloadLen;
      if (n > MESH_MAX_PAYLOAD) n = MESH_MAX_PAYLOAD;
      memcpy(lastText, p->payload, n);
      lastText[n]  = '\0';
      lastTextFrom = p->srcId;
      newText      = true;
      break;
    }
  }
}

// ============================================================
//  API publica
// ============================================================
void meshBegin() {
  memset(nodes,   0, sizeof(nodes));
  memset(seen,    0, sizeof(seen));
  memset(&metrics,0, sizeof(metrics));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(MESH_CHANNEL, WIFI_SECOND_CHAN_NONE);

  WiFi.macAddress(myMac);
  myId = idFromMac(myMac);
  if (strlen(NODE_NAME) > 0) {
    strncpy(myName, NODE_NAME, sizeof(myName) - 1);
    myName[sizeof(myName) - 1] = '\0';
  } else {
    meshFormatId(myId, myName, sizeof(myName));
  }

  randomSeed(myId ^ millis());

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("ESP-NOW init FALLO"));
    return;
  }
  esp_now_register_recv_cb(onRecv);

  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = MESH_CHANNEL;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  ppsWindowStart = millis();
  Serial.print(F("MeshNow listo. Nodo="));
  Serial.print(myName);
  Serial.print(F(" canal="));
  Serial.println(MESH_CHANNEL);
}

void meshLoop() {
  uint32_t now = millis();

  // 1) drenar cola de recepcion
  while (rxTail != rxHead) {
    RxItem local;
    memcpy(&local, (const void*)&rxq[rxTail], sizeof(RxItem));
    rxTail = (rxTail + 1) % RXQ_SIZE;
    processPacket(&local);
  }

  // 2) HELLO periodico
  if (now - lastHello >= HELLO_INTERVAL) {
    lastHello = now;
    originate(MT_HELLO, 0xFFFF, nullptr, 0);
  }

  // 3) PONG pendiente (respuesta a ping con jitter)
  if (pongPending && now >= pongDue) {
    pongPending = false;
    uint8_t pl[2] = { (uint8_t)(pongNonce & 0xFF), (uint8_t)(pongNonce >> 8) };
    originate(MT_PONG, 0xFFFF, pl, 2);
  }

  // 4) cerrar ventana de ping
  if (pingActive && now - pingStart > PING_WINDOW) {
    pingActive = false;
  }

  // 5) recalcular pps cada segundo
  if (now - ppsWindowStart >= 1000) {
    metrics.pps    = ppsCounter;
    ppsCounter     = 0;
    ppsWindowStart = now;
  }
}

void meshSendText(const char* text) {
  uint8_t n = strlen(text);
  if (n > MESH_MAX_PAYLOAD) n = MESH_MAX_PAYLOAD;
  originate(MT_TEXT, 0xFFFF, (const uint8_t*)text, n);
}

void meshSendPing() {
  pingNonce     = (uint16_t)random(1, 65535);
  pingActive    = true;
  pingStart     = millis();
  pingRespCount = 0;
  uint8_t pl[2] = { (uint8_t)(pingNonce & 0xFF), (uint8_t)(pingNonce >> 8) };
  originate(MT_PING, 0xFFFF, pl, 2);
}

MeshMetrics meshGetMetrics() { return metrics; }

bool meshNodeAlive(const MeshNode* n) {
  if (!n || !n->used) return false;
  return (millis() - n->lastSeen) < NODE_TIMEOUT;
}

int meshNodeCount() {
  int c = 0;
  for (int i = 0; i < MAX_NODES; i++)
    if (meshNodeAlive(&nodes[i])) c++;
  return c;
}

int meshDirectCount() {
  uint32_t now = millis();
  int c = 0;
  for (int i = 0; i < MAX_NODES; i++) {
    if (meshNodeAlive(&nodes[i]) && nodes[i].direct &&
        (now - nodes[i].lastDirect) < NODE_TIMEOUT) c++;
  }
  return c;
}

const MeshNode* meshNodeAt(int idx) {
  if (idx < 0 || idx >= MAX_NODES) return nullptr;
  return &nodes[idx];
}

int  meshPingResponders() { return pingRespCount; }
bool meshPingInProgress() { return pingActive; }

bool        meshHasNewText()   { return newText; }
const char* meshLastText()     { return lastText; }
uint16_t    meshLastTextFrom() { return lastTextFrom; }
void        meshClearNewText() { newText = false; }
