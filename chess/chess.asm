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
rfEval   equ 0xE129      ; (2) reverse-futility static eval
humanLastFrom equ 0xE12B ; the human's last move (for the opening book)
humanLastTo equ 0xE12C
bkF      equ 0xE12D      ; book move scratch (genLegal clobbers mvFrom/To)
bkT      equ 0xE12E
lmrPA    equ 0xE12F      ; (2) parent alpha captured for LMR re-search
lmrPB    equ 0xE131      ; (2) parent beta
lmrReduced equ 0xE133    ; 1 if the current child was searched reduced
matBalTmp equ 0xE134     ; (2) material-balance accumulator
openingNamePtr equ 0xE136 ; (2) opening name string, 0 = none
wAny     equ 0xE138      ; white has a non-king piece
bAny     equ 0xE139
wHeavy   equ 0xE13A      ; white has a rook or queen
bHeavy   equ 0xE13B
ckSavePhase equ 0xE13C   ; perft self-test: saved gamePhase
pstScore equ 0xE13D      ; (2) incremental material+PST, non-king, white-rel
pdAcc    equ 0xE13F      ; (2) pst delta accumulator
pdTmp    equ 0xE141      ; (2)
pdRookFrom equ 0xE143
pdRookTo equ 0xE144
ckSavePst equ 0xE145     ; (2) perft self-test: saved pstScore
wClock   equ 0xE147      ; (2) white time remaining, in 50 Hz frames
bClock   equ 0xE149      ; (2) black time remaining, in 50 Hz frames
clkTurnStart equ 0xE14B  ; (2) FRAMES snapshot at the start of this turn
clkTurnSide equ 0xE14D    ; side that owns the current turn (0/8)
clkDispW equ 0xE14E      ; (2) white time to display (stored - live elapsed)
clkDispB equ 0xE150      ; (2) black time to display
clkLastSec equ 0xE152    ; throttle: last to-move whole-second drawn
clkBuf   equ 0xE153      ; (5) "M:SS",0 formatting buffer

FRAMES   equ 0x5C78      ; ROM 50 Hz frame counter (3 bytes), low 16 used
INITCLK  equ 15000       ; starting time per side: 5:00 at 50 Hz
is128    equ 0xE158      ; 1 on a 128K machine (paging available), else 0
colorScheme equ 0xE159   ; selected board colour scheme (0..NSCHEMES-1)
whiteStyle equ 0xE15A    ; white-piece style: 0 = outline, 1 = white fill
seeTo    equ 0xE15B      ; SEE capture-ordering: target square scratch
seeBad   equ 0xE15C      ; SEE capture-ordering: 1 if capture loses material
moveLogN equ 0xE15D      ; plies recorded in the full move history (cap 255)
blackDepth equ 0xE15E    ; Black's search depth (odds / handicap play)
effDepth equ 0xE15F      ; effective depth for the side currently moving
moveLog  equ 0xE200      ; full game move history: 2 bytes/ply (from,to)
saveBuf  equ 0xE160      ; game-save buffer: 64 board + side/cas/ep + extras
SAVELEN  equ 71          ; 64 + side + castle + ep + halfmove + moveCount(2) + depth
SA_BYTES equ 0x04C2      ; ROM tape save  (IX=addr, DE=len, A=flag)
LD_BYTES equ 0x0556      ; ROM tape load  (IX=addr, DE=len, A=flag, CF=load)

killerArr equ 0xD100     ; 4 bytes/ply: k1from,k1to,k2from,k2to
inChkArr  equ 0xD140     ; 1/ply: side-to-move in check at this node

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
historyTbl equ 0xDC00    ; 6 piece types * 64 squares (quiet-move history)
TT_BASE   equ 0xC000     ; 512 entries * 8 bytes = 4 KB (0xC000..0xCFFF)
TT_MASK   equ 0x01FF
gameKeys  equ 0x5B00     ; game position-key history (2 bytes/ply)
gameUndo  equ 0x5D00     ; take-back stack: 48 plies * 16-byte undo records

PHASE_EG equ 8           ; below this non-pawn phase, use endgame king PST
DOUBLED  equ 12
ISOLATED equ 14
BISHOP_PAIR equ 30

; per-ply move buffers: base + ply*512 (128 moves * 4 bytes).  Placed at
; 0x6000 (below the program) so the search never overwrites the program's
; glyphs/strings, which now extend past 0xB000.
moveBufBase equ 0x6000   ; 0x6000..0x7FFF = 16 plies
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
        ld iy,0x5C3A           ; ROM sysvar base, for the IM1 keyboard/FRAMES ISR
        im 1
        ei                     ; let the ROM tick FRAMES (0x5C78) at 50 Hz
        call detect128         ; set is128: enables the banked transposition table
        xor a
        ld (colorScheme),a     ; default scheme 0 = Classic (yellow/red)
        ld (whiteStyle),a      ; default white pieces = outline (contour)
        call seedRng
        call zobInit
        call newGame
        call drawScreenFull
mainLoop:
        ld a,(gameState)
        or a
        jp nz,gameOverLoop
        call clkStartTurn      ; begin charging time to the side to move
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
        call moveSound
        call pushGameUndo      ; save undo[0] for take-back
        call recordGameKey
        call recordMoveLog     ; full move history (from,to per ply)
        call updateTerminal
        call clkCommit         ; charge the finished turn; may flag-fall
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
        ld (openingNamePtr),a
        ld (openingNamePtr+1),a
        ld (halfmove),a
        ld a,0x0F
        ld (castling),a
        ld a,0xFF
        ld (epSquare),a
        ld (selSq),a
        ld a,2
        ld (aiDepth),a
        ld (blackDepth),a       ; symmetric by default (Black matches White)
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
        call computePhase
        call computePstScore
        xor a
        ld (gameKeyN),a
        ld (gameUndoN),a
        ld (moveLogN),a
        call recordGameKey     ; record the initial position
        call ttClear
        call clearHistory
        call clkInit
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
dsP:    ld c,a                 ; save raw piece (colour bit needed below)
        and TYPEMASK
        dec a
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl               ; *32
        ld a,c
        and COLBIT
        jr nz,dsBlack          ; black -> solid silhouette
        ld a,(whiteStyle)      ; white: fill mode -> solid, outline mode -> outline
        or a
        jr nz,dsBlack          ; fill mode uses the solid glyph (white ink)
        ld de,glyphsW          ; outline mode -> hollow outline (black keyline)
        jr dsAddGlyph
dsBlack: ld de,glyphs
dsAddGlyph:
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

; attribute for dsSquare -> A.  The paper is the square colour from the
; active scheme (cursor/selected squares override it); only the ink varies.
; Outline mode (whiteStyle=0) uses the schemeTable palettes (light squares
; for black ink to read) and ink 0 for every piece — black draws a solid
; silhouette, white an outline.  Fill mode (whiteStyle=1) uses the separate
; schemeTableFill palettes — mid-tone squares where BOTH a solid black and
; a solid white (ink 7) piece read across the whole board — and gives white
; pieces ink 7.  schemeAttr selects the table; rows are [light, dark,
; cursor, selected].
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
        and 1                  ; 1 = light square, 0 = dark square
        jr z,saDark
        xor a                  ; field 0: light
        jr saPaper
saDark: ld a,1                 ; field 1: dark
saPaper:
        ld b,a                 ; B = light/dark field
        ld a,(dsSquare)
        ld hl,cursorSq
        cp (hl)
        ld a,2                 ; cursor field
        jr z,saField
        ld a,(dsSquare)
        ld hl,selSq
        cp (hl)
        ld a,3                 ; selected field
        jr z,saField
        ld a,b                 ; plain square: light/dark
saField:
        call schemeAttr        ; A = paper attribute (bright, ink 0)
        ld e,a
        ; white piece in fill mode -> white ink (solid, no contour)
        ld a,(whiteStyle)
        or a
        jr z,saEnd             ; outline mode -> ink 0
        ld a,(dsSquare)
        ld h,0xE0
        ld l,a
        ld a,(hl)
        or a
        jr z,saEnd             ; empty
        and COLBIT
        jr nz,saEnd            ; black piece -> ink 0
        ld a,e
        or 0x07                ; white piece, fill mode: ink 7 (white)
        ret
saEnd:  ld a,e
        ret

; schemeAttr(A = field 0..3) -> A = attribute byte.  Outline mode reads the
; schemeTable palettes, fill mode the schemeTableFill palettes.
schemeAttr:
        ld e,a
        ld a,(colorScheme)
        add a,a
        add a,a                ; scheme*4
        add a,e
        ld e,a
        ld d,0
        ld a,(whiteStyle)
        or a
        ld hl,schemeTable
        jr z,saTbl
        ld hl,schemeTableFill
saTbl:  add hl,de
        ld a,(hl)
        ret

; Colour schemes — 4 attribute bytes each: light, dark, cursor, selected.
NSCHEMES equ 3
; Outline mode: light squares so the black-ink outlines/silhouettes read.
schemeTable:
        defb 0x70,0x50,0x68,0x58   ; 0 Classic: yellow / red   (cyan, magenta)
        defb 0x70,0x60,0x68,0x58   ; 1 Meadow:  yellow / green (cyan, magenta)
        defb 0x78,0x68,0x60,0x58   ; 2 Clean:   white  / cyan  (green, magenta)
; Fill mode: mid-tone squares where a solid white AND a solid black piece
; both read on every square (no near-white or near-black squares).
; Cursor is blue: it's not a square colour in any fill scheme and contrasts
; with every mid-tone (a green/cyan cursor blended into cyan/green squares).
schemeTableFill:
        defb 0x60,0x50,0x48,0x58   ; 0 Holly:  green / red     (blue, magenta)
        defb 0x60,0x58,0x48,0x50   ; 1 Orchid: green / magenta (blue, red)
        defb 0x50,0x68,0x48,0x58   ; 2 Coral:  red   / cyan    (blue, magenta)
schemeNames:
        defw nmSchClassic
        defw nmSchMeadow
        defw nmSchClean
schemeNamesFill:
        defw nmSchHolly
        defw nmSchOrchid
        defw nmSchCoral
whiteNames:
        defw nmWOutline
        defw nmWFilled

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

; drawScheme — show the active colour scheme + its cycle key (C) at row 16
drawScheme:
        ld hl,msgColK
        ld b,16
        ld c,20
        call printStr
        ld a,(colorScheme)
        add a,a
        ld e,a
        ld d,0
        ld a,(whiteStyle)
        or a
        ld hl,schemeNames
        jr z,dschNm
        ld hl,schemeNamesFill
dschNm: add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a                 ; HL = scheme-name string
        ld b,16
        ld c,22
        call printStr
        ; fall through to the white-piece style line
; drawWhiteStyle — show the white-piece style + its toggle key (W) at row 17
drawWhiteStyle:
        ld hl,msgWhiteK
        ld b,17
        ld c,20
        call printStr
        ld a,(whiteStyle)
        add a,a
        ld e,a
        ld d,0
        ld hl,whiteNames
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a                 ; HL = style-name string
        ld b,17
        ld c,22
        call printStr
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
        ; opening name (if a book line was used)
        ld hl,(openingNamePtr)
        ld a,h
        or l
        jr z,diNoName
        ld b,11
        ld c,20
        call printStr
diNoName:
        call drawClocks        ; per-side time, rows 13/14
        ; material balance (pawns) - always shown
        ld hl,msgMatl
        ld b,9
        ld c,20
        call printStr
        call materialBalance
        call div100s
        ld b,9
        ld c,25
        call printScore
        call drawScheme        ; "C:" + active colour scheme, row 16
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

; materialBalance -> HL = (white material - black material) in centipawns
materialBalance:
        ld hl,0
        ld (matBalTmp),hl
        ld c,0
mbLoop:
        ld a,c
        and 0x88
        jr nz,mbNext
        ld h,0xE0
        ld l,c
        ld a,(hl)
        or a
        jr z,mbNext
        ld b,a
        and 7
        add a,a
        ld e,a
        ld d,0
        ld hl,materialTbl
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)                ; DE = material value
        ld a,b
        and 8
        jr nz,mbBlack
        ld hl,(matBalTmp)
        add hl,de
        ld (matBalTmp),hl
        jr mbNext
mbBlack:
        ld hl,(matBalTmp)
        or a
        sbc hl,de
        ld (matBalTmp),hl
mbNext:
        inc c
        ld a,c
        cp 0x78
        jr nz,mbLoop
        ld hl,(matBalTmp)
        ret

; div100s — HL = HL / 100, truncated toward zero, signed
div100s:
        bit 7,h
        jr z,d100p
        ex de,hl
        ld hl,0
        or a
        sbc hl,de                ; HL = -HL (now positive)
        call d100u
        ex de,hl
        ld hl,0
        or a
        sbc hl,de                ; restore sign
        ret
d100p:
        call d100u
        ret
d100u:                           ; HL = HL/100 (unsigned, small values)
        ld de,100
        ld bc,0
d100L:
        ld a,h
        cp d
        jr c,d100D
        jr nz,d100S
        ld a,l
        cp e
        jr c,d100D
d100S:
        or a
        sbc hl,de
        inc bc
        jr d100L
d100D:
        ld h,b
        ld l,c
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
        jr nz,sk_e
        ld a,'T'
        ret
sk_e:   ld bc,0xFBFE           ; Q,W,E,R,T
        in a,(c)
        bit 2,a                ; E = load endgame demo (KRK)
        jr nz,sk_w
        ld a,'E'
        ret
sk_w:   ld bc,0xFBFE
        in a,(c)
        bit 1,a                ; W = toggle white-piece style
        jr nz,sk_v
        ld a,'W'
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
        jr nz,sk_c
        ld a,'Z'
        ret
sk_c:   ld bc,0xFEFE
        in a,(c)
        bit 3,a                ; C = cycle colour scheme
        jr nz,sk_a
        ld a,'C'
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
        jr nz,sk_s
        ld a,'F'
        ret
sk_s:   ld bc,0xFDFE
        in a,(c)
        bit 1,a                ; S = set-up position editor
        jr nz,sk_g
        ld a,'S'
        ret
sk_g:   ld bc,0xFDFE
        in a,(c)
        bit 4,a                ; G = save game to tape
        jr nz,sk_op
        ld a,'G'
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
        jr nz,sk_l
        ld a,13
        ret
sk_l:   ld bc,0xBFFE
        in a,(c)
        bit 1,a                ; L = load game from tape
        jr nz,sk_spc
        ld a,'L'
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
hmLoop: call clkWaitKey        ; like readKeyDebounced, but ticks the clock
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
        cp 'E'
        jp z,hmEndgame
        cp 'S'
        jp z,hmSetup
        cp 'C'
        jp z,hmColor
        cp 'W'
        jp z,hmWhiteStyle
        cp 'G'
        jp z,hmSave
        cp 'L'
        jp z,hmLoad
        cp '1'
        jp c,hmLoop
        cp '6'
        jp nc,hmLoop
        ; set difficulty 1..5 (both sides; odds play sets blackDepth apart)
        sub '0'
        ld (aiDepth),a
        ld (blackDepth),a
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

hmColor:
        ld a,(colorScheme)
        inc a
        cp NSCHEMES
        jr c,hmcSet
        xor a
hmcSet: ld (colorScheme),a
        ld hl,msgColour
        call setMsg
        call drawScreenFull    ; repaint the board in the new scheme
        jp hmLoop

hmWhiteStyle:
        ld a,(whiteStyle)
        xor 1
        ld (whiteStyle),a
        or a
        ld hl,msgWOut
        jr z,hwsMsg
        ld hl,msgWFill
hwsMsg: call setMsg
        call drawScreenFull    ; repaint white pieces in the new style
        jp hmLoop

hmTakeBack:
        call takeBack
        ld hl,msgTaken
        call setMsg
        call drawScreenFull
        jp hmLoop

hmEndgame:
        ld hl,krkPos
        call loadGamePos
        ld sp,0xFFF0           ; unwind back to a clean main loop
        jp mainLoop

; hmSave — write the game state to tape (ROM SA-BYTES); hmLoad reads it back.
; The 71-byte block is laid out exactly like setupBoard's input (64 board
; bytes a1..h8 + side + castling + ep) followed by halfmove, moveCount and
; the difficulty, so loading reuses setupBoard and then restores the extras.
hmSave:
        call packState
        di                     ; SA-BYTES is interrupt-timing critical
        ld ix,saveBuf
        ld de,SAVELEN
        ld a,0xFF              ; data block
        call SA_BYTES
        ei
        ld hl,msgSaved
        call setMsg
        call drawStatus
        jp hmLoop

hmLoad:
        di
        ld ix,saveBuf
        ld de,SAVELEN
        ld a,0xFF
        scf                    ; CF set = load (not verify)
        call LD_BYTES
        ei
        jr nc,hmLoadErr
        ld hl,saveBuf
        call setupBoard        ; board + side/castling/ep, finalize position
        call resetGameState    ; fresh history / TT / clocks
        ld a,(saveBuf+67)
        ld (halfmove),a
        ld hl,(saveBuf+68)
        ld (moveCount),hl
        ld a,(saveBuf+70)
        ld (aiDepth),a
        ld a,0xFF
        ld (selSq),a
        ld sp,0xFFF0           ; clean stack, return to the main loop
        ld hl,msgLoaded
        call setMsg
        jp mainLoop
hmLoadErr:
        ld hl,msgLoadErr
        call setMsg
        call drawStatus
        jp hmLoop

; packState — gather the live game state into saveBuf in setupBoard layout.
packState:
        ld de,saveBuf          ; 64 board bytes, rank-major a1..h8
        ld c,0                 ; rank
psR:    ld a,c
        add a,a
        add a,a
        add a,a
        add a,a
        ld l,a
        ld h,0xE0              ; board base
        ld b,8                 ; files
psF:    ld a,(hl)
        ld (de),a
        inc hl
        inc de
        djnz psF
        inc c
        ld a,c
        cp 8
        jr nz,psR
        ld a,(sideToMove)
        ld (de),a
        inc de
        ld a,(castling)
        ld (de),a
        inc de
        ld a,(epSquare)
        ld (de),a
        inc de
        ld a,(halfmove)
        ld (de),a
        inc de
        ld hl,(moveCount)
        ld a,l
        ld (de),a
        inc de
        ld a,h
        ld (de),a
        inc de
        ld a,(aiDepth)
        ld (de),a
        ret

hmSetup:
        call setupEditor       ; never returns (jp mainLoop inside)

; setupEditor — place pieces with the cursor, then start a game from the
; resulting position.  SPACE cycles the square's piece, W toggles the side
; to move, C clears, ENTER plays.
setupEditor:
        xor a
        ld (castling),a        ; editor positions: no castling rights
        ld a,0xFF
        ld (epSquare),a
seLoop:
        call drawScreenFull
        ld hl,msgSetup
        ld b,21
        ld c,0
        call clearRow
        ld b,21
        ld c,0
        call printStr
        call seScan
        or a
        jr z,seLoop
seWait:                        ; (debounce: wait for release)
        push af
sePoll: call seScan
        or a
        jr nz,sePoll
        pop af
        cp 'Q'
        jr z,seUp
        cp 'A'
        jr z,seDown
        cp 'O'
        jr z,seLeft
        cp 'P'
        jr z,seRight
        cp ' '
        jr z,seCycle
        cp 'W'
        jr z,seSide
        cp 'C'
        jr z,seClear
        cp 13
        jr z,seDone
        jr seLoop
seUp:   ld d,16
        jr seMove
seDown: ld d,-16
        jr seMove
seLeft: ld d,-1
        jr seMove
seRight: ld d,1
seMove: ld a,(cursorSq)
        add a,d
        ld e,a
        and 0x88
        jp nz,seLoop
        ld a,e
        ld (cursorSq),a
        jp seLoop
seCycle:
        ld a,(cursorSq)
        ld l,a
        ld h,0xE0
        ld a,(hl)
        call nextPiece
        ld (hl),a
        jp seLoop
seSide: ld a,(sideToMove)
        xor 8
        ld (sideToMove),a
        jp seLoop
seClear:
        ld hl,board
        ld de,board+1
        ld bc,127
        ld (hl),0
        ldir
        jp seLoop
seDone:
        call finalizePosition
        call resetGameState
        ld sp,0xFFF0
        jp mainLoop

; nextPiece(A) -> next in the cycle empty,WP..WK,BP..BK,empty
nextPiece:
        or a
        jr nz,np1
        ld a,WP
        ret
np1:    cp WK
        jr nz,np2
        ld a,BP
        ret
np2:    cp BK
        jr nz,np3
        xor a
        ret
np3:    inc a
        ret

; seScan — editor keys: Q/A/O/P cursor, SPACE cycle, W side, C clear, ENTER
seScan:
        ld bc,0xFBFE
        in a,(c)
        bit 0,a
        jr nz,ses1
        ld a,'Q'
        ret
ses1:   ld bc,0xFBFE
        in a,(c)
        bit 1,a                ; W
        jr nz,ses2
        ld a,'W'
        ret
ses2:   ld bc,0xFDFE
        in a,(c)
        bit 0,a
        jr nz,ses3
        ld a,'A'
        ret
ses3:   ld bc,0xDFFE
        in a,(c)
        bit 0,a
        jr nz,ses4
        ld a,'P'
        ret
ses4:   ld bc,0xDFFE
        in a,(c)
        bit 1,a
        jr nz,ses5
        ld a,'O'
        ret
ses5:   ld bc,0x7FFE
        in a,(c)
        bit 0,a
        jr nz,ses6
        ld a,' '
        ret
ses6:   ld bc,0xFEFE
        in a,(c)
        bit 3,a                ; C
        jr nz,ses7
        ld a,'C'
        ret
ses7:   ld bc,0xBFFE
        in a,(c)
        bit 0,a                ; ENTER
        jr nz,ses8
        ld a,13
        ret
ses8:   xor a
        ret

; loadGamePos(HL=ptr) — set up an arbitrary position and reset game state
loadGamePos:
        call setupBoard        ; board, side, castling, ep, kings, key
        ; fall through to resetGameState
resetGameState:
        xor a
        ld (gameState),a
        ld (haveLast),a
        ld (gameKeyN),a
        ld (gameUndoN),a
        ld (moveLogN),a
        ld (openingNamePtr),a
        ld (openingNamePtr+1),a
        ld a,0xFF
        ld (selSq),a
        call recordGameKey
        call ttClear
        call clearHistory
        call clkInit
        ret

; KRK endgame demo: white Ke1 (lone), black Ke8 + Ra8, black (engine) to move
krkPos:
        defb 0,0,0,0,WK,0,0,0       ; rank1: white Ke1
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb 0,0,0,0,0,0,0,0
        defb BR,0,0,0,BK,0,0,0      ; rank8: black Ra8, Ke8
        defb 8,0,0xFF              ; side=black, no castling, no ep

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
        ld a,(mvFrom)
        ld (humanLastFrom),a
        ld a,(mvTo)
        ld (humanLastTo),a
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
        call maybeForceQueen   ; promotion piece chooser
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
mfqYes: call promptPromo        ; A = chosen piece type (2..5)
        add a,a
        add a,a
        add a,a
        add a,a                ; type << 4
        ld b,a
        ld a,(mvFlag)
        and 0x0F               ; keep special bits
        or b
        ld (mvFlag),a
        ret

; promptPromo — ask the human for the promotion piece; A = 2(N)..5(Q)
promptPromo:
        ld hl,msgPromote
        call setMsg
        call drawStatus
ppRel:  call ppScan
        or a
        jr nz,ppRel            ; wait release
ppWait: call ppScan
        or a
        jr z,ppWait            ; wait a Q/R/B/N press
        ret
ppScan:
        ld bc,0xFBFE           ; Q -> queen
        in a,(c)
        bit 0,a
        jr nz,pps1
        ld a,5
        ret
pps1:   ld bc,0xFBFE           ; R -> rook
        in a,(c)
        bit 3,a
        jr nz,pps2
        ld a,4
        ret
pps2:   ld bc,0x7FFE           ; B -> bishop
        in a,(c)
        bit 4,a
        jr nz,pps3
        ld a,3
        ret
pps3:   ld bc,0x7FFE           ; N -> knight
        in a,(c)
        bit 3,a
        jr nz,pps4
        ld a,2
        ret
pps4:   xor a
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

; moveSound — a short move cue: an AY-3-8912 blip (audible on the 128K
; family, harmless no-op on 48K) layered over a beeper click.
moveSound:
        xor a
        ld e,0
        call ayWrite            ; R0 tone fine = 0
        ld a,1
        ld e,1
        call ayWrite            ; R1 tone coarse = 1  (period 0x100, ~430 Hz)
        ld a,7
        ld e,0xFE
        call ayWrite            ; R7 mixer: tone A enabled
        ld a,8
        ld e,0x0F
        call ayWrite            ; R8 channel-A amplitude (max)
        ld b,90                 ; beeper click (AY note plays alongside)
        ld a,0x17               ; border 7 + speaker bit set
msLoop:
        out (0xFE),a
        xor 0x10                ; toggle speaker
        ld c,45                 ; pitch delay
msDelay:
        dec c
        jr nz,msDelay
        djnz msLoop
        ld a,7
        out (0xFE),a            ; restore border
        ld de,9000              ; brief pure-AY tail (silent on 48K)
msTail: dec de
        ld a,d
        or e
        jr nz,msTail
        ld a,8
        ld e,0
        call ayWrite            ; silence channel A
        ret

; ayWrite(A=register, E=value) — select via 0xFFFD, write via 0xBFFD
ayWrite:
        ld bc,0xFFFD
        out (c),a
        ld bc,0xBFFD
        out (c),e
        ret

; detect128 — is this a 128K machine?  Page bank 1 then bank 2 into the
; 0xC000 window writing a different marker to each, page bank 1 back, and
; see which marker survived: on a 48K the writes alias the same fixed RAM
; so the second wins; on a 128K the banks are distinct so the first does.
; Register-only between DI/EI (the stack lives in the paged window), and
; 0xC000-0xCFFF is unused this early so the markers clobber nothing.
detect128:
        di
        ld bc,0x7FFD
        ld a,0x11              ; ROM1, bank 1
        out (c),a
        ld hl,0xC000
        ld (hl),0xAA
        ld bc,0x7FFD
        ld a,0x12              ; ROM1, bank 2
        out (c),a
        ld (hl),0x55
        ld bc,0x7FFD
        ld a,0x11              ; back to bank 1
        out (c),a
        ld e,(hl)              ; 0xAA on 128K, 0x55 on 48K (aliased)
        ld bc,0x7FFD
        ld a,0x10              ; restore ROM1, bank 0
        out (c),a
        ei
        ld a,e
        cp 0xAA
        ld a,0
        jr nz,d128set
        inc a                  ; A = 1: 128K
d128set:
        ld (is128),a
        ret

; ttBanks — the 0x7FFD values that page each spare 16K RAM bank into the
; 0xC000 window for the banked transposition table (ROM1 kept, screen norm).
ttBanks: defb 0x11,0x13,0x14,0x16

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
;  CHESS CLOCKS — per-side countdown driven by the 50 Hz ROM FRAMES
;  counter (enabled by IM1/EI in `start`).  Each turn's full elapsed
;  time (human thinking or AI searching) is charged to the side to move
;  when the move completes; the human's clock also ticks live while the
;  player thinks.  Running out of time is a loss on the clock.
; =====================================================================
clkInit:
        ld hl,INITCLK
        ld (wClock),hl
        ld (bClock),hl
        call clkStartTurn
        ld a,0xFF
        ld (clkLastSec),a      ; force the next live draw
        ret

clkStartTurn:
        ld hl,(FRAMES)
        ld (clkTurnStart),hl
        ld a,(sideToMove)
        ld (clkTurnSide),a
        ret

; clkElapsed -> HL = FRAMES - clkTurnStart (frames used so far this turn)
clkElapsed:
        ld hl,(FRAMES)
        ld de,(clkTurnStart)
        or a
        sbc hl,de
        ret

; clkBudgetExceeded -> CF=1 if this move has used its time budget (the side
; to move's remaining clock >> 5, i.e. ~1/32 of the clock), so iterative
; deepening should stop before starting another, slower iteration.  Lets
; the engine pace itself by the clock instead of always paying full depth.
clkBudgetExceeded:
        call clkElapsed          ; HL = frames used this move
        ld a,(clkTurnSide)
        or a
        ld de,(wClock)
        jr z,cbeShift
        ld de,(bClock)
cbeShift:
        srl d
        rr e
        srl d
        rr e
        srl d
        rr e
        srl d
        rr e
        srl d
        rr e                     ; DE = clock >> 5  (time budget for this move)
        or a
        sbc hl,de                ; CF=1 (borrow) iff elapsed < budget
        ccf                      ; CF=1 iff elapsed >= budget -> exceeded
        ret

; recordMoveLog — append the move just made (mvFrom,mvTo) to the full game
; history at moveLog (2 bytes/ply).  Unlike the 48-ply take-back stack this
; keeps the whole game (cap 255 plies) for export / review.
recordMoveLog:
        ld a,(moveLogN)
        inc a
        ret z                    ; full (wrapped past 255)
        dec a
        ld l,a
        ld h,0
        add hl,hl                ; *2
        ld de,moveLog
        add hl,de
        ld a,(mvFrom)
        ld (hl),a
        inc hl
        ld a,(mvTo)
        ld (hl),a
        ld a,(moveLogN)
        inc a
        ld (moveLogN),a
        ret

; clkCommit — subtract this turn's elapsed time from the mover's clock,
; clamping at zero; a zero clock is a flag-fall loss (unless the position
; is already terminal, in which case that result stands).
clkCommit:
        call clkElapsed
        ex de,hl               ; de = elapsed
        ld a,(clkTurnSide)
        or a
        jr nz,ccB
        ld hl,(wClock)
        or a
        sbc hl,de
        jr nc,ccWok
        ld hl,0
ccWok:  ld (wClock),hl
        jr ccFlag
ccB:    ld hl,(bClock)
        or a
        sbc hl,de
        jr nc,ccBok
        ld hl,0
ccBok:  ld (bClock),hl
ccFlag:
        ld a,h
        or l
        ret nz                 ; time remains
        ld a,(gameState)
        or a
        ret nz                 ; checkmate/draw already decided
        ld a,(clkTurnSide)
        or a
        ld hl,msgWflag         ; white's flag fell -> Black wins
        jr z,ccSet
        ld hl,msgBflag
ccSet:  call setMsg
        ld a,5
        ld (gameState),a
        ret

; clkComputeDisp — fill clkDispW/clkDispB with the values to show; for the
; side to move (while play is live) subtract the in-progress elapsed time.
; Returns A = that side's whole-seconds low byte (throttle key).
clkComputeDisp:
        ld hl,(wClock)
        ld (clkDispW),hl
        ld hl,(bClock)
        ld (clkDispB),hl
        ld a,(gameState)
        or a
        jr nz,ccdStatic        ; game over: freeze at stored values
        call clkElapsed
        ex de,hl               ; de = elapsed
        ld a,(clkTurnSide)
        or a
        jr nz,ccdB
        ld hl,(clkDispW)
        or a
        sbc hl,de
        jr nc,ccdWok
        ld hl,0
ccdWok: ld (clkDispW),hl
        jr ccdSec
ccdB:   ld hl,(clkDispB)
        or a
        sbc hl,de
        jr nc,ccdBok
        ld hl,0
ccdBok: ld (clkDispB),hl
        jr ccdSec
ccdStatic:
ccdSec:
        ld a,(clkTurnSide)
        or a
        ld hl,(clkDispW)
        jr z,ccdDiv
        ld hl,(clkDispB)
ccdDiv: ld c,50
        call divHLbyC          ; HL = seconds
        ld a,l
        ret

; clkLive — redraw the clocks at most once per second while the human is
; thinking (no full-screen redraw, just the two clock cells).
clkLive:
        ld a,(gameState)
        or a
        ret nz
        call clkComputeDisp
        ld hl,clkLastSec
        cp (hl)
        ret z
        ld (hl),a
        jp drawClocksRaw

; clkWaitKey — wait for a key (release then press) like readKeyDebounced,
; ticking the live clock during the press wait.
clkWaitKey:
        call scanKeys
        or a
        jr nz,clkWaitKey       ; wait release
cwkP:   call clkLive
        call scanKeys
        or a
        jr z,cwkP              ; wait press
        ret

; drawClocks — recompute then draw (used by drawInfo / full redraws).
drawClocks:
        call clkComputeDisp
drawClocksRaw:
        ld hl,msgWclk
        ld b,13
        ld c,20
        call printStr
        ld hl,(clkDispW)
        call fmtClk
        ld hl,clkBuf
        ld b,13
        ld c,22
        call printStr
        ld hl,msgBclk
        ld b,14
        ld c,20
        call printStr
        ld hl,(clkDispB)
        call fmtClk
        ld hl,clkBuf
        ld b,14
        ld c,22
        call printStr
        ret

; fmtClk(HL=frames) -> clkBuf = "M:SS",0
fmtClk:
        ld c,50
        call divHLbyC          ; HL = total seconds, A = leftover frames
        ld c,60
        call divHLbyC          ; HL = minutes, A = seconds remainder
        ld b,a                 ; b = seconds (0..59)
        ld a,l
        add a,'0'
        ld (clkBuf),a          ; minutes digit
        ld a,':'
        ld (clkBuf+1),a
        ld a,b
        ld d,'0'
fcT:    cp 10
        jr c,fcU
        sub 10
        inc d
        jr fcT
fcU:    add a,'0'
        ld (clkBuf+3),a        ; seconds units
        ld a,d
        ld (clkBuf+2),a        ; seconds tens
        xor a
        ld (clkBuf+4),a
        ret

; divHLbyC — HL / C -> HL = quotient, A = remainder (C <= 60 here)
divHLbyC:
        xor a
        ld b,16
dhcL:   add hl,hl
        rla
        cp c
        jr c,dhcSkip
        sub c
        inc l
dhcSkip:
        djnz dhcL
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
msgKeys:     defb "QAOP+ENT  C,W=look  NTEVZFSGL",0
msgPerftHdr: defb "PERFT self-test (start position)",0
msgPerftN:   defb "perft",0
msgOK:       defb "OK",0
msgBAD:      defb "BAD",0
msgPerftOK:  defb "PERFT OK - movegen verified",0
msgPerftBad: defb "PERFT BAD - movegen error",0
msgKiwi:     defb "kiwipete d3",0
msgEpT:      defb "enpassant d4",0
msgPromo:    defb "promotion d3",0
msgZob:      defb "incr key/phase/pst",0
msgWmate:    defb "Checkmate! Black wins   SPC=new",0
msgBmate:    defb "Checkmate! White wins   SPC=new",0
msgStale:    defb "Stalemate - draw        SPC=new",0
msgDraw:     defb "Draw (50-move)          SPC=new",0
msgMat:      defb "Draw - insufficient mtl SPC=new",0
msgRep:      defb "Draw - repetition       SPC=new",0
msgTwoP:     defb "Two-player mode toggled",0
msgTaken:    defb "Take back done",0
msgPromote:  defb "Promote: Q=Queen R B N",0
msgSetup:    defb "SET-UP QAOP SPC=cyc W=side ENT",0
msgWclk:     defb "W",0
msgBclk:     defb "B",0
msgWflag:    defb "Flag! Black wins (time) SPC=new",0
msgBflag:    defb "Flag! White wins (time) SPC=new",0
msgSaved:    defb "Game saved to tape ",0
msgLoaded:   defb "Game loaded        ",0
msgLoadErr:  defb "Load error         ",0
msgLevel:    defb "Level",0
msg2pL:      defb "2-player",0
msgMoveL:    defb "Move",0
msgEval:     defb "Eval",0
msgMatl:     defb "Matl",0
nmOpen:      defb "Open game",0
nmClosed:    defb "Closed game",0
nmReti:      defb "Reti",0
nmEnglish:   defb "English",0
nmBird:      defb "Bird",0
nmKK:        defb "King's Knight",0
nmRuy:       defb "Ruy Lopez",0
nmItalian:   defb "Italian Game",0
nmQG:        defb "Queen's Gambit",0
nmQP:        defb "Queen's Pawn",0
nmQGD:       defb "QGD",0
nmScotch:    defb "Scotch",0
nmLondon:    defb "London",0
nmSchClassic: defb "Classic",0
nmSchMeadow:  defb "Meadow ",0
nmSchClean:   defb "Clean  ",0
nmSchHolly:   defb "Holly  ",0
nmSchOrchid:  defb "Orchid ",0
nmSchCoral:   defb "Coral  ",0
nmWOutline:   defb "Outline",0
nmWFilled:    defb "Filled ",0
msgColK:      defb "C:",0
msgWhiteK:    defb "W:",0
msgColour:    defb "Colour scheme (C)  ",0
msgWOut:      defb "White pieces: outline",0
msgWFill:     defb "White pieces: filled ",0
msgCheck:    defb "Check!             ",0

        include "pieces.inc"

; ttStage — 8-byte staging copy of a transposition-table entry, in
; non-pageable RAM (0x8000-0xBFFF) so it stays mapped while a spare bank is
; paged into 0xC000 for the banked TT on 128K machines.
ttStage: defs 8
