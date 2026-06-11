/* ay.c — AY-3-8912 PSG (HC-128). Register file + mono synthesis.
 *
 * The chip runs at 1.7734 MHz; tone/noise/envelope prescale is /16, so
 * generators tick at 110.84 kHz — ay_sample() advances ~2.513 ticks per
 * 44.1 kHz output sample via a fractional accumulator. Register writes
 * are sample-accurate (machine.c flushes the audio synthesis up to the
 * current T-state before applying one). Output is the sum of the three
 * channels through the classic measured 4-bit log volume curve.
 */
#include <string.h>
#include "machine.h"

/* Master tick = clock/8: the tone flip-flop then divides by 2, giving
 * the datasheet f = clock / (16 x period). */
#define AY_TICKS_PER_SAMPLE (1773400.0 / 8.0 / 44100.0)

/* Per-register writable-bit masks (read-back is masked accordingly). */
static const uint8_t reg_mask[16] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
    0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF
};

/* Normalized AY volume curve (Hacker KAY measurements), scaled to a
 * per-channel peak that keeps beeper+3ch inside 16-bit range. */
static const int16_t vol_tab[16] = {
    0, 33, 49, 71, 103, 150, 206, 333,
    411, 644, 858, 1094, 1387, 1671, 2063, 2432
};
#define CH_SCALE 2               /* per-channel peak ~4864 */

void ay_reset(Ay *ay)
{
    memset(ay, 0, sizeof *ay);
    ay->lfsr = 1;
    ay->reg[7] = 0xFF;           /* everything off */
}

void ay_select(Ay *ay, uint8_t v)
{
    ay->sel = (uint8_t)(v & 0x0F);
}

void ay_write(Ay *ay, uint8_t v)
{
    ay->reg[ay->sel] = (uint8_t)(v & reg_mask[ay->sel]);
    if (ay->sel == 13) {         /* envelope shape: restart generator */
        ay->env_cnt = 0;
        ay->env_pos = 0;
    }
}

uint8_t ay_read(const Ay *ay)
{
    if (ay->sel >= 14)
        return 0xFF;             /* I/O port: nothing attached */
    return ay->reg[ay->sel];
}

/* Envelope: 64-entry position; shapes repeat the second half (32..63)
 * unless HOLD/one-shot. Encoded per the CONT/ATT/ALT/HOLD bits. */
static uint8_t env_level(const Ay *ay)
{
    uint8_t shape = ay->reg[13];
    int pos = ay->env_pos;
    int seg = pos >> 5, off = pos & 31;     /* two 32-step halves */
    int attack = (shape & 4) != 0;
    int cont = (shape & 8) != 0;
    int alt = (shape & 2) != 0;
    int hold = (shape & 1) != 0;
    int up;

    if (!cont) {                 /* 0-7: one ramp, then stay at 0 */
        if (seg > 0)
            return 0;
        up = attack;
    } else if (seg == 0) {
        up = attack;
    } else if (hold) {
        /* held level after first ramp */
        return (uint8_t)((attack ^ alt) ? 15 : 0);
    } else {
        up = attack ^ (alt ? (seg & 1) : 0);
    }
    {
        int step = off >> 1;     /* 32 half-steps -> 16 levels */
        return (uint8_t)(up ? step : 15 - step);
    }
}

/* One generator tick at chip-clock/16. */
static void ay_tick(Ay *ay)
{
    int ch;
    uint32_t np = ay->reg[6] & 0x1F;
    uint32_t ep = (uint32_t)ay->reg[11] | ((uint32_t)ay->reg[12] << 8);

    for (ch = 0; ch < 3; ch++) {
        uint32_t period = (uint32_t)ay->reg[ch * 2]
                        | ((uint32_t)(ay->reg[ch * 2 + 1] & 0x0F) << 8);
        if (period == 0)
            period = 1;
        if (++ay->tone_cnt[ch] >= period) {
            ay->tone_cnt[ch] = 0;
            ay->tone_out[ch] ^= 1;
        }
    }

    if (np == 0)
        np = 1;
    if (++ay->noise_cnt >= np * 2) {        /* f = clock/(16 x NP) */
        ay->noise_cnt = 0;
        ay->noise_out = (int)(ay->lfsr & 1);
        ay->lfsr = (ay->lfsr >> 1)
                 ^ ((ay->lfsr & 1) ? 0x12000 : 0);   /* 17-bit taps 0,3 */
    }

    if (ep == 0)
        ep = 1;
    if (++ay->env_cnt >= ep * 2) {          /* step f = clock/(16 x EP) */
        ay->env_cnt = 0;
        if (ay->env_pos < 63)
            ay->env_pos++;
        else if ((ay->reg[13] & 8) && !(ay->reg[13] & 1))
            ay->env_pos = 0;    /* continuous: restart (ALT alternates) */
    }
    ay->env_vol = env_level(ay);
}

int ay_sample(Ay *ay)
{
    int out = 0, ch;
    ay->frac += AY_TICKS_PER_SAMPLE;
    while (ay->frac >= 1.0) {
        ay->frac -= 1.0;
        ay_tick(ay);
    }
    for (ch = 0; ch < 3; ch++) {
        int tdis = (ay->reg[7] >> ch) & 1;
        int ndis = (ay->reg[7] >> (ch + 3)) & 1;
        int on = (ay->tone_out[ch] | tdis) & (ay->noise_out | ndis);
        uint8_t v = ay->reg[8 + ch];
        uint8_t lvl = (v & 0x10) ? ay->env_vol : (v & 0x0F);
        int amp = vol_tab[lvl] * CH_SCALE;
        /* bipolar around 0 so the mix carries no DC offset */
        out += on ? amp / 2 : -amp / 2;
    }
    return out;
}
