/* fdc.c — uPD765/i8272 floppy disk controller (polled mode) for the HC
 * disk interface. See fdc.h for the port map. Implemented commands:
 * SPECIFY, SENSE DRIVE STATUS, RECALIBRATE, SEEK, SENSE INTERRUPT
 * STATUS, READ ID, READ/WRITE DATA (multi-sector, ended by TC or EOT),
 * FORMAT TRACK; anything else returns the invalid-command ST0 (0x80).
 * Seeks are instantaneous; transfers are byte-polled via MSR RQM/DIO.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdc.h"

static int fdc_debug = -1;

/* MSR bits */
#define MSR_RQM 0x80
#define MSR_DIO 0x40
#define MSR_EXM 0x20
#define MSR_CB  0x10

/* known raw image geometries */
static const struct { long size; int t, h, s, sz; } geos[] = {
    { 655360, 80, 2, 16, 256 },      /* HC BASIC 3.5"  640K */
    { 737280, 80, 2,  9, 512 },      /* CP/M 2.2 3.5"  720K */
    { 327680, 40, 2, 16, 256 },      /* HC BASIC 5.25" 320K */
    { 368640, 40, 2,  9, 512 },      /* CP/M 2.2 5.25" 360K */
};

int fdc_insert(Fdc *f, int drive, const char *path)
{
    FILE *fp;
    long size;
    FdcDrive *d;
    size_t i;

    if (drive < 0 || drive >= FDC_DRIVES)
        return -1;
    d = &f->drv[drive];
    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "error: cannot open disk image '%s'\n", path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    for (i = 0; i < sizeof geos / sizeof geos[0]; i++)
        if (geos[i].size == size)
            break;
    if (i == sizeof geos / sizeof geos[0]) {
        fprintf(stderr, "error: '%s': unknown disk image size %ld "
                "(known: 640K/720K 80-track, 320K/360K 40-track raw)\n",
                path, size);
        fclose(fp);
        return -1;
    }
    free(d->img);
    free(d->path);
    d->img = malloc((size_t)size);
    if (!d->img || (long)fread(d->img, 1, (size_t)size, fp) != size) {
        fprintf(stderr, "error: cannot read '%s'\n", path);
        fclose(fp);
        free(d->img);
        d->img = NULL;
        return -1;
    }
    fclose(fp);
    d->size = size;
    d->tracks = geos[i].t;
    d->heads = geos[i].h;
    d->spt = geos[i].s;
    d->secsz = geos[i].sz;
    d->szcode = geos[i].sz == 128 ? 0 : geos[i].sz == 256 ? 1
              : geos[i].sz == 512 ? 2 : 3;
    d->track = 0;
    d->dirty = 0;
    d->path = malloc(strlen(path) + 1);
    if (d->path)
        strcpy(d->path, path);
    fprintf(stderr, "fdc: drive %d: %s (%d tracks x %d heads x %d x %d)\n",
            drive, path, d->tracks, d->heads, d->spt, d->secsz);
    return 0;
}

void fdc_flush(Fdc *f)
{
    int i;
    for (i = 0; i < FDC_DRIVES; i++) {
        FdcDrive *d = &f->drv[i];
        if (d->img && d->dirty && d->path) {
            FILE *fp = fopen(d->path, "wb");
            if (fp) {
                fwrite(d->img, 1, (size_t)d->size, fp);
                fclose(fp);
                fprintf(stderr, "fdc: drive %d: wrote back %s\n",
                        i, d->path);
            }
            d->dirty = 0;
        }
    }
}

void fdc_free(Fdc *f)
{
    int i;
    fdc_flush(f);
    for (i = 0; i < FDC_DRIVES; i++) {
        free(f->drv[i].img);
        free(f->drv[i].path);
        f->drv[i].img = NULL;
        f->drv[i].path = NULL;
    }
}

void fdc_reset(Fdc *f)
{
    f->phase = 0;
    f->cmdlen = f->cmdneed = 0;
    f->reslen = f->respos = 0;
    f->xlen = f->xpos = 0;
    f->xwrite = 0;
    f->int_pending = 0;
}

uint8_t fdc_status(Fdc *f)
{
    switch (f->phase) {
    case 1:  return MSR_RQM | MSR_CB;
    case 2:  return (uint8_t)(MSR_RQM | MSR_EXM | MSR_CB
                              | (f->xwrite ? 0 : MSR_DIO));
    case 3:  return MSR_RQM | MSR_DIO | MSR_CB;
    default: return MSR_RQM;
    }
}

/* ---- command execution ---- */

static FdcDrive *cur_drive(Fdc *f)
{
    return &f->drv[f->drive & 1];
}

static void result_phase(Fdc *f, int n)
{
    f->phase = 3;
    f->reslen = n;
    f->respos = 0;
}

/* Standard 7-byte result: ST0 ST1 ST2 C H R N. */
static void result_chrn(Fdc *f, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    f->res[0] = f->st0;
    f->res[1] = f->st1;
    f->res[2] = f->st2;
    f->res[3] = c;
    f->res[4] = h;
    f->res[5] = r;
    f->res[6] = n;
    result_phase(f, 7);
    if (fdc_debug > 0)
        fprintf(stderr, "fdc: res %02X %02X %02X chrn %02X %02X %02X %02X"
                " (xpos %ld/%ld)\n", f->res[0], f->res[1], f->res[2],
                c, h, r, n, f->xpos, f->xlen);
}

/* Set up the exec phase for READ/WRITE DATA. */
static void xfer_start(Fdc *f, int write)
{
    FdcDrive *d = cur_drive(f);
    uint8_t c = f->cmd[2], h = f->cmd[3], r = f->cmd[4], n = f->cmd[5];
    uint8_t eot = f->cmd[6];
    long off, nsect;

    f->drive = f->cmd[1] & 3;
    f->head = (f->cmd[1] >> 2) & 1;
    d = cur_drive(f);
    f->st0 = (uint8_t)(f->drive | (f->head << 2));
    f->st1 = 0;
    f->st2 = 0;

    if (!d->img || c >= d->tracks || h >= d->heads
        || r < 1 || r > d->spt || n != d->szcode) {
        f->st0 |= 0x40;                       /* abnormal termination */
        f->st1 |= d->img ? 0x04 : 0x08;       /* no data / not ready */
        result_chrn(f, c, h, r, n);
        return;
    }
    if (eot < r)
        eot = r;
    if (eot > d->spt)
        eot = (uint8_t)d->spt;
    nsect = eot - r + 1;
    off = (((long)c * d->heads + h) * d->spt + (r - 1)) * d->secsz;
    f->xfer = d->img + off;
    f->xlen = nsect * d->secsz;
    f->xpos = 0;
    f->xwrite = write;
    if (write)
        d->dirty = 1;
    f->phase = 2;
}

/* End the exec phase (TC pulse or data exhausted). */
static void xfer_end(Fdc *f, int tc)
{
    FdcDrive *d = cur_drive(f);
    uint8_t c = f->cmd[2], h = f->cmd[3], n = f->cmd[5];
    long done = (f->xpos + d->secsz - 1) / d->secsz;
    uint8_t r = (uint8_t)(f->cmd[4] + done);

    if (!tc) {
        f->st0 |= 0x40;                       /* ran past EOT */
        f->st1 |= 0x80;                       /* end of cylinder */
    }
    if (r > d->spt) {
        r = 1;
        c++;
    }
    result_chrn(f, c, h, r, n);
}

static void cmd_done(Fdc *f)
{
    uint8_t op = (uint8_t)(f->cmd[0] & 0x1F);
    FdcDrive *d;

    if (fdc_debug < 0)
        fdc_debug = getenv("HC91_FDC_DEBUG") != NULL;
    if (fdc_debug) {
        int i;
        fprintf(stderr, "fdc: cmd");
        for (i = 0; i < f->cmdlen; i++)
            fprintf(stderr, " %02X", f->cmd[i]);
        fprintf(stderr, "\n");
    }

    switch (op) {
    case 0x03:                                /* SPECIFY */
        f->phase = 0;
        break;
    case 0x04:                                /* SENSE DRIVE STATUS */
        f->drive = f->cmd[1] & 3;
        f->head = (f->cmd[1] >> 2) & 1;
        d = cur_drive(f);
        f->res[0] = (uint8_t)(f->drive | (f->head << 2) | 0x20
                              | (d->img && d->track == 0 ? 0x10 : 0)
                              | (d->heads > 1 ? 0x08 : 0));
        result_phase(f, 1);
        break;
    case 0x07:                                /* RECALIBRATE */
        f->drive = f->cmd[1] & 3;
        cur_drive(f)->track = 0;
        f->int_pending = 1;
        f->int_st0 = (uint8_t)(0x20 | f->drive);
        f->phase = 0;
        break;
    case 0x0F:                                /* SEEK */
        f->drive = f->cmd[1] & 3;
        d = cur_drive(f);
        d->track = f->cmd[2] < d->tracks || !d->img ? f->cmd[2]
                                                    : d->tracks - 1;
        f->int_pending = 1;
        f->int_st0 = (uint8_t)(0x20 | f->drive);
        f->phase = 0;
        break;
    case 0x08:                                /* SENSE INTERRUPT STATUS */
        if (f->int_pending) {
            f->int_pending = 0;
            f->res[0] = f->int_st0;
            f->res[1] = (uint8_t)cur_drive(f)->track;
            result_phase(f, 2);
        } else {
            f->res[0] = 0x80;
            result_phase(f, 1);
        }
        break;
    case 0x0A:                                /* READ ID */
        f->drive = f->cmd[1] & 3;
        f->head = (f->cmd[1] >> 2) & 1;
        d = cur_drive(f);
        f->st0 = (uint8_t)(f->drive | (f->head << 2));
        f->st1 = 0;
        f->st2 = 0;
        if (!d->img) {
            f->st0 |= 0x40;
            f->st1 |= 0x08;
            result_chrn(f, 0, 0, 0, 0);
        } else {
            result_chrn(f, (uint8_t)d->track, (uint8_t)f->head,
                        (uint8_t)((f->idcnt++ % d->spt) + 1),
                        (uint8_t)d->szcode);
        }
        break;
    case 0x06: case 0x0C:                     /* READ (DELETED) DATA */
        xfer_start(f, 0);
        break;
    case 0x05: case 0x09:                     /* WRITE (DELETED) DATA */
        xfer_start(f, 1);
        break;
    case 0x0D: {                              /* FORMAT TRACK */
        uint8_t n = f->cmd[2], filler = f->cmd[5];
        f->drive = f->cmd[1] & 3;
        f->head = (f->cmd[1] >> 2) & 1;
        d = cur_drive(f);
        f->st0 = (uint8_t)(f->drive | (f->head << 2));
        f->st1 = 0;
        f->st2 = 0;
        if (d->img && f->head < d->heads && n == d->szcode) {
            long off = (((long)d->track * d->heads + f->head)
                        * d->spt) * d->secsz;
            memset(d->img + off, filler, (size_t)(d->spt * d->secsz));
            d->dirty = 1;
        } else {
            f->st0 |= 0x40;
        }
        /* swallow the ID stream the CPU sends: 4 bytes per sector */
        f->xfer = NULL;
        f->xlen = 4L * (f->cmd[3] ? f->cmd[3] : (d->img ? d->spt : 9));
        f->xpos = 0;
        f->xwrite = 1;
        f->phase = 2;
        break;
    }
    default:                                  /* invalid */
        f->res[0] = 0x80;
        result_phase(f, 1);
        break;
    }
}

static const int cmd_len[32] = {
    /* 0x00 */ 1, 1, 9, 3, 2, 9, 9, 2,
    /* 0x08 */ 1, 9, 2, 1, 9, 6, 1, 3,
    /* 0x10 */ 1, 9, 1, 1, 1, 1, 1, 1,
    /* 0x18 */ 1, 9, 1, 1, 1, 9, 1, 1
};

void fdc_data_write(Fdc *f, uint8_t v)
{
    if (f->phase == 2 && f->xwrite) {         /* exec: data from CPU */
        if (!f->xfer) {                       /* FORMAT: swallow IDs */
            if (++f->xpos >= f->xlen)
                result_chrn(f, (uint8_t)cur_drive(f)->track,
                            (uint8_t)f->head, 1, f->cmd[2]);
            return;
        }
        if (f->xpos < f->xlen) {
            f->xfer[f->xpos++] = v;
            /* transfer complete: wait for TC (normal end) rather than
             * ending here — the driver raises TC after the last byte */
        } else {
            xfer_end(f, 0);                   /* wrote past end: EN */
        }
        return;
    }
    if (f->phase == 0) {
        f->phase = 1;
        f->cmdlen = 0;
        f->cmdneed = cmd_len[v & 0x1F];
    }
    if (f->phase == 1) {
        f->cmd[f->cmdlen++] = v;
        if (f->cmdlen >= f->cmdneed)
            cmd_done(f);
    }
}

uint8_t fdc_data_read(Fdc *f)
{
    uint8_t v = 0xFF;
    if (f->phase == 2 && !f->xwrite) {        /* exec: data to CPU */
        if (f->xpos < f->xlen)
            return f->xfer[f->xpos++];
        /* read past the prepared data without TC: end-of-cylinder */
        xfer_end(f, 0);
        return v;
    }
    if (f->phase == 3) {
        v = f->res[f->respos++];
        if (f->respos >= f->reslen)
            f->phase = 0;
    }
    return v;
}

uint8_t fdc_sel_read(const Fdc *f)
{
    /* bit 0 low while a motor runs (armed + a drive selected) */
    int motor = (f->latch & 0x08) && (f->latch & 0x06);
    return (uint8_t)(motor ? 0xFE : 0xFF);
}

void fdc_sel_write(Fdc *f, uint8_t v)
{
    if (!(v & 0x10))                          /* /reset */
        fdc_reset(f);
    if ((v & 1) && f->phase == 2)             /* TC ends the transfer */
        xfer_end(f, 1);
    f->latch = v;
}
