/* dtest.c — Z80 disassembler unit test: byte sequences vs expected text
 * and length, covering documented + undocumented opcodes and all prefix
 * combinations (incl. dead prefixes, DDCB result-copy forms, ED no-ops).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/disasm.h"

typedef struct {
    const char *bytes;        /* hex, space-separated */
    uint16_t addr;            /* disassembly address (matters for JR/DJNZ) */
    int len;
    const char *txt;
} Case;

static const Case cases[] = {
    /* basic loads / arithmetic */
    {"00",          0x8000, 1, "NOP"},
    {"78",          0x8000, 1, "LD A,B"},
    {"41",          0x8000, 1, "LD B,C"},
    {"36 5A",       0x8000, 2, "LD (HL),$5A"},
    {"3E FF",       0x8000, 2, "LD A,$FF"},
    {"21 34 12",    0x8000, 3, "LD HL,$1234"},
    {"31 FF FF",    0x8000, 3, "LD SP,$FFFF"},
    {"3A 78 5C",    0x8000, 3, "LD A,($5C78)"},
    {"32 78 5C",    0x8000, 3, "LD ($5C78),A"},
    {"22 00 40",    0x8000, 3, "LD ($4000),HL"},
    {"0A",          0x8000, 1, "LD A,(BC)"},
    {"12",          0x8000, 1, "LD (DE),A"},
    {"86",          0x8000, 1, "ADD A,(HL)"},
    {"96",          0x8000, 1, "SUB (HL)"},
    {"D6 10",       0x8000, 2, "SUB $10"},
    {"CE 01",       0x8000, 2, "ADC A,$01"},
    {"FE 41",       0x8000, 2, "CP $41"},
    {"A7",          0x8000, 1, "AND A"},
    {"34",          0x8000, 1, "INC (HL)"},
    {"03",          0x8000, 1, "INC BC"},
    {"3B",          0x8000, 1, "DEC SP"},
    {"09",          0x8000, 1, "ADD HL,BC"},
    {"27",          0x8000, 1, "DAA"},
    {"3F",          0x8000, 1, "CCF"},
    {"37",          0x8000, 1, "SCF"},
    {"08",          0x8000, 1, "EX AF,AF'"},
    {"D9",          0x8000, 1, "EXX"},
    {"76",          0x8000, 1, "HALT"},
    /* jumps / calls (relative targets resolved) */
    {"C3 00 80",    0x8000, 3, "JP $8000"},
    {"DA 34 12",    0x8000, 3, "JP C,$1234"},
    {"18 FE",       0x8000, 2, "JR $8000"},
    {"20 05",       0x0000, 2, "JR NZ,$0007"},
    {"38 80",       0x8000, 2, "JR C,$7F82"},
    {"10 00",       0x4000, 2, "DJNZ $4002"},
    {"E9",          0x8000, 1, "JP (HL)"},
    {"C0",          0x8000, 1, "RET NZ"},
    {"D8",          0x8000, 1, "RET C"},
    {"C9",          0x8000, 1, "RET"},
    {"E4 34 12",    0x8000, 3, "CALL PO,$1234"},
    {"CD 34 12",    0x8000, 3, "CALL $1234"},
    {"C7",          0x8000, 1, "RST $00"},
    {"FF",          0x8000, 1, "RST $38"},
    {"F5",          0x8000, 1, "PUSH AF"},
    {"E1",          0x8000, 1, "POP HL"},
    {"E3",          0x8000, 1, "EX (SP),HL"},
    {"EB",          0x8000, 1, "EX DE,HL"},
    {"F9",          0x8000, 1, "LD SP,HL"},
    {"F3",          0x8000, 1, "DI"},
    {"FB",          0x8000, 1, "EI"},
    {"D3 FE",       0x8000, 2, "OUT ($FE),A"},
    {"DB FE",       0x8000, 2, "IN A,($FE)"},
    /* CB */
    {"CB 00",       0x8000, 2, "RLC B"},
    {"CB 36",       0x8000, 2, "SLL (HL)"},
    {"CB 3F",       0x8000, 2, "SRL A"},
    {"CB 47",       0x8000, 2, "BIT 0,A"},
    {"CB 7E",       0x8000, 2, "BIT 7,(HL)"},
    {"CB 8E",       0x8000, 2, "RES 1,(HL)"},
    {"CB FD",       0x8000, 2, "SET 7,L"},
    /* DD/FD index prefixes */
    {"DD 21 34 12", 0x8000, 4, "LD IX,$1234"},
    {"FD 2A 00 40", 0x8000, 4, "LD IY,($4000)"},
    {"DD 22 00 40", 0x8000, 4, "LD ($4000),IX"},
    {"DD 7E FF",    0x8000, 3, "LD A,(IX-$01)"},
    {"FD 36 05 42", 0x8000, 4, "LD (IY+$05),$42"},
    {"DD 70 03",    0x8000, 3, "LD (IX+$03),B"},
    {"DD 66 0A",    0x8000, 3, "LD H,(IX+$0A)"},
    {"DD 65",       0x8000, 2, "LD IXH,IXL"},
    {"DD 26 42",    0x8000, 3, "LD IXH,$42"},
    {"DD 24",       0x8000, 2, "INC IXH"},
    {"DD 34 7F",    0x8000, 3, "INC (IX+$7F)"},
    {"DD 35 80",    0x8000, 3, "DEC (IX-$80)"},
    {"DD 86 03",    0x8000, 3, "ADD A,(IX+$03)"},
    {"FD 96 FE",    0x8000, 3, "SUB (IY-$02)"},
    {"DD 09",       0x8000, 2, "ADD IX,BC"},
    {"DD 29",       0x8000, 2, "ADD IX,IX"},
    {"DD 23",       0x8000, 2, "INC IX"},
    {"DD E3",       0x8000, 2, "EX (SP),IX"},
    {"DD EB",       0x8000, 2, "EX DE,HL"},      /* never indexed */
    {"FD E9",       0x8000, 2, "JP (IY)"},
    {"DD F9",       0x8000, 2, "LD SP,IX"},
    {"DD E5",       0x8000, 2, "PUSH IX"},
    {"DD E1",       0x8000, 2, "POP IX"},
    {"DD 76",       0x8000, 2, "HALT"},
    {"DD 04",       0x8000, 2, "INC B"},         /* wasted prefix */
    /* dead prefixes */
    {"DD DD 21 34 12", 0x8000, 1, "DEFB $DD"},
    {"FD DD 21 34 12", 0x8000, 1, "DEFB $FD"},
    {"DD ED B0",    0x8000, 1, "DEFB $DD"},
    /* DDCB / FDCB */
    {"DD CB 05 06", 0x8000, 4, "RLC (IX+$05)"},
    {"DD CB FB 16", 0x8000, 4, "RL (IX-$05)"},
    {"DD CB 05 00", 0x8000, 4, "RLC (IX+$05),B"},
    {"FD CB 0A 88", 0x8000, 4, "RES 1,(IY+$0A),B"},
    {"DD CB 00 7E", 0x8000, 4, "BIT 7,(IX+$00)"},
    {"DD CB 10 41", 0x8000, 4, "BIT 0,(IX+$10)"},  /* z ignored */
    {"FD CB 00 C6", 0x8000, 4, "SET 0,(IY+$00)"},
    {"DD CB 00 C7", 0x8000, 4, "SET 0,(IX+$00),A"},
    /* ED */
    {"ED B0",       0x8000, 2, "LDIR"},
    {"ED A3",       0x8000, 2, "OUTI"},
    {"ED BB",       0x8000, 2, "OTDR"},
    {"ED A1",       0x8000, 2, "CPI"},
    {"ED 78",       0x8000, 2, "IN A,(C)"},
    {"ED 70",       0x8000, 2, "IN F,(C)"},
    {"ED 71",       0x8000, 2, "OUT (C),0"},
    {"ED 79",       0x8000, 2, "OUT (C),A"},
    {"ED 42",       0x8000, 2, "SBC HL,BC"},
    {"ED 4A",       0x8000, 2, "ADC HL,BC"},
    {"ED 7A",       0x8000, 2, "ADC HL,SP"},
    {"ED 43 78 5C", 0x8000, 4, "LD ($5C78),BC"},
    {"ED 4B 78 5C", 0x8000, 4, "LD BC,($5C78)"},
    {"ED 63 78 5C", 0x8000, 4, "LD ($5C78),HL"},  /* duplicate encoding */
    {"ED 44",       0x8000, 2, "NEG"},
    {"ED 4C",       0x8000, 2, "NEG"},            /* undocumented copy */
    {"ED 45",       0x8000, 2, "RETN"},
    {"ED 4D",       0x8000, 2, "RETI"},
    {"ED 55",       0x8000, 2, "RETN"},
    {"ED 46",       0x8000, 2, "IM 0"},
    {"ED 56",       0x8000, 2, "IM 1"},
    {"ED 5E",       0x8000, 2, "IM 2"},
    {"ED 6E",       0x8000, 2, "IM 0"},           /* undocumented copy */
    {"ED 47",       0x8000, 2, "LD I,A"},
    {"ED 5F",       0x8000, 2, "LD A,R"},
    {"ED 67",       0x8000, 2, "RRD"},
    {"ED 6F",       0x8000, 2, "RLD"},
    {"ED 77",       0x8000, 2, "DEFB $ED,$77"},
    {"ED 00",       0x8000, 2, "DEFB $ED,$00"},
    {"ED A4",       0x8000, 2, "DEFB $ED,$A4"},
};

typedef struct { uint8_t b[8]; } Mem;

static uint8_t peek(void *ctx, uint16_t addr)
{
    const Mem *mem = (const Mem *)ctx;
    return mem->b[addr & 7];    /* cases are <= 8 bytes, 8-aligned addrs */
}

int main(void)
{
    int i, fails = 0;
    int n = (int)(sizeof cases / sizeof cases[0]);

    for (i = 0; i < n; i++) {
        const Case *t = &cases[i];
        Mem mem;
        char out[48];
        int len, k = 0;
        const char *s = t->bytes;

        memset(&mem, 0, sizeof mem);
        while (*s && k < 8) {
            mem.b[(t->addr + k) & 7] = (uint8_t)strtoul(s, (char **)&s, 16);
            k++;
        }
        len = z80_disasm(peek, &mem, t->addr, out, sizeof out);
        if (len != t->len || strcmp(out, t->txt) != 0) {
            printf("FAIL [%s] @%04X: got len=%d \"%s\", want len=%d \"%s\"\n",
                   t->bytes, t->addr, len, out, t->len, t->txt);
            fails++;
        }
    }
    if (fails) {
        printf("dtest: %d/%d FAILED\n", fails, n);
        return 1;
    }
    printf("dtest: all %d disassembly cases OK\n", n);
    return 0;
}
