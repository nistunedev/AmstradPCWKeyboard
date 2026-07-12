# PCW Transmitter — Tick-Based State-Machine Rewrite (Handoff)

Context for a **new session** to rewrite the PCW keyboard-transmitter firmware
as a clean, uniform-tick state machine. Everything needed to continue is here.

## Where we are (committed baseline)

- Repo: `D:\PCW\projects\AmstradPCWKeyboard`, branch `AmstradPCW`.
- Commit **`06a0922`** — "Checkpoint: clean clock/data timing baseline before
  state-machine rewrite." (local; not yet pushed.)
- Firmware: `PCW8256_PS2_Keyboard_Emulator/PCW8256_PS2_Keyboard_Emulator.ino`
- At this checkpoint the per-word DATA preamble is **disabled**
  (`DIAG_DISABLE_WORD_PREAMBLE 1`), so the ISR no longer busy-waits
  (`delayMicroseconds`) inside the handler. Output is the 2-phase clock
  (12µs high / 21µs low, 48µs after bit 3) with DATA loaded at the rising edge.
- Accepted at this checkpoint: PCW won't word-align (preamble off) and PS/2 is
  unreliable — both to be fixed by this rewrite.

## Why we are rewriting

The old ISR mixed variable timer intervals and `delayMicroseconds` busy-waits
to place 6µs preamble edges. That (a) held interrupts off ~24µs per word
(dropping PS/2 bytes) and (b) was hard to reason about. Root problem: on one
16 MHz AVR the tight 6µs PCW preamble and interrupt-driven PS/2 reception fight
over interrupt timing. **Decision: keep both on one AVR, prioritise a clean,
deterministic PCW waveform, and accept imperfect PS/2** (poll PS/2 only in safe
windows / accept missed or repeated keys).

## Target design (from the user's spec + James Ols' capture)

Reference: James Ols keyboard-timing capture (README §10 links / `images/`),
John Elliott's PCW keyboard notes.

### One uniform tick
- Timer1 CTC, fixed **6µs** compare interval (auto-reload; set `OCR1A` once).
- ISR does only: advance the state machine, toggle CLK/DATA pins. No scheduling
  math, no busy-waits. Keep it < ~3µs.
- A hierarchical counter set decides what each tick does:
  frame state → word index → within-word phase → clock sub-phase.

### Frame state
- `INTER_FRAME`: CLK low, DATA low, for 6.25ms (= 1040 ticks). (Optimisation
  option: use one long CTC interval here instead of 1040 ticks.)
- `CURRENT_FRAME`: iterate the words. **Reconcile the word count**: our proven
  protocol is 17 transmitted words — `0xF` (transmit bit set), `0x00…0x0E`,
  `0xF` (transmit bit clear). The user's draft says 13 (overview) / D0–D15
  (16); confirm against the ADR and emit 17.

### Word phases (per word, all pre-clock ticks are 6µs)
Preamble (2 DATA pulses then a low tail — user's draft, grid-quantised):
1. DATA low
2. DATA high
3. DATA low
4. DATA high
5. DATA low
6. DATA low
7. DATA low
8. DATA low
Then run the clock sub-state machine for each of the 12 bits (below), advancing
the bit index until the word is sent. **Verify** the optional inter-word gap
(user draft: 144µs = 24 ticks) against James's capture before including it —
current working frames do not have it.

NOTE vs earlier scope reading: the measured preamble was high/low/high/low
ending low + ~30µs tail; the draft is a low-first, 24µs-tail grid version. Close
but confirm on the analyser.

### Clock sub-phase (per bit, 6 ticks = 36µs)
1. CLK high
2. CLK high — put this bit's DATA on the bus (transition at mid-high, matching
   James: data valid in the middle of the clock)
3. CLK low — falling edge latches DATA into the gate array
4. CLK low
5. CLK low
6. CLK low
→ next bit
Special case: **the 4th clock pulse has no 12µs gap** — after the 4th bit
(bit index 3) extend the low (48µs = 8 low ticks instead of 4). Add an explicit
branch keyed on the within-word bit index.

## Open items to resolve while coding

1. Word count: emit 17 words (flag/offset/flag), not 13/16. Confirm vs ADR.
2. PS/2 strategy: mask the PS/2 pin interrupt during active transmit and poll
   the library only in `INTER_FRAME`; or accept unreliable input. A 6µs tick is
   ~50% CPU, so PS/2 cannot run un-jittered during transmit on this MCU.
3. 144µs inter-word gap: keep only if James's trace shows it.
4. 4th-pulse long low: explicit bit-index-3 branch.
5. Clock low 21µs → 24µs on the 6µs grid: confirm gate-array tolerance.
6. Inter-frame gap: 1040 ticks vs one long interval.

## Suggested execution phases

- **Phase 1 — scaffolding:** define the frame/word/clock enums and counters,
  the 6µs CTC setup, and a lean tick ISR that only advances state + toggles
  pins. Verify pure timing on the scope with a static/idle frame (no PS/2).
- **Phase 2 — waveform:** implement preamble, per-bit clock sub-phase, mid-clock
  DATA, and the 4th-pulse long low. Confirm on the analyser that one frame
  decodes: offsets `F,0,1,…,E,F`, DATA mid-clock, 48µs after the 4th pulse.
- **Phase 3 — PCW decode:** test on the real PCW with `KEYTEST.COM`; expect
  clean idle (all `00` except `3FFD=80`, `3FFF=C0`).
- **Phase 4 — key path:** reuse the existing PS/2 → matrix → frame-build code
  (unchanged from the current file: `updatePcwState`, `applyKeyState`, the
  keymap, the double-buffered `frameBuffers`). Feed the built 17-word frame into
  the new sequencer.
- **Phase 5 — PS/2 policy:** mask PS/2 during transmit + poll in the gap, and
  keep the dropped-break keepalive (`releaseStuckKeys`) as a safety net. Only if
  input is still unacceptable, revisit the 2-MCU split.

## Reusable pieces from the current firmware (keep)

- PS/2 decode + matrix: `readKeyboardEvent`, `updatePcwState`, `applyKeyState`,
  `kKeyMap`, `findKeyMapEntry`, `isPs2Held`/`setPs2Held`, `holdCount`.
- Frame build + double buffer: `sendPcwFrame`, `beginFrameBuild`, `sendPcwWord`,
  `appendBit`, `frameBuffers[2]`, `frameLengths[2]`, the pending-frame swap.
- Dropped-break keepalive: `releaseStuckKeys`, `keyLastMakeMs`,
  `clearHeldForKey`, `isNonRepeatingModifier`, `KEY_STUCK_RELEASE_MS`.
- Pin helpers + `PCW_INVERT_CLK` (=0, verified correct), `PCW_INVERT_DATA` (=0).

## Replace (do not carry over)

- `ISR(TIMER1_COMPA_vect)` variable-interval + `delayMicroseconds` preamble.
- `scheduleTimer1Us` per-phase scheduling model.
- `TxPhase` enum (FrameGap/PreambleBurst/PreambleTail/ClockRise/ClockFall) and
  `delayMicrosecondsAllowInterrupts` (unused after rewrite).

## Key facts / gotchas

- ATmega328P @16MHz, Timer1 /8 → 2 counts/µs, so 6µs = 12 counts (OCR1A = 11).
- Frame = 17 words × 12 bits, MSB first, bits 11..8 = offset, 7..0 = value.
- Idle correct read (KEYTEST): all `00`, `3FFD=0x80` (LK2 absent), `3FFF` cycles
  `00/40/C0/80` (bit7 transmit, bit6 toggle).
- Build: Arduino IDE (no arduino-cli here). Watch RAM (`frameBuffers` = 408 B
  plus the keepalive arrays).
- Disk/KEYTEST tooling: see `PCW_PROTOCOL_HANDOFF.md`, `idsk_issues.md`.
