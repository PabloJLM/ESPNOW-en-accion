#include "ui.h"
#include <U8g2lib.h>
#include "mesh.h"
#include "neopixel.h"
#include "buzzer.h"
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
  "Easter egg"
};
static const Screen MENU_TARGET[] = {
  SCR_METRICS, SCR_NEIGHBORS, SCR_PING, SCR_SCANNER, SCR_EASTER
};
static const int MENU_COUNT = 5;
static int menuIdx = 0;

// scroll para listas
static int listOffset = 0;

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

static void header(const char* title) {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(2, 9, title);
  // id propio a la derecha
  char me[10];
  snprintf(me, sizeof(me), "[%s]", meshMyName());
  int w = u8g2.getStrWidth(me);
  u8g2.drawStr(SCREEN_W - w - 2, 9, me);
  u8g2.drawLine(0, 12, 127, 12);
}

// Recolecta nodos vivos en arr. Si scannerSort: ordena por RSSI
// (directos primero). Si no: directos por rssi, luego por saltos.
static int collectNodes(const MeshNode** arr, int maxN) {
  int n = 0;
  for (int i = 0; i < MAX_NODES && n < maxN; i++) {
    const MeshNode* nd = meshNodeAt(i);
    if (meshNodeAlive(nd)) arr[n++] = nd;
  }
  // ordenamiento burbuja (n pequeno): directos primero, luego mejor rssi / menos saltos
  for (int a = 0; a < n - 1; a++) {
    for (int b = 0; b < n - 1 - a; b++) {
      const MeshNode* x = arr[b];
      const MeshNode* y = arr[b + 1];
      bool swap = false;
      if (x->direct != y->direct) {
        swap = (!x->direct && y->direct);          // directos arriba
      } else if (x->direct) {
        swap = (x->rssi < y->rssi);                 // mejor rssi arriba
      } else {
        swap = (x->hops > y->hops);                 // menos saltos arriba
      }
      if (swap) { const MeshNode* t = arr[b]; arr[b] = arr[b + 1]; arr[b + 1] = t; }
    }
  }
  return n;
}

// ============================================================
//  Pantallas
// ============================================================
static void drawMenu() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(2, 9, "MeshNow");
  char sub[18];
  snprintf(sub, sizeof(sub), "%d nodos", meshNodeCount());
  int w = u8g2.getStrWidth(sub);
  u8g2.drawStr(SCREEN_W - w - 2, 9, sub);
  u8g2.drawLine(0, 12, 127, 12);

  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 24 + i * 10;
    if (i == menuIdx) {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(6, y, MENU_ITEMS[i]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(6, y, MENU_ITEMS[i]);
    }
  }
}

static void drawMetrics() {
  header("Metricas");
  MeshMetrics m = meshGetMetrics();
  char l[26];
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
    char l[26];
    if (nd->direct) snprintf(l, sizeof(l), "%s  directo %ddBm", id, nd->rssi);
    else            snprintf(l, sizeof(l), "%s  %d saltos", id, nd->hops);
    u8g2.drawStr(2, y, l);
  }
  // indicador de scroll
  if (n > rows) {
    char c[10]; snprintf(c, sizeof(c), "%d/%d", listOffset + 1, n);
    int w = u8g2.getStrWidth(c);
    u8g2.drawStr(SCREEN_W - w - 2, 62, c);
  }
  u8g2.drawStr(2, 62, "UP/DN mueve");
}

static void drawPing() {
  header("Ping activos");
  u8g2.setFont(u8g2_font_6x10_tr);

  if (meshPingInProgress()) {
    u8g2.drawStr(20, 34, "Sondeando...");
  } else {
    char big[20];
    snprintf(big, sizeof(big), "Activos: %d", meshPingResponders());
    u8g2.setFont(u8g2_font_ncenB10_tr);
    int w = u8g2.getStrWidth(big);
    u8g2.drawStr((SCREEN_W - w) / 2, 36, big);
  }
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 62, "SEL: sondear   BACK: menu");
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
    // recorta el texto a lo que cabe
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

    default: break;
  }
}

// ============================================================
//  API
// ============================================================
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
    case SCR_EASTER:    drawEaster();    break;
    default:            drawMenu();      break;
  }
  maybeShowToast();
  u8g2.sendBuffer();

  // neopixeles = salud de la malla (refresco suave)
  static uint32_t lastNeo = 0;
  if (millis() - lastNeo > 300) {
    lastNeo = millis();
    int best = -100;
    for (int i = 0; i < MAX_NODES; i++) {
      const MeshNode* nd = meshNodeAt(i);
      if (meshNodeAlive(nd) && nd->direct && nd->rssi > best) best = nd->rssi;
    }
    neopixelMeshStatus(meshDirectCount(), best);
  }
}
