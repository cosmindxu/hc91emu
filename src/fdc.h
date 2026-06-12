/* fdc.h — uPD765/i8272 floppy controller + HC "IF1" disk interface.
 *
 * The Romanian HC disk interface ("Interfața 1") puts an i8272-class
 * FDC (MMN8272) at:
 *   0x85  main status register (read)
 *   0x87  data register (read/write)
 *   0x05/0x07 (mask 0xFD = 0x05): control latch — bit 0 TC, bit 1
 *   drive 0 select+motor, bit 2 drive 1, bit 3 armed, bit 4 /reset;
 *   reading returns bit 0 low while a motor runs.
 * (Derived from the interface ROM disassembly; the port decode and
 * latch semantics match Alex Badea's FUSE hc-if1 branch.)
 *
 * The controller is polled (no DMA/interrupt line on this interface);
 * seeks complete instantly with the result delivered via SENSE
 * INTERRUPT STATUS, and data transfers are byte-polled through MSR
 * RQM/DIO as on the real chip.
 */
#ifndef HC91_FDC_H
#define HC91_FDC_H

#include <stdint.h>

struct Machine;

#define FDC_DRIVES 2

typedef struct FdcDrive {
    uint8_t *img;            /* raw disk image (malloc'd) */
    long size;
    int tracks, heads, spt, secsz, szcode;
    int track;               /* current head position */
    int dirty;
    char *path;              /* for write-back */
} FdcDrive;

typedef struct Fdc {
    int phase;               /* 0 idle, 1 command, 2 exec, 3 result */
    uint8_t cmd[9];
    int cmdlen, cmdneed;
    uint8_t res[7];
    int reslen, respos;
    uint8_t *xfer;           /* exec-phase data (points into img) */
    long xlen, xpos;
    int xwrite;
    int int_pending;         /* SENSE INTERRUPT has something to say */
    uint8_t int_st0;
    uint8_t st0, st1, st2;
    int drive, head;
    int idcnt;               /* READ ID rotation */
    uint8_t latch;           /* last control-latch write */
    FdcDrive drv[FDC_DRIVES];
} Fdc;

int  fdc_insert(Fdc *f, int drive, const char *path);  /* 0 ok */
void fdc_flush(Fdc *f);          /* write back dirty images */
void fdc_free(Fdc *f);
void fdc_reset(Fdc *f);

uint8_t fdc_status(Fdc *f);                /* port 0x85 read */
uint8_t fdc_data_read(Fdc *f);             /* port 0x87 read */
void    fdc_data_write(Fdc *f, uint8_t v); /* port 0x87 write */
uint8_t fdc_sel_read(const Fdc *f);        /* port 0x05/0x07 read */
void    fdc_sel_write(Fdc *f, uint8_t v);  /* port 0x05/0x07 write */

#endif
