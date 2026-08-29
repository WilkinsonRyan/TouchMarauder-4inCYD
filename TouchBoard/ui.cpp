//
// Screen rendering, hit testing, and the view state machine. Portrait 240x320.
//
// T9 is traditional buffered multi-tap (the report in the project docs):
// taps edit a LOCAL candidate buffer shown in the preview strip — nothing
// goes over BLE until the character COMMITS, via:
//   - the multi-tap timeout expiring,
//   - a different key starting a new sequence,
//   - the '>' advance key (for same-key sequences like "ab"),
//   - Enter / leaving the view.
// One HID character per commit: no backspace churn, no dropped notifications.
//
// Ghost-touch defense: keypad keys fire on RELEASE, and only if the contact
// lasted >= T9_MIN_TAP_MS. A one-frame phantom never types anything.
//
#include <Arduino.h>
#include <ctype.h>
#include <Preferences.h>     // persist theme + brightness in NVS
#include "ui.h"
#include "config.h"
#include "display.h"
#include "hidkb.h"
#include "keymap.h"
#include "touch.h"           // touch_read() for the screensaver wake
#include "battery.h"         // 18650 gauge for the status-strip indicator
#include "esp_ota_ops.h"     // dual-boot: hand off to the Marauder app slot
#include "esp_partition.h"

// ---------------- layout constants ----------------
static const int TAB_H   = 28;
static const int TAB_W   = SCREEN_W / VIEW_COUNT;   // 60
static const int STRIP_Y = TAB_H;                   // preview/status strip
static const int STRIP_H = 36;
static const int AREA_Y  = TAB_H + STRIP_H;         // 64
static const int AREA_H  = SCREEN_H - AREA_Y;       // 256
static const int KEY_GAP = 2;

// T9 grid: 3 cols x 5 rows (1-9, *0#, then bksp / advance / enter)
static const int T9_COL_W = SCREEN_W / 3;           // 80
static const int T9_ROW_H = AREA_H / 5;             // 51
static const int T9_BKSP    = 12;
static const int T9_ADVANCE = 13;
static const int T9_ENTER   = 14;

// ---------------- palette (RGB565) ----------------
// Mutable so the theme picker (BT > Settings > Toggle Theme) can re-skin the UI.
// applyTheme() loads one set; every draw function reads these live.
static uint16_t COL_BG, COL_KEY, COL_KEY_SPEC, COL_KEY_DOWN, COL_TEXT,
                COL_TAB, COL_TAB_ON, COL_OK, COL_ADV, COL_DIM;

// ---------------- theme + display prefs (NVS ns "tbdisp") ----------------
enum { TB_LIGHT = 0, TB_DARK = 1, TB_HACKER = 2, TB_PATRIOT = 3 };
static uint8_t theme         = TB_DARK;   // default: the original dark skin
static uint8_t brightnessPct = 80;        // 10..100

// Patriot palette (RGB565): flag red, white, navy/blue.
static const uint16_t USA_RED  = 0xB926;  // flag red (#B22234)
static const uint16_t USA_BLUE = 0x0011;  // flag navy (#3C3B6E, darkened)
static const uint16_t USA_KEYB = 0x1A3F;  // brighter blue for key fills (reads white text)

static inline bool patriotOn() { return theme == TB_PATRIOT; }

// Patriot key fill: horizontal red/blue "stripes" across the key area so the
// keyboard reads like a flag; white text + white outline stay legible on both.
static uint16_t patriotKeyColor(int cx, int cy, int x0, int y0, int W, int H) {
  int band = (H > 0) ? ((cy - y0) * 6 / H) : 0;   // 6 stripes top->bottom
  if (band < 0) band = 0; if (band > 5) band = 5;
  return (band & 1) ? USA_KEYB : USA_RED;
}

// Outline colour for clear key separation in Dark (white) / Hacker (neon-green)
// / Patriot (white). Light already separates via fill contrast, so it gets none.
static void keyOutlineRect(int x, int y, int w, int h, int r) {
  if      (theme == TB_DARK)    tft.drawRoundRect(x, y, w, h, r, 0xFFFF);
  else if (theme == TB_HACKER)  tft.drawRoundRect(x, y, w, h, r, 0x07E0);
  else if (theme == TB_PATRIOT) tft.drawRoundRect(x, y, w, h, r, 0xFFFF);
}

static const char* themeName(uint8_t t) {
  switch (t) { case TB_LIGHT: return "Light"; case TB_HACKER: return "Hacker";
               case TB_PATRIOT: return "USA"; default: return "Dark"; }
}

static void applyTheme() {
  switch (theme) {
    case TB_LIGHT:   // black text reads on every key state
      COL_BG=0xFFFF; COL_KEY=0xC618; COL_KEY_SPEC=0x9CD3; COL_KEY_DOWN=0x8E5F;
      COL_TEXT=0x0000; COL_TAB=0xBDD7; COL_TAB_ON=0x1C9F; COL_OK=0x0480;
      COL_ADV=0x001F; COL_DIM=0x528A; break;
    case TB_HACKER:  // black bg + black (outline-only) keys so the rain shows through, neon-green text
      COL_BG=0x0000; COL_KEY=0x0000; COL_KEY_SPEC=0x0000; COL_KEY_DOWN=0x03E0;
      COL_TEXT=0x07E0; COL_TAB=0x1082; COL_TAB_ON=0x07E0; COL_OK=0x07E0;
      COL_ADV=0x07FF; COL_DIM=0x0560; break;
    case TB_PATRIOT: // navy bg, red/blue striped keys (below), white text + outlines
      COL_BG=USA_BLUE; COL_KEY=USA_RED; COL_KEY_SPEC=USA_KEYB; COL_KEY_DOWN=0xF800;
      COL_TEXT=0xFFFF; COL_TAB=USA_BLUE; COL_TAB_ON=USA_RED; COL_OK=0x07E0;
      COL_ADV=0xFFFF; COL_DIM=0xC618; break;
    default:         // TB_DARK (original)
      COL_BG=0x10A2; COL_KEY=0x39E7; COL_KEY_SPEC=0x29A6; COL_KEY_DOWN=0x04F3;
      COL_TEXT=0xFFFF; COL_TAB=0x2104; COL_TAB_ON=0x04F3; COL_OK=0x07E8;
      COL_ADV=0x04DF; COL_DIM=0x8C71; break;
  }
}

static void applyBrightness() {
  tft.setBrightness((uint16_t)brightnessPct * 255 / 100);
}

static void savePrefs() {
  Preferences p; p.begin("tbdisp", false);
  p.putUChar("theme", theme);
  p.putUChar("bright", brightnessPct);
  p.end();
}

static void loadPrefs() {
  Preferences p; p.begin("tbdisp", true);
  uint8_t def = p.getBool("dark", true) ? TB_DARK : TB_LIGHT;   // migrate old bool
  theme = p.getUChar("theme", def);
  if (theme > TB_PATRIOT) theme = TB_DARK;   // clamp any stale/removed value
  brightnessPct = p.getUChar("bright", 80);
  p.end();
  if (brightnessPct < 10) brightnessPct = 10;    // guard against a bad stored value
  if (brightnessPct > 100) brightnessPct = 100;
}

static void adjustBrightness(int delta) {
  int v = (int)brightnessPct + delta;
  if (v < 10)  v = 10;
  if (v > 100) v = 100;
  brightnessPct = (uint8_t)v;
  applyBrightness();
  savePrefs();
}

// ---------------- state ----------------
static ViewId curView = VIEW_KB;

enum CaseMode : uint8_t { CASE_LOWER, CASE_SHIFT, CASE_CAPS };
static CaseMode caseMode = CASE_LOWER;

// T9 candidate buffer
static int      pendKey      = -1;   // key the open sequence belongs to
static int      pendIdx      = 0;
static char     pendChar     = 0;    // cased candidate; 0 = buffer empty
static uint32_t pendDeadline = 0;

// current physical touch on the T9 view
static int      t9DownCell = -1;
static uint32_t t9DownTime = 0;
static bool     t9LongDone = false;

// generic views (numpad/nav) pressed key
struct ActiveKey {
  const KeyDef* k;
  int x, y, w, h;
  int tab;
  bool valid;
};
static ActiveKey down = { nullptr, 0, 0, 0, 0, -1, false };

static bool lastConn  = false;
static int  lastBonds = -1;

// ---------------- Home launcher ----------------
// A full-screen app launcher, opened by the Home button at the left of the
// status strip. Lives inside this (keyboard) program; "Open Marauder" reboots
// into the other app slot. Books/Soundboard/MIDI are placeholders until built.
static bool homeOpen     = false;
static int  homeDownTile = -1;
static const int HOME_BTN_W = 36;   // tap zone at the strip's left edge

static uint32_t ss_last_activity = 0;   // idle-screensaver timer (declared early: used in ui_onTouchDown)
static int      ss_drop[64];

static const char* TAB_LABELS[VIEW_COUNT] = { "T9", "123", "nav", "QWE", "BT" };

// BT screen buttons (stacked, portrait). Full-width now (was a 200px column
// sized for the 2.4" CYD); four evenly-spaced slots hold both the info-screen
// buttons and the settings-screen buttons.
static const KeyDef BT_BTN_ADV      = { "Re-advertise",     0, 0, ACT_BT_ADV,        1 };
static const KeyDef BT_BTN_CLEAR    = { "Forget hosts",     0, 0, ACT_BT_CLEAR,      1 };
static const KeyDef BT_BTN_SETTINGS = { "Settings",         0, 0, ACT_BT_SETTINGS,   1 };
static const KeyDef BT_BTN_EXIT     = { "Exit to Marauder", 0, 0, ACT_EXIT_MARAUDER, 1 };

// Settings sub-screen buttons ("Toggle Theme" now opens the theme picker)
static const KeyDef ST_BTN_BR_UP = { "Brightness +", 0, 0, ACT_BRIGHT_UP,   1 };
static const KeyDef ST_BTN_BR_DN = { "Brightness -", 0, 0, ACT_BRIGHT_DN,   1 };
static const KeyDef ST_BTN_THEME = { "Toggle Theme", 0, 0, ACT_OPEN_THEMES, 1 };
static const KeyDef ST_BTN_BACK  = { "Back",         0, 0, ACT_BT_BACK,     1 };

// Theme-picker sub-screen buttons (Light/Dark/Hacker + Back)
static const KeyDef TH_BTN_LIGHT  = { "Light Theme",  0, 0, ACT_SET_LIGHT,   1 };
static const KeyDef TH_BTN_DARK   = { "Dark Theme",   0, 0, ACT_SET_DARK,    1 };
static const KeyDef TH_BTN_HACKER = { "Hacker Theme", 0, 0, ACT_SET_HACKER,  1 };
static const KeyDef TH_BTN_USA    = { "USA Patriot",  0, 0, ACT_SET_USA,     1 };
static const KeyDef TH_BTN_BACK   = { "Back",         0, 0, ACT_THEMES_BACK, 1 };

static const int BT_BTN_X = 12, BT_BTN_W = SCREEN_W - 24, BT_BTN_H = 40;
// Four slots stacked up from the bottom (pitch 46) for info/settings screens.
static const int BT_BTN_Y[4] = { SCREEN_H - 196, SCREEN_H - 150, SCREEN_H - 104, SCREEN_H - 58 };
// Five slots down from the top for the theme picker.
static const int TH_BTN_Y[5] = { AREA_Y + 40, AREA_Y + 92, AREA_Y + 144, AREA_Y + 196, AREA_Y + 248 };

// Which button occupies each slot, per screen.
static const KeyDef* const BT_INFO_BTNS[4] = { &BT_BTN_ADV, &BT_BTN_CLEAR, &BT_BTN_SETTINGS, &BT_BTN_EXIT };
static const KeyDef* const BT_SET_BTNS[4]  = { &ST_BTN_BR_UP, &ST_BTN_BR_DN, &ST_BTN_THEME, &ST_BTN_BACK };
static const KeyDef* const TH_BTNS[5]      = { &TH_BTN_LIGHT, &TH_BTN_DARK, &TH_BTN_HACKER, &TH_BTN_USA, &TH_BTN_BACK };

// BT tab has three sub-screens.
enum { BTS_INFO = 0, BTS_SETTINGS, BTS_THEMES };
static uint8_t btScreen = BTS_INFO;

static const ViewDef* currentViewDef() {
  switch (curView) {
    case VIEW_NUM: return &VIEW_NUM_DEF;
    case VIEW_NAV: return &VIEW_NAV_DEF;
    default:       return nullptr;  // VIEW_KB and VIEW_BT draw themselves
  }
}

// ---------------- glyph drawing ----------------
static void drawGlyph(int cx, int cy, char code, uint16_t color) {
  const int s = 7;
  switch (code) {
    case 1:  tft.fillTriangle(cx, cy - s, cx - s, cy + s, cx + s, cy + s, color); break;
    case 2:  tft.fillTriangle(cx, cy + s, cx - s, cy - s, cx + s, cy - s, color); break;
    case 3:  tft.fillTriangle(cx - s, cy, cx + s, cy - s, cx + s, cy + s, color); break;
    case 4:  tft.fillTriangle(cx + s, cy, cx - s, cy - s, cx - s, cy + s, color); break;
    case 6:  // backspace: left arrow with tail
      tft.fillTriangle(cx - s - 2, cy, cx - 1, cy - 6, cx - 1, cy + 6, color);
      tft.fillRect(cx - 1, cy - 2, s + 3, 5, color);
      break;
    case 7:  // enter: down-then-left return arrow
      tft.fillRect(cx + 4, cy - s, 3, s + 2, color);
      tft.fillRect(cx - 3, cy - 1, 10, 3, color);
      tft.fillTriangle(cx - s, cy, cx - 1, cy - 5, cx - 1, cy + 5, color);
      break;
  }
}

// ---------------- battery indicator ----------------
// Little battery symbol at the right of the status strip: an outlined shell
// (themed via COL_TEXT so it reads on any skin), a proportional fill that goes
// green -> amber -> red as it drains, and the percent to its left. Returns the
// left-most x it painted so the strip can lay out other text without overlap.
// A small lightning bolt centred at (cx,cy): yellow with a dark outline so it
// reads over any fill colour. Shown only while charging.
static void drawChargeBolt(int cx, int cy) {
  tft.fillTriangle(cx + 4, cy - 8, cx - 5, cy + 2, cx + 1, cy + 1, 0x0000);
  tft.fillTriangle(cx - 4, cy + 8, cx + 5, cy - 2, cx - 1, cy - 1, 0x0000);
  tft.fillTriangle(cx + 3, cy - 7, cx - 4, cy + 1, cx + 0, cy + 1, 0xFFE0);
  tft.fillTriangle(cx - 3, cy + 7, cx + 4, cy - 1, cx + 0, cy - 1, 0xFFE0);
}

static int drawBatteryGlyph() {
  const int bw = 30, bh = 15, nub = 3, pad = 6;   // horizontal battery, enlarged
  int cy = STRIP_Y + STRIP_H / 2;
  int bx = SCREEN_W - pad - nub - bw;   // shell left edge
  int by = cy - bh / 2;

  // shell + positive-terminal nub
  tft.drawRect(bx, by, bw, bh, COL_TEXT);
  tft.drawRect(bx + 1, by + 1, bw - 2, bh - 2, COL_TEXT);   // 2px border, easier to read
  tft.fillRect(bx + bw, cy - 4, nub, 8, COL_TEXT);
  tft.fillRect(bx + 2, by + 2, bw - 4, bh - 4, COL_TAB);    // clear interior to strip bg

  if (!battery_present()) {
    // no plausible cell on the pin: leave the shell empty, show a dash
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextColor(COL_DIM, COL_TAB);
    tft.drawString("-", bx + bw / 2, cy);
    return bx;
  }

  uint8_t pct = battery_percent();
  uint16_t fill = pct > 50 ? 0x07E0 : pct > 20 ? 0xFD20 : 0xF800;   // green / amber / red
  int innerW = bw - 4;
  int fw = (innerW * pct + 50) / 100;
  if (fw > 0) tft.fillRect(bx + 2, by + 2, fw, bh - 4, fill);

  if (battery_charging()) drawChargeBolt(bx + bw / 2, cy);   // ⚡ overlay while charging

  char pb[6];
  snprintf(pb, sizeof pb, "%d%%", pct);
  tft.setTextDatum(textdatum_t::middle_right);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(COL_TEXT, COL_TAB);
  int txtRight = bx - 4;
  tft.drawString(pb, txtRight, cy);
  return txtRight - tft.textWidth(pb);
}

// Little house icon for the Home button (left of the status strip).
static void drawHomeIcon(int cx, int cy) {
  tft.fillTriangle(cx, cy - 8, cx - 9, cy + 1, cx + 9, cy + 1, COL_TEXT);  // roof
  tft.fillRect(cx - 6, cy + 1, 12, 8, COL_TEXT);                           // body
  tft.fillRect(cx - 2, cy + 4, 4, 5, COL_TAB);                            // door cutout
}

// ---------------- preview / status strip ----------------
static void drawStrip() {
  tft.fillRect(0, STRIP_Y, SCREEN_W, STRIP_H, COL_TAB);
  int scy = STRIP_Y + STRIP_H / 2;
  drawHomeIcon(16, scy);                       // Home button, far left (opposite the battery)
  bool conn = hidkb_connected();
  tft.fillCircle(46, scy, 5, conn ? COL_OK : COL_ADV);   // conn dot, shifted right of Home
  int battLeft = drawBatteryGlyph();   // right-aligned; other strip text stays left of this

  if (homeOpen) return;   // on the launcher, the strip is just Home + battery

  tft.setTextDatum(textdatum_t::middle_left);
  tft.setFont(&fonts::FreeSans9pt7b);

  if (curView == VIEW_KB) {
    // the "Nokia screen": pending candidate + case mode
    if (pendChar) {
      char buf[4] = { pendChar == ' ' ? '_' : pendChar, 0 };
      tft.setTextDatum(textdatum_t::middle_center);
      tft.setFont(&fonts::FreeSansBold12pt7b);
      tft.setTextColor(COL_TAB_ON, COL_TAB);
      tft.drawString(buf, SCREEN_W / 2, STRIP_Y + STRIP_H / 2);
    } else {
      tft.setTextColor(COL_DIM, COL_TAB);
      tft.drawString(conn ? "ready" : "pair me", 28, STRIP_Y + STRIP_H / 2);
    }
    tft.setTextDatum(textdatum_t::middle_right);
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextColor(caseMode == CASE_LOWER ? COL_DIM : COL_TAB_ON, COL_TAB);
    tft.drawString(caseMode == CASE_LOWER ? "abc" : caseMode == CASE_SHIFT ? "Abc" : "ABC",
                   battLeft - 6, STRIP_Y + STRIP_H / 2);
  } else {
    tft.setTextColor(conn ? COL_OK : COL_ADV, COL_TAB);
    tft.drawString(conn ? "linked" : "pair me", 28, STRIP_Y + STRIP_H / 2);
  }
}

// ---------------- T9 view ----------------
static void t9CellRect(int idx, int& x, int& y, int& w, int& h) {
  x = (idx % 3) * T9_COL_W;
  y = AREA_Y + (idx / 3) * T9_ROW_H;
  w = T9_COL_W;
  h = T9_ROW_H;
}

static void drawT9Key(int idx, bool pressed) {
  int x, y, w, h;
  t9CellRect(idx, x, y, w, h);
  bool spec = (idx >= 12 || idx == T9_CASE_KEY);
  uint16_t bg = pressed ? COL_KEY_DOWN : (spec ? COL_KEY_SPEC : COL_KEY);
  if (patriotOn() && !pressed) bg = patriotKeyColor(x + w / 2, y + h / 2, 0, AREA_Y, SCREEN_W, AREA_H);
  tft.fillRoundRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6, bg);
  keyOutlineRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6);

  int cx = x + w / 2, cy = y + h / 2;
  if (idx == T9_BKSP)    { drawGlyph(cx, cy, 6, COL_TEXT); return; }
  if (idx == T9_ADVANCE) { drawGlyph(cx, cy, 4, COL_TEXT); return; }
  if (idx == T9_ENTER)   { drawGlyph(cx, cy, 7, COL_TEXT); return; }

  const T9KeyDef& k = T9_KEYS[idx];
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, bg);
  tft.drawString(k.big, cx, y + 17);

  char buf[8];
  if (idx == T9_CASE_KEY) {
    strcpy(buf, caseMode == CASE_LOWER ? "abc" : caseMode == CASE_SHIFT ? "Abc" : "ABC");
  } else {
    strncpy(buf, k.small, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    if (caseMode != CASE_LOWER)
      for (char* p = buf; *p; p++) *p = toupper((unsigned char)*p);
  }
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(idx == T9_CASE_KEY ? COL_TAB_ON : COL_DIM, bg);
  tft.drawString(buf, cx, y + h - 14);
}

static void drawT9View() {
  tft.fillRect(0, AREA_Y, SCREEN_W, AREA_H, COL_BG);
  for (int i = 0; i < 15; i++) drawT9Key(i, false);
}

static char t9ApplyCase(char c) {
  if (caseMode != CASE_LOWER && isalpha((unsigned char)c))
    return toupper((unsigned char)c);
  return c;
}

// Send the buffered candidate as one HID character and close the sequence.
static void t9Commit() {
  pendKey = -1;
  if (!pendChar) return;
  char c = pendChar;
  pendChar = 0;
  Serial.printf("[t9] commit '%c'\n", c);
  hidkb_tapChar(c);
  if (caseMode == CASE_SHIFT && isalpha((unsigned char)c)) {
    caseMode = CASE_LOWER;  // one-shot shift consumed
    if (curView == VIEW_KB) { drawT9View(); }
  }
  if (curView == VIEW_KB) drawStrip();
}

// A confirmed tap on a cycle key: start or advance the candidate buffer.
static void t9Tap(int idx) {
  const T9KeyDef& k = T9_KEYS[idx];
  uint32_t now = millis();

  if (idx == pendKey && pendChar && now < pendDeadline) {
    pendIdx = (pendIdx + 1) % strlen(k.cycle);   // cycle in the buffer
  } else {
    t9Commit();                                  // different key interrupts
    pendKey = idx;
    pendIdx = 0;
  }
  pendChar = t9ApplyCase(k.cycle[pendIdx]);
  pendDeadline = now + T9_MULTITAP_MS;
  drawStrip();
}

static void t9TouchDown(int tx, int ty) {
  int col = tx / T9_COL_W;            if (col > 2) col = 2;
  int row = (ty - AREA_Y) / T9_ROW_H; if (row > 4) row = 4;
  int idx = row * 3 + col;

  t9DownCell = idx;
  t9DownTime = millis();
  t9LongDone = false;
  drawT9Key(idx, true);
  Serial.printf("[t9] down cell=%d\n", idx);

  // Act on DOWN: down is the reliable edge on this noisy panel; the touch
  // layer already filters ghosts, so there's nothing left to wait for.
  // Long-press (in ui_tick) can still upgrade a held letter key to its digit.
  switch (idx) {
    case T9_BKSP:
      if (pendChar) { pendChar = 0; pendKey = -1; drawStrip(); }  // drop candidate
      else hidkb_press(0x2A, 0);                                  // real backspace, repeats
      break;
    case T9_ENTER:
      t9Commit();
      hidkb_press(0x28, 0);
      break;
    case T9_ADVANCE:
      t9Commit();                                                 // manual same-key advance
      break;
    case T9_CASE_KEY:
      caseMode = (CaseMode)((caseMode + 1) % 3);
      drawT9View();
      drawStrip();
      break;
    default:
      t9Tap(idx);                                                 // buffer the letter now
      break;
  }
}

static void t9TouchUp() {
  if (t9DownCell < 0) return;
  int idx = t9DownCell;
  t9DownCell = -1;
  drawT9Key(idx, false);
  if (idx == T9_BKSP || idx == T9_ENTER) hidkb_releaseAll();
}

// ---------------- chrome: tabs ----------------
static void drawTabs() {
  for (int i = 0; i < VIEW_COUNT; i++) {
    bool on = (i == (int)curView);
    uint16_t tb = on ? COL_TAB_ON : COL_TAB;
    tft.fillRect(i * TAB_W, 0, TAB_W, TAB_H, tb);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextColor(on ? COL_BG : COL_TEXT, tb);
    tft.drawString(TAB_LABELS[i], i * TAB_W + TAB_W / 2, TAB_H / 2);
  }
}

// ---------------- generic key views (numpad / nav) ----------------
static void drawKey(const KeyDef* k, int x, int y, int w, int h, bool pressed) {
  uint16_t bg = pressed ? COL_KEY_DOWN
              : (k->action != ACT_NONE ? COL_KEY_SPEC : COL_KEY);
  if (patriotOn() && !pressed) bg = patriotKeyColor(x + w / 2, y + h / 2, 0, AREA_Y, SCREEN_W, AREA_H);
  tft.fillRoundRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6, bg);
  keyOutlineRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6);

  int cx = x + w / 2, cy = y + h / 2;
  if (k->label[0] != 0 && k->label[0] < 0x08) {
    drawGlyph(cx, cy, k->label[0], COL_TEXT);
    return;
  }
  if (k->label[0] == 0) return;  // space bar: blank face

  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(strlen(k->label) > 2 ? &fonts::FreeSans9pt7b : &fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, bg);
  tft.drawString(k->label, cx, cy);
}

static bool keyRect(const ViewDef* v, int row, int col, int& x, int& y, int& w, int& h) {
  if (!v || row >= v->count) return false;
  const RowDef& r = v->rows[row];
  if (col >= r.count) return false;

  float total = 0;
  for (int i = 0; i < r.count; i++) total += r.keys[i].w;

  int rowH = AREA_H / v->count;
  y = AREA_Y + row * rowH;
  h = rowH;

  float acc = 0;
  for (int i = 0; i < col; i++) acc += r.keys[i].w;
  x = (int)(SCREEN_W * acc / total + 0.5f);
  int x2 = (int)(SCREEN_W * (acc + r.keys[col].w) / total + 0.5f);
  w = x2 - x;
  return true;
}

static void drawView();  // defined below; used by the switch-failed fallback

// ---------------- dual-boot app switch ----------------
// Reboot into another app slot. Marauder lives in ota_0, TouchBoard in ota_1;
// esp_ota_set_boot_partition points the bootloader at the target, esp_restart
// jumps there (~1s). No image is copied — both apps stay resident in flash.
static void switchToApp(esp_partition_subtype_t sub, const char* name) {
  const esp_partition_t* p =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, nullptr);
  if (!p) { Serial.printf("[switch] %s partition not found\n", name); return; }

  tft.fillScreen(COL_BG);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(String("Starting ") + name + "...", SCREEN_W / 2, SCREEN_H / 2);

  esp_err_t err = esp_ota_set_boot_partition(p);
  Serial.printf("[switch] boot -> %s: %s\n", name, esp_err_to_name(err));
  if (err != ESP_OK) {                       // stay put if the target is bad
    tft.setTextColor(COL_ADV, COL_BG);
    tft.drawString("switch failed", SCREEN_W / 2, SCREEN_H / 2 + 26);
    delay(1500);
    drawView();
    return;
  }
  delay(200);
  esp_restart();
}

// ---------------- BT info screen ----------------
static void drawBTButton(const KeyDef* k, int y, bool pressed) {
  uint16_t bg = pressed ? COL_KEY_DOWN : COL_KEY_SPEC;
  if (patriotOn() && !pressed) bg = patriotKeyColor(BT_BTN_X + BT_BTN_W / 2, y + BT_BTN_H / 2, BT_BTN_X, AREA_Y, BT_BTN_W, AREA_H);
  tft.fillRoundRect(BT_BTN_X, y, BT_BTN_W, BT_BTN_H, 8, bg);
  keyOutlineRect(BT_BTN_X, y, BT_BTN_W, BT_BTN_H, 8);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold9pt7b);
  tft.setTextColor(COL_TEXT, bg);
  tft.drawString(k->label, BT_BTN_X + BT_BTN_W / 2, y + BT_BTN_H / 2);
}

static void drawBTView() {
  tft.fillRect(0, AREA_Y, SCREEN_W, AREA_H, COL_BG);
  bool conn = hidkb_connected();

  tft.setTextDatum(textdatum_t::top_left);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Bluetooth", 12, AREA_Y + 8);

  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(String("Name: ") + BLE_DEVICE_NAME, 12, AREA_Y + 32);

  tft.setTextColor(conn ? COL_OK : COL_ADV, COL_BG);
  if (conn) {
    tft.drawString("Connected:", 12, AREA_Y + 56);
    tft.drawString(hidkb_peerAddress(), 12, AREA_Y + 76);
  } else {
    tft.drawString("Advertising...", 12, AREA_Y + 56);
  }

  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(String("Paired hosts: ") + hidkb_bondCount(), 12, AREA_Y + 96);

  for (int i = 0; i < 4; i++) drawBTButton(BT_INFO_BTNS[i], BT_BTN_Y[i], false);
}

// ---------------- BT > Settings (display prefs) ----------------
static void drawBTSettings() {
  tft.fillRect(0, AREA_Y, SCREEN_W, AREA_H, COL_BG);

  tft.setTextDatum(textdatum_t::top_left);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Display", 12, AREA_Y + 8);

  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(String("Brightness: ") + brightnessPct + "%", 12, AREA_Y + 40);
  tft.drawString(String("Theme: ") + themeName(theme), 12, AREA_Y + 64);

  for (int i = 0; i < 4; i++) drawBTButton(BT_SET_BTNS[i], BT_BTN_Y[i], false);
}

// ---------------- BT > Settings > Theme picker ----------------
static void drawBTThemes() {
  tft.fillRect(0, AREA_Y, SCREEN_W, AREA_H, COL_BG);

  tft.setTextDatum(textdatum_t::top_left);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Theme", 12, AREA_Y + 8);

  // Buttons render in the *active* theme's style (so tapping one previews it live).
  for (int i = 0; i < 5; i++) drawBTButton(TH_BTNS[i], TH_BTN_Y[i], false);
}

// ---------------- QWERTY (landscape) ----------------
// T9 is charming but slow. This is the "I actually need to type a sentence"
// mode: a full keyboard, and the ONLY screen that flips the panel to landscape
// (320x240). Enter from the QWE tab; the on-key "abc" button drops back to
// portrait T9.
static const int LAND_W = 480, LAND_H = 320;   // 4" ST7796 landscape (was 320x240)
static const int QW_ROWS = 4;

static CaseMode qwCase = CASE_LOWER;      // tap shift: one-shot -> CAPS lock -> off
static int  qwDownRow = -1, qwDownCol = -1;

enum { QK_CHAR, QK_SHIFT, QK_BKSP, QK_ENTER, QK_BACK, QK_SPACE };
struct QKey { const char* cap; uint8_t type; char ch; float w; };

static const QKey QROW0[] = {
  {"q",QK_CHAR,'q',1},{"w",QK_CHAR,'w',1},{"e",QK_CHAR,'e',1},{"r",QK_CHAR,'r',1},{"t",QK_CHAR,'t',1},
  {"y",QK_CHAR,'y',1},{"u",QK_CHAR,'u',1},{"i",QK_CHAR,'i',1},{"o",QK_CHAR,'o',1},{"p",QK_CHAR,'p',1},
};
static const QKey QROW1[] = {
  {"a",QK_CHAR,'a',1},{"s",QK_CHAR,'s',1},{"d",QK_CHAR,'d',1},{"f",QK_CHAR,'f',1},{"g",QK_CHAR,'g',1},
  {"h",QK_CHAR,'h',1},{"j",QK_CHAR,'j',1},{"k",QK_CHAR,'k',1},{"l",QK_CHAR,'l',1},
};
static const QKey QROW2[] = {
  {"shift",QK_SHIFT,0,1.6f},{"z",QK_CHAR,'z',1},{"x",QK_CHAR,'x',1},{"c",QK_CHAR,'c',1},{"v",QK_CHAR,'v',1},
  {"b",QK_CHAR,'b',1},{"n",QK_CHAR,'n',1},{"m",QK_CHAR,'m',1},{"bksp",QK_BKSP,0,1.6f},
};
static const QKey QROW3[] = {
  {"abc",QK_BACK,0,1.6f},{",",QK_CHAR,',',1},{"space",QK_SPACE,' ',4},{".",QK_CHAR,'.',1},{"enter",QK_ENTER,0,1.6f},
};
static const QKey* const QROWS[QW_ROWS] = { QROW0, QROW1, QROW2, QROW3 };
static const int QROW_N[QW_ROWS] = { 10, 9, 9, 5 };

static void qKeyRect(int r, int c, int& x, int& y, int& w, int& h) {
  const QKey* row = QROWS[r];
  int n = QROW_N[r];
  float total = 0; for (int i = 0; i < n; i++) total += row[i].w;
  int rowH = LAND_H / QW_ROWS;
  y = r * rowH; h = rowH;
  float acc = 0; for (int i = 0; i < c; i++) acc += row[i].w;
  x = (int)(LAND_W * acc / total + 0.5f);
  int x2 = (int)(LAND_W * (acc + row[c].w) / total + 0.5f);
  w = x2 - x;
}

static void drawQKey(int r, int c, bool pressed) {
  const QKey& k = QROWS[r][c];
  int x, y, w, h; qKeyRect(r, c, x, y, w, h);
  bool spec = (k.type != QK_CHAR);
  uint16_t bg = pressed ? COL_KEY_DOWN : (spec ? COL_KEY_SPEC : COL_KEY);
  if (patriotOn() && !pressed) bg = patriotKeyColor(x + w / 2, y + h / 2, 0, 0, LAND_W, LAND_H);
  if (k.type == QK_SHIFT && qwCase != CASE_LOWER && !pressed) bg = COL_TAB_ON;  // lit: shift or caps
  tft.fillRoundRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6, bg);
  keyOutlineRect(x + KEY_GAP, y + KEY_GAP, w - 2 * KEY_GAP, h - 2 * KEY_GAP, 6);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(COL_TEXT, bg);
  char cap[8];
  if (k.type == QK_CHAR) {
    cap[0] = (qwCase != CASE_LOWER && k.ch >= 'a' && k.ch <= 'z') ? (char)(k.ch - 32) : k.ch;
    cap[1] = 0;
    tft.setFont(&fonts::FreeSansBold12pt7b);
  } else {
    const char* lbl = (k.type == QK_SHIFT && qwCase == CASE_CAPS) ? "CAPS" : k.cap;
    strncpy(cap, lbl, sizeof(cap) - 1); cap[sizeof(cap) - 1] = 0;
    tft.setFont(&fonts::FreeSans9pt7b);
  }
  tft.drawString(cap, x + w / 2, y + h / 2);
}

static void drawQwerty() {
  tft.fillScreen(COL_BG);
  for (int r = 0; r < QW_ROWS; r++)
    for (int c = 0; c < QROW_N[r]; c++)
      drawQKey(r, c, false);
}

// LovyanGFX's XPT2046 touch auto-rotates with the display, so in the
// setRotation(1) landscape frame tx/ty are ALREADY landscape screen coords —
// pass them straight through. (The old manual portrait->landscape rotation was
// needed only because the 2.4" board's capacitive chip didn't rotate.)
// If keys read mirrored/off in landscape, try setRotation(3) in drawView(), or
// mirror here: lx = LAND_W-1-tx and/or ly = LAND_H-1-ty.
static void qwertyMap(int tx, int ty, int& lx, int& ly) {
  lx = constrain(tx, 0, LAND_W - 1);
  ly = constrain(ty, 0, LAND_H - 1);
}

static bool qKeyAt(int lx, int ly, int& rr, int& cc) {
  int rowH = LAND_H / QW_ROWS;
  int r = ly / rowH; if (r >= QW_ROWS) r = QW_ROWS - 1;
  for (int c = 0; c < QROW_N[r]; c++) {
    int x, y, w, h; qKeyRect(r, c, x, y, w, h);
    if (lx >= x && lx < x + w) { rr = r; cc = c; return true; }
  }
  return false;
}

static void qwertyExit() {          // back to portrait T9
  qwCase = CASE_LOWER;
  qwDownRow = qwDownCol = -1;
  tft.setRotation(0);
  curView = VIEW_KB;
  drawView();
}

static void qwertyTouchDown(int tx, int ty) {
  int lx, ly; qwertyMap(tx, ty, lx, ly);
  int r, c;
  if (!qKeyAt(lx, ly, r, c)) { qwDownRow = qwDownCol = -1; return; }
  qwDownRow = r; qwDownCol = c;
  drawQKey(r, c, true);

  const QKey& k = QROWS[r][c];
  switch (k.type) {
    case QK_CHAR: {
      bool isAlpha = (k.ch >= 'a' && k.ch <= 'z');
      bool upper   = isAlpha && (qwCase != CASE_LOWER);
      hidkb_tapChar(upper ? (char)(k.ch - 32) : k.ch);
      if (isAlpha && qwCase == CASE_SHIFT) { qwCase = CASE_LOWER; drawQwerty(); }  // one-shot spent; CAPS sticks
      break;
    }
    case QK_SHIFT: qwCase = (CaseMode)((qwCase + 1) % 3); drawQwerty(); break;  // shift -> CAPS -> off
    case QK_BKSP:  hidkb_tap(0x2A, 0);                 break;  // Backspace
    case QK_ENTER: hidkb_tap(0x28, 0);                 break;  // Enter
    case QK_SPACE: hidkb_tapChar(' ');                 break;
    case QK_BACK:  qwertyExit();                       break;
  }
}

static void qwertyTouchUp() {
  if (qwDownRow < 0) return;
  int r = qwDownRow, c = qwDownCol;
  qwDownRow = qwDownCol = -1;
  if (curView == VIEW_QWERTY) drawQKey(r, c, false);  // a full redraw may have already happened
}

// ---------------- full redraw ----------------
// ---------------- Home launcher screen ----------------
struct HomeTile { const char* label; uint8_t act; bool ready; };
enum { HACT_KB = 0, HACT_MARAUDER, HACT_SOON };
static const HomeTile HOME_TILES[] = {
  { "Keyboard",   HACT_KB,       true  },
  { "Marauder",   HACT_MARAUDER, true  },
  { "Books",      HACT_SOON,     false },
  { "Soundboard", HACT_SOON,     false },
  { "MIDI",       HACT_SOON,     false },
};
static const int HOME_N = sizeof(HOME_TILES) / sizeof(HOME_TILES[0]);

static void homeTileRect(int i, int& x, int& y, int& w, int& h) {
  const int cols = 2;
  int gy = AREA_Y + 8;
  int gh = SCREEN_H - gy - 8;
  int rows = (HOME_N + cols - 1) / cols;
  int cw = SCREEN_W / cols;
  int rh = gh / rows;
  int r = i / cols, c = i % cols;
  x = c * cw + 8;  y = gy + r * rh + 6;
  w = cw - 16;     h = rh - 12;
}

static void drawHomeTile(int i, bool pressed) {
  int x, y, w, h; homeTileRect(i, x, y, w, h);
  const HomeTile& t = HOME_TILES[i];
  uint16_t bg = !t.ready ? COL_KEY : (pressed ? COL_KEY_DOWN : COL_KEY_SPEC);
  if (patriotOn() && t.ready && !pressed)
    bg = patriotKeyColor(x + w / 2, y + h / 2, 0, AREA_Y, SCREEN_W, SCREEN_H - AREA_Y);
  tft.fillRoundRect(x, y, w, h, 8, bg);
  keyOutlineRect(x, y, w, h, 8);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(t.ready ? COL_TEXT : COL_DIM, bg);
  tft.drawString(t.label, x + w / 2, y + h / 2 - (t.ready ? 0 : 8));
  if (!t.ready) {
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(COL_DIM, bg);
    tft.drawString("Soon", x + w / 2, y + h / 2 + 12);
  }
}

static int homeHitTest(int tx, int ty) {
  for (int i = 0; i < HOME_N; i++) {
    int x, y, w, h; homeTileRect(i, x, y, w, h);
    if (tx >= x && tx < x + w && ty >= y && ty < y + h) return i;
  }
  return -1;
}

static void drawHome() {
  tft.setRotation(0);
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, SCREEN_W, TAB_H, COL_TAB);          // header bar
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(COL_TAB_ON, COL_TAB);
  tft.drawString("Home", SCREEN_W / 2, TAB_H / 2);
  drawStrip();                                           // Home button + battery
  for (int i = 0; i < HOME_N; i++) drawHomeTile(i, false);
}

static void drawView() {
  if (homeOpen) { drawHome(); return; }
  if (curView == VIEW_QWERTY) { tft.setRotation(3); drawQwerty(); return; }  // landscape (flipped)
  tft.setRotation(0);          // every other screen is portrait (flipped 180° to match Marauder)
  drawTabs();
  drawStrip();
  if (curView == VIEW_KB) { drawT9View(); return; }
  if (curView == VIEW_BT) {
    if      (btScreen == BTS_THEMES)   drawBTThemes();
    else if (btScreen == BTS_SETTINGS) drawBTSettings();
    else                               drawBTView();
    return;
  }

  tft.fillRect(0, AREA_Y, SCREEN_W, AREA_H, COL_BG);
  const ViewDef* v = currentViewDef();
  int x, y, w, h;
  for (int r = 0; r < v->count; r++)
    for (int c = 0; c < v->rows[r].count; c++)
      if (keyRect(v, r, c, x, y, w, h))
        drawKey(&v->rows[r].keys[c], x, y, w, h, false);
}

// ---------------- input handling ----------------
void ui_onTouchDown(int tx, int ty) {
  ss_last_activity = millis();   // reset the screensaver idle timer
  if (curView == VIEW_QWERTY) { qwertyTouchDown(tx, ty); return; }  // landscape, own coords

  // Home button (left of the strip) toggles the launcher, from any portrait view.
  if (tx < HOME_BTN_W && ty >= STRIP_Y && ty < STRIP_Y + STRIP_H) {
    homeOpen = !homeOpen;
    drawView();
    return;
  }
  if (homeOpen) {                       // launcher is up: only tiles are live
    homeDownTile = homeHitTest(tx, ty);
    if (homeDownTile >= 0) drawHomeTile(homeDownTile, true);
    return;
  }

  down = { nullptr, 0, 0, 0, 0, -1, false };

  // tab bar
  if (ty < TAB_H) {
    int t = tx / TAB_W;
    if (t < VIEW_COUNT) { down.tab = t; down.valid = true; }
    return;
  }
  if (ty < AREA_Y) return;  // strip is display-only

  if (curView == VIEW_KB) { t9TouchDown(tx, ty); return; }

  // BT screen buttons (info / settings / theme-picker sub-screens)
  if (curView == VIEW_BT) {
    if (tx >= BT_BTN_X && tx < BT_BTN_X + BT_BTN_W) {
      const KeyDef* const* slots;  const int* ys;  int nslots;
      if (btScreen == BTS_THEMES)        { slots = TH_BTNS;      ys = TH_BTN_Y; nslots = 5; }
      else if (btScreen == BTS_SETTINGS) { slots = BT_SET_BTNS;  ys = BT_BTN_Y; nslots = 4; }
      else                               { slots = BT_INFO_BTNS; ys = BT_BTN_Y; nslots = 4; }
      for (int i = 0; i < nslots; i++) {
        if (ty >= ys[i] && ty < ys[i] + BT_BTN_H) { down.k = slots[i]; down.y = ys[i]; break; }
      }
      if (down.k) {
        down.valid = true;
        drawBTButton(down.k, down.y, true);
      }
    }
    return;
  }

  // generic key grids
  const ViewDef* v = currentViewDef();
  int x, y, w, h;
  for (int r = 0; r < v->count; r++) {
    for (int c = 0; c < v->rows[r].count; c++) {
      if (!keyRect(v, r, c, x, y, w, h)) continue;
      if (tx >= x && tx < x + w && ty >= y && ty < y + h) {
        down = { &v->rows[r].keys[c], x, y, w, h, -1, true };
        drawKey(down.k, x, y, w, h, true);
        if (down.k->action == ACT_NONE)
          hidkb_press(down.k->usage, down.k->mods);
        return;
      }
    }
  }
}

void ui_onTouchUp() {
  if (curView == VIEW_QWERTY) { qwertyTouchUp(); return; }

  if (homeOpen) {                       // launcher: act on the released tile
    int i = homeDownTile; homeDownTile = -1;
    if (i < 0) return;
    if (!HOME_TILES[i].ready) { drawHomeTile(i, false); return; }   // "Soon" tiles do nothing
    switch (HOME_TILES[i].act) {
      case HACT_KB:       homeOpen = false; curView = VIEW_KB; drawView(); break;
      case HACT_MARAUDER: switchToApp(ESP_PARTITION_SUBTYPE_APP_OTA_0, "Marauder"); break;
      default:            drawHomeTile(i, false); break;
    }
    return;
  }

  if (curView == VIEW_KB && !down.valid) { t9TouchUp(); return; }
  if (!down.valid) return;
  ActiveKey k = down;
  down.valid = false;

  // tab released -> switch view
  if (k.tab >= 0) {
    if ((ViewId)k.tab != curView) {
      t9Commit();
      curView = (ViewId)k.tab;
      btScreen = BTS_INFO;   // entering the BT tab always lands on the info screen
      drawView();
    }
    return;
  }
  if (!k.k) return;

  if (k.k->action == ACT_NONE) {
    hidkb_releaseAll();
    drawKey(k.k, k.x, k.y, k.w, k.h, false);
    return;
  }

  switch (k.k->action) {
    case ACT_BT_ADV:   hidkb_restartAdvertising(); drawBTView(); break;
    case ACT_BT_CLEAR: hidkb_clearBonds();         drawBTView(); break;
    case ACT_EXIT_MARAUDER:
      switchToApp(ESP_PARTITION_SUBTYPE_APP_OTA_0, "Marauder");
      break;
    case ACT_BT_SETTINGS: btScreen = BTS_SETTINGS; drawBTSettings(); break;
    case ACT_BT_BACK:     btScreen = BTS_INFO;     drawBTView();     break;
    case ACT_BRIGHT_UP:   adjustBrightness(+10);   drawBTSettings(); break;  // readout reflects new %
    case ACT_BRIGHT_DN:   adjustBrightness(-10);   drawBTSettings(); break;

    case ACT_OPEN_THEMES: btScreen = BTS_THEMES;   drawBTThemes();   break;
    case ACT_THEMES_BACK: btScreen = BTS_SETTINGS; drawBTSettings(); break;
    // Pick a theme: apply + persist, then repaint the whole UI in it (stays on
    // the picker so you preview the new look immediately).
    case ACT_SET_LIGHT:  theme = TB_LIGHT;  applyTheme(); savePrefs(); drawView(); break;
    case ACT_SET_DARK:   theme = TB_DARK;   applyTheme(); savePrefs(); drawView(); break;
    case ACT_SET_HACKER: theme = TB_HACKER; applyTheme(); savePrefs(); drawView(); break;
    case ACT_SET_USA:    theme = TB_PATRIOT; applyTheme(); savePrefs(); drawView(); break;
  }
}

// ---------------- Hacker "digital rain" behind the keyboard ----------------
// Same idea as Marauder's see-through menu: Hacker keys are black + green
// outline, so rain falls behind them. Rain SKIPS each key's label box (built
// below) so text never flickers, and re-stamps the green outlines each frame.
static int      tbdrop[64];
static uint32_t tbrain_tick = 0;
static int hkNBox = 0, hkBx0[96], hkBy0[96], hkBx1[96], hkBy1[96];   // protected label boxes
static int hkNKey = 0, hkKx[96], hkKy[96], hkKw[96], hkKh[96];       // key rects (for outline re-stamp)

static void hkAddBox(int x0, int y0, int x1, int y1) {
  if (hkNBox < 96) { hkBx0[hkNBox]=x0; hkBy0[hkNBox]=y0; hkBx1[hkNBox]=x1; hkBy1[hkNBox]=y1; hkNBox++; }
}
static void hkAddKey(int x, int y, int w, int h) {
  if (hkNKey < 96) { hkKx[hkNKey]=x; hkKy[hkNKey]=y; hkKw[hkNKey]=w; hkKh[hkNKey]=h; hkNKey++; }
}
static bool hkBlocked(int px, int py, int cw, int ch) {
  for (int b = 0; b < hkNBox; b++)
    if (px < hkBx1[b] && px + cw > hkBx0[b] && py < hkBy1[b] && py + ch > hkBy0[b]) return true;
  return false;
}

static void tbHackerRain(uint32_t now) {
  if (theme != TB_HACKER) return;
  bool land = (curView == VIEW_QWERTY);
  bool keyview = (curView == VIEW_KB || curView == VIEW_NUM || curView == VIEW_NAV || land);
  if (!keyview) return;
  static int tb_last_view = -1;
  if ((int)curView != tb_last_view) {   // re-seed the columns when the view changes
    tb_last_view = curView;
    for (int c = 0; c < 64; c++) tbdrop[c] = -(int)random(0, 30);
  }
  if (now - tbrain_tick < 60) return;   // ~16 fps
  tbrain_tick = now;

  int W = land ? LAND_W : SCREEN_W;
  int H = land ? LAND_H : SCREEN_H;
  int top = land ? 0 : AREA_Y;          // portrait: below tabs+strip; QWERTY: full landscape
  const int CW = 8, CH = 10, TRAIL = 6;
  int ncols = W / CW; if (ncols > 64) ncols = 64;
  int nrows = (H - top) / CH;

  // Collect key rects + protected label boxes for the active view.
  hkNBox = 0; hkNKey = 0;
  if (curView == VIEW_KB) {
    for (int i = 0; i < 15; i++) {
      int x, y, w, h; t9CellRect(i, x, y, w, h);
      hkAddKey(x, y, w, h);
      int cx = x + w / 2;
      hkAddBox(cx - 24, y + 10, cx + 24, y + h - 10);   // covers big char + small-letter row
    }
  } else if (land) {
    for (int r = 0; r < QW_ROWS; r++)
      for (int c = 0; c < QROW_N[r]; c++) {
        int x, y, w, h; qKeyRect(r, c, x, y, w, h);
        hkAddKey(x, y, w, h);
        int cx = x + w / 2, cy = y + h / 2;
        int hw = (int)strlen(QROWS[r][c].cap) * 5 + 5; if (hw > w / 2 - 2) hw = w / 2 - 2;
        hkAddBox(cx - hw, cy - 12, cx + hw, cy + 13);
      }
  } else {
    const ViewDef* v = currentViewDef();
    if (v)
      for (int r = 0; r < v->count; r++)
        for (int c = 0; c < v->rows[r].count; c++) {
          int x, y, w, h;
          if (!keyRect(v, r, c, x, y, w, h)) continue;
          hkAddKey(x, y, w, h);
          const char* lbl = v->rows[r].keys[c].label;
          int len = (lbl[0] && lbl[0] < 0x08) ? 2 : (int)strlen(lbl);
          int cx = x + w / 2, cy = y + h / 2;
          int hw = len * 5 + 5; if (hw > w / 2 - 2) hw = w / 2 - 2;
          hkAddBox(cx - hw, cy - 12, cx + hw, cy + 13);
        }
  }

  static const char* SET = "0123456789ABCDEF#$%&*+<>=/";
  int nch = strlen(SET);
  tft.setFont(&fonts::Font0);
  tft.setTextDatum(textdatum_t::top_left);
  for (int c = 0; c < ncols; c++) {
    int y = tbdrop[c], x = c * CW;
    for (int k = 0; k < 2; k++) {                    // bright head, dim trail
      int yy = y - k; if (yy < 0 || yy >= nrows) continue;
      int py = top + yy * CH;
      if (hkBlocked(x, py, CW, CH)) continue;
      char buf[2] = { SET[random(0, nch)], 0 };
      tft.setTextColor(k == 0 ? 0x07E0 : 0x0320, 0x0000);
      tft.drawString(buf, x, py);
    }
    int yt = y - TRAIL;
    if (yt >= 0 && yt < nrows) {
      int py = top + yt * CH;
      if (!hkBlocked(x, py, CW, CH)) tft.fillRect(x, py, CW, CH, 0x0000);
    }
    tbdrop[c]++;
    if (tbdrop[c] - TRAIL > nrows) tbdrop[c] = -(int)random(0, 20);
  }
  // Re-stamp the green key outlines over the rain (labels are left alone).
  for (int i = 0; i < hkNKey; i++)
    tft.drawRoundRect(hkKx[i] + KEY_GAP, hkKy[i] + KEY_GAP, hkKw[i] - 2 * KEY_GAP, hkKh[i] - 2 * KEY_GAP, 6, 0x07E0);
}

// ---------------- idle screensaver ----------------
// Mirrors Marauder: after ~25s of no touch, dim to 40% and play a themed
// animation with big, random, word-wrapped headlines. Hacker = green rain +
// green quotes (its 7 phrases + 7 technobabble). USA Patriot = navy backdrop,
// red/white/blue star rain + patriotic phrases in R/W/B. Light/Dark = plain
// black + white technobabble quotes. Touch to wake.
static void runScreensaver() {
  bool hacker  = (theme == TB_HACKER);
  bool patriot = (theme == TB_PATRIOT);
  tft.setRotation(0);                       // portrait for the screensaver
  int W = tft.width(), H = tft.height();
  uint16_t bg0 = patriot ? USA_BLUE : 0x0000;   // screensaver background (navy for Patriot)

  uint8_t saved = brightnessPct;
  tft.setBrightness(40 * 255 / 100);        // dim to 40%

  tft.fillScreen(bg0);

  static const char* HACK_TXT[]  = { "Enter the Matrix", "Zero-Day Exploit", "I'm bypassing the firewall",
                                     "Backlooping through the Mainframe", "Come on, baby, talk to me",
                                     "Now, we wait", "Too Easy" };
  static const char* USA_TXT[]   = { "USA! USA! USA!", "Land of the Free", "Home of the Brave",
                                     "1776", "We the People", "Don't Tread on Me",
                                     "Life, Liberty, Happiness", "E Pluribus Unum",
                                     "Give me Liberty", "Stars and Stripes Forever" };
  static const char* COMMON_TXT[] = {
    "Differential girdlespring: Connects the up-and-down parts to the analytical line.",
    "Sperry bearings: Parts used to balance the machine against a specific type of fake magnetic pull.",
    "Panendermic semi-boloid slots: Grooves cut into the base to help hold the fake gears.",
    "Non-reversible tremie pipe: A specialized tube used to control the fake flow of liquid.",
    "Lotus-o-delta type stator: A main power coil that helps stop electric feedback.",
    "Capacitive diractance: A fake electronic force that works against regular power resistance.",
    "Malleable logarithmic casing: The strong outer shell designed to hold the gears together." };
  const char** BASE = hacker ? HACK_TXT : (patriot ? USA_TXT : nullptr);
  int nbase = hacker ? 7 : (patriot ? 10 : 0);
  // Patriot shows only its own phrases (no technobabble); others append COMMON.
  int ncommon = 7, NPHR = patriot ? nbase : (nbase + ncommon);
  int phrase = -1; uint32_t lastPhrase = 0; bool firstPhrase = true;
  int bcy = H / 2, bandY0 = H / 2, bandY1 = H / 2, prevBandY0 = H / 2, prevBandY1 = H / 2;

  const int CW = 8, CH = 10, TRAIL = 7;
  int ncols = W / CW; if (ncols > 64) ncols = 64;
  int nrows = H / CH;
  for (int c = 0; c < ncols; c++) ss_drop[c] = -(int)random(0, nrows);
  static const char* SET = "0123456789ABCDEF#$%&*+<>=/";
  int nch = strlen(SET);

  TouchPoint tp;
  while (!touch_read(tp)) {                  // run until touched
    if (hacker) {
      tft.setFont(&fonts::Font0); tft.setTextSize(1); tft.setTextDatum(textdatum_t::top_left);
      for (int c = 0; c < ncols; c++) {
        int y = ss_drop[c], x = c * CW, hy = y * CH, dy = (y - 1) * CH, ty2 = (y - TRAIL) * CH;
        bool hb = (hy + CH > bandY0 && hy < bandY1), db = (dy + CH > bandY0 && dy < bandY1), tb = (ty2 + CH > bandY0 && ty2 < bandY1);
        char buf[2] = {0, 0};
        if (y >= 0 && y < nrows && !hb) { buf[0] = SET[random(0, nch)]; tft.setTextColor(0x07E0, 0x0000); tft.drawString(buf, x, hy); }
        if (y - 1 >= 0 && y - 1 < nrows && !db) { buf[0] = SET[random(0, nch)]; tft.setTextColor(0x0320, 0x0000); tft.drawString(buf, x, dy); }
        if (y - TRAIL >= 0 && y - TRAIL < nrows && !tb) tft.fillRect(x, ty2, CW, CH, 0x0000);
        ss_drop[c]++;
        if (ss_drop[c] - TRAIL > nrows) ss_drop[c] = -(int)random(0, 20);
      }
    } else if (patriot) {
      // Star rain: '*' glyphs falling in cycling red / white / blue over navy.
      static const uint16_t PC[3] = { 0xFFFF, USA_RED, USA_KEYB };
      tft.setFont(&fonts::Font0); tft.setTextSize(1); tft.setTextDatum(textdatum_t::top_left);
      for (int c = 0; c < ncols; c++) {
        int y = ss_drop[c], x = c * CW, hy = y * CH, ty2 = (y - TRAIL) * CH;
        bool hb = (hy + CH > bandY0 && hy < bandY1), tb = (ty2 + CH > bandY0 && ty2 < bandY1);
        if (y >= 0 && y < nrows && !hb) { tft.setTextColor(PC[c % 3], bg0); tft.drawString("*", x, hy); }
        if (y - TRAIL >= 0 && y - TRAIL < nrows && !tb) tft.fillRect(x, ty2, CW, CH, bg0);
        ss_drop[c]++;
        if (ss_drop[c] - TRAIL > nrows) ss_drop[c] = -(int)random(0, 20);
      }
    }

    uint32_t now = millis();
    if (firstPhrase || now - lastPhrase > 8000) {
      firstPhrase = false; lastPhrase = now;
      int np = 0; if (NPHR > 1) { do { np = random(0, NPHR); } while (np == phrase); }
      phrase = np;
      const char* s = (phrase < nbase) ? BASE[phrase] : COMMON_TXT[phrase - nbase];

      char lineText[12][40]; int lineSize[12]; int nAll = 0;
      auto wrapInto = [&](const char* str, int sz) {
        int cw = 6 * sz, cap = (W - 8) / cw; if (cap < 1) cap = 1;
        char work[224]; strncpy(work, str, sizeof(work) - 1); work[sizeof(work) - 1] = 0;
        char cur[40]; cur[0] = 0; int curlen = 0;
        for (char* tok = strtok(work, " "); tok && nAll < 12; tok = strtok(NULL, " ")) {
          int tl = strlen(tok);
          if (curlen == 0) { strncpy(cur, tok, 39); cur[39] = 0; curlen = tl; }
          else if (curlen + 1 + tl <= cap) { strcat(cur, " "); strncat(cur, tok, 39 - curlen - 1); curlen += 1 + tl; }
          else { strncpy(lineText[nAll], cur, 39); lineText[nAll][39] = 0; lineSize[nAll] = sz; nAll++; strncpy(cur, tok, 39); cur[39] = 0; curlen = tl; }
        }
        if (curlen > 0 && nAll < 12) { strncpy(lineText[nAll], cur, 39); lineText[nAll][39] = 0; lineSize[nAll] = sz; nAll++; }
      };
      const char* colon = strchr(s, ':');
      if (colon) {
        char term[80]; int tn = (int)(colon - s) + 1; if (tn > 79) tn = 79;
        strncpy(term, s, tn); term[tn] = 0;
        const char* def = colon + 1; while (*def == ' ') def++;
        wrapInto(term, 3); wrapInto(def, 2);      // 24pt term / 18pt definition
      } else wrapInto(s, 3);

      tft.fillRect(0, prevBandY0, W, prevBandY1 - prevBandY0, bg0);   // plate behind the headline
      int totalH = 0; for (int i = 0; i < nAll; i++) totalH += 8 * lineSize[i] + 4;
      int y0 = bcy - totalH / 2;
      bandY0 = y0 - 2; bandY1 = y0 + totalH + 2; prevBandY0 = bandY0; prevBandY1 = bandY1;

      static const uint16_t USA_LINE[3] = { 0xFFFF, USA_RED, USA_KEYB };  // Patriot: cycle white/red/blue per line
      tft.setFont(&fonts::Font0); tft.setTextDatum(textdatum_t::top_left);
      int ly = y0;
      for (int i = 0; i < nAll; i++) {
        int sz = lineSize[i], cw = 6 * sz, lh = 8 * sz + 4, llen = strlen(lineText[i]);
        int lx = (W - llen * cw) / 2; if (lx < 0) lx = 0;
        tft.setTextSize(sz);
        uint16_t fg = patriot ? USA_LINE[i % 3] : (hacker ? 0x07E0 : 0xFFFF);
        tft.setTextColor(fg, bg0);
        tft.drawString(lineText[i], lx, ly);
        ly += lh;
      }
    }
    delay(50);
  }

  while (touch_read(tp)) delay(10);          // wait for release so the wake tap doesn't type
  ss_last_activity = millis();
  applyBrightness();                         // restore brightness
  drawView();                                // repaint (also restores QWERTY rotation)
}

void ui_tick(uint32_t now) {
  if (now - ss_last_activity > 25000) { runScreensaver(); return; }   // idle screensaver
  tbHackerRain(now);   // live rain behind the Hacker keyboard (no-op in other themes/views)
  // T9: multi-tap window expired -> commit the candidate.
  // (Long-press-for-digit was removed: this panel holds "finger down" well
  // past the threshold after a physical lift, so it fired on normal taps and
  // turned every letter into its digit. Digits live on the 123 tab and at the
  // end of each key's cycle, e.g. tap '2' four times -> '2'.)
  if (pendChar && now > pendDeadline && curView == VIEW_KB) t9Commit();

  static uint32_t lastCheck = 0;
  if (now - lastCheck < 300) return;
  lastCheck = now;

  bool conn = hidkb_connected();
  int bonds = hidkb_bondCount();
  if (conn != lastConn || bonds != lastBonds) {
    lastConn = conn;
    lastBonds = bonds;
    if (curView != VIEW_QWERTY) drawStrip();   // strip is portrait-only
    if (!homeOpen && curView == VIEW_BT && btScreen == BTS_INFO) drawBTView();  // don't clobber settings/theme/home screens
  }

  // Battery: sample slowly, and only touch the screen / BLE when the % actually
  // moves — the gauge is smoothed, so this fires rarely.
  static uint32_t lastBatt    = 0;
  static uint8_t  lastBattPct = 255;
  static bool     lastChg     = false;
  if (now - lastBatt > 15000) {
    lastBatt = now;
    battery_update();
    uint8_t pct = battery_percent();
    bool chg = battery_charging();
    if (pct != lastBattPct || chg != lastChg) {  // % moved, or the bolt appeared/vanished
      lastBattPct = pct;
      lastChg = chg;
      hidkb_setBattery(pct);                     // keep the paired host in sync
      if (curView != VIEW_QWERTY) drawStrip();   // refresh the on-screen glyph
    }
  }
}

void ui_begin() {
  loadPrefs();          // restore saved theme + brightness
  applyTheme();
  applyBrightness();
  battery_begin();      // first sample so the strip has a real reading to draw
  hidkb_setBattery(battery_percent());   // seed the BLE battery char (was hardcoded 100)
  ss_last_activity = millis();   // start the screensaver idle timer fresh
  tft.fillScreen(COL_BG);
  drawView();
}
