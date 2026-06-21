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
ptrAlpha:
        ld a,(searchPly)
        add a,a
        add a,0xB0
        ld l,a
        ld h,0xD4
        ret
ptrBeta:
        ld a,(searchPly)
        add a,a
        add a,0xD0
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
;  negamax(A=depth) -> HL=score.  Alpha-beta over a searchPly-indexed
;  frame; the caller presets alphaArr[ply]/betaArr[ply].  Depth 0 falls
;  into the quiescence search.
; =====================================================================
negamax:
        or a
        jp z,quiesce
        ld b,a
        call ptrDepth
        ld (hl),b
        ld bc,-INF
        call ptrBest
        ld (hl),c
        inc hl
        ld (hl),b
        call ptrLegal
        ld (hl),0
        call genMoves
        call orderMoves
        ld a,(genCount)
        ld b,a
        call ptrCnt
        ld (hl),b
        call curMoveBuf
        ex de,hl
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
        call ptrMptr
        ld a,(hl)
        ld e,a
        inc hl
        ld a,(hl)
        ld d,a
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
        ld a,(searchPly)
        or a
        jr nz,nmNoRoot
        ld a,(mvFrom)
        ld (rootFrom),a
        ld a,(mvTo)
        ld (rootTo),a
        ld a,(mvFlag)
        ld (rootFlag),a
nmNoRoot:
        call makeMove
        call moverInCheck
        or a
        jp z,nmLegal
        call unmakeMove
        jp nmLoop
nmLegal:
        call ptrLegal
        inc (hl)
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)
        ld hl,0
        or a
        sbc hl,bc
        push hl                  ; childAlpha = -parentBeta
        call ptrAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)
        ld hl,0
        or a
        sbc hl,bc
        push hl                  ; childBeta = -parentAlpha
        call ptrDepth
        ld a,(hl)
        dec a
        ld (nmCd),a
        ld a,(searchPly)
        inc a
        ld (searchPly),a
        pop de                   ; childBeta
        call ptrBeta
        ld (hl),e
        inc hl
        ld (hl),d
        pop de                   ; childAlpha
        call ptrAlpha
        ld (hl),e
        inc hl
        ld (hl),d
        ld a,(nmCd)
        call negamax
        ex de,hl
        ld hl,0
        or a
        sbc hl,de                ; HL = -child = score
        ld (nmScore),hl
        ld a,(searchPly)
        dec a
        ld (searchPly),a
        call unmakeMove
        ld de,(nmScore)
        call ptrBest
        ld c,(hl)
        inc hl
        ld b,(hl)
        call cmpDEgtBC
        jp nc,nmLoop             ; score <= best
        call ptrBest
        ld (hl),e
        inc hl
        ld (hl),d
        ld a,(searchPly)
        or a
        jr nz,nmTryA
        ld a,(rootFrom)
        ld (bestFrom),a
        ld a,(rootTo)
        ld (bestTo),a
        ld a,(rootFlag)
        ld (bestFlag),a
nmTryA:
        ld de,(nmScore)
        call ptrAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)
        call cmpDEgtBC
        jp nc,nmLoop             ; score <= alpha
        ld de,(nmScore)
        call ptrAlpha
        ld (hl),e
        inc hl
        ld (hl),d                ; alpha = score
        call ptrAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)                ; BC = alpha
        call ptrBeta
        ld e,(hl)
        inc hl
        ld d,(hl)                ; DE = beta
        call cmpDEgtBC
        jp c,nmLoop              ; beta > alpha -> keep going
        jp nmRetBest             ; alpha >= beta -> cutoff
nmDone:
        call ptrLegal
        ld a,(hl)
        or a
        jp nz,nmRetBest
        ld a,(sideToMove)
        call inCheckSide
        or a
        jp z,nmStale
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
;  quiesce -> HL=score.  Stand-pat + capture/promotion search to settle
;  the position before evaluating (kills the horizon effect).
; =====================================================================
quiesce:
        ld a,(searchPly)
        cp MAXPLY
        jp nc,eval               ; too deep -> static eval
        call eval
        ld (qStand),hl
        ; if stand >= beta -> return beta
        ld de,(qStand)
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)
        push bc
        push de
        pop bc                   ; BC = stand
        pop de                   ; DE = beta
        call cmpDEgtBC           ; beta > stand ?
        jr c,qNoFH
        call ptrBeta
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        ret                      ; return beta
qNoFH:
        ; if stand > alpha -> alpha = stand
        ld de,(qStand)
        call ptrAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)
        call cmpDEgtBC
        jr nc,qGen
        ld de,(qStand)
        call ptrAlpha
        ld (hl),e
        inc hl
        ld (hl),d
qGen:
        call genMoves
        call orderMoves
        ld a,(genCount)
        ld b,a
        call ptrCnt
        ld (hl),b
        call curMoveBuf
        ex de,hl
        call ptrMptr
        ld (hl),e
        inc hl
        ld (hl),d
qLoop:
        call ptrCnt
        ld a,(hl)
        or a
        jp z,qRetA
        dec a
        ld (hl),a
        call ptrMptr
        ld a,(hl)
        ld e,a
        inc hl
        ld a,(hl)
        ld d,a
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
        ; only captures / promotions
        ld a,(mvTo)
        ld l,a
        ld h,0xE0
        ld a,(hl)
        or a
        jr nz,qIsCap
        ld a,(mvFlag)
        and 7
        cp SP_EP
        jr z,qIsCap
        ld a,(mvFlag)
        and 0xF0
        jp z,qLoop               ; quiet, non-promotion -> skip
qIsCap:
        call makeMove
        call moverInCheck
        or a
        jr z,qLegal
        call unmakeMove
        jp qLoop
qLegal:
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)
        ld hl,0
        or a
        sbc hl,bc
        push hl                  ; childAlpha = -beta
        call ptrAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)
        ld hl,0
        or a
        sbc hl,bc
        push hl                  ; childBeta = -alpha
        ld a,(searchPly)
        inc a
        ld (searchPly),a
        pop de
        call ptrBeta
        ld (hl),e
        inc hl
        ld (hl),d
        pop de
        call ptrAlpha
        ld (hl),e
        inc hl
        ld (hl),d
        call quiesce
        ex de,hl
        ld hl,0
        or a
        sbc hl,de
        ld (nmScore),hl
        ld a,(searchPly)
        dec a
        ld (searchPly),a
        call unmakeMove
        ld de,(nmScore)
        ; if score >= beta -> return beta
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)
        push bc
        push de
        pop bc                   ; BC = score
        pop de                   ; DE = beta
        call cmpDEgtBC           ; beta > score ?
        jr c,qNoCut
        call ptrBeta
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        ret
qNoCut:
        ld de,(nmScore)
        call ptrAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)
        call cmpDEgtBC
        jp nc,qLoop
        ld de,(nmScore)
        call ptrAlpha
        ld (hl),e
        inc hl
        ld (hl),d
        jp qLoop
qRetA:
        call ptrAlpha
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        ret

; =====================================================================
;  move ordering — MVV-LVA score per move, then selection sort.
; =====================================================================
scoreMoveHL:                     ; HL=record; stores score byte at [HL+3]
        push hl
        ld a,(hl)
        ld e,a                   ; from
        inc hl
        ld a,(hl)
        ld d,a                   ; to
        inc hl
        ld a,(hl)
        ld c,a                   ; flag
        push hl
        ld h,0xE0
        ld l,d
        ld a,(hl)
        and 7
        ld b,a                   ; victim type
        pop hl
        ld a,c
        and 7
        cp SP_EP
        jr nz,smv1
        ld b,1                   ; ep captures a pawn
smv1:
        ld a,c
        and 0xF0
        ld c,a                   ; promo flag
        ld a,b
        or a
        jr nz,smvCap
        ld a,c
        or a
        jr nz,smvCap
        xor a                    ; quiet move
        jr smvStore
smvCap:
        ld a,b
        add a,a
        add a,a
        add a,a                  ; victim*8
        add a,0x80
        ld b,a
        push hl
        ld h,0xE0
        ld l,e
        ld a,(hl)
        and 7
        pop hl
        ld d,a                   ; attacker type
        ld a,b
        sub d                    ; victim-weighted minus attacker
        ld d,a
        ld a,c
        or a
        ld a,d
        jr z,smvStore
        add a,8                  ; promotion bonus
smvStore:
        pop hl
        push hl
        inc hl
        inc hl
        inc hl
        ld (hl),a
        pop hl
        ret

orderMoves:
        ld a,(genCount)
        cp 2
        ret c
        call curMoveBuf
        ld a,(genCount)
        ld b,a
osScore:
        push bc
        push hl
        call scoreMoveHL
        pop hl
        pop bc
        ld de,4
        add hl,de
        djnz osScore
        call curMoveBuf
        ld (osIPtr),hl
        ld a,(genCount)
        dec a
        ld (osOuter),a
osOut:
        ld a,(osOuter)
        or a
        ret z
        ld (osInner),a
        ld hl,(osIPtr)
        ld (osMaxPtr),hl
        ld de,3
        add hl,de
        ld a,(hl)
        ld (osMaxScore),a
        ld hl,(osIPtr)
        ld de,4
        add hl,de
        ld (osJPtr),hl
osIn:
        ld hl,(osJPtr)
        ld de,3
        add hl,de
        ld a,(hl)
        ld hl,osMaxScore
        cp (hl)
        jr c,osInNext
        jr z,osInNext
        ld (osMaxScore),a
        ld hl,(osJPtr)
        ld (osMaxPtr),hl
osInNext:
        ld hl,(osJPtr)
        ld de,4
        add hl,de
        ld (osJPtr),hl
        ld a,(osInner)
        dec a
        ld (osInner),a
        jr nz,osIn
        ; swap record iPtr <-> maxPtr if different
        ld hl,(osIPtr)
        ld de,(osMaxPtr)
        ld a,h
        cp d
        jr nz,osSwap
        ld a,l
        cp e
        jr z,osNoSwap
osSwap:
        ld b,4
osSw1:
        ld a,(hl)
        ld c,a
        ld a,(de)
        ld (hl),a
        ld a,c
        ld (de),a
        inc hl
        inc de
        djnz osSw1
osNoSwap:
        ld hl,(osIPtr)
        ld de,4
        add hl,de
        ld (osIPtr),hl
        ld a,(osOuter)
        dec a
        ld (osOuter),a
        jp osOut

; =====================================================================
;  aiMove — search from the root and play the best move.
; =====================================================================
aiMove:
        xor a
        ld (searchPly),a
        ld bc,-INF
        call ptrAlpha
        ld (hl),c
        inc hl
        ld (hl),b
        ld bc,INF
        call ptrBeta
        ld (hl),c
        inc hl
        ld (hl),b
        ld a,0xFF
        ld (bestFrom),a
        ld a,(aiDepth)
        call negamax
        ld a,(bestFrom)
        cp 0xFF
        ret z
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
