; KEYTEST.COM - Amstrad PCW8256 CP/M keyboard matrix diagnostic
;
; Reads the 16 memory-mapped PCW keyboard state bytes at BFF0h-BFFFh and
; continuously displays each address, hexadecimal value, and eight-bit binary
; value. Screen output uses CP/M BDOS functions 2 and 9. The keyboard memory
; range is read only; the program never prints into or otherwise modifies it.
;
; Build with the Pasmo Z80 assembler:
; https://pasmo.speccy.org/
;
; Insert KEYTEST.COM into the CP/M disk image with iDSK 0.20:
; https://github.com/cpcsdk/idsk
;
        org     0100h

BDOS    equ     0005h

start:
        ld      de,clear_screen
        call    print_string

refresh:
        ld      de,cursor_home
        call    print_string

        ld      hl,0bff0h
        ld      b,16

row:
        push    hl
        call    print_hex16
        pop     hl

        ld      a,' '
        call    put_char

        ld      d,(hl)
        ld      a,d
        call    print_hex8

        ld      a,' '
        call    put_char

        ld      a,d
        call    print_binary8

        ld      a,13
        call    put_char
        ld      a,10
        call    put_char

        inc     hl
        ; Continue until all 16 keyboard bytes have been displayed.
        djnz    row

        call    delay
        ; Return to the top of the screen for the next live snapshot.
        jr      refresh

print_hex16:
        ld      a,h
        call    print_hex8
        ld      a,l
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

        end     start
