#include "serialmaster.h"
#include <U8g2lib.h>
#include "mesh.h"
#include "buzzer.h"
#include "neopixel.h"
#include <stdlib.h>

#if IS_MASTER

// Pantalla "Modo PC": mismo patron que screen_pcmode.cpp de camioneta
// -- este archivo es DUENO total de la pantalla mientras currentScreen
// == SCR_PCMODE (dibuja su propio buffer, lee su propio boton BACK, y
// procesa el serial como una terminal linux).

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

static const unsigned char image_terminal_bits[] U8X8_PROGMEM = {
  0x00,0x00,0x00,0x00,0xfe,0xff,0xff,0x7f,0x02,0x00,0x00,0x40,0x02,0x00,0x00,0x40,
  0x02,0x00,0x00,0x40,0x02,0x00,0x00,0x40,0x72,0x00,0x00,0x40,0x8a,0x00,0x00,0x40,
  0x8a,0x00,0x00,0x40,0x8a,0x00,0x00,0x40,0x72,0x00,0x00,0x40,0x02,0x00,0x00,0x40,
  0x02,0x00,0x00,0x40,0x02,0x00,0x00,0x40,0xfe,0xff,0xff,0x7f,0x00,0x00,0x00,0x00
};
static const unsigned char image_Layer_9_bits[] U8X8_PROGMEM = {
  0x7e,0x7e,0x7e,0x7e,0x99,0x99,0x99,0x99,
  0x67,0xe6,0x67,0xe6,0x18,0x18,0x18,0x18,
  0x67,0xe6,0x67,0xe6,0x99,0x99,0x99,0x99,
  0x7e,0x7e,0x7e,0x7e
};

static bool pcModeInitialized = false;

static void macStr(const uint8_t* m, char* out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
           m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void printPrompt() {
  Serial.print(F("meshnow:~$ "));
}

static void printSection(const char* title) {
  Serial.println();
  Serial.print(F("== "));
  Serial.println(title);
}

static void printRow(const char* key, const String& value) {
  Serial.print(F("  "));
  Serial.print(key);
  int pad = 14 - (int)strlen(key);
  for (int i = 0; i < pad; i++) Serial.print(' ');
  Serial.println(value);
}

static void printBanner() {
  Serial.println();
  Serial.println(F("=================================="));
  Serial.println(F("  MeshNow -- terminal serial"));
  Serial.println(F("  (nodo maestro, USB 115200)"));
  Serial.println(F("=================================="));
  Serial.println(F("  escribe 'help' para ver los comandos"));
}

static void printHelp() {
  printSection("comandos");
  Serial.println(F("  help                     esta ayuda"));
  Serial.println(F("  status                   metricas de la malla"));
  Serial.println(F("  info                     info de este nodo"));
  Serial.println(F("  nodes / ls               tabla de nodos vivos"));
  Serial.println(F("  ping                     lanza un barrido de activos"));
  Serial.println(F("  send <texto>             manda a TODOS"));
  Serial.println(F("  sendto <id_hex> <texto>  manda a un nodo especifico"));
  Serial.println(F("  canned                   lista prehechos"));
  Serial.println(F("  canned <idx> [id_hex]    manda un prehecho"));
  Serial.println(F("  rgb/R/G/B                prueba color en las neopixeles (3s)"));
  Serial.println(F("  buzzer                   prueba el buzzer"));
  Serial.println(F("  clear / cls              limpia la terminal"));
  Serial.println(F("  exit                     vuelve al menu"));
}

static void printNodes() {
  printSection("nodos vivos");
  int n = meshNodeCount();
  if (n == 0) { Serial.println(F("  ninguno todavia")); return; }
  uint32_t now = millis();
  Serial.println(F("  id    mac                rssi/saltos   directo  edad"));
  for (int i = 0; i < MAX_NODES; i++) {
    const MeshNode* nd = meshNodeAt(i);
    if (!meshNodeAlive(nd)) continue;
    char id[10]; meshFormatId(nd->id, id, sizeof(id));
    char mac[20]; macStr(nd->mac, mac, sizeof(mac));
    char rs[14];
    if (nd->direct) snprintf(rs, sizeof(rs), "%d dBm", nd->rssi);
    else            snprintf(rs, sizeof(rs), "%d saltos", nd->hops);
    Serial.printf("  %-5s %-18s %-13s %-8s %lus\n",
                  id, mac, rs, nd->direct ? "si" : "no",
                  (unsigned long)((now - nd->lastSeen) / 1000));
  }
}

// parte el siguiente token separado por espacio de 'p'; deja *p
// apuntando al resto de la linea.
static void nextToken(char*& p, char* buf, size_t n) {
  while (*p == ' ') p++;
  size_t i = 0;
  while (*p && *p != ' ' && i < n - 1) buf[i++] = *p++;
  buf[i] = '\0';
  while (*p == ' ') p++;
}

// ---------- boton BACK (debounce propio, igual que screen_pcmode) ----------
static bool isButtonJustPressed(int pin) {
  static uint8_t lastStableState = HIGH;
  static uint8_t lastReading     = HIGH;
  static unsigned long lastDebounceTime = 0;
  uint8_t reading = digitalRead(pin);
  if (reading != lastReading) { lastDebounceTime = millis(); lastReading = reading; }
  if ((millis() - lastDebounceTime) > 50) {
    if (lastStableState == HIGH && reading == LOW) { lastStableState = reading; return true; }
    lastStableState = reading;
  }
  return false;
}

// ---------- procesamiento de comandos (buffer String) ----------
static void processSerialCommand() {
  static String commandBuffer = "";
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (commandBuffer.length() > 0) {
        commandBuffer.trim();
        Serial.println();

        if (commandBuffer == "help" || commandBuffer == "?") {
          printHelp();

        } else if (commandBuffer == "status") {
          printSection("estado de la malla");
          MeshMetrics m = meshGetMetrics();
          printRow("tx", String((unsigned long)m.tx));
          printRow("rx", String((unsigned long)m.rx));
          printRow("relay", String((unsigned long)m.relay));
          printRow("dup", String((unsigned long)m.dup));
          printRow("pps", String(m.pps));
          printRow("rssi", String(m.lastRssi));
          printRow("vivos", String(meshNodeCount()));
          printRow("directos", String(meshDirectCount()));

        } else if (commandBuffer == "info") {
          printSection("info del nodo");
          char id[10]; meshFormatId(meshMyId(), id, sizeof(id));
          printRow("id", String(id));
          printRow("nombre", String(meshMyName()));
          printRow("rol", String("maestro"));
          printRow("canal", String(MESH_CHANNEL));
          printRow("ttl", String(MESH_TTL));

        } else if (commandBuffer == "nodes" || commandBuffer == "ls") {
          printNodes();

        } else if (commandBuffer == "ping") {
          meshSendPing();
          Serial.println(F("  ok: barrido lanzado"));

        } else if (commandBuffer.startsWith("send ")) {
          String txt = commandBuffer.substring(5);
          meshSendText(txt.c_str());
          Serial.print(F("  ok: mandado a todos -> "));
          Serial.println(txt);

        } else if (commandBuffer.startsWith("sendto ")) {
          char line[96];
          commandBuffer.substring(7).toCharArray(line, sizeof(line));
          char* p = line;
          char idBuf[8];
          nextToken(p, idBuf, sizeof(idBuf));
          uint16_t dst = (uint16_t)strtoul(idBuf, nullptr, 16);
          meshSendText(p, dst);
          Serial.printf("  ok: mandado a %s -> %s\n", idBuf, p);

        } else if (commandBuffer == "canned") {
          printSection("mensajes prehechos");
          for (int i = 0; i < meshCannedCount(); i++)
            Serial.printf("  %d: %s\n", i, meshCannedAt(i));

        } else if (commandBuffer.startsWith("canned ")) {
          char line[64];
          commandBuffer.substring(7).toCharArray(line, sizeof(line));
          char* p = line;
          char idxBuf[6];
          nextToken(p, idxBuf, sizeof(idxBuf));
          int idx = atoi(idxBuf);
          uint16_t dst = 0xFFFF;
          if (*p) dst = (uint16_t)strtoul(p, nullptr, 16);
          if (idx < 0 || idx >= meshCannedCount()) {
            Serial.println(F("  error: indice invalido (usa 'canned' para verlos)"));
          } else {
            meshSendText(meshCannedAt(idx), dst);
            Serial.printf("  ok: prehecho %d -> %04X\n", idx, dst);
          }

        } else if (commandBuffer.startsWith("rgb/")) {
          int r, g, b;
          if (sscanf(commandBuffer.c_str(), "rgb/%d/%d/%d", &r, &g, &b) == 3) {
            r = constrain(r, 0, 255); g = constrain(g, 0, 255); b = constrain(b, 0, 255);
            neopixelManual((uint8_t)r, (uint8_t)g, (uint8_t)b, 3000);
            Serial.printf("  ok: rgb -> %d/%d/%d (3s)\n", r, g, b);
          } else {
            Serial.println(F("  uso: rgb/R/G/B"));
          }

        } else if (commandBuffer == "buzzer") {
          Serial.println(F("  probando buzzer..."));
          buzzerBeep();
          delay(200);
          buzzerBeep();
          Serial.println(F("  ok"));

        } else if (commandBuffer == "clear" || commandBuffer == "cls") {
          Serial.print(F("\033[2J\033[H"));

        } else if (commandBuffer == "exit") {
          Serial.println(F("  saliendo de Modo PC..."));
          pcModeInitialized = false;
          currentScreen = SCR_MENU;
          commandBuffer = "";
          return;

        } else {
          Serial.print(F("  command not found: "));
          Serial.println(commandBuffer);
          Serial.println(F("  escribe 'help' para ver los comandos"));
        }

        commandBuffer = "";
        Serial.println();
        printPrompt();
      }
    } else if (c == 8 || c == 127) {
      if (commandBuffer.length() > 0) {
        commandBuffer.remove(commandBuffer.length() - 1);
        Serial.write(8); Serial.write(' '); Serial.write(8);
      }
    } else if (c >= 32 && c <= 126) {
      commandBuffer += c;
      Serial.write(c);
    }
  }
}

static void drawPcModeScreen() {
  u8g2.clearBuffer();
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setDrawColor(1);
  u8g2.drawBox(16, 1, 96, 14);
  u8g2.setDrawColor(2);
  u8g2.drawStr(35, 11, "MODO PC");
  u8g2.setDrawColor(1);
  u8g2.drawXBM(0, 1, 16, 14, image_Layer_9_bits);
  u8g2.drawXBM(112, 1, 16, 14, image_Layer_9_bits);
  u8g2.drawXBM(48, 24, 32, 16, image_terminal_bits);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(25, 48, "Terminal activo");
  u8g2.drawStr(15, 58, "Ver monitor serial");
  u8g2.sendBuffer();
}

void serialMasterBegin() {}

// Duena total de la pantalla mientras currentScreen == SCR_PCMODE:
// dibuja, procesa el serial, y escucha su propio boton BACK.
void serialMasterLoop() {
  if (currentScreen != SCR_PCMODE) { pcModeInitialized = false; return; }

  if (!pcModeInitialized) {
    printBanner();
    printPrompt();
    pcModeInitialized = true;
  }

  processSerialCommand();
  if (currentScreen != SCR_PCMODE) return;   // 'exit' ya nos saco

  if (isButtonJustPressed(PIN_BACK)) {
    buzzerClick();
    pcModeInitialized = false;
    Serial.println();
    Serial.println(F("  saliendo de Modo PC..."));
    currentScreen = SCR_MENU;
    return;
  }

  drawPcModeScreen();
  neopixelTick(NEO_IDLE);
}

#else  // ---- nodo normal: funciones vacias ----

void serialMasterBegin() {}
void serialMasterLoop()  {}

#endif
