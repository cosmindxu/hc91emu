/* debug.h — monitor/debugger: breakpoints, watchpoints, single-step,
 * disassembly, memory dump, per-instruction trace-to-file.
 *
 * Command input comes from a script string (--debug "cmd;cmd;..."), then
 * stdin (prompting only on a tty); when both are exhausted the monitor
 * auto-continues so headless runs never hang.
 */
#ifndef HC91_DEBUG_H
#define HC91_DEBUG_H

#include <stdint.h>
#include <stdio.h>

struct Machine;

#define DBG_MAX_BP 32

typedef struct Debugger {
    uint16_t bp[DBG_MAX_BP];     int nbp;    /* PC breakpoints */
    uint16_t wwatch[DBG_MAX_BP]; int nww;    /* memory write watches */
    uint16_t rwatch[DBG_MAX_BP]; int nrw;    /* memory read watches */
    uint16_t pwatch[DBG_MAX_BP]; int npw;    /* I/O port watches */
    int step;                 /* stop after N more instructions (0 = off) */
    int stop_now;             /* enter monitor before the next instruction */
    char pending[96];         /* watch-hit message ("" = none) */
    FILE *trace;              /* per-instruction trace stream (or NULL) */
    const char *script;       /* remaining scripted commands (or NULL) */
    int is_tty, stdin_eof;
    char last[128];           /* last command (empty input repeats it) */
} Debugger;

/* Zero d, detect tty, point m->dbg at it. Configure fields afterwards. */
void debug_attach(struct Machine *m, Debugger *d);

/* Hooks called by machine.c. step_hook runs before every instruction
 * (trace + stop decisions); the note_* functions run on bus activity and
 * arm a stop that fires before the next instruction. */
void debug_step_hook(struct Machine *m);
void debug_note_mem_read(struct Machine *m, uint16_t addr, uint8_t val);
void debug_note_mem_write(struct Machine *m, uint16_t addr, uint8_t val);
void debug_note_io(struct Machine *m, uint16_t port, uint8_t val,
                   int is_write);

#endif
