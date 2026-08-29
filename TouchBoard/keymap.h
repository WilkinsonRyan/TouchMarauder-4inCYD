#pragma once
//
// Key layout data. Included only by ui.cpp.
//
// The main typing view is T9 multi-tap: each key carries a cycle string and
// repeated taps walk through it (a -> b -> c -> 2). Every cycle ends with the
// key's digit, which is also what a long-press types directly.
//
// HID usage IDs come from the USB HID Usage Tables, page 0x07 ("Keyboard").
//
#include <stdint.h>
#include "hidkb.h"

// Internal actions (keys that don't directly send a keycode)
enum : uint8_t {
  ACT_NONE = 0,      // normal key: send usage+mods
  ACT_BT_ADV,        // BT screen: restart advertising
  ACT_BT_CLEAR,      // BT screen: forget all bonds
  ACT_EXIT_MARAUDER, // BT screen: reboot into the Marauder app (ota_0)
  ACT_BT_SETTINGS,   // BT screen: open the display-settings sub-screen
  ACT_BRIGHT_UP,     // settings: brightness +10%
  ACT_BRIGHT_DN,     // settings: brightness -10%
  ACT_THEME_TOGGLE,  // (legacy) settings: switch dark/light theme
  ACT_BT_BACK,       // settings: back to the BT info screen
  ACT_OPEN_THEMES,   // settings: open the theme picker
  ACT_THEMES_BACK,   // theme picker: back to settings
  ACT_SET_LIGHT,     // theme picker: pick Light
  ACT_SET_DARK,      // theme picker: pick Dark
  ACT_SET_HACKER,    // theme picker: pick Hacker
};

// Single-byte label codes the renderer draws as shapes instead of text
#define LBL_UP    "\x01"
#define LBL_DOWN  "\x02"
#define LBL_LEFT  "\x03"
#define LBL_RIGHT "\x04"
#define LBL_BKSP  "\x06"
#define LBL_ENTER "\x07"

// ---------------- T9 keypad ----------------
// Index = row * 3 + col. Index 9 ('*') is the case-cycle key and has no
// cycle string; its small label is drawn from the current case mode.
struct T9KeyDef {
  const char* big;    // the digit/symbol shown large
  const char* small;  // the letters shown beneath it
  const char* cycle;  // multi-tap sequence; last char = long-press digit
};

static const T9KeyDef T9_KEYS[12] = {
  { "1", ".,?!",  ".,?!'\"1" },
  { "2", "abc",   "abc2"     },
  { "3", "def",   "def3"     },
  { "4", "ghi",   "ghi4"     },
  { "5", "jkl",   "jkl5"     },
  { "6", "mno",   "mno6"     },
  { "7", "pqrs",  "pqrs7"    },
  { "8", "tuv",   "tuv8"     },
  { "9", "wxyz",  "wxyz9"    },
  { "*", nullptr, nullptr    },  // case: abc / Abc / ABC
  { "0", "space", " 0"       },
  { "#", "@-_/:", "@-_/:#"   },
};
static const int T9_CASE_KEY = 9;

// ---------------- generic grid views (numpad / nav) ----------------
struct KeyDef {
  const char* label;
  uint8_t usage;   // HID usage ID (0 if action-only)
  uint8_t mods;    // modifier bits sent with the key
  uint8_t action;  // ACT_*
  float   w;       // width in relative units
};

struct RowDef { const KeyDef* keys; uint8_t count; };
struct ViewDef { const RowDef* rows; uint8_t count; };

#define N(arr) (sizeof(arr) / sizeof((arr)[0]))

// ---------------- Numpad ----------------
// Digits use the top-row usages (0x1E..0x27), not keypad usages (0x59+),
// so they work regardless of the host's Num Lock state.
static const KeyDef ROW_N1[] = {
  {"7",0x24,0,0,1},{"8",0x25,0,0,1},{"9",0x26,0,0,1},{"/",0x38,0,0,1},{LBL_BKSP,0x2A,0,0,1},
};
static const KeyDef ROW_N2[] = {
  {"4",0x21,0,0,1},{"5",0x22,0,0,1},{"6",0x23,0,0,1},{"*",0x25,KMOD_SHIFT,0,1},{"Tab",0x2B,0,0,1},
};
static const KeyDef ROW_N3[] = {
  {"1",0x1E,0,0,1},{"2",0x1F,0,0,1},{"3",0x20,0,0,1},{"-",0x2D,0,0,1},{"%",0x22,KMOD_SHIFT,0,1},
};
static const KeyDef ROW_N4[] = {
  {"0",0x27,0,0,1},{".",0x37,0,0,1},{"=",0x2E,0,0,1},{"+",0x2E,KMOD_SHIFT,0,1},{LBL_ENTER,0x28,0,0,1},
};
static const RowDef NUM_ROWS[] = {
  {ROW_N1,N(ROW_N1)},{ROW_N2,N(ROW_N2)},{ROW_N3,N(ROW_N3)},{ROW_N4,N(ROW_N4)},
};
static const ViewDef VIEW_NUM_DEF = { NUM_ROWS, N(NUM_ROWS) };

// ---------------- Navigation / arrows ----------------
static const KeyDef ROW_V1[] = {
  {"Esc",0x29,0,0,1},{"Home",0x4A,0,0,1},{LBL_UP,0x52,0,0,1},{"End",0x4D,0,0,1},{"PgUp",0x4B,0,0,1},
};
static const KeyDef ROW_V2[] = {
  {"Tab",0x2B,0,0,1},{LBL_LEFT,0x50,0,0,1},{LBL_DOWN,0x51,0,0,1},{LBL_RIGHT,0x4F,0,0,1},{"PgDn",0x4E,0,0,1},
};
static const KeyDef ROW_V3[] = {
  {"Del",0x4C,0,0,1},
  {"Copy",0x06,KMOD_CTRL,0,1},   // Ctrl+C — change KMOD_CTRL to KMOD_GUI for macOS
  {"Cut",0x1B,KMOD_CTRL,0,1},
  {"Paste",0x19,KMOD_CTRL,0,1},
  {LBL_ENTER,0x28,0,0,1},
};
static const KeyDef ROW_V4[] = {
  {LBL_BKSP,0x2A,0,0,2},
  {"",0x2C,0,0,3},               // space
};
static const RowDef NAV_ROWS[] = {
  {ROW_V1,N(ROW_V1)},{ROW_V2,N(ROW_V2)},{ROW_V3,N(ROW_V3)},{ROW_V4,N(ROW_V4)},
};
static const ViewDef VIEW_NAV_DEF = { NAV_ROWS, N(NAV_ROWS) };

#undef N
