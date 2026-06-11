/* z80.c — Z80 CPU core for the HC-91 emulator.
 *
 * Implements all documented and undocumented instructions, exact flag
 * behavior (including X/Y bits 3/5 and the Q register), MEMPTR (WZ),
 * R refresh semantics and per-M-cycle T-state accounting with optional
 * memory/IO contention callbacks (ULA contention patterns follow the
 * canonical "Contended memory" documentation: internal cycles are
 * charged 1T each at the address the hardware re-asserts).
 * Validated against zexdoc, zexall and Patrik Rak's z80full.
 */
#include "z80.h"

/* ------------------------------------------------------------------ */
/* Flag lookup tables                                                  */
/* ------------------------------------------------------------------ */
static uint8_t sz53_tab[256];
static uint8_t sz53p_tab[256];
static int tables_ready = 0;

static void init_tables(void)
{
    int i;
    if (tables_ready) return;
    for (i = 0; i < 256; i++) {
        uint8_t f = (uint8_t)(i & (ZF_S | ZF_X | ZF_Y));
        int p = i;
        if (i == 0) f |= ZF_Z;
        sz53_tab[i] = f;
        p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
        sz53p_tab[i] = (uint8_t)(f | ((p & 1) ? 0 : ZF_PV));
    }
    tables_ready = 1;
}

/* Convenience accessors */
#define rA  (z->af.b.h)
#define rF  (z->af.b.l)
#define rB  (z->bc.b.h)
#define rC  (z->bc.b.l)
#define rD  (z->de.b.h)
#define rE  (z->de.b.l)
#define rH  (z->hl.b.h)
#define rL  (z->hl.b.l)
#define wBC (z->bc.w)
#define wDE (z->de.w)
#define wHL (z->hl.w)
#define wSP (z->sp.w)
#define wPC (z->pc.w)
#define wMP (z->memptr.w)

/* I:R pair as a bus address (contention target of internal cycles) */
#define IRW ((uint16_t)(((uint16_t)z->i << 8) | z->r))

/* ------------------------------------------------------------------ */
/* Timed bus access helpers                                            */
/* ------------------------------------------------------------------ */
/* Charge a memory access cycle of `len` T at `addr` (contention first) */
static void ct(Z80 *z, uint16_t addr, int len)
{
    if (z->mem_contend) z->tstates += (uint64_t)z->mem_contend(z->ctx, addr);
    z->tstates += (uint64_t)len;
}

/* n internal 1T cycles, each individually contended at addr */
static void intern(Z80 *z, uint16_t addr, int n)
{
    while (n--) ct(z, addr, 1);
}

static uint8_t mrd(Z80 *z, uint16_t a)
{
    ct(z, a, 3);
    return z->mem_read(z->ctx, a);
}

static void mwr(Z80 *z, uint16_t a, uint8_t v)
{
    ct(z, a, 3);
    z->mem_write(z->ctx, a, v);
}

#define MRD(a)    mrd(z, (uint16_t)(a))
#define MWR(a,v)  mwr(z, (uint16_t)(a), (uint8_t)(v))

/* Full 4T I/O cycle with the documented contention pattern */
static uint8_t io_in(Z80 *z, uint16_t port)
{
    uint8_t v;
    if (z->io_contend_early)
        z->tstates += (uint64_t)z->io_contend_early(z->ctx, port);
    z->tstates += 1;
    if (z->io_contend_late)
        z->tstates += (uint64_t)z->io_contend_late(z->ctx, port);
    v = z->io_read(z->ctx, port);
    z->tstates += 3;
    return v;
}

static void io_out(Z80 *z, uint16_t port, uint8_t v)
{
    if (z->io_contend_early)
        z->tstates += (uint64_t)z->io_contend_early(z->ctx, port);
    z->tstates += 1;
    if (z->io_contend_late)
        z->tstates += (uint64_t)z->io_contend_late(z->ctx, port);
    z->io_write(z->ctx, port, v);
    z->tstates += 3;
}

#define IORD(p)   io_in(z, (uint16_t)(p))
#define IOWR(p,v) io_out(z, (uint16_t)(p), (uint8_t)(v))

/* ------------------------------------------------------------------ */
/* Fetch / stack helpers                                               */
/* ------------------------------------------------------------------ */
static void r_inc(Z80 *z)
{
    z->r = (uint8_t)((z->r & 0x80) | ((z->r + 1) & 0x7f));
}

static uint8_t fetch8(Z80 *z)            /* operand fetch: 3T */
{
    ct(z, wPC, 3);
    return z->mem_read(z->ctx, wPC++);
}

static uint8_t fetch_op(Z80 *z)          /* M1 fetch: 4T, bumps R */
{
    r_inc(z);
    ct(z, wPC, 4);
    return z->mem_read(z->ctx, wPC++);
}

static uint16_t fetch16(Z80 *z)
{
    uint16_t lo = fetch8(z);
    return (uint16_t)(lo | ((uint16_t)fetch8(z) << 8));
}

static void push16(Z80 *z, uint16_t v)
{
    MWR(--wSP, v >> 8);
    MWR(--wSP, v & 0xff);
}

static uint16_t pop16(Z80 *z)
{
    uint16_t lo = MRD(wSP++);
    return (uint16_t)(lo | ((uint16_t)MRD(wSP++) << 8));
}

static uint16_t rd16(Z80 *z, uint16_t a)
{
    uint16_t lo = MRD(a);
    return (uint16_t)(lo | ((uint16_t)MRD((uint16_t)(a + 1)) << 8));
}

static void wr16(Z80 *z, uint16_t a, uint16_t v)
{
    MWR(a, v & 0xff);
    MWR((uint16_t)(a + 1), v >> 8);
}

/* ------------------------------------------------------------------ */
/* Register selection                                                  */
/* ------------------------------------------------------------------ */
/* idx 0..7 = B C D E H L (HL) A ; pfx 0=none 1=IX 2=IY (H/L remapped).
 * Never called with idx==6. */
static uint8_t *r8(Z80 *z, int idx, int pfx)
{
    switch (idx) {
    case 0: return &z->bc.b.h;
    case 1: return &z->bc.b.l;
    case 2: return &z->de.b.h;
    case 3: return &z->de.b.l;
    case 4: return pfx == 1 ? &z->ix.b.h : pfx == 2 ? &z->iy.b.h : &z->hl.b.h;
    case 5: return pfx == 1 ? &z->ix.b.l : pfx == 2 ? &z->iy.b.l : &z->hl.b.l;
    default: return &z->af.b.h;
    }
}

static RegPair *rp(Z80 *z, int idx, int pfx)      /* BC DE HL SP */
{
    switch (idx) {
    case 0: return &z->bc;
    case 1: return &z->de;
    case 2: return pfx == 1 ? &z->ix : pfx == 2 ? &z->iy : &z->hl;
    default: return &z->sp;
    }
}

static RegPair *rp2(Z80 *z, int idx, int pfx)     /* BC DE HL AF */
{
    if (idx == 3) return &z->af;
    return rp(z, idx, pfx);
}

/* Effective address for (HL) / (IX+d) / (IY+d). Sets MEMPTR for indexed.
 * Indexed form charges the displacement fetch + 5 internal cycles at the
 * displacement byte's address (standard pattern pc+2:3, pc+2:1 x5). */
static uint16_t mem_ea(Z80 *z, int pfx)
{
    int8_t d;
    uint16_t a;
    if (pfx == 0) return wHL;
    d = (int8_t)fetch8(z);
    intern(z, (uint16_t)(wPC - 1), 5);
    a = (uint16_t)((pfx == 1 ? z->ix.w : z->iy.w) + d);
    wMP = a;
    return a;
}

/* ------------------------------------------------------------------ */
/* ALU helpers                                                         */
/* ------------------------------------------------------------------ */
static void alu_add8(Z80 *z, uint8_t v, uint8_t cy)
{
    uint16_t res = (uint16_t)(rA + v + cy);
    uint8_t  r = (uint8_t)res;
    rF = (uint8_t)(sz53_tab[r]
        | ((rA ^ v ^ r) & ZF_H)
        | ((((rA ^ ~v) & (rA ^ r)) & 0x80) >> 5)
        | ((res >> 8) & ZF_C));
    rA = r;
}

static void alu_sub8(Z80 *z, uint8_t v, uint8_t cy)
{
    uint16_t res = (uint16_t)(rA - v - cy);
    uint8_t  r = (uint8_t)res;
    rF = (uint8_t)(sz53_tab[r] | ZF_N
        | ((rA ^ v ^ r) & ZF_H)
        | ((((rA ^ v) & (rA ^ r)) & 0x80) >> 5)
        | ((res >> 8) & ZF_C));
    rA = r;
}

static void alu_cp(Z80 *z, uint8_t v)
{
    uint16_t res = (uint16_t)(rA - v);
    uint8_t  r = (uint8_t)res;
    rF = (uint8_t)((sz53_tab[r] & (ZF_S | ZF_Z)) | (v & (ZF_X | ZF_Y)) | ZF_N
        | ((rA ^ v ^ r) & ZF_H)
        | ((((rA ^ v) & (rA ^ r)) & 0x80) >> 5)
        | ((res >> 8) & ZF_C));
}

static void alu_op(Z80 *z, int op, uint8_t v)
{
    switch (op) {
    case 0: alu_add8(z, v, 0); break;                       /* ADD */
    case 1: alu_add8(z, v, rF & ZF_C); break;               /* ADC */
    case 2: alu_sub8(z, v, 0); break;                       /* SUB */
    case 3: alu_sub8(z, v, rF & ZF_C); break;               /* SBC */
    case 4: rA &= v; rF = (uint8_t)(sz53p_tab[rA] | ZF_H); break;
    case 5: rA ^= v; rF = sz53p_tab[rA]; break;
    case 6: rA |= v; rF = sz53p_tab[rA]; break;
    default: alu_cp(z, v); break;                           /* CP */
    }
}

static uint8_t inc8(Z80 *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    rF = (uint8_t)((rF & ZF_C) | sz53_tab[r]
        | (r == 0x80 ? ZF_PV : 0)
        | ((r & 0x0f) == 0 ? ZF_H : 0));
    return r;
}

static uint8_t dec8(Z80 *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    rF = (uint8_t)((rF & ZF_C) | ZF_N | sz53_tab[r]
        | (r == 0x7f ? ZF_PV : 0)
        | ((r & 0x0f) == 0x0f ? ZF_H : 0));
    return r;
}

static uint16_t add16(Z80 *z, uint16_t a, uint16_t b)
{
    uint32_t res = (uint32_t)a + b;
    wMP = (uint16_t)(a + 1);
    rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_PV))
        | (((a ^ b ^ res) >> 8) & ZF_H)
        | ((res >> 16) ? ZF_C : 0)
        | ((res >> 8) & (ZF_X | ZF_Y)));
    return (uint16_t)res;
}

static void adc16(Z80 *z, uint16_t v)
{
    uint32_t res = (uint32_t)wHL + v + (rF & ZF_C);
    uint16_t r = (uint16_t)res;
    wMP = (uint16_t)(wHL + 1);
    rF = (uint8_t)((((wHL ^ v ^ r) >> 8) & ZF_H)
        | ((res >> 16) ? ZF_C : 0)
        | ((r >> 8) & (ZF_S | ZF_X | ZF_Y))
        | (r == 0 ? ZF_Z : 0)
        | ((((wHL ^ ~v) & (wHL ^ r)) & 0x8000) >> 13));
    wHL = r;
}

static void sbc16(Z80 *z, uint16_t v)
{
    uint32_t res = (uint32_t)wHL - v - (rF & ZF_C);
    uint16_t r = (uint16_t)res;
    wMP = (uint16_t)(wHL + 1);
    rF = (uint8_t)(ZF_N
        | (((wHL ^ v ^ r) >> 8) & ZF_H)
        | ((res >> 16) ? ZF_C : 0)
        | ((r >> 8) & (ZF_S | ZF_X | ZF_Y))
        | (r == 0 ? ZF_Z : 0)
        | ((((wHL ^ v) & (wHL ^ r)) & 0x8000) >> 13));
    wHL = r;
}

/* CB rotate/shift group, op 0..7 = RLC RRC RL RR SLA SRA SLL SRL */
static uint8_t shift_op(Z80 *z, int op, uint8_t v)
{
    uint8_t c, r;
    switch (op) {
    case 0:  c = v >> 7;   r = (uint8_t)((v << 1) | c); break;
    case 1:  c = v & 1;    r = (uint8_t)((v >> 1) | (c << 7)); break;
    case 2:  c = v >> 7;   r = (uint8_t)((v << 1) | (rF & ZF_C)); break;
    case 3:  c = v & 1;    r = (uint8_t)((v >> 1) | ((rF & ZF_C) << 7)); break;
    case 4:  c = v >> 7;   r = (uint8_t)(v << 1); break;
    case 5:  c = v & 1;    r = (uint8_t)((v & 0x80) | (v >> 1)); break;
    case 6:  c = v >> 7;   r = (uint8_t)((v << 1) | 1); break;          /* SLL */
    default: c = v & 1;    r = (uint8_t)(v >> 1); break;
    }
    rF = (uint8_t)(sz53p_tab[r] | c);
    return r;
}

/* BIT test; xy = source byte for X/Y flags */
static void bit_op(Z80 *z, int n, uint8_t v, uint8_t xy)
{
    uint8_t res = (uint8_t)(v & (1 << n));
    rF = (uint8_t)((rF & ZF_C) | ZF_H
        | (res ? 0 : (ZF_Z | ZF_PV))
        | (res & ZF_S)
        | (xy & (ZF_X | ZF_Y)));
}

static int cond(Z80 *z, int cc)
{
    switch (cc) {
    case 0: return !(rF & ZF_Z);
    case 1: return  (rF & ZF_Z) != 0;
    case 2: return !(rF & ZF_C);
    case 3: return  (rF & ZF_C) != 0;
    case 4: return !(rF & ZF_PV);
    case 5: return  (rF & ZF_PV) != 0;
    case 6: return !(rF & ZF_S);
    default: return (rF & ZF_S) != 0;
    }
}

/* ------------------------------------------------------------------ */
/* CB-prefixed                                                         */
/* ------------------------------------------------------------------ */
static void do_cb(Z80 *z, int pfx)
{
    if (pfx == 0) {
        uint8_t op = fetch_op(z);
        int sel = op & 7, kind = op >> 6, n = (op >> 3) & 7;
        if (sel == 6) {
            uint8_t v = MRD(wHL);
            intern(z, wHL, 1);
            if (kind == 0)      MWR(wHL, shift_op(z, n, v));
            else if (kind == 1) bit_op(z, n, v, z->memptr.b.h);
            else if (kind == 2) MWR(wHL, (uint8_t)(v & ~(1 << n)));
            else                MWR(wHL, (uint8_t)(v | (1 << n)));
        } else {
            uint8_t *p = r8(z, sel, 0);
            if (kind == 0)      *p = shift_op(z, n, *p);
            else if (kind == 1) bit_op(z, n, *p, *p);
            else if (kind == 2) *p &= (uint8_t)~(1 << n);
            else                *p |= (uint8_t)(1 << n);
        }
    } else {
        /* DDCB / FDCB: DD CB d op — d and op are NOT M1 fetches.
         * Pattern: pc:4 pc+1:4 pc+2:3 pc+3:3 pc+3:1x2 ea:3 [ea:1, ea:3w] */
        int8_t d = (int8_t)fetch8(z);
        uint8_t op = fetch8(z);
        uint16_t a = (uint16_t)((pfx == 1 ? z->ix.w : z->iy.w) + d);
        int sel = op & 7, kind = op >> 6, n = (op >> 3) & 7;
        uint8_t v, res;
        intern(z, (uint16_t)(wPC - 1), 2);
        wMP = a;
        v = MRD(a);
        intern(z, a, 1);
        if (kind == 1) {
            bit_op(z, n, v, (uint8_t)(a >> 8));
            return;
        }
        if (kind == 0)      res = shift_op(z, n, v);
        else if (kind == 2) res = (uint8_t)(v & ~(1 << n));
        else                res = (uint8_t)(v | (1 << n));
        MWR(a, res);
        if (sel != 6) *r8(z, sel, 0) = res;  /* undocumented copy to register */
    }
}

/* ------------------------------------------------------------------ */
/* ED-prefixed                                                         */
/* ------------------------------------------------------------------ */
static const uint8_t im_tab[8] = { 0, 0, 1, 2, 0, 0, 1, 2 };

static void do_ed(Z80 *z)
{
    uint8_t op = fetch_op(z);

    if (op >= 0x40 && op < 0x80) {
        int y = (op >> 3) & 7;
        switch (op & 7) {
        case 0: {                                       /* IN r,(C) */
            uint8_t v = IORD(wBC);
            wMP = (uint16_t)(wBC + 1);
            if (y != 6) *r8(z, y, 0) = v;
            rF = (uint8_t)((rF & ZF_C) | sz53p_tab[v]);
            return;
        }
        case 1:                                         /* OUT (C),r */
            IOWR(wBC, y == 6 ? 0 : *r8(z, y, 0));
            wMP = (uint16_t)(wBC + 1);
            return;
        case 2:                                         /* SBC/ADC HL,rp */
            intern(z, IRW, 7);
            if (op & 0x08) adc16(z, rp(z, (op >> 4) & 3, 0)->w);
            else           sbc16(z, rp(z, (op >> 4) & 3, 0)->w);
            return;
        case 3: {                                       /* LD (nn),rp / rp,(nn) */
            uint16_t a = fetch16(z);
            wMP = (uint16_t)(a + 1);
            if (op & 0x08) rp(z, (op >> 4) & 3, 0)->w = rd16(z, a);
            else           wr16(z, a, rp(z, (op >> 4) & 3, 0)->w);
            return;
        }
        case 4: {                                       /* NEG (+ duplicates) */
            uint8_t a = rA;
            rA = 0;
            alu_sub8(z, a, 0);
            return;
        }
        case 5:                                         /* RETN/RETI (+dups) */
            z->iff1 = z->iff2;
            wPC = pop16(z);
            wMP = wPC;
            return;
        case 6:                                         /* IM 0/1/2 (+dups) */
            z->im = im_tab[y];
            return;
        default:                                        /* case 7 */
            switch (op) {
            case 0x47:                                  /* LD I,A */
                intern(z, IRW, 1);
                z->i = rA;
                return;
            case 0x4f:                                  /* LD R,A */
                intern(z, IRW, 1);
                z->r = rA;
                return;
            case 0x57:                                  /* LD A,I */
                intern(z, IRW, 1);
                rA = z->i;
                rF = (uint8_t)((rF & ZF_C) | sz53_tab[rA] | (z->iff2 ? ZF_PV : 0));
                return;
            case 0x5f:                                  /* LD A,R */
                intern(z, IRW, 1);
                rA = z->r;
                rF = (uint8_t)((rF & ZF_C) | sz53_tab[rA] | (z->iff2 ? ZF_PV : 0));
                return;
            case 0x67: {                                /* RRD */
                uint8_t v = MRD(wHL);
                intern(z, wHL, 4);
                MWR(wHL, (uint8_t)((rA << 4) | (v >> 4)));
                rA = (uint8_t)((rA & 0xf0) | (v & 0x0f));
                rF = (uint8_t)((rF & ZF_C) | sz53p_tab[rA]);
                wMP = (uint16_t)(wHL + 1);
                return;
            }
            case 0x6f: {                                /* RLD */
                uint8_t v = MRD(wHL);
                intern(z, wHL, 4);
                MWR(wHL, (uint8_t)((v << 4) | (rA & 0x0f)));
                rA = (uint8_t)((rA & 0xf0) | (v >> 4));
                rF = (uint8_t)((rF & ZF_C) | sz53p_tab[rA]);
                wMP = (uint16_t)(wHL + 1);
                return;
            }
            default: return;                            /* 0x77/0x7f: NOP */
            }
        }
    }

    if (op >= 0xa0 && op <= 0xbb && (op & 7) <= 3) {
        int delta = (op & 0x08) ? -1 : 1;
        int repeat = (op & 0x10) != 0;
        /* Flag fixup for interrupted INxR/OTxR repeats (Rak/hoglet67):
         * X/Y from PC high; P (and H if carry) adjusted from B/data. */
        #define IO_REPEAT_FIXUP(data)                                       \
            do {                                                            \
                uint8_t pe;                                                 \
                rF = (uint8_t)((rF & ~(ZF_X | ZF_Y))                        \
                    | ((wPC >> 8) & (ZF_X | ZF_Y)));                        \
                if (rF & ZF_C) {                                            \
                    if ((data) & 0x80) {                                    \
                        pe = (uint8_t)(sz53p_tab[(rB - 1) & 7] & ZF_PV);    \
                        rF = (uint8_t)((rF & ~ZF_H)                         \
                            | (((rB & 0x0f) == 0x00) ? ZF_H : 0));          \
                    } else {                                                \
                        pe = (uint8_t)(sz53p_tab[(rB + 1) & 7] & ZF_PV);    \
                        rF = (uint8_t)((rF & ~ZF_H)                         \
                            | (((rB & 0x0f) == 0x0f) ? ZF_H : 0));          \
                    }                                                       \
                } else {                                                    \
                    pe = (uint8_t)(sz53p_tab[rB & 7] & ZF_PV);              \
                }                                                           \
                rF ^= (uint8_t)(pe ^ ZF_PV);                                \
            } while (0)
        switch (op & 3) {
        case 0: {                                       /* LDI/LDD/LDIR/LDDR */
            uint8_t v = MRD(wHL);
            uint16_t wa = wDE;                          /* write address */
            uint8_t n;
            MWR(wa, v);
            intern(z, wa, 2);
            wHL = (uint16_t)(wHL + delta);
            wDE = (uint16_t)(wDE + delta);
            wBC--;
            n = (uint8_t)(rA + v);
            rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_C))
                | (wBC ? ZF_PV : 0)
                | ((n & 0x02) ? ZF_Y : 0)
                | (n & ZF_X));
            if (repeat && wBC) {
                intern(z, wa, 5);
                wPC -= 2;
                wMP = (uint16_t)(wPC + 1);
                /* interrupted repeat: X/Y leak from PC high byte */
                rF = (uint8_t)((rF & ~(ZF_X | ZF_Y))
                    | ((wPC >> 8) & (ZF_X | ZF_Y)));
            }
            return;
        }
        case 1: {                                       /* CPI/CPD/CPIR/CPDR */
            uint16_t ra = wHL;                          /* read address */
            uint8_t v = MRD(ra);
            uint8_t res = (uint8_t)(rA - v);
            uint8_t hf = (uint8_t)((rA ^ v ^ res) & ZF_H);
            uint8_t n = (uint8_t)(res - (hf ? 1 : 0));
            intern(z, ra, 5);
            wHL = (uint16_t)(wHL + delta);
            wBC--;
            wMP = (uint16_t)(wMP + delta);
            rF = (uint8_t)((rF & ZF_C) | ZF_N | hf
                | (sz53_tab[res] & (ZF_S | ZF_Z))
                | (wBC ? ZF_PV : 0)
                | ((n & 0x02) ? ZF_Y : 0)
                | (n & ZF_X));
            if (repeat && wBC && res) {
                intern(z, ra, 5);
                wPC -= 2;
                wMP = (uint16_t)(wPC + 1);
                rF = (uint8_t)((rF & ~(ZF_X | ZF_Y))
                    | ((wPC >> 8) & (ZF_X | ZF_Y)));
            }
            return;
        }
        case 2: {                                       /* INI/IND/INIR/INDR */
            uint8_t v;
            uint16_t k, wa;
            intern(z, IRW, 1);
            wMP = (uint16_t)(wBC + delta);
            v = IORD(wBC);
            wa = wHL;
            MWR(wa, v);
            rB--;
            wHL = (uint16_t)(wHL + delta);
            k = (uint16_t)(v + ((rC + delta) & 0xff));
            rF = (uint8_t)(sz53_tab[rB]
                | ((v & 0x80) ? ZF_N : 0)
                | ((k > 0xff) ? (ZF_H | ZF_C) : 0)
                | (sz53p_tab[(k & 7) ^ rB] & ZF_PV));
            if (repeat && rB) {
                intern(z, wa, 5);
                wPC -= 2;
                wMP = (uint16_t)(wPC + 1);   /* repeat taken: WZ=PC+1 */
                IO_REPEAT_FIXUP(v);
            }
            return;
        }
        default: {                                      /* OUTI/OUTD/OTIR/OTDR */
            uint8_t v;
            uint16_t k;
            intern(z, IRW, 1);
            rB--;
            v = MRD(wHL);
            IOWR(wBC, v);
            wHL = (uint16_t)(wHL + delta);
            wMP = (uint16_t)(wBC + delta);
            k = (uint16_t)(v + rL);
            rF = (uint8_t)(sz53_tab[rB]
                | ((v & 0x80) ? ZF_N : 0)
                | ((k > 0xff) ? (ZF_H | ZF_C) : 0)
                | (sz53p_tab[(k & 7) ^ rB] & ZF_PV));
            if (repeat && rB) {
                intern(z, wBC, 5);
                wPC -= 2;
                wMP = (uint16_t)(wPC + 1);   /* repeat taken: WZ=PC+1 */
                IO_REPEAT_FIXUP(v);
            }
            return;
        }
        }
        #undef IO_REPEAT_FIXUP
    }

    /* undefined ED: NOP (8T total from the two M1 fetches) */
}

/* ------------------------------------------------------------------ */
/* Main opcode execution. T-states are charged by the access helpers;  */
/* only internal machine cycles are charged explicitly.                */
/* ------------------------------------------------------------------ */
static void do_main(Z80 *z, uint8_t op, int pfx)
{
    /* generic LD r,r' block */
    if (op >= 0x40 && op < 0x80) {
        int dst = (op >> 3) & 7, src = op & 7;
        if (op == 0x76) {                               /* HALT */
            z->halted = 1;
            return;
        }
        if (dst == 6) {                                 /* LD (HL/IX+d),r */
            uint16_t a = mem_ea(z, pfx);
            MWR(a, *r8(z, src, 0));                     /* src is real H/L */
            return;
        }
        if (src == 6) {                                 /* LD r,(HL/IX+d) */
            uint16_t a = mem_ea(z, pfx);
            *r8(z, dst, 0) = MRD(a);
            return;
        }
        *r8(z, dst, pfx) = *r8(z, src, pfx);
        return;
    }

    /* generic ALU block */
    if (op >= 0x80 && op < 0xc0) {
        int src = op & 7, aop = (op >> 3) & 7;
        if (src == 6) {
            uint16_t a = mem_ea(z, pfx);
            alu_op(z, aop, MRD(a));
            return;
        }
        alu_op(z, aop, *r8(z, src, pfx));
        return;
    }

    /* 0x00-0x3F patterned groups */
    if (op < 0x40) {
        switch (op & 0x0f) {
        case 0x01: case 0x0b:
            if ((op & 0x0f) == 0x01) {                  /* LD rp,nn */
                rp(z, op >> 4, pfx)->w = fetch16(z);
                return;
            }
            intern(z, IRW, 2);                          /* DEC rp */
            rp(z, op >> 4, pfx)->w--;
            return;
        case 0x03:                                      /* INC rp */
            intern(z, IRW, 2);
            rp(z, op >> 4, pfx)->w++;
            return;
        case 0x09:                                      /* ADD HL/IX,rp */
        {
            RegPair *d = rp(z, 2, pfx);
            intern(z, IRW, 7);
            d->w = add16(z, d->w, rp(z, op >> 4, pfx)->w);
            return;
        }
        default:
            break;
        }
        switch (op & 0x07) {
        case 4: {                                       /* INC r/(HL) */
            int dst = (op >> 3) & 7;
            if (dst == 6) {
                uint16_t a = mem_ea(z, pfx);
                uint8_t v = MRD(a);
                v = inc8(z, v);
                intern(z, a, 1);
                MWR(a, v);
                return;
            }
            { uint8_t *p = r8(z, dst, pfx); *p = inc8(z, *p); }
            return;
        }
        case 5: {                                       /* DEC r/(HL) */
            int dst = (op >> 3) & 7;
            if (dst == 6) {
                uint16_t a = mem_ea(z, pfx);
                uint8_t v = MRD(a);
                v = dec8(z, v);
                intern(z, a, 1);
                MWR(a, v);
                return;
            }
            { uint8_t *p = r8(z, dst, pfx); *p = dec8(z, *p); }
            return;
        }
        case 6: {                                       /* LD r,n / LD (HL),n */
            int dst = (op >> 3) & 7;
            if (dst == 6) {
                if (pfx) {
                    /* LD (IX+d),n: pc+2:3 pc+3:3 pc+3:1x2 ea:3 */
                    int8_t d = (int8_t)fetch8(z);
                    uint16_t a = (uint16_t)((pfx == 1 ? z->ix.w : z->iy.w) + d);
                    uint8_t n = fetch8(z);
                    intern(z, (uint16_t)(wPC - 1), 2);
                    wMP = a;
                    MWR(a, n);
                } else {
                    uint8_t n = fetch8(z);
                    MWR(wHL, n);
                }
                return;
            }
            *r8(z, dst, pfx) = fetch8(z);
            return;
        }
        default:
            break;
        }
        switch (op) {
        case 0x00: return;                              /* NOP */
        case 0x02:                                      /* LD (BC),A */
            MWR(wBC, rA);
            z->memptr.b.l = (uint8_t)(wBC + 1);
            z->memptr.b.h = rA;
            return;
        case 0x07: {                                    /* RLCA */
            rA = (uint8_t)((rA << 1) | (rA >> 7));
            rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_PV)) | (rA & (ZF_X | ZF_Y | ZF_C)));
            return;
        }
        case 0x08: {                                    /* EX AF,AF' */
            RegPair t = z->af; z->af = z->af_; z->af_ = t;
            return;
        }
        case 0x0a:                                      /* LD A,(BC) */
            rA = MRD(wBC);
            wMP = (uint16_t)(wBC + 1);
            return;
        case 0x0f: {                                    /* RRCA */
            uint8_t c = rA & 1;
            rA = (uint8_t)((rA >> 1) | (c << 7));
            rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_PV)) | (rA & (ZF_X | ZF_Y)) | c);
            return;
        }
        case 0x10: {                                    /* DJNZ d */
            int8_t d;
            intern(z, IRW, 1);
            d = (int8_t)fetch8(z);
            if (--rB) {
                intern(z, (uint16_t)(wPC - 1), 5);
                wPC = (uint16_t)(wPC + d);
                wMP = wPC;
            }
            return;
        }
        case 0x12:                                      /* LD (DE),A */
            MWR(wDE, rA);
            z->memptr.b.l = (uint8_t)(wDE + 1);
            z->memptr.b.h = rA;
            return;
        case 0x17: {                                    /* RLA */
            uint8_t c = rA >> 7;
            rA = (uint8_t)((rA << 1) | (rF & ZF_C));
            rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_PV)) | (rA & (ZF_X | ZF_Y)) | c);
            return;
        }
        case 0x18: {                                    /* JR d */
            int8_t d = (int8_t)fetch8(z);
            intern(z, (uint16_t)(wPC - 1), 5);
            wPC = (uint16_t)(wPC + d);
            wMP = wPC;
            return;
        }
        case 0x1a:                                      /* LD A,(DE) */
            rA = MRD(wDE);
            wMP = (uint16_t)(wDE + 1);
            return;
        case 0x1f: {                                    /* RRA */
            uint8_t c = rA & 1;
            rA = (uint8_t)((rA >> 1) | ((rF & ZF_C) << 7));
            rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_PV)) | (rA & (ZF_X | ZF_Y)) | c);
            return;
        }
        case 0x20: case 0x28: case 0x30: case 0x38: {   /* JR cc,d */
            int8_t d = (int8_t)fetch8(z);
            if (cond(z, (op >> 3) & 3)) {
                intern(z, (uint16_t)(wPC - 1), 5);
                wPC = (uint16_t)(wPC + d);
                wMP = wPC;
            }
            return;
        }
        case 0x22: {                                    /* LD (nn),HL/IX */
            uint16_t a = fetch16(z);
            wr16(z, a, rp(z, 2, pfx)->w);
            wMP = (uint16_t)(a + 1);
            return;
        }
        case 0x27: {                                    /* DAA */
            uint8_t add = 0, c = (uint8_t)(rF & ZF_C);
            if ((rF & ZF_H) || (rA & 0x0f) > 9) add = 6;
            if (c || rA > 0x99) add |= 0x60;
            if (rA > 0x99) c = ZF_C;
            if (rF & ZF_N) alu_sub8(z, add, 0);
            else           alu_add8(z, add, 0);
            rF = (uint8_t)((rF & ~(ZF_C | ZF_PV)) | c | (sz53p_tab[rA] & ZF_PV));
            return;
        }
        case 0x2a: {                                    /* LD HL/IX,(nn) */
            uint16_t a = fetch16(z);
            rp(z, 2, pfx)->w = rd16(z, a);
            wMP = (uint16_t)(a + 1);
            return;
        }
        case 0x2f:                                      /* CPL */
            rA = (uint8_t)~rA;
            rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_PV | ZF_C))
                | (rA & (ZF_X | ZF_Y)) | ZF_H | ZF_N);
            return;
        case 0x32: {                                    /* LD (nn),A */
            uint16_t a = fetch16(z);
            MWR(a, rA);
            z->memptr.b.l = (uint8_t)(a + 1);
            z->memptr.b.h = rA;
            return;
        }
        case 0x37:                                      /* SCF */
            rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_PV))
                | ((rA | (rF & (uint8_t)~z->q)) & (ZF_X | ZF_Y)) | ZF_C);
            return;
        case 0x3a: {                                    /* LD A,(nn) */
            uint16_t a = fetch16(z);
            rA = MRD(a);
            wMP = (uint16_t)(a + 1);
            return;
        }
        case 0x3f: {                                    /* CCF */
            uint8_t c = (uint8_t)(rF & ZF_C);
            rF = (uint8_t)((rF & (ZF_S | ZF_Z | ZF_PV))
                | ((rA | (rF & (uint8_t)~z->q)) & (ZF_X | ZF_Y))
                | (c ? ZF_H : ZF_C));
            return;
        }
        default:
            return;
        }
    }

    /* 0xC0-0xFF */
    switch (op & 0x07) {
    case 0:                                             /* RET cc */
        intern(z, IRW, 1);
        if (cond(z, (op >> 3) & 7)) {
            wPC = pop16(z);
            wMP = wPC;
        }
        return;
    case 2: {                                           /* JP cc,nn */
        uint16_t a = fetch16(z);
        wMP = a;
        if (cond(z, (op >> 3) & 7)) wPC = a;
        return;
    }
    case 4: {                                           /* CALL cc,nn */
        uint16_t a = fetch16(z);
        wMP = a;
        if (cond(z, (op >> 3) & 7)) {
            intern(z, (uint16_t)(wPC - 1), 1);
            push16(z, wPC);
            wPC = a;
        }
        return;
    }
    case 6:                                             /* alu A,n */
        alu_op(z, (op >> 3) & 7, fetch8(z));
        return;
    case 7:                                             /* RST */
        intern(z, IRW, 1);
        push16(z, wPC);
        wPC = (uint16_t)(op & 0x38);
        wMP = wPC;
        return;
    default:
        break;
    }

    switch (op) {
    case 0xc1: case 0xd1: case 0xe1: case 0xf1:         /* POP rp2 */
        rp2(z, (op >> 4) & 3, pfx)->w = pop16(z);
        return;
    case 0xc5: case 0xd5: case 0xe5: case 0xf5:         /* PUSH rp2 */
        intern(z, IRW, 1);
        push16(z, rp2(z, (op >> 4) & 3, pfx)->w);
        return;
    case 0xc3: {                                        /* JP nn */
        uint16_t a = fetch16(z);
        wPC = a;
        wMP = a;
        return;
    }
    case 0xc9:                                          /* RET */
        wPC = pop16(z);
        wMP = wPC;
        return;
    case 0xcd: {                                        /* CALL nn */
        uint16_t a = fetch16(z);
        wMP = a;
        intern(z, (uint16_t)(wPC - 1), 1);
        push16(z, wPC);
        wPC = a;
        return;
    }
    case 0xd3: {                                        /* OUT (n),A */
        uint8_t n = fetch8(z);
        IOWR(((uint16_t)rA << 8) | n, rA);
        z->memptr.b.l = (uint8_t)(n + 1);
        z->memptr.b.h = rA;
        return;
    }
    case 0xd9: {                                        /* EXX */
        RegPair t;
        t = z->bc; z->bc = z->bc_; z->bc_ = t;
        t = z->de; z->de = z->de_; z->de_ = t;
        t = z->hl; z->hl = z->hl_; z->hl_ = t;
        return;
    }
    case 0xdb: {                                        /* IN A,(n) */
        uint16_t port = (uint16_t)(((uint16_t)rA << 8) | fetch8(z));
        rA = IORD(port);
        wMP = (uint16_t)(port + 1);
        return;
    }
    case 0xe3: {                                        /* EX (SP),HL/IX */
        RegPair *p = rp(z, 2, pfx);
        uint16_t lo, hi, t;
        lo = MRD(wSP);
        hi = MRD((uint16_t)(wSP + 1));
        intern(z, (uint16_t)(wSP + 1), 1);
        MWR((uint16_t)(wSP + 1), p->w >> 8);
        MWR(wSP, p->w & 0xff);
        intern(z, wSP, 2);
        t = (uint16_t)(lo | (hi << 8));
        p->w = t;
        wMP = t;
        return;
    }
    case 0xe9:                                          /* JP (HL/IX) */
        wPC = rp(z, 2, pfx)->w;
        return;
    case 0xeb: {                                        /* EX DE,HL (never indexed) */
        RegPair t = z->de; z->de = z->hl; z->hl = t;
        return;
    }
    case 0xf3:                                          /* DI */
        z->iff1 = z->iff2 = 0;
        return;
    case 0xf9:                                          /* LD SP,HL/IX */
        intern(z, IRW, 2);
        wSP = rp(z, 2, pfx)->w;
        return;
    case 0xfb:                                          /* EI */
        z->iff1 = z->iff2 = 1;
        z->ei_pending = 1;
        return;
    default:
        return;                                         /* unreachable */
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
void z80_reset(Z80 *z)
{
    init_tables();
    z->af.w = z->af_.w = 0xffff;
    z->bc.w = z->de.w = z->hl.w = 0;
    z->bc_.w = z->de_.w = z->hl_.w = 0;
    z->ix.w = z->iy.w = 0xffff;
    z->sp.w = 0xffff;
    z->pc.w = 0;
    z->memptr.w = 0;
    z->i = 0;
    z->r = 0;
    z->iff1 = z->iff2 = 0;
    z->im = 0;
    z->halted = 0;
    z->ei_pending = 0;
    z->q = 0;
}

/* Does an unprefixed (or DD/FD-prefixed non-CB) opcode modify F?
 * Needed for the internal Q register (SCF/CCF X/Y behavior). */
static int main_modifies_f(uint8_t op)
{
    if (op < 0x40) {
        if ((op & 7) == 4 || (op & 7) == 5) return 1;   /* INC/DEC r */
        if ((op & 0x0f) == 9) return 1;                 /* ADD HL,rr */
        switch (op) {
        case 0x07: case 0x0f: case 0x17: case 0x1f:     /* RLCA etc. */
        case 0x27: case 0x2f: case 0x37: case 0x3f:     /* DAA CPL SCF CCF */
            return 1;
        }
        return 0;
    }
    if (op >= 0x80 && op <= 0xbf) return 1;             /* ALU A,r */
    if ((op & 0xc7) == 0xc6) return 1;                  /* ALU A,n */
    /* POP AF and EX AF,AF' load F without going through the ALU; on real
     * Zilog NMOS hardware they do NOT latch Q (z80ccf tests 125/129). */
    return 0;
}

static int ed_modifies_f(uint8_t sub)
{
    if (sub >= 0x40 && sub < 0x80) {
        switch (sub & 7) {
        case 0: return 1;                               /* IN r,(C) */
        case 2: return 1;                               /* SBC/ADC HL,rr */
        case 4: return 1;                               /* NEG */
        case 7: return sub == 0x57 || sub == 0x5f       /* LD A,I/R */
                    || sub == 0x67 || sub == 0x6f;      /* RRD/RLD */
        }
        return 0;
    }
    if (sub >= 0xa0 && sub <= 0xbb && (sub & 7) < 4)    /* block ops */
        return 1;
    return 0;
}

int z80_step(Z80 *z)
{
    uint64_t t0 = z->tstates;
    int pfx = 0;
    uint8_t op, fmod;

    if (!tables_ready) init_tables();
    z->ei_pending = 0;

    if (z->halted) {
        r_inc(z);
        ct(z, wPC, 4);
        z->q = 0;
        return (int)(z->tstates - t0);
    }

    for (;;) {
        op = fetch_op(z);
        if (op == 0xdd) { pfx = 1; continue; }
        if (op == 0xfd) { pfx = 2; continue; }
        break;
    }

    if (op == 0xcb) {
        /* CB sub-opcode: rotates/shifts/BIT modify F, SET/RES do not.
         * For DDCB/FDCB the displacement byte precedes the sub-opcode. */
        uint8_t sub = z->mem_read(z->ctx,
                                  (uint16_t)(z->pc.w + (pfx ? 1 : 0)));
        fmod = (uint8_t)(sub < 0x80);
        do_cb(z, pfx);
    } else if (op == 0xed) {
        fmod = (uint8_t)ed_modifies_f(z->mem_read(z->ctx, z->pc.w));
        do_ed(z);
    } else {
        fmod = (uint8_t)main_modifies_f(op);
        do_main(z, op, pfx);
    }
    z->q = fmod ? z->af.b.l : 0;

    return (int)(z->tstates - t0);
}

int z80_int(Z80 *z, uint8_t bus)
{
    uint64_t t0 = z->tstates;

    if (!z->iff1 || z->ei_pending) return 0;

    z->halted = 0;
    z->q = 0;
    z->iff1 = z->iff2 = 0;
    r_inc(z);

    z->tstates += 7;                     /* INT acknowledge cycle */
    push16(z, wPC);

    switch (z->im) {
    case 2: {
        uint16_t vec = (uint16_t)(((uint16_t)z->i << 8) | bus);
        wPC = rd16(z, vec);
        break;
    }
    default:
        /* IM 0: assume RST-shaped bus byte; 0xFF = RST 38 like IM 1 */
        if (z->im == 0 && (bus & 0xc7) == 0xc7)
            wPC = (uint16_t)(bus & 0x38);
        else
            wPC = 0x0038;
        break;
    }
    wMP = wPC;

    return (int)(z->tstates - t0);
}

int z80_nmi(Z80 *z)
{
    uint64_t t0 = z->tstates;

    z->halted = 0;
    z->q = 0;
    z->iff1 = 0;                         /* iff2 preserved */
    r_inc(z);

    z->tstates += 5;
    push16(z, wPC);
    wPC = 0x0066;
    wMP = wPC;

    return (int)(z->tstates - t0);
}
