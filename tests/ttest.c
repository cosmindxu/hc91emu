/* ttest — verify T-state counts of individual instructions against the
 * official Z80 documentation values. Focus: everything the ROM BEEPER
 * routine executes, plus common suspects.
 */
#include <stdio.h>
#include <string.h>
#include "../src/z80.h"

static uint8_t mem[65536];
static uint8_t rd(void *c, uint16_t a) { (void)c; return mem[a]; }
static void wr(void *c, uint16_t a, uint8_t v) { (void)c; mem[a] = v; }
static uint8_t in(void *c, uint16_t p) { (void)c; (void)p; return 0xFF; }
static void out(void *c, uint16_t p, uint8_t v) { (void)c; (void)p; (void)v; }

typedef struct {
    const char *name;
    uint8_t bytes[6]; int nbytes;
    int b_reg;          /* preset B (for DJNZ), -1 = don't care */
    int f_reg;          /* preset F, -1 = don't care */
    int expect;
} Case;

static Case cases[] = {
    { "NOP",            {0x00}, 1, -1, -1, 4 },
    { "LD B,n",         {0x06,5}, 2, -1, -1, 7 },
    { "INC B",          {0x04}, 1, -1, -1, 4 },
    { "DEC C",          {0x0D}, 1, -1, -1, 4 },
    { "LD A,D",         {0x7A}, 1, -1, -1, 4 },
    { "LD B,H",         {0x60}, 1, -1, -1, 4 },
    { "OR E",           {0xB3}, 1, -1, -1, 4 },
    { "XOR n",          {0xEE,0x10}, 2, -1, -1, 7 },
    { "OUT (n),A",      {0xD3,0xFE}, 2, -1, -1, 11 },
    { "IN A,(n)",       {0xDB,0xFE}, 2, -1, -1, 11 },
    { "BIT 4,A",        {0xCB,0x67}, 2, -1, -1, 8 },
    { "DEC DE",         {0x1B}, 1, -1, -1, 6 },
    { "DJNZ taken",     {0x10,0xFE}, 2, 2, -1, 13 },
    { "DJNZ not taken", {0x10,0xFE}, 2, 1, -1, 8 },
    { "JR d",           {0x18,0x00}, 2, -1, -1, 12 },
    { "JR Z taken",     {0x28,0x00}, 2, -1, ZF_Z, 12 },
    { "JR Z not taken", {0x28,0x00}, 2, -1, 0, 7 },
    { "JR NZ taken",    {0x20,0x00}, 2, -1, 0, 12 },
    { "JR NZ not taken",{0x20,0x00}, 2, -1, ZF_Z, 7 },
    { "JP nn",          {0xC3,0x00,0x10}, 3, -1, -1, 10 },
    { "JP (HL)",        {0xE9}, 1, -1, -1, 4 },
    { "JP (IX)",        {0xDD,0xE9}, 2, -1, -1, 8 },
    { "CALL nn",        {0xCD,0x00,0x10}, 3, -1, -1, 17 },
    { "RET",            {0xC9}, 1, -1, -1, 10 },
    { "RET Z taken",    {0xC8}, 1, -1, ZF_Z, 11 },
    { "RET Z not taken",{0xC8}, 1, -1, 0, 5 },
    { "PUSH BC",        {0xC5}, 1, -1, -1, 11 },
    { "POP BC",         {0xC1}, 1, -1, -1, 10 },
    { "LD A,(HL)",      {0x7E}, 1, -1, -1, 7 },
    { "LD (HL),A",      {0x77}, 1, -1, -1, 7 },
    { "ADD A,n",        {0xC6,1}, 2, -1, -1, 7 },
    { "ADD HL,DE",      {0x19}, 1, -1, -1, 11 },
    { "INC (HL)",       {0x34}, 1, -1, -1, 11 },
    { "EX (SP),HL",     {0xE3}, 1, -1, -1, 19 },
    { "LDI",            {0xED,0xA0}, 2, -1, -1, 16 },
    { "RLD",            {0xED,0x6F}, 2, -1, -1, 18 },
    { "LD A,(IX+d)",    {0xDD,0x7E,0x00}, 3, -1, -1, 19 },
    { "SRL L",          {0xCB,0x3D}, 2, -1, -1, 8 },
    { "CPL",            {0x2F}, 1, -1, -1, 4 },
    { "AND n",          {0xE6,3}, 2, -1, -1, 7 },
    { "LD IX,nn",       {0xDD,0x21,0,0}, 4, -1, -1, 14 },
    { "ADD IX,BC",      {0xDD,0x09}, 2, -1, -1, 15 },
    { "RRCA",           {0x0F}, 1, -1, -1, 4 },
    { "LD A,(nn)",      {0x3A,0,0x20}, 3, -1, -1, 13 },
    { "HALT? skip",     {0x00}, 1, -1, -1, 4 },
};

int main(void)
{
    Z80 z;
    int i, fails = 0;
    memset(&z, 0, sizeof z);
    z.mem_read = rd; z.mem_write = wr; z.io_read = in; z.io_write = out;

    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        Case *c = &cases[i];
        int t;
        z80_reset(&z);
        z.pc.w = 0x4000;
        z.sp.w = 0x8000;
        z.hl.w = 0x6000; z.de.w = 0x6100; z.bc.w = 0x0101;
        z.ix.w = 0x6000;
        if (c->b_reg >= 0) z.bc.b.h = (uint8_t)c->b_reg;
        if (c->f_reg >= 0) z.af.b.l = (uint8_t)c->f_reg;
        memcpy(mem + 0x4000, c->bytes, (size_t)c->nbytes);
        t = z80_step(&z);
        if (t != c->expect) {
            printf("FAIL %-16s got %2d expect %2d\n", c->name, t, c->expect);
            fails++;
        }
    }
    if (!fails) printf("ttest: all %d timing cases OK\n",
                       (int)(sizeof cases / sizeof cases[0]));
    return fails ? 1 : 0;
}
