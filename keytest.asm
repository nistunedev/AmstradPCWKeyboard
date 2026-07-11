; KEYTEST.COM - Amstrad PCW8256 CP/M keyboard matrix diagnostic
;
; Maps physical memory block 3 into the 8000h-BFFFh CPU window, copies the
; keyboard state bytes from BFF0h-BFFFh into local memory, restores CP/M's TPA
; block 6, and displays the physical offsets 3FF0h-3FFFh. Screen output uses
; CP/M BDOS functions 2 and 9.
; The display refreshes continuously and the program NEVER reads the console,
; so stray/phantom key characters (as seen from a faulty keyboard) cannot
; terminate it. Exit only by resetting the machine.
;
; Build with the Pasmo Z80 assembler:
; https://pasmo.speccy.org/
;
; Copy KEYTEST.COM into the CP/M disk image with a tool that honours this
; image's sector interleave (e.g. CPCDiskXP, or transfer inside the emulator).
; Do NOT use iDSK 0.20 on CPM_14_keyboard.dsk - it corrupts writes; see
; idsk_issues.md.
;
        org     0100h

BDOS    equ     0005h
MEMORY_PAGE_2_PORT equ 0f2h
KEYBOARD_BLOCK     equ 083h
TPA_BLOCK          equ 086h
KEYBOARD_ADDRESS   equ 0bff0h
KEYBOARD_BYTES     equ 16

start:
        ld      sp,stack_top
        ld      de,clear_screen
        call    print_string

refresh:
        ld      de,cursor_home
        call    print_string
        ld      de,title
        call    print_string
        ld      de,build_timestamp
        call    print_string

        call    snapshot_keyboard

        ld      hl,keyboard_snapshot
        ld      de,03ff0h
        ld      b,KEYBOARD_BYTES

row:
        call    print_hex16_de

        ld      a,' '
        call    put_char

        ld      c,(hl)
        ld      a,c
        call    print_hex8

        ld      a,' '
        call    put_char

        ld      a,c
        call    print_binary8

        ld      a,13
        call    put_char
        ld      a,10
        call    put_char

        inc     hl
        inc     de
        ; Continue until all 16 keyboard bytes have been displayed.
        djnz    row

        call    delay
        ; Loop forever. The console is never read, so a phantom key stream
        ; from a faulty keyboard cannot exit the program. Reset to leave.
        jr      refresh

snapshot_keyboard:
        push    af
        push    bc
        push    de
        push    hl
        di
        ld      a,KEYBOARD_BLOCK
        out     (MEMORY_PAGE_2_PORT),a
        ld      hl,KEYBOARD_ADDRESS
        ld      de,keyboard_snapshot
        ld      bc,KEYBOARD_BYTES
        ldir
        ld      a,TPA_BLOCK
        out     (MEMORY_PAGE_2_PORT),a
        ei
        pop     hl
        pop     de
        pop     bc
        pop     af
        ret

print_hex16_de:
        ld      a,d
        call    print_hex8
        ld      a,e
        call    print_hex8
        ret

print_hex8:
        push    af
        rrca
        rrca
        rrca
        rrca
        call    print_hex_digit
        pop     af

print_hex_digit:
        and     0fh
        add     a,'0'
        cp      '9'+1
        ; Values 0-9 are already ASCII digits, so skip alphabetic adjustment.
        jr      c,print_hex_digit_ready
        add     a,7
print_hex_digit_ready:
        call    put_char
        ret

print_binary8:
        push    bc
        push    de
        ld      e,a
        ld      b,8
print_binary_bit:
        sla     e
        ld      a,'0'
        ; If the shifted-out bit is zero, keep '0'; otherwise select '1'.
        jr      nc,print_binary_digit
        inc     a
print_binary_digit:
        call    put_char
        ; Repeat until all eight bits have been printed, most significant first.
        djnz    print_binary_bit
        pop     de
        pop     bc
        ret

put_char:
        push    af
        push    bc
        push    de
        push    hl
        ld      e,a
        ld      c,2
        call    BDOS
        pop     hl
        pop     de
        pop     bc
        pop     af
        ret

print_string:
        push    bc
        push    de
        push    hl
        ld      c,9
        call    BDOS
        pop     hl
        pop     de
        pop     bc
        ret

delay:
        push    bc
        push    de
        ld      b,2
delay_outer:
        ld      de,0000h
delay_inner:
        dec     de
        ld      a,d
        or      e
        ; Keep counting down until the 16-bit inner delay reaches zero.
        jr      nz,delay_inner
        ; Repeat the inner countdown to make screen changes easier to observe.
        djnz    delay_outer
        pop     de
        pop     bc
        ret

clear_screen:
        db      1bh,'E','$'
cursor_home:
        db      1bh,'H','$'
title:
        db      'KEYTEST.COM running - physical block 3, 3FF0h-3FFFh',13,10
        db      'Runs continuously - reset the machine to exit',13,10,'$'
build_timestamp:
        include "build_timestamp.inc"

keyboard_snapshot:
        defs    KEYBOARD_BYTES

        defs    64
stack_top:

        ; Keep the COM image on complete 128-byte CP/M record boundaries.
        defs    512-($-0100h),0e5h

        end     start
