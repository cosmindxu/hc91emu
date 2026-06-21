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

; per-ply move buffers: base + ply*512 (128 moves * 4 bytes)
moveBufBase equ 0xB000   ; 0xB000..0xD000 = 16 plies
undoBase    equ 0xD000   ; base + ply*16

MV_REC  equ 4
INF     equ 30000
MATE    equ 29000
MAXPLY  equ 15

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
        call newGame
        call drawScreenFull
mainLoop:
        ld a,(gameState)
        or a
        jp nz,gameOverLoop
        ld a,(sideToMove)
        ld hl,humanSide
        cp (hl)
        jr nz,aiTurn
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

; =====================================================================
;  KEYBOARD
;  Returns a code in A: 'Q''A''O''P' cursor, 13 enter, 32 space,
;  '1'..'5' difficulty, 'N' new, 'F' flip, 0 = none.
; =====================================================================
scanKeys:
        ld bc,0xFBFE           ; Q,W,E,R,T
        in a,(c)
        bit 0,a
        jr nz,sk_a
        ld a,'Q'
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
        jr z,hmUp
        cp 'A'
        jr z,hmDown
        cp 'O'
        jr z,hmLeft
        cp 'P'
        jr z,hmRight
        cp 13
        jr z,hmSel
        cp ' '
        jr z,hmSel
        cp 'F'
        jr z,hmFlip
        cp 'N'
        jp z,hmNew
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
        ld hl,humanSide
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
        ld hl,humanSide
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
msgKeys:     defb "QAOP move ENT pick N new F flip",0
msgWmate:    defb "Checkmate! Black wins   SPC=new",0
msgBmate:    defb "Checkmate! White wins   SPC=new",0
msgStale:    defb "Stalemate - draw        SPC=new",0
msgDraw:     defb "Draw (50-move)          SPC=new",0
msgCheck:    defb "Check!             ",0

        include "pieces.inc"
