# Amstrad PCW8256 PS/2 Keyboard Emulator

This project is an in-development keyboard emulator for the Amstrad PCW8256.
An Arduino Nano reads make and break events from a PS/2 keyboard, maintains a
representation of the PCW keyboard matrix, and simulates the original Amstrad
PCW keyboard.

The Arduino continuously transmits PCW keyboard matrix/state frames to the
PCW8256 through the original keyboard clock and data interface. The target
machine for this firmware is the Amstrad PCW8256.

![Amstrad PCW keyboard](images/pcw_keyboard.gif)

The active firmware is:

`PCW8256_PS2_Keyboard_Emulator/PCW8256_PS2_Keyboard_Emulator.ino`

The firmware uses the
[PS2KeyAdvanced](https://github.com/techpaul/PS2KeyAdvanced) Arduino library.

## Hardware

The current firmware targets an Arduino Nano with an ATmega328P running at
16 MHz. The pin assignments are documented at the top of the active firmware
source.

Verify connector pinout, voltage, current capacity, and common ground before
connecting the Arduino, PS/2 keyboard, or PCW8256. The emulator is still in
development and should be tested carefully.

PS/2 keyboard socket (female, front view):

![PS/2 female connector](images/PS2-female.png)

## Testing

`KEYTEST.COM` is included as a CP/M diagnostic utility for PCW-side testing.
It displays the keyboard state bytes visible to the PCW8256 at memory addresses
`BFF0h-BFFFh`. This helps verify whether the PCW is receiving keyboard state
updates from the emulator.

The repository includes `CPM_14_keyboard.dsk`, a CP/M disk image containing the
diagnostic utility. The diagnostic can also be rebuilt and inserted by running
`build_keytest.bat` on Windows.

## Development Status

The Arduino Nano firmware has now been validated on a real Amstrad PCW8256 as a working PS/2-to-PCW keyboard emulator. The current build uses a Timer1-driven PCW transmitter state machine and PS/2 host-inhibit handling so the PS/2 keyboard is only allowed to talk while the PCW frame transmitter is in a safe inter-frame window.

Confirmed working on hardware:

- **PCW framing** - the PCW receives stable keyboard-state frames with no idle keyboard garbage.
- **PS/2 key path** - PS/2 make/break events are decoded, mapped into the PCW matrix, transmitted, and released correctly with immediate response.
- **PS/2 host inhibit** - while the Arduino is actively transmitting a PCW frame, it holds the PS/2 clock line low so the PS/2 keyboard waits instead of sending bits that would be missed while the PCW timing ISR is protected.
- **Inter-frame PS/2 service window** - PCW frame output can pause briefly between frames while PS/2 activity completes; the PCW keeps its most recent keyboard state in memory, so this is preferable to corrupting either protocol.
- **Matrix polarity** - idle matrix bytes are `00`, and pressed keys set bits. The link/status bytes match the observed idle state for the current link configuration.

The PCW transmitter deliberately follows the behaviours the PCW gate array actually needs rather than trying to reproduce every analyser measurement exactly. The important working features are the word structure, DATA/CLK relationship, two DATA preamble pulses before each word, the longer low gap after the fourth clock pulse, and clean inter-frame idle. The exact pulse widths have some tolerance on real hardware.

Diagnostic build flags at the top of the firmware now select only the run mode:

- `DIAG_FULL_EMULATOR` - normal PS/2-to-PCW operation.
- `DIAG_PS2_ONLY` - dumps decoded PS/2 events to serial.
- `DIAG_PCW_OUTPUT_ONLY` - transmits the PCW frame stream without PS/2 input.

A D6 diagnostic pin mirrors the PCW clock output for scope/analyser comparison.

### Current protocol notes

- The PCW receives 17 12-bit words per frame.
- Words are sent MSB first.
- Word order is `0Fh` with transmit bit set, offsets `00h` through `0Eh`, then `0Fh` with transmit bit clear.
- Between frames, an attached keyboard/emulator drives both `CLK` and `DATA` low.
- Before each 12-bit word, the keyboard/emulator emits two DATA-only high pulses while CLK remains low. These pulses are necessary for reliable real-PCW behaviour in this implementation.
- Each data bit is presented so DATA changes during the clock-high phase and is stable by the clock edge used by the PCW input logic.
- The low period after the fourth clock pulse in each word is longer than the normal bit gap and acts as a per-word timing marker.
- The measured real-keyboard traces are a guide, but the PCW hardware accepts small timing differences; exact logic-analyser pulse widths did not need to be matched perfectly once the framing markers and DATA timing were correct.

Logic-analyser capture of one frame (WORD0 highlighted) and the start-of-frame detail used during protocol investigation:

![Frame with WORD0 highlighted](images/PCW%20James%20Logic%20frame1%20highlighted.png)

![Start-of-frame zoom](images/PCW%20James%20Logic%20frame1%20zoomed.png)

### Open items

- More PS/2 keyboards should be tested to confirm host-inhibit behaviour across different keyboard controllers.
- The current protocol is validated for the PCW8256 test machine; PCW variants should be checked before claiming universal compatibility.

## Tools

- `pasmo.exe` is included for Windows builds of `KEYTEST.COM`.
- `iDSK.exe` version 0.20 is included for updating the CP/M disk image.

## Credits

- Original fork base: [somhi/AmstradXTPS2](https://github.com/somhi/AmstradXTPS2)
- [Pasmo Z80 assembler](https://pasmo.speccy.org/)
- [iDSK 0.20](https://github.com/cpcsdk/idsk)

## Notes from John Elliots PCW Hardware Guide
PDF:
https://www.seasip.info/Unix/Joyce/hardware.pdf
HTML:
https://www.seasip.info/Unix/Joyce/pcwkbd.html

Revisiions by James Ols
https://hackaday.io/project/27549-the-pcw-project/log/70757-keyboard-timings

# Hardware

Both keyboards use the same controller: an 8048, part number 40027.

# PCW Keyboard

Keyboard matrix from the PCW9512 service manual:

<img src="images/PCW%20keyboard%20matrix%20-%20service%20manual.png" alt="PCW keyboard matrix (service manual)" height="748">

The keyboard appears as a memory-mapped device at 3FF0h–3FFFh in memory block 3.

Using the key numbering scheme in the PCW manual:

- Keys 0–71 correspond to bit (n mod 8) of byte (n / 8) of the map.
- Key 72 corresponds to bit 7 of byte 9.
- Keys 73–80 correspond to bits 0–7 of byte 10.

The entries marked J1 and J2 are for keyboard joysticks. The PCW keyboard has no joystick sockets, but the controller leaves space for them in the keyboard matrix.

Memory-map key layout (byte/bit of each key):

![PCW keyboard memory key map](images/PCW%20Keyboard%20memory%20keys%20matrix.png)

Bits 5-0 of the last four bytes (0xC-0xF) are for keyboard joysticks — sets of keys that could be used directionally. The assignments correspond to the joystick entries; so bit 0 is up, bit 1 is down and so on.

The W / A / D / X keyboard joystick is only available if link LK2 is connected.

Bit 6 of byte 0xD returns the state of the Shift Lock LED. This is controlled by the keyboard and cannot be set by the PCW.
The status of the three option links is reported in the top two bits of bytes 0xD and 0xE. However, if LK1 is present, the keyboard enters a self-test mode, so in normal use bit 6 of byte 0xE will never be set. For some reason the state of LK2 is inverted, so on a stock keyboard with this link disconnected, the corresponding status bit is 1.

Bit 7 of byte 0xF is set when the keyboard is transmitting data to the host, reset when it is scanning the keys. This is the reason why a keyboard data packet is 17 bytes; the first byte sets bit 7 of byte 0xF, and the last byte resets it.

Bit 6 of byte 0xF is toggled each time the keyboard transmits its state.

The last four bytes contain controller status in bits 6 and 7. Bits 0–5 of each byte are used (by analogy with the two joystick entries) to provide keyboard combinations that may be useful as joysticks.

## Controller Status Bytes

Memory-mapped control/status matrix (bytes 0xC–0xF):

![PCW keyboard memory control matrix](images/PCW%20keyboard%20memory%20control%20matrix.png)

### 3FFCh

3FFCh gives an inverted T pattern centred on F1.

### 3FFDh

3FFDh maps to the numeric keypad.

Bit 6 reports the state of the Shift Lock LED:
- 1 if lit
- 0 if not lit

Bit 7:
- 0 if LK2 is present
- 1 if LK2 is not present

### 3FFEh

3FFEh maps:

- ASDFGHJ = Up
- ZXCVBNM = Down
- QEO[L<,/ = Left
- WRP];>/2 = Right
- Space = Fire 1
- Shift = Fire 2

Bit 6:
- 1 if LK1 is present
- 0 if not present

Bit 7:
- 1 if LK3 is present
- 0 if not present

### 3FFFh

3FFFh maps:

 - HJKL,;<> = Up
 - BNM,./2 = Down
 - QEO\[ADZC = Left
 - WRP\[SFXV = Right
 - Space = Fire 1
 - Shift = Fire 2

Bit 6 toggles with each update from the keyboard to the PCW.

Bit 7:
- 1 if the keyboard is currently transmitting its state to the PCW
- 0 if it is scanning its keys

## No Keyboard Present

If no keyboard is present, all 16 bytes of the memory map are zero.

## 10.2 Keyboard Links
The keyboard has three option links. By default they are all disconnected. 

LK1: If connected, puts the keyboard into a test mode in which it repeatedly sends various patterns of test data to the PCW. The Shift Lock LED will be constantly lit (or, more accurately, blinking faster than you can see).

LK2: If connected, pressing Shift does not cancel Shift Lock. Also enables W/A/D/X joystick, and resets bit 7 of byte 3FFDh.
LK3: If connected, sets bit 7 of byte 3FFEh. Has no other effects.

## 10.3 Keyboard Joystick(s)
The key numbering scheme on the PCW exactly matches the memory map until the gap at key 72. It also exactly matches the keyboard matrix schematic in the PCW9512 service manual- until key 72.

The keyboard schematic has entries in its matrix table for one or two joysticks. (Existing PCWs have no provision for connecting a joystick to the
keyboard, but the PC1512 does)

## 10.4 Physical connection
The pinout above shows the keyboard socket on the PCW, seen from the outside of the case. The voltages used for signalling appear to be less than TTL normal, though the PCW9512 (and probably the other models) can take signals at TTL levels without apparent harm

## 10.4.1 Hardware connection

<img src="images/PCW-connector.png" alt="PCW keyboard connector pinout" height="330">

By default, the data and clock lines are high on the motherboard when no keyboard is driving them (confirmed via scope). Although they are pulled high on the PCW motherboard, James Ots' real-keyboard timing traces and our captures show that an attached keyboard actually drives both lines low most of the time, including the gaps between frame bursts.

Verifying the circuit diagrams: Confirmed that DATA and CLK signals go into a TC74HC14 (schmit trigger inverter) twice. Effectively inverting the signal and then reversing the signal, acting as a buffer. Both signals have a 100K pullup to 5VCC, explaining the internal pull up on the PCW.

![PCW keyboard input to ASIC](images/PCW-keyboard-input-to-ASIC.png)

On the keyboard side, the CLK and DATA lines enter via 470 ohm resistors, which are then pulled up to 5VCC by 4.7K ohm resistors. Internally the PCW has protection diodes to ground on both lines.

<img src="images/PCW%20keyboard%20connector%20resistors.png" alt="PCW keyboard connector resistors" height="500">


Emulator wiring (Arduino Nano to the PCW keyboard connector, with series and pull-up resistors):

![Emulator schematic](images/schematic.png)


## 10.4.2 Clock signal

From James:

![James PCW keyboard logic analysis](images/James_PCW_Keyboard_logic_analysis.png)

The real keyboard trace shows regular clocking with a longer gap after the fourth clock pulse of each 12-bit word. That longer low period is treated by this project as a per-word timing marker.

The working Arduino implementation uses a fixed Timer1 tick and keeps the DATA transition in the middle of the clock-high phase. DATA is therefore stable by the clock edge used by the PCW input circuitry. In practice the PCW gate array accepted timing that was close to, but not exactly identical to, the captured keyboard trace. The reliable behaviour came from preserving the protocol shape: two DATA preamble pulses, 12 clocked bits, the fourth-clock longer low marker, and clean low/low inter-frame idle.

The PCW input path passes CLK and DATA through two Schmitt-trigger inverter stages, so the emulator does not logically invert the protocol for the PCW side.

## 10.4.2 Wire protocol

Unlike a PC keyboard, the PCW keyboard does not send scancodes. It repeatedly sends the complete keyboard state as a frame of 17 words, with each word containing a memory-map offset and the byte value for that offset.

Each word is 12 bits, sent MSB first:

- bits 11..8: memory-map offset `0h` to `Fh`
- bits 7..0: byte value to place at that offset

The frame order is:

1. offset `0Fh` with bit 7 set, indicating transmit active
2. offsets `00h` through `0Eh`
3. offset `0Fh` with bit 7 clear, indicating transmit complete

Before each 12-bit word, the keyboard/emulator emits two DATA-only high pulses while CLK stays low. Earlier notes suggested the PCW gate array may not need these; real-hardware testing showed that including them is necessary for stable operation with this firmware and wiring.

Within each word, the low period after the fourth clock pulse is longer than the normal inter-bit low period. This marker is also required for stable word alignment. The exact durations do not need to match the analyser captures perfectly, but the relative structure must remain: preamble pulses, clocked bits, fourth-clock longer low gap, then the next word.
