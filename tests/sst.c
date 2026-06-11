/* sst.c — cross-check the Z80 core against the SingleStepTests/z80 JSON
 * vectors: per-instruction initial→final CPU + RAM state, total T-states
 * (the length of the per-T-state "cycles" trace) and I/O transactions.
 *
 * usage: sst FILE.json...        (run tests/get_vectors.sh first)
 *
 * The JSON walker is schema-specific and minimal (objects, arrays,
 * integers, short strings); unknown keys are skipped generically. The
 * vectors' "p" field (LD A,I / LD A,R history) is not modeled by the
 * core and not compared; "q", "wz" and "ei" (EI-pending) are.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/z80.h"

#define MAX_RAM   96
#define MAX_PORTS 8

typedef struct {
    uint16_t pc, sp, wz, ix, iy, af2, bc2, de2, hl2;
    int a, b, c, d, e, f, h, l, i, r, im, iff1, iff2, ei, q;
    struct { uint16_t addr; uint8_t val; } ram[MAX_RAM];
    int nram;
} State;

typedef struct {
    char name[48];
    State ini, fin;
    int ncycles;
    struct { uint16_t addr; uint8_t val; char dir; } ports[MAX_PORTS];
    int nports;
} Test;

/* ---- JSON walker ---- */

typedef struct {
    const char *p, *end, *start;
    const char *file;
} Js;

static void js_die(Js *j, const char *msg)
{
    fprintf(stderr, "%s: JSON error at offset %ld: %s\n",
            j->file, (long)(j->p - j->start), msg);
    exit(2);
}

static void ws(Js *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' ||
                             *j->p == '\r' || *j->p == '\t'))
        j->p++;
}

static int js_peek(Js *j)
{
    ws(j);
    return j->p < j->end ? (unsigned char)*j->p : -1;
}

static int js_eat(Js *j, char c)
{
    ws(j);
    if (j->p < j->end && *j->p == c) { j->p++; return 1; }
    return 0;
}

static void js_expect(Js *j, char c)
{
    if (!js_eat(j, c)) {
        char msg[24];
        snprintf(msg, sizeof msg, "expected '%c'", c);
        js_die(j, msg);
    }
}

static long js_num(Js *j)
{
    long v = 0;
    int neg = 0;
    ws(j);
    if (j->p < j->end && *j->p == '-') { neg = 1; j->p++; }
    if (j->p >= j->end || *j->p < '0' || *j->p > '9')
        js_die(j, "expected number");
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
        v = v * 10 + (*j->p++ - '0');
    return neg ? -v : v;
}

/* Read a string into buf, truncating but always consuming it fully. */
static void js_str(Js *j, char *buf, size_t sz)
{
    size_t n = 0;
    js_expect(j, '"');
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) c = *j->p++;
        if (n + 1 < sz) buf[n++] = c;
    }
    js_expect(j, '"');
    buf[n] = 0;
}

static void js_skip(Js *j)
{
    int c = js_peek(j);
    if (c == '"') {
        char tmp[8];
        js_str(j, tmp, sizeof tmp);
    } else if (c == '{') {
        js_expect(j, '{');
        if (!js_eat(j, '}')) {
            do {
                char key[32];
                js_str(j, key, sizeof key);
                js_expect(j, ':');
                js_skip(j);
            } while (js_eat(j, ','));
            js_expect(j, '}');
        }
    } else if (c == '[') {
        js_expect(j, '[');
        if (js_peek(j) != ']') {
            do js_skip(j); while (js_eat(j, ','));
        }
        js_expect(j, ']');
    } else {                       /* number / true / false / null */
        ws(j);
        while (j->p < j->end &&
               (strchr("-+.eE", *j->p) ||
                (*j->p >= '0' && *j->p <= '9') ||
                (*j->p >= 'a' && *j->p <= 'z')))
            j->p++;
    }
}

static void parse_ram(Js *j, State *s)
{
    js_expect(j, '[');
    if (js_peek(j) != ']') {
        do {
            long a, v;
            js_expect(j, '[');
            a = js_num(j);
            js_expect(j, ',');
            v = js_num(j);
            js_expect(j, ']');
            if (s->nram >= MAX_RAM) js_die(j, "too many ram entries");
            s->ram[s->nram].addr = (uint16_t)a;
            s->ram[s->nram].val = (uint8_t)v;
            s->nram++;
        } while (js_eat(j, ','));
    }
    js_expect(j, ']');
}

static void parse_state(Js *j, State *s)
{
    js_expect(j, '{');
    if (js_eat(j, '}')) return;
    do {
        char key[16];
        js_str(j, key, sizeof key);
        js_expect(j, ':');
        if      (!strcmp(key, "pc"))   s->pc  = (uint16_t)js_num(j);
        else if (!strcmp(key, "sp"))   s->sp  = (uint16_t)js_num(j);
        else if (!strcmp(key, "wz"))   s->wz  = (uint16_t)js_num(j);
        else if (!strcmp(key, "ix"))   s->ix  = (uint16_t)js_num(j);
        else if (!strcmp(key, "iy"))   s->iy  = (uint16_t)js_num(j);
        else if (!strcmp(key, "af_"))  s->af2 = (uint16_t)js_num(j);
        else if (!strcmp(key, "bc_"))  s->bc2 = (uint16_t)js_num(j);
        else if (!strcmp(key, "de_"))  s->de2 = (uint16_t)js_num(j);
        else if (!strcmp(key, "hl_"))  s->hl2 = (uint16_t)js_num(j);
        else if (!strcmp(key, "a"))    s->a   = (int)js_num(j);
        else if (!strcmp(key, "b"))    s->b   = (int)js_num(j);
        else if (!strcmp(key, "c"))    s->c   = (int)js_num(j);
        else if (!strcmp(key, "d"))    s->d   = (int)js_num(j);
        else if (!strcmp(key, "e"))    s->e   = (int)js_num(j);
        else if (!strcmp(key, "f"))    s->f   = (int)js_num(j);
        else if (!strcmp(key, "h"))    s->h   = (int)js_num(j);
        else if (!strcmp(key, "l"))    s->l   = (int)js_num(j);
        else if (!strcmp(key, "i"))    s->i   = (int)js_num(j);
        else if (!strcmp(key, "r"))    s->r   = (int)js_num(j);
        else if (!strcmp(key, "im"))   s->im  = (int)js_num(j);
        else if (!strcmp(key, "iff1")) s->iff1 = (int)js_num(j);
        else if (!strcmp(key, "iff2")) s->iff2 = (int)js_num(j);
        else if (!strcmp(key, "ei"))   s->ei  = (int)js_num(j);
        else if (!strcmp(key, "q"))    s->q   = (int)js_num(j);
        else if (!strcmp(key, "ram"))  parse_ram(j, s);
        else js_skip(j);               /* "p" and anything new */
    } while (js_eat(j, ','));
    js_expect(j, '}');
}

static void parse_ports(Js *j, Test *t)
{
    js_expect(j, '[');
    if (js_peek(j) != ']') {
        do {
            char dir[4];
            long a, v;
            js_expect(j, '[');
            a = js_num(j);
            js_expect(j, ',');
            v = js_num(j);
            js_expect(j, ',');
            js_str(j, dir, sizeof dir);
            js_expect(j, ']');
            if (t->nports >= MAX_PORTS) js_die(j, "too many ports");
            t->ports[t->nports].addr = (uint16_t)a;
            t->ports[t->nports].val = (uint8_t)v;
            t->ports[t->nports].dir = dir[0];
            t->nports++;
        } while (js_eat(j, ','));
    }
    js_expect(j, ']');
}

static int count_cycles(Js *j)
{
    int n = 0;
    js_expect(j, '[');
    if (js_peek(j) != ']') {
        do { js_skip(j); n++; } while (js_eat(j, ','));
    }
    js_expect(j, ']');
    return n;
}

static void parse_test(Js *j, Test *t)
{
    memset(t, 0, sizeof *t);
    js_expect(j, '{');
    do {
        char key[16];
        js_str(j, key, sizeof key);
        js_expect(j, ':');
        if      (!strcmp(key, "name"))    js_str(j, t->name, sizeof t->name);
        else if (!strcmp(key, "initial")) parse_state(j, &t->ini);
        else if (!strcmp(key, "final"))   parse_state(j, &t->fin);
        else if (!strcmp(key, "cycles"))  t->ncycles = count_cycles(j);
        else if (!strcmp(key, "ports"))   parse_ports(j, t);
        else js_skip(j);
    } while (js_eat(j, ','));
    js_expect(j, '}');
}

/* ---- test runner ---- */

static uint8_t mem[65536], want_mem[65536];
static Test *cur;
static struct { uint16_t addr; uint8_t val; char dir; } io_log[MAX_PORTS];
static int n_io;
static int verbose_budget;

static uint8_t cb_mem_read(void *ctx, uint16_t a)
{
    (void)ctx;
    return mem[a];
}

static void cb_mem_write(void *ctx, uint16_t a, uint8_t v)
{
    (void)ctx;
    mem[a] = v;
}

static uint8_t cb_io_read(void *ctx, uint16_t port)
{
    uint8_t v = 0xFF;
    (void)ctx;
    if (n_io < cur->nports && cur->ports[n_io].dir == 'r')
        v = cur->ports[n_io].val;
    if (n_io < MAX_PORTS) {
        io_log[n_io].addr = port;
        io_log[n_io].val = v;
        io_log[n_io].dir = 'r';
    }
    n_io++;
    return v;
}

static void cb_io_write(void *ctx, uint16_t port, uint8_t v)
{
    (void)ctx;
    if (n_io < MAX_PORTS) {
        io_log[n_io].addr = port;
        io_log[n_io].val = v;
        io_log[n_io].dir = 'w';
    }
    n_io++;
}

static int chk(const Test *t, const char *what, long got, long want)
{
    if (got == want) return 0;
    if (verbose_budget > 0) {
        printf("  FAIL %-14s %-8s got=%04lX want=%04lX\n",
               t->name, what, got, want);
        verbose_budget--;
    }
    return 1;
}

static int run_test(Test *t)
{
    Z80 z;
    int i, bad = 0, tt;

    memset(&z, 0, sizeof z);
    z.mem_read = cb_mem_read;
    z.mem_write = cb_mem_write;
    z.io_read = cb_io_read;
    z.io_write = cb_io_write;

    z.pc.w = t->ini.pc;
    z.sp.w = t->ini.sp;
    z.af.b.h = (uint8_t)t->ini.a;
    z.af.b.l = (uint8_t)t->ini.f;
    z.bc.b.h = (uint8_t)t->ini.b;
    z.bc.b.l = (uint8_t)t->ini.c;
    z.de.b.h = (uint8_t)t->ini.d;
    z.de.b.l = (uint8_t)t->ini.e;
    z.hl.b.h = (uint8_t)t->ini.h;
    z.hl.b.l = (uint8_t)t->ini.l;
    z.ix.w = t->ini.ix;
    z.iy.w = t->ini.iy;
    z.af_.w = t->ini.af2;
    z.bc_.w = t->ini.bc2;
    z.de_.w = t->ini.de2;
    z.hl_.w = t->ini.hl2;
    z.i = (uint8_t)t->ini.i;
    z.r = (uint8_t)t->ini.r;
    z.im = (uint8_t)t->ini.im;
    z.iff1 = (uint8_t)t->ini.iff1;
    z.iff2 = (uint8_t)t->ini.iff2;
    z.ei_pending = (uint8_t)t->ini.ei;
    z.q = (uint8_t)t->ini.q;
    z.memptr.w = t->ini.wz;

    memset(mem, 0, sizeof mem);
    for (i = 0; i < t->ini.nram; i++)
        mem[t->ini.ram[i].addr] = t->ini.ram[i].val;
    memcpy(want_mem, mem, sizeof mem);
    for (i = 0; i < t->fin.nram; i++)
        want_mem[t->fin.ram[i].addr] = t->fin.ram[i].val;

    cur = t;
    n_io = 0;
    tt = z80_step(&z);

    bad |= chk(t, "pc", z.pc.w, t->fin.pc);
    bad |= chk(t, "sp", z.sp.w, t->fin.sp);
    bad |= chk(t, "a", z.af.b.h, t->fin.a);
    bad |= chk(t, "f", z.af.b.l, t->fin.f);
    bad |= chk(t, "b", z.bc.b.h, t->fin.b);
    bad |= chk(t, "c", z.bc.b.l, t->fin.c);
    bad |= chk(t, "d", z.de.b.h, t->fin.d);
    bad |= chk(t, "e", z.de.b.l, t->fin.e);
    bad |= chk(t, "h", z.hl.b.h, t->fin.h);
    bad |= chk(t, "l", z.hl.b.l, t->fin.l);
    bad |= chk(t, "ix", z.ix.w, t->fin.ix);
    bad |= chk(t, "iy", z.iy.w, t->fin.iy);
    bad |= chk(t, "af_", z.af_.w, t->fin.af2);
    bad |= chk(t, "bc_", z.bc_.w, t->fin.bc2);
    bad |= chk(t, "de_", z.de_.w, t->fin.de2);
    bad |= chk(t, "hl_", z.hl_.w, t->fin.hl2);
    bad |= chk(t, "i", z.i, t->fin.i);
    bad |= chk(t, "r", z.r, t->fin.r);
    bad |= chk(t, "im", z.im, t->fin.im);
    bad |= chk(t, "iff1", z.iff1, t->fin.iff1);
    bad |= chk(t, "iff2", z.iff2, t->fin.iff2);
    bad |= chk(t, "ei", z.ei_pending, t->fin.ei);
    bad |= chk(t, "q", z.q, t->fin.q);
    bad |= chk(t, "wz", z.memptr.w, t->fin.wz);
    bad |= chk(t, "tstates", tt, t->ncycles);

    if (memcmp(mem, want_mem, sizeof mem) != 0) {
        if (verbose_budget > 0) {
            for (i = 0; i < 65536; i++)
                if (mem[i] != want_mem[i]) break;
            printf("  FAIL %-14s mem[$%04X] got=%02X want=%02X\n",
                   t->name, i, mem[i], want_mem[i]);
            verbose_budget--;
        }
        bad = 1;
    }

    if (n_io != t->nports) {
        bad |= chk(t, "io-count", n_io, t->nports);
    } else {
        for (i = 0; i < n_io && i < MAX_PORTS; i++) {
            bad |= chk(t, "io-addr", io_log[i].addr, t->ports[i].addr);
            bad |= chk(t, "io-val", io_log[i].val, t->ports[i].val);
            bad |= chk(t, "io-dir", io_log[i].dir, t->ports[i].dir);
        }
    }
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    int fi, total = 0, failed = 0, files_bad = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s FILE.json...\n", argv[0]);
        return 2;
    }
    for (fi = 1; fi < argc; fi++) {
        FILE *f = fopen(argv[fi], "rb");
        long sz;
        char *buf;
        Js j;
        Test t;
        int n = 0, bad = 0;

        if (!f) {
            fprintf(stderr, "cannot open %s\n", argv[fi]);
            return 2;
        }
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = malloc((size_t)sz + 1);
        if (!buf || (long)fread(buf, 1, (size_t)sz, f) != sz) {
            fprintf(stderr, "cannot read %s\n", argv[fi]);
            return 2;
        }
        fclose(f);
        buf[sz] = 0;

        j.p = j.start = buf;
        j.end = buf + sz;
        j.file = argv[fi];
        verbose_budget = 5;             /* detail lines per file */

        js_expect(&j, '[');
        do {
            parse_test(&j, &t);
            bad += run_test(&t);
            n++;
        } while (js_eat(&j, ','));
        js_expect(&j, ']');
        free(buf);

        total += n;
        failed += bad;
        if (bad) {
            printf("%-32s %d/%d FAILED\n", argv[fi], bad, n);
            files_bad++;
        }
    }
    printf("sst: %d files, %d tests, %d failed%s\n",
           argc - 1, total, failed, failed ? "" : " - all OK");
    return failed ? 1 : 0;
}
