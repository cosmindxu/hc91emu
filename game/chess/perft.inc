; =====================================================================
;  perft.inc — perft node counting + self-test.
;
;  perft is the standard proof that move generation, make/unmake and all
;  special moves (castling, en passant, promotion) are correct: it counts
;  the leaves of the legal move tree and compares against known values
;  for the start position (1:20 2:400 3:8902 4:197281).
; =====================================================================

PERFT_MAXD equ 4

; perftRec(A=depth) — adds the leaf count into the 32-bit (perftCnt).
perftRec:
        or a
        jp z,perftLeaf
        ld b,a
        call ptrDepth
        ld (hl),b
        call genMoves
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
prLoop:
        call ptrCnt
        ld a,(hl)
        or a
        ret z
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
        call makeMove
        ld a,(searchPly)
        cp 2
        call c,checkKey          ; verify key on the top plies (fast, but
                                 ; still covers castling/ep/promotion)
        call moverInCheck
        or a
        jr nz,prIllegal
        call ptrDepth
        ld a,(hl)
        dec a
        ld b,a
        ld a,(searchPly)
        inc a
        ld (searchPly),a
        ld a,b
        call perftRec
        ld a,(searchPly)
        dec a
        ld (searchPly),a
        call unmakeMove
        jp prLoop
prIllegal:
        call unmakeMove
        jp prLoop

; checkKey — verify the incremental key AND game phase match a from-scratch
; recompute (proves the incremental state maintained by make/unmake)
checkKey:
        ld hl,(hashKey)
        ld (ckSave),hl
        call computeKey
        ld hl,(ckSave)
        ld de,(hashKey)
        or a
        sbc hl,de
        jr z,ckKeep
        ld a,0xFF
        ld (keyMismatch),a
ckKeep:
        ld hl,(ckSave)
        ld (hashKey),hl
        ; --- game phase ---
        ld a,(gamePhase)
        ld (ckSavePhase),a
        call computePhase
        ld a,(gamePhase)
        ld b,a
        ld a,(ckSavePhase)
        cp b
        jr z,ckPhKeep
        ld a,0xFF
        ld (keyMismatch),a
ckPhKeep:
        ld a,(ckSavePhase)
        ld (gamePhase),a
        ; --- material + PST score ---
        ld hl,(pstScore)
        ld (ckSavePst),hl
        call computePstScore
        ld hl,(ckSavePst)
        ld de,(pstScore)
        or a
        sbc hl,de
        jr z,ckPstKeep
        ld a,0xFF
        ld (keyMismatch),a
ckPstKeep:
        ld hl,(ckSavePst)
        ld (pstScore),hl
        ret

perftLeaf:
        ld hl,perftCnt
        inc (hl)
        ret nz
        inc hl
        inc (hl)
        ret nz
        inc hl
        inc (hl)
        ret nz
        inc hl
        inc (hl)
        ret

; =====================================================================
;  perftSelfTest — run perft 1..MAXD from start, print + verify, wait.
; =====================================================================
perftSelfTest:
        call newGame
        call clearScreen
        xor a
        ld (perftFail),a
        ld (keyMismatch),a
        ld hl,msgPerftHdr
        ld b,0
        ld c,0
        call printStr
        ld a,1
        ld (perftDepth),a
pstLoop:
        ld hl,perftCnt
        ld (hl),0
        inc hl
        ld (hl),0
        inc hl
        ld (hl),0
        inc hl
        ld (hl),0
        xor a
        ld (searchPly),a
        ld a,(perftDepth)
        call perftRec
        ; row = depth + 1
        ld a,(perftDepth)
        inc a
        ld (pstRow),a
        ; "perft d ="
        ld a,(pstRow)
        ld b,a
        ld c,0
        ld hl,msgPerftN
        call printStr
        ld a,(pstRow)
        ld b,a
        ld c,6
        ld a,(perftDepth)
        add a,'0'
        push bc
        call printChar
        pop bc
        ; count at col 9
        ld a,(pstRow)
        ld b,a
        ld c,9
        call printDec32
        ; compare to expected[depth-1]
        ld a,(perftDepth)
        dec a
        add a,a
        add a,a
        ld e,a
        ld d,0
        ld hl,perftExpected
        add hl,de
        ld de,perftCnt
        ld b,4
pstCmp: ld a,(de)
        cp (hl)
        jr nz,pstBad
        inc de
        inc hl
        djnz pstCmp
        ld a,(pstRow)
        ld b,a
        ld c,20
        ld hl,msgOK
        call printStr
        jr pstNext
pstBad:
        ld a,(pstRow)
        ld b,a
        ld c,20
        ld hl,msgBAD
        call printStr
        ld a,0xFF
        ld (perftFail),a
pstNext:
        ld a,(perftDepth)
        inc a
        ld (perftDepth),a
        cp PERFT_MAXD+1
        jp nz,pstLoop
        ; --- positions exercising castling / en passant / promotion ---
        ld hl,kiwiBoard
        ld (rpBoard),hl
        ld a,3
        ld (rpDepth),a
        ld hl,kiwiExp
        ld (rpExp),hl
        ld a,7
        ld (pstRow),a
        ld b,7
        ld c,0
        ld hl,msgKiwi
        call printStr
        call runOnePerft
        ld hl,ep3Board
        ld (rpBoard),hl
        ld a,4
        ld (rpDepth),a
        ld hl,ep3Exp
        ld (rpExp),hl
        ld a,8
        ld (pstRow),a
        ld b,8
        ld c,0
        ld hl,msgEpT
        call printStr
        call runOnePerft
        ld hl,promoBoard
        ld (rpBoard),hl
        ld a,3
        ld (rpDepth),a
        ld hl,promoExp
        ld (rpExp),hl
        ld a,9
        ld (pstRow),a
        ld b,9
        ld c,0
        ld hl,msgPromo
        call printStr
        call runOnePerft
        ; --- zobrist key consistency ---
        ld b,10
        ld c,0
        ld hl,msgZob
        call printStr
        ld a,(keyMismatch)
        or a
        jr z,pstZobOk
        ld a,0xFF
        ld (perftFail),a
        ld b,10
        ld c,22
        ld hl,msgBAD
        call printStr
        jr pstVerdict
pstZobOk:
        ld b,10
        ld c,22
        ld hl,msgOK
        call printStr
        ; --- verdict ---
pstVerdict:
        ld a,(perftFail)
        or a
        ld hl,msgPerftOK
        jr z,pstShow
        ld hl,msgPerftBad
pstShow:
        ld b,11
        ld c,0
        call printStr
        call readKeyDebounced
        ret

; runOnePerft — set up (rpBoard), perft (rpDepth), compare (rpExp), print.
runOnePerft:
        ld hl,(rpBoard)
        ld a,h
        or l
        jr nz,ropSet
        call newGame
        jr ropRun
ropSet:
        call setupBoard
ropRun:
        ld hl,perftCnt
        ld (hl),0
        inc hl
        ld (hl),0
        inc hl
        ld (hl),0
        inc hl
        ld (hl),0
        xor a
        ld (searchPly),a
        ld a,(rpDepth)
        call perftRec
        ld a,(pstRow)
        ld b,a
        ld c,13
        call printDec32
        ld de,perftCnt
        ld hl,(rpExp)
        ld b,4
ropCmp: ld a,(de)
        cp (hl)
        jr nz,ropBad
        inc de
        inc hl
        djnz ropCmp
        ld a,(pstRow)
        ld b,a
        ld c,22
        ld hl,msgOK
        call printStr
        ret
ropBad:
        ld a,(pstRow)
        ld b,a
        ld c,22
        ld hl,msgBAD
        call printStr
        ld a,0xFF
        ld (perftFail),a
        ret

; setupBoard(HL=ptr) — 64 board bytes (a1..h8 rank-major) + side + castling + ep
setupBoard:
        push hl
        ld hl,board
        ld de,board+1
        ld bc,127
        ld (hl),0
        ldir
        pop hl
        ld c,0
sbRank: ld a,c
        add a,a
        add a,a
        add a,a
        add a,a
        ld e,a
        ld b,8
sbFile: ld a,(hl)
        inc hl
        push hl
        ld h,0xE0
        ld l,e
        ld (hl),a
        pop hl
        inc e
        djnz sbFile
        inc c
        ld a,c
        cp 8
        jr nz,sbRank
        ld a,(hl)
        ld (sideToMove),a
        inc hl
        ld a,(hl)
        ld (castling),a
        inc hl
        ld a,(hl)
        ld (epSquare),a
        call finalizePosition
        ret

; finalizePosition — derive state from board[]+side+castling+ep: locate the
; kings, reset clocks, and (re)compute the Zobrist key, phase and pstScore.
finalizePosition:
        ld c,0
sbK:    ld a,c
        and 0x88
        jr nz,sbKn
        ld h,0xE0
        ld l,c
        ld a,(hl)
        cp WK
        jr nz,sbK1
        ld a,c
        ld (wking),a
        jr sbKn
sbK1:   cp BK
        jr nz,sbKn
        ld a,c
        ld (bking),a
sbKn:   inc c
        ld a,c
        cp 0x78
        jr nz,sbK
        xor a
        ld (halfmove),a
        ld (searchPly),a
        ld a,0xFF
        ld (selSq),a
        ld hl,1
        ld (moveCount),hl
        call computeKey
        call computePhase
        call computePstScore
        ret

; --- test positions (64 board bytes a1..h8, then side, castling, ep) ---
; Kiwipete: r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -
kiwiBoard:
        defb WR,0,0,0,WK,0,0,WR
        defb WP,WP,WP,WB,WB,WP,WP,WP
        defb 0,0,WN,0,0,WQ,0,BP
        defb 0,BP,0,0,WP,0,0,0
        defb 0,0,0,WP,WN,0,0,0
        defb BB,BN,0,0,BP,BN,BP,0
        defb BP,0,BP,BP,BQ,BP,BB,0
        defb BR,0,0,0,BK,0,0,BR
        defb 0,0x0F,0xFF
kiwiExp:    defb 0x46,0x7E,0x01,0    ; perft(3)=97862
; Position 3: 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -
ep3Board:
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,WP,0,WP,0
        defb 0,0,0,0,0,0,0,0
        defb 0,WR,0,0,0,BP,0,BK
        defb WK,WP,0,0,0,0,0,BR
        defb 0,0,0,BP,0,0,0,0
        defb 0,0,BP,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0xFF
ep3Exp:     defb 0xE6,0xA8,0,0       ; perft(4)=43238
; Position 5: rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ -
promoBoard:
        defb WR,WN,WB,WQ,WK,0,0,WR
        defb WP,WP,WP,0,WN,BN,WP,WP
        defb 0,0,0,0,0,0,0,0
        defb 0,0,WB,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb 0,0,BP,0,0,0,0,0
        defb BP,BP,0,WP,BB,BP,BP,BP
        defb BR,BN,BB,BQ,0,BK,0,BR
        defb 0,0x03,0xFF
promoExp:   defb 0xAB,0xF3,0,0       ; perft(3)=62379

perftFail: defb 0
pstRow:    defb 0

perftExpected:
        defb 20,  0,   0,   0      ; 20
        defb 0x90,0x01,0,   0      ; 400
        defb 0xC6,0x22,0,   0      ; 8902
        defb 0xA1,0x02,0x03,0      ; 197281

; =====================================================================
;  printDec32 — print the 32-bit (perftCnt) at B=row, C=col.
; =====================================================================
printDec32:
        ld a,b
        ld (pdRow),a
        ld a,c
        ld (pdCol),a
        ld hl,perftCnt
        ld de,numBuf
        ld bc,4
        ldir
        xor a
        ld (pdN),a
pdGen:
        call div32by10          ; A = next digit (LSB first)
        push af
        ld a,(pdN)
        ld e,a
        ld d,0
        ld hl,pdDigits
        add hl,de
        pop af
        ld (hl),a
        ld hl,pdN
        inc (hl)
        ld a,(numBuf)
        ld hl,numBuf+1
        or (hl)
        inc hl
        or (hl)
        inc hl
        or (hl)
        jr nz,pdGen
        ld a,(pdCol)
        ld (pdCurCol),a
        ld a,(pdN)
        ld b,a                  ; digit count
pdPrint:
        ld a,(pdN)
        dec a
        ld (pdN),a
        ld e,a
        ld d,0
        ld hl,pdDigits
        add hl,de
        ld a,(hl)
        add a,'0'
        ld d,a                  ; char
        push bc
        ld a,(pdRow)
        ld b,a
        ld a,(pdCurCol)
        ld c,a
        ld a,d
        call printChar
        ld a,(pdCurCol)
        inc a
        ld (pdCurCol),a
        pop bc
        djnz pdPrint
        ret

; div32by10: (numBuf,4 bytes LE) /= 10, remainder -> A
div32by10:
        xor a                   ; running remainder
        ld b,4
        ld hl,numBuf+3          ; most-significant byte first
d32lp:
        ld d,a                  ; cur = rem*256 + byte  -> DE
        ld e,(hl)
        push hl
        ld hl,0                 ; quotient
d32sub:
        ld a,d
        or a
        jr nz,d32do
        ld a,e
        cp 10
        jr c,d32done
d32do:
        ld a,e
        sub 10
        ld e,a
        jr nc,d32nc
        dec d
d32nc:
        inc hl
        jr d32sub
d32done:
        ld a,l                  ; quotient byte (<256)
        pop hl
        ld (hl),a
        ld a,e                  ; remainder for next byte
        dec hl
        djnz d32lp
        ret
