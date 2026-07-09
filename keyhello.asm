; KEYHELLO.COM - minimal CP/M console and disk-loading diagnostic

        org     0100h

BDOS    equ     0005h

start:
        ld      de,message
        ld      c,9
        call    BDOS

        ld      c,1
        call    BDOS

        jp      0000h

message:
        db      13,10
        db      'KEYHELLO.COM loaded and BDOS output works.',13,10
        db      'Press any key to return to CP/M.',13,10,'$'

        end     start
