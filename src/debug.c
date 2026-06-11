/* debug.c — scriptable monitor: breakpoints, watchpoints (memory + port),
 * single-step, disassembly, hex dump, registers with contention-aware
 * T-state display, per-instruction trace-to-file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "machine.h"
#include "debug.h"
#include "disasm.h"

static uint8_t dbg_peek(void *ctx, uint16_t addr)
{
    return machine_peek((const Machine *)ctx, addr);
}

/* ---- formatting ---- */

static void flags_str(uint8_t f, char out[9])
{
    const char *names = "SZ5H3PNC";          /* bit 7 .. bit 0 */
    int i;
    for (i = 0; i < 8; i++)
        out[i] = (f & (0x80u >> i)) ? names[i] : '-';
    out[8] = 0;
}

static void print_regs(Machine *m)
{
    Z80 *z = &m->cpu;
    char fs[9];
    flags_str(z->af.b.l, fs);
    printf("PC=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X\n",
           z->pc.w, z->af.w, z->bc.w, z->de.w, z->hl.w,
           z->ix.w, z->iy.w, z->sp.w);
    printf("AF'=%04X BC'=%04X DE'=%04X HL'=%04X I=%02X R=%02X "
           "IM=%d IFF=%d,%d Q=%02X HALT=%d\n",
           z->af_.w, z->bc_.w, z->de_.w, z->hl_.w, z->i, z->r,
           z->im, z->iff1, z->iff2, z->q, z->halted);
    printf("F=%s T=%llu/%d frame=%u total=%llu\n",
           fs, (unsigned long long)(z->tstates - m->frame_start_ts),
           HC91_FRAME_TSTATES, m->frame_counter,
           (unsigned long long)z->tstates);
}

static int print_dis_line(Machine *m, uint16_t addr)
{
    char txt[48], bytes[16];
    int len = z80_disasm(dbg_peek, m, addr, txt, sizeof txt);
    int i, n = 0;
    for (i = 0; i < len; i++)
        n += sprintf(bytes + n, "%02X ",
                     machine_peek(m, (uint16_t)(addr + i)));
    printf("%04X: %-12s %s\n", addr, bytes, txt);
    return len;
}

static void dump_mem(Machine *m, uint16_t addr, int len)
{
    while (len > 0) {
        int n = len > 16 ? 16 : len, i;
        printf("%04X:", addr);
        for (i = 0; i < n; i++)
            printf(" %02X", machine_peek(m, (uint16_t)(addr + i)));
        for (; i < 16; i++)
            printf("   ");
        printf("  |");
        for (i = 0; i < n; i++) {
            uint8_t b = machine_peek(m, (uint16_t)(addr + i));
            putchar(b >= 32 && b < 127 ? b : '.');
        }
        printf("|\n");
        addr = (uint16_t)(addr + n);
        len -= n;
    }
}

static void trace_line(Machine *m)
{
    Debugger *d = m->dbg;
    Z80 *z = &m->cpu;
    char txt[48], fs[9];
    z80_disasm(dbg_peek, m, z->pc.w, txt, sizeof txt);
    flags_str(z->af.b.l, fs);
    fprintf(d->trace, "%04X: %-18s AF=%04X BC=%04X DE=%04X HL=%04X "
            "IX=%04X IY=%04X SP=%04X F=%s T=%llu\n",
            z->pc.w, txt, z->af.w, z->bc.w, z->de.w, z->hl.w,
            z->ix.w, z->iy.w, z->sp.w, fs,
            (unsigned long long)z->tstates);
}

/* ---- command parsing ---- */

/* Hex address, optionally $- or 0x-prefixed; also pc/sp/hl. */
static int parse_u16(Machine *m, const char *s, uint16_t *out)
{
    char *end;
    unsigned long v;
    if (!s || !*s) return -1;
    if (!strcmp(s, "pc")) { *out = m->cpu.pc.w; return 0; }
    if (!strcmp(s, "sp")) { *out = m->cpu.sp.w; return 0; }
    if (!strcmp(s, "hl")) { *out = m->cpu.hl.w; return 0; }
    if (s[0] == '$') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    v = strtoul(s, &end, 16);
    if (end == s || *end || v > 0xFFFF) return -1;
    *out = (uint16_t)v;
    return 0;
}

static int in_list(const uint16_t *arr, int n, uint16_t v)
{
    int i;
    for (i = 0; i < n; i++)
        if (arr[i] == v) return 1;
    return 0;
}

static void list_toggle(uint16_t *arr, int *n, uint16_t v, const char *what)
{
    int i;
    for (i = 0; i < *n; i++) {
        if (arr[i] == v) {
            arr[i] = arr[--(*n)];
            printf("%s at $%04X removed\n", what, v);
            return;
        }
    }
    if (*n >= DBG_MAX_BP) {
        printf("too many %ss (max %d)\n", what, DBG_MAX_BP);
        return;
    }
    arr[(*n)++] = v;
    printf("%s at $%04X set\n", what, v);
}

static void list_print(const char *what, const uint16_t *arr, int n)
{
    int i;
    printf("%s:", what);
    if (!n) printf(" none");
    for (i = 0; i < n; i++)
        printf(" $%04X", arr[i]);
    printf("\n");
}

/* Next command: script first, then stdin; 0 = exhausted (auto-continue).
 * Commands are echoed so a scripted/piped session reads as a transcript. */
static int next_cmd(Debugger *d, char *buf, size_t sz)
{
    if (d->script) {
        const char *s = d->script, *e;
        if (!*s) return 0;
        e = strchr(s, ';');
        if (e) {
            size_t n = (size_t)(e - s);
            if (n > sz - 1) n = sz - 1;
            memcpy(buf, s, n);
            buf[n] = 0;
            d->script = e + 1;
        } else {
            snprintf(buf, sz, "%s", s);
            d->script = s + strlen(s);
        }
        printf("dbg> %s\n", buf);
        return 1;
    }
    if (d->stdin_eof) return 0;
    if (d->is_tty) { printf("dbg> "); fflush(stdout); }
    if (!fgets(buf, (int)sz, stdin)) { d->stdin_eof = 1; return 0; }
    buf[strcspn(buf, "\n")] = 0;
    if (!d->is_tty) printf("dbg> %s\n", buf);
    return 1;
}

static void help(void)
{
    printf("commands (addresses hex, counts decimal; empty repeats last):\n"
           "  r|regs             registers + T-state/frame position\n"
           "  d|dis [ADDR [N]]   disassemble N instructions (PC, 8)\n"
           "  m|mem ADDR [N]     hex dump N bytes (64)\n"
           "  s|step [N]         execute N instructions, stop again\n"
           "  c|cont             continue\n"
           "  b|break [ADDR]     toggle PC breakpoint / list\n"
           "  w|watch [ADDR]     toggle memory-write watch / list\n"
           "  wr|rwatch [ADDR]   toggle memory-read watch (incl. fetches)\n"
           "  wp|pwatch [PORT]   toggle I/O watch (<=FF matches low byte)\n"
           "  t|trace FILE|off   per-instruction trace to file\n"
           "  q|quit             exit immediately (skips end-of-run saves)\n");
}

static void repl(Machine *m)
{
    Debugger *d = m->dbg;
    char line[128], cmd[16], a1[32], a2[32];

    print_dis_line(m, m->cpu.pc.w);
    for (;;) {
        int nf;
        char *s;
        if (!next_cmd(d, line, sizeof line))
            return;                            /* input exhausted: continue */
        s = line + strspn(line, " \t");
        memmove(line, s, strlen(s) + 1);
        while (line[0] && strchr(" \t", line[strlen(line) - 1]))
            line[strlen(line) - 1] = 0;
        if (!line[0]) {
            if (!d->last[0]) continue;
            strcpy(line, d->last);
        } else {
            strcpy(d->last, line);
        }
        a1[0] = a2[0] = 0;
        nf = sscanf(line, "%15s %31s %31s", cmd, a1, a2);
        if (nf < 1) continue;

        if (!strcmp(cmd, "c") || !strcmp(cmd, "cont")) {
            return;
        } else if (!strcmp(cmd, "s") || !strcmp(cmd, "step")) {
            int n = (nf >= 2) ? atoi(a1) : 1;
            d->step = n < 1 ? 1 : n;
            return;
        } else if (!strcmp(cmd, "r") || !strcmp(cmd, "regs")) {
            print_regs(m);
        } else if (!strcmp(cmd, "d") || !strcmp(cmd, "dis")) {
            uint16_t addr = m->cpu.pc.w;
            int n = 8, i;
            if (nf >= 2 && parse_u16(m, a1, &addr) != 0) {
                printf("bad address '%s'\n", a1);
                continue;
            }
            if (nf >= 3) n = atoi(a2);
            if (n < 1 || n > 64) n = 8;
            for (i = 0; i < n; i++)
                addr = (uint16_t)(addr + print_dis_line(m, addr));
        } else if (!strcmp(cmd, "m") || !strcmp(cmd, "mem")) {
            uint16_t addr;
            int n = 64;
            if (nf < 2 || parse_u16(m, a1, &addr) != 0) {
                printf("bad address '%s'\n", a1);
                continue;
            }
            if (nf >= 3) n = atoi(a2);
            if (n < 1 || n > 4096) n = 64;
            dump_mem(m, addr, n);
        } else if (!strcmp(cmd, "b") || !strcmp(cmd, "break")) {
            uint16_t v;
            if (nf < 2) { list_print("breakpoints", d->bp, d->nbp); continue; }
            if (parse_u16(m, a1, &v) != 0) printf("bad address '%s'\n", a1);
            else list_toggle(d->bp, &d->nbp, v, "breakpoint");
        } else if (!strcmp(cmd, "w") || !strcmp(cmd, "watch")) {
            uint16_t v;
            if (nf < 2) { list_print("write watches", d->wwatch, d->nww); continue; }
            if (parse_u16(m, a1, &v) != 0) printf("bad address '%s'\n", a1);
            else list_toggle(d->wwatch, &d->nww, v, "write watch");
        } else if (!strcmp(cmd, "wr") || !strcmp(cmd, "rwatch")) {
            uint16_t v;
            if (nf < 2) { list_print("read watches", d->rwatch, d->nrw); continue; }
            if (parse_u16(m, a1, &v) != 0) printf("bad address '%s'\n", a1);
            else list_toggle(d->rwatch, &d->nrw, v, "read watch");
        } else if (!strcmp(cmd, "wp") || !strcmp(cmd, "pwatch")) {
            uint16_t v;
            if (nf < 2) { list_print("port watches", d->pwatch, d->npw); continue; }
            if (parse_u16(m, a1, &v) != 0) printf("bad port '%s'\n", a1);
            else list_toggle(d->pwatch, &d->npw, v, "port watch");
        } else if (!strcmp(cmd, "t") || !strcmp(cmd, "trace")) {
            if (nf < 2) {
                printf("trace is %s\n", d->trace ? "on" : "off");
            } else if (!strcmp(a1, "off")) {
                if (d->trace) fclose(d->trace);
                d->trace = NULL;
                printf("trace off\n");
            } else {
                FILE *f = fopen(a1, "w");
                if (!f) { printf("cannot open '%s'\n", a1); continue; }
                if (d->trace) fclose(d->trace);
                d->trace = f;
                printf("tracing to %s\n", a1);
            }
        } else if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
            if (d->trace) fclose(d->trace);
            exit(0);
        } else if (!strcmp(cmd, "h") || !strcmp(cmd, "help")) {
            help();
        } else {
            printf("unknown command '%s' (h for help)\n", cmd);
        }
    }
}

/* ---- hooks ---- */

void debug_attach(Machine *m, Debugger *d)
{
    memset(d, 0, sizeof *d);
    d->is_tty = isatty(0);
    m->dbg = d;
}

void debug_step_hook(Machine *m)
{
    Debugger *d = m->dbg;
    int stop = 0;
    uint16_t pc = m->cpu.pc.w;

    /* While parked in HALT each idle cycle re-enters here at the same PC;
     * suppress trace/breakpoints so they fire once per instruction. */
    if (!m->cpu.halted) {
        if (d->trace) trace_line(m);
        if (in_list(d->bp, d->nbp, pc)) {
            printf("*** breakpoint at $%04X\n", pc);
            stop = 1;
        }
    }
    if (d->stop_now) { d->stop_now = 0; stop = 1; }
    if (d->pending[0]) {
        printf("*** %s\n", d->pending);
        d->pending[0] = 0;
        stop = 1;
    }
    if (d->step > 0 && --d->step == 0)
        stop = 1;
    if (stop)
        repl(m);
}

void debug_note_mem_read(Machine *m, uint16_t addr, uint8_t val)
{
    Debugger *d = m->dbg;
    if (d->nrw && !d->pending[0] && in_list(d->rwatch, d->nrw, addr))
        snprintf(d->pending, sizeof d->pending,
                 "watch: read $%02X at $%04X", val, addr);
}

void debug_note_mem_write(Machine *m, uint16_t addr, uint8_t val)
{
    Debugger *d = m->dbg;
    if (d->nww && !d->pending[0] && in_list(d->wwatch, d->nww, addr))
        snprintf(d->pending, sizeof d->pending,
                 "watch: write $%02X to $%04X", val, addr);
}

void debug_note_io(Machine *m, uint16_t port, uint8_t val, int is_write)
{
    Debugger *d = m->dbg;
    int i;
    if (!d->npw || d->pending[0]) return;
    for (i = 0; i < d->npw; i++) {
        uint16_t w = d->pwatch[i];
        if (w <= 0xFF ? ((port & 0xFF) == w) : (port == w)) {
            snprintf(d->pending, sizeof d->pending,
                     "watch: %s port $%04X %s $%02X",
                     is_write ? "OUT" : "IN", port,
                     is_write ? "value" : "returned", val);
            return;
        }
    }
}
