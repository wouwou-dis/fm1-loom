/* SPDX-License-Identifier: MPL-2.0 */
/* Renders for listening (nothing here plays audio): every reverb mode (the plate, the PlayStation's SPU modes) on a short dry phrase, as
 * 16-bit stereo WAVs in build/fx_renders/, with an index (index.txt) of what each file is.
 *   tools/fx_renders.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fx.h"
#include "fastmath.h"

#define SR 44100
#define BLK 64
#define LEN (6 * SR)
#define DIR "build/fx_renders/"

static int16_t dly_l[FX_DLY_LEN], dly_r[FX_DLY_LEN], rv[FX_RV_LEN] __attribute__((aligned(4)));
static uint8_t fxb[FXSLOT_BYTES] __attribute__((aligned(8)));
static fxslot_t *fx = (fxslot_t *)fxb;
static float src_l[LEN], src_r[LEN], out_l[LEN], out_r[LEN];
static FILE *idx;

static void wav(const char *name, const float *l, const float *r, int n, const char *what)
{
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
        0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    char path[160];
    FILE *f;
    uint32_t v;
    int i;
    snprintf(path, sizeof path, DIR "%s", name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(hdr, 1, 44, f);
    for (i = 0; i < n; i++) {
        int16_t s[2];
        s[0] = (int16_t)fm_clampf(l[i] * 32767.0f, -32767.0f, 32767.0f);
        s[1] = (int16_t)fm_clampf(r[i] * 32767.0f, -32767.0f, 32767.0f);
        fwrite(s, 2, 2, f);
    }
    v = (uint32_t)n * 4u + 36u;
    fseek(f, 4, SEEK_SET);
    fwrite(&v, 4, 1, f);
    v = (uint32_t)n * 4u;
    fseek(f, 40, SEEK_SET);
    fwrite(&v, 4, 1, f);
    fclose(f);
    fprintf(idx, "%-34s %s\n", name, what);
}

/* a dry phrase: eight plucked notes (a saw, a decaying low-pass) in 1.5 s, then silence for the tail (mono) */
static void phrase(void)
{
    static const float F[8] = {261.63f, 329.63f, 392.0f, 523.25f, 493.88f, 392.0f, 329.63f, 293.66f};
    float ph = 0.0f, lp = 0.0f;
    int i;
    for (i = 0; i < LEN; i++) {
        int k = i / (SR * 3 / 16);
        float t = (float)(i % (SR * 3 / 16)) / SR, s = 0.0f;
        if (k < 8) {
            ph += F[k] / SR;
            ph -= fm_floorf(ph);
            lp += (0.05f + 0.4f * fm_expf(-t * 20.0f)) * ((2.0f * ph - 1.0f) - lp);
            s = 0.4f * lp * fm_expf(-t * 7.0f);
        }
        src_l[i] = src_r[i] = s;
    }
}


/* the phrase dry, the reverb's return under it (the send at full, the return at 0.7: an FX track at level 84) */
static void reverb_render(const char *name, int pick, const uint8_t kn[4], const char *what)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int b, i;
    fx_init(fx, dly_l, dly_r, rv);
    fx_tempo(fx, 120.0f);
    fx_set(fx, pick, kn);
    fx_filter(fx, 20, 127);                                     /* the default project's FX II filter */
    for (b = 0; b < 40; b++) {                                  /* an SPU mode clears its area first */
        memset(in, 0, sizeof in);
        fx_process(fx, in, ol, orr, BLK);
    }
    for (b = 0; b < LEN / BLK; b++) {
        for (i = 0; i < BLK; i++)
            in[i] = src_l[b * BLK + i];
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            out_l[b * BLK + i] = in[i] + 0.7f * ol[i];
            out_r[b * BLK + i] = in[i] + 0.7f * orr[i];
        }
    }
    wav(name, out_l, out_r, LEN, what);
}

/* ---- the send effects A / B: the same dry material through the plate, lush, the chorus and the delay, for comparing
 * one version of dsp/fx.c with another (build/fx_renders/ab_*.wav; tools/fx_renders.sh) ---- */
#define AB_LEN (8 * SR)
static float ab_l[AB_LEN], ab_r[AB_LEN], ab_x[AB_LEN];
static uint32_t ab_rng = 1;
static float ab_noise(void)
{
    ab_rng = ab_rng * 1664525u + 1013904223u;
    return (float)(int32_t)ab_rng * (1.0f / 2147483648.0f);
}
/* two bars of drums at 120 BPM (kick, snare, closed hats on the eighths), then four seconds for the tails */
static void ab_drums(void)
{
    int i, s;
    for (i = 0; i < AB_LEN; i++)
        ab_x[i] = 0.0f;
    ab_rng = 7;
    for (s = 0; s < 16; s++) {                                  /* eighths: 0.25 s */
        int t0 = s * SR / 4, k;
        float hp = 0.0f, ph = 0.0f, lp = 0.0f;
        for (k = 0; k < SR / 2 && t0 + k < AB_LEN; k++) {
            float t = (float)k / SR, n = ab_noise(), v = 0.0f, hn;
            hn = n - hp;                                        /* a crude high-pass for the hats */
            hp += 0.3f * (n - hp);
            v += 0.12f * hn * fm_expf(-t * 60.0f);              /* hat */
            if (s % 4 == 0) {                                   /* kick: a falling sine */
                ph += (45.0f + 120.0f * fm_expf(-t * 30.0f)) / SR;
                v += 0.7f * fm_sin_turns(ph) * fm_expf(-t * 9.0f);
            }
            if (s % 4 == 2) {                                   /* snare: a tone and noise */
                lp += 0.5f * (n - lp);
                v += 0.25f * fm_sin_turns(185.0f * t) * fm_expf(-t * 25.0f) + 0.35f * lp * fm_expf(-t * 18.0f);
            }
            ab_x[t0 + k] += v;
        }
    }
}
/* a held chord (C E G B, two saws a note a little apart, a low-pass), 2.5 s, then the tails */
static void ab_chord(void)
{
    static const float F[4] = {130.81f, 164.81f, 196.0f, 246.94f};
    float ph[8] = {0}, z1 = 0.0f, z2 = 0.0f;
    int i, k;
    for (i = 0; i < AB_LEN; i++) {
        float t = (float)i / SR, s = 0.0f, env = t < 0.05f ? t / 0.05f : t < 2.5f ? 1.0f : fm_expf(-(t - 2.5f) * 15.0f);
        for (k = 0; k < 8; k++) {
            ph[k] += F[k >> 1] * (k & 1 ? 1.004f : 0.996f) / SR;
            ph[k] -= fm_floorf(ph[k]);
            s += 2.0f * ph[k] - 1.0f;
        }
        z1 += 0.25f * (s - z1);
        z2 += 0.25f * (z1 - z2);
        ab_x[i] = 0.06f * z2 * env;
    }
}
/* 50 ms of noise at 0.5, then silence: the wet alone (for the tail's spectrum and its decay) */
static void ab_burst(void)
{
    int i;
    ab_rng = 99;
    for (i = 0; i < AB_LEN; i++)
        ab_x[i] = i < SR / 20 ? 0.5f * ab_noise() : 0.0f;
}
static void ab_run(const char *name, int pick, const uint8_t kn[4], float dry, const char *what)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int b, i;
    fx_init(fx, dly_l, dly_r, rv);
    fx_tempo(fx, 120.0f);
    fx_set(fx, pick, kn);
    for (b = 0; b < 40; b++) {                                  /* (lush and the SPU modes clear their line first) */
        memset(in, 0, sizeof in);
        fx_process(fx, in, ol, orr, BLK);
    }
    for (b = 0; b < AB_LEN / BLK; b++) {
        for (i = 0; i < BLK; i++)
            in[i] = ab_x[b * BLK + i];
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            ab_l[b * BLK + i] = dry * in[i] + 0.7f * ol[i];
            ab_r[b * BLK + i] = dry * in[i] + 0.7f * orr[i];
        }
    }
    wav(name, ab_l, ab_r, AB_LEN, what);
}
static void ab_renders(void)
{
    static const uint8_t ch_fast[4] = {100, 60, 20, 127}, pl_small[4] = {30, 40, 44, 72}, pl_big[4] = {127, 90, 64, 40};
    const uint8_t *pl = FXT_DEFAULT[FXT_REVERB], *ch = FXT_DEFAULT[FXT_CHORUS], *dl = FXT_DEFAULT[FXT_DELAY];
    const int P = FXT_REVERB + RV_PLATE, LU = FXT_REVERB + RV_LUSH;
    fprintf(idx, "\nA / B material: the same dry input through the send effects (dry + 0.7 x the return; *_burst: the\n"
                 "return alone after 50 ms of noise), 8 s each:\n");
    ab_drums();
    wav("ab_dry_drums.wav", ab_x, ab_x, AB_LEN, "two bars of drums at 120 BPM, dry");
    ab_run("ab_plate_drums.wav", P, pl, 1.0f, "plate (defaults 100 40 44 72) on the drums");
    ab_run("ab_plate_small_drums.wav", P, pl_small, 1.0f, "plate, size 30 (a small room) on the drums");
    ab_run("ab_lush_drums.wav", LU, RV_LUSH_DEFAULT, 1.0f, "lush (defaults 64 64 56 48) on the drums");
    ab_run("ab_delay_drums.wav", FXT_DELAY, dl, 1.0f, "delay (defaults: 1/8 ping-pong) on the drums");
    ab_run("ab_chorus_drums.wav", FXT_CHORUS, ch, 1.0f, "chorus (defaults) on the drums");
    ab_chord();
    wav("ab_dry_chord.wav", ab_x, ab_x, AB_LEN, "a held chord, dry");
    ab_run("ab_plate_chord.wav", P, pl, 1.0f, "plate (defaults) on the chord");
    ab_run("ab_plate_big_chord.wav", P, pl_big, 1.0f, "plate, size 127, motion 90, tone 64, damp 40 on the chord");
    ab_run("ab_lush_chord.wav", LU, RV_LUSH_DEFAULT, 1.0f, "lush (defaults) on the chord");
    ab_run("ab_chorus_chord.wav", FXT_CHORUS, ch, 1.0f, "chorus (defaults: 0.33 Hz) on the chord");
    ab_run("ab_chorus_fast_chord.wav", FXT_CHORUS, ch_fast, 1.0f, "chorus, rate 100 (~1.4 Hz), depth 60 on the chord");
    ab_run("ab_delay_chord.wav", FXT_DELAY, dl, 1.0f, "delay (defaults) on the chord");
    ab_burst();
    ab_run("ab_plate_burst.wav", P, pl, 0.0f, "plate (defaults): the return alone, 50 ms of noise in");
    ab_run("ab_plate_big_burst.wav", P, pl_big, 0.0f, "plate, size 127 ...: the return alone");
    ab_run("ab_lush_burst.wav", LU, RV_LUSH_DEFAULT, 0.0f, "lush (defaults): the return alone");
    ab_run("ab_delay_burst.wav", FXT_DELAY, dl, 0.0f, "delay (defaults): the return alone");
}

int main(void)
{
    char name[64], what[160];
    int i, m;
    idx = fopen(DIR "index.txt", "w");
    if (!idx) {
        fprintf(stderr, "fx_render: no " DIR "\n");
        return 1;
    }
    fprintf(idx, "LOOM-8 effect renders (tests/host/fx_render.c, tools/fx_renders.sh): 44.1 kHz, 16-bit stereo, 6 s each.\n");
    fprintf(idx, "\nThe reverb's modes on FX II, on a dry phrase (8 plucked notes in 1.5 s, then the tail): the dry, and the\n"
                 "return at 0.7 under it. The SPU modes at their defaults (size 127 = the console's delays, decay 64 = its own\n"
                 "feedback, tone flat, damp 0):\n");
    phrase();
    wav("reverb_dry.wav", src_l, src_r, LEN, "the phrase alone");
    for (m = 0; m < NRV; m++) {
        int pick = FXT_REVERB + m;
        if (fx_pick_retired(pick))                /* (studio S and L: retired) */
            continue;
        snprintf(name, sizeof name, "reverb_%02d_%s.wav", m, fx_pick_name(pick));
        for (i = 0; name[i]; i++)
            if (name[i] == ' ')
                name[i] = '_';
        snprintf(what, sizeof what, "%s%s", fx_pick_name(pick), m ? "" : " (the plate, its defaults 100 40 44 72)");
        reverb_render(name, pick, m ? RV_SPU_DEFAULT : FXT_DEFAULT[FXT_REVERB], what);
    }
    {
        static const uint8_t k0[4] = {0, 64, 64, 0}, kd[4] = {127, 110, 64, 0}, kdark[4] = {127, 64, 30, 70};
        reverb_render("reverb_hall_size0.wav", FXT_REVERB + RV_HALL, k0, "hall at size 0: schwung-psxverb's scale (x1)");
        reverb_render("reverb_hall_decay110.wav", FXT_REVERB + RV_HALL, kd, "hall, decay 110: longer");
        reverb_render("reverb_space_echo_dark.wav", FXT_REVERB + RV_SPACE_ECHO, kdark, "space echo, tone 30, damp 70: darker");
    }
    ab_renders();
    fclose(idx);
    printf("renders in " DIR " (index.txt)\n");
    return 0;
}
