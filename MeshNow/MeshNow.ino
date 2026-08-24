// ============================================================
//  MeshNow  -  malla ESP-NOW multi-salto para ESP32-C6
//
//  Mismo firmware para todas las placas. Cambia solo el flag
//  IS_MASTER (en config.h):
//     IS_MASTER 1 -> la placa que conectas a la PC con LabVIEW
//     IS_MASTER 0 -> el resto de nodos
//
//  Pantallas (menu con UP/DOWN/SELECT, BACK vuelve al menu):
//     1. Menu principal
//     2. Metricas   (tx/rx/relay/dup/pps/rssi)
//     3. Red/Vecinos (directos con RSSI + alcanzables por salto)
//     4. Ping activos (SEL lanza barrido y cuenta respuestas)
//     5. Easter egg  (pixel art en easteregg.h, cambialo tu)
//     6. Scanner ESP-NOW (RSSI en vivo, barras de senal)
//
//  Hardware: mismo pinout del proyecto camioneta (SH1106 I2C,
//  4 botones, 9 NeoPixels, buzzer).
//
//  Requiere: U8g2, Adafruit_NeoPixel (ESP-NOW viene con el core).
// ============================================================
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "mesh.h"
#include "ui.h"
#include "neopixel.h"
#include "buzzer.h"
#include "serialmaster.h"

// Pantalla (igual que camioneta)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);

// Neopixeles
Adafruit_NeoPixel strip(NUM_PIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// Variables RGB (por compatibilidad con config.h)
uint8_t neoR = 0;
uint8_t neoG = 255;
uint8_t neoB = 0;

// Empieza en el menu
Screen currentScreen = SCR_MENU;

static void splash() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* t = "MeshNow";
  int w = u8g2.getStrWidth(t);
  u8g2.drawStr((SCREEN_W - w) / 2, 23, t);
  u8g2.setFont(u8g2_font_5x7_tr);
  char sub[24];
  snprintf(sub, sizeof(sub), "nodo %s  %s", meshMyName(), IS_MASTER ? "(MAESTRO)" : "");
  int w2 = u8g2.getStrWidth(sub);
  u8g2.drawStr((SCREEN_W - w2) / 2, 48, sub);
  u8g2.drawRFrame(25, 11, 78, 16, 3);
  u8g2.sendBuffer();
  buzzerNote(880, 90);
  buzzerNote(1320, 120);
  delay(1000);
  
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_SELECT, INPUT_PULLUP);
  pinMode(PIN_UP,     INPUT_PULLUP);
  pinMode(PIN_DOWN,   INPUT_PULLUP);
  pinMode(PIN_BACK,   INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED1,   OUTPUT);

  neopixelInit();
  u8g2.begin();

  meshBegin();          // arranca ESP-NOW (fija canal, MAC, peer broadcast)
  uiBegin();
  serialMasterBegin();  // no-op si no es maestro

  splash();
}

void loop() {
  meshLoop();           // procesa cola RX, HELLO, ping, reenvios
  uiLoop();             // botones + pantalla + neopixels (Modo PC incluido)

  // LED1 parpadea suave = "vivo"
  digitalWrite(PIN_LED1, (millis() / 500) & 1);

  delay(5);
}
