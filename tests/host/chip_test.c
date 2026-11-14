/* SPDX-License-Identifier: MPL-2.0 */
/* sequin, the chip voice (dsp/chip.h, after Felucca-Engines' LOFI, MIT) on its own: every wave, chip and knob corner
 * bounded and finite across the notes; the waves in tune; the sweep falls; the arpeggio steps an octave; the bits and the
 * hold as LOFI has them (the levels a held sample takes, how long it holds); the noise's shift register clocked once a
 * cycle, or at the held rate on 1 bit; the step chip's envelope in sixteen levels, a frame at a time; the knobs' texts and
 * the machine's picture; its presets; and its cost against knit's, by the host's instruction count (as
 * tests/host/eng_bench.c counts it: a chord of four through the whole voice path, each preset).
 * It includes synth.c to reach the engine's static functions. Nothing here plays audio.
 *   tests/host/chip_test.sh */
#ifndef SYNTH_C
#define SYNTH_C "../../app/dsp/synth.c"
#endif
#include SYNTH_C
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "instr_count.h"                       /* instr_now */

static int bad;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define N 256
static uint8_t par[NTRK + 1][NTP];
static _Alignas(16) uint8_t mem[SYNTH_BYTES];
static synth_t *S = (synth_t *)mem;
static float L[N], R[N], S1[N], S2[N];
static int finite_ok = 1;
static float peak;
static float buf[N * 200];

static int finite_f(float x) { return x == x && x < 1e30f && x > -1e30f; }
static void block(void)
{
    int i;
    memset(L, 0, sizeof L), memset(R, 0, sizeof R), memset(S1, 0, sizeof S1), memset(S2, 0, sizeof S2);
    synth_prepare(S, N);
    synth_render(S, 0, NVOICE, L, R, S1, S2, N);
    for (i = 0; i < N; i++) {
        if (!finite_f(L[i]) || !finite_f(R[i]))
            finite_ok = 0;
        peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
    }
}
/* a plain sound: the filter open (lp12 at 20 kHz), full sustain, no LFO, no character */
static void reset(int wave, int duty, int crush, int sweep, int chip, int arp)
{
    int t;
    for (t = 0; t < NTRK; t++) {
        sound_default(par[t], EG_SEQUIN);
        par[t][TP_E1] = (uint8_t)wave, par[t][TP_E2] = (uint8_t)duty, par[t][TP_E3] = (uint8_t)crush;
        par[t][TP_E4] = (uint8_t)sweep, par[t][TP_X1] = (uint8_t)chip, par[t][TP_X2] = (uint8_t)arp;
        par[t][TP_FTYPE] = FT_LP12, par[t][TP_CUT] = 127, par[t][TP_RES] = 0, par[t][TP_FENV] = 64, par[t][TP_KTRK] = 0;
        par[t][TP_A] = 0, par[t][TP_S] = 127, par[t][TP_R] = 40;
        par[t][TP_LTYPE] = LT_OFF, par[t][TP_CTYPE] = CT_OFF, par[t][TP_VOLUME] = 127, par[t][TP_LEVEL] = 108;
        par[t][TP_PAN] = 64;
    }
    synth_init(S, par);
    synth_tempo(S, 120.0f);
}
static void held(int note, int nb, float *o)
{
    int b;
    synth_note_on(S, 0, note, 127, 0, 1);
    for (b = 0; b < nb; b++) {
        block();
        if (o)
            memcpy(o + b * N, L, sizeof L);
    }
}
static double rms(const float *x, int n)
{
    double a = 0.0;
    for (int i = 0; i < n; i++)
        a += (double)x[i] * (double)x[i];
    return sqrt(a / n);
}
/* the pitch: the normalised autocorrelation's best lag within +-40 % of the period hz has, parabolic between lags */
static double pitch_of(const float *x, int n, double hz)
{
    static double ac[4200];
    double best = -2.0, p0 = 44100.0 / hz;
    int lo = (int)(p0 * 0.6), hi = (int)(p0 * 1.4) + 1, k, bk = lo, i;
    if (hi > 4000)
        hi = 4000;
    for (k = lo - 1; k <= hi + 1; k++) {
        double a = 0.0, e1 = 0.0, e2 = 0.0;
        for (i = 0; i + k < n; i++) {
            a += (double)x[i] * (double)x[i + k];
            e1 += (double)x[i] * (double)x[i];
            e2 += (double)x[i + k] * (double)x[i + k];
        }
        ac[k] = a / sqrt(e1 * e2 + 1e-30);
    }
    for (k = lo; k <= hi; k++)
        if (ac[k] > best && ac[k] >= ac[k - 1] && ac[k] >= ac[k + 1])
            best = ac[k], bk = k;
    {
        double a = ac[bk - 1], b = ac[bk], c = ac[bk + 1], d = a - 2.0 * b + c;
        return 44100.0 / ((double)bk + (d != 0.0 ? 0.5 * (a - c) / d : 0.0));
    }
}
static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }
static double mtof(int n) { return 440.0 * pow(2.0, (n - 69) / 12.0); }
static int crossings(const float *x, int n)
{
    int c = 0;
    for (int i = 1; i < n; i++)
        c += (x[i - 1] < 0.0f) != (x[i] < 0.0f);
    return c;
}

/* the engine alone: a voice set up as voice_chunk would, nb chunks of its output (the tone open: the held samples
 * themselves, times CH_GAIN) */
static voice_t vv;
static void alone(int wave, int duty, int crush, int chip, int note, int nchunk, float *o)
{
    voice_t *v = &vv;
    memset(v, 0, sizeof *v);
    v->engine = EG_SEQUIN;
    eng_init(v);
    ch_note(v);
    v->c.ek[0] = (float)wave, v->c.ek[1] = (float)duty, v->c.ek[2] = (float)crush, v->c.ek[3] = 0.0f;
    v->c.ex[18] = (float)chip, v->c.ex[19] = 0.0f, v->c.ex[20] = 0.0f, v->c.ex[21] = 127.0f;
    v->c.freq = (float)mtof(note);
    eng_prep(v, 120.0f);
    for (int i = 0; i < nchunk; i++) {
        eng_chunk(v);
        eng_run(v, o + i * SY_CHUNK, o + i * SY_CHUNK, SY_CHUNK);
    }
}
static int distinct(const float *x, int n)        /* how many different values x takes (at most 64 counted) */
{
    float seen[64];
    int k = 0;
    for (int i = 0; i < n && k < 64; i++) {
        int j;
        for (j = 0; j < k && seen[j] != x[i]; j++)
            ;
        if (j == k)
            seen[k++] = x[i];
    }
    return k;
}
static int longest_run(const float *x, int n)    /* the longest run of one value */
{
    int best = 1, run = 1;
    for (int i = 1; i < n; i++) {
        run = x[i] == x[i - 1] ? run + 1 : 1;
        best = run > best ? run : best;
    }
    return best;
}

/* the full voice path's cost: a chord of four held, each of the engine's presets, instructions a voice-sample */
static double cost(int engine)
{
    double sum = 0.0;
    int n = 0;
    for (int k = 0; k < NPRESETS; k++) {
        uint64_t i0, ti = 0;
        int t, b;
        if (PRESETS[k].par[TP_ENGINE] != engine)
            continue;
        for (t = 0; t < NTRK; t++)
            sound_default(par[t], EG_KNIT);
        memcpy(par[0], PRESETS[k].par, NTP_SOUND);
        par[0][TP_VMODE] = VM_POLY;
        synth_init(S, par);
        synth_tempo(S, 120.0f);
        synth_note_on(S, 0, 48, 100, 0, 1), synth_note_on(S, 0, 55, 90, 0, 2);
        synth_note_on(S, 0, 60, 110, 0, 3), synth_note_on(S, 0, 64, 80, 0, 4);
        for (b = 0; b < 200; b++) {
            memset(L, 0, sizeof L), memset(R, 0, sizeof R), memset(S1, 0, sizeof S1), memset(S2, 0, sizeof S2);
            synth_prepare(S, N);
            i0 = instr_now();
            synth_render(S, 0, NVOICE, L, R, S1, S2, N);
            ti += instr_now() - i0;
        }
        sum += (double)ti / (200.0 * N * 4);
        n++;
    }
    return n ? sum / n : 0.0;
}

int main(void)
{
    char what[120], num[16], unit[8];
    int w, c, k, n, b;

    /* bounded and finite: every wave x chip, the knobs at 0 / 64 / 127, an arpeggio, notes 12..120 */
    {
        int ok = 1;
        float worst = 0.0f;
        for (w = 0; w < NCW; w++)
            for (c = 0; c < NCC; c++)
                for (k = 0; k < 9; k++) {
                    static const int V[3] = {0, 64, 127};
                    for (n = 12; n <= 120; n += 27) {
                        reset(w, V[k % 3], V[(k + 1) % 3], V[k / 3], c, k & 3);
                        finite_ok = 1, peak = 0.0f;
                        held(n, 24, 0);
                        if (!finite_ok || peak > 2.0f) {
                            printf("    %s %s knobs %d %d %d note %d: peak %.2f%s\n", CH_WAVE_N[w], CH_CHIP_N[c], V[k % 3],
                                   V[(k + 1) % 3], V[k / 3], n, (double)peak, finite_ok ? "" : " NOT FINITE");
                            ok = 0;
                        }
                        worst = fmaxf(worst, peak);
                    }
                }
        printf("    the loudest corner %.2f\n", (double)worst);
        check("every wave x chip x knob corner x note 12..120: bounded, finite", ok);
    }
    /* in tune: pulse, triangle, saw and a table, 4 and 8 bit, notes 36..84 */
    {
        static const struct { int w, d, c; } P[] = {{CW_PULSE, 96, CC_4BIT}, {CW_TRI, 0, CC_4BIT}, {CW_SAW, 0, CC_8BIT},
                                                     {CW_WAVE, 3 * 8, CC_4BIT}, {CW_PULSE, 32, CC_4LO}};
        printf("pitch: the measured fundamental against the note, cents\n");
        for (k = 0; k < (int)(sizeof P / sizeof P[0]); k++) {
            int ok = 1;
            printf("  %-6s %-8s", CH_WAVE_N[P[k].w], CH_CHIP_N[P[k].c]);
            for (n = 36; n <= 84; n += 12) {
                double f;
                reset(P[k].w, P[k].d, 0, 0, P[k].c, 0);
                held(n, 40, buf);
                f = cents(pitch_of(buf + 10 * N, 30 * N, mtof(n)), mtof(n));
                printf(" %3d:%+5.1f", n, f);
                ok &= fabs(f) < 5.0;
            }
            printf("\n");
            snprintf(what, sizeof what, "%s on %s in tune (within 5 cents, notes 36..84)", CH_WAVE_N[P[k].w], CH_CHIP_N[P[k].c]);
            check(what, ok);
        }
    }
    /* the sweep: the pitch falls (fewer crossings 40 ms on than in the first 10 ms), and none without it */
    {
        int a0, a1, b0, b1;
        reset(CW_PULSE, 96, 0, 64, CC_4BIT, 0);
        held(84, 20, buf);
        a0 = crossings(buf, 441), a1 = crossings(buf + 1764, 441);
        reset(CW_PULSE, 96, 0, 0, CC_4BIT, 0);
        held(84, 20, buf);
        b0 = crossings(buf, 441), b1 = crossings(buf + 1764, 441);
        snprintf(what, sizeof what, "the sweep falls (crossings in 10 ms: %d at the start, %d 40 ms on; none: %d, %d)", a0, a1,
                 b0, b1);
        check(what, a1 * 4 < a0 && abs(b0 - b1) <= 2);
    }
    /* the arpeggio, octave: the note, then its octave, a step every 896 samples (LOFI's ~49 Hz) */
    {
        double f0, f1, f2;
        reset(CW_TRI, 0, 0, 0, CC_4BIT, 1);
        held(57, 12, buf);
        f0 = pitch_of(buf + 100, 700, mtof(57)), f1 = pitch_of(buf + 996, 700, 2.0 * mtof(57));
        f2 = pitch_of(buf + 1892, 700, mtof(57));
        snprintf(what, sizeof what, "the octave arp: %.0f, %.0f, %.0f Hz (the note 220)", f0, f1, f2);
        check(what, fabs(cents(f0, 220.0)) < 10.0 && fabs(cents(f1, 440.0)) < 10.0 && fabs(cents(f2, 220.0)) < 10.0);
    }
    /* the bits and the hold, the engine alone (the tone open: its output is the held samples) */
    {
        static float o[SY_CHUNK * 40];
        int d1, d4, d8, run0, run40, runlo;
        alone(CW_SAW, 0, 0, CC_1BIT, 45, 40, o), d1 = distinct(o, SY_CHUNK * 40);
        alone(CW_SAW, 0, 0, CC_4BIT, 45, 40, o), d4 = distinct(o, SY_CHUNK * 40), run0 = longest_run(o, SY_CHUNK * 40);
        alone(CW_SAW, 0, 0, CC_8BIT, 45, 40, o), d8 = distinct(o, SY_CHUNK * 40);
        alone(CW_SAW, 0, 40, CC_8BIT, 96, 40, o), run40 = longest_run(o, SY_CHUNK * 40);
        alone(CW_SAW, 0, 0, CC_4LO, 96, 40, o), runlo = longest_run(o, SY_CHUNK * 40);
        snprintf(what, sizeof what, "a saw's levels: 1 bit %d, 4 bit %d, 8 bit %d (LOFI's: 3, 17, many)", d1, d4, d8);
        check(what, d1 <= 3 && d4 <= 17 && d4 >= 15 && d8 > 32);
        snprintf(what, sizeof what, "the hold: crush 40 holds 6 samples, 4 bit lo 2 (runs at a high note: %d, %d)", run40, runlo);
        check(what, run40 == 6 && runlo == 2 && run0 > 1);
    }
    /* the noise: once a cycle (the note's rate) on 4 bit, at the held rate on 1 bit */
    {
        static float o[SY_CHUNK * 1400];
        int c4, c1;
        alone(CW_NOISE, 0, 0, CC_4BIT, 60, 1400, o), c4 = crossings(o, SY_CHUNK * 1400);
        alone(CW_NOISE, 0, 0, CC_1BIT, 60, 1400, o), c1 = crossings(o, SY_CHUNK * 1400);
        snprintf(what, sizeof what, "the noise: %d crossings in 2 s clocked a cycle (C4: 523 clocks), %d at the full rate", c4,
                 c1);
        check(what, c4 <= 523 && c4 >= 150 && c1 > 30000);
        alone(CW_NOISE, 0, 0, CC_1BIT, 60, 1, o);
        {
            uint32_t l = 32767u, l0;
            int period = 0;
            l0 = l;
            do {
                l = (l >> 1) | (((l ^ (l >> 1)) & 1u) << 14);
                period++;
            } while (l != l0 && period < 70000);
            snprintf(what, sizeof what, "the shift register: 15 bits, its period %d (2^15 - 1)", period);
            check(what, period == 32767);
        }
    }
    /* the step chip: the amp envelope at 16 levels, held a frame (192 samples) at a time */
    {
        int ok = 1, levels = 0, changes = 0, since = 1000, shortest = 1000;
        float last = -1.0f;
        voice_t *v;
        reset(CW_PULSE, 96, 0, 0, CC_STEP, 0);
        par[0][TP_D] = 90, par[0][TP_S] = 0;      /* a decay of ~0.8 s to nothing */
        synth_note_on(S, 0, 60, 127, 0, 1);
        v = 0;
        for (b = 0; b < 160; b++) {
            int i;
            block();
            for (i = 0; i < NVOICE; i++)
                if (S->v[i].engine == EG_SEQUIN && S->v[i].amp.stage != ENV_OFF)
                    v = &S->v[i];
            if (!v)
                break;
            {
                float q = v->o[8] * 15.0f;
                ok &= fabsf(q - (float)(int)(q + 0.5f)) < 1e-4f;
                if (v->o[8] != last) {
                    levels++, changes++;
                    if (since < shortest && changes > 2)
                        shortest = since;
                    since = 0;
                    last = v->o[8];
                }
                since += N;
            }
        }
        snprintf(what, sizeof what, "step: the envelope at 16 levels (%d seen), each held over a frame", levels);
        check(what, ok && levels >= 8 && levels <= 16 && shortest >= 192);
    }
    /* the knobs' texts and the machine's picture */
    {
        uint8_t p[NTP];
        float y[32];
        int ok;
        sound_default(p, EG_SEQUIN);
        tp_text_par(TP_E2, p[TP_E2], p, num, unit);
        ok = !strcmp(num, "1/2") && !strcmp(EG_NAME[EG_SEQUIN], "sequin");
        p[TP_E1] = CW_WAVE;
        tp_text_par(TP_E2, 11 * 8, p, num, unit);
        ok &= !strcmp(num, "vox a");
        p[TP_E1] = CW_PULSE, p[TP_X1] = CC_STEP;
        tp_text_par(TP_E2, 127, p, num, unit);
        ok &= !strcmp(num, "3/4");
        tp_text_par(TP_E1, CW_WAVE, p, num, unit);
        ok &= !strcmp(num, "table") && !strcmp(tp_info(TP_X1, EG_SEQUIN)->name, "chip") &&
              !strcmp(tp_info(TP_X4, EG_SEQUIN)->name, "tone") && tp_info(TP_X4, EG_SEQUIN)->def == 127;
        check("texts: duty 1/2, the table vox a, step's 3/4, wave table; the Sub page chip .. tone", ok);
        p[TP_X1] = CC_4BIT, p[TP_E2] = 32, p[TP_E3] = 0;  /* a quarter pulse: 8 steps high, 24 low */
        chip_picture(p, y);
        ok = 1;
        for (k = 0; k < 32; k++)
            ok &= y[k] == (k < 8 ? 1.0f : -1.0f);
        p[TP_E1] = CW_SAW, p[TP_E3] = 24;                 /* a saw held 4 steps: 8 values */
        chip_picture(p, y);
        ok &= distinct(y, 32) == 8 && longest_run(y, 32) == 4;
        check("the picture: a quarter pulse, a saw held four steps", ok);
    }
    /* levels: each wave's held note near knit's (the loudest 100 ms, RMS) */
    {
        double lo = 1e9, hi = 0.0, kn;
        reset(CW_PULSE, 96, 0, 0, CC_4BIT, 0);
        sound_default(par[0], EG_KNIT);
        par[0][TP_FTYPE] = FT_LP12, par[0][TP_CUT] = 127, par[0][TP_S] = 127, par[0][TP_VOLUME] = 127;
        par[0][TP_LEVEL] = 108;
        held(57, 40, buf);
        kn = rms(buf + 10 * N, 30 * N);
        for (w = 0; w < NCW; w++) {
            double r;
            reset(w, w == CW_WAVE ? 3 * 8 : 96, 0, 0, CC_4BIT, 0);
            held(57, 40, buf);
            r = rms(buf + 10 * N, 30 * N);
            printf("    %-6s %6.1f dBFS RMS (knit %.1f)\n", CH_WAVE_N[w], 20.0 * log10(r), 20.0 * log10(kn));
            lo = fmin(lo, r), hi = fmax(hi, r);
        }
        snprintf(what, sizeof what, "the waves within 9 dB of knit's saw (%.1f .. %.1f dB from it)", 20.0 * log10(lo / kn),
                 20.0 * log10(hi / kn));
        check(what, 20.0 * log10(hi / kn) < 9.0 && 20.0 * log10(lo / kn) > -9.0);
    }
    /* its presets: together, its first named sequin, the roles a chip voice is for */
    {
        int first = preset_first(EG_SEQUIN), cnt = 0, roles = 0;
        for (k = 0; k < NPRESETS; k++)
            if (PRESETS[k].par[TP_ENGINE] == EG_SEQUIN)
                cnt++, roles |= 1 << PRESETS[k].role;
        snprintf(what, sizeof what, "%d presets, the first \"%s\": leads, basses, a chord, a pluck, an effect, drums", cnt,
                 first >= 0 ? PRESETS[first].name : "-");
        check(what, first >= 0 && !strcmp(PRESETS[first].name, "sequin") && cnt >= 8 &&
                    (roles & (1 << RL_LEAD | 1 << RL_BASS | 1 << RL_CHORD | 1 << RL_PLUCK | 1 << RL_FX | 1 << RL_DRUM)) ==
                        (1 << RL_LEAD | 1 << RL_BASS | 1 << RL_CHORD | 1 << RL_PLUCK | 1 << RL_FX | 1 << RL_DRUM));
    }
    /* the cost against knit's: the presets' mean, a chord of four, the whole voice path */
    {
        double ck = cost(EG_KNIT), cs = cost(EG_SEQUIN);
        snprintf(what, sizeof what, "the cost: %.1f instructions a voice-sample, knit's %.1f (its presets' means)", cs, ck);
        check(what, cs > 0.0 && cs <= ck);
    }
    printf(bad ? "\n%d FAILED\n" : "\nall ok\n", bad);
    return bad != 0;
}
