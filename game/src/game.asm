; ============================================================================
;  STELLAR DRIFT  -  a cave-flyer for the HC-91 / ZX Spectrum 48K
;  Dodge + shoot + collect.  Travel is left-to-right; hazards scroll in
;  from the right past a ship the player steers up/down/left/right.
;  Controls: Q/A/O/P + Space, or a Kempston joystick.
;  Written in Z80 assembly, assembled with pasmo.
; ============================================================================

        org 32768

; ---- constants -------------------------------------------------------------
SCR     equ 0x4000          ; pixel memory
ATTR    equ 0x5800          ; attribute memory
MAXOBJ  equ 10              ; rocks / enemies / crystals
MAXBUL  equ 4               ; player bullets in flight
NSTAR   equ 16              ; parallax stars
FONT    equ 0x3C00          ; ROM font base (char*8 + FONT)

; ============================================================================
;  ENTRY
; ============================================================================
start:
        di
        ld sp, 0xEFFF
        call build_addrtab
        ld a, r
        ld (seed), a
        xor a
        ld (state), a           ; 0 = title
        call show_title
        ei

main_loop:
        halt                    ; sync to 50 Hz frame interrupt
        ld a, (state)
        or a
        jr nz, ml_not_title
        call title_poll
        jr main_loop
ml_not_title:
        cp 1
        jr nz, ml_gameover
        call play_frame
        ld a, (state)
        cp 2
        jr nz, main_loop
        call show_gameover
        jr main_loop
ml_gameover:
        call gameover_poll
        jr main_loop

; ============================================================================
;  STATE: TITLE / GAME OVER
; ============================================================================
show_title:
        call clear_screen
        xor a
        out (254), a
        ld hl, str_title
        ld b, 8
        ld c, 9
        call print_str_at
        ld hl, str_fire
        ld b, 12
        ld c, 11
        call print_str_at
        ret

title_poll:
        call fire_down
        ret nz
        call init_game
        ld a, 1
        ld (state), a
        ret

show_gameover:
        ld hl, str_over
        ld b, 10
        ld c, 11
        call print_str_at
        ret

gameover_poll:
        call fire_down
        ret nz
        xor a
        ld (state), a
        call show_title
        ret

; fire_down: Z set if Space or Kempston-fire is pressed
fire_down:
        ld bc, 0x7FFE
        in a, (c)
        bit 0, a
        jr z, fd_yes
        call read_joy
        bit 4, a
        jr nz, fd_yes
        or 1
        ret
fd_yes:
        xor a
        ret

; ============================================================================
;  GAME INIT
; ============================================================================
init_game:
        call clear_screen
        call zero_objects
        call init_stars
        ld a, 3
        ld (lives), a
        ld hl, 0
        ld (score), hl
        xor a
        ld (world), a
        call set_world_attr
        ld a, 24
        ld (ship_x), a
        ld a, 88
        ld (ship_y), a
        xor a
        ld (invuln), a
        ld (fire_prev), a
        ld a, (spawn_period)
        ld (spawn_timer), a
        ld hl, 750
        ld (world_timer), hl
        ret

zero_objects:
        ld hl, objs
        ld de, objs+1
        ld bc, MAXOBJ*6-1
        ld (hl), 0
        ldir
        ld hl, bullets
        ld de, bullets+1
        ld bc, MAXBUL*4-1
        ld (hl), 0
        ldir
        ret

; ============================================================================
;  PER-FRAME PLAY
; ============================================================================
play_frame:
        call do_stars
        ; erase ship at old position
        ld a, (ship_x)
        ld (spr_x), a
        ld a, (ship_y)
        ld (spr_y), a
        call erase_sprite
        call read_input
        call do_fire
        call do_bullets
        call do_objects
        call do_spawn
        ; invulnerability countdown + blink
        ld a, (invuln)
        or a
        jr z, pf_drawship
        dec a
        ld (invuln), a
        and 4
        jr nz, pf_skipship
pf_drawship:
        ld hl, spr_ship
        ld (spr_ptr), hl
        ld a, (ship_x)
        ld (spr_x), a
        ld a, (ship_y)
        ld (spr_y), a
        call draw_sprite
pf_skipship:
        ld hl, (world_timer)
        dec hl
        ld (world_timer), hl
        ld a, h
        or l
        jr nz, pf_hud
        call next_world
pf_hud:
        call show_hud
        ret

; ============================================================================
;  INPUT  -  QAOP, plus Kempston joystick directions
; ============================================================================
read_input:
        ld bc, 0xFBFE           ; Q row -> up
        in a, (c)
        bit 0, a
        jr nz, ri_notup
        call ship_up
ri_notup:
        ld bc, 0xFDFE           ; A row -> down
        in a, (c)
        bit 0, a
        jr nz, ri_notdn
        call ship_down
ri_notdn:
        ld bc, 0xDFFE           ; P,O row
        in a, (c)
        bit 1, a                ; O -> left
        jr nz, ri_notlf
        call ship_left
ri_notlf:
        ld bc, 0xDFFE
        in a, (c)
        bit 0, a                ; P -> right
        jr nz, ri_notrt
        call ship_right
ri_notrt:
        call read_joy           ; Kempston (0 if absent/floating)
        ld e, a
        bit 3, e
        jr z, ri_kn_up
        call ship_up
ri_kn_up:
        bit 2, e
        jr z, ri_kn_dn
        call ship_down
ri_kn_dn:
        bit 1, e
        jr z, ri_kn_lf
        call ship_left
ri_kn_lf:
        bit 0, e
        jr z, ri_kn_rt
        call ship_right
ri_kn_rt:
        ret

; read_joy: A = Kempston bits, or 0 if no interface (port floats with
; the top three bits set; a real Kempston returns 000FUDLR).
read_joy:
        ld bc, 0x001F
        in a, (c)
        ld e, a
        and 0xE0
        jr nz, rj_none          ; top bits set -> no interface
        ld a, e
        ret
rj_none:
        xor a
        ret

ship_up:
        ld a, (ship_y)
        cp 18
        ret c
        sub 2
        ld (ship_y), a
        ret
ship_down:
        ld a, (ship_y)
        cp 176
        ret nc
        add a, 2
        ld (ship_y), a
        ret
ship_left:
        ld a, (ship_x)
        cp 2
        ret c
        sub 2
        ld (ship_x), a
        ret
ship_right:
        ld a, (ship_x)
        cp 120
        ret nc
        add a, 2
        ld (ship_x), a
        ret

; ============================================================================
;  SHOOTING
; ============================================================================
do_fire:
        ld bc, 0x7FFE           ; Space
        in a, (c)
        bit 0, a
        jr z, df_pressed
        call read_joy           ; Kempston fire
        bit 4, a
        jr nz, df_pressed
        xor a
        ld (fire_prev), a
        ret
df_pressed:
        ld a, (fire_prev)
        or a
        ret nz                  ; held: no auto-repeat
        ld a, 1
        ld (fire_prev), a
        ; find a free bullet slot
        ld ix, bullets
        ld a, MAXBUL
        ld (bcount), a
fb_find:
        ld a, (ix+0)
        or a
        jr z, fb_free
        ld de, 4
        add ix, de
        ld a, (bcount)
        dec a
        ld (bcount), a
        jr nz, fb_find
        ret
fb_free:
        ld a, 1
        ld (ix+0), a
        ld a, (ship_x)
        add a, 14
        ld (ix+1), a
        ld (ix+3), a            ; ox
        ld a, (ship_y)
        add a, 7
        ld (ix+2), a
        ret

do_bullets:
        ld ix, bullets
        ld a, MAXBUL
        ld (bcount), a
db_loop:
        ld a, (ix+0)
        or a
        jp z, db_next
        ; erase old box
        ld a, (ix+3)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call erase_sprite
        ; move right
        ld a, (ix+1)
        add a, 6
        cp 240
        jr c, db_onscr
        xor a
        ld (ix+0), a            ; off the right edge -> free
        jp db_next
db_onscr:
        ld (ix+1), a
        ; collide against objects
        ld iy, objs
        ld a, MAXOBJ
        ld (ocount), a
db_objloop:
        ld a, (iy+0)
        or a
        jr z, db_objnext
        cp 3
        jr z, db_objnext        ; bullets pass through crystals
        ld a, (ix+1)
        ld b, a
        ld a, (ix+2)
        ld c, a
        ld a, (iy+1)
        ld d, a
        ld a, (iy+2)
        ld e, a
        call collide
        jr nz, db_objnext
        ; hit: erase + free object, free bullet, score
        ld a, (iy+3)
        ld (spr_x), a
        ld a, (iy+4)
        ld (spr_y), a
        call erase_sprite
        xor a
        ld (iy+0), a
        ld bc, 5
        call add_score
        xor a
        ld (ix+0), a
        jp db_next
db_objnext:
        ld de, 6
        add iy, de
        ld a, (ocount)
        dec a
        ld (ocount), a
        jr nz, db_objloop
        ; survived: draw bullet
        ld hl, spr_bullet
        ld (spr_ptr), hl
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call draw_sprite
        ld a, (ix+1)
        ld (ix+3), a
db_next:
        ld de, 4
        add ix, de
        ld a, (bcount)
        dec a
        ld (bcount), a
        jp nz, db_loop
        ret

; ============================================================================
;  OBJECTS  -  type(+0) x(+1) y(+2) ox(+3) oy(+4) spd(+5)
; ============================================================================
do_objects:
        ld ix, objs
        ld a, MAXOBJ
        ld (ocount), a
do_obj_loop:
        ld a, (ix+0)
        or a
        jp z, do_obj_next
        ; erase at old
        ld a, (ix+3)
        ld (spr_x), a
        ld a, (ix+4)
        ld (spr_y), a
        call erase_sprite
        ; move left
        ld a, (ix+1)
        sub (ix+5)
        jr nc, obj_alive
        xor a
        ld (ix+0), a            ; off the left edge -> free
        jp do_obj_next
obj_alive:
        ld (ix+1), a
        ; ship collision (unless invulnerable)
        ld a, (invuln)
        or a
        jr nz, obj_draw
        ld a, (ship_x)
        ld b, a
        ld a, (ship_y)
        ld c, a
        ld a, (ix+1)
        ld d, a
        ld a, (ix+2)
        ld e, a
        call collide
        jr nz, obj_draw
        ld a, (ix+0)
        cp 3
        jr z, obj_collect
        call ship_hit
        xor a
        ld (ix+0), a
        jp do_obj_next
obj_collect:
        ld bc, 10
        call add_score
        xor a
        ld (ix+0), a
        jp do_obj_next
obj_draw:
        ld a, (ix+0)
        cp 3
        jr z, obj_spr_c
        cp 2
        jr z, obj_spr_e
        ld hl, spr_rock
        jr obj_spr_set
obj_spr_e:
        ld hl, spr_enemy
        jr obj_spr_set
obj_spr_c:
        ld hl, spr_crystal
obj_spr_set:
        ld (spr_ptr), hl
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call draw_sprite
        ld a, (ix+1)
        ld (ix+3), a
        ld a, (ix+2)
        ld (ix+4), a
do_obj_next:
        ld de, 6
        add ix, de
        ld a, (ocount)
        dec a
        ld (ocount), a
        jp nz, do_obj_loop
        ret

ship_hit:
        ; erase ship
        ld a, (ship_x)
        ld (spr_x), a
        ld a, (ship_y)
        ld (spr_y), a
        call erase_sprite
        ld a, (lives)
        dec a
        ld (lives), a
        jr z, sh_dead
        ld a, 75
        ld (invuln), a
        ld a, 24
        ld (ship_x), a
        ld a, 88
        ld (ship_y), a
        ret
sh_dead:
        ld a, 2
        ld (state), a
        ret

; collide: B=ax C=ay D=bx E=by ; Z set if their 16x16 boxes overlap
collide:
        ld a, b
        sub d
        jr nc, c_dx
        neg
c_dx:
        cp 13
        jr nc, c_none
        ld a, c
        sub e
        jr nc, c_dy
        neg
c_dy:
        cp 13
        jr nc, c_none
        xor a
        ret
c_none:
        or 1
        ret

; ============================================================================
;  SPAWNING
; ============================================================================
do_spawn:
        ld a, (spawn_timer)
        dec a
        ld (spawn_timer), a
        ret nz
        ld a, (spawn_period)
        ld (spawn_timer), a
        ld ix, objs
        ld b, MAXOBJ
sp_find:
        ld a, (ix+0)
        or a
        jr z, sp_free
        ld de, 6
        add ix, de
        djnz sp_find
        ret
sp_free:
        call rnd
        and 3
        cp 0
        jr nz, sp_t1
        ld a, 3                 ; crystal
        ld c, 1
        jr sp_settype
sp_t1:
        cp 1
        jr nz, sp_rock
        ld a, 2                 ; enemy
        ld c, 2
        jr sp_settype
sp_rock:
        ld a, 1                 ; rock
        ld c, 1
sp_settype:
        ld (ix+0), a
        ld (ix+5), c
        ld a, 232
        ld (ix+1), a
        ld (ix+3), a
        call rnd
        and 0x7F
        add a, 24
        ld (ix+2), a
        ld (ix+4), a
        ret

; ============================================================================
;  STARS  -  x(+0) y(+1) ox(+2) spd(+3)
; ============================================================================
init_stars:
        ld ix, stars
        ld a, NSTAR
        ld (scount), a
is_loop:
        call rnd
        ld (ix+0), a
        ld (ix+2), a
        call rnd
        and 0x7F
        add a, 24
        ld (ix+1), a
        call rnd
        and 1
        inc a
        ld (ix+3), a
        ld de, 4
        add ix, de
        ld a, (scount)
        dec a
        ld (scount), a
        jr nz, is_loop
        ret

do_stars:
        ld ix, stars
        ld a, NSTAR
        ld (scount), a
ds_star_loop:
        ld a, (ix+1)            ; y
        ld b, a
        ld a, (ix+2)            ; ox
        ld c, a
        call clr_pixel
        ld a, (ix+0)
        sub (ix+3)
        jr nc, ds_star_okx
        ld a, 255
ds_star_okx:
        ld (ix+0), a
        ld a, (ix+1)
        ld b, a
        ld a, (ix+0)
        ld c, a
        call set_pixel
        ld a, (ix+0)
        ld (ix+2), a
        ld de, 4
        add ix, de
        ld a, (scount)
        dec a
        ld (scount), a
        jr nz, ds_star_loop
        ret

; ============================================================================
;  WORLDS / ZONES
; ============================================================================
set_world_attr:
        ld a, (world)
        ld b, a
        add a, a
        add a, b                ; world*3
        ld e, a
        ld d, 0
        ld hl, worlds_tab
        add hl, de
        ld a, (hl)              ; border
        out (254), a
        inc hl
        ld a, (hl)              ; play-area attribute
        push hl
        ld hl, ATTR+64
        ld (hl), a
        ld de, ATTR+65
        ld bc, 703
        ldir
        ld hl, ATTR             ; HUD rows kept readable
        ld (hl), 0x47
        ld de, ATTR+1
        ld bc, 63
        ldir
        pop hl
        inc hl
        ld a, (hl)              ; spawn period
        ld (spawn_period), a
        ret

next_world:
        ld a, (world)
        inc a
        and 3
        ld (world), a
        call set_world_attr
        ld hl, 750
        ld (world_timer), hl
        ret

; ============================================================================
;  HUD / TEXT
; ============================================================================
show_hud:
        ld hl, str_score
        ld b, 0
        ld c, 0
        call print_str_at
        call show_score
        ld hl, str_ships
        ld b, 0
        ld c, 20
        call print_str_at
        ld a, (lives)
        add a, '0'
        ld b, 0
        ld c, 26
        call print_char
        ld hl, str_zone
        ld b, 1
        ld c, 0
        call print_str_at
        ld a, (world)
        add a, '1'
        ld b, 1
        ld c, 5
        call print_char
        ret

show_score:
        ld hl, (score)
        ld ix, decbuf
        ld de, 10000
        call sc_digit
        ld de, 1000
        call sc_digit
        ld de, 100
        call sc_digit
        ld de, 10
        call sc_digit
        ld a, l
        add a, '0'
        ld (ix+0), a
        ld hl, decbuf
        ld b, 0
        ld c, 6
ss_print:
        ld a, (hl)
        push hl
        push bc
        call print_char
        pop bc
        pop hl
        inc hl
        inc c
        ld a, c
        cp 11
        jr nz, ss_print
        ret

; sc_digit: HL=value DE=divisor ; appends digit to (IX++), HL=remainder
sc_digit:
        ld b, '0'
sc_d_loop:
        or a
        sbc hl, de
        jr c, sc_d_done
        inc b
        jr sc_d_loop
sc_d_done:
        add hl, de
        ld a, b
        ld (ix+0), a
        inc ix
        ret

add_score:
        ld hl, (score)
        add hl, bc
        ld (score), hl
        ret

; print_str_at: HL=string(0-term) B=row C=col
print_str_at:
        ld a, (hl)
        or a
        ret z
        push hl
        push bc
        call print_char
        pop bc
        pop hl
        inc hl
        inc c
        jr print_str_at

; print_char: A=char B=row C=col  (8x8 cell from ROM font)
print_char:
        ld (pc_char), a
        ld a, c
        ld (pc_col), a
        ld a, b
        add a, a
        add a, a
        add a, a
        ld (pc_prow), a
        ld a, (pc_char)
        ld l, a
        ld h, 0
        add hl, hl
        add hl, hl
        add hl, hl
        ld de, FONT
        add hl, de
        ld (pc_font), hl
        ld b, 8
pc_loop:
        push bc
        ld a, (pc_prow)
        ld l, a
        ld h, 0
        add hl, hl
        ld de, addrtab
        add hl, de
        ld e, (hl)
        inc hl
        ld d, (hl)
        ld a, (pc_col)
        ld l, a
        ld h, 0
        add hl, de
        ld de, (pc_font)
        ld a, (de)
        ld (hl), a
        inc de
        ld (pc_font), de
        ld a, (pc_prow)
        inc a
        ld (pc_prow), a
        pop bc
        djnz pc_loop
        ret

; ============================================================================
;  PSEUDO-RANDOM (8-bit LCG, full period)
; ============================================================================
rnd:
        ld a, (seed)
        ld b, a
        rlca
        rlca
        add a, b
        add a, 0x3B
        ld (seed), a
        ret

; ============================================================================
;  SCREEN SETUP
; ============================================================================
clear_screen:
        ld hl, SCR
        ld (hl), 0
        ld de, SCR+1
        ld bc, 6143
        ldir
        ld hl, ATTR
        ld (hl), 0x47
        ld de, ATTR+1
        ld bc, 767
        ldir
        ret

build_addrtab:
        ld hl, addrtab
        ld c, 0
bat_loop:
        ld a, c
        and 7
        ld d, a
        ld a, c
        and 0xC0
        rrca
        rrca
        rrca
        or d
        or 0x40
        ld d, a
        ld a, c
        and 0x38
        rlca
        rlca
        ld e, a
        ld (hl), e
        inc hl
        ld (hl), d
        inc hl
        inc c
        ld a, c
        cp 192
        jr nz, bat_loop
        ret

; ============================================================================
;  SINGLE-PIXEL PLOT  (B=y C=x)
; ============================================================================
pixel_addr:                     ; -> HL=byte addr, A=bit mask
        ld a, b
        ld l, a
        ld h, 0
        add hl, hl
        ld de, addrtab
        add hl, de
        ld e, (hl)
        inc hl
        ld d, (hl)
        ld a, c
        rrca
        rrca
        rrca
        and 0x1F
        ld l, a
        ld h, 0
        add hl, de
        ld a, c
        and 7
        ld b, a
        ld a, 0x80
        inc b
pa_rl:
        dec b
        jr z, pa_done
        rrca
        jr pa_rl
pa_done:
        ret

set_pixel:
        call pixel_addr
        or (hl)
        ld (hl), a
        ret

clr_pixel:
        call pixel_addr
        cpl
        and (hl)
        ld (hl), a
        ret

; ============================================================================
;  MASKED 16x16 SPRITE BLITTER
;   spr_ptr -> 16 rows of (data_hi,data_lo,mask_hi,mask_lo)
;   spr_x, spr_y ; screen = (screen AND mask) OR data, shifted by spr_x&7
; ============================================================================
draw_sprite:
        ld a, (spr_x)
        and 7
        ld (shift_n), a
        ld a, (spr_x)
        rrca
        rrca
        rrca
        and 0x1F
        ld (xc_tmp), a
        ld a, (spr_y)
        ld (row_y), a
        ld b, 16
ds_row:
        push bc
        ld a, (row_y)
        ld l, a
        ld h, 0
        add hl, hl
        ld de, addrtab
        add hl, de
        ld e, (hl)
        inc hl
        ld d, (hl)
        ld a, (xc_tmp)
        ld l, a
        ld h, 0
        add hl, de
        ld (scr_addr), hl
        ld hl, (spr_ptr)
        ld a, (hl)
        ld (shbuf+0), a
        inc hl
        ld a, (hl)
        ld (shbuf+1), a
        inc hl
        ld a, (hl)
        ld (shbuf+3), a
        inc hl
        ld a, (hl)
        ld (shbuf+4), a
        inc hl
        ld (spr_ptr), hl
        xor a
        ld (shbuf+2), a
        ld a, 255
        ld (shbuf+5), a
        ld a, (shift_n)
        or a
        jr z, ds_noshift
        ld c, a
ds_shloop:
        ld hl, shbuf
        srl (hl)
        inc hl
        rr (hl)
        inc hl
        rr (hl)
        ld hl, shbuf+3
        scf
        rr (hl)
        inc hl
        rr (hl)
        inc hl
        rr (hl)
        dec c
        jr nz, ds_shloop
ds_noshift:
        ld hl, (scr_addr)
        ld a, (shbuf+3)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (shbuf+0)
        or c
        ld (hl), a
        inc hl
        ld a, (shbuf+4)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (shbuf+1)
        or c
        ld (hl), a
        inc hl
        ld a, (shbuf+5)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (shbuf+2)
        or c
        ld (hl), a
        ld a, (row_y)
        inc a
        ld (row_y), a
        pop bc
        dec b
        jp nz, ds_row
        ret

erase_sprite:
        ld a, (spr_x)
        rrca
        rrca
        rrca
        and 0x1F
        ld (xc_tmp), a
        ld a, (spr_y)
        ld (row_y), a
        ld b, 16
es_row:
        push bc
        ld a, (row_y)
        ld l, a
        ld h, 0
        add hl, hl
        ld de, addrtab
        add hl, de
        ld e, (hl)
        inc hl
        ld d, (hl)
        ld a, (xc_tmp)
        ld l, a
        ld h, 0
        add hl, de
        xor a
        ld (hl), a
        inc hl
        ld (hl), a
        inc hl
        ld (hl), a
        ld a, (row_y)
        inc a
        ld (row_y), a
        pop bc
        dec b
        jr nz, es_row
        ret

; ============================================================================
;  DATA
; ============================================================================
        include "src/sprites.inc"

; world*3: border colour, play-area attribute, spawn period (frames)
worlds_tab:
        db 0, 0x47, 48          ; Zone 1: black  (gentle start)
        db 1, 0x4F, 38          ; Zone 2: blue
        db 2, 0x57, 30          ; Zone 3: red
        db 3, 0x5F, 24          ; Zone 4: magenta

str_title:  db "STELLAR DRIFT",0
str_fire:   db "PRESS FIRE",0
str_over:   db "GAME OVER",0
str_score:  db "SCORE",0
str_ships:  db "SHIPS",0
str_zone:   db "ZONE",0

state:        defb 0
lives:        defb 0
world:        defb 0
invuln:       defb 0
fire_prev:    defb 0
spawn_timer:  defb 0
spawn_period: defb 40
score:        defw 0
world_timer:  defw 0
seed:         defb 1
scount:       defb 0
bcount:       defb 0
ocount:       defb 0
pc_char:      defb 0
pc_col:       defb 0
pc_prow:      defb 0
pc_font:      defw 0
decbuf:       defs 5

ship_x:   defb 0
ship_y:   defb 0
spr_ptr:  defw 0
spr_x:    defb 0
spr_y:    defb 0
shift_n:  defb 0
xc_tmp:   defb 0
row_y:    defb 0
scr_addr: defw 0
shbuf:    defb 0,0,0,0,0,0

objs:     defs MAXOBJ*6
bullets:  defs MAXBUL*4
stars:    defs NSTAR*4

addrtab:  defs 384

        end start
