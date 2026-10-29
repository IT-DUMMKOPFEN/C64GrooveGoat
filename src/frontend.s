;-------------------------------------------------------------------------------
; C64 GrooveGoat runnable PRG frontend
;
; BASIC line "10 SYS2061", shows the info screen (RLE packed screen + colors),
; plays the music from a CIA timer interrupt, counts the play time and lets
; the user switch subtunes with +/- (or cursor left/right), 1-9 and SPACE.
;
; Format of Magnus Lind's assembler from Exomizer (no SED/CLD mnemonics, so
; they are written as bytes).
;-------------------------------------------------------------------------------

        ;Defines will be inserted by the exporter here

srcptr          = $f7
dstptr          = $fb
rlecount        = $fd
rlevalue        = $fe

SCREEN          = $0400
COLORS          = $d800
RAINBOWROW      = COLORS + POS_RAINBOW

                .ORG ($0801)

                .BYTE ($0b,$08,$0a,$00,$9e,$32,$30,$36,$31,$00,$00,$00)

start:
                sei
                .BYTE ($d8)                     ;CLD
                ldx #$ff
                txs
                lda #$36                        ;BASIC ROM off, KERNAL on
                sta $01
                lda #BORDERCOL
                sta $d020
                lda #BGCOL
                sta $d021
                lda #$15                        ;Uppercase/graphics charset
                sta $d018
                lda #$80                        ;Disable SHIFT+C= case switch
                sta $0291
                lda #$01                        ;No cursor blinking
                sta $cc
                lda #$00                        ;Empty keyboard buffer
                sta $c6

        ;Unpack screen & colors

                lda #(screendata % 256)
                sta srcptr
                lda #(screendata / 256)
                sta srcptr+1
                lda #(SCREEN % 256)
                sta dstptr
                lda #(SCREEN / 256)
                sta dstptr+1
decode:
                jsr getbyte
                cmp #$ff
                bne decodeliteral
                jsr getbyte
                beq decodedone
                sta rlecount
                jsr getbyte
                sta rlevalue
decoderun:
                lda rlevalue
                jsr putbyte
                dec rlecount
                bne decoderun
                jmp decode
decodeliteral:
                jsr putbyte
                jmp decode
decodedone:

        ;Silence all SIDs in the standard area ($D400-$D7FF), then start

                lda #$00
                tax
clearsid:
                sta $d400,x
                sta $d500,x
                sta $d600,x
                sta $d700,x
                inx
                bne clearsid

        ;Logo sprites

                ldx #$00
                ldy #$00
spriteinit:
                txa
                clc
                adc #(spritedata / 64)
                sta $07f8,x
                lda spritecolors,x
                sta $d027,x
                lda #LOGOX
                sta $d000,y
                lda #LOGOY
                sta $d001,y
                iny
                iny
                inx
                cpx #$07
                bne spriteinit
                lda #$00
                sta $d010
                sta $d01b
                sta $d01c
                lda #$7f
                sta $d017
                sta $d01d
                sta $d015

                lda #(irq % 256)
                sta $0314
                lda #(irq / 256)
                sta $0315
                lda #TIMERLO
                sta $dc04
                lda #TIMERHI
                sta $dc05
                lda #$81
                sta $dc0d
                lda #$11
                sta $dc0e
                lda #$00
                jsr setsong
                cli

        ;Main loop: key handling & rainbow animation

mainloop:
waitline:
                lda $d012
                cmp #$fa
                bne waitline
waitline2:
                lda $d012
                cmp #$fa
                beq waitline2
              .IF (RAINBOWANIM != 0)
                inc animcount
                lda animcount
                and #$07
                bne noanim
                jsr rotaterainbow
noanim:
              .ENDIF
                jsr $ffe4                       ;GETIN
                beq mainloop
                cmp #$2b                        ;+
                beq nextsong
                cmp #$1d                        ;Cursor right
                beq nextsong
                cmp #$2d                        ;-
                beq prevsong
                cmp #$9d                        ;Cursor left
                beq prevsong
                cmp #$20                        ;Space
                beq restartsong
                cmp #$31                        ;1-9
                bcc mainloop
                cmp #$3a
                bcs mainloop
                sbc #$30                        ;C = 0 -> A = key - $31
                cmp #NUMSONGS
                bcs mainloop
                jsr setsong
                jmp mainloop

nextsong:
                ldx cursong
                inx
                cpx #NUMSONGS
                bcc nextsongok
                ldx #$00
nextsongok:
                txa
                jsr setsong
                jmp mainloop

prevsong:
                ldx cursong
                dex
                bpl prevsongok
                ldx #NUMSONGS-1
prevsongok:
                txa
                jsr setsong
                jmp mainloop

restartsong:
                lda cursong
                jsr setsong
                jmp mainloop

        ;Start subtune A

setsong:
                sei
                sta cursong
                ldx #$00
                stx playing
                jsr MUSICINIT
                lda #$00
                sta frames
                sta frames+1
                sta seconds
                sta minutes
                jsr showtime

        ;Subtune number (2 decimal digits)

                lda cursong
                clc
                adc #$01
                ldx #$30
tensloop:
                cmp #10
                bcc tensdone
                sbc #10
                inx
                bne tensloop
tensdone:
                ora #$30
                sta SCREEN+POS_SUBTUNE+1
                stx SCREEN+POS_SUBTUNE

        ;Song length (5 screen codes per subtune)

                lda cursong
                asl
                asl
                adc cursong
                tax
                ldy #$00
lengthcopy:
                lda lengthtbl,x
                sta SCREEN+POS_LENGTH,y
                inx
                iny
                cpy #$05
                bne lengthcopy

                lda #$01
                sta playing
                cli
                rts

        ;Interrupt: play music, count play time

irq:
                lda playing
                beq irqskip
                jsr MUSICPLAY
                inc frames
                bne irqnohi
                inc frames+1
irqnohi:
                lda frames
                cmp #(CALLSPERSEC % 256)
                bne irqskip
                lda frames+1
                cmp #(CALLSPERSEC / 256)
                bne irqskip
                lda #$00
                sta frames
                sta frames+1
                .BYTE ($f8)                     ;SED
                lda seconds
                clc
                adc #$01
                cmp #$60
                bcc irqnomin
                lda minutes
                adc #$00                        ;C = 1 -> +1
                cmp #$a0
                bcc irqminok
                lda #$00
irqminok:
                sta minutes
                lda #$00
irqnomin:
                sta seconds
                .BYTE ($d8)                     ;CLD
                jsr showtime
irqskip:
                jmp $ea31

        ;Show play time MM:SS

showtime:
                lda minutes
                lsr
                lsr
                lsr
                lsr
                ora #$30
                sta SCREEN+POS_TIME
                lda minutes
                and #$0f
                ora #$30
                sta SCREEN+POS_TIME+1
                lda #$3a
                sta SCREEN+POS_TIME+2
                lda seconds
                lsr
                lsr
                lsr
                lsr
                ora #$30
                sta SCREEN+POS_TIME+3
                lda seconds
                and #$0f
                ora #$30
                sta SCREEN+POS_TIME+4
                rts

        ;Rotate the colors of the rainbow bar

rotaterainbow:
                lda RAINBOWROW
                pha
                ldx #$00
rotateloop:
                lda RAINBOWROW+1,x
                sta RAINBOWROW,x
                inx
                cpx #37
                bne rotateloop
                pla
                sta RAINBOWROW+37
                rts

        ;Unpacker helpers

getbyte:
                ldy #$00
                lda (srcptr),y
                inc srcptr
                bne getbyteok
                inc srcptr+1
getbyteok:
                cmp #$00                        ;Set flags from the value
                rts

putbyte:
                ldy #$00
                sta (dstptr),y
                inc dstptr
                bne putbytenohi
                inc dstptr+1
putbytenohi:
                lda dstptr
                cmp #((SCREEN + 1000) % 256)
                bne putbytedone
                lda dstptr+1
                cmp #((SCREEN + 1000) / 256)
                bne putbytedone
                lda #(COLORS % 256)
                sta dstptr
                lda #(COLORS / 256)
                sta dstptr+1
putbytedone:
                rts

        ;Logo sprites (generated by tools/mksprites.py): eighth note in
        ;front of the C64 rainbow

spritepad:
                .RES ((64 - (spritepad % 64)) % 64, 0)
spritedata:
        ;Sprite 0
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$10,$00,$00,$3c,$00,$00,$3e,$00,$00)
                .BYTE ($3f,$00,$00,$37,$00,$00,$33,$80,$00,$31,$80,$00,$30,$80,$00,$30)
                .BYTE ($80,$00,$30,$80,$00,$30,$00,$07,$b0,$00,$07,$f0,$00,$0f,$f0,$00)
                .BYTE ($07,$f0,$00,$07,$e0,$00,$01,$e0,$00,$00,$00,$00,$00,$00,$00,$00)
        ;Sprite 1
                .BYTE ($00,$00,$00,$00,$38,$00,$00,$6e,$00,$00,$43,$00,$00,$41,$80,$00)
                .BYTE ($40,$80,$00,$48,$c0,$00,$4c,$40,$00,$4e,$40,$00,$4b,$40,$00,$49)
                .BYTE ($40,$00,$49,$40,$0f,$c9,$c0,$08,$48,$00,$18,$08,$00,$10,$08,$00)
                .BYTE ($18,$08,$00,$08,$18,$00,$0e,$10,$00,$03,$f0,$00,$00,$00,$00,$00)
        ;Sprite 2
                .BYTE ($00,$7f,$80,$00,$c7,$00,$03,$80,$00,$0f,$80,$00,$3f,$80,$00,$3f)
                .BYTE ($80,$00,$7e,$00,$00,$78,$00,$00,$60,$00,$00,$40,$00,$00,$00,$00)
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00)
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00)
        ;Sprite 3
                .BYTE ($00,$00,$40,$00,$00,$e0,$00,$01,$f0,$00,$00,$f0,$00,$00,$40,$00)
                .BYTE ($00,$00,$01,$80,$00,$07,$80,$00,$1f,$80,$00,$3f,$80,$00,$ff,$00)
                .BYTE ($00,$7c,$00,$00,$70,$00,$00,$60,$00,$00,$00,$00,$00,$00,$00,$00)
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00)
        ;Sprite 4
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$08,$00,$00,$3c,$00)
                .BYTE ($00,$7c,$00,$00,$3e,$00,$00,$38,$00,$00,$20,$00,$04,$00,$00,$86)
                .BYTE ($00,$03,$84,$00,$00,$00,$00,$10,$00,$00,$60,$00,$00,$20,$00,$00)
                .BYTE ($20,$00,$00,$10,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00)
        ;Sprite 5
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00)
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$06,$00,$00,$1e,$00,$00,$3e,$00,$00)
                .BYTE ($3f,$00,$02,$3c,$00,$06,$38,$00,$07,$e0,$00,$07,$80,$00,$06,$00)
                .BYTE ($00,$04,$00,$00,$00,$00,$00,$00,$00,$04,$00,$00,$02,$00,$00,$00)
        ;Sprite 6
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00)
                .BYTE ($00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00)
                .BYTE ($00,$00,$00,$02,$00,$00,$06,$00,$00,$1e,$00,$00,$7e,$00,$01,$fc)
                .BYTE ($00,$03,$fc,$00,$07,$f0,$00,$0f,$c0,$00,$0f,$00,$01,$fe,$00,$00)

spritecolors:
                .BYTE (1,0,2,8,7,5,14)

        ;Variables

cursong:        .BYTE (0)
playing:        .BYTE (0)
frames:         .BYTE (0,0)
seconds:        .BYTE (0)
minutes:        .BYTE (0)
animcount:      .BYTE (0)

        ;Song length table and packed screen will be inserted here
