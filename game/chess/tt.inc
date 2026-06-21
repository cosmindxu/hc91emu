; =====================================================================
;  tt.inc — transposition table.
;  Entry: key(2) score(2) depth(1) flag(1) from(1) to(1).
;  flag: 0 exact, 1 lower bound (>=beta), 2 upper bound (<=alpha).
;  depth 0 marks an empty slot.
;
;  48K: 8 KB / 1024 buckets at 0x6000, addressed directly.
;  128K (is128=1): 64 KB / 8192 buckets across the four spare 16K RAM
;  banks (1,3,4,6) paged through 0x7FFD into the 0xC000 window.  Each
;  access stages the 8-byte entry to/from ttStage (non-pageable RAM) with
;  a register-only LDIR inside a DI/EI window — no stack use and no ISR
;  while a bank is paged, so the workspace/stack (also in 0xC000-0xFFFF)
;  stay coherent.  ttFetch/ttFlush hide the split; the search is unchanged.
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
ptrKiller:
        ld a,(searchPly)
        add a,a
        add a,a                  ; ply*4
        ld l,a
        ld h,0xD1
        ret
ptrInChk:
        ld a,(searchPly)
        add a,0x40
        ld l,a
        ld h,0xD1
        ret

; set root alpha/beta (searchPly assumed 0) from DE
setAlphaDE:
        call ptrAlpha
        ld (hl),e
        inc hl
        ld (hl),d
        ret
setBetaDE:
        call ptrBeta
        ld (hl),e
        inc hl
        ld (hl),d
        ret

; ttAddr -> HL = 48K entry address (TT_BASE table) for the current hashKey
ttAddr:
        ld hl,(hashKey)
        ld a,h
        and 0x01
        ld h,a                  ; HL = key & 0x1FF (512 buckets)
        add hl,hl
        add hl,hl
        add hl,hl               ; *8
        ld de,TT_BASE
        add hl,de
        ret

; ttAddr128 -> HL = 0xC000 window address, A = 0x7FFD bank value, for the
; current hashKey.  Uses 13 key bits: top 2 select one of 4 spare banks,
; low 11 index an 8-byte slot within that bank (8192 slots total).
; Called outside the DI/EI window, so it may use the stack.
ttAddr128:
        ld hl,(hashKey)
        ld a,h
        and 0x1F
        ld h,a                  ; HL = key & 0x1FFF (13-bit index)
        ld a,h
        rrca
        rrca
        rrca
        and 3                   ; bank index 0..3 = idx>>11
        ld e,a
        ld d,0
        push hl                 ; save the 13-bit index
        ld hl,ttBanks
        add hl,de
        ld c,(hl)               ; C = 0x7FFD bank value
        pop hl
        ld a,h
        and 7
        ld h,a                  ; HL = idx & 0x7FF (slot within bank)
        add hl,hl
        add hl,hl
        add hl,hl               ; *8 -> 0..0x3FF8 offset
        ld de,0xC000
        add hl,de               ; HL = window address
        ld a,c                  ; A = bank value
        ret

; ttFetch — copy the current position's 8-byte entry into ttStage.
ttFetch:
        ld a,(is128)
        or a
        jr nz,ttFetch128
        call ttAddr
        ld de,ttStage
        ld bc,8
        ldir
        ret
ttFetch128:
        call ttAddr128          ; HL = src window addr, A = bank value
        ld de,ttStage
        di
        ld bc,0x7FFD
        out (c),a               ; page the TT bank in
        ld bc,8
        ldir                    ; [HL banked] -> [DE ttStage], register-only
        ld bc,0x7FFD
        ld a,0x10
        out (c),a               ; restore ROM1, bank 0
        ei
        ret

; ttFlush — write ttStage back into the current position's entry.
ttFlush:
        ld a,(is128)
        or a
        jr nz,ttFlush128
        call ttAddr
        ex de,hl                ; DE = dest
        ld hl,ttStage
        ld bc,8
        ldir
        ret
ttFlush128:
        call ttAddr128          ; HL = dest window addr, A = bank value
        ex de,hl                ; DE = dest (A preserved)
        ld hl,ttStage           ; src
        di
        ld bc,0x7FFD
        out (c),a               ; page the TT bank in
        ld bc,8
        ldir
        ld bc,0x7FFD
        ld a,0x10
        out (c),a               ; restore ROM1, bank 0
        ei
        ret

ttClear:
        ld a,(is128)
        or a
        jr nz,ttClear128
        ld hl,TT_BASE
        ld de,TT_BASE+1
        ld bc,0x0FFF            ; 4 KB (512 entries * 8 bytes)
        ld (hl),0
        ldir
        ret
; clear all four spare banks (16K each) of the 128K transposition table
ttClear128:
        ld b,4
        ld hl,ttBanks
ttc128L:
        ld a,(hl)
        push hl
        push bc
        di
        ld bc,0x7FFD
        out (c),a               ; page this bank in
        ld hl,0xC000
        ld de,0xC001
        ld bc,0x3FFF
        ld (hl),0
        ldir
        ld bc,0x7FFD
        ld a,0x10
        out (c),a               ; restore bank 0
        ei
        pop bc
        pop hl
        inc hl
        djnz ttc128L
        ret

; ttProbe(A=depth) -> A=1 and HL=score on a usable cutoff; otherwise A=0
; and (ttFrom)/(ttTo) hold the stored move (or 0xFF) for ordering.
ttProbe:
        ld (ttCurDepth),a
        ld a,0xFF
        ld (ttFrom),a
        call ttFetch            ; ttStage = entry for this position
        ld hl,ttStage
        ld e,(hl)
        inc hl
        ld d,(hl)               ; DE = entry.key
        ld hl,(hashKey)
        or a
        sbc hl,de
        jr nz,ttMiss            ; key mismatch
        ld hl,(ttStage+2)
        ld (ttEntScore),hl      ; score
        ld a,(ttStage+4)        ; depth
        or a
        jr z,ttMiss            ; empty slot
        ld (ttEntDepth),a
        ld a,(ttStage+5)
        ld (ttEntFlag),a
        ld a,(ttStage+6)
        ld (ttFrom),a
        ld a,(ttStage+7)
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
        ld hl,ttStage
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
        call ptrNbFrom
        ld a,(hl)
        ld (ttStage+6),a        ; from
        call ptrNbTo
        ld a,(hl)
        ld (ttStage+7),a        ; to
        call ttFlush            ; write the entry back (banked on 128K)
        ret
