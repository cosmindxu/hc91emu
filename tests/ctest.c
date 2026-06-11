/* ctest — verify per-M-cycle contention timing. The contention callbacks
 * record (kind, addr, tstates) for every bus access; each case asserts the
 * exact sequence against the canonical Spectrum contention breakdowns
 * (sinclair wiki / FUSE tables): which address is contended, and at which
 * T-state offset within the instruction.
 *
 * Kinds: M = mem_contend, E = io_contend_early, L = io_contend_late.
 * Clock advance after the contend call: M = entry-specific, E = 1, L = 3.
 */
#include <stdio.h>
#include <string.h>
#include "../src/z80.h"

static uint8_t mem[65536];
static Z80 z;

static uint8_t rd(void *c, uint16_t a) { (void)c; return mem[a]; }
static void wr(void *c, uint16_t a, uint8_t v) { (void)c; mem[a] = v; }
static uint8_t in(void *c, uint16_t p) { (void)c; (void)p; return 0xFF; }
static void out(void *c, uint16_t p, uint8_t v) { (void)c; (void)p; (void)v; }

typedef struct { char kind; uint16_t addr; uint64_t t; } Ev;
#define MAXEV 32
static Ev evs[MAXEV];
static int nev;

static void log_ev(char kind, uint16_t addr)
{
    if (nev < MAXEV) {
        evs[nev].kind = kind;
        evs[nev].addr = addr;
        evs[nev].t = z.tstates;
        nev++;
    }
}
static int cmem(void *c, uint16_t a) { (void)c; log_ev('M', a); return 0; }
static int cioe(void *c, uint16_t p) { (void)c; log_ev('E', p); return 0; }
static int ciol(void *c, uint16_t p) { (void)c; log_ev('L', p); return 0; }

/* Register constants used by the cases */
#define P  0x8000u   /* PC */
#define S  0x7000u   /* SP */
#define H  0x6000u   /* HL */
#define D  0x6100u   /* DE */
#define EA 0x6805u   /* IX+5 */
#define IR1 0x1235u  /* (I<<8)|R after 1 opcode fetch (I=0x12, R=0x34) */
#define IR2 0x1236u  /* after 2 opcode fetches */
#define NN 0x2000u

typedef struct { char kind; uint16_t addr; uint8_t len; } Exp;

typedef struct {
    const char *name;
    uint8_t bytes[4]; int nbytes;
    int b_reg, c_reg, f_reg, a_reg;   /* presets, -1 = default */
    int mode;                         /* 0 step, 2 two steps, 1 INT/IM1, 3 INT/IM2 */
    Exp exp[24]; int nexp;
} Case;

#define M_(a,l) {'M',(uint16_t)(a),(l)}
#define E_(p)   {'E',(uint16_t)(p),1}
#define L_(p)   {'L',(uint16_t)(p),3}

static Case cases[] = {
  { "NOP", {0x00},1, -1,-1,-1,-1, 0,
    { M_(P,4) }, 1 },
  { "LD B,n", {0x06,5},2, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,3) }, 2 },
  { "LD A,(HL)", {0x7E},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(H,3) }, 2 },
  { "LD (HL),A", {0x77},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(H,3) }, 2 },
  { "LD A,(nn)", {0x3A,0x00,0x20},3, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,3), M_(P+2,3), M_(NN,3) }, 4 },
  { "LD HL,(nn)", {0x2A,0x00,0x20},3, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,3), M_(P+2,3), M_(NN,3), M_(NN+1,3) }, 5 },
  { "INC BC", {0x03},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(IR1,1), M_(IR1,1) }, 3 },
  { "ADD HL,DE", {0x19},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(IR1,1),M_(IR1,1),M_(IR1,1),M_(IR1,1),M_(IR1,1),M_(IR1,1),M_(IR1,1) }, 8 },
  { "INC (HL)", {0x34},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(H,3), M_(H,1), M_(H,3) }, 4 },
  { "LD (HL),n", {0x36,0x77},2, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,3), M_(H,3) }, 3 },
  { "JR d", {0x18,0x00},2, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,3), M_(P+1,1),M_(P+1,1),M_(P+1,1),M_(P+1,1),M_(P+1,1) }, 7 },
  { "JR NZ not taken", {0x20,0x00},2, -1,-1,ZF_Z,-1, 0,
    { M_(P,4), M_(P+1,3) }, 2 },
  { "DJNZ taken", {0x10,0xFE},2, 2,-1,-1,-1, 0,
    { M_(P,4), M_(IR1,1), M_(P+1,3), M_(P+1,1),M_(P+1,1),M_(P+1,1),M_(P+1,1),M_(P+1,1) }, 8 },
  { "DJNZ not taken", {0x10,0xFE},2, 1,-1,-1,-1, 0,
    { M_(P,4), M_(IR1,1), M_(P+1,3) }, 3 },
  { "RET", {0xC9},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(S,3), M_(S+1,3) }, 3 },
  { "RET NZ taken", {0xC0},1, -1,-1,0,-1, 0,
    { M_(P,4), M_(IR1,1), M_(S,3), M_(S+1,3) }, 4 },
  { "RET Z not taken", {0xC8},1, -1,-1,0,-1, 0,
    { M_(P,4), M_(IR1,1) }, 2 },
  { "CALL nn", {0xCD,0x00,0x10},3, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,3), M_(P+2,3), M_(P+2,1), M_(S-1,3), M_(S-2,3) }, 6 },
  { "CALL NZ not taken", {0xC4,0x00,0x10},3, -1,-1,ZF_Z,-1, 0,
    { M_(P,4), M_(P+1,3), M_(P+2,3) }, 3 },
  { "PUSH DE", {0xD5},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(IR1,1), M_(S-1,3), M_(S-2,3) }, 4 },
  { "POP DE", {0xD1},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(S,3), M_(S+1,3) }, 3 },
  { "RST 18h", {0xDF},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(IR1,1), M_(S-1,3), M_(S-2,3) }, 4 },
  { "JP nn", {0xC3,0x00,0x10},3, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,3), M_(P+2,3) }, 3 },
  { "EX (SP),HL", {0xE3},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(S,3), M_(S+1,3), M_(S+1,1), M_(S+1,3), M_(S,3), M_(S,1), M_(S,1) }, 8 },
  { "LD SP,HL", {0xF9},1, -1,-1,-1,-1, 0,
    { M_(P,4), M_(IR1,1), M_(IR1,1) }, 3 },
  { "LD A,I", {0xED,0x57},2, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(IR2,1) }, 3 },
  { "RLD", {0xED,0x6F},2, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(H,3), M_(H,1),M_(H,1),M_(H,1),M_(H,1), M_(H,3) }, 8 },
  { "BIT 7,(HL)", {0xCB,0x7E},2, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(H,3), M_(H,1) }, 4 },
  { "SET 3,(HL)", {0xCB,0xDE},2, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(H,3), M_(H,1), M_(H,3) }, 5 },
  { "ADD IX,BC", {0xDD,0x09},2, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(IR2,1),M_(IR2,1),M_(IR2,1),M_(IR2,1),M_(IR2,1),M_(IR2,1),M_(IR2,1) }, 9 },
  { "LD A,(IX+d)", {0xDD,0x7E,0x05},3, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(P+2,3), M_(P+2,1),M_(P+2,1),M_(P+2,1),M_(P+2,1),M_(P+2,1), M_(EA,3) }, 9 },
  { "LD (IX+d),n", {0xDD,0x36,0x05,0x77},4, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(P+2,3), M_(P+3,3), M_(P+3,1),M_(P+3,1), M_(EA,3) }, 7 },
  { "BIT 1,(IX+d)", {0xDD,0xCB,0x05,0x4E},4, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(P+2,3), M_(P+3,3), M_(P+3,1),M_(P+3,1), M_(EA,3), M_(EA,1) }, 8 },
  { "SET 1,(IX+d)", {0xDD,0xCB,0x05,0xCE},4, -1,-1,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(P+2,3), M_(P+3,3), M_(P+3,1),M_(P+3,1), M_(EA,3), M_(EA,1), M_(EA,3) }, 9 },
  { "LDI", {0xED,0xA0},2, 0,1,-1,-1, 0,   /* BC=1 */
    { M_(P,4), M_(P+1,4), M_(H,3), M_(D,3), M_(D,1),M_(D,1) }, 6 },
  { "LDIR repeat", {0xED,0xB0},2, 0,2,-1,-1, 0,  /* BC=2 */
    { M_(P,4), M_(P+1,4), M_(H,3), M_(D,3), M_(D,1),M_(D,1),
      M_(D,1),M_(D,1),M_(D,1),M_(D,1),M_(D,1) }, 11 },
  { "LDIR last", {0xED,0xB0},2, 0,1,-1,-1, 0,    /* BC=1 */
    { M_(P,4), M_(P+1,4), M_(H,3), M_(D,3), M_(D,1),M_(D,1) }, 6 },
  { "CPI", {0xED,0xA1},2, 0,2,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(H,3), M_(H,1),M_(H,1),M_(H,1),M_(H,1),M_(H,1) }, 8 },
  { "CPIR repeat", {0xED,0xB1},2, 0,2,-1,0x12, 0,  /* BC=2, A!=(HL) */
    { M_(P,4), M_(P+1,4), M_(H,3), M_(H,1),M_(H,1),M_(H,1),M_(H,1),M_(H,1),
      M_(H,1),M_(H,1),M_(H,1),M_(H,1),M_(H,1) }, 13 },
  { "IN A,(n)", {0xDB,0xFE},2, -1,-1,-1,0x12, 0,
    { M_(P,4), M_(P+1,3), E_(0x12FE), L_(0x12FE) }, 4 },
  { "OUT (n),A", {0xD3,0xFE},2, -1,-1,-1,0x12, 0,
    { M_(P,4), M_(P+1,3), E_(0x12FE), L_(0x12FE) }, 4 },
  { "IN B,(C)", {0xED,0x40},2, 0x20,0xFE,-1,-1, 0,
    { M_(P,4), M_(P+1,4), E_(0x20FE), L_(0x20FE) }, 4 },
  { "OUT (C),D", {0xED,0x51},2, 0x20,0xFE,-1,-1, 0,
    { M_(P,4), M_(P+1,4), E_(0x20FE), L_(0x20FE) }, 4 },
  { "INI", {0xED,0xA2},2, 2,0xFE,-1,-1, 0,      /* port = B before dec */
    { M_(P,4), M_(P+1,4), M_(IR2,1), E_(0x02FE), L_(0x02FE), M_(H,3) }, 6 },
  { "INIR repeat", {0xED,0xB2},2, 2,0xFE,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(IR2,1), E_(0x02FE), L_(0x02FE), M_(H,3),
      M_(H,1),M_(H,1),M_(H,1),M_(H,1),M_(H,1) }, 11 },
  { "OUTI", {0xED,0xA3},2, 2,0xFE,-1,-1, 0,     /* port = B after dec */
    { M_(P,4), M_(P+1,4), M_(IR2,1), M_(H,3), E_(0x01FE), L_(0x01FE) }, 6 },
  { "OTIR repeat", {0xED,0xB3},2, 2,0xFE,-1,-1, 0,
    { M_(P,4), M_(P+1,4), M_(IR2,1), M_(H,3), E_(0x01FE), L_(0x01FE),
      M_(0x01FE,1),M_(0x01FE,1),M_(0x01FE,1),M_(0x01FE,1),M_(0x01FE,1) }, 11 },
  { "HALT 2 cycles", {0x76},1, -1,-1,-1,-1, 2,
    { M_(P,4), M_(P+1,4) }, 2 },
  { "INT IM1", {0x00},1, -1,-1,-1,-1, 1,
    { M_(S-1,3), M_(S-2,3) }, 2 },
  { "INT IM2", {0x00},1, -1,-1,-1,-1, 3,
    { M_(S-1,3), M_(S-2,3), M_(0x12FF,3), M_(0x1300,3) }, 4 },
};

int main(void)
{
    int i, fails = 0;
    memset(&z, 0, sizeof z);
    z.mem_read = rd; z.mem_write = wr; z.io_read = in; z.io_write = out;
    z.mem_contend = cmem; z.io_contend_early = cioe; z.io_contend_late = ciol;

    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        Case *c = &cases[i];
        uint64_t t0, texp;
        int j, total, bad = 0;

        z80_reset(&z);
        z.tstates = 0;
        z.pc.w = P; z.sp.w = S; z.hl.w = H; z.de.w = D;
        z.ix.w = 0x6800; z.bc.w = 0x0202;
        z.i = 0x12; z.r = 0x34;
        z.af.b.h = 0x00; z.af.b.l = 0x00;
        if (c->b_reg >= 0) z.bc.b.h = (uint8_t)c->b_reg;
        if (c->c_reg >= 0) z.bc.b.l = (uint8_t)c->c_reg;
        if (c->f_reg >= 0) z.af.b.l = (uint8_t)c->f_reg;
        if (c->a_reg >= 0) z.af.b.h = (uint8_t)c->a_reg;
        memset(mem + H, 0, 16);
        memcpy(mem + P, c->bytes, (size_t)c->nbytes);
        mem[0x12FF] = 0x00; mem[0x1300] = 0x30;   /* IM2 vector -> 0x3000 */
        nev = 0;
        t0 = z.tstates;

        if (c->mode == 1 || c->mode == 3) {
            z.iff1 = z.iff2 = 1;
            z.im = (c->mode == 1) ? 1 : 2;
            total = z80_int(&z, 0xFF);
        } else {
            total = z80_step(&z);
            if (c->mode == 2)
                total += z80_step(&z);
        }

        /* Build expected times: cumulative sum of entry lengths.
         * For INT modes the acknowledge cycle (7 T) precedes the pushes. */
        texp = (c->mode == 1 || c->mode == 3) ? 7 : 0;
        if (nev != c->nexp) {
            printf("FAIL %-18s event count %d expect %d\n", c->name, nev, c->nexp);
            bad = 1;
        }
        for (j = 0; !bad && j < c->nexp; j++) {
            if (evs[j].kind != c->exp[j].kind || evs[j].addr != c->exp[j].addr
                || evs[j].t - t0 != texp) {
                printf("FAIL %-18s ev%d: got %c@%04X t+%d, expect %c@%04X t+%d\n",
                       c->name, j, evs[j].kind, evs[j].addr,
                       (int)(evs[j].t - t0),
                       c->exp[j].kind, c->exp[j].addr, (int)texp);
                bad = 1;
            }
            texp += c->exp[j].len;
            if (c->exp[j].kind == 'L') texp += 0; /* len 3 = transfer */
        }
        if (!bad && (uint64_t)total != z.tstates - t0) {
            printf("FAIL %-18s return %d but tstates advanced %d\n",
                   c->name, total, (int)(z.tstates - t0));
            bad = 1;
        }
        if (!bad && c->mode == 0 && (uint64_t)total != texp) {
            printf("FAIL %-18s total %d expect %d\n", c->name, total, (int)texp);
            bad = 1;
        }
        fails += bad;
    }
    if (!fails)
        printf("ctest: all %d contention-pattern cases OK\n",
               (int)(sizeof cases / sizeof cases[0]));
    return fails ? 1 : 0;
}
