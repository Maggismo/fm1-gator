/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 npi3pak
 * Call/answer generator added by Maggismo, 2026 (Maggismo/fm1-gator) */
#include "macro.h"

#define PRESETS 16u
#define BANK_MAGIC 0x4D43524Fu
#define BANK_VERSION 3u                          /* (2: the call/answer generator, after the presets) */
#define KEY_VELOCITY 100u
#define SCOPE_N 240u
#define ROW_H 28u
#define DRAWS_PER_FRAME 2u
#define CPU_GUARD_Q8 218u

enum { PAGE_MAIN, PAGE_ATTN, PAGE_LPG, PAGE_ENV, PAGE_LFO, PAGE_MATRIX, PAGE_GLOBAL, PAGE_SAVE, PAGE_GEN, PAGES };
enum { FMT_100, FMT_BIP, FMT_INT, FMT_SEMI, FMT_ENUM, FMT_ONOFF, FMT_CHANNEL, FMT_TIME };
enum { D_HEADER = 0, D_ROW0 = 1, D_ENC = 12, D_SCOPE = 13, D_STATUS = 14, D_ALL = 0x7FFFu };

typedef struct {
    const char *label;
    int16_t *value;
    int16_t lo, hi, step;
    uint8_t fmt;
    const char *const *names;
} param_t;

typedef struct {
    const char *title;
    param_t knob[4];
    param_t enc[2];
} page_t;

/* ------------------------------------------------ the call/answer generator ---
 * A short figure, two beats long (or four): a call, high, short and open (a fifth, a seventh, a ninth), and one to
 * three sixteenths later its answer, lower, fuller and resolved (the root, the third, the fifth), now and then twice.
 * The call plays one preset, the answer another. It follows MIDI clock and Start / Stop (USB or TRS), or runs on its
 * own tempo. SEQ: its page. There the keys play it: F3 all new, G3 the calls, A3 the answers, B3 back (again:
 * forward), C4 run / stop (its own tempo), D4 two beats / four; F#3 G#3 A#3 C#4 the scale; OCT- / OCT+ the key.
 * K1 density, K2 variation (how much each round strays), K3 the register, K4 the tempo; SELECT the call's preset,
 * ALGO the answer's. New figures start on the next round */
#define GEN_CELL 8u                              /* sixteenths a cell: two beats */
#define GEN_STEPS 16u
#define GEN_CALLS 3u
#define GEN_CALL_BASE 48                         /* C3: the call's root (its notes a fifth, a seventh, a ninth above) */
#define GEN_ANS_BASE 36                          /* C2: the answer's, an octave under the call */
#define GEN_PPS 6u                               /* clock pulses a sixteenth (24 a beat) */
enum { G_NONE, G_CALL, G_ANS };
enum { SC_MINOR, SC_DORIAN, SC_PHRYG, SC_PENTA, SCALES };
static const char *const SCALE_NAME[SCALES] = {"MIN", "DOR", "PHR", "PENT"};
static const int8_t SCALE[SCALES][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
                                        {0, 3, 5, 7, 10, 0, 0}};
static const uint8_t SCALE_N[SCALES] = {7, 7, 7, 5};
/* (0.4) the keys a white key apart, for fingers in the dark: F3 all · A3 calls · C4 answers · E4 back | G4 run ·
 * B4 two / four beats | D5 E5 F5 G5 the scales; the black keys rest */
static const uint8_t SCALE_KEY[SCALES] = {21, 23, 24, 26};   /* D5 E5 F5 G5 */
#define GK_ALL 0u
#define GK_CALL 4u
#define GK_ANS 7u
#define GK_BACK 11u
#define GK_RUN 14u
#define GK_LEN 18u

typedef struct {                                 /* a call and its answer, in a cell */
    int8_t pos, cdeg, off, adeg, dbl, ddeg;      /* (dbl: 0 none, or the second answer's distance) */
} gcall_t;
typedef struct {
    uint8_t n, rsv;
    gcall_t c[GEN_CALLS];
} gcell_t;
typedef struct {
    int16_t root, scale, len, density, variation, reg, tempo, call_slot, ans_slot;
    int16_t tone, length, delay, reverb;         /* (0.4) -50..50 dark..bright, -50..50 snappy..long, 0..100, 0..100 */
    int16_t reserved[3];
    gcell_t cell[2];                             /* (the second: four beats long) */
} gen_t;
typedef struct {
    uint8_t type, note, vel, gate;               /* (gate: in clock pulses) */
} gstep_t;

typedef struct {
    uint32_t magic;
    uint16_t version, current;
    macro_global_t global;
    macro_patch_t preset[PRESETS];
    gen_t gen;                                   /* (version 2) */
} bank_t;
static void gen_defaults(gen_t *g);
static void gen_sanitize(gen_t *g);
static void draw_gen_header(void);
static void draw_gen_row(uint32_t row);

static const char *const MODEL_NAME[MACRO_ENGINES] = {
    "VA VCF", "PHASE DIST", "6-OP FM I", "6-OP FM II", "6-OP FM III", "WAVE TERRAIN", "STRING MACH", "CHIPTUNE",
    "VIRTUAL ANLG", "WAVESHAPER", "2-OP FM", "FORMANT", "HARMONIC", "WAVETABLE", "CHORDS", "SPEECH",
    "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DRUM", "SNARE", "HI-HAT"
};

static const char *const MODEL_PARAM[MACRO_ENGINES][3] = {
    {"RESONANCE", "CUTOFF", "WAVE+SUB"}, {"RATIO", "AMOUNT", "ASYMMETRY"},
    {"PATCH", "BRIGHTNESS", "ENVELOPE"}, {"PATCH", "BRIGHTNESS", "ENVELOPE"}, {"PATCH", "BRIGHTNESS", "ENVELOPE"},
    {"TERRAIN", "RADIUS", "OFFSET"}, {"CHORD", "FILTER/CHO", "REGISTER"}, {"CHORD", "ARP/INV", "PULSE"},
    {"DETUNE", "SQUARE", "SAW"}, {"WAVEFORM", "FOLD", "ASYMMETRY"}, {"RATIO", "INDEX", "FEEDBACK"},
    {"RATIO", "FORMANT", "WIDTH"}, {"BUMPS", "CENTER", "SHAPE"}, {"BANK", "ROW", "COLUMN"},
    {"CHORD", "INVERSION", "WAVEFORM"}, {"SYNTH/WORD", "SPECIES", "SEGMENT"},
    {"PITCH RND", "DENSITY", "GRAIN"}, {"FILTER", "CLOCK", "RESONANCE"}, {"FREQ RND", "DENSITY", "FILTER"},
    {"MATERIAL", "BRIGHTNESS", "DECAY"}, {"MATERIAL", "BRIGHTNESS", "DECAY"}, {"PUNCH", "TONE", "DECAY"},
    {"NOISE", "TONE", "DECAY"}, {"NOISE", "TONE", "DECAY"}
};

static const char *const SRC_NAME[SRC_COUNT] = {"OFF", "LFO1", "LFO2", "ENV", "VEL", "NOTE", "MODW", "AFT", "RAND", "GATE"};
static const char *const DST_NAME[DST_COUNT] = {"OFF", "PITCH", "HARM", "TIMBRE", "MORPH", "MODEL", "LEVEL", "LFO1 RT", "LFO2 RT"};
static const char *const LFO_NAME[LFO_SHAPES] = {"SINE", "TRI", "SAW", "SQUARE", "S&H", "SMOOTH"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const BANK_LETTER[3] = {"A", "B", "C"};
static const uint16_t KNOB_COLOR[4] = {RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 255, 255), RGB(255, 98, 26)};
static const uint16_t BANK_COLOR[3] = {RGB(255, 170, 30), RGB(60, 210, 90), RGB(240, 60, 60)};
static const char *const MODE_NAME[MODES] = {"TRIG", "DRONE", "GATE"};
static const uint16_t MODE_COLOR[MODES] = {C_HI, RGB(255, 170, 30), RGB(60, 210, 90)};
static const int16_t MODE_NEXT[MODES] = {MODE_GATE, MODE_TRIG, MODE_DRONE};

static macro_patch_t patch;
static macro_global_t global;
static macro_monitor_t mon;
static bank_t bank;
static uint32_t page = PAGE_MAIN, route_sel, save_slot, dirty = D_ALL, frame_count;
static int32_t octave;
static uint32_t hold, key_held;
static int32_t knob_fast[FW_KNOB_COUNT];
static uint32_t knob_ms[FW_KNOB_COUNT];
static uint8_t key_note[FW_KEYS];
static uint8_t mod_wheel, aftertouch;
static int16_t bend;
static int16_t scope[SCOPE_N];
static volatile uint32_t scope_ready;
static int16_t shown_mod[8];
static volatile uint32_t overloads;
static int shown_edited;
static uint32_t overloads_seen;
static const char *flash_msg;
static uint32_t flash_msg_until;

static const page_t PAGE_DEF[PAGES] = {
    [PAGE_ATTN] = {"ATTENUVERTERS", {
        {"FM", &patch.fm_amt, -100, 100, 2, FMT_BIP, 0},
        {"TIMBRE", &patch.timbre_amt, -100, 100, 2, FMT_BIP, 0},
        {"MORPH", &patch.morph_amt, -100, 100, 2, FMT_BIP, 0},
        {0, 0, 0, 0, 0, 0, 0}}, {{0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}},
    [PAGE_LPG] = {"LPG / OUT", {
        {"DECAY", &patch.decay, 0, 1000, 10, FMT_100, 0},
        {"COLOUR", &patch.colour, 0, 1000, 10, FMT_100, 0},
        {"OUT>AUX", &patch.blend, 0, 100, 2, FMT_INT, 0},
        {"STEREO", &patch.spread, 0, 100, 2, FMT_INT, 0}}, {{0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}},
    [PAGE_ENV] = {"MOD ENV", {
        {"ATTACK", &patch.env_a, 0, 1000, 10, FMT_TIME, 0},
        {"DECAY", &patch.env_d, 0, 1000, 10, FMT_TIME, 0},
        {"SUSTAIN", &patch.env_s, 0, 1000, 10, FMT_100, 0},
        {"RELEASE", &patch.env_r, 0, 1000, 10, FMT_TIME, 0}}, {{0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}},
    [PAGE_LFO] = {"LFO", {
        {"LFO1 RATE", &patch.lfo_rate[0], 0, 1000, 10, FMT_100, 0},
        {"LFO1 WAVE", &patch.lfo_shape[0], 0, LFO_SHAPES - 1, 1, FMT_ENUM, LFO_NAME},
        {"LFO2 RATE", &patch.lfo_rate[1], 0, 1000, 10, FMT_100, 0},
        {"LFO2 WAVE", &patch.lfo_shape[1], 0, LFO_SHAPES - 1, 1, FMT_ENUM, LFO_NAME}}, {
        {"SYNC1", &patch.lfo_sync[0], 0, 1, 1, FMT_ONOFF, 0},
        {"SYNC2", &patch.lfo_sync[1], 0, 1, 1, FMT_ONOFF, 0}}},
    [PAGE_GLOBAL] = {"GLOBAL", {
        {"GLIDE", &patch.glide, 0, 1000, 10, FMT_100, 0},
        {"BEND RANGE", &global.bend_range, 0, 12, 1, FMT_SEMI, 0},
        {"FINE TUNE", &global.fine, -100, 100, 1, FMT_BIP, 0},
        {"MIDI CH", &global.channel, 0, 16, 1, FMT_CHANNEL, 0}}, {
        {"LEGATO", &global.legato, 0, 1, 1, FMT_ONOFF, 0},
        {0, 0, 0, 0, 0, 0, 0}}},
};

static int16_t *knob_field(uint32_t i)
{
    if (i == 1u)
        return &patch.timbre;
    if (i == 2u)
        return &patch.morph;
    return &patch.harmonics;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void patch_init(macro_patch_t *p, int32_t engine)
{
    memset(p, 0, sizeof *p);
    p->engine = (int16_t)engine;
    p->mode = MODE_GATE;
    p->harmonics = 500;
    p->timbre = 500;
    p->morph = 500;
    p->decay = 500;
    p->colour = 500;
    p->lfo_rate[0] = 500;
    p->lfo_rate[1] = 350;
    p->lfo_shape[1] = LFO_TRI;
    p->env_d = 500;
    p->env_s = 700;
    p->env_r = 550;
}

static void route_set(macro_patch_t *p, uint32_t i, int32_t src, int32_t dst, int32_t amount)
{
    p->route[i].src = (int8_t)src;
    p->route[i].dst = (int8_t)dst;
    p->route[i].amount = (int16_t)amount;
}

static void factory_call_answer(void)            /* P15 CALL and P16 ANSWER: the generator's two voices */
{
    patch_init(&bank.preset[14], 10);            /* P15 CALL: a short bright 2-op FM pling */
    bank.preset[14].mode = MODE_TRIG;
    bank.preset[14].harmonics = 620;
    bank.preset[14].timbre = 560;
    bank.preset[14].morph = 260;
    bank.preset[14].decay = 80;
    bank.preset[14].colour = 450;
    patch_init(&bank.preset[15], 8);             /* P16 ANSWER: darker and fuller, detuned analog */
    bank.preset[15].mode = MODE_TRIG;
    bank.preset[15].harmonics = 700;
    bank.preset[15].timbre = 0;
    bank.preset[15].morph = 0;
    bank.preset[15].decay = 340;
    bank.preset[15].colour = 850;
}

static void factory_bank(void)
{
    static const int16_t E[PRESETS][4] = {
        {8, 250, 500, 500}, {0, 600, 300, 250}, {10, 300, 400, 500}, {2, 0, 500, 500},
        {11, 450, 400, 500}, {13, 300, 500, 400}, {14, 300, 400, 300}, {15, 600, 500, 300},
        {6, 300, 500, 400}, {5, 400, 500, 500}, {7, 300, 300, 400}, {19, 300, 500, 600},
        {20, 500, 400, 700}, {21, 400, 500, 500}, {22, 500, 500, 400}, {23, 500, 500, 300}
    };
    uint32_t i;
    memset(&bank, 0, sizeof bank);
    bank.magic = BANK_MAGIC;
    bank.version = BANK_VERSION;
    bank.global.bend_range = 2;
    bank.global.volume = 256;
    for (i = 0; i < PRESETS; i++) {
        macro_patch_t *p = &bank.preset[i];
        patch_init(p, E[i][0]);
        p->harmonics = E[i][1];
        p->timbre = E[i][2];
        p->morph = E[i][3];
    }
    route_set(&bank.preset[0], 0, SRC_LFO1, DST_PITCH, 2);
    bank.preset[0].lfo_rate[0] = 620;
    route_set(&bank.preset[1], 0, SRC_ENV, DST_TIMBRE, 45);
    bank.preset[1].env_s = 200;
    bank.preset[1].env_d = 520;
    bank.preset[2].timbre_amt = 40;
    route_set(&bank.preset[4], 0, SRC_LFO1, DST_TIMBRE, 30);
    bank.preset[4].lfo_rate[0] = 300;
    route_set(&bank.preset[5], 0, SRC_LFO2, DST_MORPH, 40);
    route_set(&bank.preset[5], 1, SRC_LFO1, DST_TIMBRE, 25);
    route_set(&bank.preset[7], 0, SRC_RAND, DST_MORPH, 50);
    route_set(&bank.preset[8], 0, SRC_ENV, DST_LEVEL, 100);
    bank.preset[8].env_a = 620;
    bank.preset[8].env_s = 800;
    bank.preset[8].env_r = 780;
    bank.preset[8].spread = 60;
    route_set(&bank.preset[9], 0, SRC_LFO1, DST_TIMBRE, 30);
    route_set(&bank.preset[9], 1, SRC_LFO2, DST_MORPH, 30);
    bank.preset[9].mode = MODE_DRONE;
    bank.preset[2].mode = MODE_TRIG;
    for (i = 11; i < PRESETS; i++)
        bank.preset[i].mode = MODE_TRIG;
    bank.preset[13].transpose = -12;
    factory_call_answer();
    gen_defaults(&bank.gen);
}

static void clamp16(int16_t *v, int32_t lo, int32_t hi) { *v = (int16_t)clampi(*v, lo, hi); }

static void patch_sanitize(macro_patch_t *p)
{
    uint32_t i;
    clamp16(&p->engine, 0, MACRO_ENGINES - 1);
    clamp16(&p->harmonics, 0, 1000);
    clamp16(&p->timbre, 0, 1000);
    clamp16(&p->morph, 0, 1000);
    clamp16(&p->transpose, -24, 24);
    clamp16(&p->fm_amt, -100, 100);
    clamp16(&p->timbre_amt, -100, 100);
    clamp16(&p->morph_amt, -100, 100);
    clamp16(&p->decay, 0, 1000);
    clamp16(&p->colour, 0, 1000);
    for (i = 0; i < MACRO_LFOS; i++) {
        clamp16(&p->lfo_rate[i], 0, 1000);
        clamp16(&p->lfo_shape[i], 0, LFO_SHAPES - 1);
        clamp16(&p->lfo_sync[i], 0, 1);
    }
    clamp16(&p->env_a, 0, 1000);
    clamp16(&p->env_d, 0, 1000);
    clamp16(&p->env_s, 0, 1000);
    clamp16(&p->env_r, 0, 1000);
    clamp16(&p->blend, 0, 100);
    clamp16(&p->spread, 0, 100);
    clamp16(&p->mode, 0, MODES - 1);
    clamp16(&p->glide, 0, 1000);
    for (i = 0; i < MACRO_ROUTES; i++) {
        p->route[i].src = (int8_t)clampi(p->route[i].src, 0, SRC_COUNT - 1);
        p->route[i].dst = (int8_t)clampi(p->route[i].dst, 0, DST_COUNT - 1);
        clamp16(&p->route[i].amount, -100, 100);
    }
}

static void bank_load(void)
{
    const int n = fw_store_load(0, &bank, sizeof bank);
    uint32_t i;
    const int old = bank.magic == BANK_MAGIC && bank.current < PRESETS &&
                    ((n == (int)__builtin_offsetof(bank_t, gen) && bank.version == 1u) || (n == (int)sizeof bank && bank.version == 2u));
    if (old) {                                   /* a bank from 0.1 or 0.2: its presets kept */
        if (bank.version == 1u)
            gen_defaults(&bank.gen);             /* (0.1: the generator new) */
        bank.gen.delay = 25;                     /* (0.4's effects) */
        bank.gen.reverb = 20;
        if (bank.preset[14].engine == 22 && bank.preset[15].engine == 23)
            factory_call_answer();               /* (0.1's snare and hi-hat, never changed: the generator's voices instead) */
        bank.version = BANK_VERSION;
    } else if (n != (int)sizeof bank || bank.magic != BANK_MAGIC || bank.version != BANK_VERSION || bank.current >= PRESETS) {
        factory_bank();
    }
    gen_sanitize(&bank.gen);
    for (i = 0; i < PRESETS; i++)
        patch_sanitize(&bank.preset[i]);
    clamp16(&bank.global.bend_range, 0, 12);
    clamp16(&bank.global.fine, -100, 100);
    clamp16(&bank.global.legato, 0, 1);
    clamp16(&bank.global.channel, 0, 16);
}

static void preset_select(uint32_t i)
{
    bank.current = (uint16_t)(i % PRESETS);
    fm1_irq_off();
    patch = bank.preset[bank.current];
    fm1_irq_on();
    dirty = D_ALL;
}

static void message(const char *s, uint32_t now)
{
    flash_msg = s;
    flash_msg_until = now + 1500u;
    dirty |= FW_BIT(D_STATUS);
}

static int edited(void) { return memcmp(&patch, &bank.preset[bank.current], sizeof patch) != 0; }

static void bank_save(uint32_t slot, uint32_t now)
{
    const uint16_t keep = bank.current;
    fm1_irq_off();                               /* (the generator may be playing it) */
    bank.preset[slot] = patch;
    fm1_irq_on();
    bank.current = (uint16_t)slot;
    bank.global = global;
    if (fw_store_save(0, &bank, sizeof bank) == 0) {
        message("SAVED", now);
        return;
    }
    bank.current = keep;
    message("SAVE FAILED", now);
}

static int adjust(int16_t *v, int32_t d, int32_t step, int32_t lo, int32_t hi)
{
    const int16_t next = (int16_t)clampi(*v + d * step, lo, hi);
    if (next == *v)
        return 0;
    *v = next;
    return 1;
}

/* steps scaled by how fast the knob turns: ms per detent, measured across frames */
static int32_t knob_accel(int32_t d, uint32_t gap)
{
    const uint32_t each = gap / (uint32_t)(d < 0 ? -d : d);
    if (each <= 20u)
        return d * 8;
    if (each <= 40u)
        return d * 4;
    if (each <= 80u)
        return d * 2;
    return d;
}

static void knob_speed(const fw_input_t *in)
{
    uint32_t k;
    for (k = 0; k < FW_KNOB_COUNT; k++) {
        knob_fast[k] = in->knob[k] ? knob_accel(in->knob[k], in->ms - knob_ms[k]) : 0;
        if (in->knob[k])
            knob_ms[k] = in->ms;
    }
}

static int adjust_param(const param_t *p, const fw_input_t *in, uint32_t k)
{
    if (!p->value || !in->knob[k])
        return 0;
    if (p->hi - p->lo < 40 * p->step)
        return adjust(p->value, in->knob[k], p->step, p->lo, p->hi);
    return adjust(p->value, knob_fast[k], p->step, p->lo, p->hi);
}

static void fmt_signed(char *b, int32_t v)
{
    if (v > 0) {
        b[0] = '+';
        fmt_int(b + 1, v);
        return;
    }
    fmt_int(b, v);
}

/* the envelope's times, as macro.cc: 1 ms * 10^(4 v / 1000), 250 steps a decade */
static uint32_t env_time_us(int32_t v)
{
    static const uint16_t DECADE[26] = {1000, 1096, 1202, 1318, 1445, 1585, 1738, 1905, 2089, 2291, 2512, 2754, 3020,
                                        3311, 3631, 3981, 4365, 4786, 5248, 5754, 6310, 6918, 7586, 8318, 9120, 10000};
    static const uint32_t SCALE[5] = {1u, 10u, 100u, 1000u, 10000u};
    const int32_t c = clampi(v, 0, 1000);
    const int32_t s = c / 10, f = c % 10;
    const int32_t j = s % 25;
    const uint32_t m = (uint32_t)(DECADE[j] * (10 - f) + DECADE[j + 1] * f) / 10u;
    return m * SCALE[s / 25];
}

static void fmt_time(char *b, uint32_t size, uint32_t us)
{
    if (us < 9950u) {
        fmt_fix(b, (int32_t)((us + 50u) / 100u), 1);
        str_cpy(b + str_len(b), " ms", size - str_len(b));
        return;
    }
    if (us < 999500u) {
        fmt_int(b, (int32_t)((us + 500u) / 1000u));
        str_cpy(b + str_len(b), " ms", size - str_len(b));
        return;
    }
    if (us < 9995000u) {
        fmt_fix(b, (int32_t)((us + 5000u) / 10000u), 2);
        str_cpy(b + str_len(b), " s", size - str_len(b));
        return;
    }
    fmt_fix(b, (int32_t)((us + 50000u) / 100000u), 1);
    str_cpy(b + str_len(b), " s", size - str_len(b));
}

static void format_param(char *b, uint32_t size, const param_t *p)
{
    const int32_t v = *p->value;
    if (p->fmt == FMT_TIME) {
        fmt_time(b, size, env_time_us(v));
        return;
    }
    if (p->fmt == FMT_100) {
        fmt_int(b, v / 10);
        return;
    }
    if (p->fmt == FMT_BIP || p->fmt == FMT_SEMI) {
        fmt_signed(b, v);
        if (p->fmt == FMT_SEMI)
            str_cpy(b + str_len(b), " ST", size - str_len(b));
        return;
    }
    if (p->fmt == FMT_ENUM) {
        str_cpy(b, p->names[v], size);
        return;
    }
    if (p->fmt == FMT_ONOFF) {
        str_cpy(b, v ? "ON" : "OFF", size);
        return;
    }
    if (p->fmt == FMT_CHANNEL && v == 0) {
        str_cpy(b, "OMNI", size);
        return;
    }
    fmt_int(b, v);
}

static void note_name(char *b, int32_t note10)
{
    const int32_t n = (note10 + 5) / 10;
    const int32_t safe = clampi(n, 0, 127);
    str_cpy(b, NOTE_NAME[safe % 12], 4);
    fmt_int(b + str_len(b), safe / 12 - 1);
}

static void draw_bar(int32_t x, int32_t y, int32_t w, int32_t base, int32_t lo, int32_t hi, int32_t mod, uint16_t c)
{
    const int32_t span = hi - lo;
    const int32_t bx = x + (clampi(base, lo, hi) - lo) * w / span;
    const int32_t mx = x + (clampi(mod, lo, hi) - lo) * w / span;
    const int32_t zero = lo < 0 ? x + (0 - lo) * w / span : x;
    cv_rect(x, y, w, 4, C_DIM);
    cv_rect(zero < bx ? zero : bx, y, zero < bx ? bx - zero : zero - bx, 4, c);
    if (mx != bx)
        cv_rect(mx - 1, y - 3, 3, 10, C_WHITE);
}

static void draw_row(uint32_t y, uint16_t c, const char *label, const char *sub, const char *value,
                     int32_t base, int32_t lo, int32_t hi, int32_t mod)
{
    int32_t x;
    cv_begin(240, ROW_H, C_BLACK);
    x = cv_text(8, 1, &FONT_S, label, c);
    if (sub)
        cv_text(x + 8, 1, &FONT_S, sub, C_HI);
    cv_text(232 - text_w(&FONT_S, value), 1, &FONT_S, value, C_HI);
    draw_bar(8, 20, 224, base, lo, hi, mod, c);
    cv_blit(0, y);
}

static void draw_title(const char *title, const char *right)
{
    cv_begin(240, 46, C_BLACK);
    cv_text(8, 8, &FONT_L, title, C_WHITE);
    if (right)
        cv_text(232 - text_w(&FONT_S, right), 2, &FONT_S, right, C_GRAY);
    cv_blit(0, 0);
}

static void draw_main_header(void)
{
    char b[16];
    const int32_t e = patch.engine;
    const uint32_t bk = (uint32_t)e / 8u;
    int32_t i;
    cv_begin(240, 46, C_BLACK);
    cv_rect(8, 2, 14, 14, BANK_COLOR[bk]);
    cv_text(11, 0, &FONT_S, BANK_LETTER[bk], C_BLACK);
    fmt_int(b, e % 8 + 1);
    str_cpy(b + str_len(b), " / 8", sizeof b - str_len(b));
    cv_text(28, 0, &FONT_S, b, C_GRAY);
    for (i = 0; i < MACRO_ENGINES; i++) {
        const int32_t x = 160 + (i % 8) * 9, y = 2 + (i / 8) * 5;
        cv_rect(x, y, 7, 3, i == e ? BANK_COLOR[i / 8] : C_DIM);
        if (i == mon.engine && i != e)
            cv_rect(x, y, 7, 3, C_GRAY);
    }
    cv_text(8, 14, &FONT_L, MODEL_NAME[e], C_WHITE);
    cv_blit(0, 0);
}

static int32_t patched(uint32_t dst) { return (mon.patched >> (dst - 1u)) & 1u; }

static int32_t main_mod(uint32_t row)
{
    static const uint8_t DST[4] = {DST_HARM, DST_TIMBRE, DST_MORPH, DST_PITCH};
    const int32_t v = mon.dst[DST[row]];
    if (row == 3)
        return patch.transpose + v * 12 / 1000;
    return *knob_field(row) + v;
}

static void draw_main_row(uint32_t row)
{
    static const char *const LABEL[4] = {"HARM", "TIMBRE", "MORPH", "FREQ"};
    char b[16], n[8];
    const uint32_t y = 48u + row * 30u;
    if (row == 3) {
        fmt_signed(b, patch.transpose);
        note_name(n, mon.note10);
        str_cpy(b + str_len(b), " ST  ", sizeof b - str_len(b));
        str_cpy(b + str_len(b), n, sizeof b - str_len(b));
        draw_row(y, KNOB_COLOR[3], LABEL[3], 0, b, patch.transpose, -24, 24, main_mod(3));
        return;
    }
    fmt_int(b, *knob_field(row) / 10);
    draw_row(y, KNOB_COLOR[row], LABEL[row], MODEL_PARAM[patch.engine][row], b, *knob_field(row), 0, 1000,
             main_mod(row));
}

static void draw_text_line(uint32_t y, const char *s, uint16_t c)
{
    cv_begin(240, 18, C_BLACK);
    cv_text(8, 1, &FONT_S, s, c);
    cv_blit(0, y);
}

static void draw_enc_line(void)
{
    char b[40], v[16];
    const page_t *pg = &PAGE_DEF[page];
    uint32_t i;
    b[0] = 0;
    if (page == PAGE_MAIN)
        str_cpy(b, "SEL MODEL  ALGO BANK", sizeof b);
    if (page == PAGE_MATRIX)
        str_cpy(b, "SEL SLOT  K1 SRC K2 DST K3/4 AMT", sizeof b);
    if (page == PAGE_SAVE)
        str_cpy(b, "SEL SLOT  SAVE = WRITE", sizeof b);
    if (page == PAGE_GEN)
        str_cpy(b, "REC+K1 DLY K2 VRB K4 TEMPO", sizeof b);
    for (i = 0; page != PAGE_MAIN && page != PAGE_MATRIX && page != PAGE_SAVE && page != PAGE_GEN && i < 2u; i++) {
        if (!pg->enc[i].label)
            continue;
        str_cpy(b + str_len(b), i ? " ALGO " : "SEL ", sizeof b - str_len(b));
        str_cpy(b + str_len(b), pg->enc[i].label, sizeof b - str_len(b));
        str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        format_param(v, sizeof v, &pg->enc[i]);
        str_cpy(b + str_len(b), v, sizeof b - str_len(b));
    }
    draw_text_line(168, b, C_GRAY);
}

static void draw_status(void)
{
    char b[48], n[8];
    cv_begin(240, 18, C_BLACK);
    cv_rect(0, 0, 240, 1, C_DIM);
    if (flash_msg) {
        cv_text(8, 2, &FONT_S, flash_msg, C_ACCENT);
        cv_blit(0, 222);
        return;
    }
    cv_text(8, 2, &FONT_S, MODE_NAME[patch.mode], MODE_COLOR[patch.mode]);
    cv_text(60, 2, &FONT_S, hold ? "HOLD" : "", BANK_COLOR[2]);
    str_cpy(b, "P", sizeof b);
    fmt_int(n, bank.current + 1);
    str_cpy(b + 1, n, sizeof b - 1);
    str_cpy(b + str_len(b), edited() ? "*" : "", sizeof b - str_len(b));
    cv_text(104, 2, &FONT_S, b, C_HI);
    str_cpy(b, "OCT", sizeof b);
    fmt_signed(n, octave);
    str_cpy(b + 3, n, sizeof b - 3);
    cv_text(140, 2, &FONT_S, b, C_GRAY);
    fmt_int(n, (int32_t)(fw_stats.cpu_q8 * 100u >> 8));
    str_cpy(n + str_len(n), overloads != overloads_seen ? "%!" : "%", sizeof n - str_len(n));
    cv_text(232 - text_w(&FONT_S, n), 2, &FONT_S, n,
            fw_stats.cpu_q8 > 192u || overloads != overloads_seen ? BANK_COLOR[2] : C_GRAY);
    overloads_seen = overloads;
    cv_blit(0, 222);
}

static void draw_scope(void)
{
    uint32_t x;
    int32_t prev = 16;
    cv_begin(240, 32, C_BLACK);
    cv_rect(0, 16, 240, 1, C_DIM);
    for (x = 0; x < SCOPE_N; x++) {
        const int32_t y = 16 - scope[x] / 900;
        cv_line((int32_t)x - 1, prev, (int32_t)x, y, BANK_COLOR[patch.engine / 8]);
        prev = y;
    }
    cv_blit(0, 188);
    scope_ready = 0;
}

static void draw_generic_row(uint32_t row)
{
    const param_t *p = &PAGE_DEF[page].knob[row];
    char b[16];
    const uint32_t y = 48u + row * 30u;
    if (!p->label) {
        cv_begin(240, ROW_H, C_BLACK);
        cv_blit(0, y);
        return;
    }
    format_param(b, sizeof b, p);
    draw_row(y, KNOB_COLOR[row], p->label, 0, b, *p->value, p->lo, p->hi, *p->value);
}

static void draw_matrix_row(uint32_t i)
{
    const macro_route_t *r = &patch.route[i];
    const int32_t live = mon.src[(uint32_t)r->src % SRC_COUNT] * r->amount / 100;
    const uint16_t c = i == route_sel ? C_WHITE : C_GRAY;
    char b[8];
    cv_begin(240, 18, i == route_sel ? RGB(30, 40, 70) : C_BLACK);
    fmt_int(b, (int32_t)i + 1);
    cv_text(6, 1, &FONT_S, b, i == route_sel ? C_ACCENT : C_DIM);
    cv_text(24, 1, &FONT_S, SRC_NAME[(uint32_t)r->src % SRC_COUNT], c);
    cv_text(72, 1, &FONT_S, ">", C_DIM);
    cv_text(86, 1, &FONT_S, DST_NAME[(uint32_t)r->dst % DST_COUNT], c);
    fmt_signed(b, r->amount);
    cv_text(186 - text_w(&FONT_S, b), 1, &FONT_S, b, c);
    cv_rect(192, 7, 44, 4, C_DIM);
    if (r->src && r->dst && r->amount) {
        const int32_t w = clampi(live, -1000, 1000) * 22 / 1000;
        cv_rect(w < 0 ? 214 + w : 214, 7, w < 0 ? -w : w, 4, C_ACCENT);
    }
    cv_blit(0, 48u + i * 20u);
}

static void draw_save_row(uint32_t row)
{
    char b[32], n[8];
    const uint32_t slot = (save_slot + PRESETS + row - 1u) % PRESETS;
    str_cpy(b, row == 1 ? "> P" : "  P", sizeof b);
    fmt_int(n, (int32_t)slot + 1);
    str_cpy(b + 3, n, sizeof b - 3);
    str_cpy(b + str_len(b), "  ", sizeof b - str_len(b));
    str_cpy(b + str_len(b), MODEL_NAME[bank.preset[slot].engine % MACRO_ENGINES], sizeof b - str_len(b));
    cv_begin(240, ROW_H, C_BLACK);
    cv_text(8, 6, &FONT_S, b, row == 1 ? C_WHITE : C_DIM);
    cv_blit(0, 48u + row * 30u);
}

static void draw_strip(uint32_t bit)
{
    if (bit == D_HEADER && page == PAGE_MAIN)
        draw_main_header();
    if (bit == D_HEADER && page == PAGE_MATRIX)
        draw_title("MOD MATRIX", 0);
    if (bit == D_HEADER && page == PAGE_SAVE)
        draw_title("SAVE", "PRESET");
    if (bit == D_HEADER && page == PAGE_GEN)
        draw_gen_header();
    if (bit >= D_ROW0 && bit < D_ROW0 + 4u && page == PAGE_GEN)
        draw_gen_row(bit - D_ROW0);
    if (bit == D_HEADER && page != PAGE_MAIN && page != PAGE_MATRIX && page != PAGE_SAVE && page != PAGE_GEN)
        draw_title(PAGE_DEF[page].title, 0);
    if (bit >= D_ROW0 && bit < D_ROW0 + 8u && page == PAGE_MATRIX)
        draw_matrix_row(bit - D_ROW0);
    if (bit >= D_ROW0 && bit < D_ROW0 + 4u && page == PAGE_MAIN)
        draw_main_row(bit - D_ROW0);
    if (bit >= D_ROW0 && bit < D_ROW0 + 3u && page == PAGE_SAVE)
        draw_save_row(bit - D_ROW0);
    if (bit >= D_ROW0 && bit < D_ROW0 + 4u && page != PAGE_MAIN && page != PAGE_MATRIX && page != PAGE_SAVE &&
        page != PAGE_GEN)
        draw_generic_row(bit - D_ROW0);
    if (bit == D_ENC && page != PAGE_MATRIX)
        draw_enc_line();
    if (bit == D_SCOPE && page != PAGE_MATRIX)
        draw_scope();
    if (bit == D_STATUS)
        draw_status();
}

static void clear_page(void)
{
    lcd_fill(0, 0, 240, 222, C_BLACK);
}

static void draw(void)
{
    uint32_t n = 0, bit;
    for (bit = 0; bit < 15u && n < DRAWS_PER_FRAME; bit++) {
        if (!(dirty & FW_BIT(bit)) || bit == D_SCOPE)
            continue;
        dirty &= ~FW_BIT(bit);
        draw_strip(bit);
        n++;
    }
    if (n == 0 && (dirty & FW_BIT(D_SCOPE)) && scope_ready) {
        dirty &= ~FW_BIT(D_SCOPE);
        draw_strip(D_SCOPE);
    }
}

static void track_modulation(void)
{
    uint32_t i;
    if (page == PAGE_MAIN) {
        for (i = 0; i < 4u; i++) {
            const int16_t m = (int16_t)main_mod(i);
            const int32_t diff = m - shown_mod[i];
            if (diff > 8 || diff < -8 || (i == 3u && diff)) {
                shown_mod[i] = m;
                dirty |= FW_BIT(D_ROW0 + i);
            }
        }
        if (mon.engine != shown_mod[4]) {
            shown_mod[4] = mon.engine;
            dirty |= FW_BIT(D_HEADER) | FW_BIT(D_ROW0 + 3u);
        }
        dirty |= FW_BIT(D_SCOPE);
        return;
    }
    if (page == PAGE_MATRIX && (frame_count & 3u) == 0u)
        dirty |= FW_BIT(D_ROW0 + frame_count / 4u % MACRO_ROUTES);
}

static void set_page(uint32_t p)
{
    if (p == page)
        return;
    page = p;
    dirty = D_ALL;
    clear_page();
}

static void note_on(uint32_t note, uint32_t vel)
{
    macro_note_on((uint8_t)note, (uint8_t)vel);
}

static void note_off(uint32_t note)
{
    if (hold)
        return;
    macro_note_off((uint8_t)note);
}

/* ------------------------------------------- the generator: the main loop's side --- */
static gen_t gen_prev;                           /* (BACK) */
static uint8_t gen_has_prev, gen_int_run, gen_ext_run, gen_want, gen_rec_held;   /* (gen_want: a figure to hand over) */
static uint32_t gen_rng = 0x2545F491u, gen_ext_ms, gen_cycle_seen, gen_step_shown = 99u;
static uint32_t gen_ts[25], gen_ts_n;            /* the last clock pulses, in samples */

/* shared with the audio interrupt */
static volatile gstep_t gen_live[GEN_STEPS], gen_pend[GEN_STEPS];
static volatile uint8_t gen_pend_ok, gen_len_isr = GEN_CELL, gen_on, gen_ext, gen_start_req, gen_stop_req;
static volatile uint8_t gen_step_now, gen_type_now;
static volatile int8_t gen_gate_add;            /* (LENGTH, on the gates, in pulses) */
static volatile uint32_t gen_ext_pulses, gen_spp_q8 = 30720000u / 124u, gen_cycles, seq_samples, gen_ext_k, gen_ext_t;
static const macro_patch_t *volatile gen_patch_p[3];

static uint32_t gen_r(uint32_t n)
{
    gen_rng ^= gen_rng << 13, gen_rng ^= gen_rng >> 17, gen_rng ^= gen_rng << 5;
    return n ? gen_rng % n : 0u;
}
static int32_t gen_pick(const uint8_t *w, uint32_t n)   /* an index, by weight */
{
    uint32_t sum = 0, i, r;
    for (i = 0; i < n; i++)
        sum += w[i];
    r = gen_r(sum);
    for (i = 0; i < n; i++) {
        if (r < w[i])
            return (int32_t)i;
        r -= w[i];
    }
    return 0;
}
static int32_t gen_semis(int32_t deg)            /* a degree of the scale, in semitones (below the root: negative) */
{
    const int32_t n = SCALE_N[bank.gen.scale % SCALES];
    const int32_t o = deg >= 0 ? deg / n : -((n - 1 - deg) / n), d = deg - o * n;
    return SCALE[bank.gen.scale % SCALES][d] + 12 * o;
}
static int32_t gen_note(int32_t base, int32_t deg) { return clampi(base + bank.gen.root + 12 * bank.gen.reg + gen_semis(deg), 12, 115); }

/* the call: on a sixteenth off the kicks (the one before a beat, the offbeat eighth likeliest), its degree open */
static void gen_new_calls(gcell_t *c, int same_degree)
{
    static const uint8_t POS[8] = {0, 10, 40, 20, 0, 8, 15, 30};   /* (beats at 0 and 4: none) */
    static const int8_t CDEG[4] = {4, 6, 8, 3};                     /* (a fifth, a seventh, a ninth, a fourth) */
    static const uint8_t CW[4] = {45, 25, 20, 10};
    const int32_t d = bank.gen.density;
    const int8_t deg = CDEG[gen_pick(CW, 4)];
    uint32_t n = 1u + (d >= 35) + (d >= 75 && gen_r(100) < 60u), i, tries = 0;
    c->n = 0;
    while (c->n < n && tries++ < 40) {
        const int8_t p = (int8_t)gen_pick(POS, 8);
        for (i = 0; i < c->n && c->c[i].pos != p && (c->c[i].pos + 1) % 8 != p && (p + 1) % 8 != c->c[i].pos; i++)
            ;
        if (i < c->n)
            continue;
        c->c[c->n].pos = p;
        c->c[c->n].cdeg = same_degree || gen_r(100) < 70u ? deg : CDEG[gen_pick(CW, 4)];
        c->n++;
    }
}
/* the answer: one to three sixteenths after its call, its degree resolved; now and then a second one */
static void gen_new_answer(gcall_t *a)
{
    static const uint8_t OFF[3] = {35, 45, 20};
    static const int8_t ADEG[4] = {0, 2, 4, -1};   /* (the root, the third, the fifth, the seventh below) */
    static const uint8_t AW[4] = {50, 25, 15, 10};
    a->off = (int8_t)(1 + gen_pick(OFF, 3));
    a->adeg = ADEG[gen_pick(AW, 4)];
    a->dbl = (int8_t)((int32_t)gen_r(140) < bank.gen.density ? 1 + (int32_t)gen_r(2) : 0);
    a->ddeg = gen_r(2) ? a->adeg : ADEG[gen_pick(AW, 4)];
}
static void gen_new(int what)                    /* 1 the calls, 2 the answers, 3 both */
{
    uint32_t k, i;
    gen_prev = bank.gen;
    gen_has_prev = 1;
    gen_rng ^= fm1_ms * 2654435761u;
    for (k = 0; k < 2u; k++) {
        gcell_t *c = &bank.gen.cell[k];
        if (k == 1u && gen_r(100) < 50u && (what & 1)) {   /* (the second cell: often the first again) */
            *c = bank.gen.cell[0];
            continue;
        }
        if (what & 1)
            gen_new_calls(c, k == 1u);
        for (i = 0; i < c->n; i++)
            gen_new_answer(&c->c[i]);
    }
    gen_want = 1;
}

static uint32_t gen_offbeat(uint32_t p)          /* a step of the cell, off the beats (the kick's) */
{
    p %= GEN_CELL;
    return p % 4u == 0u ? p + 1u : p;
}

/* the figure, as the steps it plays: a round's own small changes (Variation) on it, the figure itself kept */
static void gen_render(gstep_t *s, int vary)
{
    const uint32_t len = bank.gen.len == 16 ? 16u : 8u;
    uint32_t k, i;
    memset(s, 0, sizeof(gstep_t) * GEN_STEPS);
    for (k = 0; k < len / GEN_CELL; k++) {
        gcell_t c = bank.gen.cell[k];
        if (vary && c.n && (int32_t)gen_r(100) < bank.gen.variation) {   /* one thing a little else, this round */
            gcall_t *a = &c.c[gen_r(c.n)];
            const uint32_t w = gen_r(4);
            if (w == 0u)
                a->off = (int8_t)(a->off == 1 ? 2 : a->off - 1);
            else if (w == 1u)
                a->adeg = (int8_t)(a->adeg == 0 ? 2 : 0);
            else if (w == 2u)
                a->dbl = (int8_t)(a->dbl ? 0 : 1);
            else
                a->cdeg = (int8_t)(a->cdeg == 4 ? 6 : 4);
        }
        for (i = 0; i < c.n; i++) {
            const gcall_t *a = &c.c[i];
            const uint32_t p = k * GEN_CELL + (uint32_t)a->pos, q = k * GEN_CELL + gen_offbeat((uint32_t)(a->pos + a->off)),
                           q2 = k * GEN_CELL + gen_offbeat(q % GEN_CELL + (uint32_t)a->dbl);
            if (s[p].type == G_NONE)
                s[p] = (gstep_t){G_CALL, (uint8_t)gen_note(GEN_CALL_BASE, a->cdeg), (uint8_t)(96 + gen_r(16)), 2};
            if (s[q].type == G_NONE)
                s[q] = (gstep_t){G_ANS, (uint8_t)gen_note(GEN_ANS_BASE, a->adeg), (uint8_t)(104 + gen_r(16)), a->dbl ? 3 : 5};
            if (a->dbl && s[q2].type == G_NONE)
                s[q2] = (gstep_t){G_ANS, (uint8_t)gen_note(GEN_ANS_BASE, a->ddeg), (uint8_t)(84 + gen_r(16)), 4};
        }
    }
}

static void gen_handover(int vary)               /* the next round's steps (the interrupt takes them at its start) */
{
    gstep_t s[GEN_STEPS];
    uint32_t i;
    gen_render(s, vary);
    fm1_irq_off();
    for (i = 0; i < GEN_STEPS; i++)
        gen_pend[i] = s[i];
    gen_len_isr = (uint8_t)(bank.gen.len == 16 ? 16 : 8);
    gen_pend_ok = 1;
    fm1_irq_on();
    gen_want = 0;
}

static void gen_defaults(gen_t *g)
{
    memset(g, 0, sizeof *g);
    g->scale = SC_MINOR;
    g->len = 8;
    g->density = 50;
    g->variation = 25;
    g->tempo = 124;
    g->call_slot = 14;
    g->ans_slot = 15;
    g->delay = 25;
    g->reverb = 20;
}
static void gen_sanitize(gen_t *g)
{
    uint32_t k, i;
    clamp16(&g->root, 0, 11);
    clamp16(&g->scale, 0, SCALES - 1);
    g->len = g->len == 16 ? 16 : 8;
    clamp16(&g->density, 0, 100);
    clamp16(&g->variation, 0, 100);
    clamp16(&g->reg, -2, 2);
    clamp16(&g->tone, -50, 50);
    clamp16(&g->length, -50, 50);
    clamp16(&g->delay, 0, 100);
    clamp16(&g->reverb, 0, 100);
    clamp16(&g->tempo, 60, 190);
    clamp16(&g->call_slot, 0, PRESETS - 1);
    clamp16(&g->ans_slot, 0, PRESETS - 1);
    for (k = 0; k < 2u; k++) {
        if (g->cell[k].n > GEN_CALLS)
            g->cell[k].n = 0;
        for (i = 0; i < GEN_CALLS; i++) {
            gcall_t *a = &g->cell[k].c[i];
            a->pos = (int8_t)clampi(a->pos, 0, 7);
            a->off = (int8_t)clampi(a->off, 1, 3);
            a->dbl = (int8_t)clampi(a->dbl, 0, 2);
            a->cdeg = (int8_t)clampi(a->cdeg, -7, 14);
            a->adeg = (int8_t)clampi(a->adeg, -7, 14);
            a->ddeg = (int8_t)clampi(a->ddeg, -7, 14);
        }
    }
}

static macro_patch_t gen_mp[2][2];               /* the two voices as they play: their presets with TONE and LENGTH on
                                                  * them (two of each: the interrupt plays one while the other is made) */
static uint32_t gen_mp_sel;
static int16_t gen_shift(int32_t v, int32_t d) { return (int16_t)clampi(v + d, 0, 1000); }
static void gen_ptrs(void)                       /* the presets the call and the answer play (the one being edited: live) */
{
    uint32_t t;
    gen_mp_sel ^= 1u;
    gen_patch_p[0] = &patch;
    for (t = 0; t < 2u; t++) {
        const uint32_t slot = (uint32_t)(t ? bank.gen.ans_slot : bank.gen.call_slot);
        macro_patch_t *m = &gen_mp[t][gen_mp_sel];
        const int32_t tone = bank.gen.tone, len = bank.gen.length;
        *m = slot == bank.current ? patch : bank.preset[slot];
        m->timbre = gen_shift(m->timbre, tone * 6);          /* TONE: brighter, the gate's filter more open */
        m->colour = gen_shift(m->colour, -tone * 6);
        m->decay = gen_shift(m->decay, len * 9);             /* LENGTH: snappy .. long */
        m->env_d = gen_shift(m->env_d, len * 9);
        m->env_r = gen_shift(m->env_r, len * 9);
        gen_patch_p[G_CALL + t] = m;
    }
}

static void gen_realtime(uint32_t st, uint32_t now)   /* MIDI clock and transport */
{
    if (st == 0xF8u) {
        uint32_t i, back;
        gen_ext_ms = now;
        gen_ext = 1;
        fm1_irq_off();                           /* (the pulse and when it came, together) */
        gen_ext_k = gen_ext_pulses;
        gen_ext_t = seq_samples;
        gen_ext_pulses++;
        fm1_irq_on();
        for (i = 24; i > 0; i--)
            gen_ts[i] = gen_ts[i - 1];
        gen_ts[0] = seq_samples;
        if (gen_ts_n < 25u)
            gen_ts_n++;
        back = gen_ts_n >= 25u ? 24u : gen_ts_n >= 7u ? 6u : 0u;
        if (back) {                              /* samples a pulse, averaged (the frames' jitter evened out) */
            const uint32_t spp = (gen_ts[0] - gen_ts[back]) * 256u / back;
            if (spp > 256u * 200u && spp < 256u * 4000u)
                gen_spp_q8 = (gen_spp_q8 * 3u + spp) / 4u;
        }
    } else if (st == 0xFAu || st == 0xFBu) {
        if (st == 0xFAu)
            gen_ext_pulses = 0;
        gen_ext = 1;
        gen_ext_ms = now;
        gen_ext_run = 1;
        gen_int_run = 0;
        gen_start_req = (uint8_t)(st == 0xFAu ? 1 : 2);
    } else if (st == 0xFCu) {
        gen_ext_run = 0;
        gen_stop_req = 1;
    }
}

static void gen_frame(uint32_t now)              /* each frame */
{
    if (gen_ext && now - gen_ext_ms > 600u && !gen_ext_run) {   /* the clock gone: its own tempo again */
        gen_ext = 0;
        gen_ts_n = 0;
    }
    if (!gen_ext)
        gen_spp_q8 = (uint32_t)FW_FS * 640u / (uint32_t)bank.gen.tempo;
    gen_gate_add = (int8_t)(bank.gen.length / 6);
    gen_ptrs();
    if (gen_cycles != gen_cycle_seen) {          /* a round begun: the next one's steps, a little changed */
        gen_cycle_seen = gen_cycles;
        if (!gen_want)
            gen_want = 2;
    }
    if (gen_want)
        gen_handover(gen_want == 2);
}

static void gen_run(int on)
{
    if (gen_ext_run && on)
        return;
    gen_int_run = (uint8_t)on;
    if (on) {
        gen_ext_pulses = 0;
        gen_handover(0);
        gen_start_req = 1;
    } else {
        gen_stop_req = 1;
    }
}

/* ------------------------------------------- the generator: the audio interrupt's side --- */
static uint32_t gi_pulses, gi_acc, gi_gate, gi_note, gi_sounding, gi_frame, gi_t[64];   /* (gi_frame: where in the stream, for the host's log) */

#define GEN_LEAD 288                             /* samples (6 ms): a clock pulse is seen half a frame late, on average */
static uint32_t gen_spp_eff(void)                /* a pulse's length; on the clock, a touch faster or slower to keep up:
                                                  * the last pulse's own, against when ours of that number came (its
                                                  * error made up over a beat, the frames' jitter evened out) */
{
    int32_t spp = (int32_t)gen_spp_q8, err;
    if (gen_ext && gen_ext_pulses) {
        const uint32_t k = gen_ext_k, t = gen_ext_t - GEN_LEAD;
        const int32_t ahead = (int32_t)(gi_pulses - k);
        if (ahead > 24 || ahead < -24) {         /* (lost: back on its pulse) */
            gi_pulses = k + 1u;
            return (uint32_t)spp;
        }
        err = ahead > 0 && ahead < 64 ? (int32_t)(gi_t[k % 64u] - t)
                                       : (int32_t)(gi_frame - t) + (1 - ahead) * (spp / 256);
        err = err > 4800 ? 4800 : err < -4800 ? -4800 : err;
        spp -= err * 256 / 24;
        if (spp < (int32_t)gen_spp_q8 * 9 / 10)
            spp = (int32_t)gen_spp_q8 * 9 / 10;
        if (spp > (int32_t)gen_spp_q8 * 11 / 10)
            spp = (int32_t)gen_spp_q8 * 11 / 10;
    }
    return spp < 256 * 100 ? 256u * 100u : (uint32_t)spp;
}

static void gen_note_end(void)
{
    if (gi_sounding)
        macro_note_off_isr((uint8_t)gi_note);
    gi_sounding = 0;
}

static void gen_pulse(void)
{
    gi_t[gi_pulses % 64u] = gi_frame;
    if (gi_gate && --gi_gate == 0u)
        gen_note_end();
    if (gi_pulses % GEN_PPS == 0u) {
        const uint32_t len = gen_len_isr, step = gi_pulses / GEN_PPS % len;
        if (step == 0u) {
            if (gen_pend_ok) {
                uint32_t i;
                for (i = 0; i < GEN_STEPS; i++)
                    gen_live[i] = gen_pend[i];
                gen_pend_ok = 0;
            }
            gen_cycles++;
        }
        gen_step_now = (uint8_t)step;
        if (gen_live[step].type != G_NONE) {
            gen_note_end();
            gen_type_now = gen_live[step].type;
            gi_note = gen_live[step].note;
            macro_note_on_isr((uint8_t)gi_note, gen_live[step].vel);
#ifdef FW_HOST
            if (getenv("GEN_LOG"))
                printf("non %u %u %u\n", gi_frame, step, gen_type_now);
#endif
            gi_sounding = 1;
            gi_gate = (uint32_t)clampi((int32_t)gen_live[step].gate + gen_gate_add, 1, 23);
        }
    }
    gi_pulses++;
}

static void gen_isr_begin(void)
{
    if (gen_stop_req) {
        gen_stop_req = 0;
        gen_on = 0;
        gen_note_end();
        gen_type_now = G_NONE;
    }
    if (gen_start_req) {
        if (gen_start_req == 1) {
            gi_pulses = 0;
            gen_step_now = 0;
        }
        gen_start_req = 0;
        gi_acc = 0;
        gen_on = 1;
        gen_pulse();
    }
}

static uint32_t gen_until(void)                  /* frames to the next pulse */
{
    const uint32_t spp = gen_spp_eff();
    return gi_acc >= spp ? 1u : (spp - gi_acc + 255u) / 256u;
}

static void gen_advance(uint32_t frames)
{
    gi_acc += frames * 256u;
    for (;;) {
        const uint32_t spp = gen_spp_eff();
        if (gi_acc < spp)
            break;
        gi_acc -= spp;
        gen_pulse();
    }
}

/* ------------------------------------------- the generator's page --- */
static void gen_keys(const fw_input_t *in)
{
    uint32_t k;
    for (k = 0; k < FW_KEYS; k++) {
        if (!(in->keys_pressed & FW_BIT(k)))
            continue;
        if (k == GK_ALL) {
            gen_new(3);
            message("ALL NEW", in->ms);
        } else if (k == GK_CALL) {
            gen_new(1);
            message("NEW CALLS", in->ms);
        } else if (k == GK_ANS) {
            gen_new(2);
            message("NEW ANSWERS", in->ms);
        } else if (k == GK_BACK && gen_has_prev) {
            const gen_t t = bank.gen;
            bank.gen.cell[0] = gen_prev.cell[0];
            bank.gen.cell[1] = gen_prev.cell[1];
            gen_prev = t;
            gen_want = 1;
            message("BACK", in->ms);
        } else if (k == GK_RUN) {
            if (gen_ext_run)
                message("RUNS ON THE CLOCK", in->ms);
            else
                gen_run(!gen_int_run);
        } else if (k == GK_LEN) {
            bank.gen.len = bank.gen.len == 16 ? 8 : 16;
            gen_want = 1;
            message(bank.gen.len == 16 ? "FOUR BEATS" : "TWO BEATS", in->ms);
        } else {
            uint32_t s;
            for (s = 0; s < SCALES; s++)
                if (k == SCALE_KEY[s]) {
                    bank.gen.scale = (int16_t)s;
                    gen_want = 1;
                    message(SCALE_NAME[s], in->ms);
                }
        }
        dirty |= FW_BIT(D_HEADER) | 0x1Eu;
    }
}

static void gen_knobs(const fw_input_t *in)
{
    gen_t *g = &bank.gen;
    int changed = 0;
    if (in->held & FW_BIT(FW_BTN_REC)) {         /* REC held: the effects and the tempo */
        changed |= adjust(&g->delay, knob_fast[FW_KNOB_1], 2, 0, 100);
        changed |= adjust(&g->reverb, knob_fast[FW_KNOB_2], 2, 0, 100);
        changed |= adjust(&g->tempo, knob_fast[FW_KNOB_4], 1, 60, 190);
    } else {
        changed |= adjust(&g->density, knob_fast[FW_KNOB_1], 2, 0, 100);
        changed |= adjust(&g->variation, knob_fast[FW_KNOB_2], 2, 0, 100);
        changed |= adjust(&g->tone, knob_fast[FW_KNOB_3], 2, -50, 50);
        changed |= adjust(&g->length, knob_fast[FW_KNOB_4], 2, -50, 50);
    }
    changed |= adjust(&g->call_slot, in->knob[FW_KNOB_SELECT], 1, 0, PRESETS - 1);
    changed |= adjust(&g->ans_slot, in->knob[FW_KNOB_ALGO], 1, 0, PRESETS - 1);
    if (changed)
        dirty |= FW_BIT(D_HEADER) | 0x1Eu | FW_BIT(D_ENC);
}

static void gen_key_shift(int32_t d)             /* OCT- / OCT+ on the page: the key */
{
    bank.gen.root = (int16_t)((bank.gen.root + d + 12) % 12);
    gen_want = 1;
    dirty |= FW_BIT(D_HEADER) | 0x1Eu;
}

static void gen_oct(const fw_input_t *in, int32_t d)   /* OCT- / OCT+: the register; REC held: the key */
{
    if (in->held & FW_BIT(FW_BTN_REC)) {
        gen_key_shift(d);
        return;
    }
    if (adjust(&bank.gen.reg, d, 1, -2, 2)) {
        gen_want = 1;
        dirty |= FW_BIT(D_HEADER) | 0x1Eu;
    }
}

static void gen_note_str(char *b, uint32_t size, int32_t note)
{
    str_cpy(b, NOTE_NAME[note % 12], size);
    fmt_int(b + str_len(b), note / 12 - 1);
}

static void draw_gen_header(void)
{
    char b[24];
    str_cpy(b, NOTE_NAME[bank.gen.root % 12], sizeof b);
    str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
    str_cpy(b + str_len(b), SCALE_NAME[bank.gen.scale % SCALES], sizeof b - str_len(b));
    str_cpy(b + str_len(b), " R", sizeof b - str_len(b));
    fmt_signed(b + str_len(b), bank.gen.reg);
    str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
    if (gen_ext)
        str_cpy(b + str_len(b), "EXT", sizeof b - str_len(b));
    else
        fmt_int(b + str_len(b), bank.gen.tempo);
    draw_title("CALL/ANS", b);
}

static void draw_gen_row(uint32_t row)
{
    static const uint16_t CC = RGB(255, 170, 30), AC = RGB(60, 190, 255);
    const uint32_t y = 48u + row * 30u, len = bank.gen.len == 16 ? 16u : 8u;
    char b[48], n[12];
    uint32_t i;
    cv_begin(240, ROW_H, C_BLACK);
    if (row == 0u) {                             /* the steps: calls above, answers below, the one playing lit */
        const int32_t w = 224 / (int32_t)len;
        for (i = 0; i < len; i++) {
            const int32_t x = 8 + (int32_t)i * w;
            const gstep_t st = gen_live[i];
            const int now = gen_on && i == gen_step_now;
            cv_rect(x, 26, w - 2, 2, (i & 3u) ? C_DIM : C_GRAY);
            if (now)
                cv_rect(x, 0, w - 2, 25, RGB(40, 40, 50));
            if (st.type == G_CALL)
                cv_rect(x + 1, 2, w - 4, 8, now ? C_WHITE : CC);
            if (st.type == G_ANS)
                cv_rect(x + 1, 12, w - 4, 12, now ? C_WHITE : AC);
        }
    } else if (row == 1u) {                      /* the notes */
        int32_t x = cv_text(8, 4, &FONT_S, "CALL", CC) + 6, seen = 0;
        for (i = 0; i < GEN_STEPS; i++)
            if (gen_live[i].type == G_CALL && !(seen >> (gen_live[i].note % 24) & 1)) {
                seen |= 1 << (gen_live[i].note % 24);
                gen_note_str(n, sizeof n, gen_live[i].note);
                x = cv_text(x, 4, &FONT_S, n, C_HI) + 5;
            }
        x = cv_text(x + 6, 4, &FONT_S, "ANS", AC) + 6;
        seen = 0;
        for (i = 0; i < GEN_STEPS && x < 220; i++)
            if (gen_live[i].type == G_ANS && !(seen >> (gen_live[i].note % 24) & 1)) {
                seen |= 1 << (gen_live[i].note % 24);
                gen_note_str(n, sizeof n, gen_live[i].note);
                x = cv_text(x, 4, &FONT_S, n, C_HI) + 5;
            }
    } else if (row == 2u) {                      /* the knobs */
        if (gen_rec_held) {
            str_cpy(b, "DLY ", sizeof b);
            fmt_int(b + str_len(b), bank.gen.delay);
            str_cpy(b + str_len(b), " VRB ", sizeof b - str_len(b));
            fmt_int(b + str_len(b), bank.gen.reverb);
            str_cpy(b + str_len(b), "  TEMPO ", sizeof b - str_len(b));
            fmt_int(b + str_len(b), bank.gen.tempo);
            cv_text(8, 4, &FONT_S, b, C_ACCENT);
        } else {
            str_cpy(b, "DEN ", sizeof b);
            fmt_int(b + str_len(b), bank.gen.density);
            str_cpy(b + str_len(b), " VAR ", sizeof b - str_len(b));
            fmt_int(b + str_len(b), bank.gen.variation);
            str_cpy(b + str_len(b), " TON ", sizeof b - str_len(b));
            fmt_signed(b + str_len(b), bank.gen.tone);
            str_cpy(b + str_len(b), " LEN ", sizeof b - str_len(b));
            fmt_signed(b + str_len(b), bank.gen.length);
            cv_text(8, 4, &FONT_S, b, C_GRAY);
        }
    } else {                                     /* the two presets */
        str_cpy(b, "P", sizeof b);
        fmt_int(b + 1, bank.gen.call_slot + 1);
        str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        str_cpy(b + str_len(b), MODEL_NAME[bank.preset[bank.gen.call_slot].engine % MACRO_ENGINES], sizeof b - str_len(b));
        cv_text(8, 0, &FONT_S, b, CC);
        str_cpy(b, "P", sizeof b);
        fmt_int(b + 1, bank.gen.ans_slot + 1);
        str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        str_cpy(b + str_len(b), MODEL_NAME[bank.preset[bank.gen.ans_slot].engine % MACRO_ENGINES], sizeof b - str_len(b));
        cv_text(8, 14, &FONT_S, b, AC);
    }
    cv_blit(0, y);
}

static uint32_t gen_leds(void)
{
    uint32_t m = FW_BIT(GK_ALL) | FW_BIT(GK_CALL) | FW_BIT(GK_ANS) | FW_BIT(GK_LEN) | FW_BIT(SCALE_KEY[bank.gen.scale % SCALES]);
    if (gen_has_prev)
        m |= FW_BIT(GK_BACK);
    if (!gen_on || gen_step_now % 4u != 0u)
        m |= FW_BIT(GK_RUN);
    return m;
}

static void handle_buttons(const fw_input_t *in)
{
    static const uint8_t MAP[][2] = {
        {FW_BTN_HOME, PAGE_MAIN}, {FW_BTN_SCL, PAGE_ATTN}, {FW_BTN_FX, PAGE_LPG}, {FW_BTN_ENV, PAGE_ENV},
        {FW_BTN_LFO, PAGE_LFO}, {FW_BTN_EDIT, PAGE_MATRIX}, {FW_BTN_GLO, PAGE_GLOBAL}, {FW_BTN_SEQ, PAGE_GEN}
    };
    const uint32_t was = page;
    uint32_t i;
    for (i = 0; i < FW_NELEM(MAP); i++) {
        if (in->pressed & FW_BIT(MAP[i][0]))
            set_page(MAP[i][1]);
    }
    if ((in->pressed & FW_BIT(FW_BTN_SAVE)) && was == PAGE_SAVE) {
        bank_save(save_slot, in->ms);
        set_page(PAGE_MAIN);
    }
    if ((in->pressed & FW_BIT(FW_BTN_SAVE)) && was != PAGE_SAVE) {
        save_slot = bank.current;
        set_page(PAGE_SAVE);
    }
    if (in->pressed & FW_BIT(FW_BTN_PLAY)) {
        patch.mode = MODE_NEXT[patch.mode];
        dirty |= FW_BIT(D_STATUS);
    }
    if ((in->pressed & FW_BIT(FW_BTN_REC)) && page != PAGE_GEN) {
        hold = !hold;
        if (!hold)
            macro_all_notes_off();
        dirty |= FW_BIT(D_STATUS);
    }
    if (page == PAGE_GEN && (in->pressed & FW_BIT(FW_BTN_OCT_DOWN)))
        gen_oct(in, -1);
    else if (in->pressed & FW_BIT(FW_BTN_OCT_DOWN))
        octave = clampi(octave - 1, -3, 3);
    if (page == PAGE_GEN && (in->pressed & FW_BIT(FW_BTN_OCT_UP)))
        gen_oct(in, 1);
    else if (in->pressed & FW_BIT(FW_BTN_OCT_UP))
        octave = clampi(octave + 1, -3, 3);
    if (in->pressed & (FW_BIT(FW_BTN_OCT_DOWN) | FW_BIT(FW_BTN_OCT_UP)))
        dirty |= FW_BIT(D_STATUS);
}

static void handle_keys(const fw_input_t *in)
{
    const uint32_t model_mode = (in->held & FW_BIT(FW_BTN_HOME)) != 0u;
    uint32_t k;
    if (page == PAGE_GEN)
        gen_keys(in);
    for (k = 0; k < FW_KEYS; k++) {
        if ((in->keys_released & FW_BIT(k)) && (key_held & FW_BIT(k))) {
            key_held &= ~FW_BIT(k);
            note_off(key_note[k]);
        }
        if (!(in->keys_pressed & FW_BIT(k)) || page == PAGE_GEN)
            continue;
        if (model_mode && k < MACRO_ENGINES) {
            patch.engine = (int16_t)k;
            dirty |= FW_BIT(D_HEADER) | 0x1Eu;
            continue;
        }
        key_note[k] = (uint8_t)clampi((int32_t)(FW_KEY_BASE_NOTE + k) + octave * 12, 0, 127);
        key_held |= FW_BIT(k);
        note_on(key_note[k], KEY_VELOCITY);
    }
}

static void handle_midi_cc(uint32_t cc, uint32_t v)
{
    if (cc == 1u)
        mod_wheel = (uint8_t)v;
    if (cc >= 16u && cc <= 18u)
        *knob_field(cc - 16u) = (int16_t)(v * 1000u / 127u);
    if (cc == 19u)
        patch.engine = (int16_t)(v * MACRO_ENGINES / 128u);
    if (cc >= 16u && cc <= 19u)
        dirty |= FW_BIT(D_HEADER) | 0x1Eu;
    if (cc == 120u || cc == 123u)
        macro_all_notes_off();
}

static void handle_midi(void)
{
    fw_midi_t m;
    while (fw_midi_read(&m)) {
        const uint32_t kind = m.status & 0xF0u, ch = (m.status & 15u) + 1u;
        if (m.status >= 0xF8u) {                 /* clock and transport: the generator's */
            gen_realtime(m.status, fm1_ms);
            continue;
        }
        if (kind < 0x80u || kind == 0xF0u)
            continue;
        if (global.channel && (uint32_t)global.channel != ch)
            continue;
        if (kind == 0x90u && m.d2)
            note_on(m.d1, m.d2);
        if (kind == 0x80u || (kind == 0x90u && !m.d2))
            note_off(m.d1);
        if (kind == 0xB0u)
            handle_midi_cc(m.d1, m.d2);
        if (kind == 0xD0u)
            aftertouch = m.d1;
        if (kind == 0xE0u)
            bend = (int16_t)(((int32_t)m.d2 << 7 | m.d1) - 8192);
    }
    macro_controller(mod_wheel, aftertouch, bend);
}

static void handle_knobs_main(const fw_input_t *in)
{
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        if (adjust(knob_field(i), knob_fast[FW_KNOB_1 + i], 10, 0, 1000))
            dirty |= FW_BIT(D_ROW0 + i);
    }
    if (adjust(&patch.transpose, in->knob[FW_KNOB_4], 1, -24, 24))
        dirty |= FW_BIT(D_ROW0 + 3u);
    if (adjust(&patch.engine, in->knob[FW_KNOB_SELECT], 1, 0, MACRO_ENGINES - 1) ||
        adjust(&patch.engine, in->knob[FW_KNOB_ALGO], 8, 0, MACRO_ENGINES - 1))
        dirty |= FW_BIT(D_HEADER) | 0x0Eu;
}

static void handle_knobs_matrix(const fw_input_t *in)
{
    macro_route_t *r = &patch.route[route_sel];
    int16_t src = r->src, dst = r->dst;
    const uint32_t before = route_sel;
    route_sel = (uint32_t)clampi((int32_t)route_sel + in->knob[FW_KNOB_SELECT] + in->knob[FW_KNOB_ALGO], 0,
                                 MACRO_ROUTES - 1);
    if (route_sel != before) {
        dirty |= FW_BIT(D_ROW0 + before) | FW_BIT(D_ROW0 + route_sel);
        return;
    }
    if (adjust(&src, in->knob[FW_KNOB_1], 1, 0, SRC_COUNT - 1) | adjust(&dst, in->knob[FW_KNOB_2], 1, 0, DST_COUNT - 1) |
        adjust(&r->amount, knob_fast[FW_KNOB_3], 5, -100, 100) | adjust(&r->amount, in->knob[FW_KNOB_4], 1, -100, 100)) {
        r->src = (int8_t)src;
        r->dst = (int8_t)dst;
        dirty |= FW_BIT(D_ROW0 + route_sel);
    }
}

static void handle_knobs_save(const fw_input_t *in)
{
    const int32_t d = in->knob[FW_KNOB_SELECT] + in->knob[FW_KNOB_ALGO] + in->knob[FW_KNOB_1];
    if (!d)
        return;
    save_slot = (uint32_t)(((int32_t)save_slot + d) % (int32_t)PRESETS + (int32_t)PRESETS) % PRESETS;
    dirty |= 0x0Eu;
}

static void handle_knobs_generic(const fw_input_t *in)
{
    const page_t *pg = &PAGE_DEF[page];
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        if (adjust_param(&pg->knob[i], in, FW_KNOB_1 + i))
            dirty |= FW_BIT(D_ROW0 + i);
    }
    if (adjust_param(&pg->enc[0], in, FW_KNOB_SELECT) | adjust_param(&pg->enc[1], in, FW_KNOB_ALGO))
        dirty |= FW_BIT(D_ENC);
}

static void handle_knobs(const fw_input_t *in)
{
    const int32_t p = in->knob[FW_KNOB_PRESET];
    knob_speed(in);
    if (p && page != PAGE_SAVE) {
        preset_select((uint32_t)(((int32_t)bank.current + p) % (int32_t)PRESETS + (int32_t)PRESETS));
        clear_page();
        return;
    }
    if (page == PAGE_MAIN)
        handle_knobs_main(in);
    if (page == PAGE_MATRIX)
        handle_knobs_matrix(in);
    if (page == PAGE_SAVE)
        handle_knobs_save(in);
    if (page == PAGE_GEN)
        gen_knobs(in);
    if (page != PAGE_MAIN && page != PAGE_MATRIX && page != PAGE_SAVE && page != PAGE_GEN)
        handle_knobs_generic(in);
}

static void update_leds(const fw_input_t *in)
{
    static const uint8_t PAGE_LED[PAGES] = {FW_BTN_HOME, FW_BTN_SCL, FW_BTN_FX, FW_BTN_ENV, FW_BTN_LFO, FW_BTN_EDIT,
                                            FW_BTN_GLO, FW_BTN_SAVE, FW_BTN_SEQ};
    uint32_t i;
    for (i = 0; i < PAGES; i++)
        fw_led_button(PAGE_LED[i], i == page);
    fw_led_button(FW_BTN_PLAY, patch.mode != MODE_TRIG);
    fw_led_button(FW_BTN_REC, hold);
    if (in->held & FW_BIT(FW_BTN_HOME)) {
        fw_leds_keys(FW_BIT(patch.engine));
        return;
    }
    if (page == PAGE_GEN) {
        fw_leds_keys(gen_leds());
        return;
    }
    fw_leds_keys(key_held);
}

static void app_init(void)
{
    bank_load();
    global = bank.global;
    preset_select(bank.current);
    macro_init();
    if (!bank.gen.cell[0].n)
        gen_new(3);
    gen_has_prev = 0;
    gen_ptrs();
    gen_handover(0);
}

static void app_frame(const fw_input_t *in)
{
    const uint32_t prev_engine = (uint32_t)patch.engine;
    frame_count++;
    global.volume = (int16_t)(in->master * in->master >> 12);
    handle_buttons(in);
    handle_knobs(in);
    handle_keys(in);
    handle_midi();
    gen_frame(in->ms);
    if (page == PAGE_GEN) {
        static uint8_t ext_shown;
        const uint8_t rh = (in->held & FW_BIT(FW_BTN_REC)) != 0u;
        if (rh != gen_rec_held) {
            gen_rec_held = rh;
            dirty |= FW_BIT(D_ROW0 + 2u);
        }
        if (gen_step_now != gen_step_shown) {
            gen_step_shown = gen_step_now;
            dirty |= FW_BIT(D_ROW0) | (gen_step_now == 0u ? FW_BIT(D_ROW0 + 1u) : 0u);
        }
        if (gen_ext != ext_shown) {
            ext_shown = gen_ext;
            dirty |= FW_BIT(D_HEADER);
        }
        dirty |= FW_BIT(D_SCOPE);
    }
    macro_monitor(&mon);
    if ((uint32_t)patch.engine != prev_engine)
        dirty |= FW_BIT(D_STATUS);
    if (flash_msg && (int32_t)(in->ms - flash_msg_until) > 0) {
        flash_msg = 0;
        dirty |= FW_BIT(D_STATUS);
    }
    if ((frame_count & 31u) == 0u || edited() != shown_edited) {
        shown_edited = edited();
        dirty |= FW_BIT(D_STATUS);
    }
    track_modulation();
    update_leds(in);
    draw();
}

static void app_audio(int32_t *lr, uint32_t frames)
{
    const int over = fw_stats.cpu_q8 > CPU_GUARD_Q8;
    uint32_t i, done = 0;
    if (over)
        overloads++;
    gen_isr_begin();
    while (done < frames) {                      /* in pieces, a piece to each clock pulse (the generator's notes on
                                                  * time, to the sample block) */
        uint32_t n = frames - done;
        if (gen_on) {
            const uint32_t u = gen_until();
            n = u < n ? u : n;
        }
        if (over)
            memset(lr + 2u * done, 0, n * 2u * sizeof *lr);
        else
            macro_render(gen_on && gen_type_now ? gen_patch_p[gen_type_now] : &patch, &global, lr + 2u * done, n);
        done += n;
        gi_frame = seq_samples + done;
        if (gen_on)
            gen_advance(n);
    }
    seq_samples += frames;
    if (over)
        return;
    macro_fx(lr, frames, (int32_t)(gen_spp_q8 * 18u / 256u), bank.gen.delay, bank.gen.reverb);   /* (a dotted eighth) */
    if (scope_ready)
        return;
    for (i = 0; i < SCOPE_N && i < frames; i++)
        scope[i] = (int16_t)clampi(lr[2u * i], -32768, 32767);
    scope_ready = 1;
}
