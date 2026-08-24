#include "serialmaster.h"
#include "mesh.h"
#include <stdlib.h>

#if IS_MASTER

static uint32_t lastDump   = 0;
static char     lineBuf[96];
static uint8_t  linePos    = 0;
static uint32_t dumpCount  = 0;
static uint32_t cmdCount   = 0;
static uint32_t lastRxAt   = 0;

static void macStr(const uint8_t* m, char* out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
           m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void dumpState() {
  MeshMetrics m = meshGetMetrics();
  Serial.printf("$STAT,%04X,%lu,%lu,%lu,%lu,%d,%d,%u\n",
                meshMyId(),
                (unsigned long)m.tx, (unsigned long)m.rx,
                (unsigned long)m.relay, (unsigned long)m.dup,
                meshDirectCount(), meshNodeCount(), m.pps);

  uint32_t now = millis();
  for (int i = 0; i < MAX_NODES; i++) {
    const MeshNode* nd = meshNodeAt(i);
    if (!meshNodeAlive(nd)) continue;
    char mac[20]; macStr(nd->mac, mac, sizeof(mac));
    Serial.printf("$NODE,%04X,%s,%d,%d,%d,%lu\n",
                  nd->id, mac,
                  nd->direct ? nd->rssi : 0,
                  nd->hops,
                  nd->direct ? 1 : 0,
                  (unsigned long)(now - nd->lastSeen));
  }
  Serial.println("$END");
  dumpCount++;
}

static void handleCommand(char* cmd) {
  // recorta espacios iniciales
  while (*cmd == ' ') cmd++;
  cmdCount++;
  lastRxAt = millis();

  // parte "token" siguiente separado por espacio de 'p'; deja *p apuntando
  // al resto de la linea. Devuelve el token en buf.
  auto nextToken = [](char*& p, char* buf, size_t n) {
    while (*p == ' ') p++;
    size_t i = 0;
    while (*p && *p != ' ' && i < n - 1) buf[i++] = *p++;
    buf[i] = '\0';
    while (*p == ' ') p++;
  };

  if (strncmp(cmd, "PING", 4) == 0) {
    meshSendPing();
    Serial.println("$OK,PING");

  } else if (strncmp(cmd, "SEND ", 5) == 0) {
    // SEND <texto>            -> custom, a todos
    meshSendText(cmd + 5);
    Serial.print("$OK,SEND,");
    Serial.println(cmd + 5);

  } else if (strncmp(cmd, "SENDTO ", 7) == 0) {
    // SENDTO <id_hex> <texto> -> custom, a un nodo especifico
    char* p = cmd + 7;
    char idBuf[8];
    nextToken(p, idBuf, sizeof(idBuf));
    uint16_t dst = (uint16_t)strtoul(idBuf, nullptr, 16);
    meshSendText(p, dst);
    Serial.printf("$OK,SENDTO,%s,%s\n", idBuf, p);

  } else if (strncmp(cmd, "CANNED ", 7) == 0) {
    // CANNED <idx> [id_hex]   -> prehecho por indice, a todos o a un nodo
    char* p = cmd + 7;
    char idxBuf[6];
    nextToken(p, idxBuf, sizeof(idxBuf));
    int idx = atoi(idxBuf);
    uint16_t dst = 0xFFFF;
    if (*p) dst = (uint16_t)strtoul(p, nullptr, 16);
    if (idx < 0 || idx >= meshCannedCount()) {
      Serial.println("$ERR,CANNED,idx");
    } else {
      meshSendText(meshCannedAt(idx), dst);
      Serial.printf("$OK,CANNED,%d,%04X\n", idx, dst);
    }

  } else if (strncmp(cmd, "NODES", 5) == 0) {
    dumpState();

  } else if (cmd[0] != '\0') {
    Serial.print("$ERR,");
    Serial.println(cmd);
  }
}

void serialMasterBegin() {
  Serial.println("$READY,MASTER");
}

void serialMasterLoop() {
  // leer comandos entrantes
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (linePos > 0) {
        lineBuf[linePos] = '\0';
        handleCommand(lineBuf);
        linePos = 0;
      }
    } else if (linePos < sizeof(lineBuf) - 1) {
      lineBuf[linePos++] = c;
    }
  }

  // volcado periodico
  if (millis() - lastDump >= 500) {
    lastDump = millis();
    dumpState();
  }
}

uint32_t serialMasterDumpCount() { return dumpCount; }
uint32_t serialMasterCmdCount()  { return cmdCount; }
uint32_t serialMasterLastRxAt()  { return lastRxAt; }

#else  // ---- nodo normal: funciones vacias ----

void serialMasterBegin() {}
void serialMasterLoop()  {}
uint32_t serialMasterDumpCount() { return 0; }
uint32_t serialMasterCmdCount()  { return 0; }
uint32_t serialMasterLastRxAt()  { return 0; }

#endif
