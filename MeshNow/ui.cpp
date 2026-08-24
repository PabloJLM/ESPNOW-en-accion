#include "ui.h"
#include <U8g2lib.h>
#include "mesh.h"
#include "neopixel.h"
#include "buzzer.h"
#include "serialmaster.h"
#include "easteregg.h"

// Objetos globales definidos en el .ino
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

// ============================================================
//  Botones (mismo patron de debounce del proyecto original)
// ============================================================
static bool isButtonJustPressed(int pin) {
  static uint8_t lastStable[4] = {HIGH, HIGH, HIGH, HIGH};
  static uint8_t lastRead[4]   = {HIGH, HIGH, HIGH, HIGH};
  static unsigned long lastT[4] = {0, 0, 0, 0};

  const int pins[4] = {PIN_SELECT, PIN_UP, PIN_DOWN, PIN_BACK};
  int idx = -1;
  for (int i = 0; i < 4; i++) if (pins[i] == pin) { idx = i; break; }
  if (idx == -1) return false;

  uint8_t r = digitalRead(pin);
  if (r != lastRead[idx]) { lastT[idx] = millis(); lastRead[idx] = r; }
  if (millis() - lastT[idx] > 50) {
    if (lastStable[idx] == HIGH && r == LOW) { lastStable[idx] = r; return true; }
    lastStable[idx] = r;
  }
  return false;
}

// ============================================================
//  Menu
// ============================================================
static const char* MENU_ITEMS[] = {
  "Metricas",
  "Red / Vecinos",
  "Ping activos",
  "Scanner ESP-NOW",
  "Mensajes",
  "Ajustes",
  "Ayuda",
#if IS_MASTER
  "Modo PC",
  "Piano",
#endif
  "Easter egg"
};
static const Screen MENU_TARGET[] = {
  SCR_METRICS, SCR_NEIGHBORS, SCR_PING, SCR_SCANNER,
  SCR_MESSAGES, SCR_SETTINGS, SCR_HELP,
#if IS_MASTER
  SCR_PCMODE,
  SCR_PIANO,
#endif
  SCR_EASTER
};
static const int MENU_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
static int menuIdx = 0;

// scroll / cursor generico para listas (se resetea a 0 al entrar a cada pantalla)
static int listOffset = 0;

// pantalla Mensajes: 0 = eligiendo destino, 1 = eligiendo mensaje prehecho
static int      msgState    = 0;
static uint16_t msgTargetId = 0xFFFF;

// pantalla Piano (solo maestro): 0 = eligiendo nodo/buzzer, 1 = tocando
#if IS_MASTER
static const char* PIANO_NOTES[] = {"DO", "RE", "MI", "FA", "SOL", "LA", "SI", "DO2"};
static const int   PIANO_FREQS[] = { 262,  294,  330,  349,  392, 440, 494,  523 };
static const int   PIANO_NOTE_COUNT = sizeof(PIANO_NOTES) / sizeof(PIANO_NOTES[0]);
static const int   PIANO_NOTE_MS = 250;
static int      pianoState    = 0;
static uint16_t pianoTargetId = 0xFFFF;
static int      pianoNoteIdx  = 0;
#endif

// ============================================================
//  Utilidades de dibujo
// ============================================================
static int rssiToBars(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

// Dibuja un icono de 4 barras tipo senal wifi en (x,y) base.
static void drawBars(int x, int y, int bars) {
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;         // 2,4,6,8
    if (i < bars) u8g2.drawBox(x + i * 3, y - h, 2, h);
    else          u8g2.drawFrame(x + i * 3, y - h, 2, h);
  }
}

// Identidad propia visible en TODAS las pantallas: id (o "MAESTRO").
static void drawIdentity() {
  char tag[14];
#if IS_MASTER
  snprintf(tag, sizeof(tag), "[MAESTRO]");
#else
  snprintf(tag, sizeof(tag), "[%s]", meshMyName());
#endif
  u8g2.setFont(u8g2_font_6x10_tr);
  int w = u8g2.getStrWidth(tag);
  u8g2.drawStr(SCREEN_W - w - 2, 9, tag);
}

static void header(const char* title) {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(2, 9, title);
  drawIdentity();
  u8g2.drawLine(0, 12, 127, 12);
}

// Recolecta nodos vivos en arr. Directos primero (mejor rssi arriba),
// luego los alcanzables por salto (menos saltos arriba).
static int collectNodes(const MeshNode** arr, int maxN) {
  int n = 0;
  for (int i = 0; i < MAX_NODES && n < maxN; i++) {
    const MeshNode* nd = meshNodeAt(i);
    if (meshNodeAlive(nd)) arr[n++] = nd;
  }
  for (int a = 0; a < n - 1; a++) {
    for (int b = 0; b < n - 1 - a; b++) {
      const MeshNode* x = arr[b];
      const MeshNode* y = arr[b + 1];
      bool swap = false;
      if (x->direct != y->direct) {
        swap = (!x->direct && y->direct);
      } else if (x->direct) {
        swap = (x->rssi < y->rssi);
      } else {
        swap = (x->hops > y->hops);
      }
      if (swap) { const MeshNode* t = arr[b]; arr[b] = arr[b + 1]; arr[b + 1] = t; }
    }
  }
  return n;
}

// Destinos posibles para mandar un mensaje: "Todos" + cada nodo vivo.
static int collectTargets(uint16_t* ids, int maxN) {
  int n = 0;
  if (n < maxN) ids[n++] = 0xFFFF;
  for (int i = 0; i < MAX_NODES && n < maxN; i++) {
    const MeshNode* nd = meshNodeAt(i);
    if (meshNodeAlive(nd)) ids[n++] = nd->id;
  }
  return n;
}

// ============================================================
//  Pantallas
// ============================================================
static void drawMenu() {
  header("MeshNow");
  char sub[20];
  snprintf(sub, sizeof(sub), "%d nodos activos", meshNodeCount());
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 20, sub);
  if (MENU_COUNT > 4) {
    char c[10]; snprintf(c, sizeof(c), "%d/%d", menuIdx + 1, MENU_COUNT);
    int w = u8g2.getStrWidth(c);
    u8g2.drawStr(SCREEN_W - w - 2, 20, c);
  }

  const int rows = 4;
  int top = menuIdx - 1; if (top < 0) top = 0;
  if (top > MENU_COUNT - rows) top = (MENU_COUNT > rows) ? MENU_COUNT - rows : 0;

  u8g2.setFont(u8g2_font_6x10_tr);
  for (int i = 0; i < rows && (top + i) < MENU_COUNT; i++) {
    int idx = top + i;
    int y = 30 + i * 10;
    if (idx == menuIdx) {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(6, y, MENU_ITEMS[idx]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(6, y, MENU_ITEMS[idx]);
    }
  }
}

static void drawMetrics() {
  header("Metricas");
  MeshMetrics m = meshGetMetrics();
  char l[28];
  u8g2.setFont(u8g2_font_5x7_tr);

  snprintf(l, sizeof(l), "TX:%lu  RX:%lu", (unsigned long)m.tx, (unsigned long)m.rx);
  u8g2.drawStr(2, 22, l);
  snprintf(l, sizeof(l), "Relay:%lu  Dup:%lu", (unsigned long)m.relay, (unsigned long)m.dup);
  u8g2.drawStr(2, 32, l);
  snprintf(l, sizeof(l), "PPS:%u  RSSI:%d", m.pps, m.lastRssi);
  u8g2.drawStr(2, 42, l);
  snprintf(l, sizeof(l), "Vivos:%d  Directos:%d", meshNodeCount(), meshDirectCount());
  u8g2.drawStr(2, 52, l);

  u8g2.drawStr(2, 62, "BACK: menu");
}

static void drawNeighbors() {
  header("Red / Vecinos");
  const MeshNode* arr[MAX_NODES];
  int n = collectNodes(arr, MAX_NODES);

  u8g2.setFont(u8g2_font_5x7_tr);
  if (n == 0) {
    u8g2.drawStr(2, 30, "Buscando nodos...");
    return;
  }

  const int rows = 5;
  if (listOffset > n - rows) listOffset = (n > rows) ? n - rows : 0;
  if (listOffset < 0) listOffset = 0;

  for (int i = 0; i < rows && (listOffset + i) < n; i++) {
    const MeshNode* nd = arr[listOffset + i];
    int y = 22 + i * 8;
    char id[10]; meshFormatId(nd->id, id, sizeof(id));
    char l[28];
    if (nd->direct) snprintf(l, sizeof(l), "%s  directo %ddBm", id, nd->rssi);
    else            snprintf(l, sizeof(l), "%s  %d saltos", id, nd->hops);
    u8g2.drawStr(2, y, l);
  }
  if (n > rows) {
    char c[10]; snprintf(c, sizeof(c), "%d/%d", listOffset + 1, n);
    int w = u8g2.getStrWidth(c);
    u8g2.drawStr(SCREEN_W - w - 2, 62, c);
  }
  u8g2.drawStr(2, 62, "UP/DN mueve");
}

static void drawPing() {
  header("Ping activos");
  u8g2.setFont(u8g2_font_5x7_tr);

  if (meshPingInProgress()) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(20, 24, "Sondeando...");
    u8g2.setFont(u8g2_font_5x7_tr);
  } else {
    char big[20];
    snprintf(big, sizeof(big), "Activos: %d", meshPingResponders());
    u8g2.drawStr(2, 22, big);
    int n = meshPingResponders();
    for (int i = 0; i < 3 && i < n; i++) {
      char id[10]; meshFormatId(meshPingResponderId(i), id, sizeof(id));
      u8g2.drawStr(6, 31 + i * 8, id);
    }
  }

  if (meshLastPingFrom() != 0 && millis() - meshLastPingFromAt() < 15000) {
    char id[10]; meshFormatId(meshLastPingFrom(), id, sizeof(id));
    char l[26]; snprintf(l, sizeof(l), "Ping recibido de: %s", id);
    u8g2.drawStr(2, 55, l);
  }
  u8g2.drawStr(2, 62, "SEL: sondear  BACK: menu");
}

static void drawScanner() {
  header("Scanner ESP-NOW");
  const MeshNode* arr[MAX_NODES];
  int n = collectNodes(arr, MAX_NODES);

  u8g2.setFont(u8g2_font_5x7_tr);
  if (n == 0) { u8g2.drawStr(2, 30, "Escuchando..."); return; }

  const int rows = 5;
  if (listOffset > n - rows) listOffset = (n > rows) ? n - rows : 0;
  if (listOffset < 0) listOffset = 0;

  for (int i = 0; i < rows && (listOffset + i) < n; i++) {
    const MeshNode* nd = arr[listOffset + i];
    int y = 22 + i * 8;
    char id[10]; meshFormatId(nd->id, id, sizeof(id));
    u8g2.drawStr(2, y, id);
    if (nd->direct) {
      char rs[8]; snprintf(rs, sizeof(rs), "%d", nd->rssi);
      u8g2.drawStr(40, y, rs);
      drawBars(100, y, rssiToBars(nd->rssi));
    } else {
      char hp[10]; snprintf(hp, sizeof(hp), "%dsaltos", nd->hops);
      u8g2.drawStr(40, y, hp);
    }
  }
  if (n > rows) {
    char c[10]; snprintf(c, sizeof(c), "%d/%d", listOffset + 1, n);
    int w = u8g2.getStrWidth(c);
    u8g2.drawStr(SCREEN_W - w - 2, 62, c);
  }
}

// Etiqueta corta de un id de destino/origen ("Todos" o el id hex).
static void targetLabel(uint16_t id, char* out, size_t n) {
  if (id == 0xFFFF) snprintf(out, n, "Todos");
  else              meshFormatId(id, out, n);
}

static void drawMessages() {
  header("Mensajes");
  u8g2.setFont(u8g2_font_5x7_tr);

#if IS_MASTER
  // El maestro manda mensajes (prehechos o custom) desde Modo PC / serial;
  // aqui solo se ve el historial.
  int n = meshMsgHistCount();
  if (n == 0) { u8g2.drawStr(2, 30, "sin mensajes aun"); return; }
  const int rows = 5;
  if (listOffset > n - rows) listOffset = (n > rows) ? n - rows : 0;
  if (listOffset < 0) listOffset = 0;
  for (int i = 0; i < rows && (listOffset + i) < n; i++) {
    const MeshMsgItem* it = meshMsgHistAt(listOffset + i);
    char from[10]; meshFormatId(it->from, from, sizeof(from));
    char to[10]; targetLabel(it->to, to, sizeof(to));
    char l[30]; snprintf(l, sizeof(l), "%s>%s: %s", from, to, it->text);
    u8g2.drawStr(2, 22 + i * 8, l);
  }
#else
  // El nodo elige destino (Todos o un vecino especifico) y luego un
  // mensaje prehecho del catalogo compartido.
  uint16_t targets[MAX_NODES + 1];
  int tCount = collectTargets(targets, MAX_NODES + 1);

  if (msgState == 0) {
    if (listOffset >= tCount) listOffset = tCount - 1;
    if (listOffset < 0) listOffset = 0;
    u8g2.drawStr(2, 20, "Destino:");
    const int rows = 4;
    int top = listOffset - 1; if (top < 0) top = 0;
    if (top > tCount - rows) top = (tCount > rows) ? tCount - rows : 0;
    for (int i = 0; i < rows && (top + i) < tCount; i++) {
      int idx = top + i;
      int y = 30 + i * 9;
      char lbl[10]; targetLabel(targets[idx], lbl, sizeof(lbl));
      u8g2.drawStr(2, y, (idx == listOffset) ? ">" : " ");
      u8g2.drawStr(10, y, lbl);
    }
    u8g2.drawStr(2, 62, "SEL: elegir  BACK: menu");
  } else {
    char dstLbl[10]; targetLabel(msgTargetId, dstLbl, sizeof(dstLbl));
    char hdr[20]; snprintf(hdr, sizeof(hdr), "A: %s", dstLbl);
    u8g2.drawStr(2, 20, hdr);

    int cCount = meshCannedCount();
    int cur = listOffset;
    if (cur >= cCount) cur = cCount - 1;
    if (cur < 0) cur = 0;
    const int rows = 3;
    int top = cur - 1; if (top < 0) top = 0;
    if (top > cCount - rows) top = (cCount > rows) ? cCount - rows : 0;
    for (int i = 0; i < rows && (top + i) < cCount; i++) {
      int idx = top + i;
      int y = 30 + i * 9;
      u8g2.drawStr(2, y, (idx == cur) ? ">" : " ");
      u8g2.drawStr(10, y, meshCannedAt(idx));
    }
    u8g2.drawStr(2, 62, "SEL: enviar  BACK: destino");
  }
#endif
}

static void drawSettings() {
  header("Ajustes");
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 30, "Proximamente...");
  u8g2.drawStr(2, 62, "BACK: menu");
}

static void drawHelp() {
  header("Ayuda");
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 30, "Proximamente...");
  u8g2.drawStr(2, 62, "BACK: menu");
}

// SCR_PCMODE (maestro) es duena de si misma: serialmaster.cpp dibuja
// su propio buffer y lee su propio boton BACK (ver serialMasterLoop).
// Aqui solo queda el caso del nodo esclavo, que no tiene esa pantalla.
static void drawPcMode() {
  header("Modo PC");
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 30, "Solo el nodo maestro");
  u8g2.drawStr(2, 40, "tiene Modo PC.");
  u8g2.drawStr(2, 62, "BACK: menu");
}

// Piano remoto (solo maestro): elige un nodo y le suena SU buzzer, no
// el del maestro (ver meshSendBuzz / mesh.cpp).
static void drawPiano() {
  header("Piano remoto");
  u8g2.setFont(u8g2_font_5x7_tr);
#if IS_MASTER
  uint16_t targets[MAX_NODES + 1];
  int tCount = collectTargets(targets, MAX_NODES + 1);

  if (pianoState == 0) {
    if (listOffset >= tCount) listOffset = tCount - 1;
    if (listOffset < 0) listOffset = 0;
    u8g2.drawStr(2, 20, "Nodo (buzzer):");
    const int rows = 4;
    int top = listOffset - 1; if (top < 0) top = 0;
    if (top > tCount - rows) top = (tCount > rows) ? tCount - rows : 0;
    for (int i = 0; i < rows && (top + i) < tCount; i++) {
      int idx = top + i;
      int y = 30 + i * 9;
      char lbl[10]; targetLabel(targets[idx], lbl, sizeof(lbl));
      u8g2.drawStr(2, y, (idx == listOffset) ? ">" : " ");
      u8g2.drawStr(10, y, lbl);
    }
    u8g2.drawStr(2, 62, "SEL: elegir  BACK: menu");
  } else {
    char dstLbl[10]; targetLabel(pianoTargetId, dstLbl, sizeof(dstLbl));
    char hdr[24]; snprintf(hdr, sizeof(hdr), "Buzzer: %s", dstLbl);
    u8g2.drawStr(2, 20, hdr);

    u8g2.setFont(u8g2_font_7x14_tr);
    const char* n = PIANO_NOTES[pianoNoteIdx];
    int w = u8g2.getStrWidth(n);
    u8g2.drawStr((SCREEN_W - w) / 2, 42, n);
    u8g2.setFont(u8g2_font_5x7_tr);
    if (pianoNoteIdx > 0)                   u8g2.drawStr(4, 42, "<");
    if (pianoNoteIdx < PIANO_NOTE_COUNT - 1) u8g2.drawStr(SCREEN_W - 8, 42, ">");

    u8g2.drawStr(2, 62, "UP/DN nota SEL toca BACK<");
  }
#else
  u8g2.drawStr(2, 30, "Solo el nodo maestro");
  u8g2.drawStr(2, 40, "tiene Piano.");
  u8g2.drawStr(2, 62, "BACK: menu");
#endif
}

static void drawEaster() {
  u8g2.drawXBMP(0, 0, EASTER_W, EASTER_H, easter_bits);
  u8g2.setDrawColor(0);
  u8g2.drawStr(4, 13, "Slow");
  u8g2.setDrawColor(1);
  u8g2.setDrawColor(0);
  u8g2.drawStr(4, 24, "Summer");
  u8g2.setDrawColor(1);
  u8g2.setDrawColor(0);
  u8g2.drawStr(4, 33, "Eve");
  u8g2.setDrawColor(1);
}

// ---- toast de texto recibido (se dibuja encima de todo) ----
static uint32_t toastUntil = 0;
static void maybeShowToast() {
  if (meshHasNewText()) {
    toastUntil = millis() + 4000;
    meshClearNewText();
    buzzerBeep();
    neopixelFlashMessage();
  }
  if (millis() < toastUntil) {
    char from[10]; meshFormatId(meshLastTextFrom(), from, sizeof(from));
    u8g2.setDrawColor(1);
    u8g2.drawBox(2, 20, 124, 24);
    u8g2.setDrawColor(0);
    u8g2.drawFrame(2, 20, 124, 24);
    u8g2.setFont(u8g2_font_5x7_tr);
    char hdr[22]; snprintf(hdr, sizeof(hdr), "MSG de %s:", from);
    u8g2.drawStr(6, 30, hdr);
    char body[24];
    strncpy(body, meshLastText(), sizeof(body) - 1);
    body[sizeof(body) - 1] = '\0';
    u8g2.drawStr(6, 40, body);
    u8g2.setDrawColor(1);
  }
}

// ============================================================
//  Botones por pantalla
// ============================================================
static void handleButtons() {
  if (currentScreen == SCR_MENU) {
    if (isButtonJustPressed(PIN_UP))   { buzzerClick(); menuIdx = (menuIdx + MENU_COUNT - 1) % MENU_COUNT; }
    if (isButtonJustPressed(PIN_DOWN)) { buzzerClick(); menuIdx = (menuIdx + 1) % MENU_COUNT; }
    if (isButtonJustPressed(PIN_SELECT)) {
      buzzerBeep();
      listOffset = 0;
      msgState   = 0;
#if IS_MASTER
      pianoState = 0;
#endif
      currentScreen = MENU_TARGET[menuIdx];
    }
    return;
  }

  // BACK vuelve al menu, EXCEPTO en Mensajes/paso "elegir mensaje" o
  // Piano/paso "tocando": ahi regresa un paso (no bota toda la pantalla).
  if (isButtonJustPressed(PIN_BACK)) {
#if !IS_MASTER
    if (currentScreen == SCR_MESSAGES && msgState == 1) {
      buzzerClick(); msgState = 0; listOffset = 0; return;
    }
#endif
#if IS_MASTER
    if (currentScreen == SCR_PIANO && pianoState == 1) {
      buzzerClick(); pianoState = 0; listOffset = 0; return;
    }
#endif
    buzzerClick(); currentScreen = SCR_MENU; return;
  }

  switch (currentScreen) {
    case SCR_PING:
      if (isButtonJustPressed(PIN_SELECT)) { buzzerBeep(); meshSendPing(); }
      break;

    case SCR_NEIGHBORS:
    case SCR_SCANNER:
      if (isButtonJustPressed(PIN_UP))   { listOffset--; }
      if (isButtonJustPressed(PIN_DOWN)) { listOffset++; }
      break;

    case SCR_MESSAGES: {
#if IS_MASTER
      // el maestro solo ve el historial aqui; para mandar usa Modo PC/serial
      int n = meshMsgHistCount();
      if (isButtonJustPressed(PIN_UP))   { if (listOffset > 0) listOffset--; }
      if (isButtonJustPressed(PIN_DOWN)) { if (listOffset < n - 1) listOffset++; }
#else
      uint16_t targets[MAX_NODES + 1];
      int tCount = collectTargets(targets, MAX_NODES + 1);
      if (msgState == 0) {
        if (isButtonJustPressed(PIN_UP))   { buzzerClick(); listOffset = (listOffset - 1 + tCount) % tCount; }
        if (isButtonJustPressed(PIN_DOWN)) { buzzerClick(); listOffset = (listOffset + 1) % tCount; }
        if (isButtonJustPressed(PIN_SELECT)) {
          buzzerClick();
          msgTargetId = targets[listOffset];
          msgState    = 1;
          listOffset  = 0;
        }
      } else {
        int cCount = meshCannedCount();
        if (isButtonJustPressed(PIN_UP))   { buzzerClick(); listOffset = (listOffset - 1 + cCount) % cCount; }
        if (isButtonJustPressed(PIN_DOWN)) { buzzerClick(); listOffset = (listOffset + 1) % cCount; }
        if (isButtonJustPressed(PIN_SELECT)) {
          buzzerBeep();
          meshSendText(meshCannedAt(listOffset), msgTargetId);
          msgState   = 0;
          listOffset = 0;
        }
      }
#endif
      break;
    }

#if IS_MASTER
    case SCR_PIANO: {
      uint16_t targets[MAX_NODES + 1];
      int tCount = collectTargets(targets, MAX_NODES + 1);
      if (pianoState == 0) {
        if (isButtonJustPressed(PIN_UP))   { buzzerClick(); listOffset = (listOffset - 1 + tCount) % tCount; }
        if (isButtonJustPressed(PIN_DOWN)) { buzzerClick(); listOffset = (listOffset + 1) % tCount; }
        if (isButtonJustPressed(PIN_SELECT)) {
          buzzerClick();
          pianoTargetId = targets[listOffset];
          pianoState    = 1;
          listOffset    = 0;
        }
      } else {
        if (isButtonJustPressed(PIN_UP))   { if (pianoNoteIdx > 0) { pianoNoteIdx--; buzzerClick(); } }
        if (isButtonJustPressed(PIN_DOWN)) { if (pianoNoteIdx < PIANO_NOTE_COUNT - 1) { pianoNoteIdx++; buzzerClick(); } }
        if (isButtonJustPressed(PIN_SELECT)) {
          meshSendBuzz(PIANO_FREQS[pianoNoteIdx], PIANO_NOTE_MS, pianoTargetId);
        }
      }
      break;
    }
#endif

    default: break;
  }
}

// ============================================================
//  API
// ============================================================
void uiBegin() {
  menuIdx = 0;
  listOffset = 0;
  msgState = 0;
  msgTargetId = 0xFFFF;
#if IS_MASTER
  pianoState    = 0;
  pianoTargetId = 0xFFFF;
  pianoNoteIdx  = 0;
#endif
}

void uiLoop() {
#if IS_MASTER
  // Modo PC es duena total de su pantalla (dibuja, lee serial, y su
  // propio boton BACK) mientras esta activa -- ver serialMasterLoop().
  if (currentScreen == SCR_PCMODE) {
    serialMasterLoop();
    return;
  }
#endif

  handleButtons();

  u8g2.clearBuffer();
  u8g2.setFontMode(1);
  switch (currentScreen) {
    case SCR_MENU:      drawMenu();      break;
    case SCR_METRICS:   drawMetrics();   break;
    case SCR_NEIGHBORS: drawNeighbors(); break;
    case SCR_PING:      drawPing();      break;
    case SCR_SCANNER:   drawScanner();   break;
    case SCR_MESSAGES:  drawMessages();  break;
    case SCR_SETTINGS:  drawSettings();  break;
    case SCR_HELP:      drawHelp();      break;
    case SCR_PCMODE:    drawPcMode();    break;
    case SCR_PIANO:     drawPiano();     break;
    case SCR_EASTER:    drawEaster();    break;
    default:            drawMenu();      break;
  }
  maybeShowToast();
  u8g2.sendBuffer();

  NeoMode mode = (currentScreen == SCR_SCANNER) ? NEO_SCANNER : NEO_IDLE;
  neopixelTick(mode);
}
