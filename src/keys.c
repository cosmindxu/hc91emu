/* keys.c — scheduled keyboard events, text typing, raw chords. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "machine.h"

#define HOLD_FRAMES 6
#define GAP_FRAMES  6

/* Matrix positions. Row index = zero-bit position in port high byte. */
#define ROW_CAPS 0
#define BIT_CAPS 0
#define ROW_SYM  7
#define BIT_SYM  1

/* a..z -> {row, bit} */
static const uint8_t letter_pos[26][2] = {
    {1,0}, /* a */ {7,4}, /* b */ {0,3}, /* c */ {1,2}, /* d */
    {2,2}, /* e */ {1,3}, /* f */ {1,4}, /* g */ {6,4}, /* h */
    {5,2}, /* i */ {6,3}, /* j */ {6,2}, /* k */ {6,1}, /* l */
    {7,2}, /* m */ {7,3}, /* n */ {5,1}, /* o */ {5,0}, /* p */
    {2,0}, /* q */ {2,3}, /* r */ {1,1}, /* s */ {2,4}, /* t */
    {5,3}, /* u */ {0,4}, /* v */ {2,1}, /* w */ {0,2}, /* x */
    {5,4}, /* y */ {0,1}  /* z */
};

static int digit_pos(int d, int *row, int *bit)
{
    if (d >= 1 && d <= 5) { *row = 3; *bit = d - 1; return 0; }
    if (d == 0)           { *row = 4; *bit = 0;     return 0; }
    if (d >= 6 && d <= 9) { *row = 4; *bit = 10 - d; return 0; }
    return -1;
}

/* SYMSHIFT-ed symbols: symbol -> the key pressed together with SYM. */
static int sym_pos(char c, int *row, int *bit)
{
    char key;
    switch (c) {
    case '+':  key = 'k'; break;
    case '-':  key = 'j'; break;
    case '*':  key = 'b'; break;
    case '/':  key = 'v'; break;
    case '=':  key = 'l'; break;
    case '<':  key = 'r'; break;
    case '>':  key = 't'; break;
    case ',':  key = 'n'; break;
    case '.':  key = 'm'; break;
    case ';':  key = 'o'; break;
    case '"':  key = 'p'; break;
    case ':':  key = 'z'; break;
    case '?':  key = 'c'; break;
    case '!':  key = '1'; break;
    case '@':  key = '2'; break;
    case '#':  key = '3'; break;
    case '$':  key = '4'; break;
    case '%':  key = '5'; break;
    case '&':  key = '6'; break;
    case '\'': key = '7'; break;
    case '(':  key = '8'; break;
    case ')':  key = '9'; break;
    case '_':  key = '0'; break;
    default:   return -1;
    }
    if (key >= '0' && key <= '9')
        return digit_pos(key - '0', row, bit);
    *row = letter_pos[key - 'a'][0];
    *bit = letter_pos[key - 'a'][1];
    return 0;
}

static void add_event(Machine *m, int fstart, int fend, int row, int bit)
{
    KeyEvent *e;
    if (m->nkey_events >= HC91_MAX_KEY_EVENTS) {
        fprintf(stderr, "warning: key event queue full, event dropped\n");
        return;
    }
    e = &m->key_events[m->nkey_events++];
    e->frame_start = fstart;
    e->frame_end = fend;
    e->row = (uint8_t)row;
    e->mask = (uint8_t)(1u << bit);
}

void keys_apply(Machine *m, int frame)
{
    int i;
    memset(m->keyrows, 0, sizeof(m->keyrows));
    for (i = 0; i < m->nkey_events; i++) {
        const KeyEvent *e = &m->key_events[i];
        if (frame >= e->frame_start && frame < e->frame_end)
            m->keyrows[e->row] |= e->mask;
    }
    m->kempston = 0;
    for (i = 0; i < m->njoy_events; i++) {
        const JoyEvent *e = &m->joy_events[i];
        if (frame >= e->frame_start && frame < e->frame_end)
            m->kempston |= e->mask;
    }
}

/* Schedule joystick input for frames [f0, f1). Direction string: chars
 * from U D L R F. Kempston sets port-0x1F bits; Sinclair (Interface 2,
 * stick 1: keys 6-0) and cursor (5/8/6/7/0) press matrix keys. */
int keys_joy(Machine *m, int f0, int f1, const char *dirs)
{
    static const uint8_t kemp_bit[5] = { 0x08, 0x04, 0x02, 0x01, 0x10 };
    static const char sinclair_key[5] = { '9', '8', '6', '7', '0' };
    static const char cursor_key[5]   = { '7', '6', '5', '8', '0' };
    const char *p;

    for (p = dirs; *p; p++) {
        int d, row, bit;
        switch (toupper((unsigned char)*p)) {
        case 'U': d = 0; break;
        case 'D': d = 1; break;
        case 'L': d = 2; break;
        case 'R': d = 3; break;
        case 'F': d = 4; break;
        case '+': continue;
        default:
            fprintf(stderr, "error: unknown joystick direction '%c'\n", *p);
            return -1;
        }
        if (m->joy_type == JOY_KEMPSTON) {
            JoyEvent *e;
            if (m->njoy_events >= HC91_MAX_JOY_EVENTS) {
                fprintf(stderr, "warning: joystick event queue full\n");
                return 0;
            }
            e = &m->joy_events[m->njoy_events++];
            e->frame_start = f0;
            e->frame_end = f1;
            e->mask = kemp_bit[d];
            m->kempston_enabled = 1;
        } else {
            char key = (m->joy_type == JOY_SINCLAIR) ? sinclair_key[d]
                                                     : cursor_key[d];
            if (digit_pos(key - '0', &row, &bit) == 0)
                add_event(m, f0, f1, row, bit);
        }
    }
    return 0;
}

void keys_type(Machine *m, const char *text, int start_frame)
{
    int frame = start_frame;
    const char *p;

    for (p = text; *p; p++) {
        char c = *p;
        int row, bit;
        int fend = frame + HOLD_FRAMES;

        if (c >= 'a' && c <= 'z') {
            add_event(m, frame, fend, letter_pos[c - 'a'][0],
                      letter_pos[c - 'a'][1]);
        } else if (c >= 'A' && c <= 'Z') {
            add_event(m, frame, fend, ROW_CAPS, BIT_CAPS);
            add_event(m, frame, fend, letter_pos[c - 'A'][0],
                      letter_pos[c - 'A'][1]);
        } else if (c >= '0' && c <= '9') {
            digit_pos(c - '0', &row, &bit);
            add_event(m, frame, fend, row, bit);
        } else if (c == ' ') {
            add_event(m, frame, fend, 7, 0);
        } else if (c == '\n') {
            add_event(m, frame, fend, 6, 0);
        } else if (sym_pos(c, &row, &bit) == 0) {
            add_event(m, frame, fend, ROW_SYM, BIT_SYM);
            add_event(m, frame, fend, row, bit);
        } else {
            fprintf(stderr, "warning: cannot type character '%c' (0x%02x)\n",
                    c, (unsigned char)c);
            continue;
        }
        frame = fend + GAP_FRAMES;
    }
}

/* One key name -> row/bit. Accepts A-Z, a-z, 0-9, ENTER, SPACE,
 * CAPS (CS, SHIFT), SYM (SS, SYMSHIFT). */
static int name_pos(const char *name, int len, int *row, int *bit)
{
    char up[16];
    int i;

    if (len <= 0 || len >= (int)sizeof(up))
        return -1;
    for (i = 0; i < len; i++)
        up[i] = (char)toupper((unsigned char)name[i]);
    up[len] = '\0';

    if (len == 1) {
        if (up[0] >= 'A' && up[0] <= 'Z') {
            *row = letter_pos[up[0] - 'A'][0];
            *bit = letter_pos[up[0] - 'A'][1];
            return 0;
        }
        if (up[0] >= '0' && up[0] <= '9')
            return digit_pos(up[0] - '0', row, bit);
        return -1;
    }
    if (!strcmp(up, "ENTER"))  { *row = 6; *bit = 0; return 0; }
    if (!strcmp(up, "SPACE"))  { *row = 7; *bit = 0; return 0; }
    if (!strcmp(up, "CAPS") || !strcmp(up, "CS") || !strcmp(up, "SHIFT")) {
        *row = ROW_CAPS; *bit = BIT_CAPS; return 0;
    }
    if (!strcmp(up, "SYM") || !strcmp(up, "SS") || !strcmp(up, "SYMSHIFT")) {
        *row = ROW_SYM; *bit = BIT_SYM; return 0;
    }
    return -1;
}

int keys_raw(Machine *m, int frame, const char *names)
{
    const char *p = names;

    while (*p) {
        const char *start = p;
        int len, row, bit;

        while (*p && *p != '+')
            p++;
        len = (int)(p - start);
        if (name_pos(start, len, &row, &bit) != 0) {
            fprintf(stderr, "error: unknown key name '%.*s'\n", len, start);
            return -1;
        }
        add_event(m, frame, frame + HOLD_FRAMES, row, bit);
        if (*p == '+')
            p++;
    }
    return 0;
}
