/* z80.h — Z80 CPU core interface for the HC-91 emulator.
 *
 * Little-endian register pairs. The core is bus-agnostic: memory and I/O
 * access goes through the callback functions in struct Z80.
 */
#ifndef HC91_Z80_H
#define HC91_Z80_H

#include <stdint.h>

typedef union {
    struct { uint8_t l, h; } b;   /* little-endian host assumed (x86) */
    uint16_t w;
} RegPair;

typedef struct Z80 Z80;

struct Z80 {
    /* Register file */
    RegPair af, bc, de, hl;       /* main set; af.b.h = A, af.b.l = F */
    RegPair af_, bc_, de_, hl_;   /* alternate set */
    RegPair ix, iy, sp, pc;
    RegPair memptr;               /* internal WZ register */
    uint8_t i, r;                 /* r: full 8-bit, refresh uses low 7 */
    uint8_t iff1, iff2, im;
    uint8_t halted;               /* 1 while executing HALT */
    uint8_t ei_pending;           /* set by EI; blocks INT for one instr */
    uint8_t q;                    /* internal Q register: F if previous
                                     instruction modified flags, else 0
                                     (affects SCF/CCF X/Y flags) */

    uint64_t tstates;             /* running T-state counter */

    /* Bus callbacks (must all be non-NULL) */
    void *ctx;
    uint8_t (*mem_read)(void *ctx, uint16_t addr);
    void    (*mem_write)(void *ctx, uint16_t addr, uint8_t val);
    uint8_t (*io_read)(void *ctx, uint16_t port);
    void    (*io_write)(void *ctx, uint16_t port, uint8_t val);

    /* Optional contention callbacks (NULL = uncontended machine).
     * The core charges T-states incrementally during execution: each
     * memory access calls mem_contend first and adds the returned delay;
     * I/O cycles call io_contend_early before their first T-state and
     * io_contend_late before the value transfer (the callee may inspect
     * z->tstates, which is up to date at call time). */
    int (*mem_contend)(void *ctx, uint16_t addr);
    int (*io_contend_early)(void *ctx, uint16_t port);
    int (*io_contend_late)(void *ctx, uint16_t port);
};

/* Flag bits in F */
#define ZF_C  0x01
#define ZF_N  0x02
#define ZF_PV 0x04
#define ZF_X  0x08   /* undocumented bit 3 */
#define ZF_H  0x10
#define ZF_Y  0x20   /* undocumented bit 5 */
#define ZF_Z  0x40
#define ZF_S  0x80

/* Power-on / RESET state. Does not touch callbacks/ctx/tstates. */
void z80_reset(Z80 *z);

/* Execute one instruction (or one HALT cycle). Returns T-states consumed
 * and adds them to z->tstates. Honors ei_pending semantics. */
int z80_step(Z80 *z);

/* Maskable interrupt request with the given data-bus value (0xFF on the
 * HC-91/Spectrum). If accepted (IFF1 set, not immediately after EI),
 * services it per IM 0/1/2, returns T-states consumed (added to
 * z->tstates) ; returns 0 if ignored. */
int z80_int(Z80 *z, uint8_t bus);

/* Non-maskable interrupt: always accepted. Returns T-states consumed. */
int z80_nmi(Z80 *z);

#endif
