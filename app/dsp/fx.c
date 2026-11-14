/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 effects (fx.h): the send slots and the master section.
 *
 * Send slots: each takes a mono send and adds a stereo return. The reverb's plate is a pre-delay, four
 * diffusing allpasses and an eight-line feedback delay network in floats (do_reverb; until 2026-11 9W9's
 * four int16 combs, X0X's fxbus.c, whose knobs it keeps): "size" scales its lines (and the pre-delay and the
 * decay with them), "motion" wanders them (0.5 and 0.37 Hz), "damp" the damping in the loop, and "tone"
 * filters the wet: a low-pass below the centre, a high-pass above it, flat in the middle. The delay is a
 * ping-pong over the two int16 lines at half rate, timed in divisions of the tempo (1/32 to 1/2, dotted ones
 * between) and slewed so a change of time warps instead of clicking (9W9 again), its repeats low-passed by
 * "tone"; the chorus three voices a side on a short int16 line, swung by one sine a third of a turn apart
 * (it was the JP-8000's stepped, slewed triangle, one voice); the lofi a sample-and-hold with a quantiser, "smooth" its anti-alias
 * filters, its "width" a stereo spread: the right side reads the lofi output back from the chorus's
 * idle line a few milliseconds late, the lateness wandering slowly; the phaser four first-order
 * allpasses a side, the right side's sweep a quarter turn behind. Every effect returns its wet sound
 * only: the FX tracks are plain send / return (the phaser's wet is its notches, the send summed with
 * its allpassed copy).
 *
 * Knob values are turned into the effect's numbers in fx_set (main loop) and glide toward them in
 * the interrupt a block at a time (~10 ms): no zipper noise on a turn. A change of effect clears
 * the states at the next block; fx_set zeroes the new effect's lines itself, which is safe because
 * the running effect never uses another's line (the lofi borrows the chorus's, which the chorus
 * cannot be using at the same time). The slot's filter page (high-pass, low-pass) runs on the mono
 * send before the effect: one filter instead of two, and the same result for the linear effects.
 *
 * The reverb has modes: the plate above, the PlayStation's SPU reverbs (psxrev.h, from
 * schwung-psxverb: MIT), which run on the same line, all of it, and lush: Dattorro's plate (J. Dattorro, "Effect
 * Design, Part 1: Reverberator and Other Filters", JAES 45(9), 1997) at half the rate, on the same line (below).
 *
 * Master: the two groups' levels, a 3-band EQ (RBJ shelves and a peak; all three at 64 a true bypass), X0X's bus compressor behind its one-knob macro with X0X's PUMP in its gain
 * (a hit of the pump's source ducks the whole mix: the sequencer keys it, so it needs no detector and a
 * muted track keys it too; at 0 the stage is as before, sample for sample), the master level and X0X's
 * lookahead limiter.
 *
 * Everything here runs in the audio interrupt; fx_set / master_set write plain words from the main
 * loop. Float, fastmath.h; stack locals stay small (a 32-sample block). */
#include "fx.h"
#include "fastmath.h"

#define SR 44100.0f
#define SUB 32                                /* the block: parameter glides step here */
#define GLIDE 0.07f                           /* a block's share of the distance: ~10 ms to settle */
#define CHO_N 2048                            /* the chorus line: 46 ms (int16; the lofi's spread reads it too) */
#define CHO_V 3                               /* the chorus's voices a side */
#define CHO_RSHIFT (2.0f * SR / 1000.0f)      /* the right's voices that much later at full width (the three voices
                                               * average each side's swing out: this keeps the sides apart) */
#define CHO_WET 0.5f                          /* (three voices: their sum, at about the one voice's level) */
#define RV_WET 0.8f                           /* lush's return level */
#define PL_WET 0.62f                          /* the plate's */
#define LOFI_SPREAD 480.0f                    /* width 127: the right side ~11 ms behind the left */

static void zero16(int16_t *b, int32_t n);
#include "psxrev.h"

const char *const FXT_NAME[NFXT] = {"chorus", "delay", "lofi", "phaser", "reverb"};
const char *const RV_NAME[NRV] = {"plate", "room", "studio S", "studio M", "studio L", "hall", "half echo",
                                  "space echo", "chaos echo", "one echo", "lush"};
static const char *const FXT_KNOB[NFXT][4] = {
    {"rate", "depth", "feedback", "width"}, {"time", "fine", "repeats", "tone"},
    {"rate", "bits", "smooth", "width"},    {"freq", "depth", "rate", "feedback"},
    {"size", "motion", "tone", "damp"},
};
static const char *const SPU_KNOB[4] = {"size", "decay", "tone", "damp"};
/* the defaults, 0..127 (what each gives: fx_derive's mappings) */
const uint8_t FXT_DEFAULT[NFXT][4] = {
    {50, 86, 20, 127},                        /* chorus      0.33 Hz, +-5.5 ms (three voices), feedback 0.11, fully wide */
    {72, 64, 56, 64},                         /* delay       1/8, on the beat, feedback 0.42 (~4 repeats), 4 kHz */
    {80, 80, 64, 40},                         /* lofi        8.4 kHz, 10.8 bits, half smoothed, 3.4 ms spread */
    {50, 90, 50, 80},                         /* phaser      466 Hz, +-2.1 octaves, 0.27 Hz, feedback 0.57 */
    {100, 40, 44, 72},                        /* plate       lines x1.43 (pre-delay 18 ms, ~1.5 s), 2.4 samples of
                                               *             wander, 7.4 kHz, damp 0.45 */
};
const uint8_t RV_SPU_DEFAULT[4] = {127, 64, 64, 0};   /* the console's sizes, its decay, flat, its own filter */
const uint8_t RV_LUSH_DEFAULT[4] = {64, 64, 56, 48};  /* (fx.h) */

const char *fx_pick_name(int pick)
{
    int t = fx_pick_type(pick);
    return t == FXT_REVERB ? RV_NAME[fx_pick_mode(pick)] : FXT_NAME[t];
}
const char *fx_knob_name(int pick, int k)
{
    int t = fx_pick_type(pick);
    k &= 3;
    return t == FXT_REVERB && fx_pick_mode(pick) != RV_PLATE ? SPU_KNOB[k] : FXT_KNOB[t][k];
}

/* the plate: floats on the reverb line (FX_RV_LEN int16 = PL_RING floats: one ring whose base moves a sample a
 * sample, as lush's), its lines in order: the pre-delay, four input diffusers (Dattorro's lengths, as samples at 44.1 kHz), then the eight
 * lines of the feedback network, each with room for the largest size (x1.9) and the wander (24 samples) */
#define PL_RING (FX_RV_LEN / 2)
#define PL_MASK (PL_RING - 1)
#define PL_PRE_MIN 265.0f                     /* the pre-delay at size 0: 6 ms, so the second core's lag (256) hides in it */
#define PL_PRE_MAX 1058.0f                    /* .. at size 127: 24 ms */
#define PL_LREF 920.0f                        /* the lines decay as the old combs did at a feedback (measured: their tails) */
#define PL_SLEEP 16384                        /* quiet samples (in and out under -120 dB) before the plate sleeps */
static const float PL_TAPF[8] = {0.43f, 0.61f, 0.29f, 0.53f, 0.37f, 0.67f, 0.47f, 0.31f};
static const int32_t PL_DIF[4] = {142, 107, 379, 277};
static const float PL_DIFG[4] = {0.75f, 0.75f, 0.625f, 0.625f};
static const float PL_LEN[8] = {563.0f, 647.0f, 733.0f, 829.0f, 919.0f, 1013.0f, 1129.0f, 1201.0f};
enum { PO_PRE = 0, PO_D0 = 1088, PO_D1 = PO_D0 + 143, PO_D2 = PO_D1 + 108, PO_D3 = PO_D2 + 380, PO_L0 = PO_D3 + 278 };
static const int32_t PL_OFF[8] = {PO_L0, PO_L0 + 1097, PO_L0 + 2354, PO_L0 + 3774, PO_L0 + 5377, PO_L0 + 7151,
                                  PO_L0 + 9103, PO_L0 + 11276};   /* (each line 1.9 x its length + 27) */
_Static_assert(PO_L0 + 11276 + 2309 <= PL_RING, "the plate's lines do not fit FX_RV_LEN");
_Static_assert((PL_RING & PL_MASK) == 0, "the plate's ring must be a power of two");
typedef struct {
    uint32_t base;
    int32_t clear, quiet;             /* samples of the line still to clear (another mode ran on it); quiet samples */
    float d[8], z[8], g[8], gfb;      /* the lines' read delays (gliding), their damping, their gains (at feedback gfb) */
    float hp, tl[2], th[2], ph1, ph2; /* the send's high-pass, the tone a side, the wander's two phases */
} plate_t;
/* the delay's times in sixteenths: 1/32, 1/32D, 1/16, 1/16D, 1/8, 1/8D, 1/4, 1/2 (dotted = 1.5x) */
static const float DL_DIV[8] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f};

/* lush: Dattorro's plate at 22.05 kHz on the reverb line (int16, through psxrev.h's spu_rd / spu_wr: one ring whose
 * base moves a sample a tick; a line at offset o is written at o and read len ticks later at o + len). The lines in
 * Dattorro's order, each with room for the largest size (x1.5 of his lengths at 29761 Hz, scaled to 22050 Hz) and the
 * modulation: the pre-delay, four input diffusers, then each half of the tank: a modulated allpass, a delay, an
 * allpass, a delay */
enum { LU_PRE, LU_IN1, LU_IN2, LU_IN3, LU_IN4, LU_MAPL, LU_DL1, LU_APL, LU_DL2, LU_MAPR, LU_DR1, LU_APR, LU_DR2, NLU };
static const int32_t LU_OFF[NLU + 1] = {0, 1200, 1397, 1555, 2016, 2363, 3149, 8137, 10177, 14351, 15400, 20126,
                                        23117, 26672};
static const float LU_LEN[NLU] = {0.0f, 142.0f, 107.0f, 379.0f, 277.0f, 672.0f, 4453.0f, 1800.0f, 3720.0f, 908.0f,
                                  4217.0f, 2656.0f, 3163.0f};   /* Dattorro's, at 29761 Hz */
_Static_assert(26672 <= FX_RV_LEN, "lush's lines do not fit FX_RV_LEN");
typedef struct {
    int32_t len[NLU];                 /* the lines' lengths at the size now, 22.05 kHz samples */
    uint32_t base;
    int32_t clear, quiet;             /* (as the SPU's) */
    float lfo, dmp[2], fb[2], out[2][2], tl, th, tr, thr, scale;
    float in[128];                    /* the decimator's input and the interpolators' (psxrev.h's half-band FIR, as the */
    float up[2][64];                  /* SPU modes': rings written twice, so the taps read them without a mask) */
    uint8_t phase, ip, up_w;
} lush_t;

/* the delay's FIRs: the decimator, a half-band (11 taps: -0.2 dB at 8 kHz, -3 at 10, 17 dB down at 13 kHz and 28 or more
 * above 14; the pair's mean it replaces: 4 dB down at 13 kHz), and the midpoint between two slots that a read falls
 * between (4 slots: within +0.5 / -2.8 dB to 8 kHz; a straight line: -7.6 dB at 8 kHz). The decimator's input at 44.1 kHz (the send's), written twice so the taps read
 * it without a mask (in the reverb's union: a slot runs one effect) */
static const float DL_HB[3] = {0.29933f, -0.08493f, 0.0356f};   /* the decimator: 11 taps, Kaiser beta 2 */
static const float DL_MID[2] = {0.61f, -0.11f};       /* the midpoint: 4 slots (within +0.5 / -2.8 dB to 8 kHz) */
#define DL_PH 2.0f                            /* the read's place against the slot being written: its decimator's
                                               * 5 samples behind */
typedef struct {
    float ix[32];                             /* the send, for its decimator */
    float fn[2], ft[2], q1[2], q2[2];         /* the repeats (left, right: they come off the line already under 11 kHz):
                                               * the last frame's, the last tick's; 1/4 1/2 1/4 of them, a tick and two
                                               * ago (so they meet the send at its centre, 5 frames back) */
    float vl[3], vr[3];                       /* each line at the read: slot ms, the midpoint after it, the next slot */
    int32_t ms;
    uint8_t ip;
} dly_t;

struct fxslot {
    int16_t cho[CHO_N];
    int16_t *dl, *dr, *rv;
    uint8_t type, type_t, hpv, lpv;
    uint8_t mode, mode_t;                     /* the reverb's mode: running, and as set */
    uint8_t knob[4];
    float bpm;
    float pt[6], pc[6];                       /* the effect's numbers: targets, and gliding */
    float hpc, lpc, hps, lps;                 /* the filter page */
    /* chorus */
    int32_t cw;
    float cph, cm[CHO_V], chl, chr, cfbz;    /* the LFO's phase, each voice's swing (samples) at the block's start */
    /* delay */
    int32_t dw, dquiet, dph;                  /* (the line at half rate: dph, the pair's second frame) */
    float dcur, dlpl, dlpr;
    float dwa, dwb;                           /* PING WIDTH: a side's echo into its own side, into the other (fx_width) */
    /* lofi */
    float lph, lhold, lin1, lout1, lwph;
    /* phaser */
    float pxl[4], pyl[4], pxr[4], pyr[4], pph, pzl, pzr, pal, par_;
    /* reverb */
    uint8_t rv_dirty;                         /* another reverb mode ran on the line: the next one clears it first */
    union {                                   /* the plate, the SPU reverb or lush (one mode at a time: fx_states) */
        plate_t pl;
        spu_t spu;
        lush_t lu;
        dly_t dy;                             /* (the delay's, while the slot runs the delay) */
    };
};
_Static_assert(sizeof(struct fxslot) <= FXSLOT_BYTES, "fxslot_t does not fit FXSLOT_BYTES");

static inline float onepole_k(float hz) { return hz >= 20000.0f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * hz / SR); }
static inline float k127(int v) { return (float)(v < 0 ? 0 : v > 127 ? 127 : v) * (1.0f / 127.0f); }
static inline int16_t f16(float x)
{
    float v = x * 16384.0f;
    return (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
}
static inline float s16f(int16_t v) { return (float)v * (1.0f / 16384.0f); }

static void zero16(int16_t *b, int32_t n)
{
    int32_t i;
    for (i = 0; i < n; i++)
        b[i] = 0;
}

static void fx_states(fxslot_t *f)              /* a fresh effect: its states at rest */
{
    int i;
    zero16(f->cho, CHO_N);
    f->cw = 0; f->cph = 0.0f; f->chl = f->chr = f->cfbz = 0.0f;
    for (i = 0; i < CHO_V; i++)
        f->cm[i] = f->pt[1] * fm_sin_turns((float)i * (1.0f / CHO_V));
    f->dw = 0; f->dquiet = 2 * FX_DLY_LEN; f->dlpl = f->dlpr = 0.0f;
    f->dph = 0;
    f->dcur = f->pt[0];                       /* a new delay starts on time (the slew is for changes) */
    f->lph = f->lhold = f->lin1 = f->lout1 = f->lwph = 0.0f;
    for (i = 0; i < 4; i++)
        f->pxl[i] = f->pyl[i] = f->pxr[i] = f->pyr[i] = 0.0f;
    f->pph = 0.0f; f->pzl = f->pzr = 0.0f; f->pal = f->par_ = 0.0f;
    for (i = 0; i < 6; i++)
        f->pc[i] = f->pt[i];
    if (f->mode == RV_PLATE) {
        plate_t *p = &f->pl;
        uint8_t *z = (uint8_t *)p;
        for (i = 0; i < (int)sizeof *p; i++)
            z[i] = 0;
        p->clear = f->rv_dirty ? FX_RV_LEN : 0;   /* (a line fx_set or fx_clear zeroed needs no clearing) */
        p->quiet = PL_SLEEP;
        p->gfb = -1.0f;                           /* (the gains computed at the first block) */
        for (i = 0; i < 8; i++)
            p->d[i] = f->pt[0] * PL_LEN[i];
    } else if (f->mode != RV_LUSH) {
        spu_reset(&f->spu, f->mode);
    } else {
        lush_t *u = &f->lu;
        uint8_t *z = (uint8_t *)u;
        for (i = 0; i < (int)sizeof *u; i++)
            z[i] = 0;
        u->clear = FX_RV_LEN;
        u->quiet = FX_RV_LEN * 2;
    }
    if (f->type == FXT_DELAY) {               /* (the union is the delay's FIRs, not a reverb's: at rest) */
        uint8_t *z = (uint8_t *)&f->dy;
        for (i = 0; i < (int)sizeof f->dy; i++)
            z[i] = 0;
        f->dy.ms = -1;
    }
}

static void delay_time(fxslot_t *f)
{
    float bpm = f->bpm > 20.0f ? f->bpm : 120.0f;
    float t = DL_DIV[(f->knob[0] * 8) >> 7] * 15.0f / bpm;           /* sixteenths: 60 / (bpm * 4) */
    float max_s = (float)(FX_DLY_LEN - 128) * 2.0f / SR;            /* (the line at half rate) */
    t *= 1.0f + ((float)f->knob[1] - 64.0f) * (0.25f / 64.0f);        /* fine: +-25 % */
    while (t > max_s)                                                 /* too long for the line: half, */
        t *= 0.5f;                                                    /* still on the beat */
    f->pt[0] = t * SR;
}

/* the knobs to the effect's numbers (its pt[]: what each means is in the effect's loop) */
static void fx_derive(fxslot_t *f)
{
    const uint8_t *k = f->knob;
    float a = k127(k[0]), b = k127(k[1]), c = k127(k[2]), d = k127(k[3]);
    switch (f->type_t) {
    case FXT_CHORUS:
        f->pt[0] = 0.05f * fm_exp2f(a * 6.9f);                        /* rate 0.05 .. 6 Hz */
        f->pt[1] = (0.3f + 7.7f * b) * (SR / 1000.0f);                /* depth 0.3 .. 8 ms */
        f->pt[2] = 0.7f * c;                                          /* feedback */
        f->pt[3] = d;                                                 /* width: 0 mono .. 1 opposite */
        break;
    case FXT_DELAY:
        delay_time(f);                                                /* time and fine */
        f->pt[2] = 0.95f * c;                                         /* repeats: the feedback */
        f->pt[3] = onepole_k(800.0f * fm_exp2f(d * 4.6f));            /* tone: the repeats' low-pass 800 Hz .. 19 kHz */
        break;
    case FXT_LOFI: {
        float fs = 500.0f * fm_exp2f(a * 6.46f);                      /* 500 Hz .. 44.1 kHz */
        float bits = 2.0f + 14.0f * b;
        f->pt[0] = fm_minf(fs / SR, 1.0f);
        f->pt[1] = fm_exp2f(bits - 1.0f);                             /* quantiser levels */
        f->pt[2] = c;                                                 /* smooth: the anti-alias filters */
        f->pt[3] = d * LOFI_SPREAD;                                   /* width: the right's lateness, samples */
        f->pt[4] = onepole_k(fm_minf(0.45f * fs, 19000.0f));
        f->pt[5] = 1.0f / f->pt[1];
        break;
    }
    case FXT_PHASER:
        f->pt[0] = 100.0f * fm_exp2f(a * 5.64f);                      /* 100 Hz .. 5 kHz */
        f->pt[1] = 3.0f * b;                                          /* depth, octaves */
        f->pt[2] = 0.03f * fm_exp2f(c * 8.06f);                       /* rate 0.03 .. 8 Hz */
        f->pt[3] = 0.9f * d;                                          /* feedback */
        break;
    case FXT_REVERB:
        if (f->mode_t == RV_LUSH) {
            f->pt[0] = 0.5f + a;                                      /* size: Dattorro's lengths x0.5 .. x1.5 */
            /* decay: the tail's time, the tank's gain from it and the size (a trip round both halves ~0.715 s x size,
             * the gain met twice: 40 log10(g) = -60 trip / T, T 0.4 .. 19 s); the damping takes more, so measured
             * (an impulse, 60 dB down): 0 0.56 s, 45 0.9 s, 64 ~1.4 s, 100 3.7 s, 127 7.2 s, whatever the size */
            f->pt[1] = fm_exp2f(-1.5f * 3.32192809f * 0.715f * (0.5f + a) / (0.4f * fm_exp2f(b * 5.6f)));
            f->pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;   /* tone: the plate's */
            f->pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
            f->pt[3] = 0.05f + 0.85f * d;                             /* damp: the tank's low-pass, 0.05 open .. 0.9 */
            f->pt[4] = (0.004f + 0.03f * a) * 22050.0f;               /* the pre-delay grows with the size: 4 .. 34 ms */
            break;
        }
        if (f->mode_t != RV_PLATE) {                                  /* an SPU mode: */
            f->pt[0] = 1.0f + 3.0f * a;                               /* size: the registers x1 .. x4 */
            f->pt[1] = b;                                             /* decay: the wall feedback (spu_derive) */
            f->pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;   /* tone: the plate's */
            f->pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
            f->pt[3] = d;                                             /* damp: vIIR closes */
            f->pt[4] = 0.0f;
            break;
        }
        f->pt[0] = 0.5f * fm_exp2f(a * 1.926f);                       /* size: the lines x 0.5 .. 1.9 */
        f->pt[1] = 24.0f * b * b;                                     /* motion, samples of wander */
        /* tone: below the centre a low-pass closes to 800 Hz, above it a high-pass opens to 2 kHz */
        f->pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;
        f->pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
        f->pt[3] = 0.8f * d;                                          /* damp: the lines' damping */
        f->pt[4] = 0.55f + 0.42f * a;                                 /* the loop's feedback: decay with size */
        break;
    default:
        break;
    }
}

void fx_init(fxslot_t *f, int16_t *dly_l, int16_t *dly_r, int16_t *rv)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)f;
    for (i = 0; i < sizeof *f; i++)
        z[i] = 0;
    if ((uintptr_t)rv & 3u)                   /* (the plate keeps floats on the line: fx.h asks for 4-byte alignment) */
        rv = 0;
    f->dl = dly_l;
    f->dr = dly_r;
    f->rv = rv;
    f->bpm = 120.0f;
    f->lpv = 127;
    f->hpc = onepole_k(20.0f);
    f->lpc = onepole_k(20000.0f);
    f->dwa = 1.0f;                            /* (full width: the bounce hard left and right) */
    if (dly_l) zero16(dly_l, FX_DLY_LEN);
    if (dly_r) zero16(dly_r, FX_DLY_LEN);
    if (rv) zero16(rv, FX_RV_LEN);
    for (i = 0; i < 4; i++)
        f->knob[i] = FXT_DEFAULT[FXT_CHORUS][i];
    f->type = f->type_t = FXT_CHORUS;
    f->mode = f->mode_t = RV_PLATE;
    fx_derive(f);
    fx_states(f);
}

void fx_clear(fxslot_t *f)
{
    if (f->dl) zero16(f->dl, FX_DLY_LEN);
    if (f->dr) zero16(f->dr, FX_DLY_LEN);
    if (f->rv) zero16(f->rv, FX_RV_LEN);
    f->hps = f->lps = 0.0f;
    f->rv_dirty = 0;
    fx_states(f);
}

int fx_has(const fxslot_t *f, int pick)
{
    int type = fx_pick_type(pick);
    if (pick < 0 || pick >= NFXPICK)
        return 0;
    if (type == FXT_REVERB)
        return f->rv != 0;
    if (type == FXT_DELAY)
        return f->dl != 0 && f->dr != 0;
    return 1;
}

void fx_set(fxslot_t *f, int pick, const uint8_t knob[4])
{
    int i, type, mode;
    if (!fx_has(f, pick))
        pick = FXT_CHORUS;
    type = fx_pick_type(pick);
    mode = fx_pick_mode(pick);
    for (i = 0; i < 4; i++)
        f->knob[i] = knob[i] > 127 ? 127 : knob[i];
    if (type != f->type_t) {                  /* the lines the new effect reads are idle now: clear them */
        if (type == FXT_DELAY) {
            zero16(f->dl, FX_DLY_LEN);
            zero16(f->dr, FX_DLY_LEN);
        } else if (type == FXT_REVERB) {
            zero16(f->rv, FX_RV_LEN);
        }
    }
    /* another reverb mode while the reverb runs: its line is in use, so the interrupt clears it
     * (fx_states: spu_reset, a slice a block) when it takes the new mode at its next block */
    f->mode_t = (uint8_t)mode;
    f->type_t = (uint8_t)type;
    fx_derive(f);
}

void fx_tempo(fxslot_t *f, float bpm)
{
    if (bpm > 20.0f && bpm != f->bpm) {
        f->bpm = bpm;
        if (f->type_t == FXT_DELAY)
            delay_time(f);
    }
}

/* PING WIDTH: the echoes' mid kept, their sides times w (0 centred .. 127 hard left and right), at the same power: an
 * echo on one side (e) gives e (1 + w) / 2 and e (1 - w) / 2, power e^2 (1 + w^2) / 2, so both times sqrt(2 / (1 + w^2)) */
void fx_width(fxslot_t *f, int w)
{
    float x = k127(w), g = fm_sqrtf(2.0f / (1.0f + x * x));
    f->dwa = w >= 127 ? 1.0f : 0.5f * g * (1.0f + x);
    f->dwb = w >= 127 ? 0.0f : 0.5f * g * (1.0f - x);
}

void fx_filter(fxslot_t *f, int hp, int lp)
{
    f->hpv = (uint8_t)(hp < 0 ? 0 : hp > 127 ? 127 : hp);
    f->lpv = (uint8_t)(lp < 0 ? 0 : lp > 127 ? 127 : lp);
    f->hpc = onepole_k(20.0f * fm_exp2f(k127(hp) * 8.64f));            /* 20 Hz .. 8 kHz */
    f->lpc = onepole_k(20000.0f * fm_exp2f(-(1.0f - k127(lp)) * 8.0f)); /* 78 Hz .. 20 kHz */
}

/* ---- the effects: a block of m samples of x[] (the filtered send) into out (added) ---- */
static inline float cho_tap(const fxslot_t *f, float p)      /* the short line, read between samples */
{
    int32_t i0 = (int32_t)p;
    float a = s16f(f->cho[i0 & (CHO_N - 1)]);
    return a + (p - (float)i0) * (s16f(f->cho[(i0 + 1) & (CHO_N - 1)]) - a);
}

/* lag (fx_process_lag): the send arrives that many samples late; the effects with a line take it back by writing
 * the send that far into the line's past (a slot not read yet: every tap reads further back), so their output is
 * where it would have been, but for the line's rounding: the delay and the chorus add it to the feedback already
 * there (one int16 rounding more, and the delay's soft clip on the sum read back); the plate writes it into its
 * pre-delay's past (floats: the same output, sample for sample) */
static inline int16_t add16(int16_t a, float x)                /* a + x on the int16 line (x in its units) */
{
    float v = (float)a + x * 16384.0f;
    return (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
}
/* the chorus: three voices a side on the one line, each a tap swung by the same sine a third of a turn apart (so the
 * three never meet, and their sum stays level); the right's voices swing by "width": the left's (0), none (64), the
 * left's mirrored (127). The sine is computed a block and followed in straight lines between (at 6 Hz a block is a
 * 230th of a turn): the rate is the rate, with no stepping and no slew to blunt it */
static void do_chorus(fxslot_t *f, const float *x, float *ol, float *orr, int m, int lag)
{
    const float range = f->pc[1], fb = f->pc[2], side = 1.0f - 2.0f * f->pc[3], im = 1.0f / (float)m;
    const float centre = 15.0f * (SR / 1000.0f), cr = f->pc[3] * CHO_RSHIFT;
    const float hpk = 0.028f;                 /* ~200 Hz: the wet without its lows */
    float ph = f->cph, hl = f->chl, hr = f->chr, fz = f->cfbz, m0 = f->cm[0], m1 = f->cm[1], m2 = f->cm[2], d0, d1, d2;
    int32_t w = f->cw;
    int i;
    ph += f->pc[0] * (float)m / SR;
    if (ph >= 1.0f)
        ph -= 1.0f;
    d0 = (range * fm_sin_turns(ph) - m0) * im;
    d1 = (range * fm_sin_turns(ph + (1.0f / 3.0f)) - m1) * im;
    d2 = (range * fm_sin_turns(ph + (2.0f / 3.0f)) - m2) * im;
    for (i = 0; i < m; i++) {
        float c, wl, wr;
        if (lag) {
            f->cho[w] = f16(fz * fb);
            f->cho[(w - lag) & (CHO_N - 1)] = add16(f->cho[(w - lag) & (CHO_N - 1)], x[i]);
        } else {
            f->cho[w] = f16(x[i] + fz * fb);
        }
        w = (w + 1) & (CHO_N - 1);
        m0 += d0;
        m1 += d1;
        m2 += d2;
        c = (float)(w + CHO_N) - centre;      /* (a line's length on: every tap reads at a positive place) */
        wl = cho_tap(f, c - m0) + cho_tap(f, c - m1) + cho_tap(f, c - m2);
        c -= cr;
        wr = cho_tap(f, c - side * m0) + cho_tap(f, c - side * m1) + cho_tap(f, c - side * m2);
        fz = wl * (1.0f / CHO_V);
        hl += (wl - hl) * hpk;
        hr += (wr - hr) * hpk;
        ol[i] += (wl - hl) * CHO_WET;
        orr[i] += (wr - hr) * CHO_WET;
    }
    f->cph = ph; f->cm[0] = m0; f->cm[1] = m1; f->cm[2] = m2;
    f->chl = fm_flush(hl); f->chr = fm_flush(hr); f->cfbz = fz;
    f->cw = w;
}

/* the delay: its lines at half rate (22.05 kHz). The send goes in through DL_HB (it was a pair's mean: what was above
 * 11 kHz folded back into the echoes; the repeats, already under it, go in through three taps), and a read is made at 44.1 kHz: the slots are the even samples of
 * the band-limited signal, the odd ones the FIR's midpoints between them (one a tick, as the read moves along), and a
 * straight line goes between those two (it was a straight line between slots: an echo between them lost its top,
 * 7 dB at 8 kHz, and kept images above 11 kHz). So a change of time still warps smoothly, and the echoes land on the
 * beat (the decimator's 7 samples are taken off the read). The repeats are low-passed by the tone, 4 kHz by default */
static inline float dl_dec(const float *z)    /* the decimator around its centre z[0] (z[-5..5]) */
{
    return 0.5f * z[0] + DL_HB[0] * (z[-1] + z[1]) + DL_HB[1] * (z[-3] + z[3]) + DL_HB[2] * (z[-5] + z[5]);
}
static inline void dl_mid(const int16_t *b, int32_t s, float *v)   /* slots s, s + 1 and the line between them */
{
    int32_t i0 = s - 1, i2 = s + 1, i3 = s + 2;
    if (i0 < 0) i0 += FX_DLY_LEN;
    if (i2 >= FX_DLY_LEN) i2 -= FX_DLY_LEN;
    if (i3 >= FX_DLY_LEN) i3 -= FX_DLY_LEN;
    v[0] = s16f(b[s]);
    v[2] = s16f(b[i2]);
    v[1] = DL_MID[0] * (v[0] + v[2]) + DL_MID[1] * (s16f(b[i0]) + s16f(b[i3]));
}
static void do_delay(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any, int lag)
{
    dly_t *y = &f->dy;
    const float target = f->pt[0], fb = f->pc[2], tc = f->pc[3], wa = f->dwa, wb = f->dwb;
    float dcur = f->dcur, lpl = f->dlpl, lpr = f->dlpr;
    float fnl = y->fn[0], fnr = y->fn[1], ftl = y->ft[0], ftr = y->ft[1], q1l = y->q1[0], q1r = y->q1[1], q2l = y->q2[0],
          q2r = y->q2[1];
    int32_t w = f->dw, quiet = f->dquiet, ph = f->dph, ms = y->ms;
    uint32_t ip = y->ip;
    int i;
    if (!any && quiet >= 2 * FX_DLY_LEN) {    /* silent in, an empty line: time moves on, nothing to read */
        w += (ph + m) >> 1;
        f->dph = (ph + m) & 1;
        f->dw = w % FX_DLY_LEN;
        f->dcur += (target - dcur) * (1.0f - fm_expf(-0.0008f * (float)m));
        y->ms = -1;                           /* (the midpoint taken afresh when it wakes) */
        return;
    }
    for (i = 0; i < m; i++) {
        float rp, fr, tl, tr, a, b;
        int32_t s0, k;
        dcur += (target - dcur) * 0.0008f;
        rp = (float)w + 0.5f * (float)ph + DL_PH - 0.5f * dcur;   /* the read, in slots */
        if (rp < 0.0f)
            rp += (float)FX_DLY_LEN;
        s0 = (int32_t)rp;
        fr = 2.0f * (rp - (float)s0);
        if (s0 >= FX_DLY_LEN)
            s0 -= FX_DLY_LEN;
        if (s0 != ms) {                       /* the read moved on: its slots and the midpoints between */
            dl_mid(f->dl, s0, y->vl);
            dl_mid(f->dr, s0, y->vr);
            ms = s0;
        }
        k = fr < 1.0f ? 0 : 1;                /* between the slot and the midpoint, or the midpoint and the next */
        fr -= (float)k;
        tl = y->vl[k] + fr * (y->vl[k + 1] - y->vl[k]);
        tr = y->vr[k] + fr * (y->vr[k + 1] - y->vr[k]);
        lpl += tc * (tl - lpl);               /* tone: the repeats darken */
        lpr += tc * (tr - lpr);
        y->ix[ip] = y->ix[ip + 16u] = x[i];
        ip = (ip + 1u) & 15u;
        if (ph) {                             /* the pair's second frame: into the slot */
            float xd = dl_dec(y->ix + ip + 10u), cl, cr;
            int16_t ka, kb;
            cl = 0.25f * (ftl + lpr) + 0.5f * fnl;   /* ping-pong: the right into the left (and the send), */
            cr = 0.25f * (ftr + lpl) + 0.5f * fnr;   /* the left into the right */
            ftl = lpr;
            ftr = lpl;
            a = (lag ? 0.0f : xd) + fb * q2l;
            b = fb * q2r;
            q2l = q1l; q1l = cl;
            q2r = q1r; q1r = cr;
            if (a > 1.0f || a < -1.0f) a = fm_tanhf(a);
            if (b > 1.0f || b < -1.0f) b = fm_tanhf(b);
            f->dl[w] = ka = f16(a);
            f->dr[w] = kb = f16(b);
            if (lag) {                        /* the send into the line's past (not read yet: the time is longer) */
                int32_t p = w - (lag >> 1);
                float v;
                if (p < 0)
                    p += FX_DLY_LEN;
                v = s16f(f->dl[p]) + xd;
                if (v > 1.0f || v < -1.0f) v = fm_tanhf(v);
                f->dl[p] = f16(v);
            }
            quiet = (ka == 0 && kb == 0 && x[i] == 0.0f) ? quiet + 2 : 0;
            if (++w >= FX_DLY_LEN)
                w = 0;
        } else {
            fnl = lpr;
            fnr = lpl;
        }
        ph ^= 1;
        ol[i] += wa * tl + wb * tr;           /* PING WIDTH: at full width wa 1, wb 0, the sides as they are */
        orr[i] += wa * tr + wb * tl;
    }
    f->dw = w;
    f->dph = ph;
    y->ip = (uint8_t)ip;
    y->fn[0] = fnl; y->fn[1] = fnr; y->ft[0] = ftl; y->ft[1] = ftr;
    y->q1[0] = q1l; y->q1[1] = q1r; y->q2[0] = q2l; y->q2[1] = q2r;
    y->ms = ms;
    f->dcur = dcur;
    f->dlpl = fm_flush(lpl);
    f->dlpr = fm_flush(lpr);
    f->dquiet = quiet;
}

static void do_lofi(fxslot_t *f, const float *x, float *ol, float *orr, int m)
{
    const float q = f->pc[1], iq = f->pc[5], qual = f->pc[2], aak = f->pt[4], step = f->pc[0];
    float ph = f->lph, hold = f->lhold, in1 = f->lin1, out1 = f->lout1, late;
    int32_t w = f->cw;
    int i;
    f->lwph += 0.3f * (float)m / SR;          /* the spread wanders: a 0.3 Hz wow of the right's lateness */
    if (f->lwph >= 1.0f)
        f->lwph -= 1.0f;
    late = f->pc[3] * (1.0f + 0.04f * fm_sin_turns(f->lwph));
    for (i = 0; i < m; i++) {
        float xf, y, r;
        in1 += aak * (x[i] - in1);
        xf = x[i] + qual * (in1 - x[i]);      /* smooth: how much is band-limited before the hold */
        ph += step;
        if (ph >= 1.0f) {
            ph -= 1.0f;
            hold = fm_floorf(xf * q + 0.5f) * iq;
        }
        out1 += aak * (hold - out1);
        y = hold + qual * (out1 - hold);
        f->cho[w] = f16(y);                   /* the right side reads the left back, late */
        if (late < 1.0f) {
            r = y;
        } else {
            float rp = (float)w - late;
            if (rp < 0.0f)
                rp += CHO_N;
            r = cho_tap(f, rp);
        }
        w = (w + 1) & (CHO_N - 1);
        ol[i] += y;
        orr[i] += r;
    }
    f->lph = ph; f->lhold = hold; f->lin1 = fm_flush(in1); f->lout1 = fm_flush(out1);
    f->cw = w;
}

static inline float ap_coef(float hz)
{
    float t = fm_tanf(FM_PI * fm_clampf(hz, 20.0f, 18000.0f) / SR);
    return (t - 1.0f) / (t + 1.0f);
}

static void do_phaser(fxslot_t *f, const float *x, float *ol, float *orr, int m)
{
    const float centre = f->pc[0], depth = f->pc[1], fb = f->pc[3];
    float al_t, ar_t, al = f->pal, ar = f->par_, dal, dar, zl = f->pzl, zr = f->pzr, im = 1.0f / (float)m;
    int i, s;
    f->pph += f->pc[2] * (float)m / SR;
    if (f->pph >= 1.0f)
        f->pph -= 1.0f;
    al_t = ap_coef(centre * fm_exp2f(depth * fm_sin_turns(f->pph)));
    ar_t = ap_coef(centre * fm_exp2f(depth * fm_sin_turns(f->pph + 0.25f)));
    dal = (al_t - al) * im;
    dar = (ar_t - ar) * im;
    for (i = 0; i < m; i++) {
        float xl = x[i] + zl * fb, xr = x[i] + zr * fb;
        al += dal;
        ar += dar;
        for (s = 0; s < 4; s++) {
            float yl = al * xl + f->pxl[s] - al * f->pyl[s], yr = ar * xr + f->pxr[s] - ar * f->pyr[s];
            f->pxl[s] = xl; f->pyl[s] = yl; xl = yl;
            f->pxr[s] = xr; f->pyr[s] = yr; xr = yr;
        }
        zl = fm_flush(xl);
        zr = fm_flush(xr);
        ol[i] += 0.5f * (x[i] + xl);
        orr[i] += 0.5f * (x[i] + xr);
    }
    f->pal = al; f->par_ = ar; f->pzl = zl; f->pzr = zr;
}

/* the plate: the send high-passed (~120 Hz) into a pre-delay that grows with the size (6 .. 24 ms), four allpasses
 * that diffuse it (Dattorro's input diffusers, his lengths as 44.1 kHz samples), then an eight-line feedback delay
 * network: each line read between samples at its length (x size) wandering by "motion" (two slow sines, 0.5 and
 * 0.37 Hz, each line at its own phase), damped by a one-pole in the loop ("damp", as the combs were), scaled so it
 * decays as the old combs did at the same feedback (a project's size keeps its tail), mixed by an 8-point Hadamard and
 * written back with the diffused send. The left reads the even lines part-way along, the right the odd ones: two
 * decorrelated sides from the mono send (echo density: the combs' never passed half a noise's; this is noise's from
 * 50 ms on). The lines are floats
 * (two int16 slots each), so the tail falls into silence smoothly, with no 12-bit floor; a constant 1e-18 on the send
 * keeps them clear of denormals. A lag goes into the pre-delay's past (at least 6 ms, the second core's 256 frames
 * fit): the output is as without it, sample for sample */
#define PL_RD(o) r[(b + (uint32_t)(o)) & PL_MASK]
#define PL_AP(o, len, gg)                                                                     \
    do {                                                                                       \
        float d_ = PL_RD((o) + (len)), v_ = s - (gg) * d_;                                    \
        PL_RD(o) = v_;                                                                         \
        s = d_ + (gg) * v_;                                                                    \
    } while (0)
#define PL_TAP(k)                                                                             \
    do {                                                                                       \
        int32_t n_;                                                                            \
        float fr_, a0_;                                                                        \
        d[k] += dd[k];                                                                         \
        n_ = (int32_t)d[k];                                                                    \
        fr_ = d[k] - (float)n_;                                                                \
        a0_ = PL_RD(PL_OFF[k] + n_);                                                           \
        v[k] = a0_ + fr_ * (PL_RD(PL_OFF[k] + n_ + 1) - a0_);                                 \
        z[k] = v[k] + (z[k] - v[k]) * damp;                                                    \
        h[k] = g[k] * z[k];                                                                    \
    } while (0)
static void do_reverb(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any, int lag)
{
    plate_t *p = &f->pl;
    float *const r = (float *)(void *)f->rv;
    const float damp = f->pc[3], hpk = 0.017f, tlk = f->pc[2], thk = f->pc[5], im = 1.0f / (float)m;
    float d[8], dd[8], z[8], g[8], hp = p->hp, tl0 = p->tl[0], tl1 = p->tl[1], th0 = p->th[0], th1 = p->th[1], e = 0.0f;
    uint32_t b = p->base;
    int32_t pre, lo, tp[8];
    int i, k;
    if (p->clear > 0) {                       /* the last mode's work emptied first, silent meanwhile */
        int32_t c = p->clear > SPU_CLEAR ? SPU_CLEAR : p->clear;
        zero16(f->rv + (FX_RV_LEN - p->clear), c);
        p->clear -= c;
        return;
    }
    if (!any && p->quiet >= PL_SLEEP)         /* silent in, the tail under -120 dB: nothing to do */
        return;
    if (f->pc[4] != p->gfb) {                 /* the lines' gains: feedback^(length / the combs'), x 1/sqrt(8) */
        float lg = fm_log2f(f->pc[4]) * (1.0f / PL_LREF);
        for (k = 0; k < 8; k++)
            p->g[k] = 0.35355339f * fm_exp2f(lg * PL_LEN[k]);
        p->gfb = f->pc[4];
    }
    p->ph1 += 0.5f * (float)m / SR;
    p->ph2 += 0.37f * (float)m / SR;
    if (p->ph1 >= 1.0f) p->ph1 -= 1.0f;
    if (p->ph2 >= 1.0f) p->ph2 -= 1.0f;
    for (k = 0; k < 8; k++) {                 /* where each line's tap wants to be at the block's end: size, the wander */
        float t = f->pc[0] * PL_LEN[k] + f->pc[1] * fm_sin_turns((k & 1 ? p->ph2 : p->ph1) + 0.25f * (float)(k >> 1));
        d[k] = p->d[k];
        dd[k] = (t - d[k]) * im;
        z[k] = p->z[k];
        g[k] = p->g[k];
    }
    for (k = 0; k < 8; k++)                   /* the sides' taps part-way along the lines (whole samples, a block) */
        tp[k] = PL_OFF[k] + (int32_t)(d[k] * PL_TAPF[k]);
    pre = (int32_t)(PL_PRE_MIN + (PL_PRE_MAX - PL_PRE_MIN) * (f->pc[0] - 0.5f) * (1.0f / 1.4f));
    lo = lag < pre ? lag : pre;
    for (i = 0; i < m; i++) {
        float s, v[8], h[8], a, c, yl, yr;
        hp += hpk * (x[i] - hp);
        s = x[i] - hp + 1e-18f;
        if (lo) {                             /* the send into the pre-delay's past (a slot not read yet) */
            PL_RD(PO_PRE) = 0.0f;
            PL_RD(PO_PRE + lo) = s;
        } else {
            PL_RD(PO_PRE) = s;
        }
        s = PL_RD(PO_PRE + pre);
        PL_AP(PO_D0, PL_DIF[0], PL_DIFG[0]);
        PL_AP(PO_D1, PL_DIF[1], PL_DIFG[1]);
        PL_AP(PO_D2, PL_DIF[2], PL_DIFG[2]);
        PL_AP(PO_D3, PL_DIF[3], PL_DIFG[3]);
        PL_TAP(0); PL_TAP(1); PL_TAP(2); PL_TAP(3); PL_TAP(4); PL_TAP(5); PL_TAP(6); PL_TAP(7);
        /* the Hadamard, three butterflies */
        a = h[0] + h[1]; c = h[0] - h[1]; h[0] = a; h[1] = c;
        a = h[2] + h[3]; c = h[2] - h[3]; h[2] = a; h[3] = c;
        a = h[4] + h[5]; c = h[4] - h[5]; h[4] = a; h[5] = c;
        a = h[6] + h[7]; c = h[6] - h[7]; h[6] = a; h[7] = c;
        a = h[0] + h[2]; c = h[0] - h[2]; h[0] = a; h[2] = c;
        a = h[1] + h[3]; c = h[1] - h[3]; h[1] = a; h[3] = c;
        a = h[4] + h[6]; c = h[4] - h[6]; h[4] = a; h[6] = c;
        a = h[5] + h[7]; c = h[5] - h[7]; h[5] = a; h[7] = c;
        /* the third butterfly into the lines in another order, some signs turned (the Hadamard alone is its own
         * inverse: two trips round would bring a line's sound whole back to it; this way none of two to ten trips
         * brings back more than 0.71 of it to any line), and the diffused send into every line, the signs mixed */
        s *= 0.5f;
        PL_RD(PL_OFF[3]) = h[0] + h[4] + s;
        PL_RD(PL_OFF[1]) = -h[1] - h[5] - s;
        PL_RD(PL_OFF[2]) = h[2] + h[6] + s;
        PL_RD(PL_OFF[0]) = -h[3] - h[7] + s;
        PL_RD(PL_OFF[5]) = h[4] - h[0] - s;
        PL_RD(PL_OFF[7]) = h[5] - h[1] + s;
        PL_RD(PL_OFF[4]) = h[6] - h[2] - s;
        PL_RD(PL_OFF[6]) = h[7] - h[3] - s;
        b--;
        /* the sides read the lines part-way along (from their ends, each side would send back whatever one line
         * was just fed: a ring at that line's length) */
#define PL_MID(k) PL_RD(tp[k])
        yl = (PL_MID(0) - PL_MID(2) + PL_MID(4) - PL_MID(6)) * PL_WET;
        yr = (PL_MID(1) - PL_MID(3) + PL_MID(5) - PL_MID(7)) * PL_WET;
#undef PL_MID
        e += yl * yl + yr * yr;
        tl0 += tlk * (yl - tl0);              /* the tone: a low-pass (k = 1 passes), then a high-pass (k = 0 passes) */
        tl1 += tlk * (yr - tl1);
        th0 += thk * (tl0 - th0);
        th1 += thk * (tl1 - th1);
        ol[i] += tl0 - th0;
        orr[i] += tl1 - th1;
    }
    for (k = 0; k < 8; k++) {
        p->d[k] = d[k];
        p->z[k] = fm_flush(z[k]);
    }
    p->base = b;
    p->quiet = (!any && e < 1e-12f * (float)m) ? p->quiet + m : 0;
    p->hp = fm_flush(hp);
    p->tl[0] = fm_flush(tl0); p->tl[1] = fm_flush(tl1);
    p->th[0] = fm_flush(th0); p->th[1] = fm_flush(th1);
}
#undef PL_RD
#undef PL_AP
#undef PL_TAP

/* an SPU mode: the send decimated to 22.05 kHz, a tick of the console's reverb every second sample, its two
 * sides interpolated back; the plate's tone on the return. Its addresses follow the gliding size a block */
static void do_spu(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any)
{
    spu_t *s = &f->spu;
    int16_t *w = f->rv;
    const float tlk = f->pc[2], thk = f->pc[5], g = -SPU_GAIN[f->mode - 1];
    float tl = s->tl, th = s->th, tr = s->tr, thr = s->thr;
    int i;
    if (s->clear > 0) {                       /* a new mode: the work area emptied first, silent meanwhile */
        int32_t k = s->clear > SPU_CLEAR ? SPU_CLEAR : s->clear;
        zero16(w + (FX_RV_LEN - s->clear), k);
        s->clear -= k;
        return;
    }
    if (!any && s->quiet >= FX_RV_LEN * 2)    /* silent in, an empty area: nothing to read */
        return;
    spu_derive(s, f->mode, f->pc[0], f->pc[1], f->pc[3]);
    {   /* (the ring positions, the phase and the tick's outputs in locals: written back after the loop) */
    uint32_t ip = s->ip, up_w = s->up_w, phase = s->phase;
    float o00 = s->out[0][0], o01 = s->out[0][1], o10 = s->out[1][0], o11 = s->out[1][1];
    for (i = 0; i < m; i++) {
        float yl, yr;
        s->in[ip] = s->in[ip + 64u] = x[i];
        ip = (ip + 1u) & 63u;
        if (phase) {                          /* the second of a pair: a tick */
            float lo, ro;
            int wr;
            s->ip = (uint8_t)ip;              /* (spu_decimate reads it) */
            wr = spu_tick(s, w, -spu_decimate(s), &lo, &ro);   /* vLIN = 8000h: -1 */
            s->quiet = (wr == 0 && !any) ? s->quiet + 2 : 0;
            s->up[0][up_w] = s->up[0][up_w + 32u] = lo;
            s->up[1][up_w] = s->up[1][up_w + 32u] = ro;
            up_w = (up_w + 1u) & 31u;
            spu_interp(s->up[0], up_w, &o00, &o01);
            spu_interp(s->up[1], up_w, &o10, &o11);
            yl = o01 * g;                     /* the output volume: -1 again, and the level */
            yr = o11 * g;
        } else {
            yl = o00 * g;
            yr = o10 * g;
        }
        phase ^= 1u;
        tl += tlk * (yl - tl);
        tr += tlk * (yr - tr);
        th += thk * (tl - th);
        thr += thk * (tr - thr);
        ol[i] += tl - th;
        orr[i] += tr - thr;
    }
    s->ip = (uint8_t)ip;
    s->up_w = (uint8_t)up_w;
    s->phase = (uint8_t)phase;
    s->out[0][0] = o00; s->out[0][1] = o01; s->out[1][0] = o10; s->out[1][1] = o11;
    }
    s->tl = fm_flush(tl);
    s->th = fm_flush(th);
    s->tr = fm_flush(tr);
    s->thr = fm_flush(thr);
}

/* lush: the send decimated to 22.05 kHz through the SPU's 39-tap half-band FIR (a tick every second sample), Dattorro's
 * tank, its seven taps a side, then back to 44.1 kHz through the same FIR (flat to ~9 kHz, the images and the aliases
 * under it; it was a pair's mean in and a straight line out, which dulled the top and let it fold back), and the plate's
 * tone. The two FIRs' 38 samples come off the pre-delay. A lag is more pre-delay (as the SPU's). The lengths follow the
 * gliding size a block (whole samples); the two tank allpasses wander +-12 samples (x size) on a 0.7 Hz sine, a quarter
 * turn apart */
#define LU_FIR_TICKS 18                       /* the decimator's 19 samples and the interpolator's 9.5 ticks, in ticks */
#define LU_HB_N 7                             /* the FIR's first 7 pairs (the last three are under 3e-4: the stopband
                                               * still 56 dB down above 15 kHz) */
static inline float lu_decimate(const float *in, uint32_t ip)   /* (spu_decimate on lush's ring) */
{
    const float *z = in + ip + 64u - 20u;
    float y = 0.5f * z[0];
    int k;
    for (k = 0; k < LU_HB_N; k++)
        y += SPU_HB[k] * (z[-1 - 2 * k] + z[1 + 2 * k]);
    return y;
}
static inline void lu_interp(const float *u, uint32_t w, float *o0, float *o1)   /* (spu_interp, likewise) */
{
    const float *z = u + w + 31u - 9u;
    float y = 0.0f;
    int k;
    for (k = 0; k < LU_HB_N; k++)
        y += SPU_HB[k] * (z[k] + z[-1 - k]);
    *o0 = 2.0f * y;
    *o1 = z[0];
}
#define LU_RD(l, k) spu_rd(w, b, LU_OFF[l] + (k))
#define LU_WR(l, v) (wz |= spu_wr(w, b, LU_OFF[l], (v)))
#define LU_AP(l, g, x)                                                                        \
    do {                                                                                       \
        float d_ = LU_RD(l, u->len[l]), v_ = (x) - (g) * d_;                                  \
        LU_WR(l, v_);                                                                          \
        (x) = d_ + (g) * v_;                                                                   \
    } while (0)
static inline float lu_rdf(const int16_t *w, uint32_t b, int32_t off, float p)   /* a read between samples */
{
    int32_t i = (int32_t)p;
    float fr = p - (float)i, a0 = spu_rd(w, b, off + i), a1 = spu_rd(w, b, off + i + 1);
    return a0 + fr * (a1 - a0);
}
static void do_lush(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any)
{
    lush_t *u = &f->lu;
    int16_t *w = f->rv;
    const float sc = f->pc[0] * (22050.0f / 29761.0f), dec = f->pc[1], damp = f->pc[3], tlk = f->pc[2], thk = f->pc[5];
    const float exc = 12.0f * f->pc[0];
    float tl = u->tl, th = u->th, tr = u->tr, thr = u->thr;
    float o00 = u->out[0][0], o01 = u->out[0][1], o10 = u->out[1][0], o11 = u->out[1][1];
    uint32_t ip = u->ip, up_w = u->up_w;
    int i, l;
    if (u->clear > 0) {                       /* a new mode: the line emptied first, silent meanwhile */
        int32_t k = u->clear > SPU_CLEAR ? SPU_CLEAR : u->clear;
        zero16(w + (FX_RV_LEN - u->clear), k);
        u->clear -= k;
        return;
    }
    if (!any && u->quiet >= FX_RV_LEN * 2)    /* silent in, an empty line: nothing to read */
        return;
    if (sc != u->scale) {
        for (l = LU_IN1; l < NLU; l++)
            u->len[l] = (int32_t)(LU_LEN[l] * sc);
        u->scale = sc;
    }
    u->len[LU_PRE] = (int32_t)f->pc[4] - LU_FIR_TICKS;
    for (i = 0; i < m; i++) {
        float yl, yr;
        u->in[ip] = u->in[ip + 64u] = x[i];
        ip = (ip + 1u) & 63u;
        if (!u->phase) {                      /* the first of a pair: the interpolators' second output */
            yl = o01;
            yr = o11;
        } else {                              /* a tick */
            const uint32_t b = u->base;
            int32_t wz = 0;
            float s = lu_decimate(u->in, ip), a, c, mo, l0, r0;
            u->lfo += 0.7f / 22050.0f;
            if (u->lfo >= 1.0f)
                u->lfo -= 1.0f;
            LU_WR(LU_PRE, s);
            s = LU_RD(LU_PRE, u->len[LU_PRE]);
            LU_AP(LU_IN1, 0.75f, s);
            LU_AP(LU_IN2, 0.75f, s);
            LU_AP(LU_IN3, 0.625f, s);
            LU_AP(LU_IN4, 0.625f, s);
            /* the left half: the right's end in */
            a = s + dec * u->fb[1];
            mo = exc * fm_sin_turns(u->lfo);
            {
                float d = lu_rdf(w, b, LU_OFF[LU_MAPL], (float)u->len[LU_MAPL] + mo), v = a + 0.7f * d;
                LU_WR(LU_MAPL, v);
                a = d - 0.7f * v;
            }
            LU_WR(LU_DL1, a);
            a = LU_RD(LU_DL1, u->len[LU_DL1]);
            u->dmp[0] += (1.0f - damp) * (a - u->dmp[0]);
            a = u->dmp[0] * dec;
            LU_AP(LU_APL, 0.5f, a);
            LU_WR(LU_DL2, a);
            /* the right half: the left's end in */
            c = s + dec * u->fb[0];
            mo = exc * fm_sin_turns(u->lfo + 0.25f);
            {
                float d = lu_rdf(w, b, LU_OFF[LU_MAPR], (float)u->len[LU_MAPR] + mo), v = c + 0.7f * d;
                LU_WR(LU_MAPR, v);
                c = d - 0.7f * v;
            }
            LU_WR(LU_DR1, c);
            c = LU_RD(LU_DR1, u->len[LU_DR1]);
            u->dmp[1] += (1.0f - damp) * (c - u->dmp[1]);
            c = u->dmp[1] * dec;
            LU_AP(LU_APR, 0.5f, c);
            LU_WR(LU_DR2, c);
            u->fb[0] = LU_RD(LU_DL2, u->len[LU_DL2]);
            u->fb[1] = LU_RD(LU_DR2, u->len[LU_DR2]);
            /* Dattorro's taps (his sample counts, scaled) */
#define T(k) ((int32_t)((float)(k) * sc))
            l0 = LU_RD(LU_DR1, T(266)) + LU_RD(LU_DR1, T(2974)) - LU_RD(LU_APR, T(1913)) + LU_RD(LU_DR2, T(1996))
                 - LU_RD(LU_DL1, T(1990)) - LU_RD(LU_APL, T(187)) - LU_RD(LU_DL2, T(1066));
            r0 = LU_RD(LU_DL1, T(353)) + LU_RD(LU_DL1, T(3627)) - LU_RD(LU_APL, T(1228)) + LU_RD(LU_DL2, T(2673))
                 - LU_RD(LU_DR1, T(2111)) - LU_RD(LU_APR, T(335)) - LU_RD(LU_DR2, T(121));
#undef T
            u->up[0][up_w] = u->up[0][up_w + 32u] = l0 * 0.45f;   /* (0.6 was a little loud beside the dry: Charles) */
            u->up[1][up_w] = u->up[1][up_w + 32u] = r0 * 0.45f;
            up_w = (up_w + 1u) & 31u;
            lu_interp(u->up[0], up_w, &o00, &o01);    /* o00 between the ticks 9.5 back, o01 the tick 9 back: */
            lu_interp(u->up[1], up_w, &o10, &o11);    /* this sample, then the next */
            yl = o00;
            yr = o10;
            u->base = b - 1u;
            u->quiet = (wz == 0 && !any) ? u->quiet + 2 : 0;
        }
        u->phase ^= 1u;
        tl += tlk * (yl - tl);
        tr += tlk * (yr - tr);
        th += thk * (tl - th);
        thr += thk * (tr - thr);
        ol[i] += (tl - th) * RV_WET;
        orr[i] += (tr - thr) * RV_WET;
    }
    u->ip = (uint8_t)ip;
    u->up_w = (uint8_t)up_w;
    u->out[0][0] = o00; u->out[0][1] = o01; u->out[1][0] = o10; u->out[1][1] = o11;
    u->dmp[0] = fm_flush(u->dmp[0]);
    u->dmp[1] = fm_flush(u->dmp[1]);
    u->tl = fm_flush(tl);
    u->th = fm_flush(th);
    u->tr = fm_flush(tr);
    u->thr = fm_flush(thr);
}
#undef LU_RD
#undef LU_WR
#undef LU_AP

int fx_lags(const fxslot_t *f) { return f->type_t == FXT_CHORUS || f->type_t == FXT_DELAY || f->type_t == FXT_REVERB; }

void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n)
{
    fx_process_lag(f, in, out_l, out_r, n, 0);
}
void fx_process_lag(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n, int lag)
{
    uint32_t o;
    if (f->type_t != f->type || f->mode_t != f->mode) {   /* a new effect or mode: start it at rest */
        /* (the reverb in another mode: the line holds the last mode's work; a new effect's line fx_set zeroed) */
        f->rv_dirty = (uint8_t)(f->type == FXT_REVERB && f->type_t == FXT_REVERB);
        f->type = f->type_t;
        f->mode = f->mode_t;
        fx_states(f);
        f->rv_dirty = 0;
    }
    for (o = 0; o < n; o += SUB) {
        float x[SUB];
        int m = (int)(n - o < SUB ? n - o : SUB), i, any = 0;
        const int hp_on = f->hpv > 0, lp_on = f->lpv < 127;
        float hps = f->hps, lps = f->lps;
        const float hpc = f->hpc, lpc = f->lpc;
        for (i = 0; i < 6; i++)
            f->pc[i] += (f->pt[i] - f->pc[i]) * GLIDE;
        for (i = 0; i < m; i++) {
            float v = in[o + i];
            any |= v != 0.0f;
            if (hp_on) {
                hps += hpc * (v - hps);
                v -= hps;
            }
            if (lp_on) {
                lps += lpc * (v - lps);
                v = lps;
            }
            x[i] = v;
        }
        f->hps = fm_flush(hps);
        f->lps = fm_flush(lps);
        switch (f->type) {
        case FXT_CHORUS: do_chorus(f, x, out_l + o, out_r + o, m, lag); break;
        case FXT_DELAY: do_delay(f, x, out_l + o, out_r + o, m, any, lag); break;
        case FXT_LOFI: do_lofi(f, x, out_l + o, out_r + o, m); break;
        case FXT_PHASER: do_phaser(f, x, out_l + o, out_r + o, m); break;
        case FXT_REVERB:
            if (f->mode == RV_PLATE)
                do_reverb(f, x, out_l + o, out_r + o, m, any, lag);
            else if (f->mode == RV_LUSH)
                do_lush(f, x, out_l + o, out_r + o, m, any);
            else
                do_spu(f, x, out_l + o, out_r + o, m, any);
            break;
        default: break;
        }
    }
}

int fx_awake(const fxslot_t *f)
{
    switch (f->type) {
    case FXT_DELAY: return f->dquiet < 2 * FX_DLY_LEN;
    case FXT_REVERB:
        return f->mode == RV_PLATE ? f->pl.clear > 0 || f->pl.quiet < PL_SLEEP
             : f->mode == RV_LUSH ? f->lu.clear > 0 || f->lu.quiet < FX_RV_LEN * 2
             : f->spu.clear > 0 || f->spu.quiet < FX_RV_LEN * 2;
    case FXT_CHORUS: case FXT_LOFI: case FXT_PHASER: return 1;
    default: return 0;
    }
}

/* ================================================================ master === */
/* the master's knob names, short enough for the knob strip: percussion, melodic, compressor
 * and pump are perc, mel, comp, pump; then the out page's list */
const char *const MC_NAME[NMC] = {"off", "tape", "sampler"};
const char *const MS_NAME[NMS] = {"low", "mid", "high", "-", "color", "amount", "tone", "mix",
                                  "drums", "melodic", "comp", "pump", "pump by", "release", "accent", "level"};

typedef struct { float b0, b1, b2, a1, a2; } biq_t;
typedef struct { float x1, x2, y1, y2; } biqs_t;

struct master {
    uint8_t pot[NMS];
    float gp, gm, glev;                       /* the group and master levels (targets) */
    float gp_c, gm_c, lev_c;                  /* gliding */
    /* EQ */
    biq_t eq[3];
    biqs_t eqs[2][3];
    uint8_t eq_on, comp_on;                   /* eq_on: bit b, band b moved off flat (a flat band is not run) */
    /* compressor (X0X's master.c) */
    float thr_db, slope, knee, a_att, a_rel, makeup, gr, ms, a_det, want, g_lin, g_step, gr_view;
    uint8_t cr;
    /* PUMP (X0X's): its depth (dB), its envelope (dB) and the target it rises to; the rise and the release
     * as coefficients of MST_CR samples (it moves where the gain is computed) */
    float pump_db, pump, pump_tgt, a_up, a_down, pump_view;
    float pg, pg_step;                /* its gain on the groups, ramped as a linear gain over MST_CR samples */
    uint8_t pcr, duck_mel;            /* (the countdown); duck_mel: a kit's hit keyed it, the melodic group ducks alone */
#define MST_CR 8                              /* (the gain computed every 8 samples, ramped between: 5.5 kHz) */
    /* limiter (X0X's) */
#define MST_LA 64
    float env, gain, inv_env;
    float la[MST_LA], la_r[MST_LA];
    int32_t la_pos, hold;
    /* color (fx.h): the type running, its numbers (master_set), its states; the tape's wow line, a side */
#define MC_LINE 128
#define MC_DELAY 64.0f                        /* the wow's centre, samples (1.5 ms: the mix that late while tape is on) */
    uint8_t col;
    float c_g, c_mk, c_lp, c_dep, c_mix, c_step, c_pre, c_q, c_iq;
    float c_z[2][2], c_ph, c_fph, c_sh[2], c_sp;
    int16_t c_line[2][MC_LINE];
    int32_t c_pos;
};
_Static_assert(sizeof(struct master) <= MASTER_BYTES, "master_t does not fit MASTER_BYTES");

/* the bus's headroom: the groups enter it 8 dB down, so a project's chords and its kit peak a few dB under full scale
 * before the EQ, the color, the compressor and the limiter (at unity they peaked at +2 .. +9 dBFS: the limiter always
 * working, the organs audibly squashed; 2026-10-19). The drums and the synths keep their balance; the FM-1's volume
 * makes up the level */
#ifndef MST_HEADROOM                          /* (tests/host/fx_test.sh: 1, the master's mechanics at unity) */
#define MST_HEADROOM 0.4f
#endif
static float level_lin(int v)                 /* 0 .. 100 .. 127: silence, unity (exactly), 2x */
{
    float x = (float)(v < 0 ? 0 : v > 127 ? 127 : v);
    return x <= 100.0f ? x * x * 0.0001f : 1.0f + (x - 100.0f) * (1.0f / 27.0f);
}
static float coef_ms(float t) { return fm_expf(-1000.0f / (t * SR)); }
float master_pump_ms(int v) { return 10.0f * fm_exp2f((float)v * (7.229f / 127.0f)); }   /* X0X's release: 10 .. 1500 */
int master_max(int p) { return p == MS_PUMP_BY ? NPB - 1 : p == MS_PUMP_ACC ? 1 : p == MS_CTYPE ? NMC - 1 : 127; }
/* PUMP's duck rises over this (X0X's MASTER_PUMP_RISE_MS): 12 ms lets the kick's attack through and then ducks;
 * at 3 ms X0X heard a click on every kick */
#define MST_PUMP_RISE_MS 12.0f

/* RBJ: a low shelf, a peak, a high shelf, gain in dB */
static void biq_shelf(biq_t *q, float hz, float db, int high)
{
    float A = fm_exp2f(db * (3.3219281f / 40.0f)), w = FM_TWO_PI * hz / SR, cs = fm_cosf(w), sn = fm_sinf(w);
    float al = sn * 0.5f * 1.4142136f, sa = 2.0f * fm_sqrtf(A) * al, ia, c = high ? -cs : cs;
    float a0 = (A + 1.0f) + (A - 1.0f) * c + sa;
    ia = 1.0f / a0;
    q->b0 = A * ((A + 1.0f) - (A - 1.0f) * c + sa) * ia;
    q->b1 = (high ? -2.0f : 2.0f) * A * ((A - 1.0f) - (A + 1.0f) * c) * ia;
    q->b2 = A * ((A + 1.0f) - (A - 1.0f) * c - sa) * ia;
    q->a1 = (high ? 2.0f : -2.0f) * ((A - 1.0f) + (A + 1.0f) * c) * ia;
    q->a2 = ((A + 1.0f) + (A - 1.0f) * c - sa) * ia;
}
static void biq_peak(biq_t *q, float hz, float db, float Q)
{
    float A = fm_exp2f(db * (3.3219281f / 40.0f)), w = FM_TWO_PI * hz / SR, cs = fm_cosf(w), sn = fm_sinf(w);
    float al = sn / (2.0f * Q), ia = 1.0f / (1.0f + al / A);
    q->b0 = (1.0f + al * A) * ia;
    q->b1 = -2.0f * cs * ia;
    q->b2 = (1.0f - al * A) * ia;
    q->a1 = -2.0f * cs * ia;
    q->a2 = (1.0f - al / A) * ia;
}

static void eq_coefs(master_t *m)
{
    float lo = ((float)m->pot[MS_EQ_LO] - 64.0f) * (12.0f / 64.0f);
    float mid = ((float)m->pot[MS_EQ_MID] - 64.0f) * (12.0f / 64.0f);
    float hi = ((float)m->pot[MS_EQ_HI] - 64.0f) * (12.0f / 64.0f);
    biq_shelf(&m->eq[0], 120.0f, lo, 0);
    biq_peak(&m->eq[1], 1000.0f, mid, 0.8f);
    biq_shelf(&m->eq[2], 6000.0f, hi, 1);
    {   /* the bands moved off flat (a flat one is 0 dB: it would only round); one turned on starts from rest */
        uint8_t on = (uint8_t)((m->pot[MS_EQ_LO] != 64) | (m->pot[MS_EQ_MID] != 64) << 1 | (m->pot[MS_EQ_HI] != 64) << 2);
        int b;
        for (b = 0; b < 3; b++)
            if ((on & ~m->eq_on) >> b & 1u) {
                static const biqs_t rest;
                m->eqs[0][b] = rest;
                m->eqs[1][b] = rest;
            }
        m->eq_on = on;
    }
}

/* X0X's one-knob COMP: the threshold falls (-4 .. -32 dB), the ratio climbs (1.5:1 .. 6:1), the
 * makeup follows (0.85 of the reduction at the threshold); attack 2.6 ms, release 106 ms */
static void comp_coefs(master_t *m)
{
    static const float R[6] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
    int v = m->pot[MS_COMP], ri = v ? 1 + (int)((float)v * (4.0f / 127.0f) + 0.5f) : 0;
    float x = (float)v * (1.0f / 127.0f), thr = -4.0f - 28.0f * x, mk;
    uint8_t was = m->comp_on;
    mk = ri ? fm_minf(-thr * (1.0f - 1.0f / R[ri]) * 0.85f, 24.0f) : 0.0f;
    m->thr_db = thr;
    m->slope = 1.0f - 1.0f / R[ri];
    m->knee = 6.0f;
    m->a_att = coef_ms(2.6f);
    m->a_rel = coef_ms(106.0f);
    m->a_det = coef_ms(8.0f);
    m->makeup = ri ? fm_db2lin(mk) : 1.0f;              /* (the pump alone: unity under it) */
    m->pump_db = 24.0f * (float)m->pot[MS_PUMP] * (1.0f / 127.0f);
    m->a_up = coef_ms(MST_PUMP_RISE_MS / (float)MST_CR);
    m->a_down = coef_ms(master_pump_ms(m->pot[MS_PUMP_REL]) / (float)MST_CR);
    if (!m->pot[MS_PUMP])
        m->pump = m->pump_tgt = 0.0f;
    m->comp_on = ri != 0;                        /* (the pump is on the groups: process's first loop) */
    if (m->comp_on && !was) {
        m->g_lin = m->makeup;
        m->g_step = 0.0f;
        m->cr = 0;
    }
}

/* the color's numbers from its knobs (the main loop); a new type starts at rest */
static void color_coefs(master_t *m)
{
    float a = (float)m->pot[MS_CAMT] * (1.0f / 127.0f), t = (float)m->pot[MS_CTONE] * (1.0f / 127.0f);
    int type = m->pot[MS_CTYPE] < NMC ? m->pot[MS_CTYPE] : MC_OFF;
    m->c_mix = (float)m->pot[MS_CMIX] * (1.0f / 127.0f);
    if (type == MC_TAPE) {
        float g = 0.5f * fm_exp2f(a * 3.585f), x = fm_minf(g * 0.7f, 1.5f);
        m->c_g = g;
        m->c_mk = 0.7f / (x - x * x * x * (4.0f / 27.0f)) * (1.0f - 0.15f * a);   /* (sat_c's at 0.7: made up) */
        m->c_lp = onepole_k(1500.0f * fm_exp2f(t * 3.585f));
        m->c_dep = (4.0f + 28.0f * a) * m->c_mix;  /* samples of wow: ~1 .. 6 cents at 0.55 Hz */
    } else if (type == MC_SAMPLER) {
        float r = 26040.0f * fm_exp2f(-a * 1.243f);
        m->c_step = r / SR;
        m->c_pre = onepole_k(0.45f * r);
        m->c_q = fm_exp2f(13.0f - 3.0f * a);
        m->c_iq = 1.0f / m->c_q;
        m->c_lp = onepole_k(2000.0f * fm_exp2f(t * 3.322f));
    }
    if (type != m->col) {                         /* (the interrupt may be mid-block: the states zeroed first) */
        m->c_z[0][0] = m->c_z[0][1] = m->c_z[1][0] = m->c_z[1][1] = 0.0f;
        m->c_sh[0] = m->c_sh[1] = m->c_sp = 0.0f;
        m->col = (uint8_t)type;
    }
}
/* the color over a block of the bus, in place (master_process; from RAM: every call inlined) */
static inline float mc_sat(float x)
{
    x = fm_minf(fm_maxf(x, -1.5f), 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static inline void color_block(master_t *m, float *l, float *r, uint32_t n)
{
    const float mix = m->c_mix, lp = m->c_lp;
    float z00 = m->c_z[0][0], z01 = m->c_z[0][1], z10 = m->c_z[1][0], z11 = m->c_z[1][1];
    uint32_t i;
    if (m->col == MC_TAPE) {
        const float g = m->c_g, mk = m->c_mk, dep = m->c_dep, dw = 0.55f / SR, df = 6.1f / SR;
        float ph = m->c_ph, fph = m->c_fph;
        int32_t pos = m->c_pos;
        for (i = 0; i < n; i++) {
            float d, fr, yl, yr, a0, a1;
            int32_t k, k0, k1;
            m->c_line[0][pos] = (int16_t)(fm_clampf(l[i], -1.99f, 1.99f) * 16384.0f);
            m->c_line[1][pos] = (int16_t)(fm_clampf(r[i], -1.99f, 1.99f) * 16384.0f);
            ph += dw;
            fph += df;
            if (ph >= 1.0f) ph -= 1.0f;
            if (fph >= 1.0f) fph -= 1.0f;
            d = MC_DELAY + dep * (fm_sin_turns(ph) + 0.125f * fm_sin_turns(fph));
            k = (int32_t)d;
            fr = d - (float)k;
            k0 = (pos - k) & (MC_LINE - 1);
            k1 = (k0 - 1) & (MC_LINE - 1);
            a0 = (float)m->c_line[0][k0], a1 = (float)m->c_line[0][k1];
            yl = (a0 + fr * (a1 - a0)) * (1.0f / 16384.0f);
            a0 = (float)m->c_line[1][k0], a1 = (float)m->c_line[1][k1];
            yr = (a0 + fr * (a1 - a0)) * (1.0f / 16384.0f);
            pos = (pos + 1) & (MC_LINE - 1);
            z00 += lp * (mc_sat(yl * g) * mk - z00);
            z01 += lp * (z00 - z01);
            z10 += lp * (mc_sat(yr * g) * mk - z10);
            z11 += lp * (z10 - z11);
            l[i] = yl + mix * (z01 - yl);
            r[i] = yr + mix * (z11 - yr);
        }
        m->c_ph = ph;
        m->c_fph = fph;
        m->c_pos = pos;
    } else if (m->col == MC_SAMPLER) {
        const float step = m->c_step, pre = m->c_pre, q = m->c_q, iq = m->c_iq;
        float sp = m->c_sp, h0 = m->c_sh[0], h1 = m->c_sh[1];
        for (i = 0; i < n; i++) {
            z00 += pre * (l[i] - z00);
            z10 += pre * (r[i] - z10);
            sp += step;
            if (sp >= 1.0f) {
                sp -= 1.0f;
                h0 = fm_floorf(z00 * q + 0.5f) * iq;
                h1 = fm_floorf(z10 * q + 0.5f) * iq;
            }
            z01 += lp * (h0 - z01);
            z11 += lp * (h1 - z11);
            l[i] += mix * (z01 - l[i]);
            r[i] += mix * (z11 - r[i]);
        }
        m->c_sp = sp;
        m->c_sh[0] = h0;
        m->c_sh[1] = h1;
    }
    m->c_z[0][0] = fm_flush(z00);
    m->c_z[0][1] = fm_flush(z01);
    m->c_z[1][0] = fm_flush(z10);
    m->c_z[1][1] = fm_flush(z11);
}

void master_init(master_t *m)
{
    static const uint8_t DEF[NMS] = {64, 64, 64, 0, MC_OFF, 40, 64, 127, 100, 100, 0, 0, PB_KICK, MS_PUMP_REL_DEF, 1, 100};
    uint32_t i;
    uint8_t *z = (uint8_t *)m;
    for (i = 0; i < sizeof *m; i++)
        z[i] = 0;
    for (i = 0; i < NMS; i++)
        m->pot[i] = DEF[i];
    m->gain = 1.0f;
    m->pg = 1.0f;                                /* (the pump's gain: none ducked) */
    m->gp = m->gp_c = m->gm = m->gm_c = MST_HEADROOM;
    m->glev = m->lev_c = 1.0f;
    eq_coefs(m);
    comp_coefs(m);
    color_coefs(m);
}

void master_clear(master_t *m)
{
    uint8_t pot[NMS];
    uint32_t i;
    for (i = 0; i < NMS; i++)
        pot[i] = m->pot[i];
    master_init(m);
    for (i = 0; i < NMS; i++)
        master_set(m, (int)i, pot[i]);
}

void master_set(master_t *m, int p, int v)
{
    if (p < 0 || p >= NMS)
        return;
    m->pot[p] = (uint8_t)(v < 0 ? 0 : v > master_max(p) ? master_max(p) : v);
    if (p <= MS_EQ_HI)
        eq_coefs(m);
    else if (p == MS_RSV3)
        return;                                           /* (the blend's, gone) */
    else if (p <= MS_CMIX)
        color_coefs(m);
    else if (p == MS_PERC)
        m->gp = level_lin(v) * MST_HEADROOM;
    else if (p == MS_MEL)
        m->gm = level_lin(v) * MST_HEADROOM;
    else if (p == MS_LEVEL)
        m->glev = level_lin(v);
    else
        comp_coefs(m);                                    /* comp, the pump's depth, source, release, accent */
}

float master_gr_db(const master_t *m) { return m->gr_view; }
float master_pump_db(const master_t *m) { return m->pump_view; }

void master_key(master_t *m, int src, int vel, int kit)
{
    float t;
    if (!m->pot[MS_PUMP] || src != m->pot[MS_PUMP_BY])
        return;
    t = m->pot[MS_PUMP_ACC] ? m->pump_db * (0.5f + 0.5f * (float)(vel < 0 ? 0 : vel > 127 ? 127 : vel) * (1.0f / 127.0f))
                            : m->pump_db;                   /* an accented hit pumps deeper (X0X: 0.5 .. 1) */
    if (t > m->pump_tgt)
        m->pump_tgt = t;
    m->duck_mel = (uint8_t)(kit != 0);           /* a kick (a kit's hit) ducks the rest, never itself */
}

static inline float gr_of(const master_t *m, float lv)       /* the static curve, soft knee (dB) */
{
    float over = lv - m->thr_db, h = 0.5f * m->knee;
    if (over <= -h)
        return 0.0f;
    if (over >= h)
        return m->slope * over;
    over += h;
    return m->slope * over * over / (2.0f * m->knee);
}
static inline float soft_ceiling(float s)
{
    if (s > 0.89f || s < -0.89f)
        s = s > 0.0f ? 0.89f + 0.11f * fm_tanhf((s - 0.89f) * 9.0f) : -0.89f - 0.11f * fm_tanhf((-s - 0.89f) * 9.0f);
    return s;
}
static inline float glide1(float cur, float tgt)             /* a level knob: ~10 ms, then exact */
{
    if (cur == tgt)
        return cur;
    cur += (tgt - cur) * 0.0022651f;
    return fm_fabsf(tgt - cur) < 1e-5f ? tgt : cur;
}

#ifdef PLAT_HOST
int fx_host_stage = 9;                        /* the host's taps (host.c LOOM_MST): 0 the groups only, 1 + EQ, 2 + color,
                                               * 3 + comp and level, else all (the limiter) */
#define STAGE(k) (fx_host_stage >= (k))
#else
#define STAGE(k) 1
#endif
/* one biquad over a block, its state in registers (the band-major order of master_process: each band's samples
 * are the same as the sample-major loop's, operation for operation) */
static inline void biq_block(const biq_t *q, biqs_t *st, float *x, uint32_t n)
{
    const float b0 = q->b0, b1 = q->b1, b2 = q->b2, a1 = q->a1, a2 = q->a2;
    float x1 = st->x1, x2 = st->x2, y1 = st->y1, y2 = st->y2;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float in = x[i], y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = fm_flush(y);
        x[i] = y;
    }
    st->x1 = x1;
    st->x2 = x2;
    st->y1 = y1;
    st->y2 = y2;
}
void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l,
                    float *out_r, uint32_t n)
{
    uint32_t i;
    float gp = m->gp_c, gm = m->gm_c, lev = m->lev_c, grmax = 0.0f, pmax = 0.0f;
    const int eq_on = m->eq_on, comp_on = m->comp_on;
    /* the groups into the bus, then the EQ a band at a time, in place in mel. (Every state is a local in its loop: a
     * load from RAM costs this core more than a multiply.) */
    {   /* (with the PUMP, X0X's, on the groups as they meet: a kit's hit ducks the melodic group alone, so the kick
         * that keys it is never ducked; a melodic track's note or the click, both) */
        const float tgp = m->gp, tgm = m->gm, a_up = m->a_up, a_down = m->a_down;
        float pump = m->pump, pump_tgt = m->pump_tgt, pg = m->pg, pg_step = m->pg_step;
        int pcr = m->pcr;
        const int mel_only = m->duck_mel;
        for (i = 0; i < n; i++) {
            float pp, ml = mel_l[i], mr = mel_r[i];   /* (both read first: a caller may pass one buffer for both) */
            gp = glide1(gp, tgp);
            gm = glide1(gm, tgm);
            if (pcr == 0) {
                if (pump_tgt > pump) {            /* rise to the hit's depth ... */
                    pump = a_up * pump + (1.0f - a_up) * pump_tgt;
                    if (pump_tgt - pump < 0.05f)
                        pump_tgt = 0.0f;          /* ... then let go: the release swells back */
                } else {
                    pump = fm_flush(a_down * pump);
                }
                if (pump > pmax)
                    pmax = pump;
                pg_step = (fm_db2lin(-pump) - pg) * (1.0f / MST_CR);
                pcr = MST_CR;
            }
            pcr--;
            pg += pg_step;
            pp = mel_only ? gp : gp * pg;
            mel_l[i] = perc_l[i] * pp + ml * gm * pg;
            mel_r[i] = perc_r[i] * pp + mr * gm * pg;
        }
        m->pump = pump;
        m->pump_tgt = pump_tgt;
        m->pg = pg;
        m->pg_step = pg_step;
        m->pcr = (uint8_t)pcr;
    }
    if (eq_on && STAGE(1)) {
        int b;
        for (b = 0; b < 3; b++)
            if (eq_on >> b & 1) {
                biq_block(&m->eq[b], &m->eqs[0][b], mel_l, n);
                biq_block(&m->eq[b], &m->eqs[1][b], mel_r, n);
            }
    }
    if (m->col != MC_OFF && STAGE(2))
        color_block(m, mel_l, mel_r, n);
    {
        const float glev = m->glev, a_det = m->a_det, a_att = m->a_att, a_rel = m->a_rel, slope = m->slope;
        const float makeup = m->makeup;
        float ms = m->ms, want = m->want, gr = m->gr;
        float g_lin = m->g_lin, g_step = m->g_step, env = m->env, inv_env = m->inv_env, gain = m->gain;
        float *la = m->la, *la_r = m->la_r;
        int32_t la_pos = m->la_pos, hold = m->hold;
        int cr = m->cr;
        for (i = 0; i < n; i++) {
            float s = mel_l[i], sr = mel_r[i];
            lev = glide1(lev, glev);
            if (comp_on && STAGE(3)) {
                /* the level: mean square over ~8 ms of both sides, x2 so a sine reads its peak; the static
                 * curve every MST_CR samples; the reduction smoothed (attack / release) and ramped as a
                 * linear gain (X0X's master.c: read every sample, the gain chased each cycle of a bass) */
                ms = fm_flush(a_det * ms + 0.5f * ((1.0f - a_det) * s * s + (1.0f - a_det) * sr * sr));
                if (cr == 0) {
                    float lv = 2.0f * ms;
                    want = (lv > 1e-12f && slope > 0.0f) ? gr_of(m, 3.0103f * fm_log2f(lv)) : 0.0f;
                }
                gr = want > gr ? a_att * gr + (1.0f - a_att) * want : a_rel * gr + (1.0f - a_rel) * want;
                if (gr > grmax)
                    grmax = gr;
                if (cr == 0) {
                    g_step = (makeup * fm_db2lin(-gr) - g_lin) * (1.0f / MST_CR);
                    cr = MST_CR;
                }
                cr--;
                g_lin += g_step;
                s *= g_lin;
                sr *= g_lin;
            }
            s *= lev;
            sr *= lev;
            {   /* X0X's limiter: a peak is seen MST_LA samples before it comes out of the line; the gain
                 * eases down over those samples and holds for them */
                float a = fm_maxf(fm_fabsf(s), fm_fabsf(sr)), d, dr, w;
                if (a >= env) {
                    float r = 1.0f / fm_maxf(a, 0.8f);                   /* safe on its own: a clamp inside
                                                                          * the branch is folded away */
                    env = a;
                    hold = MST_LA;
                    inv_env = a > 0.8f ? r : 0.0f;
                } else if (hold) {
                    hold--;
                } else {
                    env *= 0.99985f;
                    inv_env *= 1.0001500225f;
                }
                w = env > 0.8f ? 0.8f * inv_env : 1.0f;
                gain += (w - gain) * (w < gain ? (5.0f / MST_LA) : 0.002f);
                d = la[la_pos];
                dr = la_r[la_pos];
                la[la_pos] = s;
                la_r[la_pos] = sr;
                la_pos = (la_pos + 1) & (MST_LA - 1);
                out_l[i] = soft_ceiling(d * gain);
                out_r[i] = soft_ceiling(dr * gain);
#ifdef PLAT_HOST
                if (!STAGE(4)) {                  /* (a tap before the limiter: the bus as it comes, unlimited) */
                    out_l[i] = d;
                    out_r[i] = dr;
                }
#endif
            }
        }
        m->ms = ms;
        m->want = want;
        m->gr = gr;
        m->g_lin = g_lin;
        m->g_step = g_step;
        m->env = env;
        m->inv_env = inv_env;
        m->gain = gain;
        m->la_pos = la_pos;
        m->hold = hold;
        m->cr = (uint8_t)cr;
    }
    m->gp_c = gp;
    m->gm_c = gm;
    m->lev_c = lev;
    m->gr_view = grmax > m->gr_view ? grmax : m->gr_view * 0.93f;
    m->pump_view = pmax > m->pump_view ? pmax : m->pump_view * 0.93f;
}
