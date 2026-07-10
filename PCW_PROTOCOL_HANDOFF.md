# PCW Keyboard Emulator Protocol Handoff

## Project Context

- Active firmware: `D:\PCW\Projects\AmstradPCWKeyboard\PCW8256_PS2_Keyboard_Emulator\PCW8256_PS2_Keyboard_Emulator.ino`
- Target: Arduino Nano emulating the Amstrad PCW keyboard controller.
- Purpose: Read PS/2 keyboard events, maintain a PCW keyboard matrix, and repeatedly transmit complete PCW keyboard state frames over the PCW keyboard `CLK` and `DATA` lines.
- Electrical model: PCW `CLK` and `DATA` are treated as open-collector style lines. The Arduino releases lines high using `INPUT_PULLUP` and actively drives low only.

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

- Normal `CLK` high/setup: about `12us`
- Normal `CLK` low: about `21us`
- Fourth `CLK` pulse of each 12-bit word: about `48us` low
- Inter-frame idle gap: about `6.25ms`
- `DATA` should be stable before the falling edge / low phase of `CLK`.
- `DATA` should not transition at the same instant as `CLK` falls.

Current intended phase sequence per normal bit:

1. `CLK` is high/released.
2. Wait `4us` recovery after `CLK` rises.
3. Set/release `DATA` for the next bit.
4. Wait `8us` data setup.
5. Pull `CLK` low.
6. Hold `CLK` low for `21us`, or `48us` if this is the 4th pulse of a 12-bit word.
7. Release `CLK` high.

## Current Firmware Implementation

Implemented so far:

- Added compile-time serial build banner using `__DATE__` and `__TIME__`.
- Switched PCW `CLK` and `DATA` handling from push-pull output to open-collector simulation.
- Reworked Timer1 from a fast tick-counter ISR to edge-scheduled compare intervals.
- Added explicit `6.25ms` inter-frame gap.
- Fixed large random frame delays by resetting `TCNT1` before setting `OCR1A` in `scheduleTimer1Us()`.
- Fixed a clock/data glitch by separating `CLK` rising edge from `DATA` transition:
  - `CLK` rises
  - wait `4us`
  - update `DATA`
  - wait `8us`
  - pull `CLK` low

Important current timing constants:

- `PCW_CLOCK_HIGH_US = 12`
- `PCW_CLOCK_RECOVERY_US = 4`
- `PCW_DATA_SETUP_US = 8`
- `PCW_CLOCK_LOW_US = 21`
- `PCW_FOURTH_CLOCK_LOW_US = 48`
- `PCW_FRAME_GAP_US = 6250`

## Current Observed Behaviour

- Frame gap now looks correct at about `6.25ms`.
- Clock/data glitch appears fixed after separating `DATA` transition from `CLK` rising edge.
- The intended longer 4th `CLK` low pulse is still not visible in the latest analyser trace.

## Current Suspect

The code currently attempts to latch the 4th-pulse low duration using `txBitInWord == 3`, but the hardware trace still shows normal low pulses. The next debugging step should verify whether the firmware believes it is scheduling the fourth pulse.

Recommended next debug step:

- Add a temporary diagnostic output on a spare Arduino pin.
- Toggle/assert that pin only when the firmware schedules `PCW_FOURTH_CLOCK_LOW_US`.
- Capture diagnostic pin, `PCW_CLK`, and `PCW_DATA` together.
- If the diagnostic pin asserts but `CLK` low is not long, the issue is timer scheduling.
- If the diagnostic pin never asserts, the issue is bit/word position tracking.

## Important Code Areas

- Timing constants:
  - `D:\PCW\Projects\AmstradPCWKeyboard\PCW8256_PS2_Keyboard_Emulator\PCW8256_PS2_Keyboard_Emulator.ino`
- Open-collector pin helpers:
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

- Latest firmware compile passed after the current timing work.
- Latest reported compile size:
  - Flash: about `6830 bytes`
  - SRAM: about `870 bytes`

