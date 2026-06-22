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

; setChildWindow — at the child ply, alpha = -parentBeta, beta = -parentAlpha
setChildWindow:
        ld de,(lmrPB)
        ld hl,0
        or a
        sbc hl,de
        ex de,hl
        call setAlphaDE
        ld de,(lmrPA)
        ld hl,0
        or a
        sbc hl,de
        ex de,hl
        call setBetaDE
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
;  Material + piece-square tables, with a tapered king table (centralise
;  in the endgame), a bishop-pair bonus, and doubled/isolated pawn
;  penalties.
; =====================================================================
; non-pawn material weight by piece type (0=empty..6=K)
phaseWtTbl: defb 0,0,1,1,2,4,0

; phaseDelta -> A = (promotion weight) - (captured weight), for the move in
; (mvFlag)/(mkCaptured).  make adds it to gamePhase; unmake subtracts it.
phaseDelta:
        ld a,(mvFlag)
        rrca
        rrca
        rrca
        rrca
        and 0x0F                ; promotion piece type (0 = none)
        ld e,a
        ld d,0
        ld hl,phaseWtTbl
        add hl,de
        ld c,(hl)               ; promotion weight
        ld a,(mkCaptured)
        and 7
        ld e,a
        ld hl,phaseWtTbl
        add hl,de
        ld a,(hl)               ; captured weight
        ld b,a
        ld a,c
        sub b
        ret

; pieceValSigned(B=piece, C=square) -> HL = signed white-relative value
; (material + piece-square), 0 for a king or empty square.
pieceValSigned:
        ld a,b
        or a
        jr z,pvsZero
        and 7
        cp 6
        jr z,pvsZero
        add a,a
        ld e,a
        ld d,0
        ld hl,materialTbl
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (matVal),de
        ld a,c
        and 0x70
        rrca
        ld e,a
        ld a,c
        and 7
        or e
        ld e,a
        ld a,b
        and 8
        jr z,pvsIdx
        ld a,e
        xor 0x38
        ld e,a
pvsIdx:
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
        add hl,hl
        ld d,0
        add hl,de
        ld de,pstTables
        add hl,de
        ld a,(hl)
        ld l,a
        rla
        sbc a,a
        ld h,a
        ld de,(matVal)
        add hl,de
        ld a,b
        and 8
        ret z
        ex de,hl
        ld hl,0
        or a
        sbc hl,de
        ret
pvsZero:
        ld hl,0
        ret

; kingPstSigned(B=king piece, C=square) -> HL signed tapered king-PST value
kingPstSigned:
        ld a,(gamePhase)
        cp PHASE_EG
        jr nc,kpsMg
        ld hl,pstKingEG
        jr kpsTbl
kpsMg:  ld hl,pstTables+5*64
kpsTbl:
        ld a,c
        and 0x70
        rrca
        ld e,a
        ld a,c
        and 7
        or e
        ld e,a
        ld a,b
        and 8
        jr z,kpsIdx
        ld a,e
        xor 0x38
        ld e,a
kpsIdx:
        ld d,0
        add hl,de
        ld a,(hl)
        ld l,a
        rla
        sbc a,a
        ld h,a
        ld a,b
        and 8
        ret z
        ex de,hl
        ld hl,0
        or a
        sbc hl,de
        ret

; addKingPst — add both kings' tapered PST to evalAcc
addKingPst:
        ld a,(wking)
        ld c,a
        ld b,WK
        call kingPstSigned
        ld de,(evalAcc)
        add hl,de
        ld (evalAcc),hl
        ld a,(bking)
        ld c,a
        ld b,BK
        call kingPstSigned
        ld de,(evalAcc)
        add hl,de
        ld (evalAcc),hl
        ret

; computePstScore — rebuild pstScore from scratch (seed + self-test)
computePstScore:
        ld hl,0
        ld (pstScore),hl
        ld c,0
cpsLoop:
        ld a,c
        and 0x88
        jr nz,cpsNext
        ld h,0xE0
        ld l,c
        ld a,(hl)
        or a
        jr z,cpsNext
        ld b,a
        push bc
        call pieceValSigned
        pop bc
        ld de,(pstScore)
        add hl,de
        ld (pstScore),hl
cpsNext:
        inc c
        ld a,c
        cp 0x78
        jr nz,cpsLoop
        ret

; pstDelta -> DE = change to pstScore for the move in mk*/(mvFrom/mvTo/mvFlag)
pstDelta:
        ld a,(mkPromo)
        or a
        jr z,pdNoProm
        ld b,a
        ld a,(mkSide)
        or b
        jr pdPlaced
pdNoProm:
        ld a,(mkPiece)
pdPlaced:
        ld b,a
        ld a,(mvTo)
        ld c,a
        call pieceValSigned
        ld (pdAcc),hl
        ld a,(mkPiece)
        ld b,a
        ld a,(mvFrom)
        ld c,a
        call pieceValSigned
        ld de,(pdAcc)
        ex de,hl
        or a
        sbc hl,de
        ld (pdAcc),hl
        ld a,(mkCaptured)
        ld b,a
        ld a,(mkCapSq)
        ld c,a
        call pieceValSigned
        ld de,(pdAcc)
        ex de,hl
        or a
        sbc hl,de
        ld (pdAcc),hl
        ld a,(mkSpecial)
        cp SP_OO
        jr z,pdOO
        cp SP_OOO
        jr z,pdOOO
        jr pdDone
pdOO:
        ld a,(mkSide)
        or a
        jr nz,pdOOb
        ld a,0x07
        ld (pdRookFrom),a
        ld a,0x05
        ld (pdRookTo),a
        jr pdRook
pdOOb:
        ld a,0x77
        ld (pdRookFrom),a
        ld a,0x75
        ld (pdRookTo),a
        jr pdRook
pdOOO:
        ld a,(mkSide)
        or a
        jr nz,pdOOOb
        ld a,0x00
        ld (pdRookFrom),a
        ld a,0x03
        ld (pdRookTo),a
        jr pdRook
pdOOOb:
        ld a,0x70
        ld (pdRookFrom),a
        ld a,0x73
        ld (pdRookTo),a
pdRook:
        ld a,(mkSide)
        or WR
        ld b,a
        ld a,(pdRookTo)
        ld c,a
        call pieceValSigned
        ld (pdTmp),hl
        ld a,(mkSide)
        or WR
        ld b,a
        ld a,(pdRookFrom)
        ld c,a
        call pieceValSigned
        ld de,(pdTmp)
        ex de,hl
        or a
        sbc hl,de
        ld de,(pdAcc)
        add hl,de
        ld (pdAcc),hl
pdDone:
        ld de,(pdAcc)
        ret

; computePhase — full scan, used to seed gamePhase and to self-test it
computePhase:
        ld c,0
        ld b,0                  ; phase
cpLoop:
        ld a,c
        and 0x88
        jr nz,cpNext
        ld h,0xE0
        ld l,c
        ld a,(hl)
        and 7
        cp 2
        jr z,cpMinor
        cp 3
        jr z,cpMinor
        cp 4
        jr z,cpRook
        cp 5
        jr z,cpQueen
        jr cpNext
cpMinor:
        inc b
        jr cpNext
cpRook:
        inc b
        inc b
        jr cpNext
cpQueen:
        ld a,b
        add a,4
        ld b,a
cpNext:
        inc c
        ld a,c
        cp 0x78
        jr nz,cpLoop
        ld a,b
        ld (gamePhase),a
        ret

eval:
        ld hl,(pstScore)         ; incremental material + PST (non-king)
        ld (evalAcc),hl
        call addKingPst          ; + tapered king tables
        xor a
        ld (wBish),a
        ld (bBish),a
        ld (wAny),a
        ld (bAny),a
        ld (wHeavy),a
        ld (bHeavy),a
        ld hl,wpFile
        ld b,16
ezf:    ld (hl),0
        inc hl
        djnz ezf
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
        ; --- bishop / pawn-file statistics ---
        ld a,b
        and 7
        cp 3
        jr nz,evNotB
        ld a,b
        and 8
        jr nz,evBb
        ld hl,wBish
        inc (hl)
        jr evStat
evBb:   ld hl,bBish
        inc (hl)
        jr evStat
evNotB:
        cp 1
        jr nz,evStat
        ld a,(tmpSq)
        and 7
        ld e,a
        ld d,0
        ld a,b
        and 8
        jr nz,evBp
        ld hl,wpFile
        add hl,de
        inc (hl)
        jr evStat
evBp:   ld hl,bpFile
        add hl,de
        inc (hl)
evStat:
        ; --- track lone-king material flags (for mating drive) ---
        ld a,b
        and 7
        cp 6
        jr z,evMatDone           ; king
        ld a,b
        and 8
        jr nz,evMatB
        ld a,1
        ld (wAny),a
        ld a,b
        and 7
        cp 4
        jr c,evMatDone
        ld a,1
        ld (wHeavy),a            ; rook or queen
        jr evMatDone
evMatB:
        ld a,1
        ld (bAny),a
        ld a,b
        and 7
        cp 4
        jr c,evMatDone
        ld a,1
        ld (bHeavy),a
evMatDone:
        ; material + piece-square score comes from the incremental pstScore
        ; (non-king pieces) plus the two kings, added once below.
evNext:
        ld a,(tmpSq)
        inc a
        cp 0x78
        jp nz,evLoop
        ; --- bishop pair ---
        ld a,(wBish)
        cp 2
        jr c,evBpb
        ld hl,(evalAcc)
        ld de,BISHOP_PAIR
        add hl,de
        ld (evalAcc),hl
evBpb:  ld a,(bBish)
        cp 2
        jr c,evPawns
        ld hl,(evalAcc)
        ld de,BISHOP_PAIR
        or a
        sbc hl,de
        ld (evalAcc),hl
        ; --- doubled / isolated pawns ---
evPawns:
        ld c,0                  ; file
evPf:
        ; white doubled
        ld b,0
        ld a,c
        ld e,a
        ld d,0
        ld hl,wpFile
        add hl,de
        ld a,(hl)
        cp 2
        jr c,evPwIso
        ld hl,(evalAcc)
        ld de,DOUBLED
        or a
        sbc hl,de
        ld (evalAcc),hl
evPwIso:
        ld a,c
        ld e,a
        ld d,0
        ld hl,wpFile
        add hl,de
        ld a,(hl)
        or a
        jr z,evPb
        ld a,c
        call hasNeighborW
        or a
        jr nz,evPb
        ld hl,(evalAcc)
        ld de,ISOLATED
        or a
        sbc hl,de
        ld (evalAcc),hl
evPb:
        ; black doubled
        ld a,c
        ld e,a
        ld d,0
        ld hl,bpFile
        add hl,de
        ld a,(hl)
        cp 2
        jr c,evPbIso
        ld hl,(evalAcc)
        ld de,DOUBLED
        add hl,de
        ld (evalAcc),hl
evPbIso:
        ld a,c
        ld e,a
        ld d,0
        ld hl,bpFile
        add hl,de
        ld a,(hl)
        or a
        jr z,evPnext
        ld a,c
        call hasNeighborB
        or a
        jr nz,evPnext
        ld hl,(evalAcc)
        ld de,ISOLATED
        add hl,de
        ld (evalAcc),hl
evPnext:
        inc c
        ld a,c
        cp 8
        jp nz,evPf
        call passedPawns
        ; --- king safety (pawn shield), middlegame only ---
        ld a,(gamePhase)
        cp 10
        jr c,evKsDone
        ld a,(wking)
        ld c,WP
        call shieldMissing       ; A = missing shield pawns (0..3)
        call ksPenaltyW          ; evalAcc -= A*KSHIELD
        ld a,(bking)
        ld c,BP
        call shieldMissing
        call ksPenaltyB          ; evalAcc += A*KSHIELD (penalise black)
evKsDone:
        call matingEval
        ; --- side relative ---
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

; matingEval — when one side is a lone king and the other has a rook or
; queen, drive the lone king to a corner and the attacker's king toward
; it (so the search converts KQK / KRK / KQRK etc. cleanly).
matingEval:
        ld a,(bAny)
        or a
        jr nz,meTryW
        ld a,(wHeavy)
        or a
        ret z
        ; black lone king, white winning -> bonus for white
        ld a,(bking)
        call centerDist          ; A = 0..6 (push black king out)
        add a,a
        add a,a
        add a,a
        add a,a                  ; *16
        ld c,a
        call kingProx            ; A = 0..7 (white king closer = bigger)
        add a,a
        add a,a                  ; *4
        add a,c
        ld e,a
        ld d,0
        ld hl,(evalAcc)
        add hl,de
        ld (evalAcc),hl
        ret
meTryW:
        ld a,(wAny)
        or a
        ret nz
        ld a,(bHeavy)
        or a
        ret z
        ld a,(wking)
        call centerDist
        add a,a
        add a,a
        add a,a
        add a,a
        ld c,a
        call kingProx
        add a,a
        add a,a
        add a,c
        ld e,a
        ld d,0
        ld hl,(evalAcc)
        or a
        sbc hl,de
        ld (evalAcc),hl
        ret

; centerDist(A=square) -> A = distance from the centre (0 centre .. 6 corner)
centerDist:
        ld c,a
        and 7                    ; file
        cp 4
        jr nc,cdFhi
        ld b,a
        ld a,3
        sub b                    ; 3-file
        jr cdFok
cdFhi:  sub 4                    ; file-4
cdFok:  ld b,a                   ; file distance
        ld a,c
        rrca
        rrca
        rrca
        rrca
        and 7                    ; rank
        cp 4
        jr nc,cdRhi
        ld c,a
        ld a,3
        sub c
        jr cdRok
cdRhi:  sub 4
cdRok:  add a,b
        ret

; kingProx -> A = 7 - Chebyshev(wking,bking)  (closer kings -> larger)
kingProx:
        ld a,(wking)
        and 7
        ld b,a
        ld a,(bking)
        and 7
        sub b
        jr nc,kpF
        neg
kpF:    ld c,a                   ; |file diff|
        ld a,(wking)
        rrca
        rrca
        rrca
        rrca
        and 7
        ld b,a
        ld a,(bking)
        rrca
        rrca
        rrca
        rrca
        and 7
        sub b
        jr nc,kpR
        neg
kpR:    cp c
        jr nc,kpMax
        ld a,c                   ; max(|df|,|dr|)
kpMax:  ld b,a
        ld a,7
        sub b
        ret

; passedPawns — bonus for pawns with no enemy pawns on their file or the
; adjacent files (a safe subset of "passed"), scaled by advancement.
PASSED equ 8
passedPawns:
        ld a,(gamePhase)
        cp 12
        ret nc                  ; middlegame: skip (passers matter in endgames)
        ld c,0                  ; square
ppLoop:
        ld a,c
        and 0x88
        jr nz,ppNext
        ld h,0xE0
        ld l,c
        ld a,(hl)
        ld b,a                  ; piece
        and 7
        cp 1
        jr nz,ppNext            ; not a pawn
        ld a,b
        and 8
        jr nz,ppBlack
        ; white pawn: clear if no black pawns on f-1,f,f+1
        ld a,c
        and 7
        push bc                 ; adjBlackPawns clobbers C (the loop square)
        call adjBlackPawns
        pop bc
        or a
        jr nz,ppNext
        ; bonus = PASSED * rank
        ld a,c
        rrca
        rrca
        rrca
        rrca
        and 7                   ; rank
        call ppMul
        ld hl,(evalAcc)
        add hl,de
        ld (evalAcc),hl
        jr ppNext
ppBlack:
        ld a,c
        and 7
        push bc                 ; adjWhitePawns clobbers C (the loop square)
        call adjWhitePawns
        pop bc
        or a
        jr nz,ppNext
        ld a,c
        rrca
        rrca
        rrca
        rrca
        and 7
        ld e,a
        ld a,7
        sub e                   ; (7-rank): black advances downward
        call ppMul
        ld hl,(evalAcc)
        or a
        sbc hl,de
        ld (evalAcc),hl
ppNext:
        inc c
        ld a,c
        cp 0x78
        jr nz,ppLoop
        ret
; ppMul(A=rank) -> DE = PASSED*rank
ppMul:
        ld e,a
        ld d,0
        ld hl,0
        ld b,PASSED
        or a
        ret z
ppMulL: add hl,de
        djnz ppMulL
        ex de,hl
        ret
; adjBlackPawns(A=file) -> A=1 if any black pawn on file-1,file,file+1
adjBlackPawns:
        ld c,a
        ld hl,bpFile
        ld d,0
        ld e,a
        add hl,de
        ld a,(hl)
        or a
        jr nz,apYes
        ld a,c
        or a
        jr z,apbR
        dec hl
        ld a,(hl)
        or a
        jr nz,apYes
apbR:
        ld a,c
        cp 7
        jr z,apNo
        ld hl,bpFile
        ld d,0
        ld e,c
        add hl,de
        inc hl
        ld a,(hl)
        or a
        jr nz,apYes
apNo:   xor a
        ret
apYes:  ld a,1
        ret
adjWhitePawns:
        ld c,a
        ld hl,wpFile
        ld d,0
        ld e,a
        add hl,de
        ld a,(hl)
        or a
        jr nz,apYes
        ld a,c
        or a
        jr z,apwR
        dec hl
        ld a,(hl)
        or a
        jr nz,apYes
apwR:
        ld a,c
        cp 7
        jr z,apwN
        ld hl,wpFile
        ld d,0
        ld e,c
        add hl,de
        inc hl
        ld a,(hl)
        or a
        jr nz,apYes
apwN:   xor a
        ret

; shieldMissing(A=kingSq, C=friendly pawn code) -> A = missing pawns in
; the three squares directly in front of the king (white: +; black: -).
shieldMissing:
        ld (tmpSq),a
        ld a,c
        and 8                    ; colour -> direction
        jr z,smWhite
        ld d,-16                 ; black shield is below
        jr smGo
smWhite:
        ld d,16
smGo:
        ld b,0                   ; missing count
        ld e,-1                  ; offset within {-1,0,+1} added to dir
smLoop:
        ld a,(tmpSq)
        add a,d                  ; king + rank dir
        add a,e                  ; + file offset
        ld h,a
        and 0x88
        jr nz,smMiss             ; off board counts as missing
        ld a,h
        ld l,a
        ld h,0xE0
        ld a,(hl)
        cp c
        jr z,smNext
smMiss:
        inc b
smNext:
        inc e
        ld a,e
        cp 2
        jr nz,smLoop
        ld a,b
        ret

KSHIELD equ 10
ksPenaltyW:                      ; evalAcc -= A*KSHIELD
        or a
        ret z
        ld b,a
        ld de,KSHIELD
        ld hl,(evalAcc)
kspW1:  or a
        sbc hl,de
        djnz kspW1
        ld (evalAcc),hl
        ret
ksPenaltyB:                      ; evalAcc += A*KSHIELD
        or a
        ret z
        ld b,a
        ld de,KSHIELD
        ld hl,(evalAcc)
kspB1:  add hl,de
        djnz kspB1
        ld (evalAcc),hl
        ret

; hasNeighborW(A=file) -> A=1 if a white pawn exists on file-1 or file+1
hasNeighborW:
        ld c,a
        or a
        jr z,hnwR
        dec a
        ld e,a
        ld d,0
        ld hl,wpFile
        add hl,de
        ld a,(hl)
        or a
        jr nz,hnY
hnwR:
        ld a,c
        cp 7
        jr z,hnN
        inc a
        ld e,a
        ld d,0
        ld hl,wpFile
        add hl,de
        ld a,(hl)
        or a
        jr nz,hnY
hnN:    xor a
        ret
hnY:    ld a,1
        ret
hasNeighborB:
        ld c,a
        or a
        jr z,hnbR
        dec a
        ld e,a
        ld d,0
        ld hl,bpFile
        add hl,de
        ld a,(hl)
        or a
        jr nz,hnY
hnbR:
        ld a,c
        cp 7
        jr z,hnN
        inc a
        ld e,a
        ld d,0
        ld hl,bpFile
        add hl,de
        ld a,(hl)
        or a
        jr nz,hnY
        xor a
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
        ; --- transposition table probe ---
        ld a,b
        call ttProbe
        or a
        jr z,ttpNoCut
        ; usable bound -> cut.  But never cut at the root (searchPly 0): the
        ; root must search a move so aiMove has something to play (else it
        ; returns no move and the game records a phantom / false draw).
        ld a,(searchPly)
        or a
        jr z,ttpNoCut
        ret                      ; usable bound -> HL = score
ttpNoCut:
        ; stash this node's TT move per-ply (recursion clobbers ttFrom)
        ld a,(ttFrom)
        ld c,a
        ld a,(ttTo)
        ld b,a
        call ptrTtMvFrom
        ld (hl),c
        call ptrTtMvTo
        ld (hl),b
        ld a,(ttCurDepth)
        ld b,a                   ; restore B = depth (ttProbe clobbered it)
        ; --- reverse futility: at shallow depth, if a static eval already
        ;     beats beta by a depth-scaled margin, prune (return eval) ---
        ld a,b
        cp 3
        jr nc,noRevFut
        ld a,(sideToMove)
        call inCheckSide
        or a
        jr nz,noRevFut
        call eval
        ld (rfEval),hl
        ld a,(ttCurDepth)
        ld b,a
        ld de,110
        ld hl,0
rfMarg: add hl,de
        djnz rfMarg              ; HL = depth*110
        ex de,hl
        ld hl,(rfEval)
        or a
        sbc hl,de                ; HL = eval - margin
        ld d,h
        ld e,l                   ; DE = threshold
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)                ; BC = beta
        push bc
        push de
        pop bc                   ; BC = threshold
        pop de                   ; DE = beta
        call cmpDEgtBC           ; beta > threshold ?
        jr c,noRevFut
        ld hl,(rfEval)           ; prune: fail-high, return eval
        ret
noRevFut:
        ; --- null-move pruning (depth>=3, not in check, has pieces) ---
        ld a,(ttCurDepth)
        ld b,a
        cp 3
        jp c,noNull
        ld a,(sideToMove)
        call inCheckSide
        or a
        jp nz,noNull
        ld a,(gamePhase)         ; maintained incrementally
        cp 3
        jp c,noNull
        ld a,(epSquare)
        ld (nullEp),a
        ld a,0xFF
        ld (epSquare),a
        ld a,(sideToMove)
        xor 8
        ld (sideToMove),a
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)                ; BC = beta
        ld hl,0
        or a
        sbc hl,bc                ; -beta
        push hl                  ; childAlpha
        inc hl                   ; -beta+1
        push hl                  ; childBeta
        call ptrDepth
        ld a,(hl)
        sub 3                    ; depth-1-R, R=2
        ld (nmCd),a
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
        ld a,(nmCd)
        call negamax
        ex de,hl
        ld hl,0
        or a
        sbc hl,de
        ld (nmScore),hl
        ld a,(searchPly)
        dec a
        ld (searchPly),a
        ld a,(sideToMove)
        xor 8
        ld (sideToMove),a
        ld a,(nullEp)
        ld (epSquare),a
        ld de,(nmScore)
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)
        push bc
        push de
        pop bc                   ; BC = score
        pop de                   ; DE = beta
        call cmpDEgtBC           ; beta > score ?
        jr c,noNull
        call ptrBeta             ; score >= beta -> fail-high, return beta
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        ret
noNull:
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
        ; save original alpha (for the TT bound flag)
        call ptrAlpha
        ld e,(hl)
        inc hl
        ld d,(hl)
        call ptrOrigAlpha
        ld (hl),e
        inc hl
        ld (hl),d
        ; is the side to move in check at this node? (LMR skips reductions)
        ld a,(sideToMove)
        call inCheckSide
        push af
        call ptrInChk
        pop af
        ld (hl),a
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
        ; capture parent alpha/beta (for the child window and LMR re-search)
        call ptrAlpha
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (lmrPA),de
        call ptrBeta
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (lmrPB),de
        ; --- LMR: reduce late, quiet moves at depth>=3 when not in check ---
        xor a
        ld (lmrReduced),a
        call ptrDepth
        ld a,(hl)
        cp 3
        jr c,lmrFull
        call ptrLegal
        ld a,(hl)
        cp 4
        jr c,lmrFull             ; first three legal moves: full depth
        ld a,(mkCaptured)
        or a
        jr nz,lmrFull
        ld a,(mkPromo)
        or a
        jr nz,lmrFull
        call ptrInChk
        ld a,(hl)
        or a
        jr nz,lmrFull
        call ptrDepth
        ld a,(hl)
        sub 2
        ld (nmCd),a              ; reduced depth
        ld a,1
        ld (lmrReduced),a
        jr lmrGo
lmrFull:
        call ptrDepth
        ld a,(hl)
        dec a
        ld (nmCd),a
lmrGo:
        ld a,(searchPly)
        inc a
        ld (searchPly),a
        call setChildWindow
        ld a,(nmCd)
        call negamax
        ex de,hl
        ld hl,0
        or a
        sbc hl,de                ; score = -child
        ld (nmScore),hl
        ; LMR re-search at full depth if the reduced search beat alpha
        ld a,(lmrReduced)
        or a
        jr z,lmrNoRe
        ld de,(nmScore)
        ld bc,(lmrPA)
        call cmpDEgtBC           ; score > parentAlpha ?
        jr nc,lmrNoRe
        call setChildWindow      ; first search dirtied the child window
        ld a,(nmCd)
        inc a                    ; full depth = reduced + 1
        call negamax
        ex de,hl
        ld hl,0
        or a
        sbc hl,de
        ld (nmScore),hl
lmrNoRe:
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
        ; record this node's best move (mvFrom/mvTo restored by unmakeMove)
        call ptrNbFrom
        ld a,(mvFrom)
        ld (hl),a
        call ptrNbTo
        ld a,(mvTo)
        ld (hl),a
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
        call updateKiller        ; alpha >= beta -> cutoff; record killer
        jp nmRetBest
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
        ; --- store this node in the TT ---
        call ptrBest
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (bestScoreTmp),de     ; DE = best
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)
        push bc
        push de
        pop bc                   ; BC = best
        pop de                   ; DE = beta
        call cmpDEgtBC           ; beta > best ?
        jr c,ttfNL
        ld a,1                   ; best >= beta -> lower bound
        jr ttfStore
ttfNL:
        ld de,(bestScoreTmp)
        call ptrOrigAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)
        call cmpDEgtBC           ; best > origAlpha ?
        jr c,ttfEx
        ld a,2                   ; fail-low -> upper bound
        jr ttfStore
ttfEx:
        xor a                    ; exact
ttfStore:
        call ttStore
        ld hl,(bestScoreTmp)
        ret

; =====================================================================
;  quiesce -> HL=score.  Stand-pat + capture/promotion search to settle
;  the position before evaluating (kills the horizon effect).
; =====================================================================
quiesce:
        ld a,(searchPly)
        cp MAXPLY
        jp nc,eval               ; too deep -> static eval
        call ptrTtMvFrom         ; no TT move in quiescence
        ld (hl),0xFF
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
        ; quiet move: score from the history heuristic (0..63)
        push hl
        ld h,0xE0
        ld l,e
        ld a,(hl)
        and 7
        dec a                    ; type-1
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl                ; *64
        ld a,d
        and 0x70
        rrca
        ld c,a
        ld a,d
        and 7
        or c                     ; to64
        ld c,a
        ld b,0
        add hl,bc
        ld bc,historyTbl
        add hl,bc
        ld a,(hl)
        pop hl
        srl a
        srl a                    ; /4 -> 0..63
        jr smvStore
smvCap:                          ; b=victim type, c=promo flag, d=to, e=from
        ; SEE-lite: is this a losing capture (heavier piece takes lighter on
        ; a defended square)?  Compute it now, before the registers are reused.
        push hl
        ld h,0xE0
        ld l,e
        ld a,(hl)                ; piece on `from`
        and 7
        pop hl
        push bc                  ; save victim(b) + promo(c)
        push de                  ; save to(d) + from(e)
        ld c,a                   ; C = attacker type, B = victim type
        call seeIsBadCapture     ; CF=1 if it loses material on a defended sq
        ld a,0
        jr nc,smvSeeOk
        inc a
smvSeeOk:
        ld (seeBad),a
        pop de
        pop bc
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
        sub d                    ; victim*8 + 0x80 - attacker
        ld d,a
        ld a,c
        or a
        ld a,d
        jr z,smvCapP
        add a,8                  ; promotion bonus
smvCapP:
        ld c,a
        ld a,(seeBad)
        or a
        ld a,c
        jr z,smvStore
        sub 0x80                 ; losing capture: drop below the good captures
smvStore:
        pop hl
        push hl
        inc hl
        inc hl
        inc hl
        ld (hl),a
        pop hl
        ret

; seeIsBadCapture(B=victim type, C=attacker type, D=to) -> CF=1 if
; materialTbl[attacker] > materialTbl[victim] AND `to` is defended by the
; side not to move (a likely material-losing capture).  Clobbers all regs.
seeIsBadCapture:
        ld a,d
        ld (seeTo),a
        ; HL = materialTbl[attacker]
        ld a,c
        add a,a
        ld l,a
        ld h,0
        ld de,materialTbl
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a                   ; HL = attacker value
        push hl
        ; DE = materialTbl[victim]
        ld a,b
        add a,a
        ld l,a
        ld h,0
        ld de,materialTbl
        add hl,de
        ld a,(hl)
        inc hl
        ld d,(hl)
        ld e,a                   ; DE = victim value
        pop hl                   ; HL = attacker value
        or a
        sbc hl,de                ; attacker - victim
        jr z,sicNo               ; equal trade -> fine
        jr c,sicNo               ; attacker cheaper -> winning capture, fine
        ; trading down: is `to` defended by the opponent?
        ld a,(sideToMove)
        xor 8
        ld (atkSide),a
        ld a,(seeTo)
        call isAttacked          ; A=1 if defended
        or a
        jr z,sicNo
        scf
        ret
sicNo:  or a
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
        ; PV-first: at the root, force the previous iteration's best move
        ; to the top of the order (drives iterative deepening).
        ld a,(searchPly)
        or a
        jr nz,osNoPv
        ld a,(pvFrom)
        cp 0xFF
        jr z,osNoPv
        call curMoveBuf
        ld a,(genCount)
        ld b,a
osPvL:  ld a,(pvFrom)
        cp (hl)
        jr nz,osPvN
        inc hl
        ld a,(pvTo)
        cp (hl)
        jr nz,osPvN2
        inc hl
        inc hl
        ld (hl),0xFF
        jr osNoPv
osPvN:  inc hl
osPvN2: inc hl
        inc hl
        inc hl
        djnz osPvL
osNoPv:
        ; TT-move bump (any ply): give the stored best move a high score
        call ptrTtMvFrom
        ld a,(hl)
        ld (osTtF),a
        call ptrTtMvTo
        ld a,(hl)
        ld (osTtT),a
        ld a,(osTtF)
        cp 0xFF
        jr z,osNoTt
        call curMoveBuf
        ld a,(genCount)
        ld b,a
osTtL:  ld a,(osTtF)
        cp (hl)
        jr nz,osTtN
        inc hl
        ld a,(osTtT)
        cp (hl)
        jr nz,osTtN2
        inc hl
        inc hl
        ld (hl),0xFE
        jr osNoTt
osTtN:  inc hl
osTtN2: inc hl
        inc hl
        inc hl
        djnz osTtL
osNoTt:
        ; killer-move bump (quiet moves that caused cutoffs at this ply)
        call ptrKiller
        ld a,(hl)
        ld (osKF),a
        inc hl
        ld a,(hl)
        ld (osKT),a
        ld a,0x70
        ld (osKScore),a
        call bumpKiller
        call ptrKiller
        inc hl
        inc hl
        ld a,(hl)
        ld (osKF),a
        inc hl
        ld a,(hl)
        ld (osKT),a
        ld a,0x68
        ld (osKScore),a
        call bumpKiller
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

; bumpKiller — if (osKF,osKT) is a quiet move in the list, raise its
; ordering score to (osKScore).  Does not override a capture's score.
bumpKiller:
        ld a,(osKF)
        cp 0xFF
        ret z
        call curMoveBuf
        ld a,(genCount)
        ld b,a
bkLp:   push hl
        ld a,(osKF)
        cp (hl)
        jr nz,bkNext
        inc hl
        ld a,(osKT)
        cp (hl)
        jr nz,bkNext
        pop hl
        push hl
        inc hl
        inc hl
        inc hl
        ld a,(hl)
        or a
        jr nz,bkDone             ; already scored (capture) -> leave
        ld a,(osKScore)
        ld (hl),a
bkDone:
        pop hl
        ret
bkNext:
        pop hl
        ld de,4
        add hl,de
        djnz bkLp
        ret

; =====================================================================
;  aiMove — search from the root and play the best move.
; =====================================================================
; updateKiller — record a quiet cutoff move as a killer for this ply.
updateKiller:
        ld a,(mvTo)
        ld l,a
        ld h,0xE0
        ld a,(hl)
        or a
        ret nz                   ; capture
        ld a,(mvFlag)
        and 7
        cp SP_EP
        ret z
        ld a,(mvFlag)
        and 0xF0
        ret nz                   ; promotion
        call ptrKiller
        ld a,(mvFrom)
        cp (hl)
        jr nz,ukShift
        inc hl
        ld a,(mvTo)
        cp (hl)
        ret z                    ; already killer 1
        dec hl
ukShift:
        ld d,(hl)                ; old k1from
        inc hl
        ld e,(hl)                ; old k1to
        inc hl
        ld (hl),d                ; k2from
        inc hl
        ld (hl),e                ; k2to
        call ptrKiller
        ld a,(mvFrom)
        ld (hl),a
        inc hl
        ld a,(mvTo)
        ld (hl),a
        ; history: history[pieceType][to64] += depth (saturating)
        call histAddr
        call ptrDepth
        ld a,(hl)
        ld b,a                   ; B = depth
        ex de,hl                 ; HL = history entry (histAddr left it in DE)
        ld a,(hl)
        add a,b
        jr nc,ukHsave
        ld a,255
ukHsave:
        ld (hl),a
        ret

; histAddr -> DE = &history[type(mvFrom)-1][to64(mvTo)]
histAddr:
        ld a,(mvFrom)
        ld l,a
        ld h,0xE0
        ld a,(hl)
        and 7
        dec a                    ; type-1 (0..5)
        add a,a
        add a,a
        add a,a
        add a,a
        add a,a
        add a,a                  ; *64
        ld e,a
        ld d,0
        ld a,(mvTo)
        ld c,a
        and 0x70
        rrca
        ld l,a
        ld a,c
        and 7
        or l                     ; to64
        ld c,a
        ld hl,historyTbl
        add hl,de
        ld d,0
        ld e,c
        add hl,de
        ex de,hl                 ; DE = entry address
        ret

clearHistory:
        ld hl,historyTbl
        ld de,historyTbl+1
        ld bc,383
        ld (hl),0
        ldir
        ret

clearKillers:
        ld hl,killerArr
        ld b,64
ckLp:   ld (hl),0xFF
        inc hl
        djnz ckLp
        ret

aiMove:
        call tryBook             ; CF=1 and move played if a book move applies
        ret c
        ; per-side depth (odds play): Black uses blackDepth, White aiDepth.
        ld a,(sideToMove)
        cp 8
        jr nz,amWDepth
        ld a,(blackDepth)
        jr amSetD
amWDepth:
        ld a,(aiDepth)
amSetD:
        ld (effDepth),a
        call clearKillers
        ld a,0xFF
        ld (bestFrom),a
        ld (pvFrom),a            ; no PV hint yet
        ld a,1
        ld (aidIter),a           ; iterative deepening: depth 1..effDepth
aidLoop:
        xor a
        ld (searchPly),a
        ; aspiration window: full for depth 1, else [prev-W, prev+W]
        ld a,(aidIter)
        cp 2
        jr c,aspFull
        ld hl,(lastScore)
        ld de,ASPW
        push hl
        or a
        sbc hl,de
        ex de,hl
        call setAlphaDE          ; alpha = lastScore - W
        pop hl
        ld de,ASPW
        add hl,de
        ex de,hl
        call setBetaDE           ; beta  = lastScore + W
        jr aspGo
aspFull:
        ld de,-INF
        call setAlphaDE
        ld de,INF
        call setBetaDE
aspGo:
        ld a,(aidIter)
        call negamax
        ld (lastScore),hl
        ; if the result fell outside the window, re-search with a full one
        ld a,(aidIter)
        cp 2
        jr c,aspOk
        call ptrAlpha
        ld c,(hl)
        inc hl
        ld b,(hl)
        ld de,(lastScore)
        call cmpDEgtBC           ; score > alpha ?
        jr nc,aspWiden           ; fail low
        call ptrBeta
        ld c,(hl)
        inc hl
        ld b,(hl)
        ld de,(lastScore)
        push bc
        push de
        pop bc                   ; BC = score
        pop de                   ; DE = beta
        call cmpDEgtBC           ; beta > score ?
        jr c,aspOk               ; inside window
aspWiden:
        ld de,-INF
        call setAlphaDE
        ld de,INF
        call setBetaDE
        ld a,(aidIter)
        call negamax
        ld (lastScore),hl
aspOk:
        ; remember this iteration's best as the PV hint for the next one
        ld a,(bestFrom)
        ld (pvFrom),a
        ld a,(bestTo)
        ld (pvTo),a
        ld a,(aidIter)
        ld hl,effDepth
        cp (hl)
        jr nc,aidDone            ; aidIter >= effDepth -> stop (depth cap)
        ; clock-aware stop: once we've already spent the move's time budget,
        ; don't start another (deeper, much slower) iteration.  Always allow
        ; at least depth 2 so play stays sensible when very low on time.
        ld a,(aidIter)
        cp 2
        jr c,aidNext
        call clkBudgetExceeded
        jr c,aidDone
aidNext:
        ld a,(aidIter)
        inc a
        ld (aidIter),a
        jp aidLoop
aidDone:
        ld a,(bestFrom)
        cp 0xFF
        ret z
        ld a,(bestFrom)
        ld (mvFrom),a
        ld a,(bestTo)
        ld (mvTo),a
        ld a,(bestFlag)
        ld (mvFlag),a
        ld a,(bestFrom)
        ld (lastFrom),a
        ld a,(bestTo)
        ld (lastTo),a
        ld a,1
        ld (haveLast),a
        xor a
        ld (searchPly),a
        call makeMove
        ret

; tryBook — if the current position's Zobrist key is in the opening book,
; play the booked reply.  Keyed by hashKey, so it fires at any ply and
; follows transpositions, not just on Black's first move.  Returns CF=1 if
; a move was made.
tryBook:
        ld hl,bookTable
tbLoop:
        ld e,(hl)
        inc hl
        ld d,(hl)              ; de = entry key
        inc hl
        ld a,d
        and e
        inc a
        jr z,tbNo              ; de == 0xFFFF -> end of table
        ; compare de against the live position key
        push hl
        ld hl,(hashKey)
        ld a,l
        cp e
        jr nz,tbMiss
        ld a,h
        cp d
        jr nz,tbMiss
        pop hl                 ; hl -> from,to,name
        ld a,(hl)
        ld (mvFrom),a
        inc hl
        ld a,(hl)
        ld (mvTo),a
        inc hl
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (openingNamePtr),de
        xor a
        ld (mvFlag),a
        call bookLegal
        jr nc,tbNo
        xor a
        ld (searchPly),a
        call makeMove
        ld a,(mvFrom)
        ld (lastFrom),a
        ld a,(mvTo)
        ld (lastTo),a
        ld a,1
        ld (haveLast),a
        ld hl,0
        ld (lastScore),hl
        scf
        ret
tbMiss:
        pop hl                 ; hl -> from,to,name; skip to next entry
        ld de,4
        add hl,de
        jr tbLoop
tbNo:
        or a
        ret

; bookLegal — CF=1 if (mvFrom,mvTo) is legal; sets mvFlag from the list.
; genLegal clobbers mvFrom/mvTo, so save the booked move first.
bookLegal:
        ld a,(mvFrom)
        ld (bkF),a
        ld a,(mvTo)
        ld (bkT),a
        xor a
        ld (searchPly),a
        call genLegal
        ld a,(genCount)
        ld b,a
        or a
        jr z,blNo
        call curMoveBuf
blL:    push hl
        ld a,(bkF)
        cp (hl)
        jr nz,blSkip
        inc hl
        ld a,(bkT)
        cp (hl)
        jr nz,blSkip
        pop hl
        ld a,(bkF)
        ld (mvFrom),a
        ld a,(bkT)
        ld (mvTo),a
        inc hl
        inc hl
        ld a,(hl)
        ld (mvFlag),a
        scf
        ret
blSkip:
        pop hl
        ld de,4
        add hl,de
        djnz blL
blNo:
        ld a,(bkF)
        ld (mvFrom),a
        ld a,(bkT)
        ld (mvTo),a
        or a
        ret

; opening book — position-keyed (generated by bookgen.py).
; entry: key16, reply from,to (0x88), opening-name ptr; terminated by 0xFFFF.
; Probed by tryBook against the live hashKey, so the same table covers the
; first reply and the deeper plies of each principal variation.
bookTable:
        defw 0xD7A4
        defb 0x64,0x44
        defw nmOpen     ; Open game
        defw 0xFD7F
        defb 0x71,0x52
        defw nmKK       ; King's Knight
        defw 0x48E7
        defb 0x60,0x50
        defw nmRuy      ; Ruy Lopez
        defw 0x9C49
        defb 0x76,0x55
        defw nmRuy      ; Ruy Lopez
        defw 0x2983
        defb 0x75,0x42
        defw nmItalian  ; Italian Game
        defw 0x2A60
        defb 0x76,0x55
        defw nmItalian  ; Italian Game
        defw 0x0D8F
        defb 0x63,0x53
        defw nmItalian  ; Italian Game
        defw 0x4C18
        defb 0x44,0x33
        defw nmScotch   ; Scotch
        defw 0xA523
        defb 0x76,0x55
        defw nmKK       ; King's Knight
        defw 0x73DD
        defb 0x76,0x55
        defw nmOpen     ; Open game
        defw 0xFF7D
        defb 0x76,0x55
        defw nmOpen     ; Open game
        defw 0x1646
        defb 0x44,0x33
        defw nmOpen     ; Open game
        defw 0xAD47
        defb 0x63,0x43
        defw nmClosed   ; Closed game
        defw 0x1DFD
        defb 0x64,0x54
        defw nmQG       ; Queen's Gambit
        defw 0xFDA2
        defb 0x76,0x55
        defw nmQP       ; Queen's Pawn
        defw 0x4EE5
        defb 0x76,0x55
        defw nmQGD      ; QGD
        defw 0x4CE7
        defb 0x76,0x55
        defw nmQGD      ; QGD
        defw 0x318B
        defb 0x76,0x55
        defw nmLondon   ; London
        defw 0x978A
        defb 0x76,0x55
        defw nmClosed   ; Closed game
        defw 0x467E
        defb 0x76,0x55
        defw nmReti     ; Reti
        defw 0xE8C4
        defb 0x64,0x54
        defw nmReti     ; Reti
        defw 0x671A
        defb 0x63,0x43
        defw nmReti     ; Reti
        defw 0xA621
        defb 0x64,0x44
        defw nmEnglish  ; English
        defw 0x2D2E
        defb 0x76,0x55
        defw nmEnglish  ; English
        defw 0x82FA
        defb 0x63,0x43
        defw nmBird     ; Bird
        defw 0x29FF
        defb 0x63,0x43
        defw nmClosed   ; Closed game
        defw 0x7AD7
        defb 0x64,0x44
        defw nmOpen     ; Open game
        defw 0xFFFF

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
; --- king (endgame): centralise ---
pstKingEG:
        defb -50,-40,-30,-20,-20,-30,-40,-50
        defb -30,-20,-10,  0,  0,-10,-20,-30
        defb -30,-10, 20, 30, 30, 20,-10,-30
        defb -30,-10, 30, 40, 40, 30,-10,-30
        defb -30,-10, 30, 40, 40, 30,-10,-30
        defb -30,-10, 20, 30, 30, 20,-10,-30
        defb -30,-30,  0,  0,  0,  0,-30,-30
        defb -50,-30,-30,-30,-30,-30,-30,-50
