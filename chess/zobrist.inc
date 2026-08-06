; =====================================================================
;  zobrist.inc — Zobrist hashing for the transposition table and for
;  repetition detection.  The key is maintained incrementally by
;  makeMove (and saved/restored by unmakeMove); computeKey rebuilds it
;  from scratch and is used to seed positions and to self-test the
;  incremental updates inside perft.
;
;  The key is 24 bits: the low 16 in (hashKey), the high 8 in (hashKey2).
;  Why the split, and why the extra byte:
;
;  * The TT indexes with the LOW bits of the key (9 bits on 48K, 13 on
;    128K) and verifies with a 2-byte field, so a 16-bit key leaves only
;    16-9 = 7 (48K) or 16-13 = 3 (128K) bits actually verifying anything.
;    Three bits means one in eight probes that land on another position's
;    slot is wrongly accepted.  The stored field is now key bits 8..23,
;    which restores 15 (48K) / 11 (128K) independent verification bits
;    without changing the entry layout or the table size.
;  * The low 16 bits are produced by the same tables as before, bit for
;    bit, because engine.inc's opening book stores precomputed 16-bit
;    keys.  Do not re-seed or re-order zobPiece/zobCastle/zobEp/zobSide
;    without regenerating bookTable with bookgen.py.
;  * zobHi may NOT simply extend the rng stream: `rng` is GF(2)-linear
;    with a 15-dimensional orbit, so every value it emits — at any width —
;    lies in a 15-dimensional subspace and so does every XOR of them.  A
;    wider key built that way carries no extra entropy at all (measured:
;    zero reduction in TT false hits).  The ADD-cascade below is there to
;    leave that subspace; the carries are the point, not the shuffling.
;  * zobHi lives in the program image rather than the 0xD000 workspace:
;    the workspace is inside the 0xC000 paging window used by the 128K
;    banked table, and there is no free 793-byte hole below it anyway.
; =====================================================================

NZOBE   equ 793                  ; table entries: 12*64 pieces + 16 + 8 + 1

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
        ; high plane: same rng, nonlinearly whitened (see header)
        ld hl,zobHi
        ld bc,NZOBE
        ld e,0                   ; E = cascade accumulator (rng preserves DE/BC)
ziH:    push hl
        call rng
        pop hl
        ld d,a                   ; D = raw byte
        add a,e
        add a,0x9E
        rlca
        xor d
        ld e,a
        ld (hl),a
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,ziH
        xor a
        ld (hashKey2),a
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
        add hl,de                ; HL = table entry index (idx*64 + sq64)
        push hl
        ld de,zobHi
        add hl,de
        ld a,(hl)
        ld hl,hashKey2
        xor (hl)
        ld (hl),a                ; key bits 16..23
        pop hl
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
        push hl
        ld l,a
        ld h,0
        ld de,zobHi+768
        add hl,de
        jr keyXorHi
keyXorEp:                        ; A = ep square (0xFF = none)
        cp 0xFF
        ret z
        and 7
        ld l,a
        ld h,0
        add hl,hl
        ld de,zobEp
        add hl,de
        push hl
        ld l,a
        ld h,0
        ld de,zobHi+784
        add hl,de
        jr keyXorHi
keyXorSide:
        ld hl,zobSide
        push hl
        ld hl,zobHi+792
keyXorHi:                        ; HL -> high byte, (SP) -> the 2-byte value
        ex de,hl
        ld hl,hashKey2
        ld a,(de)
        xor (hl)
        ld (hl),a                ; key bits 16..23
        pop hl
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
        xor a
        ld (hashKey2),a
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

; --- 24-bit key state ------------------------------------------------
; Data, not code: reached only by address, never by fall-through (keyUpdate
; above ends in RET and tt.inc opens with a label).  Both live in the
; program image at 0x8000+, which the 128K build never pages out.
hashKey2:
        defb 0                   ; key bits 16..23 (low 16 are in hashKey)
zobHi:
        defs NZOBE               ; high byte of each Zobrist table entry:
                                 ; 768 piece/square, 16 castle, 8 ep, 1 side
