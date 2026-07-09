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
