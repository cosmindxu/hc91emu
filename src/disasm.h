/* disasm.h — Z80 disassembler (all opcodes incl. undocumented). */
#ifndef HC91_DISASM_H
#define HC91_DISASM_H

#include <stdint.h>
#include <stddef.h>

/* Disassemble the instruction at addr, reading memory through peek().
 * Writes the mnemonic into out (outsz >= 40 recommended; truncated when
 * smaller) and returns the instruction length in bytes (1..4). */
typedef uint8_t (*z80_peek_fn)(void *ctx, uint16_t addr);
int z80_disasm(z80_peek_fn peek, void *ctx, uint16_t addr,
               char *out, size_t outsz);

#endif
