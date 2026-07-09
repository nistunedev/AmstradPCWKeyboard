# Amstrad PCW8256 PS/2 Keyboard Emulator

This project is an in-development keyboard emulator for the Amstrad PCW8256.
An Arduino Nano reads make and break events from a PS/2 keyboard, maintains a
representation of the PCW keyboard matrix, and simulates the original Amstrad
PCW keyboard.

The Arduino continuously transmits PCW keyboard matrix/state frames to the
PCW8256 through the original keyboard clock and data interface. The target
machine for this firmware is the Amstrad PCW8256.

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

## Testing

`KEYTEST.COM` is included as a CP/M diagnostic utility for PCW-side testing.
It displays the keyboard state bytes visible to the PCW8256 at memory addresses
`BFF0h-BFFFh`. This helps verify whether the PCW is receiving keyboard state
updates from the emulator.

The repository includes `CPM_14_keyboard.dsk`, a CP/M disk image containing the
diagnostic utility. The diagnostic can also be rebuilt and inserted by running
`build_keytest.bat` on Windows.

## Tools

- `pasmo.exe` is included for Windows builds of `KEYTEST.COM`.
- `iDSK.exe` version 0.20 is included for updating the CP/M disk image.

## Credits

- Original fork base: [somhi/AmstradXTPS2](https://github.com/somhi/AmstradXTPS2)
- [Pasmo Z80 assembler](https://pasmo.speccy.org/)
- [iDSK 0.20](https://github.com/cpcsdk/idsk)

## Notes from John Elliots PCW Hardware Guide
https://www.seasip.info/Unix/Joyce/hardware.pdf

# PCW Keyboard

The keyboard appears as a memory-mapped device at 3FF0h–3FFFh in memory block 3.

Using the key numbering scheme in the PCW manual:

- Keys 0–71 correspond to bit (n mod 8) of byte (n / 8) of the map.
- Key 72 corresponds to bit 7 of byte 9.
- Keys 73–80 correspond to bits 0–7 of byte 10.

The entries marked J1 and J2 are for keyboard joysticks.

The last four bytes contain controller status in bits 6 and 7. Bits 0–5 of each byte are used (by analogy with the two joystick entries) to provide keyboard combinations that may be useful as joysticks.

## Controller Status Bytes

### 3FFCh

3FFCh gives an inverted T pattern centred on F1.

If link LK2 is present, it will also respond to a W/A/D/X diamond, with S as Fire 1 and Shift as Fire 2.

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

However, if LK1 is present the keyboard enters a self-test mode and transmits test patterns rather than these flags.

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

LK1: If connected, puts the keyboard into a test mode in which it repeatedly sends various patterns of data to the PCW. The Shift Lock LED will be constantly lit (or, more accurately, blinking faster than you can see).
LK2: If connected, pressing Shift does not cancel Shift Lock. Also enables W/A/D/X joystick, and resets bit 7 of byte 3FFDh.
LK3: If connected, sets bit 7 of byte 3FFEh. Has no other effects.

## 10.3 Keyboard Joystick(s)
The key numbering scheme on the PCW exactly matches the memory map until the gap at key 72. It also exactly matches the keyboard matrix schematic in the PCW9512 service manual- until key 72.

The keyboard schematic has entries in its matrix table for one or two joysticks. (Existing PCWs have no provision for connecting a joystick to the
keyboard, but the PC1512 does)

## 10.4 Physical connection
The pinout above shows the keyboard socket on the PCW, seen from the outside of the case. The voltages used for signalling appear to be less than TTL normal, though the PCW9512 (and probably the other models) can take signals at TTL levels without apparent harm

## 10.4.1 Wire protocol
By default, the data and clock lines are high. To send a bit, the keyboard drives the data line low or high, pulls the clock line low, and then a little later returns them both to high. Exact timings are unknown, though the PCW motherboard seems happy to accept timings similar to those used by the PC1512 keyboard (set data, wait for 5µs, set clock, wait for 5µs, return both lines to high, wait for 40µs).

A keycode is 12 bits long, with the most significant bit first. The first four bits are the offset in the memory map (0-0Fh), and the last eight bits are the value to be placed into memory at that address. The PCW keyboard toggles the DATA line twice before sending each word, but the gate array at the PCW end doesn’t seem to need this.

When transmitting its state, the controller sends a 17-keycode packet. The first word is byte 0Fh, with the top bit set to 1; then bytes 0-0Eh; then byte 0Fh again, with the top bit set to 0.

