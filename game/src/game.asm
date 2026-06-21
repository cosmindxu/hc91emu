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
MAXOBJ  equ 8               ; rocks / enemies / crystals / power-ups / boss
OBJSZ   equ 8               ; bytes per object
MAXBUL  equ 6               ; player bullets in flight
MAXEB   equ 4               ; enemy bullets in flight
MAXEXPL equ 3               ; simultaneous explosions
DEB     equ 6               ; debris sparks
NSTAR   equ 18              ; parallax stars (3 depth layers)
FONT    equ 0x3C00          ; ROM font base (char*8 + FONT)

; object types
T_ROCK  equ 1
T_ENEMY equ 2
T_CRYS  equ 3
T_POWER equ 4
T_BOSS  equ 5
T_DIVER equ 6               ; swoops toward the ship
T_MINE  equ 7               ; slow drifting spiked hazard
T_DRONE equ 8               ; homes on the ship and fires
T_TURRET equ 9              ; clings to ceiling/floor, fires aimed
T_MIDBOSS equ 10            ; multi-hit mini-boss (hp in +7)
NZONES  equ 6               ; number of named zones in the cycle

; pre-shifted 16x16 sprite indices (into sprtab / psbuf)
NSPR    equ 19
SI_ROCK equ 0
SI_ROCK2 equ 1
SI_ENEMY equ 2
SI_CRYS equ 3
SI_CRYS2 equ 4
SI_POWER equ 5
SI_BULLET equ 6
SI_EBUL equ 7
SI_EX1  equ 8
SI_EX2  equ 9
SI_EX3  equ 10
SI_DIVER equ 11
SI_TURRET equ 12
SI_MINE equ 13
SI_DRONE equ 14
SI_PLANET equ 15
SI_SPARK equ 16
SI_FLAME1 equ 17
SI_FLAME2 equ 18

; ============================================================================
;  ENTRY
; ============================================================================
start:
        di
        ld sp, 0xEFFF
        call build_addrtab
        call build_preshift
        call ay_init
        call setup_im2          ; rock-steady 50 Hz game clock
        ld a, r
        ld (seed), a
        call init_hiscores
        xor a
        ld (state), a           ; 0 = title
        call show_title
        ei
        jp main_loop            ; (do not fall through into setup_im2)

; setup_im2: vector table of 0xFD at 0xFE00 -> ISR jp at 0xFDFD
setup_im2:
        ld hl, 0xFE00
        ld (hl), 0xFD
        ld de, 0xFE01
        ld bc, 256
        ldir
        ld a, 0xC3              ; JP opcode
        ld (0xFDFD), a
        ld hl, im2_isr
        ld (0xFDFE), hl
        ld a, 0xFE
        ld i, a
        im 2
        ret

im2_isr:
        push af
        push hl
        ld hl, tick
        inc (hl)
        pop hl
        pop af
        ei
        ret

main_loop:
        halt                    ; sync to 50 Hz frame interrupt
        ld a, (state)
        or a
        jr nz, ml_not_title
        call title_anim
        call title_poll
        jr main_loop
ml_not_title:
        cp 1
        jr nz, ml_chk2
        call check_pause
        ld a, (paused)
        or a
        jr nz, main_loop        ; frozen while paused
        call play_frame
        ld a, (state)
        cp 2
        jr nz, main_loop
        call show_gameover
        jr main_loop
ml_chk2:
        cp 2
        jr nz, ml_chk3
        call gameover_poll
        jr main_loop
ml_chk3:
        call initials_frame     ; state 3: entering initials
        jr main_loop

; check_pause: 'H' toggles pause (edge-detected); show PAUSED in the HUD
check_pause:
        ld bc, 0xBFFE           ; H,J,K,L,Enter row
        in a, (c)
        bit 4, a                ; H
        jr nz, cp_up
        ld a, (pause_prev)
        or a
        jr nz, cp_done          ; still held
        ld a, 1
        ld (pause_prev), a
        ld a, (paused)
        xor 1
        ld (paused), a
        or a
        jr z, cp_done
        ld hl, str_pause        ; just paused -> show it
        ld b, 1
        ld c, 22
        call print_str_at
        ret
cp_up:
        xor a
        ld (pause_prev), a
cp_done:
        ret

; ============================================================================
;  STATE: TITLE / GAME OVER
; ============================================================================
show_title:
        call clear_screen
        xor a
        ld (cur_border), a
        out (254), a
        ld hl, str_title
        ld b, 2
        ld c, 9
        call print_str_at
        ld hl, str_hiscores
        ld b, 6
        ld c, 9
        call print_str_at
        call draw_hstable
        call draw_scheme
        call draw_shipsel
        call draw_opts
        ld hl, str_fire
        ld b, 20
        ld c, 11
        call print_str_at
        ld hl, str_ctrl
        ld b, 22
        ld c, 4
        call print_str_at
        ret

; draw_opts: difficulty + option toggles on the title (rows 13,18)
draw_opts:
        ld hl, str_diff
        ld b, 13
        ld c, 4
        call print_str_at
        ld a, (difficulty)
        add a, a
        ld e, a
        ld d, 0
        ld hl, diff_tab
        add hl, de
        ld a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        ld b, 13
        ld c, 12
        call print_str_at
        ld hl, str_opts1
        ld b, 18
        ld c, 5
        call print_str_at
        ret

; set_ship_ptr: spr_ptr = ship_tab[ship_choice*3 + ship_bank]
; ship_bank: 0 level, 1 climb, 2 dive (from vertical intent)
set_ship_ptr:
        ld a, (ship_choice)
        ld b, a
        add a, a
        add a, b                ; choice*3
        ld b, a
        ld a, (ship_bank)
        add a, b                ; + bank
        add a, a                ; *2 (word table)
        ld e, a
        ld d, 0
        ld hl, ship_tab
        add hl, de
        ld a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        ld (spr_ptr), hl
        ret

; draw_shipsel: show the chosen ship preview + label on the title
draw_shipsel:
        ld a, 116
        ld (spr_x), a
        ld a, 104
        ld (spr_y), a
        call erase_ship
        call set_ship_ptr
        ld a, 116
        ld (spr_x), a
        ld a, 104
        ld (spr_y), a
        call draw_ship
        ld hl, str_shipsel
        ld b, 15
        ld c, 7
        call print_str_at
        ld a, (ship_choice)
        add a, 'A'
        ld b, 15
        ld c, 14
        call print_char
        ret

; title_anim: blink PRESS FIRE and scroll the credits line (attract mode)
title_anim:
        ld a, (tctr)
        inc a
        ld (tctr), a
        and 16
        jr z, ta_blinkoff
        ld hl, str_fire
        ld b, 20
        ld c, 11
        call print_str_at
        jr ta_credits
ta_blinkoff:
        ld b, 20                ; blank "PRESS FIRE"
        ld c, 11
ta_bl:
        ld a, 32
        push bc
        call print_char
        pop bc
        inc c
        ld a, c
        cp 21
        jr nz, ta_bl
ta_credits:
        ld a, (tctr)
        and 1
        ret nz                  ; scroll every other frame
        ld a, (credit_idx)
        ld e, a
        ld d, 0
        ld hl, credits_msg
        add hl, de
        ld a, (hl)
        or a
        jr nz, tc_ok
        xor a
        ld (credit_idx), a
        ld a, (credits_msg)
tc_ok:
        ld l, a                 ; font bytes = char*8 + FONT
        ld h, 0
        add hl, hl
        add hl, hl
        add hl, hl
        ld de, FONT
        add hl, de
        ld (ss_tile), hl
        ld a, 184               ; credit strip at pixel row 184 (char row 23)
        ld (ss_base), a
        call scroll_strip
        ld a, (credit_idx)
        inc a
        ld (credit_idx), a
        ret

draw_scheme:
        ld hl, str_schopts
        ld b, 16
        ld c, 8
        call print_str_at
        ld hl, str_qaop
        ld a, (ctrl_scheme)
        or a
        jr z, dsc_show
        ld hl, str_cursor
dsc_show:
        ld b, 17
        ld c, 10
        call print_str_at
        ret

; draw the 5-entry high-score table starting at row 8
draw_hstable:
        ld ix, hs_names
        ld iy, hs_scores
        ld a, 8
        ld (hst_row), a
        ld b, 5
dhs_loop:
        push bc
        ; initials at col 10
        ld a, (hst_row)
        ld b, a
        ld c, 10
        ld a, (ix+0)
        push bc
        call print_char
        pop bc
        inc c
        ld a, (ix+1)
        push bc
        call print_char
        pop bc
        inc c
        ld a, (ix+2)
        call print_char
        ; score at col 15 (5 digits)
        ld l, (iy+0)
        ld h, (iy+1)
        push ix
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
        pop ix
        ld hl, decbuf
        ld a, (hst_row)
        ld b, a
        ld c, 15
        call print_str_n5
        ; next row
        ld a, (hst_row)
        inc a
        ld (hst_row), a
        ld de, 3
        add ix, de
        ld de, 2
        add iy, de
        pop bc
        djnz dhs_loop
        ret

; print exactly 5 chars from HL at B=row C=col
print_str_n5:
        push bc
        ld d, 5
ps5_loop:
        ld a, (hl)
        push hl
        push de
        push bc
        call print_char
        pop bc
        pop de
        pop hl
        inc hl
        inc c
        dec d
        jr nz, ps5_loop
        pop bc
        ret

title_poll:
        ld bc, 0x7FFE           ; M -> cycle ship design
        in a, (c)
        bit 2, a
        jr nz, tp_mup
        ld a, (m_prev)
        or a
        jr nz, tp_scheme        ; held: ignore until released
        ld a, 1
        ld (m_prev), a
        ld a, (ship_choice)
        inc a
        cp 6
        jr c, tp_mok
        xor a
tp_mok:
        ld (ship_choice), a
        call draw_shipsel
        ret
tp_mup:
        xor a
        ld (m_prev), a
tp_scheme:
        ld bc, 0xF7FE           ; control-scheme select: 1 / 2
        in a, (c)
        bit 0, a
        jr nz, tp_n1
        xor a
        ld (ctrl_scheme), a
        call draw_scheme
        ret
tp_n1:
        bit 1, a
        jr nz, tp_opts
        ld a, 1
        ld (ctrl_scheme), a
        call draw_scheme
        ret
; ---- option keys 3/4/5/6 (edge-latched so one press = one change) ----
tp_opts:
        ld bc, 0xF7FE           ; 1-5 half-row
        in a, (c)
        ld e, a                 ; E = 1-5 row
        ld bc, 0xEFFE           ; 6-0 half-row
        in a, (c)
        and 0x10                ; key 6 = bit4
        ld d, a                 ; D = 6 bit
        ld a, e
        and 0x1C                ; bits 2,3,4 = keys 3,4,5 (active low)
        cp 0x1C
        jr nz, tp_optpress      ; one of 3/4/5 pressed
        ld a, d
        or a
        jr nz, tp_optnone       ; key 6 also up -> nothing pressed
tp_optpress:
        ld a, (opt_prev)
        or a
        jr nz, tp_n2            ; latched: wait for release
        ld a, 1
        ld (opt_prev), a
        bit 2, e
        jr nz, tp_o4
        ld a, (difficulty)      ; key 3: cycle skill
        inc a
        cp 3
        jr c, tp_dset
        xor a
tp_dset:
        ld (difficulty), a
        jr tp_optredraw
tp_o4:
        bit 3, e
        jr nz, tp_o5
        ld a, (opt_music)       ; key 4: music
        xor 1
        ld (opt_music), a
        jr tp_optredraw
tp_o5:
        bit 4, e
        jr nz, tp_o6
        ld a, (opt_shake)       ; key 5: flash
        xor 1
        ld (opt_shake), a
        jr tp_optredraw
tp_o6:
        ld a, (opt_practice)    ; key 6: safe/practice
        xor 1
        ld (opt_practice), a
tp_optredraw:
        call draw_opts
        ret
tp_optnone:
        xor a
        ld (opt_prev), a
tp_n2:
        call fire_down
        jr z, tp_press
        xor a
        ld (menu_lock), a       ; fire released -> arm
        ret
tp_press:
        ld a, (menu_lock)
        or a
        ret nz                  ; wait for a fresh press
        call init_game
        ld a, 1
        ld (state), a
        ld (menu_lock), a
        ret

show_gameover:
        ld hl, str_over
        ld b, 10
        ld c, 11
        call print_str_at
        ret

gameover_poll:
        call fire_down
        jr z, gp_press
        xor a
        ld (menu_lock), a
        ret
gp_press:
        ld a, (menu_lock)
        or a
        ret nz
        ld a, 1
        ld (menu_lock), a
        call hs_qualify         ; carry set => made the table
        jr nc, gp_totitle
        call ie_setup
        ld a, 3
        ld (state), a
        ret
gp_totitle:
        xor a
        ld (state), a
        call show_title
        ret

; hs_qualify: carry set if (score) beats the lowest table entry
hs_qualify:
        ld hl, (score)
        ld de, (hs_scores+8)    ; entry 4 (lowest)
        or a
        sbc hl, de
        jr c, hq_no
        jr z, hq_no
        scf
        ret
hq_no:
        or a
        ret

; ============================================================================
;  HIGH-SCORE TABLE + INITIALS ENTRY  (state 3)
; ============================================================================
init_hiscores:
        ld hl, hs_def_names
        ld de, hs_names
        ld bc, 15
        ldir
        ld hl, hs_def_scores
        ld de, hs_scores
        ld bc, 10
        ldir
        ret

ie_setup:
        call clear_screen
        xor a
        ld (cur_border), a
        out (254), a
        ld hl, str_newhi
        ld b, 8
        ld c, 8
        call print_str_at
        ld hl, str_entini
        ld b, 10
        ld c, 8
        call print_str_at
        xor a
        ld (ie_pos), a
        ld (ie_letter), a
        ld (ie_prevud), a
        ld a, 1                 ; require the trigger fire to be released first
        ld (ie_prevfire), a
        ld a, 'A'
        ld (ie_buf+0), a
        ld (ie_buf+1), a
        ld (ie_buf+2), a
        ret

initials_frame:
        ; ----- up/down to change the current letter -----
        ld d, 0
        ld bc, 0xFBFE
        in a, (c)
        bit 0, a
        jr nz, if_ckdn
        ld d, 1
if_ckdn:
        ld bc, 0xFDFE
        in a, (c)
        bit 0, a
        jr nz, if_ckjoy
        ld d, 2
if_ckjoy:
        call read_joy
        bit 3, a
        jr z, if_jdn
        ld d, 1
if_jdn:
        bit 2, a
        jr z, if_ud
        ld d, 2
if_ud:
        ld a, d
        or a
        jr nz, if_haveud
        xor a
        ld (ie_prevud), a
        jr if_fire
if_haveud:
        ld a, (ie_prevud)
        or a
        jr nz, if_fire
        ld a, 1
        ld (ie_prevud), a
        ld a, d
        cp 1
        jr nz, if_isdn
        ld a, (ie_letter)
        inc a
        cp 27
        jr c, if_setl
        xor a
        jr if_setl
if_isdn:
        ld a, (ie_letter)
        or a
        jr nz, if_dec
        ld a, 27
if_dec:
        dec a
if_setl:
        ld (ie_letter), a
        cp 26
        jr nc, if_space
        add a, 'A'
        jr if_putc
if_space:
        ld a, 32
if_putc:
        ld c, a
        ld hl, ie_buf
        ld a, (ie_pos)
        ld e, a
        ld d, 0
        add hl, de
        ld (hl), c
if_fire:
        ; ----- fire to confirm the current letter -----
        call fire_down
        jr z, if_firep
        xor a
        ld (ie_prevfire), a
        jr if_draw
if_firep:
        ld a, (ie_prevfire)
        or a
        jr nz, if_draw
        ld a, 1
        ld (ie_prevfire), a
        ld a, (ie_pos)
        inc a
        ld (ie_pos), a
        xor a
        ld (ie_letter), a
        ld a, (ie_pos)
        cp 3
        jr c, if_setdef
        call hs_insert
        xor a
        ld (state), a
        call show_title
        ret
if_setdef:
        ld hl, ie_buf
        ld a, (ie_pos)
        ld e, a
        ld d, 0
        add hl, de
        ld (hl), 'A'
if_draw:
        ld hl, ie_buf           ; show the three initials
        ld b, 14
        ld c, 14
        ld a, (hl)
        push bc
        call print_char
        pop bc
        inc c
        ld a, (ie_buf+1)
        push bc
        call print_char
        pop bc
        inc c
        ld a, (ie_buf+2)
        call print_char
        ; clear the cursor row, then mark the active position
        ld b, 15
        ld c, 14
        ld a, 32
        push bc
        call print_char
        pop bc
        inc c
        ld a, 32
        push bc
        call print_char
        pop bc
        inc c
        ld a, 32
        call print_char
        ld a, (ie_pos)
        cp 3
        ret nc
        add a, 14
        ld c, a
        ld b, 15
        ld a, '^'
        call print_char
        ret

; hs_insert: place ie_buf + (score) into the sorted table, dropping lowest
hs_insert:
        ld hl, (score)
        ld ix, hs_scores
        ld b, 0                 ; index
hi_find:
        ld a, b
        cp 5
        ret z                   ; no slot (shouldn't happen)
        ld e, (ix+0)
        ld d, (ix+1)
        push hl
        or a
        sbc hl, de
        pop hl
        jr c, hi_next
        jr z, hi_next
        jr hi_at
hi_next:
        inc b
        ld de, 2
        add ix, de
        jr hi_find
hi_at:
        ; B = insert index; shift entries [B..3] down by one
        ld a, 4
        sub b
        ld c, a                 ; entries to move
        or a
        jr z, hi_put            ; inserting at the bottom, no shift
        ; --- shift scores ---
        push bc
        ld a, c
        add a, a
        ld c, a
        ld b, 0                 ; BC = move bytes (scores)
        ld hl, hs_scores+7      ; entry 3 high byte
        ld de, hs_scores+9      ; entry 4 high byte
        lddr
        pop bc
        ; --- shift names ---
        push bc
        ld a, c
        ld d, a
        add a, a
        add a, d                ; *3
        ld c, a
        ld b, 0
        ld hl, hs_names+11      ; entry 3 last char
        ld de, hs_names+14      ; entry 4 last char
        lddr
        pop bc
hi_put:
        ; write new entry at index B
        ld a, b
        add a, a
        ld e, a
        ld d, 0
        ld hl, hs_scores
        add hl, de
        ld a, (score)
        ld (hl), a
        inc hl
        ld a, (score+1)
        ld (hl), a
        ; names[B] = ie_buf
        ld a, b
        ld d, a
        add a, a
        add a, d                ; *3
        ld e, a
        ld d, 0
        ld hl, hs_names
        add hl, de
        ld a, (ie_buf+0)
        ld (hl), a
        inc hl
        ld a, (ie_buf+1)
        ld (hl), a
        inc hl
        ld a, (ie_buf+2)
        ld (hl), a
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
        call init_cave
        ld a, (difficulty)      ; lives: Cadet 5, Pilot 3, Ace 2
        or a
        jr nz, ig_d1
        ld a, 5
        jr ig_setlives
ig_d1:
        cp 1
        jr nz, ig_d2
        ld a, 3
        jr ig_setlives
ig_d2:
        ld a, 2
ig_setlives:
        ld (lives), a
        xor a
        ld (combo), a
        ld (combo_timer), a
        ld (charge), a
        ld (fire_held), a
        ld (bomb_prev), a
        ld hl, 0
        ld (kill_acc), hl
        ld a, 1
        ld (combo_mult), a
        ld (bullet_type), a
        ld a, 3
        ld (bombs), a
        ld a, 0xFF
        ld (hud_combo), a
        ld (hud_bombs), a
        ld a, 180               ; planet starts off to the right
        ld (planet_x), a
        ld (planet_ox), a
        ld a, 50
        ld (planet_y), a
        ld (planet_oy), a
        ld hl, 0
        ld (score), hl
        xor a
        ld (world), a
        call set_world_attr
        ld a, 24
        ld (ship_x), a
        ld a, 88
        ld (ship_y), a
        ld a, 8
        ld (vxb), a             ; rest velocity (biased)
        ld (vyb), a
        xor a
        ld (invuln), a
        ld (fire_prev), a
        ld (fire_cd), a
        ld (pw_twin), a
        ld (pw_rapid), a
        ld (pw_shield), a
        ld (pw_speed), a
        ld (pw_next), a
        ld (boss_active), a
        ld (boss_hp), a
        ld (shake), a
        ld (paused), a
        ld (pause_prev), a
        ld (poptimer), a
        ld (pop_shown), a
        ld hl, 0xFFFF           ; force a full HUD redraw on the first frame
        ld (hud_score), hl
        ld a, 0xFF
        ld (hud_lives), a
        ld (hud_pow), a
        ld a, 0xFE
        ld (hud_bar), a
        ld (hud_zone), a
        ld a, (spawn_period)
        ld (spawn_timer), a
        call set_zone_wave      ; zone 0 wave script
        call set_music_zone
        xor a
        ld (midboss_flag), a
        ld a, 1                 ; show zone-1 intro banner
        ld (zone_msg), a
        ld a, 80
        ld (zone_msg_t), a
        ld hl, 0
        ld (bonus_timer), hl
        ld hl, 1000
        ld (next_life), hl
        ld hl, 750
        ld (world_timer), hl
        ret

zero_objects:
        ld hl, objs
        ld de, objs+1
        ld bc, MAXOBJ*OBJSZ-1
        ld (hl), 0
        ldir
        ld hl, bullets
        ld de, bullets+1
        ld bc, MAXBUL*4-1
        ld (hl), 0
        ldir
        ld hl, ebullets
        ld de, ebullets+1
        ld bc, MAXEB*6-1
        ld (hl), 0
        ldir
        ld hl, expls            ; clear explosions too (was uninitialised)
        ld de, expls+1
        ld bc, MAXEXPL*3-1
        ld (hl), 0
        ldir
        ld hl, debris
        ld de, debris+1
        ld bc, DEB*5-1
        ld (hl), 0
        ldir
        ret

; ============================================================================
;  PER-FRAME PLAY
; ============================================================================
play_frame:
        ld a, (anim_ctr)
        inc a
        ld (anim_ctr), a
        call do_planet          ; far background layer (drawn first)
        call do_stars
        call scroll_terrain
        ; erase ship at old position
        ld a, (ship_x)
        ld (spr_x), a
        ld a, (ship_y)
        ld (spr_y), a
        call erase_ship
        call uncolor_ship       ; clear old ship colour cells
        call erase_flame        ; clear last frame's exhaust
        call read_input
        call cave_collide       ; crash if the ship flew into a wall
        call do_cheats
        call do_fire
        call do_bullets
        call do_objects
        call do_ebullets
        call do_explosions
        call do_debris
        call tick_combo
        call do_bomb
        call do_spawn
        ; invulnerability countdown + blink / colour-pulse
        ld a, (invuln)
        or a
        jr z, pf_drawship
        dec a
        ld (invuln), a
        ld a, (opt_softblink)
        or a
        jr nz, pf_drawship      ; soft mode: always draw, pulse colour below
        ld a, (invuln)
        and 4
        jr nz, pf_skipship      ; classic mode: hide on alternate phases
pf_drawship:
        call draw_flame         ; animated exhaust behind the ship
        call set_ship_ptr       ; spr_ptr = the chosen ship design
        ld a, (ship_x)
        ld (spr_x), a
        ld a, (ship_y)
        ld (spr_y), a
        call draw_ship
        ld hl, pw_shield        ; ink: shield=white, else cyan (pulse if invuln)
        ld a, (hl)
        or a
        jr z, pf_ink_inv
        ld a, 7
        jr pf_shipink
pf_ink_inv:
        ld a, (invuln)
        or a
        jr z, pf_ink_cyan
        and 4                   ; pulse cyan<->white while invulnerable
        jr z, pf_ink_cyan
        ld a, 7
        jr pf_shipink
pf_ink_cyan:
        ld a, 5
pf_shipink:
        call color_ship
        jr pf_aftership
pf_skipship:
        call erase_flame        ; keep the exhaust from lingering when hidden
pf_aftership:
        ld hl, (bonus_timer)    ; bonus-stage countdown
        ld a, h
        or l
        jr z, pf_nobonus
        dec hl
        ld (bonus_timer), hl
pf_nobonus:
        call do_music
        call engine_drone
        ; screen-shake / border-flash decay
        call do_shake
        ; zone timer: only counts down when no boss is active
        ld a, (boss_active)
        or a
        jr nz, pf_hud
        ld hl, (world_timer)
        dec hl
        ld (world_timer), hl
        ld a, h
        or l
        jr nz, pf_hud
        call start_boss
pf_hud:
        call show_hud
        ret

; draw_flame: animated exhaust just behind the ship's tail
draw_flame:
        ld a, (anim_ctr)
        and 4
        ld a, SI_FLAME1
        jr z, fl_set
        ld a, SI_FLAME2
fl_set:
        ld (spr_idx), a
        ld a, (ship_x)
        sub 8                   ; tail is to the left (travel is rightward)
        jr nc, fl_xok
        xor a
fl_xok:
        ld (spr_x), a
        ld (flame_ox), a
        ld a, (ship_y)
        ld (spr_y), a
        ld (flame_oy), a
        call draw_sprite_ps
        ld a, 6                 ; yellow/orange flare
        ld hl, anim_ctr
        bit 1, (hl)
        jr z, fl_ink
        ld a, 2                 ; flicker to red
fl_ink:
        call color_obj
        ret

; erase_flame: clear last frame's exhaust at flame_ox/oy
erase_flame:
        ld a, (flame_ox)
        ld (spr_x), a
        ld a, (flame_oy)
        ld (spr_y), a
        call erase_sprite
        call uncolor_obj
        ret

; brief border flash + ship jitter while shake>0
do_shake:
        ld a, (shake)
        or a
        ret z
        dec a
        ld (shake), a
        ld b, a
        ld a, (opt_shake)       ; photosensitivity: skip the border flash
        or a
        jr z, ds_normal
        ld a, b
        and 1
        jr z, ds_normal
        ld a, 7                 ; white flash on alternate frames
        out (254), a
        ret
ds_normal:
        ld a, (cur_border)
        out (254), a
        ret

; ============================================================================
;  INPUT  -  QAOP, plus Kempston joystick directions
; ============================================================================
read_input:
        xor a                   ; reset movement intents
        ld (want_x), a
        ld (want_y), a
        ld a, (ctrl_scheme)
        or a
        jr nz, ri_cursor
        ; ----- scheme 0: QAOP -----
        ld bc, 0xFBFE           ; Q -> up
        in a, (c)
        bit 0, a
        jr nz, ri_notup
        call ship_up
ri_notup:
        ld bc, 0xFDFE           ; A -> down
        in a, (c)
        bit 0, a
        jr nz, ri_notdn
        call ship_down
ri_notdn:
        ld bc, 0xDFFE           ; O -> left
        in a, (c)
        bit 1, a
        jr nz, ri_notlf
        call ship_left
ri_notlf:
        ld bc, 0xDFFE           ; P -> right
        in a, (c)
        bit 0, a
        jr nz, ri_notrt
        call ship_right
        jr ri_notrt
ri_cursor:
        ; ----- scheme 1: cursor keys 5/6/7/8 -----
        ld bc, 0xEFFE           ; 6,7,8,9,0 row
        in a, (c)
        bit 3, a                ; 7 -> up
        jr nz, ri_cnotup
        call ship_up
ri_cnotup:
        bit 4, a                ; 6 -> down
        jr nz, ri_cnotdn
        call ship_down
ri_cnotdn:
        bit 2, a                ; 8 -> right
        jr nz, ri_cnotrt
        call ship_right
ri_cnotrt:
        ld bc, 0xF7FE           ; 1-5 row
        in a, (c)
        bit 4, a                ; 5 -> left
        jr nz, ri_notrt
        call ship_left
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
        call apply_inertia
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

; do_cheats: I=invincible (hold), G=grant power-ups (hold), K=skip zone
do_cheats:
        ld bc, 0xDFFE           ; I
        in a, (c)
        bit 2, a
        jr nz, ch_noi
        ld a, 40
        ld (invuln), a
ch_noi:
        ld bc, 0xFDFE           ; G
        in a, (c)
        bit 4, a
        jr nz, ch_nog
        ld a, 1
        ld (pw_twin), a
        ld (pw_rapid), a
        ld (pw_shield), a
        ld (pw_speed), a
ch_nog:
        ld bc, 0xBFFE           ; K -> next zone (edge)
        in a, (c)
        bit 2, a
        jr nz, ch_kup
        ld a, (cheat_kprev)
        or a
        ret nz
        ld a, 1
        ld (cheat_kprev), a
        call next_world
        ret
ch_kup:
        xor a
        ld (cheat_kprev), a
        ret

; input handlers just record intent (-1 / +1); apply_inertia does the rest
ship_up:
        ld a, 0xFF
        ld (want_y), a
        ret
ship_down:
        ld a, 1
        ld (want_y), a
        ret
ship_left:
        ld a, 0xFF
        ld (want_x), a
        ret
ship_right:
        ld a, 1
        ld (want_x), a
        ret

; apply_inertia: accelerate velocity toward intent (capped), else decay
; toward rest, then move the ship.  Velocities are biased by +8 so all
; the comparisons stay unsigned.
apply_inertia:
        ld a, (pw_speed)        ; max speed (2, or 3 with speed power-up)
        or a
        ld a, 2
        jr z, ai_mv
        ld a, 3
ai_mv:
        ld (ship_step), a
        ; ----- horizontal -----
        ld a, (want_x)
        or a
        jr z, ai_xdecay
        bit 7, a
        jr nz, ai_xleft
        ld a, (ship_step)       ; right: vxb -> min(vxb+1, 8+max)
        add a, 8
        ld c, a
        ld a, (vxb)
        inc a
        cp c
        jr c, ai_xstore
        ld a, c
ai_xstore:
        ld (vxb), a
        jr ai_xmove
ai_xleft:
        ld a, 8                 ; left: vxb -> max(vxb-1, 8-max)
        ld c, a
        ld a, (ship_step)
        ld b, a
        ld a, c
        sub b
        ld c, a
        ld a, (vxb)
        dec a
        cp c
        jr nc, ai_xstore2
        ld a, c
ai_xstore2:
        ld (vxb), a
        jr ai_xmove
ai_xdecay:
        ld a, (vxb)
        cp 8
        jr z, ai_xmove
        jr c, ai_xdec_up
        dec a
        ld (vxb), a
        jr ai_xmove
ai_xdec_up:
        inc a
        ld (vxb), a
ai_xmove:
        ld a, (vxb)
        sub 8
        ld b, a
        ld a, (ship_x)
        add a, b
        cp 4
        jr nc, ai_xlo
        ld a, 4
ai_xlo:
        cp 121
        jr c, ai_xhi
        ld a, 120
ai_xhi:
        ld (ship_x), a
        ; ----- vertical -----
        ld a, (want_y)
        or a
        jr z, ai_ydecay
        bit 7, a
        jr nz, ai_yup
        ld a, (ship_step)       ; down
        add a, 8
        ld c, a
        ld a, (vyb)
        inc a
        cp c
        jr c, ai_ystore
        ld a, c
ai_ystore:
        ld (vyb), a
        jr ai_ymove
ai_yup:
        ld a, 8                 ; up
        ld c, a
        ld a, (ship_step)
        ld b, a
        ld a, c
        sub b
        ld c, a
        ld a, (vyb)
        dec a
        cp c
        jr nc, ai_ystore2
        ld a, c
ai_ystore2:
        ld (vyb), a
        jr ai_ymove
ai_ydecay:
        ld a, (vyb)
        cp 8
        jr z, ai_ymove
        jr c, ai_ydec_up
        dec a
        ld (vyb), a
        jr ai_ymove
ai_ydec_up:
        inc a
        ld (vyb), a
ai_ymove:
        ld a, (vyb)
        sub 8
        ld b, a
        ld a, (ship_y)
        add a, b
        cp 24
        jr nc, ai_ylo
        ld a, 24
ai_ylo:
        cp 161
        jr c, ai_yhi
        ld a, 160
ai_yhi:
        ld (ship_y), a
        ; ----- banking frame from vertical intent -----
        ld a, (want_y)
        or a
        jr z, ai_banklevel
        bit 7, a
        jr nz, ai_bankup
        ld a, 2                 ; down -> dive frame
        jr ai_bankset
ai_bankup:
        ld a, 1                 ; up -> climb frame
        jr ai_bankset
ai_banklevel:
        xor a
ai_bankset:
        ld (ship_bank), a
        ret

; ============================================================================
;  SHOOTING
; ============================================================================
do_fire:
        ld a, (fire_cd)         ; tick down the fire cooldown
        or a
        jr z, df_cdok
        dec a
        ld (fire_cd), a
df_cdok:
        ld bc, 0x7FFE           ; Space
        in a, (c)
        bit 0, a
        jr z, df_pressed
        call read_joy           ; Kempston fire
        bit 4, a
        jr nz, df_pressed
        xor a                   ; released: reset hold + charge
        ld (fire_held), a
        ld (charge), a
        ret
df_pressed:
        ld a, 1
        ld (fire_held), a
        ld a, (charge)          ; build charge while held (capped)
        cp 30
        jr nc, df_chgok
        inc a
        ld (charge), a
df_chgok:
        ld a, (fire_cd)
        or a
        ret nz                  ; auto-repeat gated by cooldown
        ld a, (pw_rapid)        ; cooldown depends on rapid-fire power-up
        or a
        ld a, 8
        jr z, df_setcd
        ld a, 4
df_setcd:
        ld (fire_cd), a
        ld a, (charge)          ; fully charged -> piercing bolt
        cp 30
        jr c, df_normal
        xor a
        ld (charge), a
        ld a, 2
        ld (bullet_type), a
        ld a, 7
        call spawn_bullet
        call sfx_power
        ret
df_normal:
        ld a, 1
        ld (bullet_type), a
        ld a, 7                 ; primary shot (centre)
        call spawn_bullet
        ld a, (pw_twin)         ; spread shot?
        or a
        jr z, df_done
        ld a, 1
        call spawn_bullet
        ld a, 13
        call spawn_bullet
df_done:
        call sfx_shoot
        ret

; spawn_bullet: A = y-offset from the ship top; muzzle at the nose
spawn_bullet:
        ld (sb_yoff), a
        ld ix, bullets
        ld a, MAXBUL
        ld (bcount), a
sb_find:
        ld a, (ix+0)
        or a
        jr z, sb_free
        ld de, 4
        add ix, de
        ld a, (bcount)
        dec a
        ld (bcount), a
        jr nz, sb_find
        ret                     ; no free slot
sb_free:
        ld a, (bullet_type)
        ld (ix+0), a
        ld a, (ship_x)
        add a, 22
        ld (ix+1), a
        ld (ix+3), a            ; ox
        ld a, (ship_y)
        ld b, a
        ld a, (sb_yoff)
        add a, b
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
        jp z, db_objnext
        cp T_CRYS
        jp z, db_objnext        ; bullets pass through crystals
        cp T_POWER
        jp z, db_objnext        ; and power-ups
        ld a, 13
        ld (col_thr), a
        ld a, (ix+1)
        ld b, a
        ld a, (ix+2)
        ld c, a
        ld a, (iy+1)
        ld d, a
        ld a, (iy+2)
        ld e, a
        call collide
        jp nz, db_objnext
        ld a, (iy+0)
        cp T_BOSS
        jr z, db_hit_boss
        cp T_MIDBOSS
        jp z, db_hit_mb
        ; normal target destroyed
        ld a, (iy+3)
        ld (spr_x), a
        ld a, (iy+4)
        ld (spr_y), a
        call erase_sprite
        call uncolor_obj
        call spawn_explosion
        call spawn_debris       ; a few sparks fly out
        xor a
        ld (iy+0), a
        ld bc, 5
        call add_kill_score
        ld hl, str_p5
        call set_popup
        call sfx_explode
        ld a, (ix+0)            ; piercing bolt keeps going
        cp 2
        jp z, db_objnext
        xor a
        ld (ix+0), a
        jp db_next
db_hit_boss:
        ld a, (ix+0)            ; charged bolt does extra boss damage
        cp 2
        jr z, dhb_charged
        ld a, (boss_hp)
        dec a
        jr dhb_store
dhb_charged:
        ld a, (boss_hp)
        sub 3
        jr nc, dhb_store
        xor a
dhb_store:
        ld (boss_hp), a
        ld a, (ix+0)            ; charged pierces; normal is consumed
        cp 2
        jr z, dhb_keep
        xor a
        ld (ix+0), a
dhb_keep:
        ld bc, 2
        call add_score
        ld a, 3
        ld (shake), a
        call sfx_hit
        ld a, (boss_hp)
        or a
        jp nz, db_next
        call kill_boss
        jp db_next
db_hit_mb:
        dec (iy+7)              ; mini-boss hp in +7
        jr z, db_mb_dead
        ld bc, 1
        call add_score
        call sfx_hit
        ld a, 2
        ld (shake), a
        ld a, (ix+0)            ; charged pierces; normal consumed
        cp 2
        jp z, db_objnext
        xor a
        ld (ix+0), a
        jp db_next
db_mb_dead:
        ld a, (iy+3)
        ld (spr_x), a
        ld a, (iy+4)
        ld (spr_y), a
        call erase_sprite
        call uncolor_obj
        call spawn_explosion
        call spawn_debris
        xor a
        ld (iy+0), a
        ld bc, 50
        call add_kill_score
        ld hl, str_p50
        call set_popup
        call sfx_explode
        ld a, (ix+0)
        cp 2
        jp z, db_objnext
        xor a
        ld (ix+0), a
        jp db_next
db_objnext:
        ld de, OBJSZ
        add iy, de
        ld a, (ocount)
        dec a
        ld (ocount), a
        jp nz, db_objloop
        ; survived: draw bullet (charged bolt is a round magenta tracer)
        ld a, (ix+0)
        cp 2
        jr z, db_drawch
        ld a, SI_BULLET
        ld (spr_idx), a
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call draw_sprite_ps
        jr db_storeox
db_drawch:
        ld a, SI_EBUL
        ld (spr_idx), a
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call draw_sprite_ps
        ld a, 3                 ; magenta charged bolt
        call color_obj
db_storeox:
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
        cp T_BOSS
        jp z, do_obj_boss
        ; --- erase at old position ---
        ld a, (ix+3)
        ld (spr_x), a
        ld a, (ix+4)
        ld (spr_y), a
        call erase_sprite
        call uncolor_obj
        ; --- advance phase ---
        inc (ix+6)
        ; --- horizontal move (left) ---
        ld a, (ix+1)
        sub (ix+5)
        jr nc, obj_alive
        xor a
        ld (ix+0), a            ; off the left edge -> free
        jp do_obj_next
obj_alive:
        ld (ix+1), a
        ; --- per-type vertical behaviour ---
        ld a, (ix+0)
        cp T_ENEMY
        jr z, mv_enemy
        cp T_MIDBOSS
        jp z, mv_midboss
        cp T_DIVER
        jp z, mv_diver
        cp T_DRONE
        jp z, mv_drone
        cp T_TURRET
        jp z, mv_turret
        jp obj_movey_done       ; rocks / mines / crystals / pods: drift only
mv_enemy:
        ld a, (ix+6)
        rra                     ; phase/2
        and 0x1F                ; sine index 0..31
        ld e, a
        ld d, 0
        ld hl, sintab
        add hl, de
        ld a, (ix+7)            ; ybase
        add a, (hl)             ; + sine (0..24)
        ld (ix+2), a
        ld a, (ix+6)            ; occasional aimed shot
        and 0x3F
        cp 20
        jp nz, obj_movey_done
        ld a, (ix+1)
        cp 200
        jp nc, obj_movey_done
        call enemy_fire
        jp obj_movey_done
mv_midboss:
        ld a, (ix+1)            ; hold station at x~190 once it arrives
        cp 190
        jr nc, mvmb_fire
        add a, (ix+5)
        ld (ix+1), a
mvmb_fire:
        ld a, (ix+6)            ; fires often (every ~32 frames, two-ish)
        and 0x1F
        cp 10
        jp nz, obj_movey_done
        ld a, (ix+1)
        cp 220
        jp nc, obj_movey_done
        call enemy_fire
        jp obj_movey_done
mv_diver:
        ld a, (ship_y)          ; swoop toward the ship's height (2px)
        ld b, a
        ld a, (ix+2)
        cp b
        jp z, obj_movey_done
        jr c, mvd_down
        dec (ix+2)
        dec (ix+2)
        jp obj_movey_done
mvd_down:
        inc (ix+2)
        inc (ix+2)
        jp obj_movey_done
mv_drone:
        ld a, (ship_y)          ; home 1px/frame
        ld b, a
        ld a, (ix+2)
        cp b
        jr z, mvdr_fire
        jr c, mvdr_dn
        dec (ix+2)
        jr mvdr_fire
mvdr_dn:
        inc (ix+2)
mvdr_fire:
        ld a, (ix+6)
        and 0x3F
        cp 24
        jp nz, obj_movey_done
        ld a, (ix+1)
        cp 200
        jp nc, obj_movey_done
        call enemy_fire
        jp obj_movey_done
mv_turret:
        ld a, (ix+6)            ; fixed y, fire aimed periodically
        and 0x3F
        cp 28
        jp nz, obj_movey_done
        ld a, (ix+1)
        cp 210
        jp nc, obj_movey_done
        call enemy_fire
        jp obj_movey_done
obj_movey_done:
        ; --- ship collision (unless invulnerable) ---
        ld a, (invuln)
        or a
        jr nz, obj_draw
        ld a, 9                 ; tight, fair hit-box
        ld (col_thr), a
        ld a, (ship_x)
        add a, 4
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
        cp T_CRYS
        jr z, obj_collect
        cp T_POWER
        jr z, obj_power
        ; hazard hit
        ld a, (ix+3)
        ld (spr_x), a
        ld a, (ix+4)
        ld (spr_y), a
        call spawn_explosion
        call ship_hit
        xor a
        ld (ix+0), a
        jp do_obj_next
obj_collect:
        ld bc, 10
        call add_score
        xor a
        ld (ix+0), a
        ld hl, str_p10
        call set_popup
        call sfx_pickup
        jp do_obj_next
obj_power:
        xor a
        ld (ix+0), a
        call grant_power
        ld hl, str_pwr
        call set_popup
        call sfx_power
        jp do_obj_next
obj_draw:
        ld a, (ix+0)
        cp T_CRYS
        jr z, obj_spr_c
        cp T_POWER
        jr z, obj_spr_p
        cp T_ENEMY
        jr z, obj_spr_e
        cp T_DIVER
        jr z, obj_spr_dv
        cp T_DRONE
        jr z, obj_spr_dr
        cp T_MINE
        jr z, obj_spr_mn
        cp T_TURRET
        jr z, obj_spr_tr
        cp T_MIDBOSS
        jr z, obj_spr_mb
        ; rock: spin between two frames
        ld a, (ix+6)
        and 4
        jr z, obj_rk0
        ld a, SI_ROCK2
        jr obj_rkd
obj_rk0:
        ld a, SI_ROCK
obj_rkd:
        ld (spr_idx), a
        ld a, 7                 ; rock: white
        jr obj_spr_set
obj_spr_e:
        ld a, SI_ENEMY
        ld (spr_idx), a
        ld a, 4                 ; enemy: green
        jr obj_spr_set
obj_spr_dv:
        ld a, SI_DIVER
        ld (spr_idx), a
        ld a, 2                 ; diver: red
        jr obj_spr_set
obj_spr_dr:
        ld a, SI_DRONE
        ld (spr_idx), a
        ld a, 6                 ; drone: yellow
        jr obj_spr_set
obj_spr_mn:
        ld a, SI_MINE
        ld (spr_idx), a
        ld a, 2                 ; mine: red
        jr obj_spr_set
obj_spr_tr:
        ld a, SI_TURRET
        ld (spr_idx), a
        ld a, 7                 ; turret: white
        jr obj_spr_set
obj_spr_mb:
        ld a, SI_ENEMY
        ld (spr_idx), a
        ld a, 3                 ; mini-boss: magenta
        jr obj_spr_set
obj_spr_c:
        ; crystal: pulse between two frames
        ld a, (ix+6)
        and 4
        jr z, obj_cr0
        ld a, SI_CRYS2
        jr obj_crd
obj_cr0:
        ld a, SI_CRYS
obj_crd:
        ld (spr_idx), a
        ld a, 6                 ; crystal: yellow
        jr obj_spr_set
obj_spr_p:
        ld a, SI_POWER
        ld (spr_idx), a
        ld a, (anim_ctr)        ; power-up: shimmering cycle of bright inks
        rrca
        rrca
        and 3
        ld e, a
        ld d, 0
        ld hl, pw_coltab
        add hl, de
        ld a, (hl)
obj_spr_set:
        ld (obj_ink), a
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call draw_sprite_ps
        ld a, (obj_ink)
        call color_obj
        ld a, (ix+1)
        ld (ix+3), a
        ld a, (ix+2)
        ld (ix+4), a
do_obj_next:
        ld de, OBJSZ
        add ix, de
        ld a, (ocount)
        dec a
        ld (ocount), a
        jp nz, do_obj_loop
        ret

; ---- boss handling within the object loop ----
do_obj_boss:
        ; erase old (wide)
        ld a, (ix+3)
        ld (spr_x), a
        ld a, (ix+4)
        ld (spr_y), a
        call erase_ship
        call uncolor_ship
        inc (ix+6)
        ; approach: move left until x <= 200, then hold
        ld a, (ix+1)
        cp 201
        jr c, boss_hold
        dec (ix+1)
boss_hold:
        ; vertical oscillation on the sine
        ld a, (ix+6)
        rra
        and 0x1F
        ld e, a
        ld d, 0
        ld hl, sintab
        add hl, de
        ld a, 60                ; centre
        add a, (hl)
        add a, (hl)             ; doubled amplitude
        ld (ix+2), a
        ; boss fire — phase 2 (below half HP) fires twice as often
        ld a, (boss_hp_max)
        srl a
        ld b, a                 ; half HP
        ld a, (boss_hp)
        cp b
        ld a, (ix+6)
        jr nc, boss_p1
        and 0x0F                ; phase 2: rapid fire
        jr boss_firechk
boss_p1:
        and 0x1F
boss_firechk:
        jr nz, boss_nofire
        call boss_fire
boss_nofire:
        ; ship collision with boss (medium box)
        ld a, (invuln)
        or a
        jr nz, boss_draw
        ld a, 14
        ld (col_thr), a
        ld a, (ship_x)
        add a, 4
        ld b, a
        ld a, (ship_y)
        ld c, a
        ld a, (ix+1)
        ld d, a
        ld a, (ix+2)
        ld e, a
        call collide
        jr nz, boss_draw
        call ship_hit
boss_draw:
        ld hl, (boss_spr)       ; per-zone boss silhouette
        ld (spr_ptr), hl
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call draw_ship
        ld a, (boss_hp_max)     ; phase 2 turns the boss red
        srl a
        ld b, a
        ld a, (boss_hp)
        cp b
        ld a, 3                 ; phase 1: magenta
        jr nc, boss_ink
        ld a, 2                 ; phase 2: red (enraged)
boss_ink:
        call color_ship
        ld a, (ix+1)
        ld (ix+3), a
        ld a, (ix+2)
        ld (ix+4), a
        jp do_obj_next

ship_hit:
        call reset_combo        ; any hit breaks the chain
        ; practice mode: a tap, brief invuln, but no life lost
        ld a, (opt_practice)
        or a
        jr z, sh_noprac
        ld a, 40
        ld (invuln), a
        ld a, 6
        ld (shake), a
        call sfx_hit
        ret
sh_noprac:
        ; shield absorbs the hit?
        ld a, (pw_shield)
        or a
        jr z, sh_real
        dec a
        ld (pw_shield), a
        ld a, 30
        ld (invuln), a
        ld a, 6
        ld (shake), a
        call sfx_hit
        ret
sh_real:
        ld a, (ship_x)
        ld (spr_x), a
        ld a, (ship_y)
        ld (spr_y), a
        call erase_ship
        call uncolor_ship
        call spawn_explosion
        call sfx_explode
        ld a, 10
        ld (shake), a
        ld a, (lives)
        dec a
        ld (lives), a
        jr z, sh_dead
        ld a, 75
        ld (invuln), a
        xor a                   ; lose volatile power-ups on death
        ld (pw_twin), a
        ld (pw_rapid), a
        ld (pw_speed), a
        ld a, 24
        ld (ship_x), a
        ld a, 88
        ld (ship_y), a
        ret
sh_dead:
        ld a, 2
        ld (state), a
        ret

; collide: B=ax C=ay D=bx E=by ; Z set if |dx|<col_thr and |dy|<col_thr
collide:
        ld a, b
        sub d
        jr nc, c_dx
        neg
c_dx:
        ld hl, col_thr
        cp (hl)
        jr nc, c_none
        ld a, c
        sub e
        jr nc, c_dy
        neg
c_dy:
        ld hl, col_thr
        cp (hl)
        jr nc, c_none
        xor a
        ret
c_none:
        or 1
        ret

; ============================================================================
;  ENEMY BULLETS  -  active(+0) x(+1) y(+2) ox(+3) oy(+4) vy(+5, signed)
; ============================================================================
; enemy_fire: spawn a bullet from object IX, aimed roughly at the ship
enemy_fire:
        push ix
        ld iy, ebullets
        ld a, MAXEB
        ld (ecount), a
ef_find:
        ld a, (iy+0)
        or a
        jr z, ef_free
        ld de, 6
        add iy, de
        ld a, (ecount)
        dec a
        ld (ecount), a
        jr nz, ef_find
        pop ix
        ret
ef_free:
        ld a, 1
        ld (iy+0), a
        ld a, (ix+1)            ; start at the enemy
        ld (iy+1), a
        ld (iy+3), a
        ld a, (ix+2)
        add a, 6
        ld (iy+2), a
        ld (iy+4), a
        ; vy = sign(ship_y - enemy_y) * 2
        ld a, (ship_y)
        ld b, a
        ld a, (iy+2)
        ld c, a
        ld a, b
        sub c
        jr nc, ef_down
        ld a, -2                ; ship above -> go up
        jr ef_setvy
ef_down:
        ld a, 2
ef_setvy:
        ld (iy+5), a
        pop ix
        call sfx_efire
        ret

; boss_fire: spread of three bullets straight left
boss_fire:
        push ix
        ld a, 0
        call boss_one
        ld a, -3
        call boss_one
        ld a, 3
        call boss_one
        pop ix
        ret
; boss_one: A = vy ; spawn one boss bullet from current boss obj (IX)
boss_one:
        ld (eb_vy), a
        ld iy, ebullets
        ld a, MAXEB
        ld (ecount), a
bo_find:
        ld a, (iy+0)
        or a
        jr z, bo_free
        ld de, 6
        add iy, de
        ld a, (ecount)
        dec a
        ld (ecount), a
        jr nz, bo_find
        ret
bo_free:
        ld a, 1
        ld (iy+0), a
        ld a, (ix+1)
        ld (iy+1), a
        ld (iy+3), a
        ld a, (ix+2)
        add a, 6
        ld (iy+2), a
        ld (iy+4), a
        ld a, (eb_vy)
        ld (iy+5), a
        ret

do_ebullets:
        ld ix, ebullets
        ld a, MAXEB
        ld (ecount), a
deb_loop:
        ld a, (ix+0)
        or a
        jp z, deb_next
        ; erase old
        ld a, (ix+3)
        ld (spr_x), a
        ld a, (ix+4)
        ld (spr_y), a
        call erase_sprite
        ; move left and by vy
        ld a, (ix+1)
        sub 4
        jr nc, deb_alive
        xor a
        ld (ix+0), a
        jp deb_next
deb_alive:
        ld (ix+1), a
        ld a, (ix+2)
        add a, (ix+5)           ; + vy (signed)
        ld (ix+2), a
        ; off top/bottom?
        cp 16
        jr c, deb_kill
        cp 184
        jr nc, deb_kill
        ; collide with ship?
        ld a, (invuln)
        or a
        jr nz, deb_draw
        ld a, 8
        ld (col_thr), a
        ld a, (ship_x)
        add a, 4
        ld b, a
        ld a, (ship_y)
        ld c, a
        ld a, (ix+1)
        ld d, a
        ld a, (ix+2)
        ld e, a
        call collide
        jr nz, deb_draw
        call ship_hit
        xor a
        ld (ix+0), a
        jp deb_next
deb_kill:
        xor a
        ld (ix+0), a
        jp deb_next
deb_draw:
        ld a, SI_EBUL
        ld (spr_idx), a
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call draw_sprite_ps
        ld a, 6                 ; yellow tracer
        call color_obj
        ld a, (ix+1)
        ld (ix+3), a
        ld a, (ix+2)
        ld (ix+4), a
deb_next:
        ld de, 6
        add ix, de
        ld a, (ecount)
        dec a
        ld (ecount), a
        jp nz, deb_loop
        ret

; ============================================================================
;  EXPLOSIONS  -  timer(+0) x(+1) y(+2)   (3 expanding frames)
; ============================================================================
; spawn_explosion: at spr_x,spr_y
spawn_explosion:
        push ix                 ; preserve caller's object/bullet pointer
        ld ix, expls
        ld a, MAXEXPL
        ld (xcount), a
se_find:
        ld a, (ix+0)
        or a
        jr z, se_free
        ld de, 3
        add ix, de
        ld a, (xcount)
        dec a
        ld (xcount), a
        jr nz, se_find
        pop ix
        ret
se_free:
        ld a, 6
        ld (ix+0), a            ; timer
        ld a, (spr_x)
        ld (ix+1), a
        ld a, (spr_y)
        ld (ix+2), a
        call ay_noise_burst     ; 128K: explosion noise
        pop ix
        ret

do_explosions:
        ld ix, expls
        ld a, MAXEXPL
        ld (xcount), a
dx_loop:
        ld a, (ix+0)
        or a
        jr z, dx_next
        ; erase box
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call erase_sprite
        ; tick
        ld a, (ix+0)
        dec a
        ld (ix+0), a
        jr z, dx_next           ; finished (left erased)
        ; choose frame by timer (5,4=small 3,2=mid 1=big)
        cp 4
        jr nc, dx_f1
        cp 2
        jr nc, dx_f2
        ld a, SI_EX3
        jr dx_setspr
dx_f1:
        ld a, SI_EX1
        jr dx_setspr
dx_f2:
        ld a, SI_EX2
dx_setspr:
        ld (spr_idx), a
        ld a, (ix+1)
        ld (spr_x), a
        ld a, (ix+2)
        ld (spr_y), a
        call draw_sprite_ps
        ld a, 6                 ; yellow blast
        call color_obj
dx_next:
        ld de, 3
        add ix, de
        ld a, (xcount)
        dec a
        ld (xcount), a
        jr nz, dx_loop
        ret

; ============================================================================
;  DEBRIS / SPARKS  -  life(+0) x(+1) y(+2) vx(+3,signed) vy(+4,signed)
; ============================================================================
; spawn_debris: scatter a few sparks from spr_x,spr_y
spawn_debris:
        push ix                 ; preserve caller's object/bullet pointer
        ld b, 4                 ; try to seed up to 4 sparks
        ld ix, debris
sd_loop:
        push bc
        ld a, (ix+0)
        or a
        jr nz, sd_skip
        ld a, 8
        ld (ix+0), a            ; life
        ld a, (spr_x)
        add a, 6
        ld (ix+1), a
        ld a, (spr_y)
        add a, 6
        ld (ix+2), a
        call rnd
        and 7
        sub 3                   ; vx in -3..+4
        ld (ix+3), a
        call rnd
        and 7
        sub 3
        ld (ix+4), a
sd_skip:
        ld de, 5
        add ix, de
        pop bc
        djnz sd_loop
        pop ix
        ret

do_debris:
        ld ix, debris
        ld b, DEB
dd_loop:
        push bc
        ld a, (ix+0)
        or a
        jr z, dd_next
        ld a, (ix+1)            ; erase old pixel
        ld (px_x), a
        ld a, (ix+2)
        ld (px_y), a
        xor a
        ld (px_set), a
        call plot_px
        dec (ix+0)
        jr z, dd_next           ; expired (erased)
        ld a, (ix+1)            ; move
        add a, (ix+3)
        ld (ix+1), a
        ld (px_x), a
        ld a, (ix+2)
        add a, (ix+4)
        ld (ix+2), a
        ld (px_y), a
        ld a, 1                 ; draw new pixel (white)
        ld (px_set), a
        call plot_px
dd_next:
        ld de, 5
        add ix, de
        pop bc
        djnz dd_loop
        ret

; plot_px: set/clear one pixel.  px_x,px_y position, px_set (1 set / 0 clear)
plot_px:
        ld a, (px_y)
        cp 192
        ret nc
        ld l, a
        ld h, 0
        add hl, hl
        ld de, addrtab
        add hl, de
        ld e, (hl)
        inc hl
        ld d, (hl)              ; DE = row base address
        ld a, (px_x)
        ld b, a
        rrca
        rrca
        rrca
        and 0x1F
        ld l, a
        ld h, 0
        add hl, de              ; HL = byte address
        ld a, b
        and 7
        ld b, a
        ld c, 0x80
pp_sh:
        xor a
        cp b
        jr z, pp_have
        srl c
        dec b
        jr pp_sh
pp_have:
        ld a, (px_set)
        or a
        jr z, pp_clear
        ld a, (hl)
        or c
        ld (hl), a
        ret
pp_clear:
        ld a, c
        cpl
        ld c, a
        ld a, (hl)
        and c
        ld (hl), a
        ret

; ============================================================================
;  SMART-BOMB  -  'B' clears every hazard on screen for points
; ============================================================================
do_bomb:
        ld bc, 0x7FFE           ; B is bit4 of the SPACE half-row
        in a, (c)
        bit 4, a
        jr nz, db_brel
        ld a, (bomb_prev)
        or a
        jr nz, db_bdone         ; held: ignore until released
        ld a, 1
        ld (bomb_prev), a
        ld a, (bombs)
        or a
        ret z                   ; none left
        dec a
        ld (bombs), a
        ld a, 12
        ld (shake), a           ; flash
        call detonate_bomb
        ret
db_brel:
        xor a
        ld (bomb_prev), a
db_bdone:
        ret

; detonate_bomb: explode all hazard objects (rocks/enemies), award points
detonate_bomb:
        ld ix, objs
        ld a, MAXOBJ
        ld (ocount), a
det_loop:
        ld a, (ix+0)
        or a
        jr z, det_next
        cp T_CRYS
        jr z, det_next
        cp T_POWER
        jr z, det_next
        cp T_BOSS
        jr z, det_next          ; bomb spares the boss
        ld a, (ix+3)
        ld (spr_x), a
        ld a, (ix+4)
        ld (spr_y), a
        call erase_sprite
        call uncolor_obj
        call spawn_explosion
        xor a
        ld (ix+0), a
        ld bc, 5
        call add_score
det_next:
        ld de, OBJSZ
        add ix, de
        ld a, (ocount)
        dec a
        ld (ocount), a
        jr nz, det_loop
        call sfx_explode
        ret

; ============================================================================
;  BACKGROUND PLANET  -  a dim sprite drifting slowly in the far distance
; ============================================================================
do_planet:
        ld a, (anim_ctr)        ; drift one pixel left every 4th frame
        and 3
        jr nz, dp_draw
        ld a, (planet_x)
        dec a
        cp 240                  ; wrapped past 0 -> respawn at the right
        jr c, dp_xok
        ld a, 200
        ld (planet_x), a
        call rnd
        and 0x3F
        add a, 40
        ld (planet_y), a
        jr dp_draw
dp_xok:
        ld (planet_x), a
dp_draw:
        ld a, (planet_ox)       ; erase old
        ld (spr_x), a
        ld a, (planet_oy)
        ld (spr_y), a
        call erase_sprite
        ld a, SI_PLANET
        ld (spr_idx), a
        ld a, (planet_x)
        ld (spr_x), a
        ld (planet_ox), a
        ld a, (planet_y)
        ld (spr_y), a
        ld (planet_oy), a
        call draw_sprite_ps
        ld a, 1                 ; dim blue, on black
        call color_obj
        ret

; ============================================================================
;  BOSS  -  occupies an object slot of type T_BOSS
; ============================================================================
start_boss:
        ld a, 1
        ld (boss_active), a
        ld a, (world)
        add a, a
        add a, a
        add a, 16               ; hp = 16 + world*4
        ld (boss_hp), a
        ld (boss_hp_max), a
        ld a, (world)           ; pick the zone's boss silhouette (cycle 4)
        and 3
        add a, a
        ld e, a
        ld d, 0
        ld hl, boss_tab
        add hl, de
        ld a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        ld (boss_spr), hl
        ld hl, str_boss
        call set_popup
        ; find a free slot
        ld ix, objs
        ld a, MAXOBJ
        ld (ocount), a
sb2_find:
        ld a, (ix+0)
        or a
        jr z, sb2_free
        ld de, OBJSZ
        add ix, de
        ld a, (ocount)
        dec a
        ld (ocount), a
        jr nz, sb2_find
        ret                     ; (shouldn't happen)
sb2_free:
        ld a, T_BOSS
        ld (ix+0), a
        ld a, 232
        ld (ix+1), a
        ld (ix+3), a
        ld a, 60
        ld (ix+2), a
        ld (ix+4), a
        ld a, 0
        ld (ix+6), a
        call sfx_zone
        ret

kill_boss:
        xor a
        ld (boss_active), a
        ld bc, 200              ; boss bonus
        call add_score
        ld a, 12
        ld (shake), a
        call sfx_explode
        ; big explosion at boss position
        call spawn_explosion
        ld hl, str_p200
        call set_popup
        call next_world
        ret

; grant_power: hand out the next upgrade in sequence (twin/rapid/shield/speed)
grant_power:
        ld a, (pw_next)
        and 3
        ld c, a
        ld a, (pw_next)
        inc a
        ld (pw_next), a
        ld a, c
        or a
        jr nz, gp_n1
        ld a, 1
        ld (pw_twin), a
        ret
gp_n1:
        cp 1
        jr nz, gp_n2
        ld a, 1
        ld (pw_rapid), a
        ret
gp_n2:
        cp 2
        jr nz, gp_n3
        ld a, 1
        ld (pw_shield), a
        ret
gp_n3:
        ld a, 1
        ld (pw_speed), a
        ret

; ============================================================================
;  COLOUR  -  paint/clear an attribute block under a sprite
;  Block is (sab_w) cells wide x 3 cells tall.
; ============================================================================
; set_attr_block: A=attr  B=cell row  C=cell col
; set_attr_block: B=cell row, C=cell col.  Fills a (sab_w x 3) block.
; Each row's attribute = (attr_row[row] & sab_keep) | sab_or, so the band
; backdrop is preserved per row (no cross-band bleed).
set_attr_block:
        push ix                 ; preserve caller's IX (object pointer etc.)
        ld a, b
        ld (sab_r), a
        ld h, 0
        ld l, b
        add hl, hl
        add hl, hl
        add hl, hl
        add hl, hl
        add hl, hl              ; row*32
        ld d, 0
        ld e, c
        add hl, de              ; + col
        ld de, ATTR
        add hl, de
        push hl
        pop ix                  ; IX = cell address
        ld b, 3                 ; rows
sab_row:
        push bc
        ld a, (sab_r)           ; band attribute for this row
        ld e, a
        ld d, 0
        ld hl, attr_row
        add hl, de
        ld a, (hl)
        ld b, a
        ld a, (sab_keep)
        and b
        ld b, a
        ld a, (sab_or)
        or b
        ld c, a                 ; final attribute
        ld a, (sab_w)
        ld b, a
sab_col:
        ld (ix+0), c
        inc ix
        dec b
        jr nz, sab_col
        ld de, 32
        ld a, (sab_w)
        ld e, a
        ld a, 32
        sub e
        ld e, a
        ld d, 0
        add ix, de              ; advance to next row start
        ld a, (sab_r)
        inc a
        ld (sab_r), a
        pop bc
        djnz sab_row
        pop ix                  ; restore caller's IX
        ret

; color_obj / color_ship: A = ink (0..7); paint cells under spr_x,spr_y
; keeping the zone's paper/bright bits.
color_obj:
        ld c, a                 ; save ink
        ld a, 3
        ld (sab_w), a
        jr color_common
color_ship:
        ld c, a
        ld a, 4
        ld (sab_w), a
color_common:                   ; C = ink on entry
        ld a, 0xF8              ; keep paper+bright, replace ink
        ld (sab_keep), a
        ld a, c
        ld (sab_or), a
        ld a, (spr_y)
        srl a
        srl a
        srl a
        ld b, a                 ; cell row
        ld a, (spr_x)
        srl a
        srl a
        srl a
        ld c, a                 ; cell col
        call set_attr_block
        ret

; uncolor_obj / uncolor_ship: reset cells under spr_x,spr_y to the band attr
uncolor_obj:
        ld a, 3
        ld (sab_w), a
        jr uncolor_common
uncolor_ship:
        ld a, 4
        ld (sab_w), a
uncolor_common:
        ld a, 0xFF             ; keep the whole band attribute
        ld (sab_keep), a
        xor a
        ld (sab_or), a
        ld a, (spr_y)
        srl a
        srl a
        srl a
        ld b, a
        ld a, (spr_x)
        srl a
        srl a
        srl a
        ld c, a
        call set_attr_block
        ret

; ============================================================================
;  SOUND  -  48K beeper (bit 4 of port 254), border bits preserved
; ============================================================================
; sfx_tone: C = pitch (delay, smaller = higher), DE = number of half-cycles
sfx_tone:
st_lp:
        ld a, (cur_border)
        or 0x10
        out (254), a            ; speaker high
        ld b, c
st_d1:
        djnz st_d1
        ld a, (cur_border)
        out (254), a            ; speaker low
        ld b, c
st_d2:
        djnz st_d2
        dec de
        ld a, d
        or e
        jr nz, st_lp
        ret

sfx_shoot:
        ld c, 18
        ld de, 26
        call sfx_tone
        ret

sfx_pickup:
        ld c, 40
        ld de, 14
        call sfx_tone
        ld c, 26
        ld de, 14
        call sfx_tone
        ld c, 15
        ld de, 18
        call sfx_tone
        ret

sfx_explode:
        ld de, 80
sfx_ex_lp:
        call rnd
        and 0x3F
        add a, 12
        ld c, a
        ld a, (cur_border)
        or 0x10
        out (254), a
        ld b, c
sfx_ex1:
        djnz sfx_ex1
        ld a, (cur_border)
        out (254), a
        ld b, c
sfx_ex2:
        djnz sfx_ex2
        dec de
        ld a, d
        or e
        jr nz, sfx_ex_lp
        ret

sfx_zone:                       ; high-to-low sweep
        ld c, 12
sfx_z_lp:
        ld de, 4
        push bc
        call sfx_tone
        pop bc
        ld a, c
        add a, 5
        ld c, a
        cp 60
        jr c, sfx_z_lp
        ret

sfx_hit:                        ; short metallic tick (boss/shield)
        ld c, 30
        ld de, 8
        call sfx_tone
        ret

sfx_efire:                      ; enemy shot: quick descending blip
        ld c, 22
        ld de, 8
        call sfx_tone
        ld c, 34
        ld de, 8
        call sfx_tone
        ret

sfx_power:                      ; power-up: bright rising arpeggio
        ld c, 50
        ld de, 12
        call sfx_tone
        ld c, 34
        ld de, 12
        call sfx_tone
        ld c, 22
        ld de, 12
        call sfx_tone
        ld c, 14
        ld de, 16
        call sfx_tone
        ret

; engine_drone: a subtle low pulse every 8th frame so play is never silent
engine_drone:
        ld a, (drone_ctr)
        inc a
        ld (drone_ctr), a
        and 7
        ret nz
        ld c, 110
        ld de, 4
        call sfx_tone
        ret

; ============================================================================
;  128K AY-3-8912 MUSIC + NOISE
;  Writes to the PSG ports do nothing on a 48K machine, so this is safe to
;  drive unconditionally; on the HC-128 it plays a tune + explosion noise.
; ============================================================================
; ay_w: D = register, E = value
ay_w:
        ld bc, 0xFFFD
        out (c), d
        ld b, 0xBF
        out (c), e
        ret

ay_init:
        ld d, 7                 ; mixer: channel A tone only
        ld e, 0x3E
        call ay_w
        ld d, 8                 ; channel A volume
        ld e, 12
        call ay_w
        ld d, 9
        ld e, 0
        call ay_w
        ld d, 10
        ld e, 0
        call ay_w
        ret

; ay_noise_burst: brief explosion noise on channel C
ay_noise_burst:
        ld d, 6                 ; noise period
        ld e, 6
        call ay_w
        ld d, 7                 ; mixer: A tone + C noise
        ld e, 0x1E
        call ay_w
        ld d, 10                ; channel C volume
        ld e, 15
        call ay_w
        ld a, 4
        ld (ay_noise_t), a
        ret

do_music:
        ld a, (ay_noise_t)      ; clear the noise burst when it ends
        or a
        jr z, dm_lead
        dec a
        ld (ay_noise_t), a
        jr nz, dm_lead
        ld d, 7
        ld e, 0x3E
        call ay_w
        ld d, 10
        ld e, 0
        call ay_w
dm_lead:
        ld a, (opt_music)       ; melody disabled?
        or a
        jr nz, dm_on
        ld d, 8                 ; silence channel A volume
        ld e, 0
        jp ay_w
dm_on:
        ld a, (mus_div)
        inc a
        ld (mus_div), a
        and 7
        ret nz                  ; advance the melody every 8 frames
        ld a, (mus_idx)
        inc a
        and 15
        ld (mus_idx), a
        ld e, a
        ld d, 0
        ld hl, (music_ptr)
        add hl, de
        ld a, (hl)              ; note index
        add a, a
        ld e, a
        ld d, 0
        ld hl, note_tab
        add hl, de
        ld e, (hl)              ; period low
        inc hl
        ld d, (hl)              ; period high
        push de
        ld d, 0                 ; reg0 = period low
        call ay_w
        pop de
        ld a, d
        ld e, a
        ld d, 1                 ; reg1 = period high
        call ay_w
        ret

note_tab:
        dw 504, 423, 377, 336, 283, 252, 212, 168
; set_music_zone: pick the per-zone AY melody (capped to the table)
set_music_zone:
        ld a, (world)
        cp 6
        jr c, smz_ok
        ld a, 5
smz_ok:
        add a, a
        ld e, a
        ld d, 0
        ld hl, music_tab
        add hl, de
        ld a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        ld (music_ptr), hl
        ret
music_tab:
        dw music0, music1, music2, music3, music4, music5
music0: db 0,2,4,5, 4,2,0,2, 1,3,5,6, 5,3,1,3
music1: db 1,3,4,6, 5,4,3,1, 0,2,4,2, 3,5,4,2
music2: db 5,4,3,2, 1,2,3,4, 5,6,7,6, 5,3,1,0
music3: db 0,0,3,3, 5,5,4,2, 1,1,4,4, 6,6,5,3
music4: db 2,4,6,7, 6,4,2,0, 3,5,7,5, 3,1,3,5
music5: db 7,5,3,1, 0,2,4,6, 7,5,4,2, 1,3,5,7
music_a:
        db 0,2,4,5, 4,2,0,2, 1,3,5,6, 5,3,1,3

; ============================================================================
;  SPAWNING
; ============================================================================
; do_spawn: bonus stage drops only crystals; otherwise a looping wave
; script defines designed formations (type, y, delay), denser each zone.
do_spawn:
        ld a, (boss_active)     ; no spawns during a boss fight
        or a
        ret nz
        ld a, (bonus_timer)
        or a
        jr z, ds_wave
        ld a, (spawn_timer)     ; ----- bonus: crystals only -----
        dec a
        ld (spawn_timer), a
        ret nz
        ld a, 18
        ld (spawn_timer), a
        call sp_find_slot
        ret c
        ld a, T_CRYS
        ld (ix+0), a
        ld a, 1
        ld (ix+5), a
        call sp_setpos
        ret
ds_wave:
        ld a, (wave_delay)
        dec a
        ld (wave_delay), a
        ret nz
        ld hl, (wave_ptr)
        ld a, (hl)              ; type (0xFF = loop)
        cp 0xFF
        jr nz, dw_ok
        ld hl, (wave_base)      ; loop the current zone's script
        ld a, (hl)
dw_ok:
        ld (sp_type), a
        inc hl
        ld a, (hl)              ; y
        ld (sp_yv), a
        inc hl
        ld a, (hl)              ; delay to next
        inc hl
        ld (wave_ptr), hl
        ld b, a                 ; per-zone density: delay - world*4 (floor 8)
        ld a, (world)
        add a, a
        add a, a
        ld c, a
        ld a, b
        sub c
        cp 8
        jr nc, dw_setd
        ld a, 8
dw_setd:
        ld (wave_delay), a
        call sp_find_slot
        ret c
        ld a, (sp_type)
        ld (ix+0), a
        cp T_ENEMY              ; fast movers: enemies, divers, drones
        jr z, dw_fast
        cp T_DIVER
        jr z, dw_fast
        cp T_DRONE
        jr z, dw_fast
        ld a, 1
        jr dw_spds
dw_fast:
        ld a, 2
dw_spds:
        ld (ix+5), a
        ld a, (sp_yv)
        ld (ix+2), a
        ld (ix+4), a
        ld (ix+7), a
        ld a, 232
        ld (ix+1), a
        ld (ix+3), a
        xor a
        ld (ix+6), a
        ld a, (sp_type)         ; mini-boss: slow, hp in +7
        cp T_MIDBOSS
        ret nz
        ld a, 1
        ld (ix+5), a
        ld a, 8
        ld (ix+7), a
        ret

; sp_find_slot: free object slot -> IX; carry set if none free
sp_find_slot:
        ld ix, objs
        ld b, MAXOBJ
sfs_loop:
        ld a, (ix+0)
        or a
        jr z, sfs_found
        ld de, OBJSZ
        add ix, de
        djnz sfs_loop
        scf
        ret
sfs_found:
        or a
        ret

; sp_setpos: x=232, random y, phase 0 (used by the bonus stage)
sp_setpos:
        ld a, 232
        ld (ix+1), a
        ld (ix+3), a
        call rnd
        and 0x6F
        add a, 24
        ld (ix+2), a
        ld (ix+4), a
        ld (ix+7), a
        xor a
        ld (ix+6), a
        ret

; designed wave formations (type, y, frames-to-next); 0xFF = loop
; per-zone wave scripts (type, y, frames-to-next); 0xFF loops the zone
wave_tab:
        dw wave0, wave1, wave2, wave3, wave4, wave5
; Zone 1 - gentle: rocks, a few weavers, a power-up
wave0:
        db T_ROCK,  40, 26
        db T_ROCK,  72, 26
        db T_CRYS,  56, 30
        db T_ENEMY, 48, 22
        db T_ENEMY, 80, 22
        db T_ROCK, 100, 26
        db T_POWER, 70, 40
        db T_ENEMY, 64, 20
        db T_CRYS,  48, 28
        db T_ROCK,  84, 24
        db 0xFF
; Zone 2 - divers appear
wave1:
        db T_ROCK,  44, 24
        db T_DIVER, 30, 24
        db T_ENEMY, 80, 22
        db T_CRYS,  60, 28
        db T_DIVER, 36, 22
        db T_ENEMY, 96, 22
        db T_ROCK,  64, 22
        db T_POWER, 50, 40
        db T_DIVER, 28, 20
        db T_ENEMY, 72, 20
        db 0xFF
; Zone 3 - mines + drones
wave2:
        db T_MINE,  48, 26
        db T_DRONE, 40, 24
        db T_ENEMY, 88, 20
        db T_MINE,  72, 24
        db T_CRYS,  56, 26
        db T_DRONE, 64, 22
        db T_DIVER, 32, 20
        db T_POWER, 60, 40
        db T_MINE,  96, 22
        db T_ENEMY, 80, 18
        db 0xFF
; Zone 4 - a mini-boss + mixed swarm
wave3:
        db T_TURRET,28, 24
        db T_TURRET,140,24
        db T_DRONE, 50, 20
        db T_MIDBOSS,70, 60
        db T_DIVER, 36, 20
        db T_MINE,  90, 20
        db T_CRYS,  56, 24
        db T_ENEMY, 80, 18
        db T_DRONE, 44, 18
        db T_POWER, 64, 40
        db 0xFF
; Zone 5 - dense divers/drones
wave4:
        db T_DIVER, 30, 18
        db T_DRONE, 60, 18
        db T_MINE,  96, 20
        db T_DIVER, 40, 16
        db T_ENEMY, 84, 16
        db T_MIDBOSS,64, 56
        db T_DRONE, 50, 16
        db T_CRYS,  56, 22
        db T_DIVER, 34, 16
        db T_POWER, 60, 40
        db 0xFF
; Zone 6 - everything, fast
wave5:
        db T_TURRET,28, 18
        db T_DIVER, 30, 14
        db T_DRONE, 70, 14
        db T_MINE,  90, 16
        db T_MIDBOSS,60, 50
        db T_DIVER, 40, 14
        db T_ENEMY, 96, 14
        db T_DRONE, 54, 14
        db T_TURRET,150,18
        db T_POWER, 60, 36
        db 0xFF

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
        call rnd                ; depth layer: speed 1 (far) .. 3 (near)
        and 3
        jr nz, is_spd
        ld a, 3
is_spd:
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
        ld a, (ix+3)            ; near star? erase its 2nd pixel too
        cp 3
        jr c, ds_e1
        ld a, (ix+1)
        ld b, a
        ld a, (ix+2)
        inc a
        ld c, a
        call clr_pixel
ds_e1:
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
        ld a, (ix+3)            ; near star = 2px dash (brighter)
        cp 3
        jr c, ds_d1
        ld a, (ix+1)
        ld b, a
        ld a, (ix+0)
        inc a
        ld c, a
        call set_pixel
ds_d1:
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
;  SCROLLING TERRAIN  -  cave ceiling (row 2) and floor (row 23)
;  Character-cell scroll: every 4th frame shift each strip left one cell
;  and feed a fresh tile at the right.
; ============================================================================
; ============================================================================
;  CAVE  -  variable-height ceiling/floor heightmap (CMAX cells each side)
;  Walls live in the outer cell rows (2..2+CMAX-1 and 24-CMAX..23); the open
;  middle is where hazards spawn.  The ship may fly into a wall and crash.
;  The map scrolls one cell every 4 frames; the walls are repainted each frame.
; ============================================================================
CMAX    equ 5

scroll_terrain:
        ld a, (terr_div)
        inc a
        and 3
        ld (terr_div), a
        jr nz, draw_cave        ; only shift the map every 4th frame
        ; shift ceil_h/floor_h left by one cell
        ld hl, ceil_h+1
        ld de, ceil_h
        ld bc, 31
        ldir
        ld hl, floor_h+1
        ld de, floor_h
        ld bc, 31
        ldir
        ; generate a new right-most column via a bounded random walk
        ld a, (world)           ; later zones allow taller (tighter) walls
        add a, 2
        cp CMAX
        jr c, st_amp
        ld a, CMAX
st_amp:
        ld (cave_amp), a
        call rnd
        and 3
        sub 1                   ; -1..+2 step, biased to grow a little
        ld hl, cave_ct
        add a, (hl)
        call clamp_wall
        ld (cave_ct), a
        ld (ceil_h+31), a
        call rnd
        and 3
        sub 1
        ld hl, cave_ft
        add a, (hl)
        call clamp_wall
        ld (cave_ft), a
        ld (floor_h+31), a
        ; fall through to draw_cave

; draw_cave: repaint both wall zones from the heightmap (called every frame)
draw_cave:
        xor a
        ld (dc_c), a
dc_colloop:
        ld a, (dc_c)
        ld c, a
        ld b, 0
        ld hl, ceil_h
        add hl, bc
        ld a, (hl)
        ld (dc_ch), a
        ld hl, floor_h
        add hl, bc
        ld a, (hl)
        ld (dc_fh), a
        ; ceiling zone: cell rows 2..2+CMAX-1
        xor a
        ld (dc_k), a
dc_ceil:
        ld a, (dc_ch)           ; solid while k < ceil_h
        ld hl, dc_k
        cp (hl)
        ld a, 0xFF
        jr z, dc_cclr
        jr nc, dc_cdraw
dc_cclr:
        xor a
dc_cdraw:
        ld (dc_val), a
        ld a, (dc_k)
        add a, 2                ; cell row = 2 + k
        ld b, a
        ld a, (dc_c)
        ld c, a
        ld a, (dc_val)
        call cell_fill
        ld a, (dc_k)
        inc a
        ld (dc_k), a
        cp CMAX
        jr nz, dc_ceil
        ; floor zone: cell rows 24-CMAX..23
        xor a
        ld (dc_k), a
dc_floor:
        ld a, (dc_fh)           ; solid while k < floor_h (from the bottom up)
        ld hl, dc_k
        cp (hl)
        ld a, 0xFF
        jr z, dc_fclr
        jr nc, dc_fdraw
dc_fclr:
        xor a
dc_fdraw:
        ld (dc_val), a
        ld a, 23                ; cell row = 23 - k
        ld hl, dc_k
        sub (hl)
        ld b, a
        ld a, (dc_c)
        ld c, a
        ld a, (dc_val)
        call cell_fill
        ld a, (dc_k)
        inc a
        ld (dc_k), a
        cp CMAX
        jr nz, dc_floor
        ld a, (dc_c)
        inc a
        ld (dc_c), a
        cp 32
        jp nz, dc_colloop
        ret

; clamp_wall: clamp A to 1..cave_amp
clamp_wall:
        bit 7, a
        jr z, cw_lo
        ld a, 1
cw_lo:
        or a
        jr nz, cw_n0
        ld a, 1
cw_n0:
        ld b, a
        ld a, (cave_amp)
        cp b
        ld a, b
        ret nc
        ld a, (cave_amp)
        ret

; cell_fill: A = byte value, B = cell row, C = column ; fill the 8 pixel rows
cell_fill:
        ld (cf_val), a
        ld a, b
        add a, a
        add a, a
        add a, a                ; pixel row = cellrow*8
        ld (cf_pr), a
        ld b, 8
cf_lp:
        push bc
        ld a, (cf_pr)
        ld l, a
        ld h, 0
        add hl, hl
        ld de, addrtab
        add hl, de
        ld e, (hl)
        inc hl
        ld d, (hl)
        ld a, c
        ld l, a
        ld h, 0
        add hl, de
        ld a, (cf_val)
        ld (hl), a
        ld a, (cf_pr)
        inc a
        ld (cf_pr), a
        pop bc
        djnz cf_lp
        ret

; cave_collide: crash if the ship overlaps a wall at its centre column
cave_collide:
        ld a, (invuln)
        or a
        ret nz
        ld a, (ship_x)
        add a, 11
        srl a
        srl a
        srl a
        ld c, a
        ld b, 0
        ld hl, ceil_h
        add hl, bc
        ld a, (hl)              ; ceil_h
        add a, 2               ; ceiling bottom cell
        add a, a
        add a, a
        add a, a               ; *8 -> pixel
        ld b, a
        ld a, (ship_y)
        cp b
        jr c, cave_hit         ; ship top above ceiling bottom -> in ceiling
        ld a, (ship_x)
        add a, 11
        srl a
        srl a
        srl a
        ld c, a
        ld b, 0
        ld hl, floor_h
        add hl, bc
        ld a, (hl)             ; floor_h
        ld b, a
        ld a, 24
        sub b                  ; floor top cell
        add a, a
        add a, a
        add a, a               ; *8
        ld b, a
        ld a, (ship_y)
        add a, 16              ; ship bottom
        cp b
        ret c                  ; ship bottom above floor top -> safe
cave_hit:
        call ship_hit
        ret

; init_cave: flat, modest walls to start
init_cave:
        ld a, 2
        ld (cave_ct), a
        ld (cave_ft), a
        ld hl, ceil_h
        ld de, ceil_h+1
        ld bc, 31
        ld (hl), 2
        ldir
        ld hl, floor_h
        ld de, floor_h+1
        ld bc, 31
        ld (hl), 2
        ldir
        ret

; scroll_strip: (ss_base)=top pixel row, (ss_tile)->8 feed bytes
scroll_strip:
        ld a, (ss_base)
        ld (ss_row), a
        ld b, 8
ss_loop:
        push bc
        ld a, (ss_row)
        ld l, a
        ld h, 0
        add hl, hl
        ld de, addrtab
        add hl, de
        ld e, (hl)
        inc hl
        ld d, (hl)              ; DE = strip row address (col 0)
        ld h, d                 ; HL = addr+1 (source)
        ld l, e
        inc hl
        ld bc, 31
        ldir                    ; shift 31 bytes left; DE -> addr+31
        ld hl, (ss_tile)
        ld a, (hl)
        ld (de), a              ; feed new tile byte at the right edge
        inc hl
        ld (ss_tile), hl
        ld a, (ss_row)
        inc a
        ld (ss_row), a
        pop bc
        djnz ss_loop
        ret

; ============================================================================
;  WORLDS / ZONES
; ============================================================================
; worlds_tab entry: border, period, sky, mid, ground   (5 bytes)
; worlds_tab entry: border, period, then 6 band attributes (top->bottom).
; Papers are kept to black/blue/red/magenta (nebula hues) so the bright
; sprite inks stay readable; each band's ink tints that band's stars.
set_world_attr:
        ld a, (world)
        add a, a
        add a, a
        add a, a                ; world*8
        ld e, a
        ld d, 0
        ld hl, worlds_tab
        add hl, de
        ld a, (hl)              ; border
        ld (cur_border), a
        out (254), a
        inc hl
        ld a, (hl)              ; spawn period
        ld (spawn_period), a
        inc hl
        ld a, (hl)              ; first band = nominal zone_base
        ld (zone_base), a
        ld de, zb_band          ; copy the 6 band attributes
        ld bc, 6
        ldir
        ; build attr_row: rows 0-1 HUD, then 6 nebula bands over rows 2-23
        ld hl, attr_row
        ld (hl), 0x47
        inc hl
        ld (hl), 0x47
        inc hl
        ld ix, zb_band
        ld iy, bandcnt
        ld b, 6
swa_band:
        push bc
        ld a, (ix+0)
        ld c, a                 ; band attribute
        ld b, (iy+0)            ; rows in this band
swa_bfill:
        ld (hl), c
        inc hl
        djnz swa_bfill
        inc ix
        inc iy
        pop bc
        djnz swa_band
        ; paint the screen attributes from attr_row (32 cells per row)
        ld hl, ATTR
        ld ix, attr_row
        ld c, 24
swa_paintrow:
        ld a, (ix+0)
        ld b, 32
swa_paintcell:
        ld (hl), a
        inc hl
        djnz swa_paintcell
        inc ix
        dec c
        jr nz, swa_paintrow
        ret

bandcnt: db 4,4,4,3,3,4          ; rows per band over rows 2..23 (=22)
pw_coltab: db 5,6,7,4            ; power-up shimmer: cyan, yellow, white, green

next_world:
        ld a, (world)
        inc a
        cp NZONES
        jr c, nw_set
        xor a
nw_set:
        ld (world), a
        call set_world_attr
        call set_music_zone     ; switch the AY theme for this zone
        call sfx_zone
        xor a
        ld (midboss_flag), a
        call set_zone_wave      ; fresh per-zone formations
        ld hl, 300              ; short bonus stage between zones
        ld (bonus_timer), hl
        ld hl, 750
        ld (world_timer), hl
        ld a, 1                 ; show a ZONE CLEAR / intro flash
        ld (zone_msg), a
        ld a, 80
        ld (zone_msg_t), a
        ret

; set_zone_wave: point wave_base/wave_ptr at the current zone's script
set_zone_wave:
        ld a, (world)
        cp 6
        jr c, szw_ok
        ld a, 5
szw_ok:
        add a, a
        ld e, a
        ld d, 0
        ld hl, wave_tab
        add hl, de
        ld a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        ld (wave_base), hl
        ld (wave_ptr), hl
        ld a, 1
        ld (wave_delay), a
        ret

; ============================================================================
;  HUD / TEXT
; ============================================================================
show_hud:
        call show_score         ; 5 digits at row0 col0
        call draw_popup         ; "+N" gain by the score
        call draw_lives         ; ship icons, row0 right
        call draw_distbar       ; distance-to-boss bar, row0
        call draw_zonename      ; zone name, row1 left
        call draw_powers        ; active power-ups, row1 mid
        call draw_combo         ; combo multiplier, row1 right
        call draw_bombs         ; smart-bomb count, row1 right
        call draw_zone_banner   ; brief centered zone name on entry
        ret

; draw_zone_banner: show the new zone's name centred while zone_msg_t > 0
draw_zone_banner:
        ld a, (zone_msg_t)
        or a
        jr z, zb_chk
        dec a
        ld (zone_msg_t), a
        ld a, (world)
        add a, a
        ld e, a
        ld d, 0
        ld hl, zone_names
        add hl, de
        ld a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        ld b, 11
        ld c, 8
        call print_str_at
        ret
zb_chk:
        ld a, (zone_msg)
        or a
        ret z
        xor a
        ld (zone_msg), a
        ld b, 11                ; blank the banner row once
        ld c, 0
zb_bl:
        ld a, 32
        push bc
        call print_char
        pop bc
        inc c
        ld a, c
        cp 32
        jr nz, zb_bl
        ret

; draw_combo: "x<n>" at row1 col22 (only when a chain is active)
draw_combo:
        ld a, (combo_mult)
        ld hl, hud_combo
        cp (hl)
        ret z
        ld (hl), a
        ld b, 1
        ld c, 22
        ld a, 'x'
        push bc
        call print_char
        pop bc
        inc c
        ld a, (combo_mult)
        add a, '0'
        call print_char
        ret

; draw_bombs: "B<n>" at row1 col25
draw_bombs:
        ld a, (bombs)
        ld hl, hud_bombs
        cp (hl)
        ret z
        ld (hl), a
        ld b, 1
        ld c, 25
        ld a, 'B'
        push bc
        call print_char
        pop bc
        inc c
        ld a, (bombs)
        add a, '0'
        call print_char
        ret

; ---- HUD helpers ----
; draw_glyph: B=row C=col, (dg_ptr)->8 bytes  (copies a cell)
draw_glyph:
        ld a, b
        add a, a
        add a, a
        add a, a
        ld (dg_row), a
        ld a, c
        ld (dg_col), a
        ld b, 8
dg_loop:
        push bc
        ld a, (dg_row)
        ld l, a
        ld h, 0
        add hl, hl
        ld de, addrtab
        add hl, de
        ld e, (hl)
        inc hl
        ld d, (hl)
        ld a, (dg_col)
        ld l, a
        ld h, 0
        add hl, de
        ld de, (dg_ptr)
        ld a, (de)
        ld (hl), a
        inc de
        ld (dg_ptr), de
        ld a, (dg_row)
        inc a
        ld (dg_row), a
        pop bc
        djnz dg_loop
        ret

draw_lives:
        ld a, (lives)           ; only redraw when it changed
        ld hl, hud_lives
        cp (hl)
        ret z
        ld (hl), a
        ld b, 0                 ; blank cols 27..31
        ld c, 27
dl_blank:
        ld a, 32
        push bc
        call print_char
        pop bc
        inc c
        ld a, c
        cp 32
        jr nz, dl_blank
        ld a, (lives)
        cp 6
        jr c, dl_cap
        ld a, 5
dl_cap:
        or a
        ret z
        ld d, a                 ; count
        ld c, 27
dl_icon:
        ld hl, life_icon
        ld (dg_ptr), hl
        ld b, 0
        push de
        push bc
        call draw_glyph
        pop bc
        pop de
        inc c
        dec d
        jr nz, dl_icon
        ret

draw_distbar:
        ld a, (boss_active)     ; compute a one-byte signature
        or a
        jr z, db_calc
        ld a, 0xFF              ; boss marker
        jr db_cmp
db_calc:
        ld hl, (world_timer)
        add hl, hl
        ld a, h                 ; world_timer / 128
        cp 7
        jr c, db_cmp
        ld a, 6
db_cmp:
        ld hl, hud_bar
        cp (hl)
        ret z
        ld (hl), a
        cp 0xFF
        jr nz, db_bar
        ld hl, str_boss
        ld b, 0
        ld c, 20
        call print_str_at
        ret
db_bar:
        ld d, a                 ; filled cells
        ld e, 6                 ; total
        ld c, 20
db_cell:
        ld a, d
        or a
        jr z, db_empty
        dec d
        ld hl, bar_full
        jr db_put
db_empty:
        ld hl, bar_empty
db_put:
        ld (dg_ptr), hl
        ld b, 0
        push de
        push bc
        call draw_glyph
        pop bc
        pop de
        inc c
        dec e
        jr nz, db_cell
        ret

draw_zonename:
        ld hl, (bonus_timer)    ; signature: 0xFF in bonus, else world
        ld a, h
        or l
        jr z, dz_sigw
        ld a, 0xFF
        jr dz_sigc
dz_sigw:
        ld a, (world)
dz_sigc:
        ld hl, hud_zone
        cp (hl)
        ret z
        ld (hl), a
        ld b, 1                 ; blank cols 0..13
        ld c, 0
dz_blank:
        ld a, 32
        push bc
        call print_char
        pop bc
        inc c
        ld a, c
        cp 17
        jr nz, dz_blank
        ld hl, (bonus_timer)    ; bonus stage label?
        ld a, h
        or l
        jr z, dz_zone
        ld hl, str_bonus
        ld b, 1
        ld c, 0
        call print_str_at
        ret
dz_zone:
        ld a, (world)
        add a, a
        ld e, a
        ld d, 0
        ld hl, zone_names
        add hl, de
        ld a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        ld b, 1
        ld c, 0
        call print_str_at
        ret

; show one power-up letter if its flag is set, advancing the column
; (helper used by draw_powers): A=flag value, used with (dp_col),(dp_chr)
draw_powers:
        ld c, 0                 ; build a signature of active power-ups
        ld a, (pw_twin)
        or a
        jr z, dpw1
        set 0, c
dpw1:
        ld a, (pw_rapid)
        or a
        jr z, dpw2
        set 1, c
dpw2:
        ld a, (pw_speed)
        or a
        jr z, dpw3
        set 2, c
dpw3:
        ld a, (pw_shield)
        or a
        jr z, dpw4
        set 3, c
dpw4:
        ld a, c
        ld hl, hud_pow
        cp (hl)
        ret z
        ld (hl), a
        ld b, 1                 ; blank cols 15..20
        ld c, 15
dp_blank:
        ld a, 32
        push bc
        call print_char
        pop bc
        inc c
        ld a, c
        cp 21
        jr nz, dp_blank
        ld a, 15
        ld (dp_col), a
        ld a, (pw_twin)
        or a
        jr z, dp_r
        ld a, 'T'
        call dp_emit
dp_r:
        ld a, (pw_rapid)
        or a
        jr z, dp_f
        ld a, 'R'
        call dp_emit
dp_f:
        ld a, (pw_speed)
        or a
        jr z, dp_s
        ld a, 'F'
        call dp_emit
dp_s:
        ld a, (pw_shield)
        or a
        ret z
        ld a, 'S'
        call dp_emit
        ret
dp_emit:
        ld c, a                 ; char
        ld a, (dp_col)
        ld e, a
        ld a, c
        ld b, 1
        ld c, e
        push bc
        call print_char
        pop bc
        ld a, (dp_col)
        inc a
        ld (dp_col), a
        ret

; set_popup: show "+N" near the score; HL -> 0-terminated string
set_popup:
        ld (pop_str), hl
        ld a, 24
        ld (poptimer), a
        ret

draw_popup:
        ld a, (poptimer)
        or a
        jr z, dpop_chk
        dec a
        ld (poptimer), a
        ld a, 1
        ld (pop_shown), a
        ld hl, (pop_str)
        ld b, 0
        ld c, 6
        call print_str_at
        ret
dpop_chk:
        ld a, (pop_shown)       ; only blank once, when it expires
        or a
        ret z
        xor a
        ld (pop_shown), a
dpop_blank:
        ld b, 0                 ; clear the popup cells
        ld c, 6
dpop_b:
        ld a, 32
        push bc
        call print_char
        pop bc
        inc c
        ld a, c
        cp 11
        jr nz, dpop_b
        ret

show_score:
        ld hl, (score)          ; only redraw when the score changed
        ld de, (hud_score)
        or a
        sbc hl, de
        ret z
        ld hl, (score)
        ld (hud_score), hl
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
        ld c, 0
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
        cp 5
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

; add_kill_score: BC = base points; multiplied by the current combo
; multiplier, then a hit extends the chain.
add_kill_score:
        push bc
        call bump_combo
        pop bc
        ld a, (combo_mult)
ks_loop:
        dec a
        jr z, ks_done
        push af
        ld hl, (kill_acc)
        add hl, bc
        ld (kill_acc), hl       ; accumulate (mult-1) extra copies
        pop af
        jr ks_loop
ks_done:
        ld hl, (kill_acc)
        add hl, bc              ; base copy
        ld b, h
        ld c, l
        ld hl, 0
        ld (kill_acc), hl
        jr add_score

; bump_combo: extend the chain, refresh its timer, recompute the multiplier
bump_combo:
        ld a, (combo)
        cp 60
        jr nc, bc_cap
        inc a
        ld (combo), a
bc_cap:
        ld a, 100
        ld (combo_timer), a
        ld a, (combo)           ; mult = 1 + combo/3, capped at 6
        ld b, 1                 ; B = multiplier accumulator
bc_m:
        cp 3
        jr c, bc_store
        sub 3
        inc b
        ld c, a                 ; preserve remaining count
        ld a, b
        cp 6
        jr nc, bc_capmult
        ld a, c
        jr bc_m
bc_capmult:
        ld b, 6
bc_store:
        ld a, b
        ld (combo_mult), a
        ret

; reset_combo: drop the chain (called on a ship hit)
reset_combo:
        xor a
        ld (combo), a
        ld (combo_timer), a
        ld a, 1
        ld (combo_mult), a
        ret

; tick_combo: per-frame chain decay
tick_combo:
        ld a, (combo_timer)
        or a
        ret z
        dec a
        ld (combo_timer), a
        ret nz
        jp reset_combo

add_score:
        ld hl, (score)
        add hl, bc
        ld (score), hl
        ; extra life every 1000 points
        ld de, (next_life)
        or a
        sbc hl, de
        ret c
        ld a, (lives)
        inc a
        ld (lives), a
        ld hl, (next_life)
        ld de, 1000
        add hl, de
        ld (next_life), hl
        ld hl, str_1up
        call set_popup
        call sfx_power
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
        push hl
        ld e, a
        ld d, 0
        ld hl, maskt
        add hl, de
        ld a, (hl)              ; bit mask from table
        pop hl
        ret
maskt:  db 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01

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
;  PRE-SHIFTED SPRITES  -  the 16x16 blitter's shift loop, done once at
;  startup into psbuf, so drawing is a plain masked copy (much faster).
; ============================================================================
build_preshift:
        ld hl, psbuf
        ld (ps_dst), hl
        xor a
        ld (ps_si), a
bp_si:
        ld a, (ps_si)           ; src = sprtab[ps_si]
        add a, a
        ld e, a
        ld d, 0
        ld hl, sprtab
        add hl, de
        ld a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        ld (ps_srcbase), hl
        xor a
        ld (ps_sh), a
bp_sh:
        ld hl, (ps_srcbase)
        ld (ps_src), hl
        ld b, 16
bp_row:
        push bc
        ld hl, (ps_src)
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
        ld (ps_src), hl
        xor a
        ld (shbuf+2), a
        ld a, 255
        ld (shbuf+5), a
        ld a, (ps_sh)
        or a
        jr z, bp_noshift
        ld c, a
bp_shloop:
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
        jr nz, bp_shloop
bp_noshift:
        ld hl, (ps_dst)
        ld a, (shbuf+0)
        ld (hl), a
        inc hl
        ld a, (shbuf+1)
        ld (hl), a
        inc hl
        ld a, (shbuf+2)
        ld (hl), a
        inc hl
        ld a, (shbuf+3)
        ld (hl), a
        inc hl
        ld a, (shbuf+4)
        ld (hl), a
        inc hl
        ld a, (shbuf+5)
        ld (hl), a
        inc hl
        ld (ps_dst), hl
        pop bc
        dec b
        jp nz, bp_row
        ld a, (ps_sh)
        inc a
        ld (ps_sh), a
        cp 8
        jp nz, bp_sh
        ld a, (ps_si)
        inc a
        ld (ps_si), a
        cp NSPR
        jp nz, bp_si
        ret

; draw_sprite_ps: spr_idx, spr_x, spr_y ; fast masked draw from psbuf
draw_sprite_ps:
        push ix
        ld a, (spr_idx)         ; base = psbuf + idx*768 + (x&7)*96
        ld b, a
        add a, a
        add a, b                ; idx*3
        ld h, a
        ld l, 0                 ; idx*768
        ld de, psbuf
        add hl, de
        ld a, (spr_x)
        and 7
        jr z, dps_base
        ld b, a
        ld de, 96
dps_sh:
        add hl, de
        djnz dps_sh
dps_base:
        push hl
        pop ix
        ld a, (spr_x)
        rrca
        rrca
        rrca
        and 0x1F
        ld (xc_tmp), a
        ld a, (spr_y)
        ld (row_y), a
        ld b, 16
dps_row:
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
        add hl, de              ; HL = screen address
        ld a, (ix+3)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (ix+0)
        or c
        ld (hl), a
        inc hl
        ld a, (ix+4)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (ix+1)
        or c
        ld (hl), a
        inc hl
        ld a, (ix+5)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (ix+2)
        or c
        ld (hl), a
        ld de, 6
        add ix, de
        ld a, (row_y)
        inc a
        ld (row_y), a
        dec b
        jp nz, dps_row
        pop ix
        ret

; ============================================================================
;  WIDE (24x16) MASKED BLITTER  -  player ship only
;  spr_ptr -> 16 rows of (d0,d1,d2,m0,m1,m2); writes 4 bytes/row.
; ============================================================================
draw_ship:
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
dsh_row:
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
        ld (shbuf2+0), a
        inc hl
        ld a, (hl)
        ld (shbuf2+1), a
        inc hl
        ld a, (hl)
        ld (shbuf2+2), a
        inc hl
        ld a, (hl)
        ld (shbuf2+4), a
        inc hl
        ld a, (hl)
        ld (shbuf2+5), a
        inc hl
        ld a, (hl)
        ld (shbuf2+6), a
        inc hl
        ld (spr_ptr), hl
        xor a
        ld (shbuf2+3), a        ; data byte 3 = 0
        ld a, 255
        ld (shbuf2+7), a        ; mask byte 3 = 255
        ld a, (shift_n)
        or a
        jr z, dsh_noshift
        ld c, a
dsh_shloop:
        ld hl, shbuf2
        srl (hl)
        inc hl
        rr (hl)
        inc hl
        rr (hl)
        inc hl
        rr (hl)
        ld hl, shbuf2+4
        scf
        rr (hl)
        inc hl
        rr (hl)
        inc hl
        rr (hl)
        inc hl
        rr (hl)
        dec c
        jr nz, dsh_shloop
dsh_noshift:
        ld hl, (scr_addr)
        ld a, (shbuf2+4)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (shbuf2+0)
        or c
        ld (hl), a
        inc hl
        ld a, (shbuf2+5)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (shbuf2+1)
        or c
        ld (hl), a
        inc hl
        ld a, (shbuf2+6)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (shbuf2+2)
        or c
        ld (hl), a
        inc hl
        ld a, (shbuf2+7)
        ld c, a
        ld a, (hl)
        and c
        ld c, a
        ld a, (shbuf2+3)
        or c
        ld (hl), a
        ld a, (row_y)
        inc a
        ld (row_y), a
        pop bc
        dec b
        jp nz, dsh_row
        ret

erase_ship:
        ld a, (spr_x)
        rrca
        rrca
        rrca
        and 0x1F
        ld (xc_tmp), a
        ld a, (spr_y)
        ld (row_y), a
        ld b, 16
esh_row:
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
        inc hl
        ld (hl), a
        ld a, (row_y)
        inc a
        ld (row_y), a
        pop bc
        dec b
        jr nz, esh_row
        ret

; ============================================================================
;  DATA
; ============================================================================
        include "src/sprites.inc"

; worlds_tab entry: border, spawn-period, sky, mid, ground attributes
; (all attrs bright | paper | white ink; banded backdrop top->bottom)
; entry: border, spawn-period, then 6 band attributes (top->bottom).
; attr = 0x40(bright) | paper<<3 | ink.  The backdrop paper stays BLACK in
; every band (and the border too) so the screen is easy on the eyes and the
; bright object inks pop; each zone's identity comes from its star ink, which
; tints that band's stars (the only colour in the background).
worlds_tab:
        ; Zone 1  ORION DRIFT      black sky, cyan/white stars
        db 0, 48, 0x45,0x47,0x45,0x47,0x45,0x47
        ; Zone 2  CRIMSON VEIL     black sky, red/magenta stars
        db 0, 38, 0x42,0x43,0x42,0x43,0x42,0x43
        ; Zone 3  SAPPHIRE EXPANSE black sky, blue/cyan stars
        db 0, 30, 0x41,0x45,0x41,0x45,0x41,0x45
        ; Zone 4  MAGENTA STORM    black sky, magenta/white stars
        db 0, 24, 0x43,0x47,0x43,0x47,0x43,0x47
        ; Zone 5  EMERALD RIFT     black sky, green/yellow stars
        db 0, 22, 0x44,0x46,0x44,0x46,0x44,0x46
        ; Zone 6  VOID NEXUS       black sky, mixed bright stars
        db 0, 20, 0x47,0x45,0x43,0x46,0x42,0x47

str_title:  db "STELLAR DRIFT",0
str_fire:   db "PRESS FIRE",0
str_over:   db "GAME OVER",0
str_score:  db "SCORE",0
str_ships:  db "SHIPS",0
str_zone:   db "ZONE",0
str_hiscores: db "HIGH SCORES",0
str_ctrl:   db "QAOP/KEMPSTON   H-PAUSE",0
str_newhi:  db "NEW HIGH SCORE!",0
str_entini: db "ENTER INITIALS - FIRE",0
str_schopts: db "1-QAOP   2-CURSOR",0
str_qaop:   db "USING QAOP  ",0
str_cursor: db "USING CURSOR",0
str_shipsel: db "M-SHIP",0
str_diff:    db "3-SKILL:",0
str_d0:      db "CADET ",0
str_d1:      db "PILOT ",0
str_d2:      db "ACE   ",0
str_opts1:   db "4-MUSIC 5-FLASH 6-SAFE",0
str_on:      db "ON ",0
str_off:     db "OFF",0
diff_tab:    dw str_d0, str_d1, str_d2
credits_msg: db "STELLAR DRIFT - A CAVE FLYER FOR THE HC-91 - DODGE, "
             db "SHOOT, COLLECT - BEAT THE ZONE BOSSES - GOOD LUCK PILOT     ",0

hs_def_names:  db "ACE","ZAP","HC9","FOX","BEE"
hs_def_scores: dw 500,400,300,200,100

hs_names:   defs 15
hs_scores:  defs 10
ie_buf:     defs 3
ie_pos:     defb 0
ie_letter:  defb 0
ie_prevud:  defb 0
ie_prevfire: defb 0
hst_row:    defb 0

state:        defb 0
lives:        defb 0
world:        defb 0
invuln:       defb 0
fire_prev:    defb 0
fire_cd:      defb 0
menu_lock:    defb 0
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
cur_border:   defb 0
zone_base:    defb 0x47
zb_sky:       defb 0x47
zb_mid:       defb 0x47
zb_gnd:       defb 0x47
zb_band:      defs 6
attr_row:     defs 24
sab_attr:     defb 0
sab_keep:     defb 0xF8
sab_or:       defb 0
sab_r:        defb 0
sab_w:        defb 3
shbuf2:       defb 0,0,0,0,0,0,0,0

; gameplay state added for the roadmap
ecount:       defb 0
xcount:       defb 0
eb_vy:        defb 0
sb_yoff:      defb 0
obj_ink:      defb 0
col_thr:      defb 13
ship_step:    defb 2
want_x:       defb 0
want_y:       defb 0
vxb:          defb 8
vyb:          defb 8
spawn_count:  defb 0
drone_ctr:    defb 0
pw_twin:      defb 0
pw_rapid:     defb 0
pw_shield:    defb 0
pw_speed:     defb 0
pw_next:      defb 0
boss_active:  defb 0
boss_hp:      defb 0
boss_hp_max:  defb 16
boss_spr:     defw 0
midboss_flag: defb 0
shake:        defb 0
anim_ctr:     defb 0
terr_div:     defb 0
terr_tile:    defb 0
ceil_h:       defs 32
floor_h:      defs 32
cave_ct:      defb 2
cave_ft:      defb 2
cave_amp:     defb 4
dc_c:         defb 0
dc_ch:        defb 0
dc_fh:        defb 0
dc_k:         defb 0
dc_val:       defb 0
cf_val:       defb 0
cf_pr:        defb 0
ss_base:      defb 0
ss_row:       defb 0
ss_tile:      defw 0
dg_row:       defb 0
dg_col:       defb 0
dg_ptr:       defw 0
dp_col:       defb 0
poptimer:     defb 0
pop_str:      defw 0
pop_shown:    defb 0
hud_score:    defw 0xFFFF
hud_lives:    defb 0xFF
hud_bar:      defb 0xFE
hud_zone:     defb 0xFE
hud_pow:      defb 0xFF
paused:       defb 0
pause_prev:   defb 0
ctrl_scheme:  defb 0
; ---- options & Phase-2 state ----
opt_softblink: defb 1         ; 1 = pulse ship colour while invuln; 0 = hide
opt_music:    defb 1          ; 1 = AY melody on
opt_shake:    defb 1          ; 1 = full border flash; 0 = soft (photosensitive)
opt_practice: defb 0          ; 1 = practice mode (no life loss)
difficulty:   defb 1          ; 0 Cadet, 1 Pilot, 2 Ace
combo:        defb 0          ; current chain length
combo_mult:   defb 1          ; score multiplier (1..)
combo_timer:  defb 0          ; frames left before the chain resets
kill_acc:     defw 0          ; scratch accumulator for add_kill_score
hud_combo:    defb 0xFF
bombs:        defb 3          ; smart-bombs in reserve
hud_bombs:    defb 0xFF
bomb_prev:    defb 0
bomb_flash:   defb 0
charge:       defb 0          ; fire-charge counter while held
fire_held:    defb 0
bullet_type:  defb 1          ; 1 normal, 2 charged/piercing
graze_prev:   defb 0
flame_ox:     defb 0
flame_oy:     defb 0
planet_x:     defb 0
planet_y:     defb 0
planet_ox:    defb 0
planet_oy:    defb 0
opt_prev:     defb 0
demo_active:  defb 0
demo_idx:     defw 0
demo_timer:   defb 0
idle_ctr:     defw 0
intro_done:   defb 0
ship_choice:  defb 0
m_prev:       defb 0
tctr:         defb 0
credit_idx:   defb 0

; 6 designs x 3 banks (level, climb, dive)
ship_tab:
        dw spr_ship0, spr_ship0_up, spr_ship0_dn
        dw spr_ship1, spr_ship1_up, spr_ship1_dn
        dw spr_ship2, spr_ship2_up, spr_ship2_dn
        dw spr_ship3, spr_ship3_up, spr_ship3_dn
        dw spr_ship4, spr_ship4_up, spr_ship4_dn
        dw spr_ship5, spr_ship5_up, spr_ship5_dn
ship_bank:    defb 0
boss_tab:
        dw spr_boss, spr_boss1, spr_boss2, spr_boss3
wave_ptr:     defw 0
wave_base:    defw wave0
music_ptr:    defw music0
zone_msg:     defb 0
zone_msg_t:   defb 0
wave_delay:   defb 1
sp_type:      defb 0
sp_yv:        defb 0
bonus_timer:  defw 0
next_life:    defw 1000
mus_div:      defb 0
mus_idx:      defb 0
ay_noise_t:   defb 0
tick:         defb 0
cheat_kprev:  defb 0

zone_names:   dw zn0, zn1, zn2, zn3, zn4, zn5
zn0:          db "ORION DRIFT",0
zn1:          db "CRIMSON VEIL",0
zn2:          db "SAPPHIRE EXPANSE",0
zn3:          db "MAGENTA STORM",0
zn4:          db "EMERALD RIFT",0
zn5:          db "VOID NEXUS",0
str_boss:     db "BOSS!!",0
str_pause:    db "PAUSED",0
str_p10:      db "+10",0
str_p5:       db "+5",0
str_p200:     db "+200",0
str_p50:      db "+50",0
str_midboss:  db "WARSHIP!",0
str_pwr:      db "PWR!",0
str_1up:      db "1UP!",0
str_bonus:    db "BONUS STAGE!",0

life_icon:    db 0,48,60,255,255,60,48,0
bar_full:     db 0,0,0,255,255,0,0,0
bar_empty:    db 0,0,0,24,24,0,0,0

; cave ceiling tiles (4 x 8 bytes; top pixel first, solid at top)
ceil_tiles:
        db 255,255,255,126, 60, 24,  0,  0
        db 255,255,255,255,255,126, 60, 24
        db 255,255,126, 60, 24,  0,  0,  0
        db 255,255,255,219,126, 60, 24,  0
; cave floor tiles (4 x 8 bytes; solid at bottom)
floor_tiles:
        db   0,  0, 24, 60,126,255,255,255
        db  24, 60,126,255,255,255,255,255
        db   0,  0,  0, 24, 60,126,255,255
        db   0, 24, 60,126,219,255,255,255

; sine table: 32 entries, 0..24 (centre 12), one full period
sintab:
        db 12,14,17,19,21,22,23,24,24,24,23,22,21,19,17,14
        db 12,10, 7, 5, 3, 2, 1, 0, 0, 0, 1, 2, 3, 5, 7,10

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

sprtab:
        dw spr_rock, spr_rock2, spr_enemy, spr_crystal, spr_crystal2
        dw spr_power, spr_bullet, spr_ebullet, spr_expl1, spr_expl2, spr_expl3
        dw spr_diver, spr_turret, spr_mine, spr_drone, spr_planet, spr_spark
        dw spr_flame1, spr_flame2

spr_idx:  defb 0
ps_dst:   defw 0
ps_src:   defw 0
ps_srcbase: defw 0
ps_si:    defb 0
ps_sh:    defb 0

objs:     defs MAXOBJ*OBJSZ
bullets:  defs MAXBUL*4
ebullets: defs MAXEB*6
expls:    defs MAXEXPL*3
debris:   defs DEB*5
stars:    defs NSTAR*4
px_x:     defb 0
px_y:     defb 0
px_set:   defb 0

addrtab:  defs 384
psbuf:    defs NSPR*768

        end start
