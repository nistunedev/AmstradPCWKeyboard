# PCW Keyboard Emulator Protocol Handoff

## Project Context

- Active firmware: `D:\PCW\projects\AmstradPCWKeyboard\PCW8256_PS2_Keyboard_Emulator\PCW8256_PS2_Keyboard_Emulator.ino`
- Target: Arduino Nano emulating the Amstrad PCW keyboard controller.
- Purpose: Read PS/2 keyboard events, maintain a PCW keyboard matrix, and repeatedly transmit complete PCW keyboard state frames over the PCW keyboard `CLK` and `DATA` lines.
- Electrical model: PCW `CLK` and `DATA` are driven push-pull as `OUTPUT` pins (D3/D5), backed by external 10k pull-ups to +5V. The PCW motherboard also pulls the lines high when disconnected/undriven, but a real attached keyboard actively drives both lines low most of the time, including the gaps between frame bursts.

## Intended Wire Protocol

- The PCW keyboard does not send PS/2-style scancodes.
- It repeatedly sends complete keyboard state packets.
- Each packet is 17 words.
- Each word is 12 bits, MSB first:
  - Bits `11..8`: memory-map offset `0x0..0xF`
  - Bits `7..0`: byte value for that offset

Packet order:

1. Offset `0x0F` with transmit/status bit set.
2. Offsets `0x00` through `0x0E`.
3. Offset `0x0F` with transmit/status bit clear.

### Per-word DATA double-toggle - implemented in current test build

John Elliott's protocol notes (see README section 10) state:

> "The PCW keyboard toggles the DATA line twice before sending each word, but the gate array at the PCW end doesn't seem to need this."

The original keyboard emits **two DATA pulses as a preamble before each 12-bit
word**. This is now implemented as two full DATA-only pulses before every word,
with CLK held low throughout the preamble. Each pulse edge is held for
`PCW_DATA_TOGGLE_US`.

## Matrix State Intention

- Idle key matrix bytes are `0x00`.
- Pressed keys set bits.
- Released keys clear bits.
- Initial status bytes follow observed idle state:
  - Offset `0x0D = 0x80`
  - Offset `0x0E = 0x00`
  - Offset `0x0F = 0xC0`
- Link state assumptions:
  - `LK1` not fitted
  - `LK2` not fitted
  - `LK3` not fitted

## Expected Timing Targets

Observed/target timing from real PCW keyboard traces:

- Normal `CLK` high: about `12us`
- Normal `CLK` low: about `21us`
- Once per 12-bit word, the 5th bit's high pulse is skipped entirely (no `CLK` edge), merging that bit's low period into the previous one. Combined low is about `42-45us`, consistent with the `~48us` extended low observed on the real keyboard.
- Inter-frame idle gap: about `6.25ms`.
- Inter-frame/default driven level from an attached keyboard: `CLK` low and `DATA` low. This differs from the disconnected motherboard fail-safe level, which is pulled high.
- Before each 12-bit word: two full DATA-only pulses, with CLK held low.
- `DATA` latched by the PCW on the falling edge of `CLK`; the firmware sets each bit's `DATA` during the preceding low period, giving ample setup time.

### Polarity / inversion

`PCW_INVERT_CLK` (default `0`) selects physical CLK polarity; `0` makes the measured waveform read `12us` high / `21us` low like the real keyboard on the current rig. **Confirm against real hardware with `KEYTEST.COM`** — if keys misread, flip `PCW_INVERT_CLK`.

Two-phase bit sequence (each phase toggles `CLK` as its first action; intervals timed off the Timer1 CTC compare match so pulse widths don't carry ISR work):

1. `RaiseClock`: raise the D6 mirror; raise the real CLK (D3) too **unless** this is the skip bit; hold `PCW_CLOCK_HIGH_US` (~12us).
2. `LowerClock`: drive both CLKs low (PCW latches this bit's `DATA` on D3's edge), advance to the next bit and load its `DATA`, hold `PCW_CLOCK_LOW_US` (~21us).

The skip bit omits **D3's** rising edge only, so D3 stays low across it (extended low ~54us: 21 + 12 + 21). The **D6 mirror pulses on every bit** as a uniform no-skip reference for scope comparison. Extended-low duration is ~54us vs the real keyboard's ~48us — tune if `KEYTEST` shows the PCW cares.

## Current Firmware Implementation

Implemented so far:

- Added compile-time serial build banner using `__DATE__` and `__TIME__`.
- PCW `CLK`/`DATA` are push-pull `OUTPUT` pins (D3/D5) backed by external 10k pull-ups; briefly went through an open-collector (`INPUT_PULLUP` + drive-low) phase before switching back once the pull-ups made that unnecessary.
- Reworked Timer1 from a fast tick-counter ISR to edge-scheduled compare intervals.
- Added explicit `6.25ms` inter-frame gap.
- Added the two DATA-only pre-word pulses documented for the original 8048 keyboard.
- Fixed large random frame delays by resetting `TCNT1` before setting `OCR1A` in `scheduleTimer1Us()`.
- Fixed a clock/data glitch by separating `CLK` rising edge from `DATA` transition:
  - `CLK` rises
  - wait `4us`
  - update `DATA`
  - wait `8us`
  - pull `CLK` low

Important current timing constants:

- `PCW_CLOCK_HIGH_US = 12`
- `PCW_CLOCK_LOW_US = 21`
- `PCW_WORD_SKIP_BIT_INDEX = 4` (0-indexed 5th bit of each word; its high pulse is skipped)
- `PCW_FRAME_GAP_US = 6250`
- `PCW_IDLE_HIGH = 0` (drive CLK/DATA low between frames, matching the real keyboard trace)
- `PCW_DATA_TOGGLE_US = 12` (hold time for each DATA-only pre-word pulse edge)
- `PCW_INVERT_DATA = 0` (natural DATA polarity through the double-inverted PCW input path)
- `PCW_INVERT_CLK = 1` (drive physical CLK inverted; see Polarity note above)

## Current Observed Behaviour / open items

- Frame gap looks correct at about `6.25ms`.
- Root cause of the persistent width jitter identified: the old ISR used 3 entries per bit and toggled the pin *after* counter work, so every pulse carried variable ISR/interrupt latency (Timer0's `millis()` tick every ~1.024ms being the always-on offender). Rewritten to the two-phase toggle-first ISR below; `DIAG_DISABLE_TIMER0_IRQ` added to remove the `millis()` tick during transmission.
- CLK polarity was inverted vs the real keyboard; addressed with `PCW_INVERT_CLK` (verify with `KEYTEST.COM`).

## State Machine

Firmware models the transmitter as `TxPhase` (`FrameGap`, `RaiseClock`, `LowerClock`):

- `FrameGap`: lines held idle. Used before the first frame and during the inter-frame gap. Its timer firing starts a fresh bit via `startCurrentPcwBit()` (sets DATA, raises CLK).
- `RaiseClock`: drives `CLK` high (first action), holds `PCW_CLOCK_HIGH_US`, → `LowerClock`.
- `LowerClock`: drives `CLK` low (first action — PCW latches this bit's DATA), advances bit/word counters, ends the frame if complete, else loads the next bit's DATA and holds `PCW_CLOCK_LOW_US`. If the upcoming bit is `PCW_WORD_SKIP_BIT_INDEX`, stays in `LowerClock` (omits its rising edge) to merge the two lows.

## Diagnostic build flags

Set exactly one run mode (`DIAG_PCW_OUTPUT_ONLY` / `DIAG_PS2_ONLY` / `DIAG_FULL_EMULATOR`), OR `DIAG_CLK_TIMING_TEST` alone (standalone busy-loop bypass; a compile guard now enforces this). Modifiers: `DIAG_DISABLE_WORD_SKIP` (plain square wave, no merge), `DIAG_FORCE_DATA_LOW` (freeze DATA), `DIAG_DISABLE_TIMER0_IRQ` (kill `millis()` tick), `PCW_INVERT_CLK` (physical CLK polarity).

## Important Code Areas

- Timing constants:
  - `D:\PCW\projects\AmstradPCWKeyboard\PCW8256_PS2_Keyboard_Emulator\PCW8256_PS2_Keyboard_Emulator.ino`
- Push-pull pin helpers:
  - `pcwClockHigh()`
  - `pcwClockLow()`
  - `pcwDataHigh()`
  - `pcwDataLow()`
- Timer scheduler:
  - `scheduleTimer1Us()`
- Bit/data setup helpers:
  - `loadCurrentPcwData()`
  - `startCurrentPcwBit()`
- Main transmitter ISR:
  - `ISR(TIMER1_COMPA_vect)`

## Validation State

- **Bench/analyser: fully confirmed.** Consistent 204-bit frames, correct
  content (all-zero "no keys" idle, `F,0,1,…,E,F` offsets, link 0xD=0x80,
  flag/toggle bits), ~12us high / ~21us low, ~48us gap after the 4th pulse, no
  jitter. The emitted frame is provably correct.
- **Real PCW: not yet working.** The machine still shows garbage even though the
  frame is provably correct, so this is a signalling-convention mismatch, not a
  frame bug. `KEYTEST.COM` cannot be used as a check here — the PCW phantoms
  keypresses at idle, which aborts `PROFILE.SUB` before KEYTEST launches. The
  practical success oracle is instead **"PCW sits quietly at `A>`"** = correct.
- **Remaining convention to verify**:
  - **Clock polarity** `PCW_INVERT_CLK` (couples with idle level).

