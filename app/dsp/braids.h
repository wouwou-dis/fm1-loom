/* SPDX-License-Identifier: MIT */
/* LOOM-8's Braids engine: voile (the voices). (gingham, the toys and chips, went 2026-10-19; cane, the physical models,
 * 2026-11, for sequin's room: its lines, 12 KB of the pool, went with it.) Included by synth.c only (static code on its
 * voice_t, no unit of its own).
 *
 * Ported from Mutable Instruments Braids (github.com/pichenettes/eurorack, braids/digital_oscillator.cc,
 * macro_oscillator.cc and the tables braids/resources/ makes), Copyright 2012 Emilie Gillet, MIT License
 * (the licence text: LICENSING.md); the port Copyright (c) 2026 Charles Vestal, MIT. Braids is fixed point at
 * 96 kHz (some models at half that); here every model is float at LOOM-8's 44.1 kHz: the resonators moved to the same
 * cutoff, the envelopes in seconds, the oscillators' increments in turns of 44.1 kHz. What each knob does is Braids':
 * E2 is its TIMBRE, E3 its COLOR; E1 picks the model (Braids' shape) and E4 is LOOM-8's own (breath).
 *
 *   voile   vosim  VOSM: two formants (sines reset each period, under a bell-shaped window); timbre and color
 *                  the two formants' pitches
 *           vowel  VOWL: Braids' speech synth (three 4-bit formants, a consonant at each note); timbre the
 *                  vowel, color the formants' shift; breath the consonant's length
 *           choir  VFOF: five resonant formants on a saw, singers' data from bass to soprano; timbre the vowel
 *                  (a e i o u), color the voice
 *           whisper TWNQ: two resonant peaks (the note's and one color sets) on noise; timbre their
 *                  sharpness; breath 127 is all noise (Braids'), lower brings a buzz in under it
 *   (voile's breath: noise into the excitation, 0 clean)
 *
 * Changes from Braids, and why:
 *   - one note a voice: LOOM-8's voices;
 *   - vowel: the three formants' sum is clamped before the window (Braids' int16 wraps at the loudest corner);
 *   - DC blockers after vosim and vowel (their output carries the window's DC);
 *   - in tune at 44.1 kHz (tests/host/braids_test measures it);
 *   - choir: all five formants at one gain, as Braids' (its code reads the first formant's amplitude for each);
 *   - whisper: the buzz under the noise is LOOM-8's (breath, 127 = Braids' TWNQ);
 *   - Braids' parameter interpolation and its sync input are not here (LOOM-8's knobs move once a chunk). */

/* ------------------------------------------------------------ shared ---- */
/* what a voice of each model costs in the cores' split's units (an ordinary voice 4, ~1.1 % of a block a unit): as the
 * FM-1 measured them (tools/measure_load.sh 2026-10-18, two cores: voile's choir one and a half, the rest about one) */
static const uint8_t VO_COST[NVOILE] = {4, 4, 6, 4};
static inline float bd_mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static inline float bd_note(float hz) { return 69.0f + 12.0f * fm_log2f(hz * (1.0f / 440.0f)); }
/* fm_clampf's value (lo < hi; x not NaN) as the FPU's max and min: two instructions on the FM-1, where the
 * compares and selects take four */
static inline float bd_clamp(float x, float lo, float hi) { return fm_minf(fm_maxf(x, lo), hi); }
/* Braids' ws_moderate_overdrive, tanh(2x) x 1.037, for |x| <= 1 (voile's vowel and whisper): below |2x| = 0.5
 * fm_tanhf's own Taylor branch (the same value), above it a polynomial in |2x| - 1.25 (within 1.3e-7 of tanh to |2x| = 2: fm_tanhf's exp2 and divide are
 * costly on the FM-1) */
static inline float vo_od(float x)
{
    float y = 2.0f * x, a = fm_fabsf(y), u, r;
    if (a < 0.5f)
        return fm_tanhf(y) * 1.0373147f;
    u = a - 1.25f;
    r = 1.690596691e-03f;
    r = r * u - 1.836245297e-03f;
    r = r * u - 4.638960119e-03f;
    r = r * u + 1.562794298e-02f;
    r = r * u - 1.920663565e-02f;
    r = r * u - 1.267151069e-02f;
    r = r * u + 1.083122119e-01f;
    r = r * u - 2.378656268e-01f;
    r = r * u + 2.804148197e-01f;
    r = r * u + 8.482835889e-01f;
    return (y < 0.0f ? -r : r) * 1.0373147f;
}

/* ------------------------------------------------------------ voile ---- */
enum { VO_VOSIM, VO_VOWEL, VO_CHOIR, VO_WHISPER };
static const float VO_GAIN[NVOILE] = {1.4f, 1.0f, 3.5f, 2.8f};
/* vowel: Braids' phonemes, the three formants' frequencies (x shift x 0.0916 Hz) and amplitudes (0..15) */
static const uint8_t VOWELS[9][6] = {{27, 40, 89, 15, 13, 1}, {18, 51, 62, 13, 12, 6}, {15, 69, 93, 14, 12, 7},
                                     {10, 84, 110, 13, 10, 8}, {23, 44, 87, 15, 12, 1}, {13, 29, 80, 13, 8, 0},
                                     {6, 46, 81, 12, 3, 0},    {9, 51, 95, 15, 3, 0},   {6, 73, 99, 7, 3, 14}};
static const uint8_t CONSONANTS[8][6] = {{6, 54, 121, 9, 9, 0},   {18, 50, 51, 12, 10, 5}, {11, 24, 70, 13, 8, 0},
                                         {15, 69, 74, 14, 12, 7}, {16, 37, 111, 14, 8, 1}, {18, 51, 62, 14, 12, 6},
                                         {6, 26, 81, 5, 5, 5},    {6, 73, 99, 7, 10, 14}};
/* Braids' wav_formant_sine and _square: 16 phases x 16 amplitudes (waveforms.py) */
static const int8_t FMT_SINE[256] = {
    0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 2,  2,  3,  3,  4,  5,  6,  7,  8,  10, 12, 14, 17, 20, 24,
    0, 3,  4,  5,  6,  7,  9,  10, 12, 15, 18, 21, 26, 31, 37, 45, 0, 4,  5,  6,  8,  9,  11, 13, 16, 19, 23, 28, 34, 40, 49, 58,
    0, 5,  6,  7,  8,  10, 12, 15, 17, 21, 25, 30, 36, 44, 53, 63, 0, 4,  5,  6,  8,  9,  11, 13, 16, 19, 23, 28, 34, 40, 49, 58,
    0, 3,  4,  5,  6,  7,  9,  10, 12, 15, 18, 21, 26, 31, 37, 45, 0, 2,  2,  3,  3,  4,  5,  6,  7,  8,  10, 12, 14, 17, 20, 24,
    0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, -2, -2, -3, -3, -4, -5, -6, -7, -8, -10, -12, -14, -17, -20, -24,
    0, -3, -4, -5, -6, -7, -9, -10, -12, -15, -18, -21, -26, -31, -37, -45, 0, -4, -5, -6, -8, -9, -11, -13, -16, -19, -23, -28, -34, -40, -49, -58,
    0, -5, -6, -7, -8, -10, -12, -15, -17, -21, -25, -30, -36, -44, -53, -63, 0, -4, -5, -6, -8, -9, -11, -13, -16, -19, -23, -28, -34, -40, -49, -58,
    0, -3, -4, -5, -6, -7, -9, -10, -12, -15, -18, -21, -26, -31, -37, -45, 0, -2, -2, -3, -3, -4, -5, -6, -7, -8, -10, -12, -14, -17, -20, -24};
static const int8_t FMT_SQ[16] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 6, 8, 9, 11, 13, 16};   /* (+ the first half, - the second) */
/* choir: Braids' formant_f_data (1/128 semitone; voice bass, tenor, countertenor, alto, soprano x vowel a e i o u
 * x formant) */
static const int16_t FOF_F[5][5][5] = {
    {{9519, 10738, 12448, 12636, 12892}, {8620, 11720, 12591, 12932, 13158}, {7579, 11891, 12768, 13122, 13323},
     {8620, 10013, 12591, 12768, 13010}, {8324, 9519, 12591, 12831, 13048}},
    {{9696, 10821, 12810, 13010, 13263}, {8620, 11827, 12768, 13228, 13477}, {7908, 12038, 12932, 13263, 13452},
     {8620, 10156, 12768, 12932, 13085}, {8324, 9519, 12852, 13010, 13296}},
    {{9730, 10902, 12892, 13085, 13330}, {8832, 11953, 12852, 13085, 13296}, {7749, 12014, 13010, 13330, 13483},
     {8781, 10211, 12852, 13085, 13296}, {8448, 9627, 12892, 13085, 13363}},
    {{10156, 10960, 12932, 13427, 14195}, {8620, 11692, 12852, 13296, 14195}, {8324, 11827, 12852, 13550, 14195},
     {8881, 10156, 12956, 13427, 14195}, {8160, 9860, 12708, 13427, 14195}},
    {{10156, 10960, 13010, 13667, 14195}, {8324, 12187, 12932, 13489, 14195}, {7749, 12337, 13048, 13667, 14195},
     {8881, 10156, 12956, 13609, 14195}, {8160, 9860, 12852, 13609, 14195}}};
static inline int vo_model(float k) { int m = (int)(k + 0.5f); return m < 0 ? 0 : m >= NVOILE ? NVOILE - 1 : m; }
/* Braids' bell window for VOSIM (lut_bell: up over the first 1/16 of the period, down over the rest), as
 * smoothsteps (within 1 % of its Hann halves) */
static inline float vo_bell(float p)
{
    float x;
    if (p < 0.0625f) {
        x = p * 16.0f;
        return x * x * (3.0f - 2.0f * x);
    }
    x = (p - 0.0625f) * (16.0f / 15.0f);
    return 1.0f - x * x * (3.0f - 2.0f * x);
}
/* the consonant's (o[6]) formants into ex[7..13] */
static void vo_consonant(voice_t *v)
{
    cfg_t *c = &v->c;
    int k = (int)v->o[6], i;
    float sh = (200.0f + (float)((int)(c->e[2] * 32767.0f) >> 6)) * (96000.0f / 4294967296.0f * 4096.0f) * INV_SR;
    if (k < 0 || k > 7)
        k = 0;
    for (i = 0; i < 3; i++) {
        c->ex[7 + i] = (float)CONSONANTS[k][i] * sh;
        c->ex[10 + i] = (float)CONSONANTS[k][3 + i];
    }
    c->ex[13] = k >= 6 ? 0.0312f : 0.0f;         /* the last two: noisy resets (Braids' 4095) */
}
/* o[]: 0 the model (+1), 1 the period's phase; vosim 2 / 8 and 3 / 9 the formants' phasors (cos / sin), 4 / 5 its DC
 * blocker; vowel 2..4 the formants' phases, 5 the consonant's samples left, 6 which one, 7 a strike to make, 8 / 9
 * its DC blocker; choir 2..6 the formants' low-passes, 7..11 their band-passes, 12 / 13 the timbre and color
 * ex[1..5] were made from (14: 1 when they were); whisper 2..5 the two resonators, 6 the buzz's phase */
static void vo_init(voice_t *v)
{
    int i;
    for (i = 0; i < 16; i++)
        v->o[i] = 0.0f;
    v->o[7] = 1.0f;
}
static void vo_strike(voice_t *v) { v->o[7] = 1.0f; }
static void vo_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int m = vo_model(c->ek[0]), i;
    float f = c->freq, tb = c->e[1], co = c->e[2], b = c->e[3];
    c->stereo = 0;
    c->ex[19] = (float)m;
    if ((int)v->o[0] != m + 1) {
        float s7 = v->o[7];
        vo_init(v);
        v->o[0] = (float)(m + 1);
        v->o[7] = s7;
        if (m == VO_VOSIM)
            v->o[2] = v->o[3] = 1.0f;            /* (its formants: phasors at 1 + 0i) */
    }
    c->ex[0] = fm_minf(f * INV_SR, 0.45f);
    c->ex[18] = b;
    switch (m) {
    case VO_VOSIM:                               /* the formants' turns a sample, as rotations */
        c->ex[1] = fm_minf(bd_mtof(tb * 127.99f) * INV_SR, 0.49f);
        c->ex[2] = fm_minf(bd_mtof(co * 127.99f) * INV_SR, 0.49f);
        c->ex[3] = fm_cosf(FM_TWO_PI * c->ex[1]);
        c->ex[4] = fm_sinf(FM_TWO_PI * c->ex[1]);
        c->ex[5] = fm_cosf(FM_TWO_PI * c->ex[2]);
        c->ex[6] = fm_sinf(FM_TWO_PI * c->ex[2]);
        break;
    case VO_VOWEL: {
        int p0 = (int)(tb * 32767.0f), vi = p0 >> 12;
        float bal = (float)(p0 & 0xFFF) * (1.0f / 4096.0f);
        float sh = (200.0f + (float)((int)(co * 32767.0f) >> 6)) * (96000.0f / 4294967296.0f * 4096.0f) * INV_SR;
        for (i = 0; i < 3; i++) {
            c->ex[1 + i] = ((float)VOWELS[vi][i] * (1.0f - bal) + (float)VOWELS[vi + 1][i] * bal) * sh;
            c->ex[4 + i] = (float)(((int)VOWELS[vi][3 + i] * (0x1000 - (p0 & 0xFFF)) + (int)VOWELS[vi + 1][3 + i] * (p0 & 0xFFF)) >> 12);
        }
        vo_consonant(v);
        c->ex[14] = b * (0.08f * SY_SR);          /* the consonant's length: 0 .. 80 ms (Braids' 40 at 64) */
        break;
    }
    case VO_CHOIR: {
        float x = co * 4.0f, y = tb * 4.0f, xf, yf;
        int xi = (int)x, yi = (int)y;
        if (v->o[14] > 0.5f && v->o[12] == tb && v->o[13] == co)
            break;                               /* the formants as they are (the note does not move them) */
        v->o[12] = tb;
        v->o[13] = co;
        v->o[14] = 1.0f;
        if (xi > 3)
            xi = 3;
        if (yi > 3)
            yi = 3;
        xf = x - (float)xi;
        yf = y - (float)yi;
        for (i = 0; i < 5; i++) {
            float a = (float)FOF_F[xi][yi][i] + (float)(FOF_F[xi + 1][yi][i] - FOF_F[xi][yi][i]) * xf;
            float d = (float)FOF_F[xi][yi + 1][i] + (float)(FOF_F[xi + 1][yi + 1][i] - FOF_F[xi][yi + 1][i]) * xf;
            float fc = bd_mtof((a + (d - a) * yf) * (1.0f / 128.0f));
            c->ex[1 + i] = 2.0f * fm_sinf(FM_PI * fm_minf(fc * INV_SR, 0.125f));
        }
        break;
    }
    default: {                                   /* whisper: two resonators, the note's and color's */
        float r = fm_exp2f(fm_log2f((65240.0f + tb * 255.99f) * (1.0f / 65536.0f)) * (48000.0f / SY_SR));
        float n = bd_note(f), w1 = fm_minf(f * INV_SR, 0.24f), w2 = fm_minf(bd_mtof(n + (co - 0.5f) * 128.0f) * INV_SR, 0.24f);
        float r2 = r * r, ws[2];
        ws[0] = w1;
        ws[1] = w2;
        for (i = 0; i < 2; i++) {               /* each at unit RMS for white noise */
            float cs = fm_cosf(FM_TWO_PI * ws[i]), q = (1.0f + r2) * (1.0f + r2) - 4.0f * r2 * cs * cs;
            c->ex[1 + i] = 2.0f * r * cs;
            c->ex[3 + i] = fm_sqrtf((1.0f - r2) * fm_maxf(q, 1e-9f) / (1.0f + r2));
        }
        c->ex[5] = r2;
        c->ex[7] = 0.03f;                        /* the buzz: at the peaks it rings, so a little */
        c->ex[6] = 1.0f + (8191.0f - (float)((int)(tb * 32767.0f) >> 2)) * (1.0f / 8192.0f);   /* the makeup */
        break;
    }
    }
}
static void vo_chunk(voice_t *v)
{
    cfg_t *c = &v->c;
    if ((int)c->ex[19] == VO_VOWEL && v->o[7] > 0.5f) {   /* the vowel's consonant at a note */
        v->o[7] = 0.0f;
        v->o[6] = (float)((xs(&v->rng) >> 8) & 7u);
        v->o[5] = c->ex[14];
        vo_consonant(v);
    }
    v->o[7] = 0.0f;
}
/* the two formants as rotating phasors (o[2] / o[8] and o[3] / o[9]: cos / sin), back to 1 + 0i each period
 * (Braids' phases reset); a rotation is four multiplies where a sine is a polynomial */
static void vo_vosim(voice_t *v, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    float p = v->o[1], ar = v->o[2], ai = v->o[8], br_ = v->o[3], bi = v->o[9], dx = v->o[4], dy = v->o[5];
    float d = c->ex[0], ca = c->ex[3], sa = c->ex[4], cb = c->ex[5], sb = c->ex[6], br = c->ex[18];
    uint32_t r = v->rng;
    int i;
    for (i = 0; i < m; i++) {
        float s, w, t;
        p += d;
        t = ar * ca - ai * sa;
        ai = ar * sa + ai * ca;
        ar = t;
        t = br_ * cb - bi * sb;
        bi = br_ * sb + bi * cb;
        br_ = t;
        if (p >= 1.0f) {                         /* a period: the formants start again */
            p -= 1.0f;
            ar = br_ = 1.0f;
            ai = bi = 0.0f;
            s = 0.0f;
        } else {
            w = vo_bell(p);
            s = (0.75f + 0.5f * ai + 0.25f * bi) * w;
            s = s + (frand(&r) * w - s) * br;    /* breath: noise under the window */
        }
        dy = (s - 0.75f) - dx + 0.9985f * dy;
        dx = s - 0.75f;
        xl[i] = dy * g;
    }
    v->rng = r;
    v->o[1] = p;
    v->o[2] = ar;
    v->o[8] = ai;
    v->o[3] = br_;
    v->o[9] = bi;
    v->o[4] = dx;
    v->o[5] = fm_flush(dy);
}
static void vo_vowel(voice_t *v, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    float p = v->o[1], f1 = v->o[2], f2 = v->o[3], f3 = v->o[4], left = v->o[5], d = c->ex[0], dx = v->o[8], dy = v->o[9];
    int cons = left > 0.0f, i;
    const float *inc = cons ? c->ex + 7 : c->ex + 1, *amp = cons ? c->ex + 10 : c->ex + 4;
    int a1 = (int)amp[0], a2 = (int)amp[1], a3 = (int)amp[2];
    float nz = cons ? c->ex[13] : 0.0f;
    uint32_t r = v->rng;
    for (i = 0; i < m; i++) {
        int s, k3;
        float x, pn;
        p += d;
        if (p >= 1.0f)
            p -= 1.0f;
        f1 = step1(f1, inc[0]);
        f2 = step1(f2, inc[1]);
        f3 = step1(f3, inc[2]);
        k3 = (int)(f3 * 16.0f);
        s = FMT_SINE[((int)(f1 * 16.0f) << 4) | a1] + FMT_SINE[((int)(f2 * 16.0f) << 4) | a2] + (k3 < 8 ? FMT_SQ[a3] : -FMT_SQ[a3]);
        x = (float)(s < -128 ? -128 : s > 128 ? 128 : s) * (float)(255 - (int)(p * 256.0f));
        pn = nz > 0.0f ? p + frand(&r) * nz : p;
        if ((pn >= 0.0f && pn < d) || (pn >= 1.0f && pn < 1.0f + d)) {   /* the period: the formants' phases back to 0 */
            f1 = f2 = f3 = 0.0f;
            x = 0.0f;
        }
        x = vo_od(x * (1.0f / 32768.0f));
        dy = x - dx + 0.995f * dy;              /* its DC out (the window's ramp makes it one-sided) */
        dx = x;
        xl[i] = dy * g;
    }
    if (cons) {
        left -= (float)m;
        if (left < 0.0f)
            left = 0.0f;
    }
    v->rng = r;
    v->o[1] = p;
    v->o[2] = f1;
    v->o[3] = f2;
    v->o[4] = f3;
    v->o[5] = left;
    v->o[8] = dx;
    v->o[9] = fm_flush(dy);
}
/* Braids' SVF (Q 64): its band-pass */
SY_INLINE float vo_svf(float *lp, float *bp, float k, float in)
{
    float h;
    *lp = bd_clamp(*lp + k * *bp, -32767.0f, 32767.0f);
    h = in - *bp * (1.0f / 64.0f) - *lp;
    *bp = bd_clamp(*bp + k * h, -32767.0f, 32767.0f);
    return *bp;
}
/* the choir's formants j and j + 1 over the m samples into acc (the last pair: the output, at gain g). Two at a
 * time keep their state in registers (the FM-1's floats share its sixteen general registers: five at once
 * spilled most of them every sample); the sum in the order of five at once, so the same samples */
SY_INLINE void vo_fof2(voice_t *v, int j, const float *ins, float *acc, float *xl, int m, float g, const int last)
{
    float l0 = v->o[2 + j], b0 = v->o[7 + j], k0 = v->c.ex[1 + j], l1 = v->o[3 + j], b1 = v->o[8 + j], k1 = v->c.ex[2 + j];
    int i;
    for (i = 0; i < m; i++) {
        float in = ins[i], s = acc[i] + vo_svf(&l0, &b0, k0, in);
        s += vo_svf(&l1, &b1, k1, in);
        if (last)
            xl[i] = bd_clamp(s * (1.0f / (8.0f * 32768.0f)), -1.0f, 1.0f) * g;
        else
            acc[i] = s;
    }
    v->o[2 + j] = fm_flush(l0);
    v->o[7 + j] = fm_flush(b0);
    v->o[3 + j] = fm_flush(l1);
    v->o[8 + j] = fm_flush(b1);
}
static void vo_choir(voice_t *v, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    float p = v->o[1], d = c->ex[0], id = 1.0f / d, br = c->ex[18], lp = v->o[2], bp = v->o[7], k = c->ex[1];
    float ins[SUB], acc[SUB];
    uint32_t r = v->rng;
    int i;
    for (i = 0; i < m; i++) {                    /* the saw (and the breath), and the first formant */
        float in;
        p = step1(p, d);
        in = (2.0f * p - 1.0f - blep(p, d, id)) * 16384.0f;
        in += (frand(&r) * 16384.0f - in) * br;  /* breath */
        ins[i] = in;
        acc[i] = 0.0f + vo_svf(&lp, &bp, k, in);
    }
    v->rng = r;
    v->o[1] = p;
    v->o[2] = fm_flush(lp);
    v->o[7] = fm_flush(bp);
    vo_fof2(v, 1, ins, acc, xl, m, g, 0);
    vo_fof2(v, 3, ins, acc, xl, m, g, 1);
}
static void vo_whisper(voice_t *v, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    float y11 = v->o[2], y12 = v->o[3], y21 = v->o[4], y22 = v->o[5], ph = v->o[6], d = c->ex[0], id = 1.0f / d;
    float c1 = c->ex[1], c2 = c->ex[2], s1 = c->ex[3], s2 = c->ex[4], q2 = c->ex[5], mk = c->ex[6], br = c->ex[18];
    float gb = c->ex[7] * (1.0f - br);
    uint32_t r = v->rng;
    int i;
    for (i = 0; i < m; i++) {
        float x, a, b;
        ph = step1(ph, d);
        x = frand(&r) * 0.5f * br + (2.0f * ph - 1.0f - blep(ph, d, id)) * gb;   /* (Braids' noise >> 1) */
        a = x * s1 + y11 * c1 - y12 * q2;
        b = x * s2 + y21 * c2 - y22 * q2;
        y12 = y11;
        y11 = a;
        y22 = y21;
        y21 = b;
        xl[i] = vo_od(bd_clamp((a + b) * 0.1f * mk, -1.0f, 1.0f)) * g;
    }
    v->rng = r;
    v->o[2] = fm_flush(y11);
    v->o[3] = fm_flush(y12);
    v->o[4] = fm_flush(y21);
    v->o[5] = fm_flush(y22);
    v->o[6] = ph;
}
static void vo_run(voice_t *v, float *xl, float *xr, int m)   /* (each model's gain at its last step) */
{
    int md = (int)v->c.ex[19];
    float g = VO_GAIN[md];
    (void)xr;
    switch (md) {
    case VO_VOSIM: vo_vosim(v, xl, m, g); break;
    case VO_VOWEL: vo_vowel(v, xl, m, g); break;
    case VO_CHOIR: vo_choir(v, xl, m, g); break;
    default: vo_whisper(v, xl, m, g); break;
    }
}
