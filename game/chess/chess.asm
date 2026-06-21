; =====================================================================
;  ZX-CHESS  —  a chess engine + game for the ZX Spectrum / HC-91
;
;  Board model : 0x88 (16x8), the classic fast 8-bit representation.
;  Engine      : negamax alpha-beta + quiescence, MVV-LVA ordering,
;                material + piece-square evaluation, selectable depth.
;  Rules       : full legal moves incl. castling, en passant, promotion,
;                check / checkmate / stalemate, fifty-move draw.
;  Display     : 8x8 board of 2x2 character squares, 16x16 piece glyphs,
;                ROM-font text (no paging), keyboard cursor input.
;
;  Build: pasmo --bin chess.asm chess.bin ; tools/zxtap.py -> bootable tap.
;  Origin 0x8000; big arrays are page-aligned at 0xB000+ so board[sq] is
;  simply (H=0xE0, L=sq).
; =====================================================================

        org 0x8000

; ---- piece codes -----------------------------------------------------
; 0 = empty.  type in bits0-2 (1=P 2=N 3=B 4=R 5=Q 6=K), colour in bit3.
WP equ 1
WN equ 2
WB equ 3
WR equ 4
WQ equ 5
WK equ 6
BP equ 9
BN equ 10
BB equ 11
BR equ 12
BQ equ 13
BK equ 14
COLBIT   equ 8
TYPEMASK equ 7

; ---- workspace (page-aligned at 0xE000) ------------------------------
board    equ 0xE000      ; 128 bytes, indexed by 0x88 square
sideToMove equ 0xE080    ; 0 = white to move, 8 = black to move
castling equ 0xE081      ; bit0 WK, bit1 WQ, bit2 BK, bit3 BQ
epSquare equ 0xE082      ; en-passant target 0x88 square, 0xFF = none
halfmove equ 0xE083      ; halfmove clock (fifty-move rule)
wking    equ 0xE084      ; white king square
bking    equ 0xE085      ; black king square
cursorSq equ 0xE086      ; UI cursor square
selSq    equ 0xE087      ; selected from-square, 0xFF = none
gameState equ 0xE088     ; 0 play,1 white-mated,2 black-mated,3 stalemate,4 50move
humanSide equ 0xE089     ; colour the human plays (0 white)
aiDepth  equ 0xE08A      ; search depth (difficulty 1..5)
searchPly equ 0xE08B     ; current ply within search
bestFrom equ 0xE08C
bestTo   equ 0xE08D
bestFlag equ 0xE08E
rngState equ 0xE08F      ; 2 bytes
moveCount equ 0xE093     ; full move number (2 bytes)
flipFlag equ 0xE095      ; board orientation
msgPtr   equ 0xE096      ; status message pointer (2 bytes)
evalAcc  equ 0xE098      ; evaluation accumulator (2 bytes)
mvFrom   equ 0xE09A
mvTo     equ 0xE09B
mvFlag   equ 0xE09C
atkSide  equ 0xE09D      ; attacker colour for isAttacked
genPtr   equ 0xE09E      ; move-generation write pointer (2 bytes)
genCount equ 0xE0A0      ; moves generated (byte)
tmpSq    equ 0xE0A1
saveAlpha equ 0xE0A2     ; (2)
saveBeta  equ 0xE0A4     ; (2)
bestScore equ 0xE0A6     ; (2)
dsSquare equ 0xE0A8
dsCol    equ 0xE0A9
dsRow    equ 0xE0AA
dsAttr   equ 0xE0AB
genFrom  equ 0xE0AC
genCurSq equ 0xE0AD
genTo    equ 0xE0AE
genFlag  equ 0xE0AF
mkPiece  equ 0xE0B0
mkSide   equ 0xE0B1
mkCaptured equ 0xE0B2
mkCapSq  equ 0xE0B3
mkSpecial equ 0xE0B4
mkPromo  equ 0xE0B5
iaDir    equ 0xE0B6
iaExp2   equ 0xE0B7
glRead   equ 0xE0B8      ; 2
glWrite  equ 0xE0BA      ; 2
glCount  equ 0xE0BC
glLegal  equ 0xE0BD
rootFrom equ 0xE0BE
rootTo   equ 0xE0BF
rootFlag equ 0xE0C0
perftCnt equ 0xE0C2      ; 4 bytes (32-bit node counter)
perftDepth equ 0xE0C6
numBuf   equ 0xE0C7      ; 4 bytes working value for division
pdDigits equ 0xE0CB      ; 11 bytes digit scratch
pdN      equ 0xE0D6
pdRow    equ 0xE0D7
pdCol    equ 0xE0D8
pdCurCol equ 0xE0D9
rpBoard  equ 0xE0DA      ; perft test: position pointer (0 = start)
rpDepth  equ 0xE0DC
rpExp    equ 0xE0DD      ; pointer to 4-byte expected count
nmScore  equ 0xE0DF      ; search: negated child score (2)
nmCd     equ 0xE0E1      ; search: child depth
qStand   equ 0xE0E2      ; quiescence stand-pat (2)
osIPtr   equ 0xE0E4      ; move-order scratch (2)
osJPtr   equ 0xE0E6      ; (2)
osMaxPtr equ 0xE0E8      ; (2)
osMaxScore equ 0xE0EA
osOuter  equ 0xE0EB
osInner  equ 0xE0EC
nodeLo   equ 0xE0ED      ; node counter (2)
nodeHi   equ 0xE0EF
pvFrom   equ 0xE0F1      ; root PV move hint for ordering
pvTo     equ 0xE0F2
twoPlayer equ 0xE0F3     ; 1 = human vs human
aidIter  equ 0xE0F4      ; current iterative-deepening depth
wpFile   equ 0xE0F5      ; white pawns per file (8)
bpFile   equ 0xE0FD      ; black pawns per file (8)
wBish    equ 0xE105
bBish    equ 0xE106
gamePhase equ 0xE107
matVal   equ 0xE108      ; (2)
nullEp    equ 0xE10A      ; saved ep square across a null move
hashKey  equ 0xE10C      ; 16-bit Zobrist key of the current position (2)
mkOldCastle equ 0xE10E
mkOldEp  equ 0xE10F
ttFrom   equ 0xE110      ; transposition-table move hint
ttTo     equ 0xE111
keyMismatch equ 0xE112   ; perft key self-test flag
gameKeyN equ 0xE113      ; plies recorded in the game key history
ckSave   equ 0xE115      ; (2) saved key during the consistency check
ttCurDepth equ 0xE117    ; TT probe: current search depth
ttEntDepth equ 0xE118
ttEntFlag equ 0xE119
ttEntScore equ 0xE11A    ; (2)
bestScoreTmp equ 0xE11C  ; (2) score to store in the TT
osTtF    equ 0xE11E      ; orderMoves TT-move scratch
osTtT    equ 0xE11F
lastScore equ 0xE120     ; (2) engine score of the last AI move
lastFrom equ 0xE122
lastTo   equ 0xE123
haveLast equ 0xE124      ; 1 once the engine has moved
osKF     equ 0xE125      ; killer bump scratch
osKT     equ 0xE126
osKScore equ 0xE127
gameUndoN equ 0xE128     ; plies on the take-back stack

killerArr equ 0xD100     ; 4 bytes/ply: k1from,k1to,k2from,k2to

; per-ply search arrays (continued, page 0xD4/0xD5)
origAlphaArr equ 0xD4F0  ; 16 * 2 = original alpha for TT bound flags
nbFromArr equ 0xD510     ; 16   node best-move from
nbToArr   equ 0xD518     ; 16   node best-move to
ttMvFromArr equ 0xD520   ; 16   per-ply TT move (survives recursion)
ttMvToArr equ 0xD530     ; 16

; Zobrist random tables (filled at start) and the transposition table,
; both in otherwise-unused RAM.
zobPiece  equ 0xD540     ; 12 pieces * 64 squares * 2 bytes = 1536
zobCastle equ 0xDB40     ; 16 * 2
zobEp     equ 0xDB60     ; 8 * 2
zobSide   equ 0xDB70     ; 2
TT_BASE   equ 0x6000     ; 1024 entries * 8 bytes = 8 KB
TT_MASK   equ 0x03FF
gameKeys  equ 0x5B00     ; game position-key history (2 bytes/ply)
gameUndo  equ 0x5D00     ; take-back stack: 48 plies * 16-byte undo records

PHASE_EG equ 8           ; below this non-pawn phase, use endgame king PST
DOUBLED  equ 12
ISOLATED equ 14
BISHOP_PAIR equ 30

; per-ply move buffers: base + ply*512 (128 moves * 4 bytes)
moveBufBase equ 0xB000   ; 0xB000..0xD000 = 16 plies
undoBase    equ 0xD000   ; base + ply*16

MV_REC  equ 4
INF     equ 30000
MATE    equ 29000
MAXPLY  equ 15
ASPW    equ 40           ; aspiration-window half-width (centipawns)

; flag byte: bits0-2 special, bits4-7 promo type
SP_NONE   equ 0
SP_DPUSH  equ 1
SP_OO     equ 2
SP_OOO    equ 3
SP_EP     equ 4

; =====================================================================
;  ENTRY
; =====================================================================
start:
        di
        ld sp,0xFFF0
        call seedRng
        call zobInit
        call newGame
        call drawScreenFull
mainLoop:
        ld a,(gameState)
        or a
        jp nz,gameOverLoop
        ld a,(twoPlayer)
        or a
        jr nz,humanTurn        ; both sides human
        ld a,(sideToMove)
        ld hl,humanSide
        cp (hl)
        jr nz,aiTurn
humanTurn:
        ld hl,msgYourMove
        call setMsg
        call drawStatus
        call humanMove
        jr afterMove
aiTurn:
        ld hl,msgThinking
        call setMsg
        call drawStatus
        call aiMove
afterMove:
        call pushGameUndo      ; save undo[0] for take-back
        call recordGameKey
        call updateTerminal
        call drawScreenFull
        jp mainLoop

gameOverLoop:
        call drawScreenFull
gov1:   call readKeyDebounced
        cp 'N'
        jr z,goNew
        cp ' '
        jr nz,gov1
goNew:  call newGame
        jp mainLoop

; =====================================================================
;  NEW GAME
; =====================================================================
newGame:
        ld hl,board
        ld de,board+1
        ld bc,127
        ld (hl),0
        ldir
        ld hl,startPos
        ld c,0                 ; rank
ngRank: ld a,c
        add a,a
        add a,a
        add a,a
        add a,a
        ld e,a                 ; base square = rank*16
        ld b,8
ngFile: ld a,(hl)
        inc hl
        push hl
        ld h,0xE0
        ld l,e
        ld (hl),a
        pop hl
        inc e
        djnz ngFile
        inc c
        ld a,c
        cp 8
        jr nz,ngRank
        xor a
        ld (sideToMove),a
        ld (humanSide),a
        ld (gameState),a
        ld (flipFlag),a
        ld (twoPlayer),a
        ld (haveLast),a
        ld (halfmove),a
        ld a,0x0F
        ld (castling),a
        ld a,0xFF
        ld (epSquare),a
        ld (selSq),a
        ld a,2
        ld (aiDepth),a
        xor a
        ld (searchPly),a
        ld a,0x14
        ld (cursorSq),a
        ld hl,1
        ld (moveCount),hl
        ld a,0x04
        ld (wking),a
        ld a,0x74
        ld (bking),a
        call computeKey
        xor a
        ld (gameKeyN),a
        ld (gameUndoN),a
        call recordGameKey     ; record the initial position
        call ttClear
        ret

startPos:
        defb WR,WN,WB,WQ,WK,WB,WN,WR
        defb WP,WP,WP,WP,WP,WP,WP,WP
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb BP,BP,BP,BP,BP,BP,BP,BP
        defb BR,BN,BB,BQ,BK,BB,BN,BR

; =====================================================================
;  DISPLAY
;  Board origin (BCOL,BROW); each square = 2x2 character cells.
;     col = BCOL + file*2 ,  row = BROW + (7-rank)*2   (rank 8 at top)
; =====================================================================
BCOL equ 4
BROW equ 2

drawScreenFull:
        call clearScreen
        call drawBoard
        call drawLabels
        call drawStatus
        call drawInfo
        ret

clearScreen:
        ld hl,0x4000
        ld de,0x4001
        ld bc,0x17FF
        ld (hl),0
        ldir
        ld hl,0x5800
        ld de,0x5801
        ld bc,0x2FF
        ld (hl),0x07
        ldir
        ret

drawBoard:
        ld c,0
dbRank: ld b,0
dbFile: push bc
        ld a,c
        add a,a
        add a,a
        add a,a
        add a,a
        add a,b
        call drawSquare
        pop bc
        inc b
        ld a,b
        cp 8
        jr nz,dbFile
        inc c
        ld a,c
        cp 8
        jr nz,dbRank
        ret

; draw one square; A = 0x88 square
drawSquare:
        ld (dsSquare),a
        call mapColRow         ; -> dsCol, dsRow
        ld a,(dsSquare)
        call squareAttr        ; -> A = attribute
        ld (dsAttr),a
        call setAttr2x2
        ; glyph source
        ld a,(dsSquare)
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jr nz,dsP
        ld hl,blankCell        ; 8 zeros; reused for 4 cells
        call drawBlank2x2
        ret
dsP:    and TYPEMASK
        dec a
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl               ; *32
        ld de,glyphs
        add hl,de
        call drawGlyph2x2
        ret

; map dsSquare to dsCol,dsRow (honours flipFlag)
mapColRow:
        ld a,(dsSquare)
        and 7
        ld b,a                 ; file
        ld a,(dsSquare)
        rrca
        rrca
        rrca
        rrca
        and 7
        ld c,a                 ; rank
        ld a,(flipFlag)
        or a
        jr z,mcrNo
        ld a,7
        sub b
        ld b,a
        jr mcrCol
mcrNo:  ld a,7
        sub c
        ld c,a                 ; displayed rank index (7-rank)
mcrCol: ld a,b
        add a,a
        add a,BCOL
        ld (dsCol),a
        ld a,c
        add a,a
        add a,BROW
        ld (dsRow),a
        ret

; attribute for dsSquare -> A
squareAttr:
        ld b,a
        and 7
        ld c,a
        ld a,b
        rrca
        rrca
        rrca
        rrca
        and 7
        add a,c
        and 1
        jr z,saDark
        ld d,0x30              ; light: paper 6 (yellow)
        jr saInk
saDark: ld d,0x10              ; dark: paper 2 (red)
saInk:  ld a,(dsSquare)
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jr z,saNo
        and COLBIT
        jr z,saWhite
        ld a,d                 ; black piece: ink 0 + paper
        jr saCur
saWhite:
        ld a,0x47              ; white piece: bright + ink 7
        or d
        jr saCur
saNo:   ld a,d
saCur:  ld e,a                 ; base attr in E
        ld a,(dsSquare)
        ld hl,cursorSq
        cp (hl)
        jr nz,saSel
        ld a,e
        and 0x47               ; keep ink+bright
        or 0x28                ; paper cyan(5)
        or 0x40
        ret
saSel:  ld a,(dsSquare)
        ld hl,selSq
        cp (hl)
        jr nz,saEnd
        ld a,e
        and 0x47
        or 0x20                ; paper green(4)
        or 0x40
        ret
saEnd:  ld a,e
        ret

; set 4 attribute cells of square (dsCol,dsRow) to dsAttr
setAttr2x2:
        ld a,(dsRow)
        ld b,a
        ld a,(dsCol)
        ld c,a
        call attrCell          ; HL = attr addr
        ld a,(dsAttr)
        ld (hl),a              ; TL
        inc hl
        ld (hl),a              ; TR
        ld de,31
        add hl,de
        ld (hl),a              ; BL
        inc hl
        ld (hl),a              ; BR
        ret

; --- glyph painters ---------------------------------------------------
; HL = 32-byte glyph (TL,TR,BL,BR), draws into (dsCol,dsRow)
drawGlyph2x2:
        push hl
        ld a,(dsRow)
        ld b,a
        ld a,(dsCol)
        ld c,a
        pop hl
        call drawCell8         ; TL
        ld a,(dsRow)
        ld b,a
        ld a,(dsCol)
        inc a
        ld c,a
        call drawCell8         ; TR (HL already advanced by 8)
        ld a,(dsRow)
        inc a
        ld b,a
        ld a,(dsCol)
        ld c,a
        call drawCell8         ; BL
        ld a,(dsRow)
        inc a
        ld b,a
        ld a,(dsCol)
        inc a
        ld c,a
        call drawCell8         ; BR
        ret

; blank a 2x2 block (zeros). HL=blankCell
drawBlank2x2:
        ld a,(dsRow)
        ld b,a
        ld a,(dsCol)
        ld c,a
        call zeroCell
        ld a,(dsRow)
        ld b,a
        ld a,(dsCol)
        inc a
        ld c,a
        call zeroCell
        ld a,(dsRow)
        inc a
        ld b,a
        ld a,(dsCol)
        ld c,a
        call zeroCell
        ld a,(dsRow)
        inc a
        ld b,a
        ld a,(dsCol)
        inc a
        ld c,a
        call zeroCell
        ret

; draw 8 source bytes (HL) into cell (B=row,C=col); advances HL by 8
drawCell8:
        push bc
        call cellPix           ; HL_dest in DE? -> returns DE=addr, preserves HL? no
        pop bc
        ; cellPix returns address in DE, source HL preserved
        ld b,8
dc8:    ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz dc8
        ret

; zero a cell (B=row,C=col)
zeroCell:
        call cellPix           ; DE = addr
        ld b,8
zc8:    xor a
        ld (de),a
        inc d
        djnz zc8
        ret

; cellPix: B=row(0..23) C=col(0..31) -> DE = top pixel address. HL preserved.
;   high = 0x40 | (row & 0x18) ;  low = ((row&7)<<5) | col
cellPix:
        ld a,b
        and 0x18
        or 0x40
        ld d,a
        ld a,b
        and 7
        rrca
        rrca
        rrca
        or c
        ld e,a
        ret

; attrCell: B=row C=col -> HL = 0x5800 + row*32 + col
;   high = 0x58 + (row>>3) ; low = ((row&7)<<5) | col
attrCell:
        ld a,b
        rrca
        rrca
        rrca
        and 3
        add a,0x58
        ld h,a
        ld a,b
        and 7
        rrca
        rrca
        rrca
        or c
        ld l,a
        ret

blankCell: defb 0,0,0,0,0,0,0,0

; --- text (ROM font at 0x3C00) ----------------------------------------
; printChar: A=char, B=row, C=col  (8x8 cell)
printChar:
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl              ; char*8
        ld de,0x3C00
        add hl,de              ; font src
        push hl
        call cellPix           ; DE = dest, but B,C consumed? cellPix uses B,C
        pop hl
        ld b,8
pc8:    ld a,(hl)
        ld (de),a
        inc hl
        inc d
        djnz pc8
        ret

; printStr: HL=0-terminated string, B=row, C=col
printStr:
ps1:    ld a,(hl)
        or a
        ret z
        push hl
        push bc
        call printChar
        pop bc
        pop hl
        inc hl
        inc c                  ; next column
        jr ps1

; clear a text row (row in B): zero pixels of 32 cells, set attr 0x07
clearRow:
        ld c,0
crl:    push bc
        call cellPix
        ld b,8
crl8:   xor a
        ld (de),a
        inc d
        djnz crl8
        pop bc
        inc c
        ld a,c
        cp 32
        jr nz,crl
        ret

drawLabels:
        ; files a-h under board at row BROW+16
        ld a,'a'
        ld (tmpSq),a
        ld b,0                 ; file
dlf:    ld a,(flipFlag)
        or a
        ld a,b
        jr z,dlf2
        ld a,7
        sub b
dlf2:   add a,a
        add a,BCOL
        ld c,a                 ; col
        ld a,'a'
        add a,b
        push bc
        ld b,BROW+16
        call printChar
        pop bc
        inc b
        ld a,b
        cp 8
        jr nz,dlf
        ; ranks 1-8 left of board at col BCOL-2
        ld b,0
dlr:    ld a,(flipFlag)
        or a
        ld a,b
        jr nz,dlr2
        ld a,7
        sub b                  ; no-flip: rank shown top-down
dlr2:   add a,a
        add a,BROW
        push af
        ld a,'1'
        add a,b
        ld d,a                 ; char
        pop af
        push bc
        ld b,a                 ; row
        ld c,BCOL-2
        ld a,d
        call printChar
        pop bc
        inc b
        ld a,b
        cp 8
        jr nz,dlr
        ; title
        ld hl,msgTitle
        ld b,0
        ld c,0
        call printStr
        ret

drawStatus:
        ld b,21
        call clearRow
        ld hl,(msgPtr)
        ld b,21
        ld c,0
        call printStr
        ; controls line
        ld b,23
        call clearRow
        ld hl,msgKeys
        ld b,23
        ld c,0
        call printStr
        ret

setMsg:                        ; HL = string
        ld (msgPtr),hl
        ret

; --- analysis info panel (right of the board) ------------------------
drawInfo:
        ld hl,msgLevel
        ld b,3
        ld c,20
        call printStr
        ld a,(aiDepth)
        add a,'0'
        ld b,3
        ld c,26
        call printChar
        ld a,(twoPlayer)
        or a
        jr z,diNo2p
        ld hl,msg2pL
        ld b,4
        ld c,20
        call printStr
diNo2p:
        ld a,(haveLast)
        or a
        ret z
        ld hl,msgMoveL
        ld b,6
        ld c,20
        call printStr
        ld a,(lastFrom)
        ld b,6
        ld c,25
        call printSq
        ld a,(lastTo)
        ld b,6
        ld c,27
        call printSq
        ld hl,msgEval
        ld b,7
        ld c,20
        call printStr
        ld hl,(lastScore)
        ld b,7
        ld c,25
        call printScore
        ret

; printSq(A=square, B=row, C=col) — coordinate like "e4"
printSq:
        push af
        and 7
        add a,'a'
        push bc
        call printChar
        pop bc
        inc c
        pop af
        rrca
        rrca
        rrca
        rrca
        and 7
        add a,'1'
        call printChar
        ret

; printScore(HL=signed value, B=row, C=col)
printScore:
        bit 7,h
        jr z,psPos
        push hl
        push bc
        ld a,'-'
        call printChar
        pop bc
        pop hl
        inc c
        ex de,hl
        ld hl,0
        or a
        sbc hl,de
psPos:
        ld (perftCnt),hl
        ld hl,0
        ld (perftCnt+2),hl
        call printDec32
        ret

; =====================================================================
;  KEYBOARD
;  Returns a code in A: 'Q''A''O''P' cursor, 13 enter, 32 space,
;  '1'..'5' difficulty, 'N' new, 'F' flip, 0 = none.
; =====================================================================
scanKeys:
        ld bc,0xFBFE           ; Q,W,E,R,T
        in a,(c)
        bit 0,a
        jr nz,sk_t
        ld a,'Q'
        ret
sk_t:   ld bc,0xFBFE
        in a,(c)
        bit 4,a                ; T = perft self-test
        jr nz,sk_v
        ld a,'T'
        ret
sk_v:   ld bc,0xFEFE           ; CAPS,Z,X,C,V
        in a,(c)
        bit 4,a                ; V = toggle two-player
        jr nz,sk_z
        ld a,'V'
        ret
sk_z:   ld bc,0xFEFE
        in a,(c)
        bit 1,a                ; Z = take back
        jr nz,sk_a
        ld a,'Z'
        ret
sk_a:   ld bc,0xFDFE           ; A,S,D,F,G
        in a,(c)
        bit 0,a
        jr nz,sk_f
        ld a,'A'
        ret
sk_f:   ld bc,0xFDFE
        in a,(c)
        bit 3,a
        jr nz,sk_op
        ld a,'F'
        ret
sk_op:  ld bc,0xDFFE           ; P,O,I,U,Y
        in a,(c)
        bit 0,a
        jr nz,sk_o
        ld a,'P'
        ret
sk_o:   ld bc,0xDFFE
        in a,(c)
        bit 1,a
        jr nz,sk_ent
        ld a,'O'
        ret
sk_ent: ld bc,0xBFFE           ; ENTER,L,K,J,H
        in a,(c)
        bit 0,a
        jr nz,sk_spc
        ld a,13
        ret
sk_spc: ld bc,0x7FFE           ; SPACE,SYM,M,N,B
        in a,(c)
        bit 0,a
        jr nz,sk_n
        ld a,' '
        ret
sk_n:   ld bc,0x7FFE
        in a,(c)
        bit 3,a
        jr nz,sk_dig
        ld a,'N'
        ret
sk_dig: ld bc,0xF7FE           ; 1,2,3,4,5
        in a,(c)
        ld e,a
        ld d,'1'
        ld b,5
sk_dl:  rra
        jr nc,sk_digHit
        inc d
        djnz sk_dl
        xor a
        ret
sk_digHit:
        ld a,d
        ret

readKeyDebounced:
        call scanKeys
        or a
        jr nz,readKeyDebounced ; wait release
rkdP:   call scanKeys
        or a
        jr z,rkdP              ; wait press
        ret

; =====================================================================
;  HUMAN MOVE
; =====================================================================
humanMove:
        call drawScreenFull
hmLoop: call readKeyDebounced
        cp 'Q'
        jp z,hmUp
        cp 'A'
        jp z,hmDown
        cp 'O'
        jp z,hmLeft
        cp 'P'
        jp z,hmRight
        cp 13
        jp z,hmSel
        cp ' '
        jp z,hmSel
        cp 'F'
        jp z,hmFlip
        cp 'N'
        jp z,hmNew
        cp 'T'
        jp z,hmPerft
        cp 'V'
        jp z,hmTwoP
        cp 'Z'
        jp z,hmTakeBack
        cp '1'
        jp c,hmLoop
        cp '6'
        jp nc,hmLoop
        ; set difficulty 1..5
        sub '0'
        ld (aiDepth),a
        ld hl,msgDiff
        call setMsg
        call drawStatus
        jp hmLoop

hmUp:   ld d,16                ; rank+1
        jr hmMove
hmDown: ld d,-16
        jr hmMove
hmLeft: ld d,-1
        jr hmMove
hmRight: ld d,1
hmMove: ld a,(cursorSq)
        add a,d
        ld e,a
        and 0x88
        jp nz,hmLoop           ; off board -> ignore
        ld a,e
        ld (cursorSq),a
        call drawBoard
        jp hmLoop

hmFlip: ld a,(flipFlag)
        xor 1
        ld (flipFlag),a
        call drawScreenFull
        jp hmLoop

hmNew:  call newGame
        call drawScreenFull
        jp hmLoop

hmPerft:
        call perftSelfTest     ; runs perft, shows results, waits for a key
        call newGame
        call drawScreenFull
        jp hmLoop

hmTwoP:
        ld a,(twoPlayer)
        xor 1
        ld (twoPlayer),a
        ld hl,msgTwoP
        call setMsg
        call drawStatus
        jp hmLoop

hmTakeBack:
        call takeBack
        ld hl,msgTaken
        call setMsg
        call drawScreenFull
        jp hmLoop

hmSel:  ld a,(selSq)
        cp 0xFF
        jr nz,hmHave
        ; no selection: select own piece under cursor
        ld a,(cursorSq)
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jp z,hmBad
        and COLBIT
        ld hl,sideToMove
        cp (hl)
        jp nz,hmBad
        ld a,(cursorSq)
        ld (selSq),a
        call drawBoard
        jp hmLoop
hmHave: ; have selection
        ld a,(cursorSq)
        ld hl,selSq
        cp (hl)
        jr nz,hmTry
        ; clicked same square -> deselect
        ld a,0xFF
        ld (selSq),a
        call drawBoard
        jp hmLoop
hmTry:  ; if cursor is own piece, reselect
        ld a,(cursorSq)
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jr z,hmTry2
        and COLBIT
        ld hl,sideToMove
        cp (hl)
        jr nz,hmTry2
        ld a,(cursorSq)
        ld (selSq),a
        call drawBoard
        jp hmLoop
hmTry2: ; attempt move selSq -> cursor; validate against legal list
        call validateHumanMove ; CF set if legal; mvFrom/mvTo/mvFlag set
        jr nc,hmIllegal
        xor a
        ld (searchPly),a
        call makeMove
        ld a,0xFF
        ld (selSq),a
        ret
hmIllegal:
        ld hl,msgIllegal
        call setMsg
        call drawStatus
        jp hmLoop
hmBad:  ld hl,msgPick
        call setMsg
        call drawStatus
        jp hmLoop

; Validate selSq->cursorSq.  Generates legal moves; if found sets
; mvFrom/mvTo/mvFlag and returns CF=1.  Auto-queens promotions.
validateHumanMove:
        xor a
        ld (searchPly),a
        call genLegal          ; legal list at current-ply buffer
        ld a,(genCount)
        ld b,a
        or a
        jr z,vhmNo
        call curMoveBuf        ; HL = list base
vhmL:   push hl
        ld a,(selSq)
        cp (hl)                ; from?
        jr nz,vhmSkip
        inc hl
        ld a,(cursorSq)
        cp (hl)                ; to?
        jr nz,vhmSkip
        ; match! restore record start
        pop hl
        ld a,(hl)
        ld (mvFrom),a
        inc hl
        ld a,(hl)
        ld (mvTo),a
        inc hl
        ld a,(hl)              ; flag
        ld (mvFlag),a
        call maybeForceQueen   ; auto-queen promotions
        scf
        ret
vhmSkip:
        pop hl
        ld de,4
        add hl,de              ; next 4-byte record
        djnz vhmL
vhmNo:  or a                   ; CF=0
        ret

; if the moved piece is a pawn reaching last rank, force promo to queen
maybeForceQueen:
        ld a,(mvFrom)
        ld h,0xE0
        ld l,a
        ld a,(hl)
        and TYPEMASK
        cp WP
        ret nz
        ld a,(mvTo)
        and 0x70               ; rank bits
        jr z,mfqYes            ; rank 0
        cp 0x70
        ret nz                 ; not last rank
mfqYes: ld a,(mvFlag)
        and 0x0F               ; keep special bits
        or 0x50                ; promo = queen (5<<4)
        ld (mvFlag),a
        ret

        include "movegen.inc"
        include "engine.inc"
        include "perft.inc"
        include "zobrist.inc"
        include "tt.inc"

; =====================================================================
;  MISC
; =====================================================================
seedRng:
        ld hl,0xA55A
        ld (rngState),hl
        ret

; 16-bit xorshift-ish PRNG -> A
rng:
        ld hl,(rngState)
        ld a,h
        rra
        ld a,l
        rra
        xor h
        ld h,a
        ld a,l
        xor h
        ld l,a
        ld (rngState),hl
        ld a,l
        ret

; =====================================================================
;  STRINGS
; =====================================================================
msgTitle:    defb "ZX-CHESS  HC-91",0
msgYourMove: defb "Your move          ",0
msgThinking: defb "Thinking...        ",0
msgIllegal:  defb "Illegal move       ",0
msgPick:     defb "Pick your piece    ",0
msgDiff:     defb "Difficulty set     ",0
msgKeys:     defb "QAOP=move ENT=pick N T V F 1-5",0
msgPerftHdr: defb "PERFT self-test (start position)",0
msgPerftN:   defb "perft",0
msgOK:       defb "OK",0
msgBAD:      defb "BAD",0
msgPerftOK:  defb "PERFT OK - movegen verified",0
msgPerftBad: defb "PERFT BAD - movegen error",0
msgKiwi:     defb "kiwipete d3",0
msgEpT:      defb "enpassant d4",0
msgPromo:    defb "promotion d3",0
msgZob:      defb "zobrist key",0
msgWmate:    defb "Checkmate! Black wins   SPC=new",0
msgBmate:    defb "Checkmate! White wins   SPC=new",0
msgStale:    defb "Stalemate - draw        SPC=new",0
msgDraw:     defb "Draw (50-move)          SPC=new",0
msgMat:      defb "Draw - insufficient mtl SPC=new",0
msgRep:      defb "Draw - repetition       SPC=new",0
msgTwoP:     defb "Two-player mode toggled",0
msgTaken:    defb "Take back done",0
msgLevel:    defb "Level",0
msg2pL:      defb "2-player",0
msgMoveL:    defb "Move",0
msgEval:     defb "Eval",0
msgCheck:    defb "Check!             ",0

        include "pieces.inc"
