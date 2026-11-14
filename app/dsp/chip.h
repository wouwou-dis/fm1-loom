/* SPDX-License-Identifier: MIT */
/* LOOM-8's chip voice, sequin: an imagined home computer's sound chip. Included by synth.c only (static code on its
 * voice_t, no unit of its own).
 *
 * Ported from Felucca-Engines' LOFI engine (github.com/hugelton/Felucca-Engines, src/eng_lofi.c with what it takes from
 * src/dsp.c), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments, MIT License (the licence text:
 * LICENSING.md); the port Copyright (c) 2026 the LOOM-8 authors, MIT. LOFI is fixed point on Felucca's voice; here it is
 * float on LOOM-8's: the waves, the sample hold, the bit depth, the wave memory (LOFI's sixteen 32 x 4-bit tables, as
 * they are), the shift-register noise, the sweep, the arpeggio, the vibrato and the tone are LOFI's, at its timing
 * (44.1 kHz: LOFI's 32-sample blocks are two to LOOM-8's 64-sample chunk); LOOM-8's own envelope, filter, LFO and
 * character stage follow the chip as on any engine.
 *
 *   E1 wave    pulse, triangle, saw, noise (a 15-bit shift register clocked once a cycle of the note), table (the
 *              wave memory: 32 steps of 4 bits, read step by step, no interpolation)
 *   E2 duty    the pulse's width, 12.5 / 25 / 37.5 / 50 % (step: 12.5 / 25 / 50 / 75 %); on table the table (duty / 8:
 *              CH_TABLE_N)
 *   E3 crush   the sample hold, 1 .. 16 samples (the output held at a lower rate: the chip's grain); past the
 *              middle the held value also falls to 16 steps
 *   E4 sweep   the pitch falls from the note's start (a glide down, a tom, a zap, a laser); 0 none, 1 the slowest,
 *              8 semitones a second, 127 the fastest, LOFI's 15/8 semitone a chunk (~1290 a second), the rate even in
 *              ratio between (CH_SWEEP_OCT: x1.041 a step, about doubling every 17). The note ends where the fall leaves hearing (20 Hz: CH_SWEEP_HZ), the chip silent till
 *              the next note, as a console's sweep unit mutes its channel past its range
 *   X1 chip    4 bit, 4 bit lo (the hold a sample longer: half the rate at no crush), 8 bit, 1 bit (the noise
 *              clocked at the held rate: hiss), step (the pulse's widths as a console's, the triangle a 32-step
 *              staircase, and the amp envelope itself stepped: LOOM-8's ADSR taken to 16 levels at a 230 Hz frame
 *              rate, LOFI's STEP envelope on LOOM-8's envelope page)
 *   X2 arp     off, octave (note, octave, note), major, minor: a chord broken at ~49 Hz, a step every 896 samples
 *   X3 vib     the chip's own vibrato, 5.4 Hz, to +-27 cents
 *   X4 tone    LOFI's one-pole after the hold, ~650 Hz .. open
 *
 * Changes from LOFI, and why:
 *   - LOFI's STEP chip mode drove its own 4-bit volume envelope from the CRSH knob (constant, one-shot or looping
 *     decays); here the envelope is LOOM-8's ADSR, stepped (its Envelope page sets it, the knob stays crush): one
 *     envelope a track, as every engine has it;
 *   - the tone's cutoff ignores LOFI's filter modulation: LOOM-8's own filter follows (its keytrack, envelope, LFO);
 *   - the vibrato is LOOM-8's sine (fm_sin_turns) per chunk, LOFI's table per block; the pitch's floor is 8 Hz
 *     (LOFI's lowest table step);
 *   - LOFI's SWP was 16 steps, linear (SWP / 8 eighths of a semitone a block): 1..7 did nothing, the slowest already
 *     fell 86 semitones a second and the top three quarters all zapped alike. Here every step sweeps, exponentially
 *     from 8 to LOFI's fastest (127: the same);
 *   - LOFI's sweep fell to that floor and stayed: a held note became a full-scale 8 Hz click train (a motorboat) for
 *     as long as it was held. Here the fall ends the note at 20 Hz (a quick fade to silence, ch_end) */

#define CH_TOP 127                                     /* (the knobs' top) */
enum { CW_PULSE, CW_TRI, CW_SAW, CW_NOISE, CW_WAVE, NCW };
enum { CC_4BIT, CC_4LO, CC_8BIT, CC_1BIT, CC_STEP, NCC };
static const char *const CH_WAVE_N[NCW] = {"pulse", "tri", "saw", "noise", "table"};
static const char *const CH_CHIP_N[NCC] = {"4 bit", "4 bit lo", "8 bit", "1 bit", "step"};
static const char *const CH_ARP_N[4] = {"off", "octave", "major", "minor"};
/* the wave memory: LOFI's sixteen tables x 32 samples x 4 bits, two samples a byte (high nibble first) */
static const char *const CH_TABLE_N[16] = {"sq 50", "sq 25", "sq 12", "sine", "tri", "saw", "saw 2", "bass 1", "bass 2",
                                           "organ", "hollow", "vox a", "vox o", "vox e", "buzz", "stair"};
static const uint8_t CH_WRAM[16][16] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* sq 50 */
    {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* sq 25 */
    {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* sq 12 */
    {0x89, 0xAC, 0xDE, 0xEF, 0xFF, 0xEE, 0xDC, 0xA9, 0x86, 0x53, 0x21, 0x10, 0x00, 0x11, 0x23, 0x56},   /* sine */
    {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10},   /* tri */
    {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},   /* saw */
    {0x01, 0x12, 0x33, 0x45, 0x56, 0x77, 0x89, 0x9A, 0x56, 0x67, 0x88, 0x9A, 0xAB, 0xCC, 0xDE, 0xEF},   /* saw 2 */
    {0xEE, 0xEF, 0xFF, 0xFF, 0xFF, 0xEE, 0xED, 0xDC, 0x11, 0x10, 0x00, 0x00, 0x00, 0x11, 0x12, 0x23},   /* bass 1 */
    {0xCE, 0xFF, 0xED, 0xBA, 0x9A, 0xBD, 0xEF, 0xFE, 0xC9, 0x64, 0x21, 0x00, 0x00, 0x01, 0x24, 0x69},   /* bass 2 */
    {0x8B, 0xEF, 0xED, 0xBA, 0x9A, 0xAA, 0x98, 0x77, 0x88, 0x87, 0x65, 0x55, 0x65, 0x42, 0x10, 0x14},   /* organ */
    {0x8D, 0xFE, 0xDD, 0xDD, 0xCD, 0xDD, 0xDE, 0xFD, 0x82, 0x01, 0x22, 0x22, 0x32, 0x22, 0x21, 0x02},   /* hollow */
    {0x8F, 0xFC, 0xA7, 0x67, 0x89, 0x88, 0x88, 0x87, 0x88, 0x77, 0x77, 0x76, 0x78, 0x98, 0x53, 0x00},   /* vox a */
    {0x8F, 0xEE, 0xFD, 0xDB, 0x98, 0x66, 0x66, 0x77, 0x88, 0x89, 0x99, 0x97, 0x64, 0x22, 0x01, 0x10},   /* vox o */
    {0x7F, 0x96, 0xCF, 0xBB, 0xCC, 0xBB, 0xAA, 0x98, 0x77, 0x65, 0x54, 0x43, 0x34, 0x40, 0x39, 0x60},   /* vox e */
    {0xF3, 0x00, 0x12, 0x22, 0x11, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22},   /* buzz */
    {0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55, 0x00, 0x00, 0x00, 0x00},   /* stair */
};
/* the bits: x (about -1..1) to lv levels a side, rounded (a floor is DC: the offset keeps the cast a floor) */
static inline float ch_q(float x, float lv, float ilv) { return ((float)(int)(x * lv + lv + 0.5f) - lv) * ilv; }
static inline int ch_nib(const uint8_t *w, int k) { return k & 1 ? w[k >> 1] & 15 : w[k >> 1] >> 4; }   /* step k, 0..15 */
#define CH_GAIN 0.7f                                   /* a full-scale pulse near LOOM-8's other engines' level */
#define CH_ARP_CHUNKS 14                               /* LOFI's arpeggio step: 28 blocks of 32 samples */
#define CH_FRAME 192                                   /* the step envelope's frame: 12 sub-blocks, ~230 Hz (LOFI's 240) */
#define CH_SWEEP_TOP 1.875f                            /* the sweep at 127: semitones a chunk (LOFI's fastest) */
#define CH_SWEEP_OCT 0.0582173f                        /* its octaves a knob step: log2(1.875 / (8 * 64 / 44100)) / 126 */
#define CH_SWEEP_HZ 20.0f                              /* the sweep's end: the pitch it falls past, out of hearing */
#define CH_SWEEP_END 1000.0f                           /* o[3] once the sweep has ended (past any fall: 120) */
static const int8_t CH_ARPS[4][3] = {{0, 0, 0}, {0, 12, 0}, {0, 4, 7}, {0, 3, 7}};

/* o[]: 0 the phase (turns), 1 the held sample, 2 the hold's countdown, 3 the sweep (semitones down; CH_SWEEP_END
 * when it has ended the note), 4 the tone's
 * one-pole, 5 the arpeggio's chunk count, 6 the vibrato's phase, 7 the shift register, 8 the step envelope's level,
 * 9 its frame's samples. ex[]: 0 the note's increment (turns), 1 the duty, 2 the hold (samples), 3 the levels a side
 * (0: no quantising), 4 the coarse fall (crush past the middle), 5 the wave, 6 the table, 7 the tone's coefficient,
 * 8 the table's mean, 9 the sweep a chunk (semitones), 10 the arpeggio, 11 the vibrato's depth, 12 the increment now
 * (ch_chunk: sweep, arpeggio, vibrato), 13 the noise's clock every hold (1 bit), 14 1 / ex[3], 15 / 16 the pulse's (the
 * noise's) high and low levels; 18..21 X1..X4 (voice_chunk) */
static void ch_note(voice_t *v)                       /* a note from rest: LOFI's note_on */
{
    v->o[1] = v->o[3] = v->o[4] = v->o[5] = 0.0f;
    v->o[2] = 0.0f;
    v->o[7] = 32767.0f;
    v->o[8] = 0.0f;
    v->o[9] = 0.0f;
}
static void ch_retrigger(voice_t *v)                  /* a mono retrigger: the sweep and the arpeggio from the start */
{
    v->o[3] = 0.0f;
    v->o[5] = 0.0f;
}
static void ch_prep(voice_t *v)
{
    static const uint8_t STEP_DUTY[4] = {1, 2, 4, 6};   /* eighths: 12.5 / 25 / 50 / 75 % */
    cfg_t *c = &v->c;
    int wave = (int)(c->ek[0] + 0.5f), duty = (int)c->ek[1], crush = (int)c->ek[2];
    int chip = (int)c->ex[18], bits, i, sum = 0;
    wave = wave < 0 ? 0 : wave >= NCW ? NCW - 1 : wave;
    chip = chip < 0 ? 0 : chip >= NCC ? NCC - 1 : chip;
    bits = chip == CC_8BIT ? 8 : chip == CC_1BIT ? 1 : 4;
    c->ex[0] = c->freq * INV_SR;
    c->ex[1] = (float)(chip == CC_STEP ? STEP_DUTY[(duty >> 5) & 3] : 1 + (duty >> 5)) * 0.125f;
    c->ex[2] = (float)(1 + crush / 8 + (chip == CC_4LO));
    c->ex[3] = (float)(1 << (bits - 1));
    c->ex[4] = crush > 64 ? 1.0f : 0.0f;
    c->ex[5] = (float)wave;
    c->ex[6] = (float)((duty >> 3) & 15);
    c->ex[7] = fm_minf((3000.0f + c->ex[21] * 256.0f) * (1.0f / 32768.0f), 1.0f);
    for (i = 0; i < 32; i++)                          /* the table's mean: no DC from a lopsided shape */
        sum += bits == 1 ? (ch_nib(CH_WRAM[(int)c->ex[6]], i) >= 8 ? 1 : -1) : 2 * ch_nib(CH_WRAM[(int)c->ex[6]], i) - 15;
    c->ex[8] = bits == 1 ? (float)sum * (1.0f / 32.0f) : (float)sum * (1.0f / (32.0f * 15.0f));
    c->ex[9] = (int)c->ek[3] > 0 ? CH_SWEEP_TOP * fm_exp2f((float)((int)c->ek[3] - CH_TOP) * CH_SWEEP_OCT) : 0.0f;
    c->ex[10] = (float)(((int)c->ex[19]) & 3);
    c->ex[11] = c->ex[20] * (0.0155f / 127.0f);
    c->ex[13] = chip == CC_1BIT ? 1.0f : 0.0f;
    c->ex[14] = 1.0f / c->ex[3];
    if (wave == CW_NOISE)                             /* the noise's two levels; the pulse's, less its mean, quantised */
        c->ex[15] = 1.0f, c->ex[16] = -1.0f;
    else {
        c->ex[15] = ch_q(1.0f - (2.0f * c->ex[1] - 1.0f), c->ex[3], c->ex[14]);
        c->ex[16] = ch_q(-1.0f - (2.0f * c->ex[1] - 1.0f), c->ex[3], c->ex[14]);
    }
    c->stereo = 0;
}
static void ch_chunk(voice_t *v)                       /* the sweep, the arpeggio and the vibrato: the increment now */
{
    cfg_t *c = &v->c;
    float st = -v->o[3], inc;
    int ar = (int)c->ex[10];
    if (v->o[3] < 120.0f)
        v->o[3] += c->ex[9];
    if (v->o[3] > 0.0f && v->o[3] < CH_SWEEP_END &&   /* fallen out of hearing (or as far as it goes): the end */
        (v->o[3] >= 120.0f || c->ex[0] * fm_exp2f(v->o[3] * (-1.0f / 12.0f)) < CH_SWEEP_HZ * INV_SR))
        v->o[3] = CH_SWEEP_END;
    if (ar)
        st += (float)CH_ARPS[ar][((int)v->o[5] / CH_ARP_CHUNKS) % 3];
    v->o[5] = v->o[5] >= (float)(3 * CH_ARP_CHUNKS - 1) ? 0.0f : v->o[5] + 1.0f;
    inc = st != 0.0f ? c->ex[0] * fm_exp2f(st * (1.0f / 12.0f)) : c->ex[0];
    if (c->ex[11] > 0.0f)
        inc *= 1.0f + c->ex[11] * fm_sin_turns(v->o[6]);
    v->o[6] = wrap1(v->o[6] + 1.0f / 128.0f);         /* 5.4 Hz */
    c->ex[12] = fm_clampf(inc, 8.0f * INV_SR, 0.49f);
}
/* the sample loop, one per wave (the compiler folds the switch away): a new held sample every hold samples, the phase,
 * the tone's one-pole. The held sample's bits and its coarse fall where the wave has them */
SY_INLINE void ch_loop(voice_t *v, float *xl, int m, const int wave)
{
    const cfg_t *c = &v->c;
    const float inc = c->ex[12], hold = c->ex[2], k = c->ex[7], lv = c->ex[3], ilv = c->ex[14], duty = c->ex[1];
    const float hi = c->ex[15], lo = c->ex[16], dc = c->ex[8];
    const int coarse = c->ex[4] > 0.0f, stair = (int)c->ex[18] == CC_STEP, every = c->ex[13] > 0.0f;
    const uint8_t *w = CH_WRAM[(int)c->ex[6] & 15];
    float p = v->o[0], held = v->o[1], cnt = v->o[2], lp = v->o[4];
    uint32_t l = (uint32_t)v->o[7];
    int i;
    for (i = 0; i < m; i++) {
        cnt -= 1.0f;
        if (cnt <= 0.0f) {
            float s;
            cnt = hold;
            if (wave == CW_PULSE) {
                s = p < duty ? hi : lo;           /* (its two levels quantised once: ch_prep) */
            } else if (wave == CW_TRI) {
                if (stair) {                      /* 32 steps of 4 bits: 15..0, 0..15 */
                    int kk = (int)(p * 32.0f);
                    s = (float)(kk < 16 ? 15 - kk : kk - 16) * (2.0f / 15.0f) - 1.0f;
                } else
                    s = ch_q(p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p, lv, ilv);
            } else if (wave == CW_SAW) {
                s = ch_q(2.0f * p - 1.0f, lv, ilv);
            } else if (wave == CW_NOISE) {        /* clocked once a cycle (the phase wraps within the hold), or every hold */
                if (every || p + inc * hold >= 1.0f)
                    l = (l >> 1) | (((l ^ (l >> 1)) & 1u) << 14);
                s = l & 1u ? hi : lo;
            } else {                              /* the wave memory: step by step, no interpolation */
                int kk = (int)(p * 32.0f) & 31, b = kk & 1 ? w[kk >> 1] & 15 : w[kk >> 1] >> 4;
                s = (lv < 1.5f ? (b >= 8 ? 1.0f : -1.0f) : (float)(2 * b - 15) * (1.0f / 15.0f)) - dc;
            }
            if (coarse)                           /* (LOFI's >> 12: 8 steps a side) */
                s = ((float)(int)(s * 8.0f + 16.0f) - 16.0f) * 0.125f;
            held = s;
        }
        p += inc;
        p = p >= 1.0f ? p - 1.0f : p;
        lp += (held - lp) * k;
        xl[i] = lp * CH_GAIN;
    }
    v->o[0] = p;
    v->o[1] = held;
    v->o[2] = cnt;
    v->o[4] = fm_flush(lp);
    v->o[7] = (float)l;
}
/* the sweep's end: the tone's one-pole let down to silence (~1 ms), the chip stopped till the next note */
static void ch_end(voice_t *v, float *xl, int m)
{
    float lp = v->o[4];
    int i;
    for (i = 0; i < m; i++) {
        lp *= 0.97f;
        xl[i] = lp * CH_GAIN;
    }
    v->o[4] = fm_flush(lp);
}
static void ch_run(voice_t *v, float *xl, int m)
{
    if (v->o[3] >= CH_SWEEP_END) {
        ch_end(v, xl, m);
        return;
    }
    switch ((int)v->c.ex[5]) {
    case CW_TRI: ch_loop(v, xl, m, CW_TRI); break;
    case CW_SAW: ch_loop(v, xl, m, CW_SAW); break;
    case CW_NOISE: ch_loop(v, xl, m, CW_NOISE); break;
    case CW_WAVE: ch_loop(v, xl, m, CW_WAVE); break;
    default: ch_loop(v, xl, m, CW_PULSE); break;
    }
}
/* the amp envelope's level as this chip plays it: on step, LOOM-8's ADSR at 16 levels, taken once a frame */
static float ch_amp(voice_t *v)
{
    if ((int)v->c.ex[18] != CC_STEP)
        return v->amp.level;
    v->o[9] -= (float)SUB;
    if (v->o[9] <= 0.0f) {
        v->o[9] += (float)CH_FRAME;
        v->o[8] = fm_floorf(v->amp.level * 15.0f + 0.5f) * (1.0f / 15.0f);
    }
    return v->amp.stage == ENV_OFF ? 0.0f : v->o[8];
}

/* the Engine page's picture (synth.h chip_picture): a cycle of the wave as the chip holds it, 32 steps, each -1..1; the
 * hold counted in steps (a step a sample: a note of ~1.4 kHz), the noise 32 bits of the shift register (from a seed
 * that shows it: from 32767, the start, its first fifteen are all ones) */
void chip_picture(const uint8_t *par, float y[32])
{
    int wave = par[TP_E1] < NCW ? par[TP_E1] : NCW - 1, duty = par[TP_E2] & 127, crush = par[TP_E3] & 127;
    int chip = par[TP_X1] < NCC ? par[TP_X1] : NCC - 1, bits = chip == CC_8BIT ? 8 : chip == CC_1BIT ? 1 : 4;
    int hold = 1 + crush / 8 + (chip == CC_4LO), k;
    float lv = (float)(1 << (bits - 1)), dt = (float)(chip == CC_STEP ? (int[4]){1, 2, 4, 6}[duty >> 5] : 1 + (duty >> 5)) * 0.125f;
    uint32_t l = 0x1D35u;
    float s = 0.0f;
    for (k = 0; k < 32; k++) {
        float p = ((float)k + 0.5f) * (1.0f / 32.0f);
        if (wave == CW_NOISE)
            l = (l >> 1) | (((l ^ (l >> 1)) & 1u) << 14);
        if (k % hold == 0) {
            switch (wave) {
            case CW_PULSE: s = p < dt ? 1.0f : -1.0f; break;
            case CW_TRI:
                s = chip == CC_STEP ? (float)(k < 16 ? 15 - k : k - 16) * (2.0f / 15.0f) - 1.0f
                                    : ch_q(p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p, lv, 1.0f / lv);
                break;
            case CW_SAW: s = ch_q(2.0f * p - 1.0f, lv, 1.0f / lv); break;
            case CW_NOISE: s = l & 1u ? 1.0f : -1.0f; break;
            default: {
                int b = ch_nib(CH_WRAM[(duty >> 3) & 15], k);
                s = bits == 1 ? (b >= 8 ? 1.0f : -1.0f) : (float)(2 * b - 15) * (1.0f / 15.0f);
                break;
            }
            }
            if (crush > 64)
                s = ((float)(int)(s * 8.0f + 16.0f) - 16.0f) * 0.125f;
        }
        y[k] = fm_clampf(s, -1.0f, 1.0f);
    }
}
