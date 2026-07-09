/*
 * Amstrad PCW8256 PS/2 Keyboard Emulator
 *
 * In-development firmware for an Arduino Nano acting as an Amstrad PCW8256
 * keyboard emulator. It reads PS/2 keyboard make/break events, maintains a PCW
 * keyboard state buffer, and continuously transmits PCW keyboard state frames
 * through the original PCW keyboard clock/data interface.
 *
 * KEYTEST.COM is the PCW-side CP/M diagnostic used to observe the keyboard
 * state bytes at BFF0h-BFFFh while testing and debugging.
 */

#include <Arduino.h>
#include <PS2KeyAdvanced.h>
// Include the Library Manager item: PS2KeyAdvanced (currently 1.0.9)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

// Target platform: Arduino Nano, ATmega328P, 16 MHz.
// Hardware Serial is available over the Nano's onboard USB interface.
// Pinout:
// PS/2 keyboard CLK  -> D2
// PS/2 keyboard DATA -> D4
// PCW keyboard CLK   -> D3
// PCW keyboard DATA  -> D5
// Common GND
//
// The PCW +5V keyboard rail may be able to power both the Nano and the PS/2
// keyboard, but measure the available current and the keyboard's inrush/current
// draw before relying on that supply path.

#define PCW_DEBUG 1
#define DIAG_PCW_OUTPUT_ONLY 0
#define DIAG_PS2_ONLY        0
#define DIAG_FULL_EMULATOR   1

namespace
{
constexpr uint8_t PS2_CLK_PIN = 2;
constexpr uint8_t PS2_DATA_PIN = 4;
constexpr uint8_t PCW_CLK_PIN = 3;
constexpr uint8_t PCW_DATA_PIN = 5;

#if (DIAG_PCW_OUTPUT_ONLY + DIAG_PS2_ONLY + DIAG_FULL_EMULATOR) != 1
#error Exactly one diagnostic mode must be enabled.
#endif

constexpr uint8_t PCW_FRAME_WORDS = 17;
constexpr uint8_t PCW_FRAME_BITS = PCW_FRAME_WORDS * 12;
constexpr uint8_t PCW_WORD_OFFSET_FLAG = 0x0F;
constexpr uint8_t PCW_WORD_FLAG_UPDATE = 0x80;
constexpr uint8_t PCW_INVALID_OFFSET = 0xFF;
constexpr uint8_t PCW_IDLE_BYTE = 0xFF;
constexpr uint16_t PCW_STARTUP_TEST_TOGGLE_MS = 500;
constexpr uint8_t PCW_STARTUP_TEST_TOGGLES = 10;

// One complete PCW bit is three Timer1 ticks: data setup/high, clock low,
// clock high/advance. 7 us x 3 ~= 21 us per bit.
constexpr uint16_t TIMER1_TICK_US = 7;
#ifdef PCW_DEBUG
constexpr bool kDebugEnabled = true;
#else
constexpr bool kDebugEnabled = false;
#endif

#if PCW_DEBUG && !DIAG_PCW_OUTPUT_ONLY && !DIAG_FULL_EMULATOR
constexpr bool kVerboseKeyDebug = true;
#else
constexpr bool kVerboseKeyDebug = false;
#endif

enum PcwKey : uint8_t;

struct KeyMapEntry
{
  uint16_t ps2Key;
  PcwKey key;
};

struct KeyEvent
{
  uint16_t code;
  bool pressed;
  bool released;
  uint16_t raw;
};

struct PcwMatrixEntry
{
  uint8_t offset;
  uint8_t bit;
};

enum PcwKey : uint8_t
{
  PCW_KEY_NONE = 0,
  PCW_KEY_A,
  PCW_KEY_B,
  PCW_KEY_C,
  PCW_KEY_D,
  PCW_KEY_E,
  PCW_KEY_F,
  PCW_KEY_G,
  PCW_KEY_H,
  PCW_KEY_I,
  PCW_KEY_J,
  PCW_KEY_K,
  PCW_KEY_L,
  PCW_KEY_M,
  PCW_KEY_N,
  PCW_KEY_O,
  PCW_KEY_P,
  PCW_KEY_Q,
  PCW_KEY_R,
  PCW_KEY_S,
  PCW_KEY_T,
  PCW_KEY_U,
  PCW_KEY_V,
  PCW_KEY_W,
  PCW_KEY_X,
  PCW_KEY_Y,
  PCW_KEY_Z,
  PCW_KEY_0,
  PCW_KEY_1,
  PCW_KEY_2,
  PCW_KEY_3,
  PCW_KEY_4,
  PCW_KEY_5,
  PCW_KEY_6,
  PCW_KEY_7,
  PCW_KEY_8,
  PCW_KEY_9,
  PCW_KEY_SPACE,
  PCW_KEY_TAB,
  PCW_KEY_SHIFT,
  PCW_KEY_SHIFT_LOCK,
  PCW_KEY_ALT,
  PCW_KEY_ENTER,
  PCW_KEY_RETURN,
  PCW_KEY_STOP,
  PCW_KEY_CAN,
  PCW_KEY_EXTRA,
  PCW_KEY_CLR,
  PCW_KEY_COPY,
  PCW_KEY_CUT,
  PCW_KEY_PASTE,
  PCW_KEY_EXIT,
  PCW_KEY_PTR,
  PCW_KEY_RELAY,
  PCW_KEY_WORD_CHAR,
  PCW_KEY_UNIT_PARA,
  PCW_KEY_LINE_EOL,
  PCW_KEY_DOC_PAGE,
  PCW_KEY_EXCH_FIND,
  PCW_KEY_DEL_LEFT,
  PCW_KEY_DEL_RIGHT,
  PCW_KEY_CURSOR_LEFT,
  PCW_KEY_CURSOR_RIGHT,
  PCW_KEY_CURSOR_UP,
  PCW_KEY_CURSOR_DOWN,
  PCW_KEY_F1,
  PCW_KEY_F2,
  PCW_KEY_F3,
  PCW_KEY_F4,
  PCW_KEY_F5,
  PCW_KEY_F6,
  PCW_KEY_F7,
  PCW_KEY_F8,
  PCW_KEY_KP_0,
  PCW_KEY_KP_1,
  PCW_KEY_KP_2,
  PCW_KEY_KP_3,
  PCW_KEY_KP_4,
  PCW_KEY_KP_5,
  PCW_KEY_KP_6,
  PCW_KEY_KP_7,
  PCW_KEY_KP_8,
  PCW_KEY_KP_9,
  PCW_KEY_HALF,
  PCW_KEY_AT,
  PCW_KEY_HASH,
  PCW_KEY_GREATER_THAN,
  PCW_KEY_OPEN_BRACKET,
  PCW_KEY_OPEN_BRACE,
  PCW_KEY_CLOSE_BRACKET,
  PCW_KEY_CLOSE_BRACE,
  PCW_KEY_SLASH,
  PCW_KEY_QUESTION_MARK,
  PCW_KEY_SEMICOLON,
  PCW_KEY_COLON,
  PCW_KEY_MINUS,
  PCW_KEY_UNDERSCORE,
  PCW_KEY_EQUAL,
  PCW_KEY_PLUS,
  PCW_KEY_APOSTROPHE,
  PCW_KEY_QUOTE,
  PCW_KEY_OPEN_PAREN,
  PCW_KEY_CLOSE_PAREN,
  PCW_KEY_AMPERSAND,
  PCW_KEY_ASTERISK,
  PCW_KEY_PERCENT,
  PCW_KEY_DOLLAR,
  PCW_KEY_EXCLAMATION,
  PCW_KEY_DOUBLE_QUOTE,
  PCW_KEY_POUND,
  PCW_KEY_LESS_THAN,
  PCW_KEY_BOX_PLUS,
  PCW_KEY_CHECKERBOARD,
  PCW_KEY_DOTS,
  PCW_KEY_SPARE,
  PCW_KEY_JOY1_UP,
  PCW_KEY_JOY1_DOWN,
  PCW_KEY_JOY1_LEFT,
  PCW_KEY_JOY1_RIGHT,
  PCW_KEY_JOY1_FIRE1,
  PCW_KEY_JOY1_FIRE2,
  PCW_KEY_JOY2_UP,
  PCW_KEY_JOY2_DOWN,
  PCW_KEY_JOY2_LEFT,
  PCW_KEY_JOY2_RIGHT,
  PCW_KEY_JOY2_FIRE1,
  PCW_KEY_JOY2_FIRE2,
  PCW_KEY_COUNT
};

struct PcwKeyMatrixEntry
{
  PcwKey key;
  PcwMatrixEntry matrix;
};

volatile uint8_t pcwState[16];
uint8_t ps2Held[32];
uint8_t holdCount[PCW_KEY_COUNT];
volatile bool frameDirty = false;

volatile uint8_t activeFrameIndex = 0;
volatile uint16_t txBitIndex = 0;
volatile uint8_t txPhase = 0;

uint8_t frameBuffers[2][PCW_FRAME_BITS];
uint16_t frameLengths[2] = {0, 0};

uint8_t buildFrameIndex = 0;
uint16_t buildBitCount = 0;

PS2KeyAdvanced keyboard;

constexpr uint8_t pcwOffsetFromRow(uint8_t row)
{
  return static_cast<uint8_t>(row - 1U);
}

constexpr uint8_t pcwBitFromColumn(uint8_t column)
{
  return static_cast<uint8_t>(column - 1U);
}

constexpr uint8_t pcwMaskFromBit(uint8_t bit)
{
  return static_cast<uint8_t>(1U << bit);
}

constexpr PcwMatrixEntry makePcwMatrixEntry(uint8_t row, uint8_t column)
{
  return {pcwOffsetFromRow(row), pcwBitFromColumn(column)};
}

constexpr uint8_t kPs2HeldBytes = sizeof(ps2Held) / sizeof(ps2Held[0]);

bool isPs2Held(uint8_t code)
{
  return (ps2Held[code >> 3] & static_cast<uint8_t>(1U << (code & 0x07U))) != 0;
}

void setPs2Held(uint8_t code, bool held)
{
  const uint8_t mask = static_cast<uint8_t>(1U << (code & 0x07U));
  uint8_t &slot = ps2Held[code >> 3];
  if (held)
  {
    slot |= mask;
  }
  else
  {
    slot &= static_cast<uint8_t>(~mask);
  }
}

bool isIgnoredPs2Code(uint8_t code)
{
  switch (code)
  {
    case 0x00:
    case 0xAA:
    case 0xFA:
    case 0xFE:
      return true;

    default:
      return false;
  }
}

inline void pcwClockHigh()
{
  PORTD |= _BV(PD3);
}

inline void pcwClockLow()
{
  PORTD &= static_cast<uint8_t>(~_BV(PD3));
}

inline void pcwDataHigh()
{
  PORTD |= _BV(PD5);
}

inline void pcwDataLow()
{
  PORTD &= static_cast<uint8_t>(~_BV(PD5));
}

void initializePcwState()
{
  for (uint8_t i = 0; i < 16; ++i)
  {
    pcwState[i] = PCW_IDLE_BYTE;
  }

  for (uint8_t i = 0; i < PCW_KEY_COUNT; ++i)
  {
    holdCount[i] = 0;
  }

  for (uint8_t i = 0; i < kPs2HeldBytes; ++i)
  {
    ps2Held[i] = false;
  }
}

const PcwKeyMatrixEntry kPcwKeyboardMatrix[] PROGMEM = {
  {PCW_KEY_CHECKERBOARD, makePcwMatrixEntry(1, 8)},
  {PCW_KEY_2, makePcwMatrixEntry(1, 8)},
  {PCW_KEY_CURSOR_RIGHT, makePcwMatrixEntry(1, 7)},
  {PCW_KEY_3, makePcwMatrixEntry(1, 7)},
  {PCW_KEY_WORD_CHAR, makePcwMatrixEntry(1, 6)},
  {PCW_KEY_8, makePcwMatrixEntry(1, 6)},
  {PCW_KEY_UNIT_PARA, makePcwMatrixEntry(1, 5)},
  {PCW_KEY_9, makePcwMatrixEntry(1, 5)},
  {PCW_KEY_PASTE, makePcwMatrixEntry(1, 4)},
  {PCW_KEY_F1, makePcwMatrixEntry(1, 3)},
  {PCW_KEY_F2, makePcwMatrixEntry(1, 3)},
  {PCW_KEY_RELAY, makePcwMatrixEntry(1, 2)},
  {PCW_KEY_0, makePcwMatrixEntry(1, 2)},
  {PCW_KEY_F3, makePcwMatrixEntry(1, 1)},
  {PCW_KEY_F4, makePcwMatrixEntry(1, 1)},
  {PCW_KEY_CURSOR_LEFT, makePcwMatrixEntry(2, 8)},
  {PCW_KEY_1, makePcwMatrixEntry(2, 8)},
  {PCW_KEY_CURSOR_UP, makePcwMatrixEntry(2, 7)},
  {PCW_KEY_5, makePcwMatrixEntry(2, 7)},
  {PCW_KEY_LINE_EOL, makePcwMatrixEntry(2, 6)},
  {PCW_KEY_4, makePcwMatrixEntry(2, 6)},
  {PCW_KEY_DOC_PAGE, makePcwMatrixEntry(2, 5)},
  {PCW_KEY_6, makePcwMatrixEntry(2, 5)},
  {PCW_KEY_COPY, makePcwMatrixEntry(2, 4)},
  {PCW_KEY_CUT, makePcwMatrixEntry(2, 3)},
  {PCW_KEY_PTR, makePcwMatrixEntry(2, 2)},
  {PCW_KEY_EXIT, makePcwMatrixEntry(2, 1)},
  {PCW_KEY_BOX_PLUS, makePcwMatrixEntry(3, 8)},
  {PCW_KEY_HALF, makePcwMatrixEntry(3, 7)},
  {PCW_KEY_AT, makePcwMatrixEntry(3, 7)},
  {PCW_KEY_SHIFT, makePcwMatrixEntry(3, 6)},
  {PCW_KEY_EXCH_FIND, makePcwMatrixEntry(3, 5)},
  {PCW_KEY_7, makePcwMatrixEntry(3, 5)},
  {PCW_KEY_HASH, makePcwMatrixEntry(3, 4)},
  {PCW_KEY_GREATER_THAN, makePcwMatrixEntry(3, 4)},
  {PCW_KEY_RETURN, makePcwMatrixEntry(3, 3)},
  {PCW_KEY_CLOSE_BRACKET, makePcwMatrixEntry(3, 2)},
  {PCW_KEY_CLOSE_BRACE, makePcwMatrixEntry(3, 2)},
  {PCW_KEY_DEL_RIGHT, makePcwMatrixEntry(3, 1)},
  {PCW_KEY_DOTS, makePcwMatrixEntry(4, 8)},
  {PCW_KEY_SLASH, makePcwMatrixEntry(4, 7)},
  {PCW_KEY_QUESTION_MARK, makePcwMatrixEntry(4, 7)},
  {PCW_KEY_SEMICOLON, makePcwMatrixEntry(4, 6)},
  {PCW_KEY_COLON, makePcwMatrixEntry(4, 6)},
  {PCW_KEY_POUND, makePcwMatrixEntry(4, 5)},
  {PCW_KEY_LESS_THAN, makePcwMatrixEntry(4, 5)},
  {PCW_KEY_P, makePcwMatrixEntry(4, 4)},
  {PCW_KEY_OPEN_BRACKET, makePcwMatrixEntry(4, 3)},
  {PCW_KEY_OPEN_BRACE, makePcwMatrixEntry(4, 3)},
  {PCW_KEY_MINUS, makePcwMatrixEntry(4, 2)},
  {PCW_KEY_UNDERSCORE, makePcwMatrixEntry(4, 2)},
  {PCW_KEY_EQUAL, makePcwMatrixEntry(4, 1)},
  {PCW_KEY_PLUS, makePcwMatrixEntry(4, 1)},
  {PCW_KEY_APOSTROPHE, makePcwMatrixEntry(5, 8)},
  {PCW_KEY_QUOTE, makePcwMatrixEntry(5, 8)},
  {PCW_KEY_M, makePcwMatrixEntry(5, 7)},
  {PCW_KEY_K, makePcwMatrixEntry(5, 6)},
  {PCW_KEY_L, makePcwMatrixEntry(5, 5)},
  {PCW_KEY_I, makePcwMatrixEntry(5, 4)},
  {PCW_KEY_O, makePcwMatrixEntry(5, 3)},
  {PCW_KEY_KP_9, makePcwMatrixEntry(5, 2)},
  {PCW_KEY_OPEN_PAREN, makePcwMatrixEntry(5, 2)},
  {PCW_KEY_KP_0, makePcwMatrixEntry(5, 1)},
  {PCW_KEY_CLOSE_PAREN, makePcwMatrixEntry(5, 1)},
  {PCW_KEY_SPACE, makePcwMatrixEntry(6, 8)},
  {PCW_KEY_N, makePcwMatrixEntry(6, 7)},
  {PCW_KEY_J, makePcwMatrixEntry(6, 6)},
  {PCW_KEY_H, makePcwMatrixEntry(6, 5)},
  {PCW_KEY_Y, makePcwMatrixEntry(6, 4)},
  {PCW_KEY_U, makePcwMatrixEntry(6, 3)},
  {PCW_KEY_KP_7, makePcwMatrixEntry(6, 2)},
  {PCW_KEY_AMPERSAND, makePcwMatrixEntry(6, 2)},
  {PCW_KEY_KP_8, makePcwMatrixEntry(6, 1)},
  {PCW_KEY_ASTERISK, makePcwMatrixEntry(6, 1)},
  {PCW_KEY_V, makePcwMatrixEntry(7, 8)},
  {PCW_KEY_B, makePcwMatrixEntry(7, 7)},
  {PCW_KEY_F, makePcwMatrixEntry(7, 6)},
  {PCW_KEY_G, makePcwMatrixEntry(7, 5)},
  {PCW_KEY_T, makePcwMatrixEntry(7, 4)},
  {PCW_KEY_R, makePcwMatrixEntry(7, 3)},
  {PCW_KEY_KP_5, makePcwMatrixEntry(7, 2)},
  {PCW_KEY_PERCENT, makePcwMatrixEntry(7, 2)},
  {PCW_KEY_KP_6, makePcwMatrixEntry(7, 1)},
  {PCW_KEY_X, makePcwMatrixEntry(8, 8)},
  {PCW_KEY_C, makePcwMatrixEntry(8, 7)},
  {PCW_KEY_D, makePcwMatrixEntry(8, 6)},
  {PCW_KEY_S, makePcwMatrixEntry(8, 5)},
  {PCW_KEY_W, makePcwMatrixEntry(8, 4)},
  {PCW_KEY_E, makePcwMatrixEntry(8, 3)},
  {PCW_KEY_KP_3, makePcwMatrixEntry(8, 2)},
  {PCW_KEY_POUND, makePcwMatrixEntry(8, 2)},
  {PCW_KEY_KP_4, makePcwMatrixEntry(8, 1)},
  {PCW_KEY_DOLLAR, makePcwMatrixEntry(8, 1)},
  {PCW_KEY_Z, makePcwMatrixEntry(9, 8)},
  {PCW_KEY_SHIFT_LOCK, makePcwMatrixEntry(9, 7)},
  {PCW_KEY_A, makePcwMatrixEntry(9, 6)},
  {PCW_KEY_TAB, makePcwMatrixEntry(9, 5)},
  {PCW_KEY_Q, makePcwMatrixEntry(9, 4)},
  {PCW_KEY_STOP, makePcwMatrixEntry(9, 3)},
  {PCW_KEY_KP_2, makePcwMatrixEntry(9, 2)},
  {PCW_KEY_DOUBLE_QUOTE, makePcwMatrixEntry(9, 2)},
  {PCW_KEY_KP_1, makePcwMatrixEntry(9, 1)},
  {PCW_KEY_EXCLAMATION, makePcwMatrixEntry(9, 1)},
  {PCW_KEY_DEL_LEFT, makePcwMatrixEntry(10, 8)},
  {PCW_KEY_CURSOR_DOWN, makePcwMatrixEntry(10, 7)},
  {PCW_KEY_ENTER, makePcwMatrixEntry(10, 6)},
  {PCW_KEY_F7, makePcwMatrixEntry(10, 5)},
  {PCW_KEY_F8, makePcwMatrixEntry(10, 5)},
  {PCW_KEY_CLR, makePcwMatrixEntry(10, 4)},
  {PCW_KEY_CAN, makePcwMatrixEntry(10, 3)},
  {PCW_KEY_EXTRA, makePcwMatrixEntry(10, 2)},
  {PCW_KEY_F5, makePcwMatrixEntry(10, 1)},
  {PCW_KEY_F6, makePcwMatrixEntry(10, 1)},
  {PCW_KEY_ALT, makePcwMatrixEntry(11, 8)},
  {PCW_KEY_SPACE, makePcwMatrixEntry(11, 7)},
  {PCW_KEY_SPARE, makePcwMatrixEntry(12, 7)}
};

const PcwKeyMatrixEntry kPcwJoystickMatrix[] PROGMEM = {
  {PCW_KEY_JOY1_FIRE1, makePcwMatrixEntry(11, 6)},
  {PCW_KEY_JOY1_FIRE2, makePcwMatrixEntry(11, 5)},
  {PCW_KEY_JOY1_RIGHT, makePcwMatrixEntry(11, 4)},
  {PCW_KEY_JOY1_LEFT, makePcwMatrixEntry(11, 3)},
  {PCW_KEY_JOY1_DOWN, makePcwMatrixEntry(11, 2)},
  {PCW_KEY_JOY1_UP, makePcwMatrixEntry(11, 1)},
  {PCW_KEY_JOY2_FIRE1, makePcwMatrixEntry(12, 6)},
  {PCW_KEY_JOY2_FIRE2, makePcwMatrixEntry(12, 5)},
  {PCW_KEY_JOY2_RIGHT, makePcwMatrixEntry(12, 4)},
  {PCW_KEY_JOY2_LEFT, makePcwMatrixEntry(12, 3)},
  {PCW_KEY_JOY2_DOWN, makePcwMatrixEntry(12, 2)},
  {PCW_KEY_JOY2_UP, makePcwMatrixEntry(12, 1)}
};

const KeyMapEntry kKeyMap[] PROGMEM = {
  {PS2_KEY_ESC, PCW_KEY_EXIT},
  {PS2_KEY_1, PCW_KEY_1},
  {PS2_KEY_2, PCW_KEY_2},
  {PS2_KEY_3, PCW_KEY_3},
  {PS2_KEY_4, PCW_KEY_4},
  {PS2_KEY_5, PCW_KEY_5},
  {PS2_KEY_6, PCW_KEY_6},
  {PS2_KEY_7, PCW_KEY_7},
  {PS2_KEY_8, PCW_KEY_8},
  {PS2_KEY_9, PCW_KEY_9},
  {PS2_KEY_0, PCW_KEY_0},
  {PS2_KEY_TAB, PCW_KEY_TAB},
  {PS2_KEY_Q, PCW_KEY_Q},
  {PS2_KEY_W, PCW_KEY_W},
  {PS2_KEY_E, PCW_KEY_E},
  {PS2_KEY_R, PCW_KEY_R},
  {PS2_KEY_T, PCW_KEY_T},
  {PS2_KEY_Y, PCW_KEY_Y},
  {PS2_KEY_U, PCW_KEY_U},
  {PS2_KEY_I, PCW_KEY_I},
  {PS2_KEY_O, PCW_KEY_O},
  {PS2_KEY_P, PCW_KEY_P},
  {PS2_KEY_OPEN_SQ, PCW_KEY_OPEN_BRACKET},
  {PS2_KEY_CLOSE_SQ, PCW_KEY_CLOSE_BRACKET},
  {PS2_KEY_ENTER, PCW_KEY_ENTER},
  // The current PCW matrix table has no dedicated Control entry.
  {PS2_KEY_L_CTRL, PCW_KEY_EXTRA},
  {PS2_KEY_R_CTRL, PCW_KEY_EXTRA},
  {PS2_KEY_A, PCW_KEY_A},
  {PS2_KEY_S, PCW_KEY_S},
  {PS2_KEY_D, PCW_KEY_D},
  {PS2_KEY_F, PCW_KEY_F},
  {PS2_KEY_G, PCW_KEY_G},
  {PS2_KEY_H, PCW_KEY_H},
  {PS2_KEY_J, PCW_KEY_J},
  {PS2_KEY_K, PCW_KEY_K},
  {PS2_KEY_L, PCW_KEY_L},
  {PS2_KEY_SEMI, PCW_KEY_SEMICOLON},
  {PS2_KEY_APOS, PCW_KEY_APOSTROPHE},
  {PS2_KEY_L_SHIFT, PCW_KEY_SHIFT},
  {PS2_KEY_R_SHIFT, PCW_KEY_SHIFT},
  {PS2_KEY_Z, PCW_KEY_Z},
  {PS2_KEY_X, PCW_KEY_X},
  {PS2_KEY_C, PCW_KEY_C},
  {PS2_KEY_V, PCW_KEY_V},
  {PS2_KEY_B, PCW_KEY_B},
  {PS2_KEY_N, PCW_KEY_N},
  {PS2_KEY_M, PCW_KEY_M},
  {PS2_KEY_DIV, PCW_KEY_SLASH},
  {PS2_KEY_MINUS, PCW_KEY_MINUS},
  {PS2_KEY_EQUAL, PCW_KEY_EQUAL},
  {PS2_KEY_L_ALT, PCW_KEY_ALT},
  {PS2_KEY_R_ALT, PCW_KEY_ALT},
  {PS2_KEY_SPACE, PCW_KEY_SPACE},
  {PS2_KEY_CAPS, PCW_KEY_SHIFT_LOCK},
  {PS2_KEY_F1, PCW_KEY_F1},
  {PS2_KEY_F2, PCW_KEY_F2},
  {PS2_KEY_F3, PCW_KEY_F3},
  {PS2_KEY_F4, PCW_KEY_F4},
  {PS2_KEY_F5, PCW_KEY_F5},
  {PS2_KEY_F6, PCW_KEY_F6},
  {PS2_KEY_F7, PCW_KEY_F7},
  {PS2_KEY_F8, PCW_KEY_F8},
  {PS2_KEY_F9, PCW_KEY_WORD_CHAR},
  {PS2_KEY_F10, PCW_KEY_UNIT_PARA},
  {PS2_KEY_F11, PCW_KEY_RELAY},
  {PS2_KEY_F12, PCW_KEY_CLR},
  {PS2_KEY_SCROLL, PCW_KEY_HALF},
  {PS2_KEY_PRTSCR, PCW_KEY_PTR},
  {PS2_KEY_PAUSE, PCW_KEY_CAN},
  {PS2_KEY_L_GUI, PCW_KEY_EXCH_FIND},
  {PS2_KEY_R_GUI, PCW_KEY_EXCH_FIND},
  {PS2_KEY_UP_ARROW, PCW_KEY_CURSOR_UP},
  {PS2_KEY_DN_ARROW, PCW_KEY_CURSOR_DOWN},
  {PS2_KEY_L_ARROW, PCW_KEY_CURSOR_LEFT},
  {PS2_KEY_R_ARROW, PCW_KEY_CURSOR_RIGHT},
  {PS2_KEY_INSERT, PCW_KEY_PASTE},
  {PS2_KEY_HOME, PCW_KEY_COPY},
  {PS2_KEY_END, PCW_KEY_LINE_EOL},
  {PS2_KEY_PGUP, PCW_KEY_DOC_PAGE},
  {PS2_KEY_PGDN, PCW_KEY_CUT},
  {PS2_KEY_DELETE, PCW_KEY_DEL_RIGHT},
  {PS2_KEY_BS, PCW_KEY_DEL_LEFT},
  {PS2_KEY_KP0, PCW_KEY_KP_0},
  {PS2_KEY_KP1, PCW_KEY_KP_1},
  {PS2_KEY_KP2, PCW_KEY_KP_2},
  {PS2_KEY_KP3, PCW_KEY_KP_3},
  {PS2_KEY_KP4, PCW_KEY_KP_4},
  {PS2_KEY_KP5, PCW_KEY_KP_5},
  {PS2_KEY_KP6, PCW_KEY_KP_6},
  {PS2_KEY_KP7, PCW_KEY_KP_7},
  {PS2_KEY_KP8, PCW_KEY_KP_8},
  {PS2_KEY_KP9, PCW_KEY_KP_9},
  {PS2_KEY_KP_ENTER, PCW_KEY_ENTER}
};

constexpr uint16_t kPcwKeyboardMatrixCount = sizeof(kPcwKeyboardMatrix) / sizeof(kPcwKeyboardMatrix[0]);
constexpr uint16_t kPcwJoystickMatrixCount = sizeof(kPcwJoystickMatrix) / sizeof(kPcwJoystickMatrix[0]);
constexpr uint16_t kKeyMapCount = sizeof(kKeyMap) / sizeof(kKeyMap[0]);

bool findKeyMapEntry(uint16_t ps2Key, KeyMapEntry &result)
{
  for (uint16_t i = 0; i < kKeyMapCount; ++i)
  {
    KeyMapEntry entry;
    memcpy_P(&entry, &kKeyMap[i], sizeof(entry));
    if (entry.ps2Key == ps2Key)
    {
      result = entry;
      return true;
    }
  }
  return false;
}

bool findMatrixEntry(const PcwKeyMatrixEntry *table, uint16_t count, PcwKey key, PcwMatrixEntry &result)
{
  for (uint16_t i = 0; i < count; ++i)
  {
    PcwKeyMatrixEntry entry;
    memcpy_P(&entry, &table[i], sizeof(entry));
    if (entry.key == key)
    {
      result = entry.matrix;
      return true;
    }
  }
  return false;
}

bool findKeyboardMatrixEntry(PcwKey key, PcwMatrixEntry &result)
{
  return findMatrixEntry(kPcwKeyboardMatrix, kPcwKeyboardMatrixCount, key, result);
}

bool findJoystickMatrixEntry(PcwKey key, PcwMatrixEntry &result)
{
  return findMatrixEntry(kPcwJoystickMatrix, kPcwJoystickMatrixCount, key, result);
}

void beginFrameBuild()
{
  buildFrameIndex = activeFrameIndex ^ 0x01;
  buildBitCount = 0;
}

void appendBit(bool high)
{
  if (buildBitCount < PCW_FRAME_BITS)
  {
    frameBuffers[buildFrameIndex][buildBitCount++] = high ? 1 : 0;
  }
}

void sendPcwWord(uint8_t offset, uint8_t value)
{
  const uint16_t word = (static_cast<uint16_t>(offset & 0x0F) << 8) | value;
  for (int8_t bit = 11; bit >= 0; --bit)
  {
    appendBit((word >> bit) & 0x01U);
  }
}

void sendPcwFrame()
{
  uint8_t snapshot[16];

  noInterrupts();
  for (uint8_t i = 0; i < 16; ++i)
  {
    snapshot[i] = pcwState[i];
  }
  interrupts();

  beginFrameBuild();
  sendPcwWord(PCW_WORD_OFFSET_FLAG, static_cast<uint8_t>(snapshot[PCW_WORD_OFFSET_FLAG] | PCW_WORD_FLAG_UPDATE));
  for (uint8_t offset = 0; offset <= 0x0E; ++offset)
  {
    sendPcwWord(offset, snapshot[offset]);
  }
  sendPcwWord(PCW_WORD_OFFSET_FLAG, static_cast<uint8_t>(snapshot[PCW_WORD_OFFSET_FLAG] & ~PCW_WORD_FLAG_UPDATE));

  noInterrupts();
  frameLengths[buildFrameIndex] = buildBitCount;
  activeFrameIndex = buildFrameIndex;
  txBitIndex = 0;
  txPhase = 0;
  interrupts();
}

const __FlashStringHelper *activeDiagnosticModeName()
{
  #if DIAG_PCW_OUTPUT_ONLY
  return F("DIAG_PCW_OUTPUT_ONLY");
  #elif DIAG_PS2_ONLY
  return F("DIAG_PS2_ONLY");
  #else
  return F("DIAG_FULL_EMULATOR");
  #endif
}

void runStartupHardwareTest()
{
  for (uint8_t i = 0; i < PCW_STARTUP_TEST_TOGGLES; ++i)
  {
    if ((i & 0x01U) == 0)
    {
      pcwClockLow();
      pcwDataLow();
    }
    else
    {
      pcwClockHigh();
      pcwDataHigh();
    }
    delay(PCW_STARTUP_TEST_TOGGLE_MS);
  }

  pcwClockHigh();
  pcwDataHigh();
}

void applyKeyState(PcwKey key, bool pressed)
{
  PcwMatrixEntry matrixEntry;
  if (!findKeyboardMatrixEntry(key, matrixEntry))
  {
    if (!findJoystickMatrixEntry(key, matrixEntry))
    {
      if (kVerboseKeyDebug)
      {
        Serial.print(F("Unmapped PCW key "));
        Serial.println(static_cast<uint8_t>(key), DEC);
      }
      return;
    }
  }

  if (matrixEntry.offset > 0x0F || matrixEntry.bit > 7)
  {
    if (kVerboseKeyDebug)
    {
      Serial.print(F("Unmapped PCW key "));
      Serial.println(static_cast<uint8_t>(key), DEC);
    }
    return;
  }

  const uint8_t mask = pcwMaskFromBit(matrixEntry.bit);
  uint8_t updatedByte;

  noInterrupts();
  if (pressed)
  {
    pcwState[matrixEntry.offset] &= static_cast<uint8_t>(~mask);
  }
  else
  {
    pcwState[matrixEntry.offset] |= mask;
  }
  updatedByte = pcwState[matrixEntry.offset];
  frameDirty = true;
  interrupts();

  if (kVerboseKeyDebug)
  {
    Serial.print(F("PCW key "));
    Serial.print(static_cast<uint8_t>(key), DEC);
    Serial.print(pressed ? F(" make -> ") : F(" break -> "));
    Serial.print(F("PCW[0x"));
    Serial.print(matrixEntry.offset, HEX);
    Serial.print(F("] mask 0x"));
    Serial.print(mask, HEX);
    Serial.print(F(" value 0x"));
    Serial.println(updatedByte, HEX);
  }
}

// This wrapper keeps PS/2 library details in one place so we can later swap
// the keyboard source for USB, serial test input, or a different library
// without changing the PCW state array or the transmitter logic.
bool readKeyboardEvent(KeyEvent &event)
{
  while (keyboard.available())
  {
    event.raw = keyboard.read();
    event.code = event.raw & 0x00FFU;
    event.released = (event.raw & PS2_BREAK) != 0;
    event.pressed = !event.released;

    if (isIgnoredPs2Code(static_cast<uint8_t>(event.code)))
    {
      if (kVerboseKeyDebug)
      {
        Serial.print(F("Ignoring PS/2 non-key 0x"));
        Serial.println(event.code, HEX);
      }
      continue;
    }

    return true;
  }

  return false;
}

void updatePcwState(const KeyEvent &event)
{
  if (kVerboseKeyDebug)
  {
    Serial.print(F("PS/2 event raw=0x"));
    Serial.print(event.raw, HEX);
    Serial.print(F(" code=0x"));
    Serial.print(event.code, HEX);
    Serial.print(F(" state="));
    if (event.pressed)
    {
      Serial.println(F("pressed"));
    }
    else
    {
      Serial.println(F("released"));
    }
  }

  const uint8_t ps2Code = static_cast<uint8_t>(event.code & 0x00FFU);
  if (event.pressed)
  {
    if (isPs2Held(ps2Code))
    {
      if (kVerboseKeyDebug)
      {
        Serial.print(F("Ignoring PS/2 typematic repeat 0x"));
        Serial.println(ps2Code, HEX);
      }
      return;
    }

    setPs2Held(ps2Code, true);
  }
  else
  {
    setPs2Held(ps2Code, false);
  }

  KeyMapEntry entry;
  if (!findKeyMapEntry(event.code, entry))
  {
    if (kVerboseKeyDebug)
    {
      Serial.print(F("No table entry for PS/2 key 0x"));
      Serial.println(event.code, HEX);
    }
    return;
  }

  const uint8_t keyIndex = static_cast<uint8_t>(entry.key);
  if (keyIndex >= PCW_KEY_COUNT)
  {
    return;
  }

  if (event.pressed)
  {
    if (holdCount[keyIndex] == 0)
    {
      applyKeyState(entry.key, true);
    }

    if (holdCount[keyIndex] < 0xFF)
    {
      ++holdCount[keyIndex];
    }
  }
  else
  {
    if (holdCount[keyIndex] > 0)
    {
      --holdCount[keyIndex];
      if (holdCount[keyIndex] == 0)
      {
        applyKeyState(entry.key, false);
      }
    }
  }
}

void setupTimer1()
{
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1B |= _BV(WGM12);
  TCCR1B |= _BV(CS11);
  OCR1A = static_cast<uint16_t>((F_CPU / 8UL / (1000000UL / TIMER1_TICK_US)) - 1UL);
  TIMSK1 |= _BV(OCIE1A);
  sei();
}

ISR(TIMER1_COMPA_vect)
{
  const uint8_t currentFrame = activeFrameIndex;
  const uint16_t frameLength = frameLengths[currentFrame];

  if (frameLength == 0)
  {
    pcwClockHigh();
    pcwDataHigh();
    return;
  }

  const uint8_t bitValue = frameBuffers[currentFrame][txBitIndex];

  switch (txPhase)
  {
    case 0:
      if (bitValue)
      {
        pcwDataHigh();
      }
      else
      {
        pcwDataLow();
      }
      pcwClockHigh();
      txPhase = 1;
      break;

    case 1:
      pcwClockLow();
      txPhase = 2;
      break;

    default:
      pcwClockHigh();
      ++txBitIndex;
      if (txBitIndex >= frameLength)
      {
        txBitIndex = 0;
      }
      txPhase = 0;
      break;
  }
}
} // namespace

void setup()
{
  Serial.begin(115200);
  Serial.println(F("PCW keyboard emulator startup"));
  Serial.print(F("Active mode: "));
  Serial.println(activeDiagnosticModeName());

  pinMode(PS2_CLK_PIN, INPUT_PULLUP);
  pinMode(PS2_DATA_PIN, INPUT_PULLUP);

  pinMode(PCW_CLK_PIN, OUTPUT);
  pinMode(PCW_DATA_PIN, OUTPUT);
  pcwClockHigh();
  pcwDataHigh();
  runStartupHardwareTest();

  initializePcwState();

  // TODO: Some PCW8256 keys may live in byte 0xF as real matrix bits. If so,
  // reserve a non-conflicting bit for the update flag before filling the table.
  pcwState[PCW_WORD_OFFSET_FLAG] |= PCW_WORD_FLAG_UPDATE;

  #if DIAG_PS2_ONLY || DIAG_FULL_EMULATOR
  keyboard.begin(PS2_DATA_PIN, PS2_CLK_PIN);
  #endif

  #if DIAG_PCW_OUTPUT_ONLY || DIAG_FULL_EMULATOR
  sendPcwFrame();
  setupTimer1();
  #endif
}

void loop()
{
  #if DIAG_PCW_OUTPUT_ONLY
  static uint32_t lastHeartbeatMs = 0;
  sendPcwFrame();
  if (millis() - lastHeartbeatMs >= 1000UL)
  {
    Serial.println(F("PCW frame transmitter running"));
    lastHeartbeatMs = millis();
  }
  return;
  #endif

  #if DIAG_PS2_ONLY
  KeyEvent ev;
  while (readKeyboardEvent(ev))
  {
    Serial.print(F("raw=0x"));
    Serial.print(ev.raw, HEX);
    Serial.print(F(" code=0x"));
    Serial.print(ev.code, HEX);
    Serial.println(ev.pressed ? F(" pressed") : F(" released"));
  }
  return;
  #endif

  #if DIAG_FULL_EMULATOR
  KeyEvent ev;
  while (readKeyboardEvent(ev))
  {
    updatePcwState(ev);
  }

  sendPcwFrame();
  #endif
}
