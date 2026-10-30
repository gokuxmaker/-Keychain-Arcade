/*
  =============================================================================
  KEYCHAIN ARCADE - Halloween 2-in-1 Retro System
  Board:       Seeed Studio XIAO nRF52840 (or XIAO nRF52840 Plus)
  Display:     0.96" IPS ST7789 TFT (80x160 resolution, landscape 160x80)
  Driver:      Seeed_GFX2 with Full Double-Buffering & Fast DMA Stream
  Theme:       Halloween Retro Edition (Full-Screen Sunset & Dark Graveyard)
  Buttons:     D6 (BTN_USR1) / D7 (BTN_USR2)
  Storage:     InternalFS (LittleFS) non-volatile Save Data

  GAMES INCLUDED:
    1. GOST RUN    - Full-Screen Cute Ghost Endless Runner (Sunset Sky, No Black Bars!)
    2. GHOST PONG  - Full-Screen Dark Moody Graveyard Bone Pong
                     (User on Right with Bone Paddle vs CPU on Left)
  =============================================================================
*/

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <Seeed_GFX.h>
#include "board/boards/XIAO_LCD_Board.h"
#include "driver/tft/Driver_ST7789.h"
#include "panel/Panel_TFT.h"
#include <nrf.h>
#include <nrf_gpio.h>
#include "PongBg.h"

using namespace Adafruit_LittleFS_Namespace;

// =============================================================================
// ── USER GAMEPLAY CONFIGURATION ──────────────────────────────────────────────
// =============================================================================
// 1. GHOST RUNNER PHYSICS:
static constexpr float CONFIG_GHOST_GRAVITY      = 0.88f; // Fast, snappy jump drop
static constexpr float CONFIG_GHOST_JUMP_IMPULSE = -7.4f; // High upward leap
static constexpr float CONFIG_RUNNER_START_SPEED = 3.8f;  // Initial running speed
static constexpr float CONFIG_RUNNER_MAX_SPEED   = 7.5f;  // Top speed
static constexpr float CONFIG_RUNNER_ACCEL       = 0.0012f;

// 2. GHOST PONG PADDLE SPEED:
static constexpr float CONFIG_PONG_PADDLE_SPEED  = 2.8f;

// 3. SCREEN ROTATION:
//    3 = Inverted Landscape (right-side up for keychain hanging)
//    1 = Normal Landscape (flip 180 if needed)
#ifndef SCREEN_ROTATION
#define SCREEN_ROTATION 3
#endif

// ── Pin Definitions ────────────────────────────────────────────────────────
static constexpr int8_t LCD_RST_PIN   = 38;  // Display Reset (raw GPIO on Plus)
static constexpr int8_t LCD_BL_PIN    = 37;  // Display Backlight (raw GPIO)
static constexpr uint8_t PIN_BTN_JUMP  = D6; // USR1: Action / Select / Down
static constexpr uint8_t PIN_BTN_RESET = D7; // USR2: Toggle / Up

// Battery measurement pins (onboard XIAO nRF52840 circuit)
static constexpr uint8_t READ_BAT_P0_PIN = 14; // P0.14: Active-low divider enable
static constexpr uint8_t CHG_P0_PIN      = 17; // P0.17: Active-low charging detect
#ifndef PIN_VBAT
#define PIN_VBAT 35
#endif

// ── Geometry (Landscape 160x80) ───────────────────────────────────────────
static constexpr int16_t SCREEN_W       = 160;
static constexpr int16_t SCREEN_H       = 80;
static constexpr int16_t GROUND_Y       = 64;  // Ground silhouette trail (Full-Screen Sky, No Black Bar!)
static constexpr int16_t MOON_X         = 136;
static constexpr int16_t MOON_Y         = 14;
static constexpr int16_t MOON_R         = 8;

static constexpr int16_t GHOST_W        = 24;  // Cute Ghost Dimensions (24x25 pixels)
static constexpr int16_t GHOST_H        = 25;
static constexpr float   GHOST_X        = 16.0f;
static constexpr int16_t GHOST_GROUND_Y = GROUND_Y - GHOST_H; // 39

// ── Colors (RGB565) ────────────────────────────────────────────────────────
static constexpr uint16_t C_TRANS          = 0xF81F; // Magenta chroma-key
static constexpr uint16_t C_BLACK          = 0x0000;
static constexpr uint16_t C_WHITE          = 0xFFFF;
static constexpr uint16_t C_MOON_YELLOW    = 0xFFE0;
static constexpr uint16_t C_MOON_CRATER    = 0xFEC0;
static constexpr uint16_t C_MOON_AURA      = 0xC40F; // Warm sunset moon aura
static constexpr uint16_t C_TRAIL_SIL      = 0x1842; // Silhouette trail line
static constexpr uint16_t C_TREE_SIL       = 0x3106; // Spooky dead tree silhouette
static constexpr uint16_t C_ORANGE         = 0xFD20; // Halloween vibrant orange
static constexpr uint16_t C_GOLD           = 0xFEA0; // Trophy / High score gold
static constexpr uint16_t C_CYAN           = 0x07FF; // Electric cyan
static constexpr uint16_t C_PURPLE         = 0x915F; // Spooky twilight purple
static constexpr uint16_t C_RED            = 0xF800; // Crimson red

// ── Full-Screen Sunset Sky Gradient for Runner (All 80 rows: Dusk Blue -> Pink -> Orange -> Amber)
// Eliminates the black bar completely!
static const uint16_t SUNSET_SKY[80] = {
  0x10CB, 0x10CB, 0x10CB, 0x10CB, 0x10CB, 0x10CB, 0x10CB, 0x18CB,
  0x18CC, 0x18CC, 0x18CC, 0x18CC, 0x18EC, 0x18EC, 0x20EC, 0x20ED,
  0x20ED, 0x20ED, 0x20ED, 0x28ED, 0x28ED, 0x28ED, 0x30ED, 0x30ED,
  0x38ED, 0x38ED, 0x38ED, 0x40ED, 0x40EE, 0x48EE, 0x48EE, 0x48EE,
  0x50EE, 0x50EE, 0x590E, 0x590E, 0x610E, 0x690E, 0x690E, 0x710E,
  0x790E, 0x810E, 0x810E, 0x890D, 0x910D, 0x990D, 0x990D, 0xA10D,
  0xA92D, 0xA92D, 0xB12C, 0xB14C, 0xB94B, 0xB94B, 0xC16A, 0xC16A,
  0xC989, 0xC989, 0xD188, 0xD1A8, 0xD9A7, 0xD9C7, 0xE1E6, 0xE206,
  0xE225, 0xEA45, 0xEA64, 0xEA84, 0xF2A3, 0xF2C3, 0xF2E2, 0xF302,
  0xF322, 0xF362, 0xF381, 0xF3A1, 0xFBC1, 0xFBE1, 0xFC01, 0xFC21
};

// ── Twinkling Stars ───────────────────────────────────────────────────────
struct Star { int16_t x; int16_t y; };
static const Star g_stars[] = {
  {14, 4}, {38, 8}, {62, 3}, {82, 11},
  {105, 6}, {122, 10}, {148, 5}, {26, 14},
  {50, 18}, {92, 16}, {112, 19}, {8, 12}
};
static constexpr int NUM_STARS = sizeof(g_stars) / sizeof(g_stars[0]);

// ── System Game Mode States ───────────────────────────────────────────────
enum SystemMode {
  MODE_BOOT = 0,
  MODE_MENU,
  MODE_PLAYING,
  MODE_GAMEOVER
};

enum GameID {
  GAME_RUNNER = 0,
  GAME_PONG,
  NUM_GAMES
};

struct Obstacle {
  bool    active;
  float   x;
  int16_t y;
  uint8_t type;   // 0 = Pumpkin, 1 = Tombstone, 2 = Bat
  uint8_t width;
  uint8_t height;
  uint8_t anim;
};

static SystemMode g_systemMode   = MODE_BOOT;
static GameID     g_selectedGame = GAME_RUNNER;
static uint32_t   g_bootStartMs  = 0;
static uint32_t   g_menuHoldStartMs = 0;

// Display & Double-buffer canvas objects
Seeed_GFX display;
Seeed_Sprite canvas;

// ── Fast Full-Screen DMA / SPI Stream Transfer ────────────────────────────
static inline void flushCanvasFast() {
  display.setAddrWindow(0, 0, SCREEN_W, SCREEN_H);
  display.panelPtr()->writePixels((const uint16_t*)canvas.getPointer(), (size_t)SCREEN_W * SCREEN_H);
}

// ── Shared Scene Renderers ────────────────────────────────────────────────
// 1. Full-Screen Sunset Sky Background (for Gost Run - Zero Black Bars!)
static void renderBaseSunsetSky(bool showMoon = true) {
  // 80-row gradient fills the entire screen
  for (int16_t y = 0; y < SCREEN_H; ++y) {
    canvas.drawFastHLine(0, y, SCREEN_W, SUNSET_SKY[y]);
  }
  for (int i = 0; i < NUM_STARS; ++i) {
    canvas.drawPixel(g_stars[i].x, g_stars[i].y, C_WHITE);
  }
  if (showMoon) {
    canvas.fillCircle(MOON_X, MOON_Y, MOON_R + 1, C_MOON_AURA);
    canvas.fillCircle(MOON_X, MOON_Y, MOON_R, C_MOON_YELLOW);
    canvas.fillCircle(MOON_X - 2, MOON_Y - 1, 2, C_MOON_CRATER);
    canvas.fillCircle(MOON_X + 2, MOON_Y + 3, 1, C_MOON_CRATER);
  }

  // Horizon trees & silhouettes
  canvas.drawFastVLine(75, 52, 10, C_TREE_SIL);
  canvas.drawLine(75, 56, 70, 52, C_TREE_SIL);
  canvas.drawLine(75, 54, 79, 50, C_TREE_SIL);
  canvas.drawFastVLine(148, 50, 12, C_TREE_SIL);
  canvas.drawLine(148, 54, 144, 50, C_TREE_SIL);
  canvas.drawLine(148, 52, 153, 48, C_TREE_SIL);

  // Subtle ground silhouette trail (Sunset sky flows through without any black block!)
  canvas.drawFastHLine(0, GROUND_Y, SCREEN_W, C_TRAIL_SIL);
  for (int16_t gx = 6; gx < SCREEN_W; gx += 16) {
    canvas.drawPixel(gx, GROUND_Y - 1, C_TRAIL_SIL);
    canvas.drawPixel(gx + 1, GROUND_Y - 2, C_TRAIL_SIL);
    canvas.drawPixel(gx + 2, GROUND_Y - 1, C_TRAIL_SIL);
  }
}

// 2. Full-Screen Dark Moody Graveyard Background (for Ghost Pong - Bright spot removed!)
static inline void renderPongBackground() {
  memcpy(canvas.getPointer(), PONG_BG, sizeof(PONG_BG));

  // Subtle top & bottom boundary rails
  canvas.drawFastHLine(0, 0, SCREEN_W, 0x0926);
  canvas.drawFastHLine(0, 1, SCREEN_W, 0x1188);
  canvas.drawFastHLine(0, SCREEN_H - 2, SCREEN_W, 0x1188);
  canvas.drawFastHLine(0, SCREEN_H - 1, SCREEN_W, 0x0926);

  // Subtle dotted center court line (moody dark cobalt/cyan dashes)
  for (int16_t y = 3; y < SCREEN_H - 3; y += 6) {
    canvas.drawFastVLine(80, y, 3, 0x22AD);
  }
}

// ── Persistent Multi-Game Flash Storage (LittleFS) ────────────────────────
struct GameSaveData {
  uint32_t runnerHighScore;
  uint32_t pongWins;
};

static const char* SAVE_FILE = "/keychain_arcade.dat";
static GameSaveData g_saveData = { 0, 0 };
static bool g_newRecordThisRun = false;

static void loadGameSave() {
  if (!InternalFS.begin()) {
    InternalFS.format();
    InternalFS.begin();
  }
  File f(InternalFS);
  if (f.open(SAVE_FILE, FILE_O_READ)) {
    f.read((uint8_t*)&g_saveData, sizeof(g_saveData));
    f.close();
  } else {
    g_saveData.runnerHighScore = 0;
    g_saveData.pongWins        = 0;
  }
}

static void saveGameSave() {
  if (!InternalFS.begin()) return;
  InternalFS.remove(SAVE_FILE);
  File f(InternalFS);
  if (f.open(SAVE_FILE, FILE_O_WRITE)) {
    f.write((uint8_t*)&g_saveData, sizeof(g_saveData));
    f.close();
  }
}

// ── Battery Monitoring (Non-blocking) ─────────────────────────────────────
static int  g_batPercent    = 100;
static bool g_isCharging    = false;
static bool g_usbConnected  = false;
static uint32_t g_lastBatCheckMs = 0;

static void updateBatteryStatus() {
  NRF_P0->OUTCLR = (1UL << READ_BAT_P0_PIN);
  NRF_P0->DIRSET = (1UL << READ_BAT_P0_PIN);
  delayMicroseconds(200);

  uint32_t sum = 0;
  for (int i = 0; i < 4; ++i) {
    sum += analogRead(PIN_VBAT);
    delayMicroseconds(50);
  }
  NRF_P0->DIRCLR = (1UL << READ_BAT_P0_PIN);

  float raw = (float)sum / 4.0f;
  float vadc = (raw * 3.6f) / 4095.0f;
  float vbat = vadc * ((1000.0f + 499.0f) / 499.0f);

  g_isCharging   = ((NRF_P0->IN & (1UL << CHG_P0_PIN)) == 0);
  g_usbConnected = ((NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0);

  int pct = (int)((vbat - 3.20f) * 100.0f / (4.15f - 3.20f));
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g_batPercent = pct;
}

static void renderBatteryIcon(int16_t x, int16_t y) {
  canvas.drawRect(x, y, 12, 7, 0x5AEB);
  canvas.drawFastVLine(x + 12, y + 2, 3, 0x5AEB);
  if (g_isCharging || g_usbConnected) {
    canvas.drawPixel(x + 4, y + 2, C_GOLD);
    canvas.drawPixel(x + 5, y + 3, C_GOLD);
    canvas.drawPixel(x + 6, y + 3, C_GOLD);
    canvas.drawPixel(x + 7, y + 4, C_GOLD);
  } else {
    int bars = (g_batPercent * 4) / 100;
    if (bars == 0 && g_batPercent > 5) bars = 1;
    uint16_t bCol = (g_batPercent <= 20) ? 0xF800 : ((g_batPercent <= 45) ? 0xFFE0 : 0x07E0);
    for (int b = 0; b < bars && b < 4; ++b) {
      canvas.drawFastVLine(x + 2 + b * 2, y + 2, 3, bCol);
    }
  }
}

// ── Sprites ───────────────────────────────────────────────────────────────
#define _ C_TRANS
#define W C_WHITE
#define S 0xCE7F  // Soft lavender shadow
#define B C_BLACK
#define P 0xFCB2  // Cute pink cheeks
#define O 0xFD20  // Vibrant pumpkin orange
#define D 0x9340  // Shadow orange
#define Y 0xFFE0  // Glowing yellow carved face
#define G 0x2482  // Green stem
#define L 0x9CD3  // Stone highlight
#define N 0x6B6D  // Stone body
#define X 0x39E7  // Dark shadow stone
#define E 0xF800  // Spooky glowing red inscription dot
#define BD 0x18C3 // Dark bat wing
#define BP 0x39E7 // Bat purple body
#define BR 0xF800 // Glowing red eye

static const uint16_t SPRITE_GHOST_RUN0[24 * 25] = {
  _,_,_,_,_,_,_,_,_,_,_,B,B,B,B,_,_,_,_,_,_,_,_,_,
  _,_,_,_,_,_,_,_,_,B,B,W,W,W,W,B,B,_,_,_,_,_,_,_,
  _,_,_,_,_,_,_,_,B,W,W,W,W,W,W,W,W,B,_,_,_,_,_,_,
  _,_,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,_,
  _,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,
  _,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,W,B,W,W,W,W,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,B,W,W,W,B,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,B,W,W,W,B,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,B,W,P,P,W,W,W,W,W,P,P,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,B,W,W,W,B,W,B,W,W,W,B,B,_,_,
  _,_,_,_,_,B,W,W,W,W,W,B,W,W,W,B,W,W,W,W,B,W,B,_,
  _,_,_,_,_,B,W,B,W,W,W,B,W,W,W,W,W,W,W,W,B,W,W,B,
  _,_,_,_,B,W,W,S,B,B,B,S,W,W,W,W,W,W,W,W,B,S,W,B,
  _,_,_,_,B,W,W,W,S,S,S,W,W,W,W,W,W,W,W,W,B,S,S,B,
  _,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,B,B,_,
  _,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,_,_,_,
  _,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,_,_,_,_,
  B,S,W,W,W,W,W,W,W,W,W,W,W,W,S,W,W,W,S,B,_,_,_,_,
  B,S,W,W,W,W,W,W,S,W,W,W,W,W,S,W,W,W,S,B,_,_,_,_,
  _,B,S,S,W,W,W,S,S,W,W,W,W,S,S,W,W,W,S,B,_,_,_,_,
  _,_,B,B,S,S,S,S,B,S,W,W,S,S,B,S,W,S,S,B,_,_,_,_,
  _,_,_,_,B,B,B,B,_,B,S,S,S,B,_,B,S,S,B,_,_,_,_,_,
  _,_,_,_,_,_,_,_,_,_,B,B,B,_,_,_,B,B,_,_,_,_,_,_,
};

// Cute Ghost: Frame 1 (Running Right) - 24x25
static const uint16_t SPRITE_GHOST_RUN1[24 * 25] = {
  _,_,_,_,_,_,_,_,_,_,_,B,B,B,B,_,_,_,_,_,_,_,_,_,
  _,_,_,_,_,_,_,_,_,B,B,W,W,W,W,B,B,_,_,_,_,_,_,_,
  _,_,_,_,_,_,_,_,B,W,W,W,W,W,W,W,W,B,_,_,_,_,_,_,
  _,_,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,_,
  _,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,
  _,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,W,B,W,W,W,W,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,B,W,W,W,B,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,B,W,W,W,B,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,B,W,P,P,W,W,W,W,W,P,P,B,B,B,_,
  _,_,_,_,_,B,W,W,W,W,B,W,W,W,B,W,B,W,W,W,W,B,W,B,
  _,_,_,_,_,B,W,W,W,W,W,B,W,W,W,B,W,W,W,W,B,S,W,B,
  _,_,_,_,_,B,W,B,W,W,W,B,W,W,W,W,W,W,W,W,B,S,B,_,
  _,_,_,_,B,W,W,S,B,B,B,S,W,W,W,W,W,W,W,W,B,S,W,B,
  _,_,_,_,B,W,W,W,S,S,S,W,W,W,W,W,W,W,W,W,B,S,S,B,
  _,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,B,B,_,
  _,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,_,_,_,
  _,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,_,_,_,_,
  _,B,S,W,W,W,W,W,W,W,W,W,W,W,S,W,W,W,S,B,_,_,_,_,
  _,_,B,S,S,W,W,W,W,W,S,W,W,W,W,S,S,W,W,W,S,B,_,_,
  _,_,_,B,B,S,S,W,W,W,S,S,W,W,S,S,S,W,W,W,S,B,_,_,
  _,_,_,_,_,B,B,S,S,S,S,B,S,W,W,S,S,S,W,S,S,B,_,_,
  _,_,_,_,_,_,_,B,B,B,B,_,B,S,S,S,B,_,_,B,S,S,B,_,
  _,_,_,_,_,_,_,_,_,_,_,_,_,B,B,B,_,_,_,_,B,B,_,_,
};

// Cute Ghost: Jump Pose (Hands Up) - 24x25
static const uint16_t SPRITE_GHOST_JUMP[24 * 25] = {
  _,_,_,_,_,_,_,_,_,_,_,B,B,B,B,_,_,_,_,_,_,_,_,_,
  _,_,_,_,_,_,_,_,_,B,B,W,W,W,W,B,B,_,_,_,_,_,_,_,
  _,_,_,_,_,_,_,_,B,W,W,W,W,W,W,W,W,B,_,_,_,_,_,_,
  _,_,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,_,
  _,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,
  _,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,W,B,W,W,W,W,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,B,W,W,W,B,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,B,W,W,W,B,B,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,
  _,_,_,B,B,B,W,W,W,B,W,P,P,W,W,W,W,W,P,P,B,B,B,_,
  _,_,B,W,W,B,W,W,W,W,W,B,W,W,W,B,W,W,W,W,B,W,W,B,
  _,_,_,_,_,B,W,W,W,W,W,B,W,B,B,B,W,B,B,B,W,B,W,B,
  _,_,_,_,_,B,W,B,W,W,W,B,W,B,B,B,W,B,B,B,W,W,W,B,
  _,_,_,_,B,W,W,S,B,B,B,S,W,W,B,B,B,W,W,B,S,S,W,B,
  _,_,_,_,B,W,W,W,S,S,S,W,W,W,W,W,W,W,W,W,B,S,S,B,
  _,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,B,B,_,
  _,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,_,_,_,
  _,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,_,_,_,_,
  B,S,W,W,W,W,W,W,W,W,W,W,W,W,S,W,W,W,S,B,_,_,_,_,
  B,S,W,W,W,W,W,W,S,W,W,W,W,W,S,W,W,W,S,B,_,_,_,_,
  _,B,S,S,W,W,W,S,S,W,W,W,W,S,S,W,W,W,S,B,_,_,_,_,
  _,_,B,B,S,S,S,S,B,S,W,W,S,S,B,S,W,S,S,B,_,_,_,_,
  _,_,_,_,B,B,B,B,_,B,S,S,S,B,_,B,S,S,B,_,_,_,_,_,
  _,_,_,_,_,_,_,_,_,_,B,B,B,_,_,_,B,B,_,_,_,_,_,_,
};

// Cute Ghost: Dead Pose (X_X Eyes) - 24x25
static const uint16_t SPRITE_GHOST_DEAD[24 * 25] = {
  _,_,_,_,_,_,_,_,_,_,_,B,B,B,B,_,_,_,_,_,_,_,_,_,
  _,_,_,_,_,_,_,_,_,B,B,W,W,W,W,B,B,_,_,_,_,_,_,_,
  _,_,_,_,_,_,_,_,B,W,W,W,W,W,W,W,W,B,_,_,_,_,_,_,
  _,_,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,_,
  _,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,
  _,_,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,_,B,W,W,W,B,_,B,W,B,_,
  _,_,_,_,_,B,W,W,W,W,W,W,_,B,_,W,W,W,_,B,_,W,B,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,_,B,W,W,W,B,_,B,W,B,_,
  _,_,_,_,_,B,W,W,W,B,W,P,P,W,W,W,W,W,P,P,B,_,_,_,
  _,_,_,_,_,B,W,W,W,W,B,W,W,W,B,W,B,W,W,W,B,B,_,_,
  _,_,_,_,_,B,W,W,W,W,W,W,B,B,B,B,B,B,W,W,W,B,W,B,
  _,_,_,_,_,B,W,B,W,W,W,W,_,_,_,_,_,_,W,W,W,W,W,B,
  _,_,_,_,B,W,W,S,B,B,B,S,W,W,W,W,W,W,W,W,B,S,W,B,
  _,_,_,_,B,W,W,W,S,S,S,W,W,W,W,W,W,W,W,W,B,S,S,B,
  _,_,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,B,B,_,
  _,_,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,_,_,_,
  _,B,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,S,B,_,_,_,_,
  B,S,W,W,W,W,W,W,W,W,W,W,W,W,S,W,W,W,S,B,_,_,_,_,
  B,S,W,W,W,W,W,W,S,W,W,W,W,W,S,W,W,W,S,B,_,_,_,_,
  _,B,S,S,W,W,W,S,S,W,W,W,W,S,S,W,W,W,S,B,_,_,_,_,
  _,_,B,B,S,S,S,S,B,S,W,W,S,S,B,S,W,S,S,B,_,_,_,_,
  _,_,_,_,B,B,B,B,_,B,S,S,S,B,_,B,S,S,B,_,_,_,_,_,
  _,_,_,_,_,_,_,_,_,_,B,B,B,_,_,_,B,B,_,_,_,_,_,_,
};

// Obstacle: Pumpkin (14x14)
#define O 0xFD20 // Vibrant pumpkin orange
#define D 0x9340 // Shadow orange
#define Y 0xFFE0 // Glowing yellow carved face
#define G 0x2482 // Green stem

static const uint16_t SPRITE_PUMPKIN[196] = {
  _,_,_,_,_,_,G,G,_,_,_,_,_,_,
  _,_,_,_,_,_,G,_,_,_,_,_,_,_,
  _,_,_,O,O,O,O,O,O,O,O,_,_,_,
  _,_,O,O,O,O,O,O,O,O,O,O,_,_,
  _,O,O,O,O,O,O,O,O,O,O,O,O,_,
  _,O,O,Y,Y,O,O,O,O,Y,Y,O,O,_,
  O,O,O,Y,B,O,O,O,O,Y,B,O,O,O,
  O,O,O,O,O,O,Y,Y,O,O,O,O,O,O,
  O,O,O,Y,O,Y,Y,Y,Y,O,Y,O,O,O,
  O,O,O,Y,Y,Y,Y,Y,Y,Y,Y,O,O,O,
  _,O,O,O,Y,_,Y,_,Y,_,O,O,O,_,
  _,O,D,D,O,O,O,O,O,O,D,D,O,_,
  _,_,D,D,D,D,D,D,D,D,D,D,_,_,
  _,_,_,D,D,D,D,D,D,D,D,_,_,_
};

// Obstacle: Tombstone (12x15)
#define L 0x9CD3 // Stone highlight
#define N 0x6B6D // Stone body
#define X 0x39E7 // Dark shadow stone
#define E 0xF800 // Spooky glowing red inscription dot

static const uint16_t SPRITE_TOMBSTONE[180] = {
  _,_,_,_,L,L,L,L,_,_,_,_,
  _,_,L,L,N,N,N,N,L,L,_,_,
  _,L,N,N,N,N,N,N,N,N,L,_,
  L,N,N,N,N,N,N,N,N,N,N,X,
  L,N,N,B,B,N,N,B,B,N,N,X,
  L,N,N,N,B,N,N,B,N,N,N,X,
  L,N,B,B,B,B,B,B,B,B,N,X,
  L,N,N,N,B,N,N,B,N,N,N,X,
  L,N,N,N,B,N,N,B,N,N,N,X,
  L,N,N,N,N,E,E,N,N,N,N,X,
  L,N,N,N,N,N,N,N,N,N,N,X,
  L,N,N,N,N,N,N,N,N,N,N,X,
  L,N,N,N,N,N,N,N,N,N,N,X,
  X,X,X,X,X,X,X,X,X,X,X,X,
  X,X,X,X,X,X,X,X,X,X,X,X
};

// Obstacle: Flying Bat Frame 0 (16x9)
#define BD 0x18C3 // Dark bat wing
#define BP 0x39E7 // Bat purple body
#define BR 0xF800 // Glowing red eye

static const uint16_t SPRITE_BAT0[144] = {
  BD,_,_,_,_,_,_,_,_,_,_,_,_,_,_,BD,
  BD,BD,_,_,_,_,_,_,_,_,_,_,_,_,BD,BD,
  BD,BD,BD,_,_,_,BP,BP,BP,BP,_,_,_,BD,BD,BD,
  _,BD,BD,BD,_,BP,BR,BP,BP,BR,BP,_,BD,BD,BD,_,
  _,_,BD,BD,BP,BP,BP,BP,BP,BP,BP,BP,BD,BD,_,_,
  _,_,_,BD,BP,BP,B,BP,BP,B,BP,BP,BD,_,_,_,
  _,_,_,_,BP,BP,BP,BP,BP,BP,BP,BP,_,_,_,_,
  _,_,_,_,_,BP,_,_,_,_,BP,_,_,_,_,_,
  _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_
};

// Obstacle: Flying Bat Frame 1 (16x9)
static const uint16_t SPRITE_BAT1[144] = {
  _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
  _,_,_,_,_,BP,BP,BP,BP,_,_,_,_,_,_,_,
  _,_,_,_,BP,BR,BP,BP,BR,BP,_,_,_,_,_,_,
  BD,_,_,BP,BP,BP,BP,BP,BP,BP,BP,_,_,BD,_,_,
  BD,BD,_,BP,BP,BP,BP,BP,BP,BP,BP,_,BD,BD,_,_,
  BD,BD,BD,BD,_,BP,BP,BP,BP,_,BD,BD,BD,BD,_,_,
  _,BD,BD,BD,_,_,BP,BP,_,_,BD,BD,BD,_,_,_,
  _,_,BD,_,_,_,_,_,_,_,_,_,BD,_,_,_,
  _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_
};

// ── Halloween Carved Jack-o'-Lantern Pong Ball (8x8 Animated) ─────────────
// Frame 0: Upright carved pumpkin ball
static const uint16_t SPRITE_PUMPKIN_BALL0[64] = {
  _,_,_,G,G,_,_,_,
  _,_,O,O,O,O,_,_,
  _,O,Y,O,O,Y,O,_,
  O,O,O,Y,Y,O,O,O,
  O,O,Y,O,O,Y,O,O,
  _,O,O,Y,Y,O,O,_,
  _,_,D,D,D,D,_,_,
  _,_,_,_,_,_,_,_
};

// Frame 1: Rolling / tilted carved pumpkin ball
static const uint16_t SPRITE_PUMPKIN_BALL1[64] = {
  _,_,_,_,G,G,_,_,
  _,_,O,O,O,O,_,_,
  _,O,O,Y,O,O,Y,_,
  O,O,O,O,Y,Y,O,O,
  O,O,Y,Y,O,O,Y,O,
  _,O,O,O,Y,Y,O,_,
  _,_,D,D,D,D,_,_,
  _,_,_,_,_,_,_,_
};

// ── Small Bone Paddles for Pong (6x18 pixels) ─────────────────────────────
#define BI    0xEF7E // Bone Ivory
#define BS    0x9495 // Lavender Bone Shadow
#define BO    0xFD20 // Haunted Pumpkin Orange
#define BG    0xFEA0 // Haunted Gold Highlight
#define BD_DK 0x6200 // Haunted Dark Brown Shadow

// User Bone Paddle (6x18) - Pure Ivory/White with lavender shadow
static const uint16_t SPRITE_BONE_USER[108] = {
  _, W, _, _, W, _,
  W, BI, W, W, BI, BS,
  W, BI, BI, BI, BI, BS,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  _, W, BI, BI, BS, _,
  W, BI, BI, BI, BI, BS,
  W, BI, W, W, BI, BS,
  _, W, _, _, BS, _
};

// CPU Bone Paddle (6x18) - Cursed Haunted Bone (Glowing Amber / Pumpkin Orange)
static const uint16_t SPRITE_BONE_CPU[108] = {
  _, BO, _, _, BO, _,
  BG, BO, BG, BG, BO, BD_DK,
  BG, BO, BO, BO, BO, BD_DK,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  _, BG, BO, BO, BD_DK, _,
  BG, BO, BO, BO, BO, BD_DK,
  BG, BO, BG, BG, BO, BD_DK,
  _, BO, _, _, BD_DK, _
};

#undef _
#undef W
#undef S
#undef B
#undef P
#undef O
#undef D
#undef Y
#undef G
#undef L
#undef N
#undef X
#undef E
#undef BD
#undef BP
#undef BR
#undef BI
#undef BS
#undef BO
#undef BG
#undef BD_DK

// =============================================================================
// ── GAME 1: GHOST RUNNER (GOST RUN) ──────────────────────────────────────────
// =============================================================================
static constexpr int MAX_OBSTACLES = 3;
static Obstacle g_obstacles[MAX_OBSTACLES];
static float    g_runnerY         = GHOST_GROUND_Y;
static float    g_runnerVy        = 0.0f;
static bool     g_runnerGrounded  = true;
static float    g_runnerSpeed     = CONFIG_RUNNER_START_SPEED;
static uint32_t g_runnerScore     = 0;
static uint32_t g_runnerFrame     = 0;
static uint8_t  g_runnerAnimFrame = 0;

static void initRunnerGame() {
  g_runnerY         = GHOST_GROUND_Y;
  g_runnerVy        = 0.0f;
  g_runnerGrounded  = true;
  g_runnerSpeed     = CONFIG_RUNNER_START_SPEED;
  g_runnerScore     = 0;
  g_runnerFrame     = 0;
  g_newRecordThisRun = false;
  for (int i = 0; i < MAX_OBSTACLES; ++i) g_obstacles[i].active = false;
}

static void spawnRunnerObstacle() {
  int slot = -1;
  for (int i = 0; i < MAX_OBSTACLES; ++i) {
    if (!g_obstacles[i].active) { slot = i; break; }
  }
  if (slot == -1) return;

  float rightmostX = -100.0f;
  for (int i = 0; i < MAX_OBSTACLES; ++i) {
    if (g_obstacles[i].active && g_obstacles[i].x > rightmostX) {
      rightmostX = g_obstacles[i].x;
    }
  }
  if (rightmostX > (SCREEN_W - 75.0f)) return;

  Obstacle &obs = g_obstacles[slot];
  obs.active = true;
  obs.x = SCREEN_W + 2;
  obs.anim = 0;

  uint8_t r = random(0, 100);
  if (r < 45) {
    obs.type = 0; obs.width = 14; obs.height = 14; obs.y = GROUND_Y - 14; // 50
  } else if (r < 75) {
    obs.type = 1; obs.width = 12; obs.height = 15; obs.y = GROUND_Y - 15; // 49
  } else {
    obs.type = 2; obs.width = 16; obs.height = 9;  obs.y = 26 + random(-3, 3);
  }
}

static bool checkRunnerCollision(float gx, float gy, const Obstacle &obs) {
  float gLeft   = gx + 5;
  float gRight  = gx + 20;
  float gTop    = gy + 4;
  float gBottom = gy + 24;

  float oLeft   = obs.x + 2;
  float oRight  = obs.x + obs.width - 2;
  float oTop    = obs.y + 2;
  float oBottom = obs.y + obs.height - 1;

  return (gLeft < oRight && gRight > oLeft && gTop < oBottom && gBottom > oTop);
}

static void updateAndRenderRunner(bool jumpEdge) {
  g_runnerFrame++;

  // Jump impulse
  if (jumpEdge && g_runnerGrounded) {
    g_runnerVy = CONFIG_GHOST_JUMP_IMPULSE;
    g_runnerGrounded = false;
  }

  // Physics
  g_runnerVy += CONFIG_GHOST_GRAVITY;
  g_runnerY  += g_runnerVy;
  if (g_runnerY >= GHOST_GROUND_Y) {
    g_runnerY = GHOST_GROUND_Y;
    g_runnerVy = 0.0f;
    g_runnerGrounded = true;
  }

  // Obstacles update
  for (int i = 0; i < MAX_OBSTACLES; ++i) {
    if (!g_obstacles[i].active) continue;
    g_obstacles[i].x -= g_runnerSpeed;
    if (g_runnerFrame % 5 == 0) g_obstacles[i].anim ^= 1;
    if (g_obstacles[i].x + g_obstacles[i].width < 0) {
      g_obstacles[i].active = false;
      g_runnerScore += 15; // Bonus
    }
  }

  spawnRunnerObstacle();
  if (g_runnerSpeed < CONFIG_RUNNER_MAX_SPEED) g_runnerSpeed += CONFIG_RUNNER_ACCEL;
  if (g_runnerFrame % 3 == 0) g_runnerScore++;
  if (g_runnerFrame % 6 == 0) g_runnerAnimFrame ^= 1;

  // Collision check
  for (int i = 0; i < MAX_OBSTACLES; ++i) {
    if (!g_obstacles[i].active) continue;
    if (checkRunnerCollision(GHOST_X, g_runnerY, g_obstacles[i])) {
      if (g_runnerScore > g_saveData.runnerHighScore) {
        g_saveData.runnerHighScore = g_runnerScore;
        saveGameSave();
        g_newRecordThisRun = true;
      }
      g_systemMode = MODE_GAMEOVER;
      return;
    }
  }

  // ── Render Full-Screen Sunset Sky Scene (Zero Black Bars!) ──
  renderBaseSunsetSky(true);

  // Obstacles
  for (int i = 0; i < MAX_OBSTACLES; ++i) {
    if (!g_obstacles[i].active) continue;
    int16_t ox = (int16_t)g_obstacles[i].x;
    int16_t oy = g_obstacles[i].y;
    if (g_obstacles[i].type == 0) {
      canvas.pushImage(ox, oy, 14, 14, SPRITE_PUMPKIN, C_TRANS);
    } else if (g_obstacles[i].type == 1) {
      canvas.pushImage(ox, oy, 12, 15, SPRITE_TOMBSTONE, C_TRANS);
    } else {
      const uint16_t* batFrame = (g_obstacles[i].anim == 0) ? SPRITE_BAT0 : SPRITE_BAT1;
      canvas.pushImage(ox, oy, 16, 9, batFrame, C_TRANS);
    }
  }

  // Cute Ghost Player
  const uint16_t* ghostSprite = (!g_runnerGrounded) ? SPRITE_GHOST_JUMP
                              : ((g_runnerAnimFrame == 0) ? SPRITE_GHOST_RUN0 : SPRITE_GHOST_RUN1);
  canvas.pushImage((int16_t)GHOST_X, (int16_t)g_runnerY, GHOST_W, GHOST_H, ghostSprite, C_TRANS);

  // Floating Bottom HUD with drop shadow (Sunset gradient flows across entire screen!)
  canvas.setTextSize(1);
  char hsBuf[6]; snprintf(hsBuf, sizeof(hsBuf), "%04lu", (unsigned long)g_saveData.runnerHighScore);
  char scBuf[6]; snprintf(scBuf, sizeof(scBuf), "%04lu", (unsigned long)g_runnerScore);

  // HI score with shadow on bottom-left
  canvas.setTextColor(C_BLACK);
  canvas.drawString("HI", 5, 71);
  canvas.drawString(hsBuf, 19, 71);
  canvas.setTextColor(C_GOLD);
  canvas.drawString("HI", 4, 70);
  canvas.setTextColor(C_WHITE);
  canvas.drawString(hsBuf, 18, 70);

  renderBatteryIcon(74, 70);

  // SC score with shadow on bottom-right
  canvas.setTextColor(C_BLACK);
  canvas.drawString("SC", 111, 71);
  canvas.drawString(scBuf, 127, 71);
  canvas.setTextColor(C_CYAN);
  canvas.drawString("SC", 110, 70);
  canvas.setTextColor(C_WHITE);
  canvas.drawString(scBuf, 126, 70);

  flushCanvasFast();
}

// =============================================================================
// ── GAME 2: GHOST PONG (FULL-SCREEN DARK MOODY GRAVEYARD BONE PONG) ──────────
// =============================================================================
// USER IS ON THE RIGHT (X = 148, Bone White Paddle)
// CPU IS ON THE LEFT (X = 6, Haunted Orange Bone Paddle)
// CONTROLS SWITCHED UPSIDE-DOWN:
//   D7 = MOVE UP
//   D6 = MOVE DOWN
static constexpr int16_t PONG_PADDLE_H = 18;
static constexpr int16_t PONG_PADDLE_W = 6;
static constexpr int16_t PONG_TOP_Y    = 2;
static constexpr int16_t PONG_BOT_Y    = 78;
static constexpr int16_t PONG_CPU_X    = 6;   // CPU on Left (X: 6..11)
static constexpr int16_t PONG_USER_X   = 148; // User on Right (X: 148..153)

static float    g_pongUserY       = 30.0f; // User Bone Paddle on Right
static float    g_pongCpuY        = 30.0f; // CPU Bone Paddle on Left
static float    g_pongBallX       = 80.0f;
static float    g_pongBallY       = 38.0f;
static float    g_pongBallVx      = 2.2f;
static float    g_pongBallVy      = 1.2f;
static int      g_pongPlayerScore = 0;
static int      g_pongCpuScore    = 0;
static bool     g_pongInPlay      = false;
static uint32_t g_pongServeDelayMs = 0;
static bool     g_pongPlayerWon   = false;
static uint32_t g_pongBallFrame   = 0;

static void servePongBall(bool towardsUser) {
  g_pongBallX = 80.0f;
  g_pongBallY = 38.0f + random(-12, 12);
  g_pongBallVx = towardsUser ? 2.3f : -2.3f;
  g_pongBallVy = (random(0, 2) == 0 ? 1.3f : -1.3f);
  g_pongInPlay = false;
  g_pongServeDelayMs = millis();
}

static void initPongGame() {
  g_pongUserY       = 30.0f;
  g_pongCpuY        = 30.0f;
  g_pongPlayerScore = 0;
  g_pongCpuScore    = 0;
  g_pongPlayerWon   = false;
  g_pongBallFrame   = 0;
  servePongBall(random(0, 2) == 0);
}

static void updateAndRenderPong(bool btnUp, bool btnDown) {
  g_pongBallFrame++;

  // USER PADDLE ON RIGHT (D7 = UP, D6 = DOWN)
  if (btnUp)   g_pongUserY -= CONFIG_PONG_PADDLE_SPEED;
  if (btnDown) g_pongUserY += CONFIG_PONG_PADDLE_SPEED;
  if (g_pongUserY < PONG_TOP_Y) g_pongUserY = PONG_TOP_Y;
  if (g_pongUserY > PONG_BOT_Y - PONG_PADDLE_H) g_pongUserY = PONG_BOT_Y - PONG_PADDLE_H;

  // Serve countdown
  if (!g_pongInPlay) {
    if (millis() - g_pongServeDelayMs > 600) g_pongInPlay = true;
  } else {
    // Ball movement
    g_pongBallX += g_pongBallVx;
    g_pongBallY += g_pongBallVy;

    // Top & Bottom wall bounce (Full 80px playfield)
    if (g_pongBallY <= PONG_TOP_Y + 1) {
      g_pongBallY = PONG_TOP_Y + 1;
      g_pongBallVy = fabs(g_pongBallVy);
    }
    if (g_pongBallY >= PONG_BOT_Y - 8) {
      g_pongBallY = PONG_BOT_Y - 8;
      g_pongBallVy = -fabs(g_pongBallVy);
    }

    // CPU AI (On Left at X = 6)
    if (g_pongBallVx < 0) { // Ball moving towards CPU
      if (g_pongCpuY + (PONG_PADDLE_H / 2.0f) < g_pongBallY + 2) g_pongCpuY += 2.0f;
      else if (g_pongCpuY + (PONG_PADDLE_H / 2.0f) > g_pongBallY + 6) g_pongCpuY -= 2.0f;
    } else { // Return smoothly towards center
      if (g_pongCpuY < 30.0f) g_pongCpuY += 0.8f;
      else if (g_pongCpuY > 30.0f) g_pongCpuY -= 0.8f;
    }
    if (g_pongCpuY < PONG_TOP_Y) g_pongCpuY = PONG_TOP_Y;
    if (g_pongCpuY > PONG_BOT_Y - PONG_PADDLE_H) g_pongCpuY = PONG_BOT_Y - PONG_PADDLE_H;

    // Ball Collision with CPU Bone Paddle on Left (X = 6, Width = 6)
    if (g_pongBallX <= PONG_CPU_X + PONG_PADDLE_W && g_pongBallX >= PONG_CPU_X - 4) {
      if (g_pongBallY + 7 >= g_pongCpuY && g_pongBallY <= g_pongCpuY + PONG_PADDLE_H) {
        g_pongBallX = PONG_CPU_X + PONG_PADDLE_W + 1;
        float hitNorm = ((g_pongBallY + 4) - (g_pongCpuY + PONG_PADDLE_H / 2.0f)) / (PONG_PADDLE_H / 2.0f);
        g_pongBallVx = fabs(g_pongBallVx) * 1.05f;
        if (g_pongBallVx > 4.8f) g_pongBallVx = 4.8f;
        g_pongBallVy = hitNorm * 2.5f;
      }
    }

    // Ball Collision with User Bone Paddle on Right (X = 148, Width = 6)
    if (g_pongBallX + 7 >= PONG_USER_X && g_pongBallX <= PONG_USER_X + PONG_PADDLE_W + 2) {
      if (g_pongBallY + 7 >= g_pongUserY && g_pongBallY <= g_pongUserY + PONG_PADDLE_H) {
        g_pongBallX = PONG_USER_X - 8;
        float hitNorm = ((g_pongBallY + 4) - (g_pongUserY + PONG_PADDLE_H / 2.0f)) / (PONG_PADDLE_H / 2.0f);
        g_pongBallVx = -fabs(g_pongBallVx) * 1.05f;
        if (g_pongBallVx < -4.8f) g_pongBallVx = -4.8f;
        g_pongBallVy = hitNorm * 2.5f;
      }
    }

    // Scoring (First to 5 points wins)
    if (g_pongBallX < 0) {
      // Past CPU -> User Scores!
      g_pongPlayerScore++;
      if (g_pongPlayerScore >= 5) {
        g_pongPlayerWon = true;
        g_saveData.pongWins++;
        saveGameSave();
        g_systemMode = MODE_GAMEOVER;
        return;
      }
      servePongBall(false);
    } else if (g_pongBallX > SCREEN_W) {
      // Past User -> CPU Scores!
      g_pongCpuScore++;
      if (g_pongCpuScore >= 5) {
        g_pongPlayerWon = false;
        g_systemMode = MODE_GAMEOVER;
        return;
      }
      servePongBall(true);
    }
  }

  // ── Render Full-Screen Dark Moody Graveyard Pong Scene ──
  renderPongBackground();

  // Left Bone Paddle: Haunted CPU (Cursed Orange Bone)
  canvas.pushImage(PONG_CPU_X, (int16_t)g_pongCpuY, PONG_PADDLE_W, PONG_PADDLE_H, SPRITE_BONE_CPU, C_TRANS);

  // Right Bone Paddle: User (Pure Bone White / Ivory)
  canvas.pushImage(PONG_USER_X, (int16_t)g_pongUserY, PONG_PADDLE_W, PONG_PADDLE_H, SPRITE_BONE_USER, C_TRANS);

  // Ball: Carved Jack-o'-Lantern Pumpkin (8x8 Animated Spin)
  const uint16_t* pumpkinBallSprite = ((g_pongBallFrame / 5) % 2 == 0) ? SPRITE_PUMPKIN_BALL0 : SPRITE_PUMPKIN_BALL1;
  canvas.pushImage((int16_t)g_pongBallX, (int16_t)g_pongBallY, 8, 8, pumpkinBallSprite, C_TRANS);

  // Score HUD with drop shadows for maximum contrast over dark night sky
  canvas.setTextSize(1);
  canvas.setTextColor(C_BLACK);
  canvas.drawString("CPU", 17, 5);
  canvas.drawNumber(g_pongCpuScore, 41, 5);
  canvas.setTextColor(C_ORANGE);
  canvas.drawString("CPU", 16, 4);
  canvas.setTextColor(C_WHITE);
  canvas.drawNumber(g_pongCpuScore, 40, 4);

  canvas.setTextColor(C_BLACK);
  canvas.drawString("YOU", 119, 5);
  canvas.drawNumber(g_pongPlayerScore, 143, 5);
  canvas.setTextColor(C_CYAN);
  canvas.drawString("YOU", 118, 4);
  canvas.setTextColor(C_WHITE);
  canvas.drawNumber(g_pongPlayerScore, 142, 4);

  // Battery icon
  renderBatteryIcon(74, 70);

  flushCanvasFast();
}

// =============================================================================
// ── SCREENS: BOOT, MENU, GAMEOVER ────────────────────────────────────────────
// =============================================================================

// Boot Showcase Screen - "KEYCHAIN ARCADE" Full-Screen Sunset Edition
static void renderBootScreen(uint32_t elapsedMs) {
  renderBaseSunsetSky(true);

  int16_t ghostBob = ((elapsedMs / 180) % 2 == 0) ? 0 : 2;
  canvas.pushImage(48, 34 + ghostBob, GHOST_W, GHOST_H, SPRITE_GHOST_RUN0, C_TRANS);

  const uint16_t* pumpkinBall = ((elapsedMs / 120) % 2 == 0) ? SPRITE_PUMPKIN_BALL0 : SPRITE_PUMPKIN_BALL1;
  canvas.pushImage(98, 42 - ghostBob, 8, 8, pumpkinBall, C_TRANS);

  // Name of the Device: KEYCHAIN ARCADE (Bold with drop shadow)
  canvas.setTextSize(2);
  canvas.setTextColor(C_BLACK);
  canvas.drawCentreString("KEYCHAIN", 81, 5, 1);
  canvas.setTextColor(C_ORANGE);
  canvas.drawCentreString("KEYCHAIN", 80, 4, 1);

  canvas.setTextColor(C_BLACK);
  canvas.drawCentreString("ARCADE", 81, 20, 1);
  canvas.setTextColor(C_GOLD);
  canvas.drawCentreString("ARCADE", 80, 19, 1);


  flushCanvasFast();
}

// Creative Full-Screen Game Selection Menu (Dynamically switches background per game!)
static void renderGameMenuScreen() {
  if (g_selectedGame == GAME_RUNNER) {
    // Runner: Showcase Full-Screen Sunset Sky (Zero black bars)
    renderBaseSunsetSky(true);

    // Header Title with shadow
    canvas.setTextSize(1);
    canvas.setTextColor(C_BLACK);
    canvas.drawCentreString("* KEYCHAIN ARCADE *", 81, 6, 1);
    canvas.setTextColor(C_GOLD);
    canvas.drawCentreString("* KEYCHAIN ARCADE *", 80, 5, 1);

    // Game Title with glow / shadow
    canvas.setTextColor(C_BLACK);
    canvas.drawCentreString("<  1. GOST RUN  >", 81, 18, 1);
    canvas.setTextColor(C_WHITE);
    canvas.drawCentreString("<  1. GOST RUN  >", 80, 17, 1);

    // Floating cute mascot scene
    int16_t ghostBob = ((millis() / 200) % 2 == 0) ? 0 : 2;
    canvas.pushImage(46, 30 + ghostBob, GHOST_W, GHOST_H, SPRITE_GHOST_RUN0, C_TRANS);
    canvas.pushImage(94, 38, 14, 14, SPRITE_PUMPKIN, C_TRANS);

    // Subtitle

  } else {
    // Pong: Showcase Dark Moody Graveyard
    renderPongBackground();

    // Header Title with shadow
    canvas.setTextSize(1);
    canvas.setTextColor(C_BLACK);
    canvas.drawCentreString("* KEYCHAIN ARCADE *", 81, 6, 1);
    canvas.setTextColor(C_GOLD);
    canvas.drawCentreString("* KEYCHAIN ARCADE *", 80, 5, 1);

    // Game Title with glow / shadow
    canvas.setTextColor(C_BLACK);
    canvas.drawCentreString("<  2. GHOST PONG  >", 81, 18, 1);
    canvas.setTextColor(C_CYAN);
    canvas.drawCentreString("<  2. GHOST PONG  >", 80, 17, 1);

    // Floating Pong showcase: CPU bone paddle, rolling pumpkin, User bone paddle
    int16_t paddleBob = ((millis() / 250) % 2 == 0) ? 0 : 3;
    canvas.pushImage(36, 30 + paddleBob, PONG_PADDLE_W, PONG_PADDLE_H, SPRITE_BONE_CPU, C_TRANS);

    const uint16_t* pumpkinBall = ((millis() / 150) % 2 == 0) ? SPRITE_PUMPKIN_BALL0 : SPRITE_PUMPKIN_BALL1;
    canvas.pushImage(76, 35 - paddleBob, 8, 8, pumpkinBall, C_TRANS);

    canvas.pushImage(118, 32 - paddleBob, PONG_PADDLE_W, PONG_PADDLE_H, SPRITE_BONE_USER, C_TRANS);

    // Subtitle

  }

  // Navigation hint floating at bottom


  renderBatteryIcon(144, 69);
  flushCanvasFast();
}

// Unified Game Over Screen (No clunky solid card blocking background!)
static void renderGameOverScreen() {
  if (g_selectedGame == GAME_RUNNER) {
    renderBaseSunsetSky(false);
  } else {
    renderPongBackground();
  }

  canvas.setTextSize(1);
  if (g_selectedGame == GAME_RUNNER) {
    canvas.setTextColor(C_BLACK);
    canvas.drawCentreString("GAME OVER", 81, 8, 1);
    canvas.setTextColor(C_RED);
    canvas.drawCentreString("GAME OVER", 80, 7, 1);

    // Dead ghost on left
    canvas.pushImage(22, 22, GHOST_W, GHOST_H, SPRITE_GHOST_DEAD, C_TRANS);

    // Score on right
    char scBuf[8]; snprintf(scBuf, sizeof(scBuf), "%lu", (unsigned long)g_runnerScore);
    canvas.setTextColor(C_BLACK);
    canvas.drawString("SC: ", 57, 23);
    canvas.drawString(scBuf, 81, 23);
    canvas.setTextColor(C_WHITE);
    canvas.drawString("SC: ", 56, 22);
    canvas.drawString(scBuf, 80, 22);

    char hsBuf[8]; snprintf(hsBuf, sizeof(hsBuf), "%lu", (unsigned long)g_saveData.runnerHighScore);
    canvas.setTextColor(C_BLACK);
    canvas.drawString("HI: ", 57, 35);
    canvas.drawString(hsBuf, 81, 35);
    canvas.setTextColor(C_GOLD);
    canvas.drawString("HI: ", 56, 34);
    canvas.drawString(hsBuf, 80, 34);

    if (g_newRecordThisRun) {
      canvas.setTextColor(C_BLACK);
      canvas.drawCentreString("* NEW RECORD! *", 81, 49, 1);
      canvas.setTextColor(C_GOLD);
      canvas.drawCentreString("* NEW RECORD! *", 80, 48, 1);
    }
  } else {
    // Pong Game Over
    if (g_pongPlayerWon) {
      canvas.setTextColor(C_BLACK);
      canvas.drawCentreString("YOU WIN!", 81, 8, 1);
      canvas.setTextColor(C_GOLD);
      canvas.drawCentreString("YOU WIN!", 80, 7, 1);
      canvas.pushImage(22, 20, GHOST_W, GHOST_H, SPRITE_GHOST_JUMP, C_TRANS);
    } else {
      canvas.setTextColor(C_BLACK);
      canvas.drawCentreString("CPU WINS!", 81, 8, 1);
      canvas.setTextColor(C_RED);
      canvas.drawCentreString("CPU WINS!", 80, 7, 1);
      canvas.pushImage(22, 20, GHOST_W, GHOST_H, SPRITE_GHOST_DEAD, C_TRANS);
    }

    char pBuf[24]; snprintf(pBuf, sizeof(pBuf), "CPU %d - %d YOU", g_pongCpuScore, g_pongPlayerScore);
    canvas.setTextColor(C_BLACK);
    canvas.drawCentreString(pBuf, 91, 25, 1);
    canvas.setTextColor(C_WHITE);
    canvas.drawCentreString(pBuf, 90, 24, 1);

    char wBuf[16]; snprintf(wBuf, sizeof(wBuf), "WINS: %lu", (unsigned long)g_saveData.pongWins);
    canvas.setTextColor(C_BLACK);
    canvas.drawCentreString(wBuf, 91, 37, 1);
    canvas.setTextColor(C_CYAN);
    canvas.drawCentreString(wBuf, 90, 36, 1);
  }

  // Bottom action hint with shadow


  renderBatteryIcon(144, 69);
  flushCanvasFast();
}

// ── Arduino Setup ──────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Button inputs with internal pullups (Active-Low)
  pinMode(PIN_BTN_JUMP, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);

  // Backlight Power ON
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, HIGH);

  // Battery ADC configuration
  nrf_gpio_cfg_input(CHG_P0_PIN, NRF_GPIO_PIN_PULLUP);
  analogReadResolution(12);
  NRF_P0->DIRCLR = (1UL << READ_BAT_P0_PIN);

  // Load persistent save data
  loadGameSave();
  updateBatteryStatus();

  // Initialize 0.96" IPS ST7789 display
  if (!display.begin<Board_XIAO_0inch96_LCD<LCD_RST_PIN, LCD_BL_PIN>,
                     Config_Seeed_0inch96_LCD_ST7789>()) {
    Serial.println("Display init failed!");
    return;
  }

  display.setRotation(SCREEN_ROTATION);
  display.fillScreen(TFT_BLACK);

  // Allocate 160x80 RGB565 double-buffer canvas (25.6 KB RAM)
  if (!canvas.createSprite(&display, SCREEN_W, SCREEN_H)) {
    Serial.println("Canvas allocation failed!");
  }

  // Start at Boot Screen
  g_bootStartMs = millis();
  g_systemMode  = MODE_BOOT;
}

// ── Main Game Loop (60 FPS Pacing) ─────────────────────────────────────────
void loop() {
  uint32_t frameStartMs = millis();

  // Read Buttons (Active-Low)
  static bool lastJumpDown  = false;
  static bool lastResetDown = false;

  bool jumpDown  = (digitalRead(PIN_BTN_JUMP) == LOW);
  bool resetDown = (digitalRead(PIN_BTN_RESET) == LOW);

  bool jumpEdge  = (jumpDown && !lastJumpDown);
  bool resetEdge = (resetDown && !lastResetDown);

  // Non-blocking battery update every 5 seconds
  if (millis() - g_lastBatCheckMs > 5000) {
    g_lastBatCheckMs = millis();
    updateBatteryStatus();
  }

  // State Machine
  switch (g_systemMode) {
    case MODE_BOOT: {
      uint32_t elapsed = millis() - g_bootStartMs;
      renderBootScreen(elapsed);
      if (jumpEdge || resetEdge || elapsed > 2400) {
        g_systemMode = MODE_MENU;
      }
      break;
    }

    case MODE_MENU: {
      // D7: Toggle between the 2 games
      if (resetEdge) {
        g_selectedGame = (GameID)((g_selectedGame + 1) % NUM_GAMES);
      }
      // D6: Play chosen game
      if (jumpEdge) {
        if (g_selectedGame == GAME_RUNNER) initRunnerGame();
        else if (g_selectedGame == GAME_PONG) initPongGame();
        g_systemMode = MODE_PLAYING;
      }
      renderGameMenuScreen();
      break;
    }

    case MODE_PLAYING: {
      // In Runner: Hold D7 for 1.2s to return to Menu
      // In Pong: Hold both D6 + D7 for 1.2s to return to Menu
      bool holdCondition = false;
      if (g_selectedGame == GAME_RUNNER) {
        holdCondition = resetDown;
      } else {
        holdCondition = (jumpDown && resetDown);
      }

      if (holdCondition) {
        if (g_menuHoldStartMs == 0) g_menuHoldStartMs = millis();
        if (millis() - g_menuHoldStartMs > 1200) {
          g_menuHoldStartMs = 0;
          g_systemMode = MODE_MENU;
          break;
        }
      } else {
        g_menuHoldStartMs = 0;
      }

      // Route to active game
      if (g_selectedGame == GAME_RUNNER) {
        updateAndRenderRunner(jumpEdge);
      } else if (g_selectedGame == GAME_PONG) {
        // PONG CONTROLS SWITCHED:
        // D7 = Move UP
        // D6 = Move DOWN
        updateAndRenderPong(resetDown, jumpDown);
      }
      break;
    }

    case MODE_GAMEOVER: {
      renderGameOverScreen();
      // D6: Retry current game
      if (jumpEdge) {
        if (g_selectedGame == GAME_RUNNER) initRunnerGame();
        else if (g_selectedGame == GAME_PONG) initPongGame();
        g_systemMode = MODE_PLAYING;
      }
      // D7: Return to Game Select Menu
      if (resetEdge) {
        g_systemMode = MODE_MENU;
      }
      break;
    }
  }

  lastJumpDown  = jumpDown;
  lastResetDown = resetDown;

  // 60 FPS pacing (16 ms target)
  uint32_t frameElapsed = millis() - frameStartMs;
  if (frameElapsed < 16) {
    delay(16 - frameElapsed);
  }
}
