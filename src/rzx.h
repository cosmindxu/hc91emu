/* rzx.h — RZX input-recording record/playback state. */
#ifndef HC91_RZX_H
#define HC91_RZX_H

#include <stdint.h>
#include <stddef.h>

struct Machine;

typedef struct RzxFrame {
    uint16_t fetches;            /* R/M1 fetches in this frame */
    uint16_t nin;                /* recorded port reads */
    uint32_t in_off;             /* offset of values in the pool */
} RzxFrame;

typedef struct Rzx {
    /* playback */
    RzxFrame *frames;
    uint32_t nframes, cur;
    uint8_t *pool;               /* frame stream (holds the IN values) */
    uint32_t in_pos;             /* reads consumed in current frame */
    int playing;
    int desync;                  /* warned about under/overrun */
    /* recording */
    int recording;
    const char *rec_path;
    uint64_t fetch_base;
    uint8_t *rin;                /* current frame's IN values */
    size_t rin_n, rin_cap;
    uint8_t *fdata;              /* serialized frames so far */
    size_t fdata_n, fdata_cap;
    uint32_t rec_frames;
    uint8_t *snap;               /* .z80 of the state at record start */
    size_t snap_len;
} Rzx;

/* recording (call start before the first frame, finish after the last) */
int  rzx_record_start(struct Machine *m, const char *path);
void rzx_frame_begin(struct Machine *m);      /* no-op unless recording */
void rzx_log_in(struct Machine *m, uint8_t v);
void rzx_frame_end(struct Machine *m);
int  rzx_record_finish(struct Machine *m);

/* playback */
int  rzx_load(struct Machine *m, const char *path);
int  rzx_playing(const struct Machine *m);
uint8_t rzx_in(struct Machine *m);
void rzx_play_frame(struct Machine *m);

void rzx_free(struct Machine *m);

#endif
