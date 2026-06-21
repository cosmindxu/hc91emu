; =====================================================================
;  engine.inc — evaluation + negamax search.
;
;  v1 is a clean fixed-depth negamax (minimax) with a static leaf
;  evaluation (material + piece-square tables).  Alpha-beta pruning,
;  quiescence, transposition tables, killer/history ordering and
;  iterative deepening are layered on in the Optimization phase of the
;  roadmap; the per-ply array frame here is built to accept them.
;
;  Per-ply search frame (arrays indexed by searchPly, page 0xD4):
;     bestArr  0xD440 (2)   depthArr 0xD460 (1)   legalArr 0xD470 (1)
;     mptrArr  0xD480 (2)   cntArr   0xD4A0 (1)
; =====================================================================

ptrDepth:
        ld a,(searchPly)
        add a,0x60
        ld l,a
        ld h,0xD4
        ret
ptrLegal:
        ld a,(searchPly)
        add a,0x70
        ld l,a
        ld h,0xD4
        ret
ptrCnt:
        ld a,(searchPly)
        add a,0xA0
        ld l,a
        ld h,0xD4
        ret
ptrBest:
        ld a,(searchPly)
        add a,a
        add a,0x40
        ld l,a
        ld h,0xD4
        ret
ptrMptr:
        ld a,(searchPly)
        add a,a
        add a,0x80
        ld l,a
        ld h,0xD4
        ret

; cmpDEgtBC: CF=1 if DE > BC  (signed 16-bit, overflow-correct)
cmpDEgtBC:
        ld h,b
        ld l,c
        or a
        sbc hl,de               ; BC - DE
        jp pe,cgOv
        jp m,cgYes
        or a
        ret
cgOv:
        jp m,cgNo
cgYes:  scf
        ret
cgNo:   or a
        ret

; =====================================================================
;  eval — static evaluation, side-to-move relative, returns HL.
; =====================================================================
eval:
        ld hl,0
        ld (evalAcc),hl
        ld a,0
evLoop:
        ld (tmpSq),a
        and 0x88
        jp nz,evNext
        ld a,(tmpSq)
        ld l,a
        ld h,0xE0
        ld a,(hl)
        or a
        jp z,evNext
        ld b,a                  ; piece
        ; material -> DE
        and 7
        add a,a
        ld e,a
        ld d,0
        ld hl,materialTbl
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)               ; DE = material value
        ; pst index (white) = ((sq&0x70)>>1)|(sq&7)
        ld a,(tmpSq)
        ld c,a
        and 0x70
        rrca
        ld l,a
        ld a,c
        and 7
        or l
        ld c,a                  ; C = white index
        ld a,b
        and 8
        jr z,evWi
        ld a,c
        xor 0x38
        ld c,a                  ; black: mirror rank
evWi:
        ; pst row base = pstTables + (type-1)*64
        ld a,b
        and 7
        dec a
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl               ; *64
        ld a,c
        ld c,l
        ld b,h                  ; BC = (type-1)*64
        ld l,a
        ld h,0
        add hl,bc               ; + index
        ld bc,pstTables
        add hl,bc
        ld a,(hl)               ; signed PST byte
        ; HL = sign-extend(A)
        ld l,a
        rla
        sbc a,a
        ld h,a
        add hl,de               ; total = material + pst
        ; re-load piece colour (B was clobbered) from board
        ld a,(tmpSq)
        ld e,a
        ld d,0xE0
        ld a,(de)
        and 8
        jr nz,evBlack
        ld de,(evalAcc)
        add hl,de
        ld (evalAcc),hl
        jr evNext
evBlack:
        ex de,hl
        ld hl,(evalAcc)
        or a
        sbc hl,de
        ld (evalAcc),hl
evNext:
        ld a,(tmpSq)
        inc a
        cp 0x78
        jp nz,evLoop
        ld a,(sideToMove)
        or a
        jr z,evWhiteRet
        ld hl,0
        ld de,(evalAcc)
        or a
        sbc hl,de
        ret
evWhiteRet:
        ld hl,(evalAcc)
        ret

; =====================================================================
;  negamax(A=depth) -> HL=score (side-to-move relative).
; =====================================================================
negamax:
        or a
        jp z,eval                ; leaf: static eval
        ld b,a                   ; B = depth (ptrDepth clobbers A)
        call ptrDepth
        ld (hl),b
        ld bc,-INF
        call ptrBest
        ld (hl),c
        inc hl
        ld (hl),b
        call ptrLegal
        ld (hl),0                ; legalArr[ply] = 0
        call genMoves
        ld a,(genCount)
        ld b,a                   ; B = count (ptrCnt clobbers A)
        call ptrCnt
        ld (hl),b
        call curMoveBuf
        ex de,hl                 ; DE = buffer base
        call ptrMptr
        ld (hl),e
        inc hl
        ld (hl),d
nmLoop:
        call ptrCnt
        ld a,(hl)
        or a
        jp z,nmDone
        dec a
        ld (hl),a
        ; load current move from mptr
        call ptrMptr
        ld a,(hl)
        ld e,a
        inc hl
        ld a,(hl)
        ld d,a                   ; DE = move ptr
        ld a,(de)
        ld (mvFrom),a
        inc de
        ld a,(de)
        ld (mvTo),a
        inc de
        ld a,(de)
        ld (mvFlag),a
        inc de
        inc de
        push de
        call ptrMptr
        pop de
        ld (hl),e
        inc hl
        ld (hl),d
        ; save root move
        ld a,(searchPly)
        or a
        jr nz,nmNoRootSave
        ld a,(mvFrom)
        ld (rootFrom),a
        ld a,(mvTo)
        ld (rootTo),a
        ld a,(mvFlag)
        ld (rootFlag),a
nmNoRootSave:
        call makeMove
        call moverInCheck
        or a
        jp z,nmLegal
        call unmakeMove
        jp nmLoop
nmLegal:
        call ptrLegal
        inc (hl)
        call ptrDepth
        ld a,(hl)
        dec a
        ld b,a                   ; child depth
        ld a,(searchPly)
        inc a
        ld (searchPly),a
        ld a,b
        call negamax             ; HL = child score
        ex de,hl
        ld hl,0
        or a
        sbc hl,de                ; HL = -child = our score
        push hl
        ld a,(searchPly)
        dec a
        ld (searchPly),a
        call unmakeMove
        pop hl
        ; compare with best
        ex de,hl                 ; DE = score
        call ptrBest
        ld c,(hl)
        inc hl
        ld b,(hl)                ; BC = best
        call cmpDEgtBC
        jp nc,nmLoop
        ; new best
        call ptrBest
        ld (hl),e
        inc hl
        ld (hl),d
        ld a,(searchPly)
        or a
        jp nz,nmLoop
        ; root: record best move
        ld a,(rootFrom)
        ld (bestFrom),a
        ld a,(rootTo)
        ld (bestTo),a
        ld a,(rootFlag)
        ld (bestFlag),a
        jp nmLoop
nmDone:
        call ptrLegal
        ld a,(hl)
        or a
        jp nz,nmRetBest
        ; no legal moves
        ld a,(sideToMove)
        call inCheckSide
        or a
        jp z,nmStale
        ; checkmated: -(MATE - ply)
        ld a,(searchPly)
        ld e,a
        ld d,0
        ld hl,MATE
        or a
        sbc hl,de
        ex de,hl
        ld hl,0
        or a
        sbc hl,de
        ret
nmStale:
        ld hl,0
        ret
nmRetBest:
        call ptrBest
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        ret

; =====================================================================
;  aiMove — search from the root and play the best move.
; =====================================================================
aiMove:
        xor a
        ld (searchPly),a
        ld a,0xFF
        ld (bestFrom),a
        ld a,(aiDepth)
        call negamax
        ld a,(bestFrom)
        cp 0xFF
        ret z                    ; no move (terminal); caller handles
        ld a,(bestFrom)
        ld (mvFrom),a
        ld a,(bestTo)
        ld (mvTo),a
        ld a,(bestFlag)
        ld (mvFlag),a
        xor a
        ld (searchPly),a
        call makeMove
        ret

; =====================================================================
;  TABLES
; =====================================================================
materialTbl:
        defw 0,100,320,330,500,900,0

; piece-square tables, index 0 = a1 .. 63 = h8 (white perspective);
; black is mirrored by index ^ 0x38.  Values are signed centipawns.
pstTables:
; --- pawn ---
        defb   0,  0,  0,  0,  0,  0,  0,  0
        defb   5, 10, 10,-20,-20, 10, 10,  5
        defb   5, -5,-10,  0,  0,-10, -5,  5
        defb   0,  0,  0, 20, 20,  0,  0,  0
        defb   5,  5, 10, 25, 25, 10,  5,  5
        defb  10, 10, 20, 30, 30, 20, 10, 10
        defb  50, 50, 50, 50, 50, 50, 50, 50
        defb   0,  0,  0,  0,  0,  0,  0,  0
; --- knight ---
        defb -50,-40,-30,-30,-30,-30,-40,-50
        defb -40,-20,  0,  5,  5,  0,-20,-40
        defb -30,  5, 10, 15, 15, 10,  5,-30
        defb -30,  0, 15, 20, 20, 15,  0,-30
        defb -30,  5, 15, 20, 20, 15,  5,-30
        defb -30,  0, 10, 15, 15, 10,  0,-30
        defb -40,-20,  0,  0,  0,  0,-20,-40
        defb -50,-40,-30,-30,-30,-30,-40,-50
; --- bishop ---
        defb -20,-10,-10,-10,-10,-10,-10,-20
        defb -10,  5,  0,  0,  0,  0,  5,-10
        defb -10, 10, 10, 10, 10, 10, 10,-10
        defb -10,  0, 10, 10, 10, 10,  0,-10
        defb -10,  5,  5, 10, 10,  5,  5,-10
        defb -10,  0,  5, 10, 10,  5,  0,-10
        defb -10,  0,  0,  0,  0,  0,  0,-10
        defb -20,-10,-10,-10,-10,-10,-10,-20
; --- rook ---
        defb   0,  0,  0,  5,  5,  0,  0,  0
        defb  -5,  0,  0,  0,  0,  0,  0, -5
        defb  -5,  0,  0,  0,  0,  0,  0, -5
        defb  -5,  0,  0,  0,  0,  0,  0, -5
        defb  -5,  0,  0,  0,  0,  0,  0, -5
        defb  -5,  0,  0,  0,  0,  0,  0, -5
        defb   5, 10, 10, 10, 10, 10, 10,  5
        defb   0,  0,  0,  0,  0,  0,  0,  0
; --- queen ---
        defb -20,-10,-10, -5, -5,-10,-10,-20
        defb -10,  0,  5,  0,  0,  0,  0,-10
        defb -10,  5,  5,  5,  5,  5,  0,-10
        defb   0,  0,  5,  5,  5,  5,  0, -5
        defb  -5,  0,  5,  5,  5,  5,  0, -5
        defb -10,  0,  5,  5,  5,  5,  0,-10
        defb -10,  0,  0,  0,  0,  0,  0,-10
        defb -20,-10,-10, -5, -5,-10,-10,-20
; --- king (middlegame) ---
        defb -30,-40,-40,-50,-50,-40,-40,-30
        defb -30,-40,-40,-50,-50,-40,-40,-30
        defb -30,-40,-40,-50,-50,-40,-40,-30
        defb -30,-40,-40,-50,-50,-40,-40,-30
        defb -20,-30,-30,-40,-40,-30,-30,-20
        defb -10,-20,-20,-20,-20,-20,-20,-10
        defb  20, 20,  0,  0,  0,  0, 20, 20
        defb  20, 30, 10,  0,  0, 10, 30, 20
