# PCW Keyboard Emulator — Convention Sweep Test (Handoff)

Context for a **new session** to build an auto-sweep test that finds the correct
electrical signalling convention for driving a real Amstrad PCW8256 from the
Arduino keyboard emulator. Everything needed to continue is here.

---

## 1. The situation in one paragraph

The emulator's transmitter is **proven correct on a logic analyser** — it emits
a structurally valid 17-word PCW keyboard frame, and a decode of the captured
DATA vs clock recovers every byte (including a pressed `A` key → byte 0x08 bit 5
= 0x20). But on the **real PCW** it produces garbage. The PCW's keyboard receive
path is alive (our transmission visibly changes what the PCW displays), so this
is a **signalling-convention mismatch**, not a dead machine or a framing bug. We
have tried several conventions by hand without success. The goal now is a
firmware **auto-sweep** that cycles through the convention combinations so the
real machine tells us which one it accepts.

There is **no genuine PCW keyboard available** to use as a reference (they are
rare), and **KEYTEST.COM cannot be launched** on this machine (see §7), so the
only feedback channel is the PCW screen itself.

## 2. Success oracle (how we know when a convention is right)

**If the frames decode correctly, the PCW reads all-zero keyboard state (no keys)
and sits QUIETLY at the `A>` prompt with no characters appearing.** So:

- **Screen goes quiet at `A>`** = correct convention. ✅
- **Screen floods with garbage characters** = wrong convention.

(The machine's own baseline with NO keyboard attached is a burst then endless
`z` — that is the PCW holding stale keyboard memory with no valid frames, and is
NOT our signal. Do not mistake it for a result; the test is whether *active,
correct* transmission makes it go quiet.)

## 3. Hardware / wiring

- Arduino **Nano, ATmega328P, 16 MHz**.
- `D3` → PCW **CLK**, `D5` → PCW **DATA**, push-pull outputs, each with an
  external **10k pull-up to +5V** and a **470Ω** series resistor into the PCW
  connector. Common ground.
- `D6` = diagnostic CLK mirror (analyser only; not wired to the PCW).
- `D2`/`D4` = PS/2 keyboard CLK/DATA.
- Connector pinout / resistor detail: see `images/PCW-connector.png`,
  `images/PCW keyboard connector resistors.png`, `images/schematic.png`.

## 4. Protocol reference (from John Elliott + James Ols; see README §10)

- Frame = **17 words**, each **12 bits, MSB first**. Bits 11..8 = memory-map
  offset (0x0–0xF); bits 7..0 = the value for that offset.
- Word order: word 0 = offset 0xF with **transmit flag (bit 7) = 1**; words
  1–15 = offsets 0x0–0xE; word 16 = offset 0xF with transmit flag = 0. Bit 6 of
  offset 0xF is an **update-toggle** that flips every frame.
- Idle offset-0xD value = 0x80 (LK1 not fitted); all key bytes 0x00 when no keys.
- Clock: ~**12µs high, ~21µs low** per bit; **~48µs low after the 4th pulse** of
  each word (a longer LOW *between* pulses — **all 12 pulses are still sent**,
  it is NOT a missing pulse). DATA latched on the CLK **falling edge**.
- Real keyboard rests both lines **high** between frames (motherboard pull-ups);
  drives them (low-biased) only while sending a frame. Inter-frame gap ~6.25ms.
- James notes his captured clock "is inverted" — so clock polarity is genuinely
  ambiguous and is a prime sweep variable.
- DATA polarity is **not documented**. John Elliott: "no keyboard present → all
  16 bytes zero, lines high," which implies a high DATA line reads as **bit 0**
  (so a `1` bit is driven low) — but this must be confirmed empirically.

## 5. What is already confirmed (on the logic analyser, not the PCW)

- Clean 12µs/21µs clock, 6.25ms frame gap, no jitter (Timer1 CTC scheduler).
- Full frame decodes: offset nibbles run `F,0,1,…,E,F`, transmit + toggle bits
  behave, link byte 0xD = 0x80, and `A` sets offset 0x08 bit 5 = 0x20.
- So the frame *content* and *timing* are correct; only the PCW-facing electrical
  convention is unresolved.

## 6. Conventions tried on the real PCW so far (all → garbage)

| idle level | data sense | clock | word structure | result |
|---|---|---|---|---|
| low | normal | normal | skip (11 pulses) | random changing garbage |
| high | normal | normal | skip (11 pulses) | repeating flood + beeps |
| high | inverted | normal | skip (11 pulses) | structured bursts |
| high | inverted | normal | uniform (12 pulses, no gap) | garbage, faster |
| high | inverted | normal | **48µs gap (12 pulses)** | **garbage — but see below** |

**Important result for the 48µs-gap build:** a logic-analyser capture
(`PCW7-boot.vcd`) confirms the emitted frame is **completely correct** — every
frame has exactly 204 DIAG rising edges (12×17), the DATA-inverted decode gives
a valid all-zeros "no keys" frame with the right `F,0,1,…,E,F` offset sequence,
link byte 0xD = 0x80, and correct flag/toggle bits, and the waveform matches
James's 12-pulse + ~48µs-gap timing. So **we have now matched every documented,
measurable aspect of the protocol and the PCW still garbages.**

**Two untested dimensions remain — both belong in the sweep:**

1. **Clock polarity (`PCW_INVERT_CLK=1`)** — never tried. NOTE: flipping it also
   flips the idle level (idle uses `pcwClockHigh`), so pair it with the idle
   flag so idle stays physically high, or the two interact.
2. **DATA double-toggle before each word.** John Elliott: *"the keyboard toggles
   the DATA line twice before sending each word, but the gate array doesn't seem
   to need this."* We have NEVER sent this. It is the one documented protocol
   feature still missing and a strong candidate for the per-word sync this gate
   array may actually require. Add it as a sweep option (off / two DATA toggles
   inserted before each 12-bit word).

## 7. Why KEYTEST.COM can't be used here

`CPM_14_keyboard.dsk` auto-runs `KEYTEST.COM` via `PROFILE.SUB` (which is just
the line `KEYTEST`). KEYTEST maps block 3 and displays the 16 state bytes
(3FF0h–3FFFh) as hex + binary, refreshing continuously. BUT the PCW phantoms
keypresses even at idle, which **aborts the SUBMIT before KEYTEST launches**.
"Hold the lines idle at startup" does not help, because idle/unclocked lines do
not quiet this PCW. So the screen-quiet oracle in §2 is the practical feedback,
not KEYTEST.

## 8. THE TASK: build an auto-sweep test firmware

Make the four convention settings **runtime** (not compile-time) and cycle
through their combinations automatically, so the real PCW reveals the right one.

**Settings to sweep** (currently compile-time `#define`s in the firmware):

1. `PCW_INVERT_CLK` — physical clock polarity (0/1). **Highest priority; never
   tested.** Pair with idle level so idle stays physically high.
2. `PCW_INVERT_DATA` — a `1` bit driven low (1) vs high (0).
3. `PCW_IDLE_HIGH` — lines rest high (1) vs low (0) between frames.
4. Word structure — uniform 21µs lows vs a 48µs long low after the 4th pulse
   (the `DIAG_DISABLE_WORD_SKIP` mechanism, now reworked to keep all 12 pulses).
5. **DATA double-toggle before each word** (off / on). Never tried; documented
   but assumed unnecessary — a prime candidate for this gate array's word sync.
   With the 48µs-gap build's frame proven correct on the analyser yet still
   rejected, this and clock polarity are the most likely missing pieces.

**Suggested sweep implementation:**

- Convert the four `#define`s into `volatile` runtime flags read by the pin
  helpers (`pcwClockHigh/Low`, `diagClockHigh/Low`, `pcwLinesIdle`), by
  `loadCurrentPcwData`, and by the `LowerClock` long-low decision in
  `ISR(TIMER1_COMPA_vect)`. (They are currently `#if`-gated; change to
  `if (flag)`. The extra runtime branch in the ISR is negligible.)
- A sweep driver in `loop()` advances a combo index every **~12s** (long enough
  for the PCW to settle and show quiet-vs-garbage), updating the flags at a
  frame boundary, and prints the combo to **USB serial @ 115200**, e.g.
  `Combo 5/16: INVERT_CLK=1 INVERT_DATA=1 IDLE_HIGH=1 LONG_LOW=1`.
- Operator watches the PCW screen next to the Arduino serial monitor; when the
  PCW **goes quiet**, note the combo number/line. That is the answer.
- Start with the most likely-relevant order: fix `IDLE_HIGH=1` and `LONG_LOW=1`
  (best supported by the docs) and sweep `INVERT_CLK` × `INVERT_DATA` first (4
  combos); if none quiet, expand to all 16.

**Then** bake the winning combo back into the main firmware as the permanent
convention, set `DIAG_FULL_EMULATOR`, and verify: quiet at `A>`, and pressing a
known key (e.g. `A` → offset 0x08 bit 5) types the right character.

## 9. Current firmware flag state (starting point)

In `PCW8256_PS2_Keyboard_Emulator/PCW8256_PS2_Keyboard_Emulator.ino`:

- `DIAG_FULL_EMULATOR 1` (reads PS/2, builds/sends frames)
- `DIAG_DISABLE_TIMER0_IRQ 0` (keep millis() for the PS/2 library)
- `PCW_INVERT_CLK 0`
- `PCW_IDLE_HIGH 1`
- `PCW_INVERT_DATA 1`
- `DIAG_DISABLE_WORD_SKIP 0` (all 12 pulses + 48µs long low after bit 3)
- `STARTUP_IDLE_MS 0`

Other diagnostics available: `DIAG_PS2_ONLY` (dump decoded PS/2 keys to serial),
`DIAG_PCW_OUTPUT_ONLY`, `DIAG_CLK_TIMING_TEST`, `DIAG_FORCE_DATA_LOW`, and the
`DIAG_CLK_MIRROR_PIN` (D6) analyser reference.

## 10. Key files

- Firmware: `PCW8256_PS2_Keyboard_Emulator/PCW8256_PS2_Keyboard_Emulator.ino`
- Protocol/timing notes: `README.md` (§10) and `PCW_PROTOCOL_HANDOFF.md`
- KEYTEST source: `keytest.asm`; disk `CPM_14_keyboard.dsk`; auto-run `PROFILE.SUB`
- Reference images in `images/` (connector, resistors, schematic, matrices,
  logic-analyser frame captures, James's timing analysis)
- Git: branch `AmstradPCW`, remote `origin` (github.com/nistunedev/AmstradPCWKeyboard).
  Note: this machine needs `git config --global http.sslBackend schannel` (Avast
  intercepts TLS) — already set.

## 11. Gotchas

- Analyser decode reads the PHYSICAL DATA level, so with `PCW_INVERT_DATA=1` the
  captured bytes look inverted vs the logical frame — that is expected; the PCW
  un-inverts.
- The build has no CMake/arduino-cli here; compile in the Arduino IDE.
  Brace-balance is a quick sanity check (`grep -o '{' | wc -l` vs `}`).
- Changing convention flags mid-transmission can glitch one frame; prefer to
  switch at a frame boundary in the sweep driver.
