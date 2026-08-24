#include "ui.h"
#include <U8g2lib.h>
#include "mesh.h"
#include "neopixel.h"
#include "buzzer.h"
#include "serialmaster.h"
#include "easteregg.h"

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

//botones 
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

// menuu
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
#endif
  "Easter egg"
};
static const Screen MENU_TARGET[] = {
  SCR_METRICS, SCR_NEIGHBORS, SCR_PING, SCR_SCANNER,
  SCR_MESSAGES, SCR_SETTINGS, SCR_HELP,
#if IS_MASTER
  SCR_PCMODE,
#endif
  SCR_EASTER
};
static const int MENU_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
static int menuIdx = 0;

static int listOffset = 0;

// mensajes custom que puede mandar el maestro (no hay teclado en la placa)-----------------------------------------------------
static const char* CANNED_MSGS[] = {
  "Todo OK", "Necesito ayuda", "Reagrupar aqui",
  "Objetivo visto", "Retirada", "Cargando bateria"
};
static const int CANNED_COUNT = sizeof(CANNED_MSGS) / sizeof(CANNED_MSGS[0]);


static int rssiToBars(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}


static void drawBars(int x, int y, int bars) {
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;         // 2,4,6,8
    if (i < bars) u8g2.drawBox(x + i * 3, y - h, 2, h);
    else          u8g2.drawFrame(x + i * 3, y - h, 2, h);
  }
}


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

// pantallas------------------
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

  int bestRssi = -127; uint16_t bestVal = 0; uint16_t bestId = 0; bool any = false;
  for (int i = 0; i < MAX_NODES; i++) {
    const MeshNode* nd = meshNodeAt(i);
    if (meshNodeAlive(nd) && nd->hasSensor && nd->direct && nd->rssi > bestRssi) {
      bestRssi = nd->rssi; bestVal = nd->sensorVal; bestId = nd->id; any = true;
    }
  }
  if (any) {
    char id[10]; meshFormatId(bestId, id, sizeof(id));
    snprintf(l, sizeof(l), "Mas cerca: %s v=%u %ddBm", id, bestVal, bestRssi);
    u8g2.drawStr(2, 62, l);
  } else {
    u8g2.drawStr(2, 62, "BACK: menu");
  }
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
    char sens[8] = "";
    if (nd->hasSensor) snprintf(sens, sizeof(sens), " s%u", nd->sensorVal);
    char l[28];
    if (nd->direct) snprintf(l, sizeof(l), "%s  %ddBm%s", id, nd->rssi, sens);
    else            snprintf(l, sizeof(l), "%s  %dsalt%s", id, nd->hops, sens);
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

static void drawMessages() {
  header("Mensajes");
  u8g2.setFont(u8g2_font_5x7_tr);

#if IS_MASTER
  if (listOffset < 0) listOffset = 0;
  if (listOffset >= CANNED_COUNT) listOffset = CANNED_COUNT - 1;
  const int rows = 3;
  int top = listOffset - 1; if (top < 0) top = 0;
  if (top > CANNED_COUNT - rows) top = (CANNED_COUNT > rows) ? CANNED_COUNT - rows : 0;
  for (int i = 0; i < rows && (top + i) < CANNED_COUNT; i++) {
    int idx = top + i;
    int y = 21 + i * 9;
    u8g2.drawStr(2, y, (idx == listOffset) ? ">" : " ");
    u8g2.drawStr(10, y, CANNED_MSGS[idx]);
  }
  u8g2.drawLine(0, 49, 127, 49);
  if (meshMsgHistCount() > 0) {
    const MeshMsgItem* it = meshMsgHistAt(0);
    char id[10]; meshFormatId(it->from, id, sizeof(id));
    char l[28]; snprintf(l, sizeof(l), "%s: %s", id, it->text);
    u8g2.drawStr(2, 58, l);
  } else {
    u8g2.drawStr(2, 58, "sin mensajes aun");
  }
#else
  int n = meshMsgHistCount();
  if (n == 0) { u8g2.drawStr(2, 30, "sin mensajes aun"); return; }
  const int rows = 5;
  if (listOffset > n - rows) listOffset = (n > rows) ? n - rows : 0;
  if (listOffset < 0) listOffset = 0;
  for (int i = 0; i < rows && (listOffset + i) < n; i++) {
    const MeshMsgItem* it = meshMsgHistAt(listOffset + i);
    char id[10]; meshFormatId(it->from, id, sizeof(id));
    char l[28]; snprintf(l, sizeof(l), "%s: %s", id, it->text);
    u8g2.drawStr(2, 22 + i * 8, l);
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

static void drawPcMode() {
  header("Modo PC");
  u8g2.setFont(u8g2_font_5x7_tr);
#if IS_MASTER
  char l[28];
  snprintf(l, sizeof(l), "Volcados: %lu", (unsigned long)serialMasterDumpCount());
  u8g2.drawStr(2, 24, l);
  snprintf(l, sizeof(l), "Comandos: %lu", (unsigned long)serialMasterCmdCount());
  u8g2.drawStr(2, 34, l);
  uint32_t lastRx = serialMasterLastRxAt();
  if (lastRx == 0) snprintf(l, sizeof(l), "Sin comandos aun");
  else snprintf(l, sizeof(l), "Ultimo cmd hace %lus", (unsigned long)((millis() - lastRx) / 1000));
  u8g2.drawStr(2, 44, l);
  u8g2.drawStr(2, 54, "USB 115200 -> LabVIEW");
#else
  u8g2.drawStr(2, 30, "Solo el nodo maestro");
  u8g2.drawStr(2, 40, "tiene Modo PC.");
#endif
  u8g2.drawStr(2, 62, "BACK: menu");
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

static void handleButtons() {
  if (currentScreen == SCR_MENU) {
    if (isButtonJustPressed(PIN_UP))   { buzzerClick(); menuIdx = (menuIdx + MENU_COUNT - 1) % MENU_COUNT; }
    if (isButtonJustPressed(PIN_DOWN)) { buzzerClick(); menuIdx = (menuIdx + 1) % MENU_COUNT; }
    if (isButtonJustPressed(PIN_SELECT)) {
      buzzerBeep();
      listOffset = 0;
      currentScreen = MENU_TARGET[menuIdx];
    }
    return;
  }

  // en cualquier sub-pantalla, BACK vuelve al menu
  if (isButtonJustPressed(PIN_BACK)) { buzzerClick(); currentScreen = SCR_MENU; return; }

  switch (currentScreen) {
    case SCR_PING:
      if (isButtonJustPressed(PIN_SELECT)) { buzzerBeep(); meshSendPing(); }
      break;

    case SCR_NEIGHBORS:
    case SCR_SCANNER:
      if (isButtonJustPressed(PIN_UP))   { listOffset--; }
      if (isButtonJustPressed(PIN_DOWN)) { listOffset++; }
      break;

    case SCR_MESSAGES:
#if IS_MASTER
      if (isButtonJustPressed(PIN_UP))   { buzzerClick(); if (listOffset > 0) listOffset--; }
      if (isButtonJustPressed(PIN_DOWN)) { buzzerClick(); if (listOffset < CANNED_COUNT - 1) listOffset++; }
      if (isButtonJustPressed(PIN_SELECT)) { buzzerBeep(); meshSendText(CANNED_MSGS[listOffset]); }
#else
      if (isButtonJustPressed(PIN_UP))   { if (listOffset > 0) listOffset--; }
      if (isButtonJustPressed(PIN_DOWN)) { listOffset++; }
#endif
      break;

    default: break;
  }
}


void uiBegin() {
  menuIdx = 0;
  listOffset = 0;
}

void uiLoop() {
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
    case SCR_EASTER:    drawEaster();    break;
    default:            drawMenu();      break;
  }
  maybeShowToast();
  u8g2.sendBuffer();

  NeoMode mode = (currentScreen == SCR_SCANNER) ? NEO_SCANNER : NEO_IDLE;
  neopixelTick(mode);
}
