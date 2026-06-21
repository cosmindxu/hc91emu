; =====================================================================
;  movegen.inc — 0x88 move generation, attack detection, make/unmake,
;                legal-move filtering, terminal detection.
; =====================================================================

; --- direction tables -------------------------------------------------
knightDirs: defb 0x21,0x1F,0x12,0x0E,0xDF,0xE1,0xEE,0xF2
kingDirs:   defb 0x01,0x0F,0x10,0x11,0xFF,0xF1,0xF0,0xEF
bishopDirs: defb 0x0F,0x11,0xF1,0xEF
rookDirs:   defb 0x01,0x10,0xFF,0xF0
promoFlags: defb 0x50,0x40,0x30,0x20   ; Q,R,B,N promotions

; --- per-ply move buffer pointer --------------------------------------
; HL = moveBufBase + ply*512  (high = 0x60 + ply*2, low = 0)
curMoveBuf:
        ld a,(searchPly)
        add a,a
        add a,0x60
        ld h,a
        ld l,0
        ret

; --- append a move: from=(genFrom), to=(genTo), flag=(genFlag) --------
addMove:
        push hl
        ld hl,(genPtr)
        ld a,(genFrom)
        ld (hl),a
        inc hl
        ld a,(genTo)
        ld (hl),a
        inc hl
        ld a,(genFlag)
        ld (hl),a
        inc hl
        xor a
        ld (hl),a
        inc hl
        ld (genPtr),hl
        ld hl,genCount
        inc (hl)
        pop hl
        ret

; add the 4 promotions for to=(genTo) already set
addPromos:
        ld hl,promoFlags
        ld b,4
apL:    ld a,(hl)
        ld (genFlag),a
        call addMove
        inc hl
        djnz apL
        ret

; =====================================================================
;  genMoves — generate all pseudo-legal moves for sideToMove into the
;  current ply's buffer.  Result count in (genCount).
; =====================================================================
genMoves:
        call curMoveBuf
        ld (genPtr),hl
        xor a
        ld (genCount),a
        ld a,0
gmLoop:
        ld (genCurSq),a
        and 0x88
        jp nz,gmCont
        ld a,(genCurSq)
        ld (genFrom),a
        ld h,0xE0
        ld l,a
        ld a,(hl)             ; piece
        or a
        jp z,gmCont
        ld d,a                ; piece in D
        and COLBIT
        ld hl,sideToMove
        cp (hl)
        jp nz,gmCont          ; not our piece
        ld a,d
        and TYPEMASK
        cp WP
        jp z,gmPawn
        cp WN
        jp z,gmKnight
        cp WK
        jp z,gmKing
        cp WB
        jp z,gmBishop
        cp WR
        jp z,gmRook
        jp gmQueen
gmCont:
        ld a,(genCurSq)
        inc a
        cp 0x78
        jp nz,gmLoop
        ret

; --- castling generation (called from gmKing) ------------------------
; Adds O-O / O-O-O when the right is held, the path is clear, the king is
; not in check and does not pass through or land on an attacked square.
genCastling:
        ld a,(sideToMove)
        or a
        jp nz,gcBlack
        ld a,(genFrom)
        cp 0x04                 ; white king home e1
        ret nz
        ld a,8
        ld (atkSide),a
        ld a,0x04
        call isAttacked
        or a
        ret nz                  ; in check
        ld a,(castling)
        and 1
        jr z,gcWq
        ld h,0xE0
        ld l,0x05
        ld a,(hl)
        or a
        jr nz,gcWq
        ld l,0x06
        ld a,(hl)
        or a
        jr nz,gcWq
        ld a,0x05
        call isAttacked
        or a
        jr nz,gcWq
        ld a,0x06
        call isAttacked
        or a
        jr nz,gcWq
        ld a,0x06
        ld (genTo),a
        ld a,SP_OO
        ld (genFlag),a
        call addMove
gcWq:
        ld a,(castling)
        and 2
        ret z
        ld h,0xE0
        ld l,0x03
        ld a,(hl)
        or a
        ret nz
        ld l,0x02
        ld a,(hl)
        or a
        ret nz
        ld l,0x01
        ld a,(hl)
        or a
        ret nz
        ld a,0x03
        call isAttacked
        or a
        ret nz
        ld a,0x02
        call isAttacked
        or a
        ret nz
        ld a,0x02
        ld (genTo),a
        ld a,SP_OOO
        ld (genFlag),a
        call addMove
        ret
gcBlack:
        ld a,(genFrom)
        cp 0x74                 ; black king home e8
        ret nz
        xor a
        ld (atkSide),a
        ld a,0x74
        call isAttacked
        or a
        ret nz
        ld a,(castling)
        and 4
        jr z,gcBq
        ld h,0xE0
        ld l,0x75
        ld a,(hl)
        or a
        jr nz,gcBq
        ld l,0x76
        ld a,(hl)
        or a
        jr nz,gcBq
        ld a,0x75
        call isAttacked
        or a
        jr nz,gcBq
        ld a,0x76
        call isAttacked
        or a
        jr nz,gcBq
        ld a,0x76
        ld (genTo),a
        ld a,SP_OO
        ld (genFlag),a
        call addMove
gcBq:
        ld a,(castling)
        and 8
        ret z
        ld h,0xE0
        ld l,0x73
        ld a,(hl)
        or a
        ret nz
        ld l,0x72
        ld a,(hl)
        or a
        ret nz
        ld l,0x71
        ld a,(hl)
        or a
        ret nz
        ld a,0x73
        call isAttacked
        or a
        ret nz
        ld a,0x72
        call isAttacked
        or a
        ret nz
        ld a,0x72
        ld (genTo),a
        ld a,SP_OOO
        ld (genFlag),a
        call addMove
        ret

; --- knight ----------------------------------------------------------
gmKnight:
        ld hl,knightDirs
        ld b,8
        jp gmHop
gmKing:
        call genCastling
        ld hl,kingDirs
        ld b,8
gmHop:                          ; non-sliding: HL=dir table, B=count
        ld a,(genFrom)
        add a,(hl)
        ld d,a                  ; target
        and 0x88
        jr nz,gmHopNext
        push hl
        ld h,0xE0
        ld l,d
        ld a,(hl)               ; board[target]
        pop hl
        or a
        jr z,gmHopAdd
        and COLBIT
        ld e,a
        ld a,(sideToMove)
        cp e
        jr z,gmHopNext          ; own piece
gmHopAdd:
        ld a,d
        ld (genTo),a
        xor a
        ld (genFlag),a
        call addMove
gmHopNext:
        inc hl
        djnz gmHop
        jp gmCont

; --- sliding ---------------------------------------------------------
gmBishop:
        ld hl,bishopDirs
        ld b,4
        jp gmSlide
gmRook:
        ld hl,rookDirs
        ld b,4
        jp gmSlide
gmQueen:
        ld hl,kingDirs
        ld b,8
gmSlide:                        ; HL=dir table, B=count
        ld a,(genFrom)
        ld c,a                  ; ray square
        ld e,(hl)               ; direction
gmRay:
        ld a,c
        add a,e
        ld c,a
        and 0x88
        jr nz,gmSlideNext
        push hl
        ld h,0xE0
        ld l,c
        ld a,(hl)
        pop hl
        or a
        jr z,gmRayQuiet
        and COLBIT
        ld d,a
        ld a,(sideToMove)
        cp d
        jr z,gmSlideNext        ; own piece blocks
        ld a,c                  ; enemy capture
        ld (genTo),a
        xor a
        ld (genFlag),a
        call addMove
        jr gmSlideNext          ; stop after capture
gmRayQuiet:
        ld a,c
        ld (genTo),a
        xor a
        ld (genFlag),a
        call addMove
        jr gmRay
gmSlideNext:
        inc hl
        djnz gmSlide
        jp gmCont

; --- pawns -----------------------------------------------------------
gmPawn:
        ld a,(sideToMove)
        or a
        jp nz,gmPawnBlack
; white pawn
        ld a,(genFrom)
        add a,16
        ld c,a                  ; one-ahead square
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jr nz,wpCaps            ; blocked
        ; one ahead empty
        ld a,(genFrom)
        and 0x70
        cp 0x60                 ; promotion rank?
        jr nz,wpPush
        ld a,c
        ld (genTo),a
        call addPromos
        jr wpCaps
wpPush:
        ld a,c
        ld (genTo),a
        xor a
        ld (genFlag),a
        call addMove
        ld a,(genFrom)
        and 0x70
        cp 0x10                 ; start rank -> double push
        jr nz,wpCaps
        ld a,(genFrom)
        add a,32
        ld c,a
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jr nz,wpCaps
        ld a,c
        ld (genTo),a
        ld a,SP_DPUSH
        ld (genFlag),a
        call addMove
wpCaps:
        ld a,15
        call wpOneCap
        ld a,17
        call wpOneCap
        jp gmCont
; white capture toward offset in A
wpOneCap:
        ld c,a                  ; offset
        ld a,(genFrom)
        add a,c
        ld d,a                  ; target
        and 0x88
        ret nz
        ld h,0xE0
        ld l,d
        ld a,(hl)
        or a
        jr z,wpEp
        and COLBIT
        ret z                   ; own (white) piece -> not a capture
        ; enemy black: capture (promote if rank6)
        ld a,(genFrom)
        and 0x70
        cp 0x60
        jr nz,wpCapNorm
        ld a,d
        ld (genTo),a
        jp addPromos
wpCapNorm:
        ld a,d
        ld (genTo),a
        xor a
        ld (genFlag),a
        jp addMove
wpEp:
        ld a,(epSquare)
        cp d
        ret nz
        ld a,d
        ld (genTo),a
        ld a,SP_EP
        ld (genFlag),a
        jp addMove

; black pawn
gmPawnBlack:
        ld a,(genFrom)
        sub 16
        ld c,a
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jr nz,bpCaps
        ld a,(genFrom)
        and 0x70
        cp 0x10                 ; black promotes from rank index 1
        jr nz,bpPush
        ld a,c
        ld (genTo),a
        call addPromos
        jr bpCaps
bpPush:
        ld a,c
        ld (genTo),a
        xor a
        ld (genFlag),a
        call addMove
        ld a,(genFrom)
        and 0x70
        cp 0x60                 ; black start rank index 6
        jr nz,bpCaps
        ld a,(genFrom)
        sub 32
        ld c,a
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jr nz,bpCaps
        ld a,c
        ld (genTo),a
        ld a,SP_DPUSH
        ld (genFlag),a
        call addMove
bpCaps:
        ld a,-15
        call bpOneCap
        ld a,-17
        call bpOneCap
        jp gmCont
bpOneCap:
        ld c,a
        ld a,(genFrom)
        add a,c
        ld d,a
        and 0x88
        ret nz
        ld h,0xE0
        ld l,d
        ld a,(hl)
        or a
        jr z,bpEp
        and COLBIT
        ret nz                  ; own (black) piece
        ; enemy white capture
        ld a,(genFrom)
        and 0x70
        cp 0x10
        jr nz,bpCapNorm
        ld a,d
        ld (genTo),a
        jp addPromos
bpCapNorm:
        ld a,d
        ld (genTo),a
        xor a
        ld (genFlag),a
        jp addMove
bpEp:
        ld a,(epSquare)
        cp d
        ret nz
        ld a,d
        ld (genTo),a
        ld a,SP_EP
        ld (genFlag),a
        jp addMove

; =====================================================================
;  isAttacked — is square A attacked by side (atkSide)?  A=1 if yes.
; =====================================================================
isAttacked:
        ld (tmpSq),a
        ; knights
        ld a,(atkSide)
        add a,WN
        ld c,a
        ld hl,knightDirs
        ld b,8
        call iaScanType
        jr c,iaYes
        ; king
        ld a,(atkSide)
        add a,WK
        ld c,a
        ld hl,kingDirs
        ld b,8
        call iaScanType
        jr c,iaYes
        ; pawns
        ld a,(atkSide)
        or a
        jr nz,iaPawnB
        ; white attacker: pawns at target-15, target-17
        ld a,-15
        ld c,WP
        call iaPawnAt
        jr c,iaYes
        ld a,-17
        ld c,WP
        call iaPawnAt
        jr c,iaYes
        jr iaSliders
iaPawnB:
        ld a,15
        ld c,BP
        call iaPawnAt
        jr c,iaYes
        ld a,17
        ld c,BP
        call iaPawnAt
        jr c,iaYes
iaSliders:
        ; diagonal: bishop/queen
        ld a,(atkSide)
        add a,WB
        ld c,a
        ld a,(atkSide)
        add a,WQ
        ld (iaExp2),a
        ld hl,bishopDirs
        ld b,4
        call iaSlide
        jr c,iaYes
        ; orthogonal: rook/queen
        ld a,(atkSide)
        add a,WR
        ld c,a
        ld a,(atkSide)
        add a,WQ
        ld (iaExp2),a
        ld hl,rookDirs
        ld b,4
        call iaSlide
        jr c,iaYes
        xor a
        ret
iaYes:  ld a,1
        or a
        ret

; scan offset list HL (B entries); hit (CF=1) if board[target+dir]==C
iaScanType:
        ld a,(tmpSq)
        add a,(hl)
        ld d,a
        and 0x88
        jr nz,iaScanNext
        push hl
        ld h,0xE0
        ld l,d
        ld a,(hl)
        pop hl
        cp c
        jr z,iaScanHit
iaScanNext:
        inc hl
        djnz iaScanType
        or a
        ret
iaScanHit:
        scf
        ret

; pawn-at: A=offset, C=expected pawn code; CF=1 if board[target+off]==C
iaPawnAt:
        ld e,a
        ld a,(tmpSq)
        add a,e
        ld d,a
        and 0x88
        jr nz,iaPawnNo
        ld h,0xE0
        ld l,d
        ld a,(hl)
        cp c
        jr z,iaScanHit
iaPawnNo:
        or a
        ret

; slide scan: HL=dir table, B=count, C=expected1, (iaExp2)=expected2.
iaSlide:
        ld a,(hl)
        ld (iaDir),a
        ld a,(tmpSq)
        ld e,a                  ; ray square
iaSlideStep:
        ld a,(iaDir)
        add a,e
        ld e,a
        and 0x88
        jr nz,iaSlideNext
        push hl
        ld h,0xE0
        ld l,e
        ld a,(hl)
        pop hl
        or a
        jr z,iaSlideStep        ; empty -> continue ray
        cp c
        jr z,iaSlideHit
        ld d,a
        ld a,(iaExp2)
        cp d
        jr z,iaSlideHit
        jr iaSlideNext          ; blocked by irrelevant piece
iaSlideHit:
        scf
        ret
iaSlideNext:
        inc hl
        djnz iaSlide
        or a
        ret

; inCheckSide(A=side): A=1 if that side's king is attacked
inCheckSide:
        ld e,a                  ; side
        or a
        jr z,icsW
        ld a,(bking)
        jr icsGo
icsW:   ld a,(wking)
icsGo:  ld d,a                  ; king square
        ld a,e
        xor 8
        ld (atkSide),a
        ld a,d
        call isAttacked
        ret

; moverInCheck: did the side that just moved leave its king in check?
moverInCheck:
        ld a,(sideToMove)
        xor 8
        call inCheckSide
        ret

; =====================================================================
;  makeMove / unmakeMove — operate on (mvFrom,mvTo,mvFlag), undo[ply].
; =====================================================================
undoPtr:
        ld a,(searchPly)
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl               ; ply*16
        ld de,undoBase
        add hl,de
        ret

makeMove:
        ld a,(castling)
        ld (mkOldCastle),a
        ld a,(epSquare)
        ld (mkOldEp),a
        ld a,(mvFlag)
        ld b,a
        and 7
        ld (mkSpecial),a
        ld a,b
        and 0xF0
        rrca
        rrca
        rrca
        rrca
        ld (mkPromo),a
        ld a,(mvFrom)
        ld l,a
        ld h,0xE0
        ld a,(hl)
        ld (mkPiece),a
        and 8
        ld (mkSide),a
        ld a,(mkSpecial)
        cp SP_EP
        jr z,mkEp
        ld a,(mvTo)
        ld (mkCapSq),a
        ld l,a
        ld h,0xE0
        ld a,(hl)
        ld (mkCaptured),a
        jr mkUndo
mkEp:
        ld a,(mkSide)
        or a
        ld a,(mvTo)
        jr nz,mkEpB
        sub 16
        jr mkEpS
mkEpB:  add a,16
mkEpS:  ld (mkCapSq),a
        ld l,a
        ld h,0xE0
        ld a,(hl)
        ld (mkCaptured),a
mkUndo:
        call undoPtr
        ld a,(mvFrom)
        ld (hl),a
        inc hl
        ld a,(mvTo)
        ld (hl),a
        inc hl
        ld a,(mvFlag)
        ld (hl),a
        inc hl
        ld a,(mkPiece)
        ld (hl),a
        inc hl
        ld a,(mkCaptured)
        ld (hl),a
        inc hl
        ld a,(mkCapSq)
        ld (hl),a
        inc hl
        ld a,(castling)
        ld (hl),a
        inc hl
        ld a,(epSquare)
        ld (hl),a
        inc hl
        ld a,(halfmove)
        ld (hl),a
        inc hl
        ld a,(wking)
        ld (hl),a
        inc hl
        ld a,(bking)
        ld (hl),a
        inc hl
        ld a,(moveCount)
        ld (hl),a
        inc hl
        ld a,(moveCount+1)
        ld (hl),a
        inc hl
        ld a,(hashKey)           ; save key for unmake (undo +13/+14)
        ld (hl),a
        inc hl
        ld a,(hashKey+1)
        ld (hl),a
        ; --- mutate board ---
        ld a,(mvFrom)
        ld l,a
        ld h,0xE0
        ld (hl),0
        ld a,(mkSpecial)
        cp SP_EP
        jr nz,mkPlace
        ld a,(mkCapSq)
        ld l,a
        ld (hl),0
mkPlace:
        ld a,(mkPromo)
        or a
        jr z,mkNoProm
        ld b,a
        ld a,(mkSide)
        or b
        jr mkPut
mkNoProm:
        ld a,(mkPiece)
mkPut:
        ld c,a
        ld a,(mvTo)
        ld l,a
        ld h,0xE0
        ld (hl),c
        ; castling rook
        ld a,(mkSpecial)
        cp SP_OO
        jr z,mkOO
        cp SP_OOO
        jr z,mkOOO
        jr mkKing
mkOO:
        ld a,(mkSide)
        or a
        jr nz,mkOOb
        ld h,0xE0
        ld l,0x07
        ld (hl),0
        ld l,0x05
        ld (hl),WR
        jr mkKing
mkOOb:
        ld h,0xE0
        ld l,0x77
        ld (hl),0
        ld l,0x75
        ld (hl),BR
        jr mkKing
mkOOO:
        ld a,(mkSide)
        or a
        jr nz,mkOOOb
        ld h,0xE0
        ld l,0x00
        ld (hl),0
        ld l,0x03
        ld (hl),WR
        jr mkKing
mkOOOb:
        ld h,0xE0
        ld l,0x70
        ld (hl),0
        ld l,0x73
        ld (hl),BR
mkKing:
        ld a,(mkPiece)
        and 7
        cp WK
        jr nz,mkRights
        ld a,(mkSide)
        or a
        ld a,(mvTo)
        jr nz,mkKb
        ld (wking),a
        jr mkRights
mkKb:   ld (bking),a
mkRights:
        ld a,(castling)
        ld b,a
        ld a,(mkPiece)
        and 7
        cp WK
        jr nz,mkCR1
        ld a,(mkSide)
        or a
        jr nz,mkCRkb
        ld a,b
        and 0xFC
        ld b,a
        jr mkCR1
mkCRkb:
        ld a,b
        and 0xF3
        ld b,a
mkCR1:
        ld a,(mvFrom)
        call clrCastleSq
        ld a,(mvTo)
        call clrCastleSq
        ld a,b
        ld (castling),a
        ; ep square
        ld a,(mkSpecial)
        cp SP_DPUSH
        jr nz,mkNoDp
        ld a,(mkSide)
        or a
        ld a,(mvFrom)
        jr nz,mkDpB
        add a,16
        jr mkDpS
mkDpB:  sub 16
mkDpS:  ld (epSquare),a
        jr mkHalf
mkNoDp:
        ld a,0xFF
        ld (epSquare),a
mkHalf:
        ld a,(mkPiece)
        and 7
        cp WP
        jr z,mkHm0
        ld a,(mkCaptured)
        or a
        jr nz,mkHm0
        ld a,(halfmove)
        inc a
        ld (halfmove),a
        jr mkCount
mkHm0:
        xor a
        ld (halfmove),a
mkCount:
        ld a,(mkSide)
        or a
        jr z,mkTog
        ld hl,(moveCount)
        inc hl
        ld (moveCount),hl
mkTog:
        ld a,(sideToMove)
        xor 8
        ld (sideToMove),a
        call phaseDelta          ; gamePhase += promoWt - capWt
        ld hl,gamePhase
        add a,(hl)
        ld (hl),a
        call pstDelta            ; pstScore += delta
        ld hl,(pstScore)
        add hl,de
        ld (pstScore),hl
        call keyUpdate
        ret

clrCastleSq:
        cp 0x00
        jr nz,cc1
        ld a,b
        and 0xFD
        ld b,a
        ret
cc1:    cp 0x07
        jr nz,cc2
        ld a,b
        and 0xFE
        ld b,a
        ret
cc2:    cp 0x70
        jr nz,cc3
        ld a,b
        and 0xF7
        ld b,a
        ret
cc3:    cp 0x77
        ret nz
        ld a,b
        and 0xFB
        ld b,a
        ret

unmakeMove:
        call undoPtr
        ld a,(hl)
        ld (mvFrom),a
        inc hl
        ld a,(hl)
        ld (mvTo),a
        inc hl
        ld a,(hl)
        ld (mvFlag),a
        inc hl
        ld a,(hl)
        ld (mkPiece),a
        and 8
        ld (mkSide),a            ; restore side (pstDelta/castle need it)
        ld a,(mvFlag)            ; restore special + promo (pstDelta needs them)
        and 7
        ld (mkSpecial),a
        ld a,(mvFlag)
        rrca
        rrca
        rrca
        rrca
        and 0x0F
        ld (mkPromo),a
        inc hl
        ld a,(hl)
        ld (mkCaptured),a
        inc hl
        ld a,(hl)
        ld (mkCapSq),a
        inc hl
        ld a,(hl)
        ld (castling),a
        inc hl
        ld a,(hl)
        ld (epSquare),a
        inc hl
        ld a,(hl)
        ld (halfmove),a
        inc hl
        ld a,(hl)
        ld (wking),a
        inc hl
        ld a,(hl)
        ld (bking),a
        inc hl
        ld a,(hl)
        ld (moveCount),a
        inc hl
        ld a,(hl)
        ld (moveCount+1),a
        inc hl
        ld a,(hl)                ; restore saved Zobrist key
        ld (hashKey),a
        inc hl
        ld a,(hl)
        ld (hashKey+1),a
        ; restore board: from = movedPiece
        ld a,(mvFrom)
        ld l,a
        ld h,0xE0
        ld a,(mkPiece)
        ld (hl),a
        ; to / capSq
        ld a,(mkCapSq)
        ld b,a
        ld a,(mvTo)
        cp b
        jr nz,umEp
        ld a,(mvTo)
        ld l,a
        ld h,0xE0
        ld a,(mkCaptured)
        ld (hl),a
        jr umCastle
umEp:
        ld a,(mvTo)
        ld l,a
        ld h,0xE0
        ld (hl),0
        ld a,(mkCapSq)
        ld l,a
        ld a,(mkCaptured)
        ld (hl),a
umCastle:
        ld a,(mvFlag)
        and 7
        cp SP_OO
        jr z,umOO
        cp SP_OOO
        jr z,umOOO
        jr umDone
umOO:
        ld a,(mkPiece)
        and 8
        jr nz,umOOb
        ld h,0xE0
        ld l,0x05
        ld (hl),0
        ld l,0x07
        ld (hl),WR
        jr umDone
umOOb:
        ld h,0xE0
        ld l,0x75
        ld (hl),0
        ld l,0x77
        ld (hl),BR
        jr umDone
umOOO:
        ld a,(mkPiece)
        and 8
        jr nz,umOOOb
        ld h,0xE0
        ld l,0x03
        ld (hl),0
        ld l,0x00
        ld (hl),WR
        jr umDone
umOOOb:
        ld h,0xE0
        ld l,0x73
        ld (hl),0
        ld l,0x70
        ld (hl),BR
umDone:
        ld a,(sideToMove)
        xor 8
        ld (sideToMove),a
        call phaseDelta          ; gamePhase -= (promoWt - capWt)
        ld b,a
        ld a,(gamePhase)
        sub b
        ld (gamePhase),a
        call pstDelta            ; pstScore -= delta
        ld hl,(pstScore)
        or a
        sbc hl,de
        ld (pstScore),hl
        ret

; =====================================================================
;  genLegal — pseudo-legal moves filtered to legal (current ply).
; =====================================================================
genLegal:
        call genMoves
        ld a,(genCount)
        or a
        ret z
        call curMoveBuf
        ld (glRead),hl
        ld (glWrite),hl
        ld a,(genCount)
        ld (glCount),a
        xor a
        ld (glLegal),a
glLoop:
        ld hl,(glRead)
        ld a,(hl)
        ld (mvFrom),a
        inc hl
        ld a,(hl)
        ld (mvTo),a
        inc hl
        ld a,(hl)
        ld (mvFlag),a
        inc hl
        inc hl
        ld (glRead),hl
        call makeMove
        call moverInCheck
        push af
        call unmakeMove
        pop af
        or a
        jr nz,glSkip
        ld hl,(glWrite)
        ld a,(mvFrom)
        ld (hl),a
        inc hl
        ld a,(mvTo)
        ld (hl),a
        inc hl
        ld a,(mvFlag)
        ld (hl),a
        inc hl
        xor a
        ld (hl),a
        inc hl
        ld (glWrite),hl
        ld hl,glLegal
        inc (hl)
glSkip:
        ld hl,glCount
        dec (hl)
        jr nz,glLoop
        ld a,(glLegal)
        ld (genCount),a
        ret

; =====================================================================
;  updateTerminal — set gameState after a move.
; =====================================================================
updateTerminal:
        xor a
        ld (searchPly),a
        call genLegal
        ld a,(genCount)
        or a
        jr nz,utHas
        ld a,(sideToMove)
        call inCheckSide
        or a
        jr z,utStale
        ld a,(sideToMove)
        or a
        jr z,utWmate
        ld a,2
        ld (gameState),a
        ld hl,msgBmate
        call setMsg
        ret
utWmate:
        ld a,1
        ld (gameState),a
        ld hl,msgWmate
        call setMsg
        ret
utStale:
        ld a,3
        ld (gameState),a
        ld hl,msgStale
        call setMsg
        ret
utHas:
        call countReps
        cp 3
        jr c,utMatChk
        ld a,4
        ld (gameState),a
        ld hl,msgRep
        call setMsg
        ret
utMatChk:
        call isInsufficient
        or a
        jr z,utHm
        ld a,4
        ld (gameState),a
        ld hl,msgMat
        call setMsg
        ret
utHm:
        ld a,(halfmove)
        cp 100
        jr c,utPlay
        ld a,4
        ld (gameState),a
        ld hl,msgDraw
        call setMsg
        ret
utPlay:
        xor a
        ld (gameState),a
        ld a,(sideToMove)
        call inCheckSide
        or a
        ret z
        ld hl,msgCheck
        call setMsg
        ret

; isInsufficient -> A=1 if the position is a draw by insufficient material
; (KvK, KNvK, KBvK).  Any pawn/rook/queen, or >=2 minors, is sufficient.
isInsufficient:
        ld c,0                  ; square
        ld d,0                  ; minor count
        ld e,0                  ; pawn/rook/queen present?
iiLoop:
        ld a,c
        and 0x88
        jr nz,iiNext
        ld h,0xE0
        ld l,c
        ld a,(hl)
        and 7
        cp 1
        jr z,iiHeavy
        cp 4
        jr z,iiHeavy
        cp 5
        jr z,iiHeavy
        cp 2
        jr z,iiMinor
        cp 3
        jr z,iiMinor
        jr iiNext
iiHeavy:
        ld e,1
        jr iiNext
iiMinor:
        inc d
iiNext:
        inc c
        ld a,c
        cp 0x78
        jr nz,iiLoop
        ld a,e
        or a
        jr nz,iiNo
        ld a,d
        cp 2
        jr nc,iiNo
        ld a,1
        ret
iiNo:
        xor a
        ret

; record the current position key in the game history (for repetition)
recordGameKey:
        ld a,(gameKeyN)
        cp 250
        ret nc
        ld e,a
        ld d,0
        ld hl,gameKeys
        add hl,de
        add hl,de
        ld de,(hashKey)
        ld (hl),e
        inc hl
        ld (hl),d
        ld a,(gameKeyN)
        inc a
        ld (gameKeyN),a
        ret

; pushGameUndo — copy undo[0] (the move just made) onto the take-back stack
pushGameUndo:
        ld a,(gameUndoN)
        cp 48
        ret nc
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl                ; n*16
        ld de,gameUndo
        add hl,de
        ex de,hl                 ; DE = dest
        ld hl,undoBase
        ld bc,16
        ldir
        ld a,(gameUndoN)
        inc a
        ld (gameUndoN),a
        ret

; popUnmake — pop one ply off the take-back stack and unmake it
popUnmake:
        ld a,(gameUndoN)
        or a
        ret z
        dec a
        ld (gameUndoN),a
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,gameUndo
        add hl,de                ; HL = source
        ld de,undoBase
        ld bc,16
        ldir
        xor a
        ld (searchPly),a
        call unmakeMove
        ld a,(gameKeyN)
        or a
        ret z
        dec a
        ld (gameKeyN),a
        ret

; takeBack — undo to the previous position where the human is to move
takeBack:
        ld a,(gameUndoN)
        or a
        ret z
        call popUnmake
        ld a,(twoPlayer)
        or a
        jr nz,tbDone
        ld a,(sideToMove)
        ld hl,humanSide
        cp (hl)
        jr z,tbDone
        ld a,(gameUndoN)
        or a
        jr z,tbDone
        call popUnmake
tbDone:
        xor a
        ld (gameState),a
        ld (haveLast),a          ; clear the stale last-move readout
        ld a,0xFF
        ld (selSq),a
        ret

; countReps -> A = how many times hashKey appears in the game history
countReps:
        ld a,(gameKeyN)
        or a
        ret z
        ld b,a
        ld c,0
        ld hl,gameKeys
crLp:   ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld a,(hashKey)
        cp e
        jr nz,crNo
        ld a,(hashKey+1)
        cp d
        jr nz,crNo
        inc c
crNo:   djnz crLp
        ld a,c
        ret
