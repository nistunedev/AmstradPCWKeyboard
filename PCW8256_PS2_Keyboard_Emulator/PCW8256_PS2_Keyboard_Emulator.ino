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
// PCW keyboard CLK   -> D3, push-pull OUTPUT, backed by an external 10k pull-up
// PCW keyboard DATA  -> D5, push-pull OUTPUT, backed by an external 10k pull-up
// Common GND
//
// The PCW +5V keyboard rail may be able to power both the Nano and the PS/2
// keyboard, but measure the available current and the keyboard's inrush/current
// draw before relying on that supply path.

#define DIAG_PCW_OUTPUT_ONLY 0
#define DIAG_PS2_ONLY        0
#define DIAG_FULL_EMULATOR   1

namespace
{
constexpr uint8_t PS2_CLK_PIN = 2;   // PS/2 keyboard clock input
constexpr uint8_t PS2_DATA_PIN = 4;  // PS/2 keyboard data input
constexpr uint8_t PCW_CLK_PIN = 3;   // PCW keyboard CLK output (push-pull)
constexpr uint8_t PCW_DATA_PIN = 5;  // PCW keyboard DATA output (push-pull)

// Diagnostic-only pin (D6/PD6): mirrors exactly what the firmware believes
// it is doing to PCW_CLK_PIN, on its own direct output with no external
// resistor network. Probe it alongside PCW_CLK_PIN to tell a firmware
// timing bug (both traces wrong) from a D3-specific hardware/probing issue
// (this trace right, PCW_CLK_PIN still wrong).
constexpr uint8_t DIAG_CLK_MIRROR_PIN = 6;  // diagnostic-only CLK echo, no external wiring

#if (DIAG_PCW_OUTPUT_ONLY + DIAG_PS2_ONLY + DIAG_FULL_EMULATOR) != 1
#error Exactly one diagnostic mode must be enabled.
#endif

constexpr uint8_t PCW_STATE_BYTES = 16;  // pcwState[] size: one byte per memory-map offset (0x0..0xF)
constexpr uint8_t PCW_FRAME_WORDS = 17;  // 12-bit words per keyboard frame
constexpr uint8_t PCW_BITS_PER_WORD = 12;  // 4-bit offset + 8-bit value
constexpr uint8_t PCW_WORD_VALUE_BITS = 8;  // width of a word's value field, i.e. the offset field's left shift
constexpr uint8_t PCW_FRAME_BITS = PCW_FRAME_WORDS * PCW_BITS_PER_WORD;  // total bits per frame
constexpr uint8_t PCW_WORD_OFFSET_FLAG = 0x0F;  // memory-map offset of the transmit/status flag word
constexpr uint8_t PCW_WORD_OFFSET_LINK_STATUS = 0x0D;  // memory-map offset of the link/status byte
constexpr uint8_t PCW_WORD_OFFSET_LAST_DATA = PCW_WORD_OFFSET_FLAG - 1U;  // last plain-data offset before the flag word
constexpr uint8_t PCW_WORD_OFFSET_OPTION_LINKS = 0x0E;  // memory-map offset of the option-links byte
constexpr uint8_t PCW_WORD_FLAG_TRANSMITTING = 0x80;  // flag-word bit set while a frame is actively transmitting
constexpr uint8_t PCW_WORD_FLAG_UPDATE_TOGGLE = 0x40;  // flag-word bit that flips every frame to signal new data

// sendPcwWord() serializes each word MSB-first: word bit 11 down to bit 0
// map to array indices 0..11 (index = PCW_BITS_PER_WORD - 1 - wordBit).
// PCW_WORD_FLAG_UPDATE_TOGGLE is word bit 6 (0x40), so it lands at array
// index 11 - 6 = 5. This is where the ISR patches the toggle bit directly
// into an already-built frame at the frame boundary (see ISR comment).
constexpr uint8_t PCW_UPDATE_TOGGLE_BIT_INDEX = 5;
constexpr uint8_t PCW_SHIFT_LOCK_LED_FLAG = 0x40;  // link-status bit driving the Shift Lock LED
constexpr uint8_t PCW_INVALID_OFFSET = 0xFF;  // sentinel for "no matrix offset"; currently unused
constexpr uint8_t PCW_IDLE_BYTE = 0x00;  // idle/reset value for a pcwState byte
constexpr uint8_t PCW_LINK_STATUS_INITIAL = 0x80;  // initial link-status byte: LK1 not fitted
constexpr uint8_t PCW_FLAG_INITIAL = 0xC0;  // initial flag-word byte before the first frame is built
constexpr uint8_t PCW_MAX_MATRIX_OFFSET = 0x0F;  // largest valid pcwState offset (4-bit nibble)
constexpr uint8_t PCW_MAX_MATRIX_BIT = 7;  // largest valid bit index within a matrix byte
constexpr uint8_t PCW_HOLD_COUNT_MAX = 0xFF;  // saturation cap for holdCount[] entries
// Dropped-break recovery: a held key whose PS/2 break byte was lost (corrupted
// by the transmitter's interrupt-off preamble windows) would otherwise stay
// "down" and auto-repeat on the PCW. We use the keyboard's typematic repeat as
// a keepalive: while a key is physically held the PS/2 keyboard resends make
// codes, refreshing keyLastMakeMs[]. If no make (initial or repeat) arrives for
// this long, the key is treated as released. Must exceed the typematic delay
// configured in setup() (typematic(0,0) = 0.25s), with margin, and also exceed
// the keyboard's default 0.5s delay in case that command didn't take effect.
constexpr uint16_t KEY_STUCK_RELEASE_MS = 600;
constexpr uint8_t FRAME_BUFFER_TOGGLE_MASK = 0x01;  // XOR mask to flip between the 2 frameBuffers[] slots

constexpr uint16_t PS2_CODE_SPACE = 256;  // PS/2 scancodes are 8-bit (0x00-0xFF)
constexpr uint8_t PS2_HELD_BITMAP_BYTES = PS2_CODE_SPACE / 8;  // ps2Held[] size, 1 bit per code
constexpr uint8_t PS2_HELD_BYTE_SHIFT = 3;  // code / 8 -> ps2Held[] byte index
constexpr uint8_t PS2_HELD_BIT_MASK = 0x07;  // code % 8 -> bit position within that byte
constexpr uint8_t PS2_CODE_NUL = 0x00;  // null/overrun, not a real key
constexpr uint8_t PS2_CODE_BAT_PASS = 0xAA;  // power-on self-test pass
constexpr uint8_t PS2_CODE_ACK = 0xFA;  // command acknowledge
constexpr uint8_t PS2_CODE_RESEND = 0xFE;  // request to resend last byte
constexpr uint16_t PS2_CODE_MASK = 0x00FFU;  // isolates the scancode byte from the raw PS2KeyAdvanced value
constexpr uint32_t SERIAL_BAUD_RATE = 115200;  // USB-serial debug console baud rate
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000UL;  // DIAG_PCW_OUTPUT_ONLY heartbeat print interval

// PCW timing follows the observed 8048 keyboard waveform on a fixed 6us
// Timer1 grid: each data bit has a 12us CLK-high window, DATA changes in the
// middle of that high window, and the normal CLK-low gap is 24us. The low
// after the fourth clock pulse is stretched to about 48us as the per-word
// marker. The D6 mirror follows the same firmware timing as D3 as an analyser
// reference.
//
// If set, the physical CLK pins are driven to the opposite of the logical
// level (logical "high" phase -> physical low). Determined empirically per
// rig: 0 makes the measured waveform match the real keyboard on the current setup. Re-check against the real PCW with
// KEYTEST.COM; flip it if keys misread.
#define PCW_INVERT_CLK 0

// Idle level driven by an attached keyboard between frames (the ~6.25 ms
// inter-frame gap) and before the first frame. The PCW motherboard pull-ups
// make disconnected/undriven lines read HIGH, but James Ots' real-keyboard
// traces and our captures show the active keyboard drives both CLK and DATA
// LOW most of the time, including between frame bursts. Default 0 (driven
// low) to match the real keyboard's active resting state.
#define PCW_IDLE_HIGH 0

// DATA line sense. The PCW input path is double-inverted through the 74HC14
// buffer chain, so the current working convention is natural polarity:
// logical 1 drives DATA high, logical 0 drives DATA low. Set 1 only for an
// explicit inverted-data diagnostic run.
#define PCW_INVERT_DATA 0

constexpr uint16_t TIMER1_PRESCALER = 8;  // Timer1 clock/8, giving TIMER1_COUNTS_PER_US counts per microsecond
constexpr uint16_t TIMER1_COUNTS_PER_US = F_CPU / TIMER1_PRESCALER / 1000000UL;  // Timer1 ticks per microsecond
constexpr uint8_t PCW_TICK_US = 6;  // one fixed Timer1 state-machine tick
constexpr uint16_t PCW_FRAME_GAP_TICKS = 1040;  // 1040 * 6us = 6.24ms inter-frame low/low idle
constexpr uint8_t PCW_PREAMBLE_TICKS = 8;  // DATA: low, high, low, high, low, low, low, low
constexpr uint8_t PCW_CLOCK_STEPS_PER_BIT = 6;  // 12us high + 24us low on a 6us grid
constexpr uint8_t PCW_CLOCK_LONG_LOW_STEPS_PER_BIT = 10;  // 12us high + 48us low after the marker pulse
constexpr uint8_t PCW_CLOCK_DATA_STEP = 1;  // update DATA in the middle of the 12us CLK-high window
constexpr uint8_t PCW_CLOCK_FALL_STEP = 2;  // falling edge latches DATA into the PCW gate array
constexpr uint8_t PCW_WORD_LONG_LOW_BIT = 3;  // stretch the low after the 4th clock pulse as the per-word marker
constexpr uint8_t PCW_INTER_WORD_GAP_TICKS = 24;  // 24 * 6us = 144us gap between words
constexpr uint8_t PCW_TICK_COUNTS = (PCW_TICK_US * TIMER1_COUNTS_PER_US);  // Timer1 counts per 6us tick
constexpr uint16_t PCW_PS2_ACTIVITY_PAUSE_TICKS = 334;  // 334 * 6us ~= 2ms quiet PS/2 window before restarting PCW frames
constexpr char BUILD_DATE[] = __DATE__;  // compile-time build date, printed in the startup banner
constexpr char BUILD_TIME[] = __TIME__;  // compile-time build time, printed in the startup banner

#if DIAG_PS2_ONLY
constexpr bool kVerboseKeyDebug = true;  // print every PS/2 event and key mapping decision to Serial
#else
constexpr bool kVerboseKeyDebug = false;  // stay quiet on Serial outside DIAG_PS2_ONLY
#endif

enum PcwKey : uint8_t;

// One entry in the PROGMEM PS/2-scancode-to-PcwKey lookup table.
struct KeyMapEntry
{
  uint16_t ps2Key;  // PS2KeyAdvanced scancode (e.g. PS2_KEY_A)
  PcwKey key;  // corresponding PCW keyboard key
};

// One decoded PS/2 key event, produced by readKeyboardEvent().
struct KeyEvent
{
  uint16_t code;  // scancode with the break flag masked off
  bool pressed;  // true if this is a make (key-down) event
  bool released;  // true if this is a break (key-up) event
  uint16_t raw;  // raw value as returned by PS2KeyAdvanced::read()
};

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

// Fixed-tick PCW transmitter state machine. Timer1 fires every 6us; the ISR
// only advances these counters and toggles pins. No variable OCR scheduling and
// no busy-waits are used in the transmit path.
enum class TxState : uint8_t
{
  InterFrame = 0,
  Preamble = 1,
  ClockBits = 2,
  InterWordGap = 3
};

volatile uint8_t pcwState[PCW_STATE_BYTES];  // live PCW state bytes, one per memory-map offset
uint8_t ps2Held[PS2_HELD_BITMAP_BYTES];  // bitmap of PS/2 scancodes currently held (make seen, no break yet)
uint8_t holdCount[PCW_KEY_COUNT];  // per-PcwKey ref count of PS/2 codes mapped to it, for overlap-safe release
uint16_t keyLastMakeMs[PCW_KEY_COUNT];  // millis() (16-bit) of the last make per held PcwKey; drives dropped-break auto-release
volatile bool frameDirty = false;  // true when pcwState changed since the last frame was built

volatile uint8_t activeFrameIndex = 0;  // index (0 or 1) of frameBuffers[] currently being transmitted
volatile uint8_t pendingFrameIndex = 0;  // index of a freshly built frame waiting to become active
volatile bool pendingFrameReady = false;  // true when pendingFrameIndex holds a frame ready to swap in
volatile bool pcwUpdateToggle = false;  // flips every transmitted frame so the PCW can detect new vs repeat
volatile uint16_t txBitIndex = 0;  // index of the next bit to transmit within the active frame buffer
volatile uint8_t txWordIndex = 0;  // 0..16 word currently being transmitted
volatile uint8_t txWordPhaseTick = 0;  // tick within the preamble state for this word
volatile uint8_t txBitInWord = 0;  // position (0-11) within the current 12-bit word being transmitted
volatile uint8_t txClockStep = 0;  // 0..5 sub-step within the current bit slot
volatile uint8_t txInterWordGapTicks = 0;  // 0..23 tick counter for the 144us inter-word gap
volatile uint16_t txInterFrameTicks = 0;  // 0..1039 tick counter for the 6.24ms inter-frame gap
volatile uint16_t txPs2ActivityPauseTicks = 0;  // extends inter-frame idle while PS/2 is actively clocking a byte
volatile TxState txState = TxState::InterFrame;  // current fixed-tick transmitter state
volatile bool pcwDataLineHigh = false;  // tracked physical DATA level, used for the pre-word pulses

uint8_t frameBuffers[2][PCW_FRAME_BITS];  // double-buffered serialized frame bits, one byte (0/1) per bit
uint16_t frameLengths[2] = {0, 0};  // bit length of each frameBuffers[] slot; 0 means "not yet built"
uint8_t * volatile txFrameBits = frameBuffers[0];  // active frame buffer cached for the Timer1 ISR
volatile uint16_t txFrameLength = 0;  // active frame bit length cached for the Timer1 ISR

uint8_t buildFrameIndex = 0;  // index of the frameBuffers[] slot currently being constructed
uint16_t buildBitCount = 0;  // number of bits written so far into the frame being built

PS2KeyAdvanced keyboard;  // PS/2 driver instance for the physical keyboard

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

// Builds the single-bit mask for a zero-based bit index within a byte.
constexpr uint8_t pcwMaskFromBit(uint8_t bit)
{
  return static_cast<uint8_t>(1U << bit);
}

// Builds a matrix entry from the 1-based (row, column) coordinates used in
// the keyboard/joystick matrix tables below.
constexpr PcwMatrixEntry makePcwMatrixEntry(uint8_t row, uint8_t column)
{
  return {pcwOffsetFromRow(row), pcwBitFromColumn(column)};
}

// Returns true if the given PS/2 scancode is currently marked as held down
// in the ps2Held bitmap.
bool isPs2Held(uint8_t code)
{
  return (ps2Held[code >> PS2_HELD_BYTE_SHIFT] &
          static_cast<uint8_t>(1U << (code & PS2_HELD_BIT_MASK))) != 0;
}

// Sets or clears the held-down bit for a PS/2 scancode in the ps2Held bitmap.
void setPs2Held(uint8_t code, bool held)
{
  const uint8_t mask = static_cast<uint8_t>(1U << (code & PS2_HELD_BIT_MASK));
  uint8_t &slot = ps2Held[code >> PS2_HELD_BYTE_SHIFT];
  if (held)
  {
    slot |= mask;
  }
  else
  {
    slot &= static_cast<uint8_t>(~mask);
  }
}

// Returns true for PS/2 codes that are not real keys (nulls, ACK, resend,
// self-test-pass) and should be dropped before matching against the map.
bool isIgnoredPs2Code(uint8_t code)
{
  switch (code)
  {
    case PS2_CODE_NUL:
    case PS2_CODE_BAT_PASS:
    case PS2_CODE_ACK:
    case PS2_CODE_RESEND:
      return true;

    default:
      return false;
  }
}

// Drives the real PCW CLK (D3) to its logical-high state (the bit's 12 us
// high phase). Push-pull OUTPUT backed by an external 10k pull-up. When
// PCW_INVERT_CLK is set the physical pin is driven LOW here so the path to
// the PCW presents a high ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â see PCW_INVERT_CLK above.
inline void pcwClockHigh()
{
#if PCW_INVERT_CLK
  PORTD &= static_cast<uint8_t>(~_BV(PD3));
#else
  PORTD |= _BV(PD3);
#endif
}

// Drives the real PCW CLK (D3) to its logical-low state (the bit's 21 us
// low phase).
inline void pcwClockLow()
{
#if PCW_INVERT_CLK
  PORTD |= _BV(PD3);
#else
  PORTD &= static_cast<uint8_t>(~_BV(PD3));
#endif
}

// Drives the diagnostic CLK mirror (D6) high/low. D6 shows the clock WITHOUT
// the per-word skip applied ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â a uniform reference every bit ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â so it can be
// compared on the scope against the real D3 CLK, which does skip. Uses the
// same PCW_INVERT_CLK polarity as D3 so the two read alike on the analyser.
inline void diagClockHigh()
{
#if PCW_INVERT_CLK
  PORTD &= static_cast<uint8_t>(~_BV(PD6));
#else
  PORTD |= _BV(PD6);
#endif
}

inline void diagClockLow()
{
#if PCW_INVERT_CLK
  PORTD |= _BV(PD6);
#else
  PORTD &= static_cast<uint8_t>(~_BV(PD6));
#endif
}

// Drives PCW DATA (D5) high. D5 is a push-pull OUTPUT; the external 10k
// pull-up backs it up but is no longer relied on for the high state.
inline void pcwDataHigh()
{
  PORTD |= _BV(PD5);
  pcwDataLineHigh = true;
}

// Drives PCW DATA (D5) low.
inline void pcwDataLow()
{
  PORTD &= static_cast<uint8_t>(~_BV(PD5));
  pcwDataLineHigh = false;
}

// Toggles PCW DATA without clocking. The original PCW keyboard emits two
// DATA pulses before each 12-bit word; from the low resting state that means
// four DATA transitions while CLK remains low.
inline void pcwDataToggle()
{
  if (pcwDataLineHigh)
  {
    pcwDataLow();
  }
  else
  {
    pcwDataHigh();
  }
}

// Drives CLK, the D6 mirror, and DATA to the inter-frame idle level selected
// by PCW_IDLE_HIGH. Used before the first frame and during the ~6.25 ms gap.
inline void pcwLinesIdle()
{
#if PCW_IDLE_HIGH
  pcwClockHigh();
  diagClockHigh();
  pcwDataHigh();
#else
  pcwClockLow();
  diagClockLow();
  pcwDataLow();
#endif
}

// Fast direct-port operations for the Timer1 ISR. These intentionally do not
// update pcwDataLineHigh and are macros rather than helpers, because the 6us
// state-machine tick cannot afford call/return or helper-side bookkeeping.
#if PCW_INVERT_CLK
#define PCW_CLK_HIGH_FAST() (PORTD &= static_cast<uint8_t>(~_BV(PD3)))
#define PCW_CLK_LOW_FAST()  (PORTD |= _BV(PD3))
#define DIAG_CLK_HIGH_FAST() (PORTD &= static_cast<uint8_t>(~_BV(PD6)))
#define DIAG_CLK_LOW_FAST()  (PORTD |= _BV(PD6))
#else
#define PCW_CLK_HIGH_FAST() (PORTD |= _BV(PD3))
#define PCW_CLK_LOW_FAST()  (PORTD &= static_cast<uint8_t>(~_BV(PD3)))
#define DIAG_CLK_HIGH_FAST() (PORTD |= _BV(PD6))
#define DIAG_CLK_LOW_FAST()  (PORTD &= static_cast<uint8_t>(~_BV(PD6)))
#endif

#if PCW_INVERT_DATA
#define PCW_DATA_HIGH_FAST() (PORTD &= static_cast<uint8_t>(~_BV(PD5)))
#define PCW_DATA_LOW_FAST()  (PORTD |= _BV(PD5))
#else
#define PCW_DATA_HIGH_FAST() (PORTD |= _BV(PD5))
#define PCW_DATA_LOW_FAST()  (PORTD &= static_cast<uint8_t>(~_BV(PD5)))
#endif

#define PCW_LINES_IDLE_FAST() \
  do \
  { \
    if (PCW_IDLE_HIGH) \
    { \
      PCW_CLK_HIGH_FAST(); \
      DIAG_CLK_HIGH_FAST(); \
      PCW_DATA_HIGH_FAST(); \
    } \
    else \
    { \
      PCW_CLK_LOW_FAST(); \
      DIAG_CLK_LOW_FAST(); \
      PCW_DATA_LOW_FAST(); \
    } \
  } while (0)

#if DIAG_FULL_EMULATOR
#define PS2_CLK_INHIBIT_FAST() \
  do \
  { \
    PORTD &= static_cast<uint8_t>(~_BV(PORTD2)); \
    DDRD |= _BV(DDD2); \
  } while (0)

#define PS2_CLK_RELEASE_FAST() \
  do \
  { \
    DDRD &= static_cast<uint8_t>(~_BV(DDD2)); \
    PORTD |= _BV(PORTD2); \
  } while (0)

#define PS2_BUS_IDLE_FAST() ((PIND & (_BV(PD2) | _BV(PD4))) == (_BV(PD2) | _BV(PD4)))

#define PCW_MASK_INPUT_IRQS_FAST() \
  do \
  { \
    PS2_CLK_INHIBIT_FAST(); \
    EIMSK &= static_cast<uint8_t>(~_BV(INT0)); \
    EIFR = _BV(INTF0); \
    TIMSK0 &= static_cast<uint8_t>(~_BV(TOIE0)); \
  } while (0)

#define PCW_UNMASK_INPUT_IRQS_FAST() \
  do \
  { \
    EIFR = _BV(INTF0); \
    EIMSK |= _BV(INT0); \
    TIMSK0 |= _BV(TOIE0); \
    PS2_CLK_RELEASE_FAST(); \
  } while (0)
#else
#define PS2_CLK_INHIBIT_FAST() do {} while (0)
#define PS2_CLK_RELEASE_FAST() do {} while (0)
#define PS2_BUS_IDLE_FAST() true
#define PCW_MASK_INPUT_IRQS_FAST() do {} while (0)
#define PCW_UNMASK_INPUT_IRQS_FAST() do {} while (0)
#endif

inline void maskPs2AndTimer0DuringPcwTransmit()
{
#if DIAG_FULL_EMULATOR
  PS2_CLK_INHIBIT_FAST();
  EIMSK &= static_cast<uint8_t>(~_BV(INT0));
  EIFR = _BV(INTF0);
  TIMSK0 &= static_cast<uint8_t>(~_BV(TOIE0));
#endif
}

inline void unmaskPs2AndTimer0DuringPcwGap()
{
#if DIAG_FULL_EMULATOR
  EIFR = _BV(INTF0);
  EIMSK |= _BV(INT0);
  TIMSK0 |= _BV(TOIE0);
  PS2_CLK_RELEASE_FAST();
#endif
}

inline void resetPcwTransmitterCounters()
{
  txBitIndex = 0;
  txWordIndex = 0;
  txWordPhaseTick = 0;
  txBitInWord = 0;
  txClockStep = 0;
  txInterWordGapTicks = 0;
  txInterFrameTicks = 0;
  txPs2ActivityPauseTicks = 0;
  txState = TxState::InterFrame;
}

// Resets the PCW state buffer, key hold counters, and PS/2 held-key bitmap
// to their idle values, and seeds the link/status bytes for the target
// keyboard's fitted-link configuration.
void initializePcwState()
{
  for (uint8_t i = 0; i < PCW_STATE_BYTES; ++i)
  {
    pcwState[i] = PCW_IDLE_BYTE;
  }

  for (uint8_t i = 0; i < PCW_KEY_COUNT; ++i)
  {
    holdCount[i] = 0;
  }

  for (uint8_t i = 0; i < PS2_HELD_BITMAP_BYTES; ++i)
  {
    ps2Held[i] = false;
  }

  // The target keyboard has LK1, LK2, and LK3 not fitted.
  pcwState[PCW_WORD_OFFSET_LINK_STATUS] = PCW_LINK_STATUS_INITIAL;
  pcwState[PCW_WORD_OFFSET_OPTION_LINKS] = PCW_IDLE_BYTE;
  pcwState[PCW_WORD_OFFSET_FLAG] = PCW_FLAG_INITIAL;
  pcwUpdateToggle = true;
  frameDirty = true;
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

// Looks up a PS/2 scancode in the PROGMEM key map. Returns true and fills
// result on a match.
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

// Looks up a PcwKey in a PROGMEM matrix table (keyboard or joystick).
// Returns true and fills result on a match.
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

// Looks up a PcwKey in the main keyboard matrix table.
bool findKeyboardMatrixEntry(PcwKey key, PcwMatrixEntry &result)
{
  return findMatrixEntry(kPcwKeyboardMatrix, kPcwKeyboardMatrixCount, key, result);
}

// Looks up a PcwKey in the joystick matrix table.
bool findJoystickMatrixEntry(PcwKey key, PcwMatrixEntry &result)
{
  return findMatrixEntry(kPcwJoystickMatrix, kPcwJoystickMatrixCount, key, result);
}

// Drives PCW DATA to the value of the bit currently pointed to by
// txBitIndex in the active frame buffer.
void loadCurrentPcwData()
{
  bool high = frameBuffers[activeFrameIndex][txBitIndex] != 0;
#if PCW_INVERT_DATA
  high = !high;
#endif
  if (high)
  {
    pcwDataHigh();
  }
  else
  {
    pcwDataLow();
  }
}
// Starts building the next frame into the inactive frame buffer slot.
void beginFrameBuild()
{
  buildFrameIndex = activeFrameIndex ^ FRAME_BUFFER_TOGGLE_MASK;
  buildBitCount = 0;
}

// Appends one bit (as a 0/1 byte) to the frame buffer currently being built.
void appendBit(bool high)
{
  if (buildBitCount < PCW_FRAME_BITS)
  {
    frameBuffers[buildFrameIndex][buildBitCount++] = high ? 1 : 0;
  }
}

// Appends a 12-bit word (4-bit offset, 8-bit value), MSB first, to the
// frame buffer currently being built.
void sendPcwWord(uint8_t offset, uint8_t value)
{
  const uint16_t word = (static_cast<uint16_t>(offset & PCW_MAX_MATRIX_OFFSET) << PCW_WORD_VALUE_BITS) | value;
  for (int8_t bit = PCW_BITS_PER_WORD - 1; bit >= 0; --bit)
  {
    appendBit((word >> bit) & 0x01U);
  }
}

// Builds a new 17-word frame from the current pcwState snapshot, if the
// state has changed and no built frame is already waiting to go active.
// The freshly built frame becomes active immediately if the transmitter is
// idle, otherwise it's queued as the pending frame and swapped in by the
// ISR at the next frame boundary.
void sendPcwFrame()
{
  noInterrupts();
  const bool shouldBuild = frameDirty && !pendingFrameReady;
  interrupts();
  if (!shouldBuild)
  {
    return;
  }

  uint8_t snapshot[PCW_STATE_BYTES];
  bool updateToggle;

  noInterrupts();
  for (uint8_t i = 0; i < PCW_STATE_BYTES; ++i)
  {
    snapshot[i] = pcwState[i];
  }
  updateToggle = pcwUpdateToggle;
  interrupts();

  beginFrameBuild();
  const uint8_t flagValue =
      static_cast<uint8_t>((snapshot[PCW_WORD_OFFSET_FLAG] &
                            ~(PCW_WORD_FLAG_TRANSMITTING |
                              PCW_WORD_FLAG_UPDATE_TOGGLE)) |
                           (updateToggle ? PCW_WORD_FLAG_UPDATE_TOGGLE : 0));
  sendPcwWord(PCW_WORD_OFFSET_FLAG,
              static_cast<uint8_t>(flagValue |
                                   PCW_WORD_FLAG_TRANSMITTING));
  for (uint8_t offset = 0; offset <= PCW_WORD_OFFSET_LAST_DATA; ++offset)
  {
    sendPcwWord(offset, snapshot[offset]);
  }
  sendPcwWord(PCW_WORD_OFFSET_FLAG, flagValue);

  noInterrupts();
  frameLengths[buildFrameIndex] = buildBitCount;
  if (frameLengths[activeFrameIndex] == 0)
  {
    activeFrameIndex = buildFrameIndex;
    txFrameBits = frameBuffers[activeFrameIndex];
    txFrameLength = frameLengths[activeFrameIndex];
    resetPcwTransmitterCounters();
  }
  else
  {
    pendingFrameIndex = buildFrameIndex;
    pendingFrameReady = true;
  }
  frameDirty = false;
  interrupts();
}

// Returns the name of whichever DIAG_* mode is compiled in, for the startup
// serial banner.
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

// Sets or clears the matrix bit for a PcwKey (keyboard or joystick) in
// pcwState and marks the frame dirty so the next sendPcwFrame() picks it up.
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

  if (matrixEntry.offset > PCW_MAX_MATRIX_OFFSET || matrixEntry.bit > PCW_MAX_MATRIX_BIT)
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
    pcwState[matrixEntry.offset] |= mask;
  }
  else
  {
    pcwState[matrixEntry.offset] &= static_cast<uint8_t>(~mask);
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

// Reads the next non-ignored PS/2 key event into event, if one is available.
// Returns false once the PS/2 library's queue is empty. This wrapper keeps
// PS/2 library details in one place so we can later swap the keyboard
// source for USB, serial test input, or a different library without
// changing the PCW state array or the transmitter logic.
bool readKeyboardEvent(KeyEvent &event)
{
  while (keyboard.available())
  {
    event.raw = keyboard.read();
    event.code = event.raw & PS2_CODE_MASK;
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

// Applies one PS/2 key event to the PCW state: handles typematic-repeat
// suppression, Shift/Shift Lock interaction, maps the PS/2 code to a PcwKey
// via kKeyMap, and applies make/break through applyKeyState() with hold
// counting so overlapping PS/2 codes mapped to the same PcwKey don't
// release it early.
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

  const uint8_t ps2Code = static_cast<uint8_t>(event.code & PS2_CODE_MASK);

  KeyMapEntry entry;
  const bool mapped = findKeyMapEntry(event.code, entry);
  const uint8_t keyIndex =
      mapped ? static_cast<uint8_t>(entry.key) : PCW_KEY_COUNT;
  const bool validKey = mapped && keyIndex < PCW_KEY_COUNT;

  if (event.pressed)
  {
    if (validKey)
    {
      // Keepalive: record every make (initial AND typematic repeat) so a held
      // key stays alive; a released key whose break was dropped stops being
      // refreshed and is auto-released by releaseStuckKeys() in loop().
      keyLastMakeMs[keyIndex] = static_cast<uint16_t>(millis());
    }
    if (isPs2Held(ps2Code))
    {
      if (kVerboseKeyDebug)
      {
        Serial.print(F("Ignoring PS/2 typematic repeat 0x"));
        Serial.println(ps2Code, HEX);
      }
      return;  // typematic repeat: heartbeat recorded above, no state change
    }
    setPs2Held(ps2Code, true);
  }
  else
  {
    setPs2Held(ps2Code, false);
  }

  if (!validKey)
  {
    if (kVerboseKeyDebug)
    {
      Serial.print(F("No table entry for PS/2 key 0x"));
      Serial.println(event.code, HEX);
    }
    return;
  }

  if (event.pressed)
  {
    if (entry.key == PCW_KEY_SHIFT_LOCK)
    {
      noInterrupts();
      pcwState[PCW_WORD_OFFSET_LINK_STATUS] ^= PCW_SHIFT_LOCK_LED_FLAG;
      frameDirty = true;
      interrupts();
    }
    else if (entry.key == PCW_KEY_SHIFT)
    {
      // With LK2 not fitted, either Shift key cancels Shift Lock.
      noInterrupts();
      pcwState[PCW_WORD_OFFSET_LINK_STATUS] &=
          static_cast<uint8_t>(~PCW_SHIFT_LOCK_LED_FLAG);
      frameDirty = true;
      interrupts();
    }

    if (holdCount[keyIndex] == 0)
    {
      applyKeyState(entry.key, true);
    }

    if (holdCount[keyIndex] < PCW_HOLD_COUNT_MAX)
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

// True for held modifier/toggle keys that do NOT reliably typematic-repeat, so
// they must be excluded from the keepalive auto-release (a held Shift would
// otherwise drop mid-hold). These rely on their PS/2 break; a dropped modifier
// break is rare and self-corrects on the next press of that modifier.
bool isNonRepeatingModifier(PcwKey key)
{
  return key == PCW_KEY_SHIFT || key == PCW_KEY_SHIFT_LOCK ||
         key == PCW_KEY_ALT || key == PCW_KEY_EXTRA || key == PCW_KEY_STOP;
}

// Clears the ps2Held bit for every PS/2 scancode that maps to the given PcwKey,
// so a future real press of that key isn't mistaken for a suppressed repeat.
void clearHeldForKey(PcwKey key)
{
  for (uint16_t i = 0; i < kKeyMapCount; ++i)
  {
    KeyMapEntry entry;
    memcpy_P(&entry, &kKeyMap[i], sizeof(entry));
    if (entry.key == key)
    {
      setPs2Held(static_cast<uint8_t>(entry.ps2Key & PS2_CODE_MASK), false);
    }
  }
}

// Dropped-break recovery. While a key is physically held the PS/2 keyboard
// resends typematic makes, refreshing keyLastMakeMs[] (see updatePcwState). If
// no make has arrived for a held key within KEY_STUCK_RELEASE_MS, its break
// byte was lost in a transmit interrupt-off window, so synthesize the release
// here. Runs in loop(), outside the timing-critical ISR, so it never disturbs
// the PCW waveform.
void releaseStuckKeys()
{
  const uint16_t now = static_cast<uint16_t>(millis());
  for (uint8_t k = 0; k < PCW_KEY_COUNT; ++k)
  {
    if (holdCount[k] == 0)
    {
      continue;
    }
    if (isNonRepeatingModifier(static_cast<PcwKey>(k)))
    {
      continue;
    }
    if (static_cast<uint16_t>(now - keyLastMakeMs[k]) > KEY_STUCK_RELEASE_MS)
    {
      applyKeyState(static_cast<PcwKey>(k), false);
      holdCount[k] = 0;
      clearHeldForKey(static_cast<PcwKey>(k));
      if (kVerboseKeyDebug)
      {
        Serial.print(F("Auto-released stuck PCW key "));
        Serial.println(k, DEC);
      }
    }
  }
}

// Configures Timer1 in CTC mode (prescaler /8) with one fixed 6us compare
// interval. The ISR never changes OCR1A; all PCW waveform timing is derived
// from tick counters in the state machine below.
void setupTimer1()
{
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1B |= _BV(WGM12);
  TCCR1B |= _BV(CS11);
  OCR1A = static_cast<uint16_t>(PCW_TICK_COUNTS - 1U);
  TCNT1 = 0;
  TIMSK1 |= _BV(OCIE1A);
  sei();
}

inline void beginInterFrameGap()
{
  pcwLinesIdle();
  unmaskPs2AndTimer0DuringPcwGap();
  txInterFrameTicks = 0;
  txPs2ActivityPauseTicks = 0;
  txState = TxState::InterFrame;
}

inline void beginWordPreamble()
{
  maskPs2AndTimer0DuringPcwTransmit();
  pcwClockLow();
  diagClockLow();
  pcwDataLow();
  txWordPhaseTick = 0;
  txState = TxState::Preamble;
}

inline void advanceCompletedFrame()
{
  txBitIndex = 0;
  txWordIndex = 0;
  txBitInWord = 0;
  txClockStep = 0;
  pcwUpdateToggle = !pcwUpdateToggle;
  if (pendingFrameReady)
  {
    activeFrameIndex = pendingFrameIndex;
    pendingFrameReady = false;
  }

  const uint8_t nextFrame = activeFrameIndex;
  txFrameBits = frameBuffers[nextFrame];
  txFrameLength = frameLengths[nextFrame];
  const uint8_t toggleBit = pcwUpdateToggle ? 1 : 0;
  frameBuffers[nextFrame][PCW_UPDATE_TOGGLE_BIT_INDEX] = toggleBit;
  frameBuffers[nextFrame][(PCW_FRAME_WORDS - 1U) * PCW_BITS_PER_WORD +
                           PCW_UPDATE_TOGGLE_BIT_INDEX] = toggleBit;
  beginInterFrameGap();
}

// Main transmitter state machine, one fixed 6us tick per Timer1 compare-match.
// The per-tick path is deliberately flat: direct PORTD operations, no helper
// calls, and interrupt masks only at frame/word transitions.
ISR(TIMER1_COMPA_vect)
{
  if (txFrameLength == 0)
  {
    PCW_LINES_IDLE_FAST();
    PCW_UNMASK_INPUT_IRQS_FAST();
    return;
  }

  switch (txState)
  {
    case TxState::InterFrame:
      PCW_LINES_IDLE_FAST();
      if (!PS2_BUS_IDLE_FAST())
      {
        txPs2ActivityPauseTicks = PCW_PS2_ACTIVITY_PAUSE_TICKS;
      }
      else if (txPs2ActivityPauseTicks > 0)
      {
        --txPs2ActivityPauseTicks;
      }
      if (txInterFrameTicks < PCW_FRAME_GAP_TICKS)
      {
        ++txInterFrameTicks;
      }
      else if (txPs2ActivityPauseTicks == 0)
      {
        PCW_MASK_INPUT_IRQS_FAST();
        PCW_CLK_LOW_FAST();
        DIAG_CLK_LOW_FAST();
        PCW_DATA_LOW_FAST();
        txWordPhaseTick = 0;
        txState = TxState::Preamble;
      }
      break;

    case TxState::Preamble:
      if (txWordPhaseTick == 1 || txWordPhaseTick == 3)
      {
        PCW_DATA_HIGH_FAST();
      }
      else
      {
        PCW_DATA_LOW_FAST();
      }
      ++txWordPhaseTick;
      if (txWordPhaseTick >= PCW_PREAMBLE_TICKS)
      {
        txClockStep = 0;
        txState = TxState::ClockBits;
      }
      break;

    case TxState::ClockBits:
    {
      const bool longLowThisBit = txBitInWord == PCW_WORD_LONG_LOW_BIT;
      const uint8_t bitSlotTicks = longLowThisBit ?
          PCW_CLOCK_LONG_LOW_STEPS_PER_BIT : PCW_CLOCK_STEPS_PER_BIT;

      if (txClockStep == 0)
      {
        PCW_CLK_HIGH_FAST();
        DIAG_CLK_HIGH_FAST();
      }
      else if (txClockStep == PCW_CLOCK_DATA_STEP)
      {
        const uint8_t bitValue = txFrameBits[txBitIndex];
        if (bitValue)
        {
          PCW_DATA_HIGH_FAST();
        }
        else
        {
          PCW_DATA_LOW_FAST();
        }
      }
      else if (txClockStep == PCW_CLOCK_FALL_STEP)
      {
        PCW_CLK_LOW_FAST();
        DIAG_CLK_LOW_FAST();
        ++txBitIndex;
        ++txBitInWord;
        if (txBitInWord >= PCW_BITS_PER_WORD)
        {
          txBitInWord = 0;
          ++txWordIndex;
        }
      }

      ++txClockStep;
      if (txClockStep >= bitSlotTicks)
      {
        txClockStep = 0;
        if (txBitIndex >= txFrameLength)
        {
          txBitIndex = 0;
          txWordIndex = 0;
          txBitInWord = 0;
          txClockStep = 0;
          pcwUpdateToggle = !pcwUpdateToggle;
          if (pendingFrameReady)
          {
            activeFrameIndex = pendingFrameIndex;
            pendingFrameReady = false;
          }

          const uint8_t nextFrame = activeFrameIndex;
          txFrameBits = frameBuffers[nextFrame];
          txFrameLength = frameLengths[nextFrame];
          const uint8_t toggleBit = pcwUpdateToggle ? 1 : 0;
          txFrameBits[PCW_UPDATE_TOGGLE_BIT_INDEX] = toggleBit;
          txFrameBits[(PCW_FRAME_WORDS - 1U) * PCW_BITS_PER_WORD +
                      PCW_UPDATE_TOGGLE_BIT_INDEX] = toggleBit;
          PCW_LINES_IDLE_FAST();
          PCW_UNMASK_INPUT_IRQS_FAST();
          txInterFrameTicks = 0;
          txState = TxState::InterFrame;
        }
        else if (txBitInWord == 0)
        {
          PCW_DATA_LOW_FAST();
          txInterWordGapTicks = 0;
          txState = TxState::InterWordGap;
        }
      }
      break;
    }

    case TxState::InterWordGap:
      PCW_CLK_LOW_FAST();
      DIAG_CLK_LOW_FAST();
      PCW_DATA_LOW_FAST();
      ++txInterWordGapTicks;
      if (txInterWordGapTicks >= PCW_INTER_WORD_GAP_TICKS)
      {
        PCW_DATA_LOW_FAST();
        txWordPhaseTick = 0;
        txState = TxState::Preamble;
      }
      break;

    default:
      txBitIndex = 0;
      txWordIndex = 0;
      txWordPhaseTick = 0;
      txBitInWord = 0;
      txClockStep = 0;
      txInterWordGapTicks = 0;
      txInterFrameTicks = 0;
      txState = TxState::InterFrame;
      PCW_LINES_IDLE_FAST();
      PCW_UNMASK_INPUT_IRQS_FAST();
      break;
  }
}
} // namespace

// Arduino entry point: prints the startup/build banner, configures pins,
// resets PCW state, and (per the compiled DIAG_* mode) starts the PS/2
// reader and/or the PCW transmitter's Timer1 ISR.
void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println(F("PCW keyboard emulator startup"));
  Serial.print(F("Build: "));
  Serial.print(BUILD_DATE);
  Serial.print(F(" "));
  Serial.println(BUILD_TIME);
  Serial.print(F("Active mode: "));
  Serial.println(activeDiagnosticModeName());

  pinMode(PS2_CLK_PIN, INPUT_PULLUP);
  pinMode(PS2_DATA_PIN, INPUT_PULLUP);

  pinMode(PCW_CLK_PIN, OUTPUT);
  pinMode(PCW_DATA_PIN, OUTPUT);
  pinMode(DIAG_CLK_MIRROR_PIN, OUTPUT);
  pcwLinesIdle();

  initializePcwState();

  #if DIAG_PS2_ONLY || DIAG_FULL_EMULATOR
  keyboard.begin(PS2_DATA_PIN, PS2_CLK_PIN);
  // NOTE: left at the keyboard's default typematic (10.9 CPS, 0.5s delay). A
  // faster rate was tried to tighten the keepalive but it flooded the PS/2 bus
  // and worsened transmitter jitter (idle/typing ghosts). KEY_STUCK_RELEASE_MS
  // is sized for the default 0.5s delay.
  #endif

  #if DIAG_PCW_OUTPUT_ONLY || DIAG_FULL_EMULATOR
  sendPcwFrame();
  setupTimer1();
  #endif
}

// Arduino main loop. Behaviour depends on the compiled DIAG_* mode:
// DIAG_PCW_OUTPUT_ONLY just keeps the transmitter fed and prints a heartbeat;
// DIAG_PS2_ONLY dumps raw PS/2 events to serial; DIAG_FULL_EMULATOR drains
// PS/2 events into PCW state and (re)builds the transmit frame.
void loop()
{
#if DIAG_PCW_OUTPUT_ONLY
  static uint32_t lastHeartbeatMs = 0;
  sendPcwFrame();
  if (millis() - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS)
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

  releaseStuckKeys();  // recover any held key whose PS/2 break byte was dropped
  sendPcwFrame();
  #endif
}
