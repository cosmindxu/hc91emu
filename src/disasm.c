/* disasm.c — Z80 disassembler: all documented and undocumented opcodes,
 * CB/ED/DD/FD/DDCB/FDCB prefixes, $-hex notation, relative jumps shown as
 * resolved targets. Decodes algorithmically by the x/y/z/p/q opcode bit
 * fields (see "decoding Z80 opcodes", z80.info) instead of literal
 * 256-entry tables.
 *
 * Conventions: a dead prefix (DD/FD followed by DD/FD/ED) and the ED
 * no-ops disassemble as DEFB so the byte stream is always represented
 * honestly; DDCB non-(HL) slots use the "RLC (IX+$05),B" result-copy
 * notation; ED70/ED71 are "IN F,(C)" / "OUT (C),0".
 */
#include <stdio.h>
#include <string.h>
#include "disasm.h"

typedef struct {
    z80_peek_fn peek;
    void *ctx;
    uint16_t addr;            /* next byte to fetch */
} Cur;

static uint8_t fetch8(Cur *c) { return c->peek(c->ctx, c->addr++); }
static uint16_t fetch16(Cur *c)
{
    uint16_t lo = fetch8(c);
    return (uint16_t)(lo | ((uint16_t)fetch8(c) << 8));
}

static const char *const r8[8]  = {"B","C","D","E","H","L","(HL)","A"};
static const char *const r8x[3][8] = {        /* per prefix mode */
    {"B","C","D","E","H","L","(HL)","A"},
    {"B","C","D","E","IXH","IXL","(IX)","A"},
    {"B","C","D","E","IYH","IYL","(IY)","A"},
};
static const char *const hls[3] = {"HL","IX","IY"};
static const char *const rpn[4]  = {"BC","DE","HL","SP"};
static const char *const rp2n[4] = {"BC","DE","HL","AF"};
static const char *const ccn[8]  = {"NZ","Z","NC","C","PO","PE","P","M"};
static const char *const alu[8]  = {"ADD A,","ADC A,","SUB ","SBC A,",
                                    "AND ","XOR ","OR ","CP "};
static const char *const rot[8]  = {"RLC","RRC","RL","RR",
                                    "SLA","SRA","SLL","SRL"};
static const char *const bli[4][4] = {
    {"LDI","CPI","INI","OUTI"},
    {"LDD","CPD","IND","OUTD"},
    {"LDIR","CPIR","INIR","OTIR"},
    {"LDDR","CPDR","INDR","OTDR"},
};

static const char *rps(int mode, int p)  { return p == 2 ? hls[mode] : rpn[p]; }
static const char *rp2s(int mode, int p) { return p == 2 ? hls[mode] : rp2n[p]; }

/* "(HL)", or "(IX+$05)" fetching the displacement byte, per mode. */
static void idx_op(Cur *c, int mode, char *dst)
{
    if (mode == 0) {
        strcpy(dst, "(HL)");
    } else {
        int8_t d = (int8_t)fetch8(c);
        sprintf(dst, "(I%c%c$%02X)", mode == 1 ? 'X' : 'Y',
                d < 0 ? '-' : '+', d < 0 ? -d : d);
    }
}

/* Fetch a relative displacement and print the resolved target. */
static void rel_target(Cur *c, char *dst)
{
    int8_t d = (int8_t)fetch8(c);
    sprintf(dst, "$%04X", (uint16_t)(c->addr + d));
}

static void main_decode(Cur *c, int mode, uint8_t op, char *o)
{
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    char a[24];

    if (x == 1) {                                 /* LD r,r' / HALT */
        if (y == 6 && z == 6) { strcpy(o, "HALT"); return; }
        if (mode && (y == 6 || z == 6)) {
            /* the (IX+d) side is indexed; the other side stays H/L */
            idx_op(c, mode, a);
            if (y == 6) sprintf(o, "LD %s,%s", a, r8[z]);
            else        sprintf(o, "LD %s,%s", r8[y], a);
        } else {
            sprintf(o, "LD %s,%s", r8x[mode][y], r8x[mode][z]);
        }
        return;
    }
    if (x == 2) {                                 /* ALU A,r */
        if (mode && z == 6) idx_op(c, mode, a);
        else strcpy(a, r8x[mode][z]);
        sprintf(o, "%s%s", alu[y], a);
        return;
    }
    if (x == 0) {
        switch (z) {
        case 0:
            switch (y) {
            case 0: strcpy(o, "NOP"); return;
            case 1: strcpy(o, "EX AF,AF'"); return;
            case 2: rel_target(c, a); sprintf(o, "DJNZ %s", a); return;
            case 3: rel_target(c, a); sprintf(o, "JR %s", a); return;
            default: rel_target(c, a);
                     sprintf(o, "JR %s,%s", ccn[y - 4], a); return;
            }
        case 1:
            if (!q) sprintf(o, "LD %s,$%04X", rps(mode, p), fetch16(c));
            else    sprintf(o, "ADD %s,%s", hls[mode], rps(mode, p));
            return;
        case 2:
            if (!q && p == 0)      strcpy(o, "LD (BC),A");
            else if (!q && p == 1) strcpy(o, "LD (DE),A");
            else if (!q && p == 2) sprintf(o, "LD ($%04X),%s",
                                           fetch16(c), hls[mode]);
            else if (!q)           sprintf(o, "LD ($%04X),A", fetch16(c));
            else if (p == 0)       strcpy(o, "LD A,(BC)");
            else if (p == 1)       strcpy(o, "LD A,(DE)");
            else if (p == 2)       sprintf(o, "LD %s,($%04X)",
                                           hls[mode], fetch16(c));
            else                   sprintf(o, "LD A,($%04X)", fetch16(c));
            return;
        case 3:
            sprintf(o, "%s %s", q ? "DEC" : "INC", rps(mode, p));
            return;
        case 4: case 5:
            if (mode && y == 6) idx_op(c, mode, a);
            else strcpy(a, r8x[mode][y]);
            sprintf(o, "%s %s", z == 4 ? "INC" : "DEC", a);
            return;
        case 6:                                   /* LD r,n (d before n) */
            if (mode && y == 6) idx_op(c, mode, a);
            else strcpy(a, r8x[mode][y]);
            sprintf(o, "LD %s,$%02X", a, fetch8(c));
            return;
        default: {
            static const char *const acc[8] = {"RLCA","RRCA","RLA","RRA",
                                               "DAA","CPL","SCF","CCF"};
            strcpy(o, acc[y]);
            return;
        }
        }
    }
    /* x == 3 (CB/ED/DD/FD slots are filtered out before we get here) */
    switch (z) {
    case 0: sprintf(o, "RET %s", ccn[y]); return;
    case 1:
        if (!q) { sprintf(o, "POP %s", rp2s(mode, p)); return; }
        switch (p) {
        case 0:  strcpy(o, "RET"); return;
        case 1:  strcpy(o, "EXX"); return;
        case 2:  sprintf(o, "JP (%s)", hls[mode]); return;
        default: sprintf(o, "LD SP,%s", hls[mode]); return;
        }
    case 2: sprintf(o, "JP %s,$%04X", ccn[y], fetch16(c)); return;
    case 3:
        switch (y) {
        case 0:  sprintf(o, "JP $%04X", fetch16(c)); return;
        case 2:  sprintf(o, "OUT ($%02X),A", fetch8(c)); return;
        case 3:  sprintf(o, "IN A,($%02X)", fetch8(c)); return;
        case 4:  sprintf(o, "EX (SP),%s", hls[mode]); return;
        case 5:  strcpy(o, "EX DE,HL"); return;   /* never indexed */
        case 6:  strcpy(o, "DI"); return;
        default: strcpy(o, "EI"); return;
        }
    case 4: sprintf(o, "CALL %s,$%04X", ccn[y], fetch16(c)); return;
    case 5:
        if (!q) sprintf(o, "PUSH %s", rp2s(mode, p));
        else    sprintf(o, "CALL $%04X", fetch16(c));
        return;
    case 6: sprintf(o, "%s$%02X", alu[y], fetch8(c)); return;
    default: sprintf(o, "RST $%02X", y * 8); return;
    }
}

static void cb_decode(Cur *c, char *o)
{
    uint8_t op = fetch8(c);
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    switch (x) {
    case 0:  sprintf(o, "%s %s", rot[y], r8[z]); break;
    case 1:  sprintf(o, "BIT %d,%s", y, r8[z]); break;
    case 2:  sprintf(o, "RES %d,%s", y, r8[z]); break;
    default: sprintf(o, "SET %d,%s", y, r8[z]); break;
    }
}

static void ddcb_decode(Cur *c, int mode, char *o)
{
    char ix[16];
    uint8_t op;
    int x, y, z;

    idx_op(c, mode, ix);                  /* displacement precedes opcode */
    op = fetch8(c);
    x = op >> 6; y = (op >> 3) & 7; z = op & 7;
    switch (x) {
    case 0:
        if (z == 6) sprintf(o, "%s %s", rot[y], ix);
        else        sprintf(o, "%s %s,%s", rot[y], ix, r8[z]);
        break;
    case 1:
        sprintf(o, "BIT %d,%s", y, ix);   /* z is ignored by hardware */
        break;
    case 2:
        if (z == 6) sprintf(o, "RES %d,%s", y, ix);
        else        sprintf(o, "RES %d,%s,%s", y, ix, r8[z]);
        break;
    default:
        if (z == 6) sprintf(o, "SET %d,%s", y, ix);
        else        sprintf(o, "SET %d,%s,%s", y, ix, r8[z]);
        break;
    }
}

static void ed_decode(Cur *c, char *o)
{
    uint8_t op = fetch8(c);
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    static const int im[8] = {0, 0, 1, 2, 0, 0, 1, 2};

    if (x == 1) {
        switch (z) {
        case 0:
            if (y == 6) strcpy(o, "IN F,(C)");
            else        sprintf(o, "IN %s,(C)", r8[y]);
            return;
        case 1:
            if (y == 6) strcpy(o, "OUT (C),0");
            else        sprintf(o, "OUT (C),%s", r8[y]);
            return;
        case 2: sprintf(o, "%s HL,%s", q ? "ADC" : "SBC", rpn[p]); return;
        case 3:
            if (!q) sprintf(o, "LD ($%04X),%s", fetch16(c), rpn[p]);
            else    sprintf(o, "LD %s,($%04X)", rpn[p], fetch16(c));
            return;
        case 4: strcpy(o, "NEG"); return;
        case 5: strcpy(o, y == 1 ? "RETI" : "RETN"); return;
        case 6: sprintf(o, "IM %d", im[y]); return;
        default:
            switch (y) {
            case 0: strcpy(o, "LD I,A"); return;
            case 1: strcpy(o, "LD R,A"); return;
            case 2: strcpy(o, "LD A,I"); return;
            case 3: strcpy(o, "LD A,R"); return;
            case 4: strcpy(o, "RRD");    return;
            case 5: strcpy(o, "RLD");    return;
            default: break;                    /* ED 77/7F: no-ops */
            }
            break;
        }
    } else if (x == 2 && z <= 3 && y >= 4) {
        strcpy(o, bli[y - 4][z]);
        return;
    }
    sprintf(o, "DEFB $ED,$%02X", op);
}

int z80_disasm(z80_peek_fn peek, void *ctx, uint16_t addr,
               char *out, size_t outsz)
{
    Cur c;
    char buf[48];
    uint8_t op;

    c.peek = peek;
    c.ctx = ctx;
    c.addr = addr;
    op = fetch8(&c);

    if (op == 0xDD || op == 0xFD) {
        int mode = (op == 0xDD) ? 1 : 2;
        uint8_t nxt = peek(ctx, c.addr);
        if (nxt == 0xDD || nxt == 0xFD || nxt == 0xED) {
            sprintf(buf, "DEFB $%02X", op);   /* dead prefix */
        } else {
            op = fetch8(&c);
            if (op == 0xCB) ddcb_decode(&c, mode, buf);
            else            main_decode(&c, mode, op, buf);
        }
    } else if (op == 0xCB) {
        cb_decode(&c, buf);
    } else if (op == 0xED) {
        ed_decode(&c, buf);
    } else {
        main_decode(&c, 0, op, buf);
    }

    snprintf(out, outsz, "%s", buf);
    return (int)(uint16_t)(c.addr - addr);
}
