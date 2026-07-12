#pragma once

// PCW keyboard matrix data, extracted from the main sketch for readability.
//
// Contents:
//   - PcwKey             : logical PCW key identifiers
//   - PcwMatrixEntry     : (offset, bit) location of a key's bit in pcwState[]
//   - PcwKeyMatrixEntry  : one matrix-table row (a PcwKey + its location)
//   - makePcwMatrixEntry : builds a PcwMatrixEntry from 1-based (row, column)
//   - kPcwKeyboardMatrix / kPcwJoystickMatrix : the PROGMEM matrix tables
//
// Header-only, meant to be included by exactly one translation unit (the .ino).
// The const PROGMEM tables have internal linkage, so even a second include from
// another translation unit would not cause a duplicate-symbol error.

#include <stdint.h>
#include <avr/pgmspace.h>

// One entry in a PROGMEM keyboard/joystick matrix table: where a PcwKey
// lives in pcwState (byte offset + bit within that byte).
struct PcwMatrixEntry
{
  uint8_t offset;  // pcwState[] byte offset (0x0-0xF)
  uint8_t bit;  // bit index within that byte (0-7)
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

// One entry in a PROGMEM keyboard/joystick matrix table.
struct PcwKeyMatrixEntry
{
  PcwKey key;  // the PCW key this row represents
  PcwMatrixEntry matrix;  // where that key's bit lives in pcwState
};

// Converts a 1-based matrix table row into its zero-based pcwState offset.
constexpr uint8_t pcwOffsetFromRow(uint8_t row)
{
  return static_cast<uint8_t>(row - 1U);
}

// Converts a 1-based matrix table column into its zero-based bit index.
constexpr uint8_t pcwBitFromColumn(uint8_t column)
{
  return static_cast<uint8_t>(column - 1U);
}

// Builds a matrix entry from the 1-based (row, column) coordinates used in
// the keyboard/joystick matrix tables below.
constexpr PcwMatrixEntry makePcwMatrixEntry(uint8_t row, uint8_t column)
{
  return {pcwOffsetFromRow(row), pcwBitFromColumn(column)};
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

constexpr uint16_t kPcwKeyboardMatrixCount = sizeof(kPcwKeyboardMatrix) / sizeof(kPcwKeyboardMatrix[0]);
constexpr uint16_t kPcwJoystickMatrixCount = sizeof(kPcwJoystickMatrix) / sizeof(kPcwJoystickMatrix[0]);
