; =====================================================================
;  tt.inc — transposition table (8 KB, 1024 buckets x 8 bytes).
;  Entry: key(2) score(2) depth(1) flag(1) from(1) to(1).
;  flag: 0 exact, 1 lower bound (>=beta), 2 upper bound (<=alpha).
;  depth 0 marks an empty slot.
; =====================================================================

ptrOrigAlpha:
        ld a,(searchPly)
        add a,a
        add a,0xF0
        ld l,a
        ld h,0xD4
        ret
ptrNbFrom:
        ld a,(searchPly)
        add a,0x10
        ld l,a
        ld h,0xD5
        ret
ptrNbTo:
        ld a,(searchPly)
        add a,0x18
        ld l,a
        ld h,0xD5
        ret
ptrTtMvFrom:
        ld a,(searchPly)
        add a,0x20
        ld l,a
        ld h,0xD5
        ret
ptrTtMvTo:
        ld a,(searchPly)
        add a,0x30
        ld l,a
        ld h,0xD5
        ret

; ttAddr -> HL = entry address for the current hashKey
ttAddr:
        ld hl,(hashKey)
        ld a,h
        and 0x03
        ld h,a                  ; HL = key & 0x3FF
        add hl,hl
        add hl,hl
        add hl,hl               ; *8
        ld de,TT_BASE
        add hl,de
        ret

ttClear:
        ld hl,TT_BASE
        ld de,TT_BASE+1
        ld bc,0x1FFF
        ld (hl),0
        ldir
        ret

; ttProbe(A=depth) -> A=1 and HL=score on a usable cutoff; otherwise A=0
; and (ttFrom)/(ttTo) hold the stored move (or 0xFF) for ordering.
ttProbe:
        ld (ttCurDepth),a
        ld a,0xFF
        ld (ttFrom),a
        call ttAddr
        ld e,(hl)
        inc hl
        ld d,(hl)               ; DE = entry.key
        push hl                  ; HL at +1
        ld hl,(hashKey)
        or a
        sbc hl,de
        pop hl
        jr nz,ttMiss            ; key mismatch
        ; HL is at entry+1; walk to fields
        inc hl                  ; +2 score lo
        ld e,(hl)
        inc hl
        ld d,(hl)               ; DE = score
        ld (ttEntScore),de
        inc hl                  ; +4 depth
        ld a,(hl)
        or a
        jr z,ttMiss            ; empty slot
        ld (ttEntDepth),a
        inc hl                  ; +5 flag
        ld a,(hl)
        ld (ttEntFlag),a
        inc hl                  ; +6 from
        ld a,(hl)
        ld (ttFrom),a
        inc hl                  ; +7 to
        ld a,(hl)
        ld (ttTo),a
        ; usable for a cutoff only if entry depth >= current depth
        ld a,(ttEntDepth)
        ld hl,ttCurDepth
        cp (hl)
        jr c,ttNoCut
        ld a,(ttEntFlag)
        or a
        jr z,ttExact
        cp 1
        jr z,ttLower
        ; flag 2 = upper bound: usable if score <= alpha
        ld de,(ttEntScore)
        call ptrAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)               ; BC = alpha
        call cmpDEgtBC          ; score > alpha ?
        jr c,ttNoCut            ; score > alpha -> not usable
        ld hl,(ttEntScore)
        ld a,1
        ret
ttLower:                         ; score is a lower bound: usable if >= beta
        ld de,(ttEntScore)
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)               ; BC = beta
        ; usable if score >= beta  <=>  not(beta > score)
        push bc
        push de
        pop bc                   ; BC = score
        pop de                   ; DE = beta
        call cmpDEgtBC           ; beta > score ?
        jr c,ttNoCut
        ld hl,(ttEntScore)
        ld a,1
        ret
ttExact:
        ld hl,(ttEntScore)
        ld a,1
        ret
ttMiss:
        ld a,0xFF
        ld (ttFrom),a
ttNoCut:
        xor a
        ret

; ttStore(score in (nmScore-ish)) — write the node result.
; Inputs: (ttCurDepth)=depth, (bestScoreTmp)=score, A=flag,
;         (nbFromArr[ply])/(nbToArr[ply]) = best move.
ttStore:
        ld (ttEntFlag),a
        call ttAddr
        ld de,(hashKey)
        ld (hl),e
        inc hl
        ld (hl),d               ; key
        inc hl
        ld de,(bestScoreTmp)
        ld (hl),e
        inc hl
        ld (hl),d               ; score
        inc hl
        ld a,(ttCurDepth)
        ld (hl),a               ; depth
        inc hl
        ld a,(ttEntFlag)
        ld (hl),a               ; flag
        inc hl
        call ptrNbFrom
        ld a,(hl)
        call ttAddrNb6          ; store from at +6
        call ptrNbTo
        ld a,(hl)
        call ttAddrNb7          ; store to at +7
        ret
ttAddrNb6:
        push af
        call ttAddr
        ld de,6
        add hl,de
        pop af
        ld (hl),a
        ret
ttAddrNb7:
        push af
        call ttAddr
        ld de,7
        add hl,de
        pop af
        ld (hl),a
        ret
