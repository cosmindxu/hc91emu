; =====================================================================
;  zobrist.inc — Zobrist hashing for the transposition table and for
;  repetition detection.  The key is maintained incrementally by
;  makeMove (and saved/restored by unmakeMove); computeKey rebuilds it
;  from scratch and is used to seed positions and to self-test the
;  incremental updates inside perft.
; =====================================================================

; fill the random tables from the PRNG
zobInit:
        ld hl,zobPiece
        ld bc,0x0632             ; 1586 bytes: piece+castle+ep+side
ziL:    push hl
        call rng                 ; clobbers HL
        pop hl
        ld (hl),a
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,ziL
        ret

; keyXorPieceSq(B=piece, C=square) — hashKey ^= zobPiece[idx][sq64]
keyXorPieceSq:
        ld a,b
        and 8
        jr z,kxpW
        ld a,6
        jr kxpI
kxpW:   xor a
kxpI:   ld d,a
        ld a,b
        and 7
        dec a
        add a,d                  ; idx 0..11
        ld e,a
        ld a,c
        and 0x70
        rrca
        ld d,a
        ld a,c
        and 7
        or d                     ; sq64
        ; HL = idx*64 + sq64
        ld h,0
        ld l,e
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl                ; idx*64
        ld d,0
        ld e,a
        add hl,de
        add hl,hl                ; *2
        ld de,zobPiece
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld hl,(hashKey)
        ld a,l
        xor e
        ld l,a
        ld a,h
        xor d
        ld h,a
        ld (hashKey),hl
        ret

keyXorCastle:                    ; A = rights (0..15)
        ld l,a
        ld h,0
        add hl,hl
        ld de,zobCastle
        add hl,de
        jr keyXorAt
keyXorEp:                        ; A = ep square (0xFF = none)
        cp 0xFF
        ret z
        and 7
        ld l,a
        ld h,0
        add hl,hl
        ld de,zobEp
        add hl,de
        jr keyXorAt
keyXorSide:
        ld hl,zobSide
keyXorAt:                        ; HL -> 2-byte value; hashKey ^= (HL)
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld hl,(hashKey)
        ld a,l
        xor e
        ld l,a
        ld a,h
        xor d
        ld h,a
        ld (hashKey),hl
        ret

; computeKey — rebuild hashKey from scratch for the current position.
computeKey:
        ld hl,0
        ld (hashKey),hl
        ld c,0
ckLoop: ld a,c
        and 0x88
        jr nz,ckNext
        ld h,0xE0
        ld l,c
        ld a,(hl)
        or a
        jr z,ckNext
        ld b,a
        call keyXorPieceSq
ckNext: inc c
        ld a,c
        cp 0x78
        jr nz,ckLoop
        ld a,(sideToMove)
        or a
        jr z,ckNoSide
        call keyXorSide
ckNoSide:
        ld a,(castling)
        call keyXorCastle
        ld a,(epSquare)
        call keyXorEp
        ret

; keyUpdate — incrementally fold a just-made move into hashKey.  Called
; at the end of makeMove, after the board and state have been mutated and
; the side toggled.  Uses mk* temporaries, mkOldCastle/mkOldEp.
keyUpdate:
        ; remove moved piece from its origin
        ld a,(mkPiece)
        ld b,a
        ld a,(mvFrom)
        ld c,a
        call keyXorPieceSq
        ; remove captured piece
        ld a,(mkCaptured)
        or a
        jr z,kuNoCap
        ld b,a
        ld a,(mkCapSq)
        ld c,a
        call keyXorPieceSq
kuNoCap:
        ; add placed piece at destination
        ld a,(mkPromo)
        or a
        jr z,kuNoProm
        ld b,a
        ld a,(mkSide)
        or b
        jr kuPlace
kuNoProm:
        ld a,(mkPiece)
kuPlace:
        ld b,a
        ld a,(mvTo)
        ld c,a
        call keyXorPieceSq
        ; castling rook
        ld a,(mkSpecial)
        cp SP_OO
        jr z,kuOO
        cp SP_OOO
        jr z,kuOOO
        jr kuRights
kuOO:
        ld a,(mkSide)
        or WR
        ld b,a
        ld a,(mkSide)
        or a
        ld c,0x07
        jr nz,kuOOb
        jr kuOOdo
kuOOb:  ld c,0x77
kuOOdo:
        call keyXorPieceSq       ; remove rook from h-file
        ld a,(mkSide)
        or WR
        ld b,a
        ld a,(mkSide)
        or a
        ld c,0x05
        jr z,kuOOto
        ld c,0x75
kuOOto: call keyXorPieceSq       ; add rook on f-file
        jr kuRights
kuOOO:
        ld a,(mkSide)
        or WR
        ld b,a
        ld a,(mkSide)
        or a
        ld c,0x00
        jr z,kuOOOdo
        ld c,0x70
kuOOOdo:
        call keyXorPieceSq
        ld a,(mkSide)
        or WR
        ld b,a
        ld a,(mkSide)
        or a
        ld c,0x03
        jr z,kuOOOto
        ld c,0x73
kuOOOto:
        call keyXorPieceSq
kuRights:
        ld a,(mkOldCastle)
        call keyXorCastle
        ld a,(castling)
        call keyXorCastle
        ld a,(mkOldEp)
        call keyXorEp
        ld a,(epSquare)
        call keyXorEp
        call keyXorSide
        ret
