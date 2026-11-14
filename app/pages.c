/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's pages: the state the input layer (ui.c) and the screen share, and the drawing. Part of the unity
 * build, after project.c and before ui.c; the host simulator includes it too. Polly's framework: a header
 * (the page, the tempo, REC, unsaved; messages and questions in an alert box), the main band (the page's
 * picture), the tag (where you are: track > pattern > scene, the page dots), the four knobs' strip (with
 * the track's ink under the values), the encoders' line and the footer (what the keys do here).
 * Each band is drawn only when what it shows changed (a hash of it). Over the main band: a help card (a
 * button held alone), a list (SEL). The look: below. */

/* The look, one only: paper, drawn as the figures of a printed service manual (ink line drawings on paper; the
 * approved sheet "Service manual"). Each instrument track has its own ink, so the steps, the arrange rows and the mix
 * say whose they are at a glance. On a track's pages its ink is K_NOW (the picture's line, the background's colour as
 * text on it) and K_TINT (its ink thinned over the paper: the steps with notes). Vermilion (K_REC) means "playing
 * now", and recording; blue (K_SEL) or the track's ink marks what is selected. (proj.rsv[1] once held a theme and
 * large type: ignored since, never read.) */
typedef struct {
    uint16_t bg, panel, panel2, line, faint, dim, text, now_m, rec, aux;
    uint16_t ink[NTRK];
    uint8_t tint, mid;                                /* the ink's share over the background, of 16 */
    uint16_t sel;                                     /* the cursor, a held step, the knob last turned (K_SEL) */
    uint16_t card, grid;                              /* a card lighter than the paper; graph paper's dots */
} theme_t;
static const theme_t PAPER = {
    RGB(236, 232, 220),                               /* paper   #ECE8DC */
    RGB(223, 218, 203),                               /* panel   #DFDACB */
    RGB(210, 204, 186),                               /* panel2: a panel a shade deeper */
    RGB(189, 184, 168),                               /* line    #BDB8A8: hairlines, empty things */
    RGB(165, 159, 143),                               /* faint: between the line and the dim text */
    RGB(133, 127, 110),                               /* dim     #857F6E: labels, the second word */
    RGB(23, 23, 23),                                  /* ink     #171717 */
    RGB(158, 152, 136),                               /* now_m */
    RGB(238, 74, 31),                                 /* vermilion #EE4A1F: playing now, recording */
    RGB(23, 23, 23),                                  /* the aux tracks' ink: the ink */
    {RGB(36, 73, 216), RGB(14, 143, 138), RGB(217, 154, 0), RGB(122, 75, 209), RGB(216, 71, 143), RGB(28, 154, 94),
     RGB(138, 90, 43), RGB(91, 107, 122)},            /* the tracks' inks (the sheet's TRK) */
    5, 9,
    RGB(36, 73, 216),                                 /* blue    #2449D8: selected */
    RGB(246, 243, 234),                               /* card    #F6F3EA */
    RGB(179, 174, 158),                               /* graph paper's dots #B3AE9E */
};
static const theme_t *const theme = &PAPER;
#define AUTOSAVE_OFF 4u                              /* proj.rsv[2]: LOOM-8 saves only when SAVE is pressed */
#define LOOP_FREE 8u                                  /* proj.rsv[2]: a loop throw let go drifts (seq.loop_free) */
#define SCENES_AT_END 2u                              /* proj.rsv[2]: a scene's key waits for T1's pattern's end (bit 0: REC's mode) */
#define CC_OUT_OFF 16u                                /* proj.rsv[2]: no MIDI CCs out (midicc.c; on by default) */
#define SCENES_OUT 32u                                 /* proj.rsv[2]: a scene change's Program Change out (midicc.c;
                                                        * off by default, old and new projects alike) */
#define CC_IN_ENDLESS 1u                               /* proj.rsv[3]: CCs in from endless encoders, each a turn
                                                        * (midicc.c cc_delta; absolute by default) */
#define BEAT_FLASH_OFF 2u                             /* proj.rsv[3]: no beat flash on Steps and Bar (on by default) */
#define K_BG theme->bg
#define K_PANEL theme->panel
#define K_PANEL2 theme->panel2
#define K_LINE theme->line
#define K_DIM theme->dim
#define K_FAINT theme->faint
#define K_TEXT theme->text
#define K_NOW_M theme->now_m
#define K_REC theme->rec
#define K_SEL theme->sel                                  /* the cursor, a held step, the knob last turned */
#define K_CARD theme->card
#define K_GRID theme->grid
#define K_NOW k_now
#define K_TINT k_tint
#define K_NOW_T k_mid                                 /* the playhead on an empty step: the ink half thinned */
static uint16_t k_now, k_tint, k_mid;
static uint16_t trk_ink(int t) { return t >= 0 && t < NTRK ? theme->ink[t] : theme->aux; }
static uint16_t ink_mix(uint16_t ink, uint32_t a)    /* the ink at a/16 over the background */
{
    uint32_t pr = K_BG >> 11, pg = K_BG >> 5 & 63u, pb = K_BG & 31u;
    uint32_t r = ink >> 11, g = ink >> 5 & 63u, b = ink & 31u;
    return (uint16_t)(((r * a + pr * (16u - a)) / 16u) << 11 | ((g * a + pg * (16u - a)) / 16u) << 5 |
                      (b * a + pb * (16u - a)) / 16u);
}
static uint16_t trk_tint(int t) { return ink_mix(trk_ink(t), theme->tint); }
static uint16_t col_mix(uint16_t a, uint16_t b, uint32_t s)   /* a moved s/16 of the way to b */
{
    uint32_t ar = a >> 11, ag = a >> 5 & 63u, ab = a & 31u, br = b >> 11, bg = b >> 5 & 63u, bb = b & 31u;
    return (uint16_t)(((ar * (16u - s) + br * s) / 16u) << 11 | ((ag * (16u - s) + bg * s) / 16u) << 5 |
                      (ab * (16u - s) + bb * s) / 16u);
}

/* The beat flash (the owner's one animation: the sequencer's own card on Steps and the bar boxes on Bar, while playing):
 * the card is seen through a coral filter on every beat, the bar's downbeat stronger, and falls back to plain in three
 * steps. Every band is drawn as always; only the main band is drawn again for a new level, and loom_blit multiplies the
 * pixels of the card's (or the boxes') rectangle by what the paper would become (the card and its beat shading turn the
 * tint, the ink stays dark, the vermilion and the blue stay themselves), so no drawing code knows of it and the header,
 * the knobs, the hints and the margin are never touched. The beat comes from the sequencer's sixteenth counter
 * (seq.step_tick, bar16: what the metronome follows, so an external clock, groove, tempo changes and a pattern's step
 * scale are all right); the decay is in time. The colour and strengths are these constants, in 64ths of the way from the
 * paper to the coral. Above FLASH_MAX_BPM only every second beat flashes (at most three a second). */
#define FLASH_CORAL RGB(255, 111, 82)                /* coral #FF6F52: lighter and pinker than the vermilion */
#define FLASH_BEAT 19                                 /* 64ths of coral over the paper at a beat's peak: 30 % */
#define FLASH_DOWN 32                                 /* ... at the bar's downbeat: 50 % */
#define FLASH_STEP_SHIFT 6                            /* each step of the decay is 64 ms (three: 192 ms) */
#define FLASH_MAX_BPM 180
static const uint8_t FLASH_DECAY[3] = {8, 5, 2};      /* 8ths of the peak by step, then the plain paper */
static uint32_t fl_t0;                                /* when the pulse began (ms) */
static uint8_t fl_six, fl_pk, fl_pct;                 /* the sixteenth counter last seen, the pulse's peak, the level now */
#ifdef PLAT_HOST
static uint32_t fl_count;                             /* (the simulator: pulses begun, expect flashes) */
#endif
static int flash_area(void);                          /* 0 none, 1 Steps' card, 2 Bar's boxes */
static void loom_blit(uint32_t x, uint32_t y)         /* cv_blit, the flash's rectangle through its filter (pages.c's blits are this) */
{
    int area = fl_pct && y == 28u /* MAIN_Y */ ? flash_area() : 0;
    if (area) {
        uint32_t p = K_BG, c = FLASH_CORAL, q = fl_pct, pr = p >> 11, pg = p >> 5 & 63u, pb = p & 31u;
        uint32_t fr = (((pr * (64u - q) + (c >> 11) * q) >> 6 << 8) + pr - 1u) / pr,   /* paper channel x f = the tint */
                 fg = (((pg * (64u - q) + (c >> 5 & 63u) * q) >> 6 << 8) + pg - 1u) / pg,
                 fb = (((pb * (64u - q) + (c & 31u) * q) >> 6 << 8) + pb - 1u) / pb;
        for (uint32_t r = 0; r < 4u; r++) {
            /* a rectangle x0 .. x1, y0 .. y1 (inside the ink edge; the card's corners cut round) */
            uint32_t x0 = area == 1 ? 9u : 9u + 57u * r, x1 = area == 1 ? 231u : 61u + 57u * r, y0 = area == 1 ? 20u : 19u,
                     y1 = area == 1 ? 118u : 56u;
            if (area == 1 && r)
                break;
            for (uint32_t yy = y0; yy < y1; yy++)
                for (uint32_t xx = x0; xx < x1; xx++) {
                    uint32_t dx = xx - x0 < x1 - 1u - xx ? xx - x0 : x1 - 1u - xx, dy = yy - y0 < y1 - 1u - yy ? yy - y0 : y1 - 1u - yy;
                    uint16_t *s = &cv_px[yy * 240u + xx];
                    uint32_t v = swap16(*s), rr = (v >> 11) * fr >> 8, g = (v >> 5 & 63u) * fg >> 8, b = (v & 31u) * fb >> 8;
                    if (area == 1 && dx + dy < 3u)
                        continue;
                    *s = swap16((rr > 31u ? 31u : rr) << 11 | (g > 63u ? 63u : g) << 5 | (b > 31u ? 31u : b));
                }
        }
    }
    cv_blit(x, y);
}
#define cv_blit(x, y) loom_blit(x, y)

/* the pages (the spec's Buttons table): a button opens its first page; SELECT turns them. EDIT: the engine
 * (a drum track: the shown key's knobs) and Play (a drum track: the key's model, random, pan, volume); ENV: the
 * envelope, the filter, the filter envelope (a drum track: the shown key's envelope and filter, then the kit's
 * envelope and the kit's bus filter: four pages); FX: the instrument's character stage (a kit's on the whole
 * kit, after its bus filter), its sends and the two effects; ARP three with the arp (its page 2, then hold). EDIT on a synth
 * track: Engine, Play, Play 2 (the octave), then Sub where the engine has a second page of knobs (TP_X1..X4: knit's
 * oscillator 2, level, sub, noise; rayon's fall); a kit or an FX track Engine and Play; the conductor Engine, Play (routing), Sub (Follow) */
enum { P_ARRANGE, P_SONG, P_STEPS, P_BAR, P_ENGINE, P_PLAY, P_PLAY2, P_SUB, P_ENV, P_FILTER, P_ENV2, P_KITF, P_CHARACTER, P_SENDS,
       P_FX1, P_FX2, P_THROWS, P_LFO, P_LFO2, P_PLAYERS, P_PLAYERS2, P_PLAYERS3, P_WEAVE, P_MIX, P_MASTER, P_MASTER2, P_MASTER3, P_GLFO, P_SETUP,
       P_STATUS, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Arrange", "Song", "Steps", "Bar", "Engine", "Play", "Play 2", "Sub", "Envelope",
                                              "Filter", "Filter env", "Kit filter", "Character", "Sends", "FX I",
                                              "FX II", "Throws", "LFO", "LFO 2", "Players", "Players 2", "Hold", "Weave", "Mix",
                                              "Master EQ", "Master color", "Master out", "LFO", "Setup", "Debug"};
static char *s_cat(char *b, const char *s);
static char *num_s(uint32_t v, char *b);
static const char *const GLFO_SHAPE[NGS + 1] = {"off", "sine", "triangle", "saw up", "saw down", "square", "random"};
static const char *const GLFO_DEST[NGD] = {"cutoff", "reso", "pitch", "pan", "volume", "FX I", "FX II", "character"};
static void glfo_rate_text(int v, char *t)       /* synth.c glfo_hz: 16 bars .. 1/16 synced, then 0.02 .. 10 Hz */
{
    static const char *const SYNC[GL_SYNC_N] = {"16 bars", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8",
                                               "1/16"};
    if (v < GL_SYNC_N) {
        s_cat(t, SYNC[v]);
        return;
    }
    {
        uint32_t c = (uint32_t)(0.02f * fm_exp2f((float)(v - GL_SYNC_N) * (8.96578428f / 118.0f)) * 100.0f + 0.5f);
        char *e = num_s(c / 100u, t);
        *e++ = '.';
        *e++ = (char)('0' + c / 10u % 10u);
        *e++ = (char)('0' + c % 10u);
        s_cat(e, " Hz");
    }
}
static uint32_t glfo_route(void)                 /* the tracks it moves: bit t */
{
    const uint8_t *g = proj.trk[AUX_BRAIN].par;
    return (uint32_t)(g[TP_LX] & 15u) | (uint32_t)(g[TP_LX2] & 15u) << 4;
}
static int mst_base(int p) { return p == P_MASTER ? MS_EQ_LO : p == P_MASTER2 ? MS_CTYPE : MS_PERC; }   /* its first knob */
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_ARRANGE, 2}, {B_SEQ, P_STEPS, 2}, {B_EDIT, P_ENGINE, 4}, {B_ENV, P_ENV, 4}, {B_FX, P_CHARACTER, 5},
    {B_LFO, P_LFO, 2}, {B_ARP, P_PLAYERS, 4}, {B_GLO, P_MIX, 7}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
static int page_group(int p)
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (p >= PAGE_BTN[i].first && p < PAGE_BTN[i].first + PAGE_BTN[i].n)
            return (int)i;
    return 0;
}
static int group_n(int g);                           /* the pages a button has now (the track decides) */
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};

/* the tracks: 1-8, then the aux tracks (the tag's short names, the pages' long ones) */
static const char *const TRACK_NAME[NALLTRK] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "COND", "THROW",
                                                "FX I", "FX II", "WEAVE"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
/* the step components (SC_*: the OP-XY's 14, on the white keys with HOME and a step held), their letter on
 * the grid and their name */
static const char *const COMP_NAME[NSC] = {"none", "pulse", "hold", "multiply", "velocity", "ramp up", "ramp down",
                                           "random", "glide", "bend", "tonality", "jump", "skip lock",
                                           "skip trick", "skip trig", "flam"};
static const char *const COMP_GLYPH[NSC] = {"", "P", "H", "M", "V", "^", "v", "?", "~", "B", "T", "J", "L", "C", "S",
                                           "F"};
static const char *const VEL_VAL[10] = {"4", "8", "16", "32", "64", "100", "112", "127", "0", "random"};
static const char *const RAMP_VAL[10] = {"2 in 1", "3 in 1", "4 in 1", "5 in 1", "6 in 1", "2 in 3", "3 in 3", "4 in 3",
                                         "5 in 3", "random"};   /* steps over one / three octaves */
static const char *const PORTA_VAL[10] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "off", "random"};
static const char *const BEND_VAL[10] = {"down-up", "up-down", "bump dn", "bump up", "spring out", "spring in",
                                         "fade dn", "fade up", "random 1", "random 2"};
static const char *const TON_VAL[10] = {"ignore", "transpose", "oct up", "5th up", "3rd up", "chrom up", "chrom dn",
                                        "q 33%", "q 66%", "q 100%"};
static const char *const JUMP_MOVE[6] = {"forward", "back", "fwd/back", "stay", "align", "random"};   /* 4..9 */
static const char *const TS_NAME[NTS] = {"1/2", "1", "2", "3", "4", "5", "6", "8", "16"};
static const uint8_t BAR_SCALE_KEY[9] = {TS_1, TS_2, TS_3, TS_4, TS_5, TS_6, TS_8, TS_16, TS_HALF};   /* Bar's black keys */
static const char *const LIGHTS_NAME[3] = {"off", "on", "keys"};
/* Setup's CLOCK: the knob's order (internal, receive, send) and the stored SEQ_CLK_* (0, 1 were off, on) */
static const char *const CLOCK_NAME[SEQ_NCLK] = {"internal", "receive", "send"};
static const uint8_t CLOCK_POS[SEQ_NCLK] = {0, 2, 1};                  /* SEQ_CLK_* -> the knob's position */
static const uint8_t CLOCK_AT[SEQ_NCLK] = {SEQ_CLK_INTERNAL, SEQ_CLK_RECEIVE, SEQ_CLK_SEND};
static const char *const CLOCK_ROW[SEQ_NCLK] = {"internal", "sends", "receives"};
static const char *const METRO_NAME[3] = {"off", "rec", "on"};
static const char *const ONOFF[2] = {"off", "on"};
static const char *const LINK_NAME[9] = {"off", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const FOLLOW_NAME[NBM] = {"no", "diatonic", "transpose", "harmony", "ch. deg"};
static const char *const CORE2_NAME[4] = {"one core", "two cores", "lost", "barred"};
static const char *const USBA_NAME[2] = {"follows knob", "full"};
static const char *const BRAIN_KNOB[3] = {"key", "scale", "link"};

/* the keys: 0 = F3; the white and the black ones in order */
static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define KEY_BASE 53                                  /* F3 at octave 0 */
enum { KM_NONE, KM_PLAY, KM_STEP, KM_SCENE, KM_MUTE, KM_PUNCH, KM_BRAIN, KM_SOUND,
       KM_TRACK, KM_SAVE, KM_SOLO, KM_SCALE, KM_ROUTE, KM_STEPSOUND, KM_IDLE, KM_WEAVE, KM_BARK,
       KM_STEPCHORD };   /* KM_IDLE: does nothing */   /* what a held key does */

/* the input's and the screen's state */
static struct {
    uint32_t btn, keys, btn_used, btn_t[NB];
    uint32_t enc_t[NE];
    uint32_t act_t;
    char msg[2][32];                             /* (the second part up to 31: "no weave set: its ARP, Weave") */
    uint32_t msg_until;
    uint8_t msg_kind, msg_no;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint8_t page;
    uint8_t any_btn;
    uint8_t card;                                    /* a help card on screen: its button + 1 */
    uint8_t ask;                                     /* a question on screen (SEL yes, HOME no) */
    char ask_q[48];
    void (*ask_yes)(void);
    void (*ask_no)(void);
    uint8_t ask_arg;
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t bar;                                     /* Steps: the bar shown 0..3 */
    uint8_t dsound;                                  /* a drum track: the key shown 0..10 */
    int8_t oct;                                      /* the keys' octave -4..4 */
    uint8_t rem[SN], nrem;                           /* the remembered notes (the last keys held together) */
    uint8_t rem_fx;                                  /* PUNCH: the remembered effect + 1 */
    uint8_t chord[8], nchord;                        /* the keys held together now (notes) */
    uint16_t held;                                   /* Steps: the white keys down as steps (bit = cell) */
    uint8_t held_first;                              /* the first held step (the one edited) */
    uint8_t held_used;                               /* a held step was edited: its release toggles nothing */
    uint8_t held_multi;                              /* more than one step has been held since the first: no release
                                                      * toggles a step */
    uint8_t len_key;                                 /* a later step held with the first alone, + 1: let go with
                                                      * nothing edited, the first's length to it (0 none) */
    uint8_t trickpg;                                 /* a step held + ARP: its tricks on the knobs */
    uint8_t chordmode;                               /* a step held, SEQ tapped: the keys write its chord, and stay
                                                      * doing so even if the step is let go (chord_held is who they
                                                      * write to). SEQ tapped again turns it off */
    uint16_t chord_held;                             /* chord mode: the steps it writes to (ui.held's snapshot when
                                                      * it turned on), in chord_bar of pattern chord_pat: a scene
                                                      * change while it is on moves neither */
    uint8_t chord_bar, chord_pat;
    uint8_t chord_pool;                              /* chord mode on a random track: the keys pick the pattern's
                                                      * pool (pattern.h LANE_POOL), not a step's chord */
    uint8_t chord_down;                              /* chord mode: the chord's keys still down (0: the next key
                                                      * starts a fresh chord, replacing the held steps' notes) */
    uint16_t wv_held, wv_latched;                    /* the WEAVE track's keys: the tracks woven now (seq.wv_live) */
    uint16_t wv_last;                                /* the tracks last woven by a key: what a tapped WEAVE step takes */
    uint8_t wv_step;                                 /* WEAVE's Steps: the step held (0xFF none) */
    uint8_t lockpg;                                  /* a step held + EDIT / ENV / FX / LFO: the lock editor's page + 1 */
    uint8_t cursor;                                  /* step record: the step the keys write */
    uint8_t rec_armed;                               /* REC tapped while stopped: step record */
    uint8_t srec_down;                               /* step record: the keys of the chord being written still down */
    uint8_t rec_held, rec_latched;                   /* REC down while playing (momentary); REC + PLAY latched it */
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note / scene / fx */
    uint32_t key_t[NKEYS];
    uint32_t play_t;
    uint32_t punch_latched;
    uint8_t last_inst;                               /* the instrument track selected last (the FX pages audition it) */
    uint16_t solo;                                   /* the black keys held on Mix: the tracks soloed (bit t) */
    uint16_t solo_mute;                              /* the mutes before the solo */
    uint16_t solo_lat;                               /* the tracks a double tap soloed: they stay soloed, let go */
    uint8_t solo_tap_trk, solo_tap_mut;              /* Mix: the last tap's track, whether it muted, and when it was
                                                      * let go (a double tap) */
    uint32_t solo_tap_t;
    uint16_t preset_i, pick_preset;               /* (more than 255 presets and user slots) */
    uint8_t pick_engine, pick_slot, pick_fx, pick_weave, pick_save;   /* (pick_save: SAVE TO's slot) */
    uint8_t pick_kit, pick_kslot;                    /* a kit track's KIT (0..3 factory, 4.. user) and SAVE KIT */
    uint8_t pick_bt;                                 /* Bluetooth audio's FORGET: the remembered device */
    uint8_t pick_clear, pick_paste, pick_move;       /* (pick_move: HOME's MOVE SLOT, 0 earlier, 1 later) */
    int8_t pick_shift, pick_rot;                     /* SHIFT NOTES' semitones (-24..24) and ROTATE's steps (-16 left ..
                                                      * 16 right), never 0 (0: the first's, 1) */        /* Steps' CLEAR (notes, locks, both), ROTATE (forward, back),
                                                      * PASTE (the copy, its locks only): kept from list to list */
    uint8_t name_slot;
    uint8_t playing;
    uint8_t scene_seen;
    uint8_t scene_pg;                                /* Arrange, Song: the white keys are scenes 1-16 (0) or 17-32 (1) */
    uint8_t scene_from, scene_made;                  /* a scene key's press: the scene playing before it (0xFF none),
                                                      * and whether the press made the scene (a copy) */
    uint32_t sig[6];
} ui;

/* SEL: the screen as a list: the page's knobs, then its actions */
enum { LK_KNOB, LK_MORE, LK_ACTION, LK_PICK, LK_NOTE, LK_HEAD };   /* LK_NOTE: a line that says what to do; LK_HEAD:
                                                                    * a section's name (neither ever picked) */
enum { A_CLEAR_NOTES, A_CLEAR_LOCKS, A_CLEAR_BOTH, A_COPY_PAT, A_PASTE_PAT, A_ROT_FWD, A_ROT_BACK, A_DUP_BAR,
       A_ENGINE, A_PRESET, A_SCRAMBLE, A_SAVE_PRESET, A_INIT, A_FOLLOW, A_INSERT_SLOT, A_DELETE_SLOT, A_CLEAR_SONG,
       A_PLAY_SONG, A_LOAD_PROJ, A_NEW_PROJ, A_USB_AUDIO, A_CLEAR_SCENE, A_PANIC, A_EFFECT, A_REMOVE_NOTE,
       A_COUNT_IN, A_PLAYMODE, A_FTYPE, A_FX_HP, A_FX_LP, A_FX_SEND, A_WEAVE, A_PUMP_BY, A_PUMP_REL,
       A_PUMP_ACC, A_LEVEL, A_SCENE_AT, A_SAVE_TO, A_AUTOSAVE, A_RANDOM_SOUNDS, A_MIDI_OUT, A_LOOP_FREE, A_COPY_BAR,
       A_COPY_STEP, A_PASTE_BAR, A_PASTE_STEP, A_PASTE_LOCKS, A_DOUBLE, A_CLEAR_STEP_LOCKS, A_CC_OUT, A_SCENES_OUT,
       A_KIT, A_SAVE_KIT, A_MIDI_CC, A_USB_HOST, A_BT_MIDI, A_BT_AUDIO, A_BT_ADEV, A_BT_DEVICE, A_BT_DEV, A_BT_LOOK,
       A_BT_FORGET, A_BT_DISCONNECT, A_CLEAR, A_ROTATE, A_PASTE, A_COPY_EMPTY, A_CLEAR_BAR,
       A_COPY_SOUND, A_PASTE_SOUND, A_COPY_KEY, A_PASTE_KEY, A_COPY_SCENE, A_PASTE_SCENE, A_DUP_SLOT, A_KIT_FILTER,
       A_SHIFT, A_CC_IN, A_FX_WIDTH, A_NEW_SECTION, A_MOVE_SLOT, A_COPY_ALL, A_PASTE_ALL, A_CLEAR_ALL, A_BEAT_FLASH, NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {
    "CLEAR NOTES", "CLEAR LOCKS", "CLEAR NOTES AND LOCKS", "COPY PATTERN", "PASTE PATTERN", "ROTATE FORWARD",
    "ROTATE BACK", "DUPLICATE BAR", "ENGINE", "PRESET", "SCRAMBLE", "SAVE AS PRESET", "RESET SOUND", "FOLLOWS CONDUCTOR",
    "INSERT SLOT", "DELETE SLOT", "CLEAR SONG", "PLAY SONG", "LOAD PROJECT", "NEW PROJECT", "USB AUDIO", "CLEAR SCENE",
    "ALL NOTES OFF", "EFFECT", "REMOVE", "COUNT-IN", "PLAY MODE", "FILTER TYPE", "HIGH-PASS", "LOW-PASS",
    "TO FX II", "KEEP AS PATTERN", "PUMP BY", "RELEASE", "ACCENT", "MASTER LEVEL", "SCENES SWITCH", "SAVE TO", "AUTOSAVE",
    "RANDOMIZE PRESETS", "MIDI OUT", "LOOP THROWS", "COPY BAR", "COPY STEP", "PASTE BAR", "PASTE STEP",
    "PASTE LOCKS ONLY", "DOUBLE PATTERN", "CLEAR LOCKS", "CC OUT", "SCENES OUT", "KIT", "SAVE KIT", "KNOB CC",
    "USB PORT", "BT MIDI", "BT AUDIO", "AUDIO DEVICE", "MIDI DEVICE", "DEVICE", "LOOK AGAIN", "FORGET", "DISCONNECT",
    "CLEAR", "ROTATE", "PASTE", "COPY TO EMPTY", "CLEAR BAR", "COPY SOUND", "PASTE SOUND", "COPY DRUM", "PASTE DRUM",
    "COPY SCENE", "PASTE SCENE", "DUPLICATE SLOT", "KIT FILTER", "SHIFT NOTES", "CC IN", "PING WIDTH",
    "NEW SECTION", "MOVE SLOT", "COPY BAR, ALL TRACKS", "PASTE BAR, ALL TRACKS", "CLEAR ALL PATTERNS", "BEAT FLASH"};
static const char *const CLEAR_WHAT[3] = {"notes", "locks", "notes and locks"};
/* a pick ALGORITHM sets at once (a setting: SEL only leaves the list), as against a choice SEL then carries out */
static int pick_sets(int a)
{
    switch (a) {
    case A_FOLLOW: case A_USB_AUDIO: case A_COUNT_IN: case A_PLAYMODE: case A_FTYPE:
    case A_FX_HP: case A_FX_LP: case A_FX_SEND: case A_PUMP_BY: case A_PUMP_REL: case A_PUMP_ACC: case A_LEVEL:
    case A_SCENE_AT: case A_AUTOSAVE: case A_MIDI_OUT: case A_LOOP_FREE: case A_CC_OUT: case A_SCENES_OUT: case A_MIDI_CC:
    case A_USB_HOST: case A_KIT_FILTER: case A_CC_IN: case A_FX_WIDTH: case A_BEAT_FLASH:
        return 1;
    default:
        return 0;
    }
}
/* a MIDI track's eight rows of A_MIDI_CC: each knob's CC (ui.c list_midi_knob reads the digit back) */
static const char *const MIDI_CC_ROW[8] = {"KNOB 1 CC", "KNOB 2 CC", "KNOB 3 CC", "KNOB 4 CC", "KNOB 5 CC",
                                           "KNOB 6 CC", "KNOB 7 CC", "KNOB 8 CC"};
/* the weave's ways (pattern.h WV_*): what this pattern keeps of itself */
static const char *const WEAVE_NAME[NWV] = {"bars", "steps", "rhythm", "pitch"};
#define LIST_ROWS 40                  /* (HOME's, EDIT's: their pages' knobs, their actions, the sections' names) */
#define LIST_SHOWN 10                 /* rows of LIST_RH on the screen, about (a section's name is LIST_HH) */
#define LIST_RH 16
#define LIST_HH 15
static struct {
    uint8_t open, n, sel, top;
    uint8_t bt;                                      /* a Bluetooth list instead of the page's: 1 audio, 2 MIDI devices */
    struct { const char *name; char val[24]; uint8_t kind, id; } row[LIST_ROWS];
} lst;

/* the undo: one stage, whatever was changed last (HOME + REC swaps it back, HOME + PLAY swaps again: redo): a
 * pattern with its locks, a track's sound (or T2-T8 at once), the master, or the arrangement (song, scenes, weave) */
enum { U_PAT, U_SOUND, U_MASTER, U_ARR, U_BARS };
#define UNDO_LANES 13                     /* (a pattern's 12 locks and its random pool) */
#define UB_LANES 30                       /* PASTE BAR, ALL TRACKS' undo: the bar's lanes it keeps (fits the arrangement's room) */
static struct {
    uint8_t valid, redo, kind, trk, n, pt, nl, rsv;
    uint32_t t;
    union {
        struct { tpat_t pat; lane_t lane[UNDO_LANES]; } p;
        trk_t s[NTRK - 1];
        uint8_t master[NMS];
        /* the arrangement; ns: NEW SECTION made scene ns_to from ns_from, copying track t's pattern ns_src[t] into its
         * empty ns_made[t] (0xFF: the track shares): the undo empties those again, the redo copies them again */
        struct { song_t song; scene_t scene[NSCENE]; wvpat_t weave[NPATT];
                 uint8_t ns, ns_from, ns_to, ns_src[NSEQT], ns_made[NSEQT]; } a;
        /* PASTE BAR, ALL TRACKS: the bar on each track it pasted (n of them): its steps, the pattern's length, the bar's
         * values of each lane it touched (swapped back and forth) */
        struct { uint8_t trk[NSEQT], pt[NSEQT], len[NSEQT], bar, nl;
                 step_t st[NSEQT][16];
                 struct { uint8_t i, rsv; uint16_t param; uint8_t v[16]; } l[UB_LANES]; } b;
    } u;
} undo PLAT_POOL;
enum { CK_PAT, CK_BAR, CK_STEP };
static uint8_t clip_ok, clip_trk, clip_pat, clip_kind, clip_from;   /* COPY PATTERN / BAR / STEP: which (CK_*: the
                                                 * bar's or the step's first step), read at PASTE (no copy kept: the
                                                 * pool is short) */
static uint8_t clipall_ok, clipall_bar, clipall_pat[NSEQT];   /* COPY BAR, ALL TRACKS: the bar (0..3) and each track's
                                                 * pattern, read at PASTE BAR, ALL TRACKS */
static uint16_t clipall_kit;                    /* (which tracks were kits then: a track since changed is left out) */
static uint8_t snd_clip, key_clip, key_clip_k, scene_clip;   /* COPY SOUND (the track + 1), COPY DRUM (the kit track + 1,
                                                 * its key), COPY SCENE (the scene + 1): read at PASTE, as above */

/* ---------------------------------------------------------- helpers --- */
static char *num_s(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *num_i(int32_t v, char *b)
{
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    return num_s((uint32_t)v, b);
}
static char *s_cat(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static uint32_t hash_u(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t hash_s(uint32_t h, const char *s)
{
    while (s && *s)
        h = hash_u(h, (uint8_t)*s++);
    return h;
}
static uint32_t s_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static int s_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static int s_pre(const char *a, const char *b, uint32_t n)   /* the first n letters alike (a strncmp == 0) */
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
        else if (!a[i])
            return 1;
    return 1;
}
static char *upcase(char *s)
{
    for (char *c = s; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    return s;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "bt.c"                                      /* Bluetooth audio and MIDI devices: their state, their words */
static char *note_name(int n, char *b)               /* "C4" (60), "Eb3" */
{
    char *e = s_cat(b, NOTE_NAME[(n % 12 + 12) % 12]);
    return num_i(n / 12 - 1, e);
}
static const int8_t SIN32[32] = {0, 20, 38, 56, 71, 83, 92, 98, 100, 98, 92, 83, 71, 56, 38, 20,
                                 0, -20, -38, -56, -71, -83, -92, -98, -100, -98, -92, -83, -71, -56, -38, -20};
static int32_t sin100(uint32_t k) { return SIN32[k & 31u]; }
static int32_t cos100(uint32_t k) { return SIN32[(k + 8u) & 31u]; }

/* a rounded box; the corners anti-aliased (each corner pixel by its distance from the corner's centre) */
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
#ifdef GFX_AUDIT
    gfx_audit_box(x, y, w, h);
#endif
    if (r > h / 2)
        r = h / 2;
    if (r > w / 2)
        r = w / 2;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x, y + r, w, h - 2 * r, c);
    cv_rect(x + r, y, w - 2 * r, r, c);
    cv_rect(x + r, y + h - r, w - 2 * r, r, c);
    for (int32_t j = 0; j < r; j++)
        for (int32_t i = 0; i < r; i++) {
            float dx = (float)r - ((float)i + 0.5f), dy = (float)r - ((float)j + 0.5f), d2 = dx * dx + dy * dy;
            float cov = (float)r + 0.5f - (d2 > 1.0f ? f_sqrt1(d2) : 1.0f);
            uint32_t a = cov >= 1.0f ? 256u : cov <= 0.0f ? 0u : (uint32_t)(cov * 256.0f);
            cv_blend(x + i, y + j, c, a);                     /* the four corners, mirrored */
            cv_blend(x + w - 1 - i, y + j, c, a);
            cv_blend(x + i, y + h - 1 - j, c, a);
            cv_blend(x + w - 1 - i, y + h - 1 - j, c, a);
        }
}
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t in)
{
    cv_round(x, y, w, h, r, c);
    cv_round(x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, in);
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t xr, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(xr - text_w(f, s), y, f, s, c);
}
static int32_t cap_y(int32_t top, int32_t h, const felucca_font_t *f)   /* the y that centres f's capitals in h from top */
{
    const uint8_t *H = f->box['H' - f->first];       /* (the ink box of H: its first row, its rows) */
    int32_t c2 = 2 * (int32_t)H[1] + (int32_t)H[3] - 1;
    return (2 * top + h - 1 - c2) >> 1;
}
static int32_t text_fit(int32_t x, int32_t y, const felucca_font_t *f, const char *s, int32_t w, uint16_t c)
{
    char b[48];
    uint32_t n;
    for (n = 0; n < sizeof b - 1u && s[n]; n++)
        b[n] = s[n];
    b[n] = 0;
    while (n && text_w(f, b) > w)
        b[--n] = 0;
#ifdef GFX_AUDIT
    if (s[n])                                        /* (the layout audit: a text cut to fit is cut off on the screen) */
        ga_report("CUT TO FIT", s, b, x, y + cv_oy);
#endif
    return cv_text(x, y, f, b, c);
}
static uint32_t s_find_sp(const char *s)            /* the first space's place (the end: none) */
{
    uint32_t i = 0;
    while (s[i] && s[i] != ' ')
        i++;
    return i;
}
static int upper_only(const char *s)
{
    while (*s)
        if ((uint8_t)*s++ > 95u)
            return 0;
    return 1;
}
static int32_t wrap_do(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c, int draw)
{
    char line[48];
    while (*s) {
        uint32_t n = 0, cut = 0;
        while (s[n] && n < sizeof line - 1u) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (text_w(f, line) > w)
                break;
            n++;
            if (!s[n] || s[n] == ' ')
                cut = n;
        }
        if (!cut)
            cut = n ? n : 1;
        line[cut] = 0;
        if (draw)
            cv_text(x, y, f, line, c);
        y += f->h > 17 ? f->h + 1 : 17;           /* (a line's pitch: 17, a larger face its height) */
        s += cut;
        while (*s == ' ')
            s++;
    }
    return y;
}
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c)
{
    return wrap_do(x, y, w, f, s, c, 1);
}
/* --------------------------------------------------------- the model --- */
/* the bands, top to bottom: the header (its double rule at 23..26), the main band (a page's picture), the knobs, the
 * hints (two rows: the encoders' and the keys', or the parts split between them) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 120
#define KNB_Y 148
#define KNB_H 58
#define ENC_Y 206
#define PIC_H (MAIN_H - 32)                          /* an engine's picture under its name (88 today) */
#define F_CAP (&LFONT_XS)                            /* a page's explanation ("tracks that follow") */
enum { AL_INFO, AL_DONE, AL_UNDO, AL_REDO, AL_ASK, AL_NO };

static int sel_trk(void) { return proj.trk_sel < NALLTRK ? proj.trk_sel : 0; }
static int is_aux(int t) { return t >= NTRK; }
static int is_seqt(int t) { return t < NSEQT; }
static int steps_page(int p) { return p == P_STEPS || p == P_BAR; }   /* SEQ's pages: both write steps */
static int is_drum(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE; }
static int is_midi(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_MIDI; }   /* (no sound: notes and CCs
                                                                                         * out, synth.h EG_MIDI) */
static int is_acid(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_ACID; }   /* (its 303's own envelope:
                                                                                         * no Filter env page) */
/* a page's name: a drum track's Play page is its Key page, its Filter env page the kit's envelope */
static const char *page_name_of(int p, int trk)
{
    if (is_drum(trk) && p == P_PLAY)
        return "Key";
    if (is_drum(trk) && p == P_ENV2)
        return "Kit env";
    if (is_drum(trk) && p == P_PLAY2)
        return "Sends";
    if (trk == AUX_WEAVE && p == P_ENGINE)
        return "Weave";
    if (trk == AUX_BRAIN && p == P_SUB)
        return "Follow";
    if (trk < NTRK && !is_drum(trk) && !is_aux(trk) && p == P_PLAYERS3 && proj.trk[trk].par[TP_PLAYER] % NPL == PL_RAND)
        return "Players 3";                          /* (the arp's page 3 is Hold) */
    if (trk < NTRK && p == P_SUB && proj.trk[trk].par[TP_ENGINE] == EG_SAMPLE)
        return "Loop";
    if ((is_drum(trk) || is_midi(trk)) && p == P_SUB)
        return "Engine 2";
    return PAGE_NAME[p % NPAGES];
}
/* the filter type the Filter pages' PRESET and their list turn: a drum track's key's own (its Envelope and
 * Filter pages), the kit bus's (Kit env, Kit filter), else the voice filter's */
static int ftype_param(int trk, int page)
{
    if (is_drum(trk) && (page == P_ENV || page == P_FILTER))
        return TP_DRUM(ui.dsound % DL_NSOUND, DP_FTYPE);
    return TP_FTYPE;
}
static int is_fx(int t) { return t == AUX_FX1 || t == AUX_FX2; }
static int cur_scene(void) { return seq.scene < NSCENE ? seq.scene : 0; }
static int cur_pat(int t) { return is_seqt(t) ? seq.ppat[t] % NPATT : 0; }
static tpat_t *tpat(int t) { return &pats.pat[is_seqt(t) ? t : 0][cur_pat(t)]; }
static int playing(void) { return seq.playing; }
#define NCIN 4                                       /* the count-in's choices (proj.rsv[0]): off, 1 bar, 2 bars, on note */
#define CIN_NOTE 3
static int recording(void) { return seq.rec || seq.rec_wait || ui.rec_armed; }
static int page_btn_held(int b);
/* a kit's EDIT pages: the white keys put the key shown (black keys pick it) on the bar's steps, as Steps does; while
 * recording, or with EDIT held, they play the kit as before */
static int kit_edit_steps(void)
{
    int t = sel_trk();
    return is_drum(t) && PAGE_BTN[page_group(ui.page)].btn == B_EDIT && !recording() && !page_btn_held(B_EDIT);
}
static int step_shown(int i) { return ui.bar * 16 + i; }
static step_t *chord_step(int t, int i)              /* chord mode's held cell i: in the bar and pattern it began in */
{
    return &pats.pat[is_seqt(t) ? t : 0][ui.chord_pat % NPATT].st[(ui.chord_bar * 16 + i) % NSTEP];
}
static int home_held(void) { return ui.btn >> B_HOME & 1u; }
/* REC held: its knobs are the record settings while stopped and on Steps and Bar; on a sound page while playing,
 * the page's own, recorded as they turn (a punch-in of motion, as the keys punch in notes) */
static int rec_settings(void)
{
    return (ui.btn >> B_REC & 1u) && !home_held() && (!playing() || steps_page(ui.page));
}
static int kit_slot(int t)                           /* a kit track's place among the kits (scene_t kmute), -1 none */
{
    int k = 0;
    if (!is_drum(t))
        return -1;
    for (int i = 0; i < t; i++)
        k += is_drum(i);
    return k < NKITM ? k : -1;
}
static int pl_type(int t) { return is_aux(t) || is_drum(t) ? PL_OFF : proj.trk[t].par[TP_PLAYER] % NPL; }   /* (a kit has no player) */
#define CTYPE_NAME(par) (tp_info_par(TP_CTYPE, par)->names[(par)[TP_CTYPE] % NCT])   /* the character's type */
static int inst_trk(void) { return is_aux(sel_trk()) ? ui.last_inst % NTRK : sel_trk(); }   /* the keys' instrument */
static int fx_page(int p) { return p == P_FX1 || p == P_FX2; }
/* the track a page's knobs edit: the FX pages their effect track, Character and Sends the instrument */
static int page_trk(int p)
{
    return p == P_FX1 ? AUX_FX1 : p == P_FX2 ? AUX_FX2 : p == P_SENDS || p == P_CHARACTER ? inst_trk() : sel_trk();
}
static int kit_x_page(int t)                         /* a kit whose key shown has eight knobs: its
                                                      * knobs 5..8 on Engine 2 (TP_DRUMX) */
{
    return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE &&
           dm_knobs(proj.trk[t].par[TP_DRUM(ui.dsound % DL_NSOUND, DP_MODEL)] % NDM) > 4;
}
static int x_page(int t)                             /* the engine has a second page of knobs (TP_X1..X4) */
{
    int e = t < NTRK ? proj.trk[t].par[TP_ENGINE] : -1, m = t < NTRK ? proj.trk[t].par[TP_E4] % NSM : 0;
    return e == EG_RAYON || e == EG_KNIT || e == EG_MIDI || e == EG_ACID || (e == EG_SAMPLE && (m == SM_LOOP || m == SM_PINGPONG)) ||   /* (the
                                                                                     * sample's loop page: looping) */
           kit_x_page(t);
}
static int group_n(int g)
{
    int trk = sel_trk(), b = PAGE_BTN[g].btn;
    if (b == B_SEQ && trk == AUX_WEAVE)
        return 1;                                    /* (WEAVE's steps: no Bar page) */
    if (b == B_EDIT)                                 /* (grp_page: the conductor's Sub third; a kit, an FX track: two) */
        return trk == AUX_PUNCH || trk == AUX_WEAVE ? 1 : trk == AUX_BRAIN ? 3 : is_aux(trk) ? 2
             : x_page(trk) ? 4 : 3;      /* (a kit: Engine, Key, Sends: the key's own; Engine 2 after Engine when the
                                          * key's model has eight knobs) */
    if (b == B_ARP)                                  /* (grp_page: Players, Players 2, Hold the arp's, Weave) */
        return is_aux(trk) ? 1
             : is_drum(trk) || pl_type(trk) == PL_OFF ? 2
             : pl_type(trk) == PL_ARP || pl_type(trk) == PL_RAND ? 4 : 3;
    if (b == B_ENV)
        return is_drum(trk) ? 4 : is_midi(trk) ? 1 : is_acid(trk) ? 2 : 3;   /* a kit: the key's envelope, filter; the
                                                           * kit's envelope, filter (a MIDI track: none, page_button
                                                           * stays; acid: its 303's envelope and filter) */
    if (b == B_LFO && is_midi(trk))
        return 1;
    if (b == B_FX && is_midi(trk))
        return 3;                                    /* (grp_page: FX I, FX II, Throws: no Character, no Sends) */
    if (b == B_GLO)
        return DEBUG_BUILD ? 7 : 6;                  /* Debug last, in a debug build only */
    return PAGE_BTN[g].n;
}
/* a button's i-th page now (ARP's depend on the player: the arp has Hold before Weave; the conductor and the
 * throws only Weave), and a page's place among them (-1: not one of them now) */
/* a track's weave (its settings' rsv[0..2]): the track woven in + 1 (0: none), its pattern, the rule */
static int wv_from(int t) { return t < NSEQT && proj.trk[t].rsv[0] <= NSEQT ? proj.trk[t].rsv[0] : 0; }
static int wv_pat(int t) { return proj.trk[t].rsv[1] % NPATT; }
static int wv_rule(int t) { return proj.trk[t].rsv[2] % NWV; }
static int wv_cued(int t) { return proj.trk[t].rsv[3] & 1u; }   /* PLAYS: 0 always, 1 only on the WEAVE track */
static int wv_kind(int t) { return t == AUX_PUNCH ? WK_FX : is_drum(t) ? WK_KIT : WK_MEL; }
static int grp_page(int g, int i)
{
    if ((PAGE_BTN[g].btn == B_ENV || PAGE_BTN[g].btn == B_LFO) && is_midi(sel_trk()))
        return P_ENGINE;                             /* (a MIDI track has no sound to shape: its knobs instead) */
    if (PAGE_BTN[g].btn == B_EDIT && sel_trk() == AUX_BRAIN)   /* Engine, Play (routing), Sub (Follow) */
        return i == 2 ? P_SUB : P_ENGINE + i;
    if (PAGE_BTN[g].btn == B_EDIT && sel_trk() < NTRK && (proj.trk[sel_trk()].par[TP_ENGINE] == EG_SAMPLE ||
        is_drum(sel_trk()) || is_midi(sel_trk())) && x_page(sel_trk())) {  /* a looping sample: its Loop page second, after Engine (a
                                                      * kit's key of eight knobs: Engine 2) */
        static const uint8_t SMPP[4] = {P_ENGINE, P_SUB, P_PLAY, P_PLAY2};
        return SMPP[i & 3];
    }
    if (PAGE_BTN[g].btn == B_FX && is_midi(sel_trk()))
        return P_FX1 + i % 3;                        /* (a MIDI track: no sound for Character or Sends) */
    if (PAGE_BTN[g].btn == B_ARP) {
        static const uint8_t ARPP[4] = {P_PLAYERS, P_PLAYERS2, P_PLAYERS3, P_WEAVE};
        static const uint8_t OTHER[3] = {P_PLAYERS, P_PLAYERS2, P_WEAVE};
        int trk = sel_trk();
        if (is_aux(trk))
            return P_WEAVE;
        if (is_drum(trk) || pl_type(trk) == PL_OFF)
            return i & 1 ? P_WEAVE : P_PLAYERS;      /* (a kit, or no player: its page shows it off, then Weave) */
        return pl_type(trk) == PL_ARP || pl_type(trk) == PL_RAND ? ARPP[i & 3] : OTHER[(i % 3 + 3) % 3];
    }
    return PAGE_BTN[g].first + i;
}
static int grp_index(int p)
{
    int g = page_group(p), n = group_n(g);
    for (int i = 0; i < n; i++)
        if (grp_page(g, i) == p)
            return i;
    return -1;
}
/* the TP_* a page's knob k edits, -1 none */
static int fx_pick_of(int trk);
static int tp_page_param(int p, int k, int trk)
{
    static const int8_t M[][4] = {
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_VMODE, TP_PORTA, TP_BEND, TP_VOLUME}, {TP_OCT, -1, -1, -1},
        {TP_X1, TP_X2, TP_X3, TP_X4},
        {TP_A, TP_D, TP_S, TP_R},
        {TP_CUT, TP_RES, TP_FENV, TP_KTRK}, {TP_FA, TP_FD, TP_FS, TP_FR}, {TP_CUT, TP_RES, TP_FENV, TP_KTRK},
        {TP_CTYPE, TP_CAMT, TP_CTONE, TP_CMIX},
        {TP_SEND1, TP_SEND2, TP_PAN, TP_LEVEL},
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_E1, TP_E2, TP_E3, TP_E4}, {-1, -1, -1, -1},
        {TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST},
        {TP_LX, TP_LX2, -1, -1}, {TP_PLAYER, TP_P1, TP_P2, TP_P3}, {TP_AP1, TP_AP2, TP_AP3, TP_AP4},
        {TP_P4, -1, -1, -1}, {-1, -1, -1, -1}, {TP_SEND1, TP_SEND2, TP_PAN, TP_LEVEL}};
    if (p < P_ENGINE || p > P_MIX || p == P_THROWS || p == P_WEAVE || k < 0 || k > 3)
        return -1;
    if (fx_page(p))                                  /* (the effect tracks' own knobs, whatever trk is) */
        return M[p - P_ENGINE][k];
    if (p == P_SENDS)                                /* (a kit's: the key's own, below) */
        return is_aux(trk) ? -1 : is_drum(trk) && k < 2 ? TP_DRUM(ui.dsound % DL_NSOUND, DP_SEND1 + k)
                                                        : M[p - P_ENGINE][k];
    if (p == P_CHARACTER)                            /* (a kit's: the whole kit's, after its bus filter) */
        return is_aux(trk) ? -1 : M[p - P_ENGINE][k];
    if (is_aux(trk)) {
        if (p == P_MIX)                              /* (the conductor and the throws: no sound, no fader) */
            return k == 3 && is_fx(trk) ? TP_LEVEL : -1;
        if (trk == AUX_BRAIN && (p == P_ENGINE || p == P_SUB))   /* key, scale, link; the Follow page: its track */
            return p == P_SUB ? (k ? -1 : TP_E1) : k < 3 ? TP_E2 + k : -1;
        if (p == P_ENGINE)
            return (trk == AUX_PUNCH || trk == AUX_WEAVE) && k ? -1 : TP_E1 + k;
        if (p == P_PLAY && is_fx(trk)) {             /* an FX track's page 2: high-pass, low-pass, the feed into FX II,
                                                      * a delay's PING WIDTH */
            static const int8_t F2[4] = {TP_FENV, TP_CUT, TP_SEND2, TP_FX_WIDTH};
            return k == 2 && trk != AUX_FX1 ? -1 : k == 3 && fx_pick_type(fx_pick_of(trk)) != FXT_DELAY ? -1 : F2[k];
        }
        return -1;
    }
    if (is_drum(trk)) {
        int key = ui.dsound % DL_NSOUND;
        if (p == P_ENGINE)
            return TP_DRUM(key, DP_K1 + k);
        if (p == P_SUB)                              /* the key's knobs 5..8 (a model's with eight) */
            return kit_x_page(trk) ? TP_DRUMX(key, k) : -1;
        if (p == P_PLAY) {
            static const uint8_t K2[4] = {DP_MODEL, DP_RANDOM, DP_PAN, DP_VOLUME};
            return TP_DRUM(key, K2[k]);
        }
        if (p == P_ENV) {                            /* the key's own envelope */
            static const uint8_t KE[4] = {DP_ATTACK, DP_HOLD, DP_DECAY, DP_LAG};
            return TP_DRUM(key, KE[k]);
        }
        if (p == P_FILTER) {                         /* and its filter (the type: PRESET) */
            static const uint8_t KF[4] = {DP_CUT, DP_RES, DP_FENV, DP_DRIVE};
            return TP_DRUM(key, KF[k]);
        }
        if (p == P_MIX)                              /* (a kit has no track sends: FX, Sends has its keys') */
            return k < 2 ? -1 : M[p - P_ENGINE][k];
        if (p == P_ENV2)                             /* the kit's envelope: lag, attack, random, length */
            return M[P_ENV - P_ENGINE][k];
        if (p == P_PLAY2)                            /* (a kit has no octave page: the key's sends, as FX's Sends) */
            return k < 2 ? TP_DRUM(key, DP_SEND1 + k) : -1;
    }
    if (p == P_SUB && !x_page(trk))
        return -1;
    if (p == P_SUB && k == 0 && home_held() && proj.trk[trk].par[TP_ENGINE] == EG_KNIT)
        return TP_O2FINE;                            /* knit's osc 2 with HOME held: its fine tune, in cents */
    if (is_drum(trk) && (p == P_PLAYERS || p == P_PLAYERS2 || p == P_PLAYERS3))
        return -1;                                   /* a kit: no player (its page shows it off) */
    if (pl_type(trk) == PL_RAND) {                  /* random: P1..P3, AP1..AP4, octave */
        if (p == P_PLAYERS)
            return TP_PLAYER + k;
        if (p == P_PLAYERS2)
            return TP_AP1 + k;
        if (p == P_PLAYERS3)
            return k == 0 ? TP_P4 : k == 1 ? TP_RUP : -1;   /* the octaves down and up */
    }
    if (p == P_PLAYERS2 && pl_type(trk) != PL_ARP)
        return k == 0 ? TP_P4 : -1;
    if (p == P_PLAYERS3 && pl_type(trk) != PL_ARP)
        return -1;
    return M[p - P_ENGINE][k];
}

/* a pen for smooth curves: pen_to draws an anti-aliased line from the last point (pen_up lifts it) */
typedef struct { float x, y, px, py; int down, steep; float w; } pen_t;   /* (px, py: the last segment's start; steep:
                                                                           * its axis, -1 none yet; w: 0 the usual 1.7 px) */
static void pen_up(pen_t *p) { p->down = 0; }
static void pen_to(pen_t *p, float x, float y, uint16_t c)
{
    if (p->down) {
        int st = f_abs(y - p->y) > f_abs(x - p->x);
        if (p->steep >= 0 && st != p->steep)     /* the curve turns to the other axis: the last segment's end column too */
            cv_line_ex(p->px, p->py, p->x, p->y, c, p->w > 0.0f ? p->w : 1.7f, 1);
        p->steep = cv_line_ex(p->x, p->y, x, y, c, p->w > 0.0f ? p->w : 1.7f, 0);
        p->px = p->x, p->py = p->y;
    } else
        p->steep = -1;
    p->x = x, p->y = y, p->down = 1;
}
static float ease_exp(float t)                       /* 1 -> 0 along an exponential, reaching 0 at t = 1 */
{
    static const float E4 = 0.0183156f;              /* exp(-4) */
    return (fm_expf(-4.0f * t) - E4) / (1.0f - E4);
}

/* ------------------------------------------------ the manual's figures --- */
/* The paper interface draws like the figures of a printed service manual: hairlines, circled callout numbers, rulers,
 * dimension lines with arrowheads, dotted leaders, graph paper, section hatching. The faces: F_LAB (capitals, 11 px:
 * labels, key-caps, section heads; nothing the player reads to operate is smaller), F_XB (12 bold: a value beside XS),
 * F_VAL (21 bold: the knobs' values). Text goes through cv_text, so the layout audit sees all of it. */
#define F_LAB (&LFONT_C)
#define F_XB (&LFONT_XB)
#define F_VAL (&LFONT_V)
static int32_t tr_w(const felucca_font_t *f, const char *s, int32_t sp)   /* letters sp px apart more (tracked) */
{
    int32_t w;
    loom_track = -sp;
    w = text_w(f, s);
    loom_track = 0;
    return w;
}
static int32_t tr_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, int32_t sp)
{
    loom_track = -sp;
    x = cv_text(x, y, f, s, c);
    loom_track = 0;
    return x;
}
static void up_copy(char *d, const char *s, uint32_t n)   /* s in capitals, at most n - 1 letters */
{
    uint32_t i;
    for (i = 0; i + 1 < n && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}
/* a circled callout number (the knobs: 1 to 4) centred on (cx, cy + 0.5): an ink ring 12 px across, the number in
 * F_LAB; fill the disc inside (K_BG: the paper) */
static void circ_num(int32_t cx, int32_t cy, const char *n, uint16_t ink, uint16_t fill)
{
    cv_disc((float)cx, (float)cy - 0.5f, 5.2f, fill);
    cv_ring((float)cx, (float)cy - 0.5f, 5.6f, 1.0f, ink);
    text_c(cx + 1, cy - 6, F_LAB, n, ink);
}
/* a ruler: a base line w px long at row y, a tick every 6 px (every fourth taller), and under it a pointer at num of max
 * (max <= 0: none) */
static void ruler(int32_t x, int32_t y, int32_t w, int32_t num, int32_t max, uint16_t pc)
{
    cv_rect(x, y, w, 1, K_TEXT);
    for (int32_t j = 0; j * 6 <= w; j++)
        cv_rect(x + j * 6 - (j * 6 == w), y - (j % 4 ? 2 : 4), 1, j % 4 ? 2 : 4, K_TEXT);
    if (max > 0) {
        float px = (float)x + (float)(w - 1) * (float)clampi(num, 0, max) / (float)max;
        cv_tri(px, (float)y + 1.6f, px - 3.4f, (float)y + 6.6f, px + 3.4f, (float)y + 6.6f, pc);
    }
}
/* an arrowhead at (x, y) pointing along (dx, dy) (a unit vector): 4 px long, 4.8 wide */
static void arrow_head(float x, float y, float dx, float dy, uint16_t c)
{
    cv_tri(x, y, x - dx * 4.0f - dy * 2.4f, y - dy * 4.0f + dx * 2.4f, x - dx * 4.0f + dy * 2.4f, y - dy * 4.0f - dx * 2.4f, c);
}
/* a dimension line from a to b: a hairline between arrowheads that point out to the ends, each end a tick across it from
 * ta px on one side to tb on the other (the side: the direction turned a quarter clockwise, on the glass's y down) */
static void dim_line(float ax, float ay, float bx, float by, float ta, float tb, uint16_t c)
{
    float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
    if (l < 4.0f)
        return;
    l = f_sqrt1(l);
    dx /= l, dy /= l;
    if (l > 10.0f)
        cv_linef(ax + dx * 3.0f, ay + dy * 3.0f, bx - dx * 3.0f, by - dy * 3.0f, c, 0.9f);
    arrow_head(ax + dx, ay + dy, -dx, -dy, c);
    arrow_head(bx - dx, by - dy, dx, dy, c);
    if (ta > 0.0f || tb > 0.0f) {
        cv_linef(ax + dy * ta, ay - dx * ta, ax - dy * tb, ay + dx * tb, c, 0.7f);
        cv_linef(bx + dy * ta, by - dx * ta, bx - dy * tb, by + dx * tb, c, 0.7f);
    }
}
/* a spec row from x1 to x2: its label (XS), a dotted leader, its value bold at the right (F_XB). y: the row's top */
static void spec_row(int32_t x1, int32_t x2, int32_t y, const char *label, const char *val, uint16_t vc)
{
    int32_t e = cv_text(x1, y, &FONT_XS, label, K_TEXT), v = x2 - text_w(F_XB, val);
    cv_text(v, y, F_XB, val, vc);
    if (v - 5 > e + 4)
        cv_leader(e + 4, v - 5, y + 10, K_DIM);
}
/* a key-cap: the control's name in F_LAB in a thin ink outline, 14 px tall; its width (fill: K_BG, or K_TEXT inked) */
#define CAP_H 14
static int32_t cap_w(const char *k) { return text_w(F_LAB, k) + 7; }
static int32_t key_cap(int32_t x, int32_t y, const char *k, int inked)
{
    int32_t w = cap_w(k);
    cv_frame(x, y, w, CAP_H, 2, K_TEXT, inked ? K_TEXT : K_BG);
    cv_text(x + 4, y + 1, F_LAB, k, inked ? K_BG : K_TEXT);
    return w;
}
/* an ellipse (rx, ry) or a part of one (turns t0 .. t1, 0 at the right, a quarter down), w px wide */
static void ellipse_arc(float cx, float cy, float rx, float ry, float t0, float t1, float w, uint16_t c)
{
    pen_t pn = {0};
    int n = (int)((t1 - t0) * (rx > ry ? rx : ry) * 0.9f) + 8;
    pn.w = w;
    for (int i = 0; i <= n; i++) {
        float t = t0 + (t1 - t0) * (float)i / (float)n;
        pen_to(&pn, cx + rx * fm_sin_turns(t + 0.25f), cy + ry * fm_sin_turns(t), c);
    }
}

/* ---------------------------------------------------------- alerts --- */
/* a message or a question over the header: a card with an ink hairline (a question inked: paper on ink), a circled
 * mark at the left (i, ?, !, a tick, undo's arrows), the text from AL_X; the double rule stays under it */
#define AL_X 26
static void alert_box(int kind)
{
    int ask = kind == AL_ASK;
    uint16_t fill = ask ? K_TEXT : K_CARD, ink = ask ? K_BG : K_TEXT;
    int32_t cx = 14, cy = 11;
    cv_frame(4, 2, 232, 19, 2, K_TEXT, fill);
    if (kind == AL_NO)
        cv_disc((float)cx, (float)cy, 6.6f, K_TEXT);
    else
        cv_ring((float)cx, (float)cy, 6.0f, 1.1f, ink);
    if (kind == AL_DONE) {
        cv_linef(10.6f, 11.2f, 13.0f, 13.8f, ink, 1.5f);
        cv_linef(13.0f, 13.8f, 17.6f, 8.2f, ink, 1.5f);
    } else if (kind == AL_UNDO || kind == AL_REDO) {
        float s = kind == AL_REDO ? -1.0f : 1.0f;
        ellipse_arc((float)cx, (float)cy + 0.5f, 3.4f, 3.4f, kind == AL_REDO ? 0.6f : -0.1f, kind == AL_REDO ? 1.1f : 0.4f, 1.3f, ink);
        arrow_head((float)cx - s * 3.4f, (float)cy - 1.2f, 0.0f, -1.0f, ink);
    } else
        text_c(cx + 1, cy - 7, &FONT_XS, ask ? "?" : kind == AL_NO ? "!" : "i", kind == AL_NO ? K_BG : ink);
}
static void tempo_text(char *t)                      /* "120.0" */
{
    char *e = num_s(proj.tempo_x10 / 10u, t);
    *e++ = '.';
    num_s(proj.tempo_x10 % 10u, e);
}
/* the engine's meters (eng_peak, the loudest since the last look), taken once a frame for everything that reads them:
 * the header's meter, the Mix page, the engine pictures */
static uint16_t ui_peak[NALLTRK];
static uint8_t ui_ghost[NTRK];                   /* (eng_ghost, taken with the meters) */
static void meters_take(void)
{
    for (int t = 0; t < NALLTRK; t++) {
        ui_peak[t] = eng_peak[t];
        eng_peak[t] = 0;
    }
    for (int t = 0; t < NTRK; t++) {
        ui_ghost[t] = eng_ghost[t];
        eng_ghost[t] = 0;
    }
}
static float lin_db(float g);
/* the header's meter, before the track's pill: what the track plays now (its level after the fader, falling 24 dB
 * a second, -45 to -9 dB: activity more than level; the aux tracks make no sound: what they do now lights it). 0 .. HM_H pixels */
#define HM_H 17
static int hdr_meter(int trk)
{
    static float db = -60.0f;
    static uint32_t last;
    uint32_t now = plat_ms(), dt = now - last;
    float v;
    last = now;
    if (trk >= NTRK && !is_fx(trk)) {            /* COND, THROW: a step with something on it, playing; WEAVE: a
                                                  * track weaving */
        const step_t *st;
        if (trk == AUX_WEAVE)
            return (seq.wv_live | (playing() ? seq.wv_rec : 0)) ? HM_H : 0;
        if (!playing() || trk >= NSEQT)
            return 0;
        st = &tpat(trk)->st[eng_step[trk] % NSTEP];
        return step_empty(st) ? 0 : HM_H;
    }
    v = lin_db((float)ui_peak[trk] * (1.0f / 16383.5f));
    db = fm_maxf(v, db - (float)(dt > 1000u ? 1000u : dt) * 0.024f);
    if (db <= -45.0f)
        return 0;
    return 1 + (int)((fm_minf(db, -9.0f) + 45.0f) * (float)(HM_H - 1) / 36.0f);
}
/* the battery, upright at the header's right (plat_battery): an outline 7 x 11 with its nub, three bars from the foot
 * (vermilion at one or none); a USB host powering the FM-1: a bolt. -1 (not read yet): nothing */
#define BATT_W 7
static void draw_batt(int32_t x, int b)
{
    uint16_t c = b >= 0 && b <= 1 ? K_REC : K_TEXT;
    cv_rect(x + 2, 4, 3, 2, K_TEXT);
    cv_frame(x, 6, BATT_W, 11, 1, K_TEXT, K_BG);
    if (b == 4) {
        cv_linef((float)x + 4.3f, 7.5f, (float)x + 1.9f, 12.0f, K_TEXT, 1.2f);
        cv_linef((float)x + 1.9f, 12.0f, (float)x + 4.3f, 11.0f, K_TEXT, 1.2f);
        cv_linef((float)x + 4.3f, 11.0f, (float)x + 2.0f, 15.5f, K_TEXT, 1.2f);
        return;
    }
    for (int i = 0; i < b && i < 3; i++)
        cv_rect(x + 2, 13 - i * 3, BATT_W - 4, 2, c);
}
#ifdef PLAT_HOST
static int msg_too_wide;                         /* hints and messages that did not fit (the host fails on them) */
#endif
static int hdr_trk_page(void)                    /* a track's page: the header's chip is the track */
{
    return ((ui.page >= P_STEPS && ui.page <= P_PLAYERS3) || ui.page == P_WEAVE) && ui.page != P_THROWS;
}
static const char *hdr_name(void)                /* the page's name (an effect track's page: its effect) */
{
    if (lst.open && lst.bt)
        return lst.bt == 1 ? "Audio devices" : "MIDI devices";
    if (fx_page(ui.page) || (is_fx(sel_trk()) && ui.page == P_ENGINE))
        return FXT_NAME[fx_pick_type(fx_pick_of(fx_page(ui.page) ? (ui.page == P_FX1 ? AUX_FX1 : AUX_FX2) : sel_trk()))];
    return page_name_of(ui.page, sel_trk());
}
/* the word after the name, dim: a SEL list open says what it holds ("ACTIONS", "SETTINGS"); else nothing */
static int list_is_setup(int p);
static const char *hdr_ctx(void)
{
    return lst.open && !lst.bt ? (list_is_setup(ui.page) ? "SETTINGS" : "ACTIONS") : "";
}
/* the header: an inked chip (the track, or the button), the page's name in tracked bold capitals, what hdr_ctx says
 * (dim); at the right the scene, the tempo (vermilion while playing), the battery; under it a double rule (thick, then
 * a hairline) whose right end carries the button's pages as squares, the one shown inked. The track's sound now: a
 * vermilion bar under the chip */
static void hdr_rules(void)
{
    int g = page_group(ui.page), n = group_n(g), d = grp_index(ui.page);
    int32_t r = 232;
    uint16_t half = col_mix(K_TEXT, K_BG, 7);
    if (n > 1 && !lst.open) {
        int32_t w = n * 7 - 2;
        r = 232 - w - 5;
        for (int i = 0; i < n; i++) {
            int32_t x = 232 - w + i * 7;
            if (i == d)
                cv_rect(x, 22, 5, 5, K_TEXT);
            else
                cv_frame(x, 22, 5, 5, 0, K_DIM, K_BG);
        }
    }
    cv_rect(8, 23, r - 8, 1, K_TEXT);
    cv_rect(8, 24, r - 8, 1, half);
    cv_rect(8, 26, r - 8, 1, half);
}
static void hdr_page(int mh, int ext, int rec)
{
    char t[40], tt[12], sc[6];
    const char *cx = hdr_ctx();
    int32_t x = 8, cw, rs, rt, rsc, re;
    int tp = hdr_trk_page();
    const char *chip = tp ? TRACK_NAME[page_trk(ui.page)] : BTN_LABEL[PAGE_BTN[page_group(ui.page)].btn];
    up_copy(t, hdr_name(), sizeof t);
    if (ext)
        s_cat(tt, "ext");
    else
        tempo_text(tt);
    num_s((uint32_t)cur_scene() + 1u, s_cat(sc, "S"));
    re = 232 - (plat_battery() >= 0 ? BATT_W + 6 : 0);   /* the tempo's right edge */
    rt = re - text_w(&FONT_B, tt);                    /* its left */
    rsc = rt - 7 - text_w(&FONT_XS, sc);              /* the scene's left */
    if (plat_battery() >= 0)
        draw_batt(232 - BATT_W, plat_battery());
    cv_text(rt, 4, &FONT_B, tt, playing() ? K_REC : K_TEXT);
    cw = text_w(F_LAB, chip) + 8;
    cv_round(x, 5, cw, 14, 2, K_TEXT);
    cv_text(x + 4, 6, F_LAB, chip, K_BG);
    if (tp && mh > 0)                                /* the track sounding now: vermilion under its chip */
        cv_rect(x, 20, cw * mh / HM_H > 2 ? cw * mh / HM_H : 2, 2, K_REC);
    if (ui.dirty)                                    /* unsaved: a dim dot in the margin, by the chip */
        cv_disc(3.5f, 11.5f, 1.6f, K_DIM);
    x += cw + 8;
    /* what gives way, in order: the name's tracking, the list's word, the scene (REC's dot sits before the scene) */
    for (int scene = 1; scene >= 0; scene--) {
        rs = (scene ? rsc : rt) - (rec ? 13 : 0) - 8;   /* the room's right edge */
        for (int k = 0; k < 4; k++) {
            int nsp = !(k & 1), ctx = *cx && !(k & 2);
            int32_t w = (nsp ? tr_w(&FONT_B, t, 1) : text_w(&FONT_B, t)) + (ctx ? 8 + tr_w(F_LAB, cx, 1) : 0);
            if (x + w <= rs) {
                int32_t e = nsp ? tr_text(x, 4, &FONT_B, t, K_TEXT, 1) : cv_text(x, 4, &FONT_B, t, K_TEXT);
                if (ctx)
                    tr_text(e + 8, 7, F_LAB, cx, K_DIM, 1);
                if (scene)
                    cv_text(rsc, 4, &FONT_XS, sc, K_DIM);
                if (rec)                             /* recording: a vermilion dot */
                    cv_disc((float)(rs + 8) + 4.0f, 11.0f, 4.0f, K_REC);
                return;
            }
        }
    }
    text_fit(x, 4, &FONT_B, t, rt - 8 - x, K_TEXT);  /* (nothing else fits: the name alone; cut: the audit says) */
}
static void draw_header(void)
{
    char t[40];
    int mh = 0;
    int msg = plat_ms() < ui.msg_until, rec = recording(), ext = seq.ext, ask = ui.ask, cnt = seq.count_in;
    int pk = BT_PASSKEY();                     /* a pairing's code, waiting to be typed on the phone */
    uint32_t h = hash_u(2166136261u + (uint32_t)(pk + 1) + ((uint32_t)lst.bt << 24), ui.page | (uint32_t)msg << 8 | (uint32_t)ui.dirty << 9 | (uint32_t)rec << 11 |
                                         (uint32_t)ext << 12 | (uint32_t)ask << 14 | (uint32_t)proj.tempo_x10 << 16);
    h = hash_u(h, (uint32_t)(plat_battery() + 1));
    h = hash_u(h, (uint32_t)cnt | (uint32_t)playing() << 8 | (uint32_t)is_drum(sel_trk()) << 9 |
                      (uint32_t)page_trk(ui.page) << 10 | (uint32_t)sel_trk() << 16 | (uint32_t)lst.open << 24);
    h = hash_u(h, (uint32_t)cur_scene() | (uint32_t)seq.rec_wait << 8 | (uint32_t)group_n(page_group(ui.page)) << 12);
    h = hash_s(hash_s(h, hdr_name()), hdr_ctx());
    if (msg)
        h = hash_u(h, ui.msg_until ^ (uint32_t)ui.msg_no << 31);
    if (ask)
        h = hash_s(h, ui.ask_q);
    meters_take();
    if (hdr_trk_page())
        mh = hdr_meter(page_trk(ui.page));
    h = hash_u(h, (uint32_t)mh);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    hdr_rules();
    if (pk >= 0) {                                   /* (first: a pairing waits only so long) */
        int32_t x;
        alert_box(AL_INFO);
        x = cv_text(AL_X, 4, &FONT_B, "PIN", K_TEXT);
        for (int i = 5, v = pk; i >= 0; i--, v /= 10)
            t[i] = (char)('0' + v % 10);
        t[6] = 0;
        cv_text(x + 8, 4, &FONT_B, t, K_TEXT);
    } else if (ask) {                                /* (inked: paper on ink) */
        alert_box(AL_ASK);
        if (text_w(&FONT_B, ui.ask_q) <= 232 - 4 - AL_X)
            cv_text(AL_X, 4, &FONT_B, ui.ask_q, K_BG);
        else if (text_w(&FONT_S, ui.ask_q) <= 232 - 4 - AL_X)
            cv_text(AL_X, 4, &FONT_S, ui.ask_q, K_BG);
        else
            text_fit(AL_X, 5, &FONT_XS, ui.ask_q, 232 - 4 - AL_X, K_BG);
    } else if (cnt && playing()) {
        int32_t x = tr_text(8, 4, &FONT_B, "COUNT-IN", K_TEXT, 1);
        num_s((uint32_t)cnt, t);
        cv_text(x + 8, 4, &FONT_B, t, K_REC);
        cv_disc(226.0f, 11.0f, 4.0f, K_REC);
    } else if (msg) {
        int32_t x;
        alert_box(ui.msg_no ? AL_NO : ui.msg_kind);
        x = cv_text(AL_X, 4, &FONT_B, ui.msg[0], K_TEXT);
        {   /* the second part smaller when it does not fit (cut only past the smallest) */
            int32_t room = 232 - 4 - (x + 7);
            const felucca_font_t *f = text_w(&FONT_B, ui.msg[1]) <= room ? &FONT_B
                                    : text_w(&FONT_S, ui.msg[1]) <= room ? &FONT_S : &FONT_XS;
            text_fit(x + 7, f == &FONT_XS ? 5 : 4, f, ui.msg[1], room, K_DIM);
        }
    } else if (seq.rec_wait && playing()) {      /* recording on the first note: waiting for it */
        tr_text(8, 4, &FONT_B, "PLAY A NOTE", K_TEXT, 1);
        cv_disc(226.0f, 11.0f, 4.0f, K_REC);
    } else
        hdr_page(mh, ext, rec);
    cv_blit(0, 0);
}

/* ----------------------------------------------------------- knobs --- */
typedef struct {
    const char *name;
    char val[24];
    int32_t num, max;
    int32_t live;                                    /* (max > 0) a lock or a motion's value now, as num; -1 none: a dot */
} knob_t;
static void kv(knob_t *k, const char *name, const char *val, int32_t num, int32_t max)
{
    uint32_t i;
    k->name = name;
    for (i = 0; i < sizeof k->val - 1u && val[i]; i++)
        k->val[i] = val[i];
    k->val[i] = 0;
    k->num = num;
    k->max = max;
}
static void kn(knob_t *k, const char *name, int32_t v, int32_t max)
{
    k->name = name;
    num_i(v, k->val);
    k->num = v;
    k->max = max;
}
/* the master color's knob m (amount, tone, mix): printed as a voice's character stage of the same type prints it
 * (tp_text_par: the tape's tone and the sampler's rate and filter in Hz, the rest in %) */
static void kmc(knob_t *k, int m)
{
    static const uint8_t CT_OF[NMC] = {CT_OFF, CT_TAPE, CT_SAMPLER};
    uint8_t par[NTP] = {0};                          /* (all of it: tp_info_par reads TP_PLAYER, past the sound's part) */
    char num[12], unit[6], val[24], *e;
    int p = TP_CAMT + (m - MS_CAMT), v = proj.master[m];
    par[TP_CTYPE] = CT_OF[proj.master[MS_CTYPE] % NMC];
    tp_text_par(p, v, par, num, unit);
    if (p == TP_CTONE && par[TP_CTYPE] == CT_OFF)      /* (off: the tone a plain number) */
        num_i(v, num), unit[0] = 0;
    e = s_cat(val, num);
    if (unit[0])
        s_cat(unit[0] != '%' ? s_cat(e, " ") : e, unit);
    kv(k, MS_NAME[m], val, v, 127);
}
/* the master's pump: its depth (0 .. 24 dB), its source, its release */
static void pump_text(int v, char *t)
{
    int d = (v * 240 + 63) / 127;
    char *e = num_i(d / 10, t);
    *e++ = '.';
    e = num_i(d % 10, e);
    s_cat(e, " dB");
}
static const char *pump_by_text(int v, char *t)
{
    if (v == PB_KICK)
        return "kick";
    if (v == PB_METRO)
        return "metronome";
    num_i(v - PB_T1 + 1, s_cat(t, "T"));
    return t;
}
static void kname(knob_t *k, const char *name, const char *const *names, int v, int n)
{
    kv(k, name, names[clampi(v, 0, n - 1)], v, n > 1 ? n - 1 : 0);   /* (the ruler's pointer: the place in the list) */
}
static const char *short_name(const char *n)        /* a knob name short enough for the strip */
{
    return s_eq(n, "frequency") ? "freq" : s_eq(n, "modulation") ? "mod" : s_eq(n, "feedback") ? "fdbk"
           : s_eq(n, "envelope") ? "env" : s_eq(n, "keytrack") ? "keytrk" : n;
}
/* a track parameter's knob: its name and value as synth.c prints them (the value v, or the track's) */
static void ktp_val(knob_t *k, int trk, int p, int v)
{
    const uint8_t *par = proj.trk[trk].par;
    const tp_info_t *in = tp_info_par(p, par);
    char num[12], unit[6], val[24], *e;
    if (!in->name || !in->name[0] || s_eq(in->name, "-"))
        return;
    tp_text_par(p, v, par, num, unit);
    e = s_cat(val, num);
    if (unit[0]) {
        if (unit[0] != '%')
            e = s_cat(e, " ");
        s_cat(e, unit);
    }
    kv(k, short_name(in->name), val, v - in->lo,
       in->kind == PK_NAMES || in->kind == PK_SWITCH || in->kind == PK_SAMPLE ? 0 : in->hi - in->lo);
}
static void ktp(knob_t *k, int trk, int p)
{
    if (p >= 0 && p < NTP) {
        int lv = engine_live(trk, p);
        ktp_val(k, trk, p, proj.trk[trk].par[p]);
        if (lv >= 0 && k->max > 0) {             /* the base shown, the locked value now a dot on its bar */
            const tp_info_t *in = tp_info_par(p, proj.trk[trk].par);
            lv -= in->lo;
            k->live = lv < 0 ? 0 : lv > k->max ? k->max : lv;
        }
    }
}
/* a length in steps, from quarter steps: "1/2", "1", "1 1/2", "8"; 0: the pattern's note length, "1/2 auto" */
static void quarters_text(int q, char *t)
{
    static const char *const Q[4] = {"", "1/4", "1/2", "3/4"};
    char *e = t;
    *t = 0;
    if (q >= 4) {
        e = num_s((uint32_t)q / 4u, t);
        if (q & 3)
            e = s_cat(e, " ");
    }
    if (q & 3 || !q)
        s_cat(e, q ? Q[q & 3] : "0");
}
static void step_len_text(int len, char *t, int nlen)
{
    if (len) {
        quarters_text(step_len_q(len), t);
        return;
    }
    if (nlen == 25 || nlen == 50 || nlen == 75 || nlen == 100)
        quarters_text(nlen / 25, t);
    else
        s_cat(num_s((uint32_t)nlen, t), "%");
    s_cat(t + s_len(t), " auto");
}
static void cond_text(int c, char *t)
{
    if (!c)
        s_cat(t, "always");
    else if (c < COND_AB)
        s_cat(num_s((uint32_t)c, t), "%");
    else {
        int a, b;
        cond_ab(c - COND_AB, &a, &b);
        num_s((uint32_t)b, s_cat(num_s((uint32_t)a, t), ":"));
    }
}
static const char *comp_val_text(int comp, int v, char *t)
{
    switch (comp) {
    case SC_VELOCITY: return VEL_VAL[clampi(v, 0, 9)];
    case SC_RAMP_UP: case SC_RAMP_DOWN: case SC_RANDOM: return RAMP_VAL[clampi(v, 0, 9)];
    case SC_PORTAMENTO: return PORTA_VAL[clampi(v, 0, 9)];
    case SC_BEND: return BEND_VAL[clampi(v, 0, 9)];
    case SC_TONALITY: return TON_VAL[clampi(v, 0, 9)];
    case SC_JUMP:                                    /* "to 13", "to 49": a beat (0..3, then 10..21), or a move */
        if (v >= 4 && v <= 9)
            return JUMP_MOVE[v - 4];
        num_s((uint32_t)(v < 4 ? v * 4 : (clampi(v, 10, JUMP_LAST) - 6) * 4) + 1u, s_cat(t, "to "));
        return t;
    case SC_SKIP_LOCK: case SC_SKIP_COMP: case SC_SKIP: {   /* "on 4", "not 4", "1st/4" of the cycle */
        int n = (int)SK_N((uint32_t)v), m = (int)SK_MODE((uint32_t)v);
        char *e;
        if (n >= SC_RANDOM_VAL)
            return "random";
        e = s_cat(t, m == 0 ? "on " : m == 1 ? "not " : "1st of ");
        num_s((uint32_t)n + 1u, e);
        return t;
    }
    default:
        if (v >= SC_RANDOM_VAL)
            return "random";
        num_s((uint32_t)v + 1u, t);
        return t;
    }
}
/* ---- a kit's hits: each sound on a step its own chance and tricks (pattern.h LANE_HIT0 lanes into pats.hit) ---- */
static void hit_of_step(const step_t *st, hit_t *h)
{
    h->cond = st->cond;
    h->comp = st->comp;
    h->cval = st->cval;
    h->comp2 = st->comp2;
    h->cval2 = st->cval2;
}
/* sound snd (0..)'s settings on step s of pattern pt, as it plays: its own, or the step's */
static void hit_get(int trk, int pt, int s, int snd, hit_t *h)
{
    const hit_t *o = step_hit(&pats, trk, pt, s, snd);
    if (o)
        *h = *o;
    else
        hit_of_step(&pats.pat[trk][pt].st[s], h);
}
/* a held step's chance and tricks are the sound shown's, on a kit step it is on; else the step's own (a synth's, an
 * empty kit step's: its jump) */
static int hit_edits(int trk, int s) { return is_drum(trk) && kit_has(&tpat(trk)->st[s], ui.dsound % NDRUM + 1); }
static void hit_view(int trk, int s, hit_t *h)   /* what the knobs show for step s */
{
    if (hit_edits(trk, s))
        hit_get(trk, cur_pat(trk), s, ui.dsound % NDRUM, h);
    else
        hit_of_step(&tpat(trk)->st[s], h);
}
/* what step s's cell marks: a chance on any of its sounds; the tricks of the sound shown, or of the first with any */
static void hit_marks(int trk, int pt, int s, const step_t *st, hit_t *h)
{
    uint32_t m = is_drum(trk) ? kit_mask(st) : 0u;
    int cond = 0, shown = ui.dsound % NDRUM;
    hit_t o;
    hit_of_step(st, h);
    if (!m)
        return;
    h->comp = h->comp2 = 0;
    if (m >> shown & 1u) {
        hit_get(trk, pt, s, shown, &o);
        h->comp = o.comp, h->cval = o.cval, h->comp2 = o.comp2, h->cval2 = o.cval2;
    }
    for (; m; m &= m - 1u) {
        hit_get(trk, pt, s, __builtin_ctz(m), &o);
        cond |= o.cond;
        if (!h->comp && !h->comp2)
            h->comp = o.comp, h->cval = o.cval, h->comp2 = o.comp2, h->cval2 = o.cval2;
    }
    h->cond = (uint8_t)(cond ? 1 : 0);
}
static int hit_same(const hit_t *a, const hit_t *b)
{
    return a->cond == b->cond && a->comp == b->comp && a->cval == b->cval && a->comp2 == b->comp2 && a->cval2 == b->cval2;
}
/* the entries no lane points at (the undo's pattern's lanes count: undoing brings them back) are free again */
static void hits_gc(void)
{
    uint64_t used = 0;
    for (int k = 0; k < NLANE; k++)
        if (pats.lane[k].used && lane_is_hit(pats.lane[k].param))
            for (int i = 0; i < NSTEP; i++)
                if (pats.lane[k].val[i] < NHIT)
                    used |= 1ull << pats.lane[k].val[i];
    if (undo.valid && undo.kind == U_PAT && undo.nl != 0xFFu)
        for (int k = 0; k < undo.nl && k < UNDO_LANES; k++)
            if (lane_is_hit(undo.u.p.lane[k].param))
                for (int i = 0; i < NSTEP; i++)
                    if (undo.u.p.lane[k].val[i] < NHIT)
                        used |= 1ull << undo.u.p.lane[k].val[i];
    for (int k = 0; k < NHIT; k++)
        if (!(used >> k & 1u))
            memset(&pats.hit[k], 0, sizeof pats.hit[k]);
}
/* the entry of these settings (an entry once pointed at never changes: the interrupt may be reading it), -1 none left */
static int hit_intern(const hit_t *h)
{
    int k, f = -1;
    for (k = 0; k < NHIT; k++) {
        if (hit_same(&pats.hit[k], h))
            return k;
        if (f < 0 && hit_plain(&pats.hit[k]))
            f = k;
    }
    if (f < 0) {
        hits_gc();
        for (k = 0; k < NHIT && f < 0; k++)
            if (hit_plain(&pats.hit[k]))
                f = k;
    }
    if (f >= 0)
        pats.hit[f] = *h;
    return f;
}
static void lane_drop_empty(int ln)              /* a lane with no step left on it goes (the lanes are few) */
{
    for (int i = 0; i < NSTEP; i++)
        if (pats.lane[ln].val[i] != LOCK_NONE)
            return;
    lane_clear(pats.lane, ln);
}
enum { HIT_OK, HIT_NO_LANE, HIT_NO_ROOM };
/* sound snd (0..) on step s of pattern pt gets these settings (plain: none of its own). The step's own settings,
 * while it still has any, go to each of its sounds first (as their own), so the others keep playing as they did */
static int hit_put(int trk, int pt, int s, int snd, const hit_t *h)
{
    step_t *st = &pats.pat[trk][pt].st[s];
    hit_t sh;
    int ln, k;
    hit_of_step(st, &sh);
    if (!hit_plain(&sh)) {
        uint32_t m = kit_mask(st);
        if ((k = hit_intern(&sh)) < 0)
            return HIT_NO_ROOM;
        for (; m; m &= m - 1u)
            if (!step_hit(&pats, trk, pt, s, __builtin_ctz(m))) {
                if ((ln = lane_alloc(pats.lane, trk, pt, (int)(LANE_HIT0 + (unsigned)__builtin_ctz(m)))) < 0)
                    return HIT_NO_LANE;
                pats.lane[ln].val[s] = (uint8_t)k;
            }
        st->comp = st->comp2 = SC_NONE;           /* (every sound has it now: the step's own goes) */
        st->cval = st->cval2 = 0;
        st->cond = 0;
    }
    ln = lane_find(pats.lane, trk, pt, (int)(LANE_HIT0 + (unsigned)snd));
    if (hit_plain(h)) {
        if (ln >= 0) {
            pats.lane[ln].val[s] = LOCK_NONE;
            lane_drop_empty(ln);
        }
        return HIT_OK;
    }
    if ((k = hit_intern(h)) < 0)
        return HIT_NO_ROOM;
    if (ln < 0 && (ln = lane_alloc(pats.lane, trk, pt, (int)(LANE_HIT0 + (unsigned)snd))) < 0)
        return HIT_NO_LANE;
    pats.lane[ln].val[s] = (uint8_t)k;
    return HIT_OK;
}
static void hit_sound_off(int trk, int pt, int s, int snd)   /* a sound taken off a step: its settings there go */
{
    int ln = lane_find(pats.lane, trk, pt, (int)(LANE_HIT0 + (unsigned)snd));
    if (ln >= 0) {
        pats.lane[ln].val[s] = LOCK_NONE;
        lane_drop_empty(ln);
    }
}

static void step_knobs(knob_t *k)
{
    const tpat_t *tp = tpat(sel_trk());
    const step_t *st = &tp->st[step_shown(ui.held_first)];
    char t[16];
    if (is_drum(sel_trk())) {                         /* a kit: the velocity of the sound shown (its own, or the step's) */
        int own = kit_has(st, ui.dsound % NDRUM + 1) ? step_vel(pats.lane, sel_trk(), cur_pat(sel_trk()),
                                                              step_shown(ui.held_first), ui.dsound % NDRUM) : 0;
        kn(&k[0], "VEL", own ? own : st->vel, 127);
    } else
        kn(&k[0], "VEL", st->vel, 127);
    t[0] = 0;
    step_len_text(st->len, t, tp->nlen);
    kv(&k[1], "LENGTH", t, st->len, step_len_code(tp->len * 4));
    t[0] = 0;
    {
        hit_t h;
        hit_view(sel_trk(), step_shown(ui.held_first), &h);   /* (a kit: the sound shown's) */
        cond_text(h.cond, t);
    }
    kv(&k[2], "CHANCE", t, 0, 0);
    if (is_drum(sel_trk())) {                         /* the step's note: semitones from the keys' pitch */
        int semi = step_tune(pats.lane, sel_trk(), cur_pat(sel_trk()), step_shown(ui.held_first));
        char *e = t;
        if (semi > 0)
            e = s_cat(e, "+");
        s_cat(num_i(semi, e), " st");
        kv(&k[3], "PITCH", t, semi + TUNE_MAX, 2 * TUNE_MAX);
    } else {                                          /* the step's (first) note */
        int n = 0;
        for (int i = 0; i < SN && !n; i++)
            n = st->note[i];
        if (n && pl_type(sel_trk()) == PL_RAND)
            s_cat(t, "on");                           /* (a random slot: its pitch is drawn) */
        else if (n)
            note_name(n, t);
        else
            s_cat(t, "-");
        kv(&k[3], "PITCH", t, n, 127);
    }
}
/* HOME + a step held: the strip shows the step's components and their values */
static void comp_knobs(knob_t *k)
{
    hit_t h, *st = &h;                                /* (a kit: the sound shown's) */
    char t[16];
    hit_view(sel_trk(), step_shown(ui.held_first), &h);
    kv(&k[0], "TRICK 1", st->comp ? COMP_NAME[st->comp % NSC] : "-", 0, 0);
    kv(&k[1], "VALUE", st->comp ? comp_val_text(st->comp, st->cval, t) : "-", 0, 0);
    kv(&k[2], "TRICK 2", st->comp2 ? COMP_NAME[st->comp2 % NSC] : "-", 0, 0);
    kv(&k[3], "VALUE", st->comp2 ? comp_val_text(st->comp2, st->cval2, t) : "-", 0, 0);
}
/* the lock editor: a step held + EDIT / ENV / FX / LFO: that page's four, with the lock's value ("-": none) */
static void lock_knobs(knob_t *k)
{
    int trk = sel_trk(), pg = ui.lockpg - 1, step = step_shown(ui.held_first);   /* (lock pages are the track's own) */
    for (int i = 0; i < 4; i++) {
        int p = tp_page_param(pg, i, trk), ln;
        if (p < 0)
            continue;
        ktp(&k[i], trk, p);
        if (!k[i].name)
            continue;
        ln = lane_find(pats.lane, trk, cur_pat(trk), p);
        if (ln < 0 || pats.lane[ln].val[step] == LOCK_NONE)
            kv(&k[i], k[i].name, "-", 0, 0);
        else
            ktp_val(&k[i], trk, p, pats.lane[ln].val[step]);
    }
}
/* an FX track's TP_ENGINE byte: the pick (fx.h), and how the screen names it: "chorus", or "reverb hall" */
static int fx_pick_of(int trk) { return proj.trk[trk].par[TP_ENGINE] % NFXPICK; }
static const char *fx_label(int pick)
{
    static char t[24];
    if (fx_pick_type(pick) != FXT_REVERB)
        return fx_pick_name(pick);
    s_cat(s_cat(s_cat(t, FXT_NAME[FXT_REVERB]), " "), fx_pick_name(pick));
    return t;
}
static void aux_engine_knobs(knob_t *k, int trk)
{
    const uint8_t *par = proj.trk[trk].par;
    if (trk == AUX_BRAIN) {
        kname(&k[0], BRAIN_KNOB[0], NOTE_NAME, par[TP_E2], 12);
        kname(&k[1], BRAIN_KNOB[1], BS_NAME, par[TP_E3], NBS);
        kname(&k[2], BRAIN_KNOB[2], LINK_NAME, par[TP_E4], 9);
    } else if (trk == AUX_PUNCH || trk == AUX_WEAVE) {
        kname(&k[0], "latch", ONOFF, par[TP_E1], 2);
    } else {
        int pick = par[TP_ENGINE] % NFXPICK;
        for (int i = 0; i < 4; i++)
            kn(&k[i], short_name(fx_knob_name(pick, i)), par[TP_E1 + i] * 100 / 128, 99);
    }
}
static void page_knobs(knob_t *k)
{
    int trk = sel_trk(), p = ui.page;
    tpat_t *tp = tpat(trk);
    char t[16], *e;
    memset(k, 0, 4 * sizeof k[0]);
    for (int i = 0; i < 4; i++)
        k[i].live = -1;
    if (rec_settings()) {                            /* REC held: the record settings */
        static const char *const CIN[NCIN] = {"off", "1 bar", "2 bars", "on note"};
        static const char *const MODE[2] = {"overdub", "replace"};
        kname(&k[0], "CLICK", METRO_NAME, proj.metronome, 3);
        kname(&k[1], "COUNT", CIN, proj.rsv[0] < NCIN ? proj.rsv[0] : 0, NCIN);
        if (is_seqt(trk))
            kn(&k[2], "QUANT", tp->quant, 100);
        kname(&k[3], "MODE", MODE, proj.rsv[2] & 1u, 2);
        return;
    }
    if (ui.lockpg) {
        lock_knobs(k);
        return;
    }
    if (steps_page(p) && ui.trickpg && is_seqt(trk)) {
        comp_knobs(k);
        return;
    }
    if (steps_page(p) && ui.held && is_seqt(trk)) {
        step_knobs(k);
        return;
    }
    if (trk == AUX_BRAIN && p == P_SUB) {            /* the Follow page: the track whose notes set the chords */
        kname(&k[0], "FOLLOW", LINK_NAME, proj.trk[AUX_BRAIN].par[TP_E1] % 9u, 9);
        return;
    }
    switch (p) {
    case P_ARRANGE:
        tempo_text(t);
        kv(&k[0], "TEMPO", t, (int)proj.tempo_x10 / 10 - 40, 180);
        kname(&k[1], "GROOVE", GR_NAME, proj.groove, NGROOVE);
        if (is_seqt(trk)) {
            static char pn[12];                      /* the track's pattern in this scene ("T2 PAT"; cued: "1>2";
                                                      * THROW's name too long for a knob: "PATTERN") */
            char c[8], *ce;
            kn(&k[2], "AMOUNT", tp->groove, 0);
            pn[0] = 0;
            if (s_len(TRACK_NAME[trk]) > 4)
                s_cat(pn, "PATTERN");
            else
                s_cat(s_cat(pn, TRACK_NAME[trk]), " PAT");
            if (playing() && seq.pcue[trk] < NPATT && seq.pcue[trk] != cur_pat(trk)) {
                ce = num_s((uint32_t)cur_pat(trk) + 1u, c);
                *ce++ = '>';
                num_s((uint32_t)seq.pcue[trk] + 1u, ce);
                kv(&k[3], pn, c, seq.pcue[trk] + 1, NPATT);
            } else
                kn(&k[3], pn, cur_pat(trk) + 1, NPATT);
        }
        break;
    case P_SONG: {
        const song_t *sg = &pats.song;
        int cur = ui.name_slot < sg->len ? ui.name_slot : sg->len;
        kn(&k[0], "SLOT", cur + 1, NSONG);
        if (cur < sg->len) {
            kn(&k[1], "SCENE", sg->slot[cur].scene + 1, NSCENE);
            kn(&k[2], "BARS", sg->slot[cur].rep, SONG_BARS);
        } else {
            kv(&k[1], "SCENE", "-", 0, 0);
            kv(&k[2], "BARS", "-", 0, 0);
        }
        kv(&k[3], "AT END", sg->loop ? "loop" : "stop", 0, 0);
        break;
    }
    case P_STEPS:
        if (trk == AUX_WEAVE) {                      /* its pattern (the scene's) and length; a step held: the step's */
            const wvpat_t *wp = &pats.weave[pats.scene[cur_scene()].weave % NPATT];
            kn(&k[0], "PATTERN", pats.scene[cur_scene()].weave % NPATT + 1, NPATT);
            if (ui.wv_step < NSTEP)
                kn(&k[1], "STEP LEN", wp->st[ui.wv_step].len ? wp->st[ui.wv_step].len : 1, NSTEP);
            else
                kn(&k[1], "LENGTH", wp->len, NSTEP);
            break;
        }
        if (!is_seqt(trk))
            break;
        if (playing() && seq.pcue[trk] < NPATT && seq.pcue[trk] != cur_pat(trk)) {   /* cued: "1>2" until the bar */
            char c[8], *ce = num_s((uint32_t)cur_pat(trk) + 1u, c);
            *ce++ = '>';
            num_s((uint32_t)seq.pcue[trk] + 1u, ce);
            kv(&k[0], "NEXT", c, seq.pcue[trk] + 1, NPATT);
        } else
            kn(&k[0], "PATTERN", cur_pat(trk) + 1, NPATT);
        kn(&k[1], "LENGTH", tp->len, NSTEP);
        kname(&k[2], "SCALE", TS_NAME, tp->scale, NTS);
        kn(&k[3], "GROOVE", tp->groove, 0);
        break;
    case P_BAR:
        if (!is_seqt(trk))
            break;
        kn(&k[0], "QUANT", tp->quant, 100);
        e = num_s(tp->nlen, t);
        s_cat(e, "%");
        kv(&k[1], "NOTE", t, tp->nlen, 100);
        kn(&k[2], "GROOVE", tp->groove, 0);
        kn(&k[3], "SHAPE", tp->shape, 100);
        break;
    case P_ENGINE:
        if (is_aux(trk))
            aux_engine_knobs(k, trk);
        else
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_FX1: case P_FX2: {                        /* an effect: its four knobs (the type: PRESETS, SEL) */
        int ft = P_FX1 == p ? AUX_FX1 : AUX_FX2, pick = fx_pick_of(ft);
        for (int i = 0; i < 4; i++)
            kn(&k[i], short_name(fx_knob_name(pick, i)), proj.trk[ft].par[TP_E1 + i] * 100 / 128, 99);
        break;
    }
    case P_CHARACTER: {                              /* the instrument's character stage */
        int it = inst_trk();
        for (int i = 0; i < 4; i++)
            ktp(&k[i], it, tp_page_param(p, i, it));
        break;
    }
    case P_SENDS: {                                  /* the instrument's sends (a kit's: the key's), pan and level */
        int it = inst_trk();
        ktp(&k[0], it, is_drum(it) ? TP_DRUM(ui.dsound % DL_NSOUND, DP_SEND1) : TP_SEND1);
        ktp(&k[1], it, is_drum(it) ? TP_DRUM(ui.dsound % DL_NSOUND, DP_SEND2) : TP_SEND2);
        ktp(&k[2], it, TP_PAN);
        ktp(&k[3], it, TP_LEVEL);
        k[0].name = "fx I";
        k[1].name = "fx II";
        break;
    }
    case P_PLAY:
        if (is_fx(trk)) {
            const uint8_t *par = proj.trk[trk].par;
            kn(&k[0], "HP", par[TP_FENV], 127);
            kn(&k[1], "LP", par[TP_CUT], 127);
            if (trk == AUX_FX1)
                kn(&k[2], "TO FX II", par[TP_SEND2], 127);
            if (fx_pick_type(fx_pick_of(trk)) == FXT_DELAY)
                kn(&k[3], "WIDTH", par[TP_FX_WIDTH], 127);
        } else if (!is_aux(trk))
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_WEAVE: {                                  /* the track's weave: the track and pattern woven in, the rule */
        int f = wv_from(trk);
        static char wt[8];
        kv(&k[0], "WITH", f ? TRACK_NAME[f - 1] : "off", f, NSEQT);
        num_s((uint32_t)wv_pat(trk) + 1u, s_cat(wt, "P"));
        kv(&k[1], "PATTERN", f ? wt : "-", wv_pat(trk), NPATT - 1);
        kname(&k[2], "RULE", WEAVE_NAME, wv_rule(trk), NWV);
        if (f) {
            static const char *const PLAYS[2] = {"always", "on WEAVE"};
            kname(&k[3], "PLAYS", PLAYS, wv_cued(trk), 2);
        }
        break;
    }
    case P_THROWS: {
        static const char *const LATCH[2] = {"off", "on"};
        kname(&k[0], "LATCH", LATCH, proj.trk[AUX_PUNCH].par[TP_E1] & 1u, 2);
        break;
    }
    case P_MIX:
        if (is_fx(trk))
            ktp(&k[3], trk, TP_LEVEL);
        else if (!is_aux(trk)) {                     /* (the conductor and the throws: no sound, no knobs; a kit's
                                                      * sends are its keys': FX, Sends) */
            if (!is_drum(trk)) {
                ktp(&k[0], trk, TP_SEND1);
                ktp(&k[1], trk, TP_SEND2);
            }
            ktp(&k[2], trk, TP_PAN);
            ktp(&k[3], trk, TP_LEVEL);
            k[0].name = "FX I";
            k[1].name = "FX II";
        }
        break;
    case P_MASTER: case P_MASTER2: case P_MASTER3:
        for (int i = 0; i < 4; i++) {
            int m = mst_base(p) + i;
            if (m == MS_RSV3)
                continue;                            /* (EQ: three knobs) */
            if (m == MS_CTYPE)
                kname(&k[i], MS_NAME[m], MC_NAME, proj.master[m] % NMC, NMC);
            else if (m >= MS_CAMT && m <= MS_CMIX)
                kmc(&k[i], m);
            else if (m == MS_PUMP) {
                char t[12];
                pump_text(proj.master[m], t);
                kv(&k[i], MS_NAME[m], t, proj.master[m], 127);
            } else
                kn(&k[i], MS_NAME[m], proj.master[m], 127);
        }
        break;
    case P_GLFO: {                                   /* the shared LFO (synth.h GS_*, GD_*): the conductor's slots */
        const uint8_t *g = proj.trk[AUX_BRAIN].par;
        static char rt[12];
        glfo_rate_text(g[TP_LSPEED], rt);
        kname(&k[0], "SHAPE", GLFO_SHAPE, g[TP_LTYPE] % (NGS + 1), NGS + 1);
        kv(&k[1], "RATE", rt, g[TP_LSPEED], 127);
        kn(&k[2], "AMOUNT", (int)g[TP_LAMT] - 64, 0);
        kname(&k[3], "DEST", GLFO_DEST, g[TP_LDEST] % NGD, NGD);
        break;
    }
    case P_SETUP:
        kname(&k[0], "LIGHTS", LIGHTS_NAME, proj.lights, 3);
        kname(&k[1], "CLOCK", CLOCK_NAME, CLOCK_POS[proj.clock % SEQ_NCLK], SEQ_NCLK);
        kname(&k[2], "NOTES", ONOFF, proj.send_notes != 0, 2);
        break;
    case P_STATUS:
        kv(&k[0], "CORES", proj.one_core ? "one" : "two", 0, 0);
        break;
    default:                                         /* the sound pages: every knob through tp_info_par */
        if (!is_aux(trk))
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    }
}
static int knobs_shown(void)
{
    knob_t k[4];
    page_knobs(k);
    return k[0].name || k[1].name || k[2].name || k[3].name;
}
/* the knob strip (a hairline over it): four 57 px columns from the 8 px margin, each its circled number and name
 * (F_LAB, dim, tracked), its value large (F_VAL), and a ruler with ticks under it, a vermilion pointer at the value.
 * The value turned now in blue; a lock's in vermilion; a lock or a motion holding it now: a vermilion tick on the
 * ruler. A value too wide for the column steps down by rule: a number and its unit (the unit small after it), 15 px
 * bold, 12, two lines of 12 */
#define KS_X 8
#define KS_P 57                                      /* (the columns' pitch; the ruler 48, a value up to 52) */
#define KS_VW 50
#define KS_LX 15                                     /* the name after its number, up to the next column's number */
#define KS_GAP 6                                     /* clear air from a label's ink to the next column's circled number: 5 px and the glyph's soft edge */
#define KS_AIR 8                                     /* the air a whole name keeps if it has a short form */
#define KS_LW (KS_P - KS_LX - KS_GAP)
#define KS_RY 47                                     /* the ruler's line */
/* a knob's name too long for its column, its standard short form: the same on every page (the screen reader says the
 * whole name: it reads knob_t's, never these). A name that fits, spaced or not, stays whole. The manual's table */
static const char *const KNOB_SHORT[][2] = {
    {"PATTERN", "PATT"}, {"REPEATS", "RPTS"}, {"RELEASE", "REL"}, {"SUSTAIN", "SUS"}, {"VIBRATO", "VIB"},
    {"MELODIC", "MEL"}, {"ENVELOPE", "ENV"}, {"KEYTRACK", "KEYTRK"}, {"KEYSCALE", "KEY SC"}, {"RHYTHM %", "RHY %"},
    {"OCT DOWN", "OCT DN"}, {"STEP LEN", "ST LEN"}, {"LOOP LEN", "LP LEN"}, {"LOOP START", "LP ST"},
    {"OSC 2 FINE", "O2 FINE"}, {"MIDI OUT", "MIDI"}, {"LOW CUT", "LO CUT"}, {"ENV MOD", "ENV MD"},
    {"FREQUENCY", "FREQ"}, {"MODULATION", "MOD"}, {"FEEDBACK", "FDBK"}, {"REVERSE", "REV"}, {"MOD DECAY", "FM DEC"},
    {"LENGTH", "LEN"}, {"AMOUNT", "AMT"}, {"ATTACK", "ATK"}, {"RANDOM", "RND"}, {"ACC DEC", "AC DEC"},
    {"LOWCUT", "LO CUT"}, {"PITCH %", "PIT %"}, {"SMOOTH", "SMTH"},
};
static int32_t label_ink_end(const char *s, int32_t sp)   /* the last inked pixel of F_LAB text drawn at 0, letters sp apart */
{
    int32_t x = 0, e = 0;
    for (; *s; s++) {
        uint32_t gi = loom_glyph(F_LAB, (uint8_t)*s);
        const uint8_t *bx = F_LAB->box[gi];
        e = x - (int32_t)F_LAB->pad + (int32_t)bx[0] + (int32_t)bx[2] - 1;
        x += F_LAB->adv[gi] + sp;
    }
    return e;
}
static int label_clear(const char *s, int32_t sp)    /* clear pixels from the label's ink to the next column's circled number */
{
    return KS_P - (KS_LX + label_ink_end(s, sp) + 1);
}
static const char *knob_short_of(const char *nm)     /* nm (capitals)'s standard short form, or 0 */
{
    for (uint32_t k = 0; k < sizeof KNOB_SHORT / sizeof KNOB_SHORT[0]; k++)
        if (s_eq(nm, KNOB_SHORT[k][0]))
            return KNOB_SHORT[k][1];
    return 0;
}
static int knob_label_fit(const char *name, char *nm, int32_t *sp)   /* -> 1: nm (its form shown) fits, spaced sp */
{
    up_copy(nm, name, 24);
    for (int pass = 0; pass < 2; pass++) {           /* whole, then its short form */
        const char *sh = knob_short_of(nm);
        if (label_clear(nm, 1) >= KS_AIR)
            return *sp = 1, 1;
        if (label_clear(nm, 0) >= KS_AIR || (label_clear(nm, 0) >= KS_GAP && !sh))
            return *sp = 0, 1;                       /* (a name with a short form gives way to it before it crowds the next number) */
        if (!sh)
            break;
        up_copy(nm, sh, 24);
    }
    return *sp = 0, label_clear(nm, 0) >= KS_GAP;
}
static void knob_label(int32_t x, const char *name, uint16_t c)
{
    char nm[24];
    int32_t sp;
    if (knob_label_fit(name, nm, &sp))
        tr_text(x, 9, F_LAB, nm, c, sp);
    else
        text_fit(x, 9, F_LAB, nm, KS_LW, c);         /* (not in the table: the audit says CUT TO FIT) */
}
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
static void knob_names_audit(void)                   /* every parameter's name on every engine, kit model, mode: its
                                                      * label fits its column, whole or by KNOB_SHORT */
{
    static uint8_t par[NTP];
    char nm[24];
    int32_t sp;
    for (int e = 0; e < NENGINE; e++)
        for (int v = 0; v < 40; v++) {
            memset(par, 0, sizeof par);
            par[TP_ENGINE] = (uint8_t)e, par[TP_E4] = (uint8_t)v, par[TP_PLAYER] = (uint8_t)(v % NPL);
            par[TP_LTYPE] = (uint8_t)v, par[TP_CTYPE] = (uint8_t)(v % NCT), par[TP_FTYPE] = (uint8_t)v;
            for (int k = 0; k < DL_NSOUND; k++)
                par[TP_DRUM(k, DP_MODEL)] = (uint8_t)(v % NDM);
            for (int p = 0; p < NTP; p++) {
                const tp_info_t *in = tp_info_par(p, par);
                if (in->name && in->name[0] && !s_eq(in->name, "-") && !knob_label_fit(short_name(in->name), nm, &sp)) {
                    printf("  FAIL knob label too wide: %s (%s)\n", in->name, nm);
                    msg_too_wide++;
                }
            }
        }
    for (int p = 0; p < NFXPICK; p++)
        for (int k = 0; k < 4; k++)
            if (!knob_label_fit(fx_knob_name(p, k), nm, &sp)) {
                printf("  FAIL knob label too wide: %s\n", fx_knob_name(p, k));
                msg_too_wide++;
            }
}
#endif
static void knob_value(int32_t x, const char *v, uint16_t vc)
{
    uint32_t sp = s_find_sp(v);
    if (text_w(F_VAL, v) <= KS_VW) {
        cv_text(x, 22, F_VAL, v, vc);
        return;
    }
    if (v[sp] && s_len(v + sp + 1) <= 3 && (v[0] <= '9' || v[0] == '-' || v[0] == '+')) {   /* 331 ms, 2.9 kHz */
        char a[16];
        uint32_t i;
        for (i = 0; i < sp && i < sizeof a - 1u; i++)
            a[i] = v[i];
        a[i] = 0;
        if (text_w(F_VAL, a) + 2 + text_w(&FONT_XS, v + sp + 1) <= KS_VW) {
            int32_t e = cv_text(x, 22, F_VAL, a, vc);
            cv_text(e + 2, 22 + 17 - 12, &FONT_XS, v + sp + 1, vc);   /* (on the number's baseline) */
            return;
        }
    }
    if (text_w(&FONT_B, v) <= KS_VW)
        cv_text(x, 26, &FONT_B, v, vc);
    else if (text_w(&FONT_XS, v) <= KS_VW)
        cv_text(x, 27, &FONT_XS, v, vc);
    else if (v[sp]) {                                /* two words: two lines */
        char a[24];
        uint32_t i;
        for (i = 0; i < sp && i < sizeof a - 1u; i++)
            a[i] = v[i];
        a[i] = 0;
        text_fit(x, 16, &FONT_XS, a, KS_VW, vc);
        text_fit(x, 29, &FONT_XS, v + sp + 1, KS_VW, vc);
    } else
        text_fit(x, 27, &FONT_XS, v, KS_VW, vc);
}
static void knobs_strip(const knob_t *k, int now_touch)
{
    cv_rect(8, 3, 224, 1, K_TEXT);
    for (int i = 0; i < 4; i++) {
        int32_t x = KS_X + i * KS_P;
        int hot = now_touch && ui.touched == i, lock = ui.lockpg && !s_eq(k[i].val, "-");
        uint16_t vc = hot ? K_SEL : lock ? K_REC : K_TEXT;
        char n[2] = {(char)('1' + i), 0};
        if (!k[i].name)
            continue;
        circ_num(x + 6, 15, n, hot ? K_SEL : K_TEXT, K_BG);
        knob_label(x + KS_LX, k[i].name, hot ? K_SEL : K_DIM);
        knob_value(x, k[i].val, vc);
        ruler(x, KS_RY, 48, k[i].num, k[i].max, hot ? K_SEL : K_REC);
        if (k[i].max > 0 && k[i].live >= 0) {        /* a lock or a motion holds it now: where */
            int32_t lx = x + 47 * clampi(k[i].live, 0, k[i].max) / k[i].max;
            cv_rect(lx - 1, KS_RY - 5, 3, 5, K_BG);
            cv_rect(lx, KS_RY - 6, 1, 7, K_REC);
        }
    }
}
static void draw_knobs(void)
{
    knob_t k[4];
    uint32_t h = hash_u(2166136261u, ui.page | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    page_knobs(k);
    for (i = 0; i < 4; i++) {
        h = hash_s(h, k[i].val);
        h = hash_s(h, k[i].name);
        h = hash_u(h, (uint32_t)k[i].num << 8 | (uint32_t)(k[i].name != 0) | (uint32_t)(k[i].live + 1) << 20 |
                          (uint32_t)k[i].max << 12);
    }
    h = hash_u(h, (uint32_t)now_touch | (uint32_t)ui.lockpg << 1);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    if (k[0].name || k[1].name || k[2].name || k[3].name)
        knobs_strip(k, now_touch);
    cv_blit(0, KNB_Y);
}

/* ------------------------------------------- the speech's crumbs, hints, cards --- */
static void track_crumb(char *t)
{
    int trk = sel_trk();
    char *e = s_cat(t, TRACK_NAME[trk]);
    if (!is_aux(trk)) {
        e = s_cat(e, " ");
        e = s_cat(e, EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE]);
    }
    if (is_seqt(trk)) {
        e = num_s((uint32_t)cur_pat(trk) + 1u, s_cat(e, ">P"));
        if (playing() && seq.pcue[trk] < NPATT && seq.pcue[trk] != cur_pat(trk))   /* cued: it comes at the bar */
            e = num_s((uint32_t)seq.pcue[trk] + 1u, s_cat(e, ">next P"));
    }
    num_s((uint32_t)cur_scene() + 1u, s_cat(e, ">S"));
}
static int song_bars(int n, uint16_t *at, char *lt);
static char *song_time(int bars, char *t);
static void page_tag(char *t)
{
    char *e;
    *t = 0;
    switch (ui.page) {
    case P_ARRANGE:
        e = num_s((uint32_t)cur_scene() + 1u, s_cat(t, "S"));
        if (seq.cue != 0xFFu && seq.cue != seq.scene)
            num_s((uint32_t)seq.cue + 1u, s_cat(e, ">next S"));
        break;
    case P_SONG: {                                   /* the song's slots, bars, time */
        int n = pats.song.len, bars = song_bars(0, 0, 0);
        e = s_cat(num_s((uint32_t)n, s_cat(t, "song>")), n == 1 ? " slot" : " slots");
        if (n) {
            e = s_cat(num_s((uint32_t)bars, s_cat(e, ">")), " bars>");
            song_time(bars, e);
        }
        break;
    }
    case P_STEPS: case P_BAR:
        track_crumb(t);
        if (ui.lockpg) {
            int lp = ui.lockpg - 1;
            e = num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
            if (is_drum(sel_trk()) && (lp == P_ENGINE || lp == P_PLAY || lp == P_ENV || lp == P_FILTER))
                e = s_cat(s_cat(e, ">"), DL_SOUND_NAME[ui.dsound % DL_NSOUND]);   /* the key the locks reach */
            s_cat(s_cat(e, ">"), page_name_of(lp, sel_trk()));
        } else if (ui.held & (ui.held - 1u))     /* several held: how many (the knobs show the first's values) */
            s_cat(num_s((uint32_t)__builtin_popcount(ui.held), s_cat(t + s_len(t), ">")), " steps");
        else if (ui.held) {
            e = num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
            if (hit_edits(sel_trk(), step_shown(ui.held_first)))   /* a kit: the sound its knobs reach */
                s_cat(s_cat(e, ">"), DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        }
        break;
    case P_ENGINE: case P_PLAY: case P_PLAY2: case P_SUB: case P_ENV: case P_FILTER: case P_ENV2: case P_KITF: case P_LFO:
    case P_LFO2:
        track_crumb(t);
        if (is_drum(sel_trk()))                      /* the key the knobs edit (the kit's own pages: the whole kit) */
            s_cat(s_cat(t + s_len(t), ">"), ui.page == P_ENV2 || ui.page == P_KITF ? "all keys"
                                             : DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        break;
    case P_SENDS:
        s_cat(s_cat(t, TRACK_NAME[inst_trk()]), ">sends");
        break;
    case P_CHARACTER:
        e = s_cat(s_cat(t, TRACK_NAME[inst_trk()]), ">character");
        s_cat(s_cat(e, ">"), CTYPE_NAME(proj.trk[inst_trk()].par));
        break;
    case P_FX1: case P_FX2: {                        /* FX II>delay, FX II>reverb>hall */
        int pick = fx_pick_of(ui.page == P_FX1 ? AUX_FX1 : AUX_FX2);
        e = s_cat(s_cat(s_cat(t, TRACK_NAME[ui.page == P_FX1 ? AUX_FX1 : AUX_FX2]), ">"), FXT_NAME[fx_pick_type(pick)]);
        if (fx_pick_type(pick) == FXT_REVERB)
            s_cat(s_cat(e, ">"), fx_pick_name(pick));
        break;
    }
    case P_MASTER: case P_MASTER2: case P_MASTER3: case P_GLFO: case P_SETUP: case P_STATUS:
        s_cat(t, "all tracks");
        break;
    default:
        track_crumb(t);
        break;
    }
}
/* the footer: what the keys do here; a list, a question, a card: what does what */
static void page_footer(char *t)
{
    int trk = sel_trk();
    *t = 0;
    if (ui.ask) {
        if (plat_ms() < ui.msg_until) {
            s_cat(s_cat(s_cat(t, ui.msg[0]), " "), ui.msg[1]);
            upcase(t);
        } else
            s_cat(t, "SEL yes|HOME no");
        return;
    }
    if (rec_settings()) {                            /* REC held: its knobs, and step record */
        s_cat(t, playing() ? "KNOBS settings|KEYS record" : "KNOBS settings|PLAY record");
        return;
    }
    if ((ui.btn >> B_REC & 1u) && !home_held()) {   /* (playing, a sound page: the knobs record) */
        s_cat(t, "KNOBS record moves|KEYS record notes");
        return;
    }
    if (ui.card) {
        s_cat(t, "let go: no change");
        return;
    }
    if (lst.open && lst.n) {
        int k = lst.row[lst.sel].kind;
        if (lst.bt && k == LK_ACTION && lst.row[lst.sel].id == A_BT_DEV) {
            s_cat(t, "SEL connect|HOME back");
            return;
        }
        if (k == LK_NOTE) {
            s_cat(t, "HOME back");
            return;
        }
        s_cat(t, k == LK_ACTION ? "SEL do it|SELECT up, down|HOME back"           /* (the menu's rules: an action, */
               : k == LK_PICK && !pick_sets(lst.row[lst.sel].id) ? "SEL do it|SELECT up, down|ALGO pick|HOME back"
               : "ALGO change|SELECT up, down|SEL back");   /* a choice, a setting or a knob's row) */
        return;
    }
    if (!ui.any_btn && ui.page == P_STEPS && !ui.held && !ui.chordmode) {
        s_cat(t, "WHITE steps|PLAY play|hold a button: help");
        return;
    }
    if (home_held()) {
        s_cat(t, "REC undo|PLAY redo|KNOBS fine");
        return;
    }
    switch (ui.page) {
    case P_ARRANGE:
        for (int i = 0; i < NKEYS; i++)             /* a kit's black key held: its sounds' mutes on the white keys */
            if (ui.kmode[i] == KM_MUTE && (ui.keys >> i & 1u) && is_drum(ui.knote[i] & 0x7F)) {
                s_cat(t, ui.knote[i] & 0x80 ? "WHITE mute a sound|let go: done" : "WHITE mute a sound|let go: mutes the kit");
                return;
            }
        s_cat(t, proj.rsv[2] & SCENES_AT_END ? "WHITE scene at end|BLACK mute|OCT 1-16, 17-32"
                                             : "WHITE scene|twice: now|BLACK mute|OCT 1-16, 17-32");
        break;
    case P_SONG:
        s_cat(t, "WHITE add scene|PLAY song|OCT 1-16, 17-32");
        break;
    case P_STEPS: case P_BAR:
        if (trk == AUX_WEAVE && ui.page == P_STEPS)
            s_cat(t, ui.wv_step < NSTEP ? "BLACK 1-10 tracks|WHITE its end" : "WHITE add or clear");
        else if (!is_seqt(trk))
            s_cat(t, "ALGO a track's steps");
        else if (ui.lockpg)
            s_cat(t, "KNOBS lock|SELECT page|SEL+KNOB clear");
        else if (ui.trickpg)
            s_cat(t, "KNOB 1 3 trick|KNOB 2 4 value");
        else if (ui.chordmode && ui.page == P_STEPS)
            s_cat(t, ui.chord_pool ? "KEYS the pool|SEQ exit" : "KEYS the chord|SEQ exit");
        else if (ui.len_key && (ui.held >> (ui.len_key - 1) & 1u))
            s_cat(t, "let go: length|KNOBS both steps");
        else if (ui.held && is_drum(trk))
            s_cat(t, "BLACK sounds|ARP tricks|EDIT etc.: lock");
        else if (ui.held & (ui.held - 1u))
            s_cat(t, "WHITE more steps|ARP tricks|EDIT etc.: lock|SEQ notes");
        else if (ui.held)
            s_cat(t, "WHITE length|ARP tricks|EDIT etc.: lock|SEQ notes");
        else if (ui.rec_armed)
            s_cat(t, "KEYS write|OCT+ rest|OCT- back|REC done");
        else if (seq.rec || seq.rec_wait)
            s_cat(t, "KEYS record|REC off");
        else if (ui.btn >> B_SEQ & 1u && ui.page == P_BAR)
            s_cat(t, "BLACK 1-4 jump|two: loop|twice: loop one");
        else if (ui.btn >> B_SEQ & 1u)
            s_cat(t, is_drum(trk) ? "KEYS pick a sound" : "KEYS a step's notes|OCT pattern octave");
        else if (is_drum(trk))
            s_cat(t, ui.page == P_BAR ? "WHITE steps|BLACK sound|SEQ held: bars" : "WHITE steps|BLACK sound|SEQ held: sounds");
        else
            s_cat(t, ui.page == P_BAR ? "WHITE steps|BLACK scale|SEQ held: bars" : "WHITE steps|SEQ held: play notes");
        break;
    case P_MIX:
        s_cat(t, "WHITE play|BLACK mute|hold: solo");
        break;
    case P_THROWS:
        s_cat(t, "KEYS throw effects|REC held: record");
        break;
    case P_WEAVE:
        s_cat(t, "BLACK woven track|WHITE its pattern");
        break;
    case P_MASTER3:
        s_cat(t, "SEL pump by, level|KEYS play");
        break;
    case P_GLFO:
        s_cat(t, "BLACK tracks moved|WHITE sound; 12: all");
        break;
    case P_SETUP:
        s_cat(t, "SEL Bluetooth|KEYS play");
        break;
    case P_MASTER: case P_MASTER2: case P_STATUS:
        s_cat(t, "KEYS play");
        break;
    case P_SENDS:
        s_cat(t, "KEYS play");
        break;
    case P_CHARACTER:
        s_cat(t, "PRESET type|KEYS play");
        break;
    case P_FX1: case P_FX2:
        s_cat(t, "PRESET effect|SEL filter|KEYS play");
        break;
    default:
        if (ui.rec_armed)
            s_cat(t, "KEYS write|OCT+ rest|OCT- back|REC done");
        else if (trk == AUX_BRAIN)
            s_cat(t, ui.page == P_PLAY ? "BLACK route track" : ui.page == P_SUB ? "BLACK the track" : "KEYS chord");
        else if (trk == AUX_PUNCH)
            s_cat(t, "KEYS fx|REC record");
        else if (trk == AUX_WEAVE)
            s_cat(t, "WHITE 1-10 weave|REC record");
        else if (is_aux(trk))
            s_cat(s_cat(t, "KEYS play "), TRACK_NAME[ui.last_inst % NTRK]);
        else if (ui.page >= P_PLAYERS && ui.page <= P_PLAYERS3 && pl_type(trk) == PL_STRUM)
            s_cat(t, "KEY strums|KEYS new chord");
        else if (ui.page >= P_PLAYERS && ui.page <= P_PLAYERS3 && pl_type(trk) == PL_ARP)
            s_cat(t, "KEYS held: arpeggio");
        else if (ui.page >= P_PLAYERS && ui.page <= P_PLAYERS3 && pl_type(trk) == PL_RAND)
            s_cat(t, "SEQ + step: pool|WHITE steps: slots");
        else if (is_drum(trk) && ui.page == P_FILTER)
            s_cat(t, "PRESET type|BLACK pick a key");
        else if (is_drum(trk) && (ui.page == P_ENV2 || ui.page == P_KITF))
            s_cat(t, ui.page == P_KITF ? "PRESET on or off|KEYS play" : "KEYS play");
        else if (is_drum(trk) && ui.page <= P_LFO2)
            s_cat(t, !kit_edit_steps() ? "BLACK pick a key|KEYS play"
                     : ui.held ? "KNOBS lock the step|SEL+KNOB clear" : "BLACK pick a key|WHITE its steps");
        else if (ui.page == P_ENGINE)
            s_cat(t, "PRESET next sound|SEL engine");
        else if (ui.page == P_FILTER && !is_acid(trk))
            s_cat(t, "PRESET filter type|KEYS play");
        else if (ui.page == P_ENV)
            s_cat(t, "KEYS play|REC held: record");
        else if (ui.page == P_LFO)
            s_cat(t, "KEYS play|REC held: record");
        else
            s_cat(t, "KEYS play|REC held: record");
        break;
    }
}
static void enc_line(char *t)
{
    int g = page_group(ui.page), n = group_n(g), p = ui.page, trk = sel_trk();
    char *e = t;
    *t = 0;
    if (ui.ask || (lst.open && lst.n) || ui.card)
        return;
    if (home_held()) {
        s_cat(t, "SELECT tempo|PRESET scene|KNOBS fine");
        return;
    }
    if (n > 1)                        /* SELECT names what it turns to where that is the point of the page */
        e = s_cat(t, p == P_CHARACTER ? "SELECT sends" : p == P_SENDS ? "SELECT effects" : p == P_ENV ? "SELECT filter"
                     : p == P_LFO ? "SELECT more" : "SELECT page");
    if (p != P_ARRANGE && p != P_SONG && p != P_THROWS && p < P_MASTER)
        e = s_cat(e, *t ? "|ALGO track" : "ALGO track");
    {
        const char *a = p == P_ARRANGE ? "scene" : p == P_SONG ? "slot"
                        : steps_page(p) && is_drum(trk) ? "sound"
                        : steps_page(p) && is_seqt(trk) ? "pattern"
                        : (p == P_ENGINE || p == P_PLAY || p == P_ENV) && is_drum(trk) ? "key"
                        : p == P_ENGINE && is_fx(trk) ? "effect"
                        : p == P_MIX ? "track" : 0;   /* (Engine, Filter, the FX pages: the footer says it) */
        if (a)
            s_cat(s_cat(e, *t ? "|PRESET " : "PRESET "), a);
    }
}
/* the hint lines: two rows under a hairline, in two fixed columns (8 and 122) when the parts allow, each part a
 * key-cap (the control, F_LAB) and what it does (XS). A row of three, or a part wider than its column, sets its parts
 * evenly across the row instead. The rows: the encoders' hints, then the keys' (or the parts split where they fit) */
#define BOT_LH 16
#define BOT_H (240 - ENC_Y)                         /* (34: the hairline, two rows) */
#define BOT_X 8
#define BOT_W 224
#define BOT_COL 114                                  /* (the second column at BOT_X + BOT_COL) */
#define BOT_JOB 4                                    /* key-cap to its text */
#define BOT_PARTS 8
#define BOT_JOBF (&FONT_XS)
static const char *const CTL_WORD[] = {"SELECT", "ALGO", "PRESET", "SEL", "HOME", "REC", "PLAY", "OCT", "KEYS", "WHITE",
                                       "BLACK", "SAVE", "EDIT", "SEQ", "ENV", "FX", "LFO", "ARP", "GLO", "KNOBS", "KNOB"};
static int ctl_is(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < sizeof CTL_WORD / sizeof CTL_WORD[0]; i++) {
        const char *w = CTL_WORD[i];
        uint32_t j = 0;
        while (j < n && w[j] && w[j] == s[j])
            j++;
        if (j == n && !w[j])
            return 1;
    }
    return 0;
}
static uint32_t ctl_len(const char *s)
{
    uint32_t n = 0, a = 0;
    if (s[0] == 'O' && s[1] == 'C' && s[2] == 'T' && (s[3] == '+' || s[3] == '-') && (s[4] == ' ' || !s[4]))
        return 4;                                    /* "OCT+ rest": one control, its sign part of the name */
    for (;; n++)
        if (s[n] == '+' || s[n] == ' ' || !s[n]) {
            if (n == a || !ctl_is(s + a, n - a))
                return 0;
            if (s[n] != '+')
                return n;
            a = n + 1;
        }
}
/* one part at x (draw 0: its width only): its controls as key-caps ("SEL held + KNOB": a cap each, + between),
 * then its job */
static int32_t bot_part(int32_t x, int32_t y, const char *s, uint32_t n, int draw)
{
    char b[48];
    uint32_t c = ctl_len(s), a = 0, i;
    int32_t x0 = x;
    if (c > n)
        c = 0;
    for (i = 0; c && i <= c; i++)
        if (i == c || (s[i] == '+' && !(i == 3 && s[0] == 'O'))) {     /* ("OCT+": its sign is part of the name) */
            uint32_t k;
            for (k = 0; k < i - a && k < sizeof b - 1u; k++)
                b[k] = s[a + k];
            b[k] = 0;
            x += draw ? key_cap(x, y + 1, b, 0) : cap_w(b);
            if (i < c) {
                if (draw)
                    cv_text(x + 2, y + 1, &FONT_XS, "+", K_TEXT);
                x += text_w(&FONT_XS, "+") + 4;
            }
            a = i + 1;
        }
    if (c) {
        c++;
        if (c >= n)
            return x - x0;
        x += BOT_JOB;
    }
    for (i = 0; c + i < n && i < sizeof b - 1u; i++)
        b[i] = s[c + i];
    b[i] = 0;
    if (draw)
        cv_text(x, y, BOT_JOBF, b, K_TEXT);
    return x + text_w(BOT_JOBF, b) - x0;
}
static int ne0(const char *e)
{
    int n = *e != 0;
    for (; *e; e++)
        n += *e == '|';
    return n;
}
typedef struct { const char *s[BOT_PARTS]; uint32_t n[BOT_PARTS]; int32_t w[BOT_PARTS]; int cnt; } bot_t;
static void bot_split(bot_t *p, const char *t)
{
    while (*t && p->cnt < BOT_PARTS) {
        uint32_t n = 0;
        while (t[n] && t[n] != '|')
            n++;
        p->s[p->cnt] = t;
        p->n[p->cnt] = n;
        p->w[p->cnt] = bot_part(0, 0, t, n, 0);
        p->cnt++;
        t += n;
        if (*t)
            t++;
    }
}
/* the parts fit the grid: four at most, each in its column (a lone last one may take the whole row) */
static int bot_fits(const bot_t *p)
{
    for (int i = 0; i < p->cnt; i++)
        if (p->w[i] > (i % 2 == 0 && i == p->cnt - 1 ? BOT_W : i % 2 ? BOT_W - BOT_COL : BOT_COL - 6))
            return 0;
    return p->cnt <= 4;
}
static void draw_footer(void)
{
    char t[80], e[64];
    int ne;
    bot_t p;
    uint32_t h = hash_u(2166136261u, (uint32_t)(lst.open && !ui.card));
    page_footer(t);
    enc_line(e);
    if (lst.open && !ui.card)
        *e = 0;
    h = hash_s(h, t);
    h = hash_s(h ^ 0x80u, e);
    if (h == ui.sig[4])
        return;
    ui.sig[4] = h;
    memset(&p, 0, sizeof p);
    bot_split(&p, e);
    ne = p.cnt;
    bot_split(&p, t);
    /* more than the grid holds: hints go, the least needed first (the help cards and the screen reader keep them all):
     * a repeated one, "hold a button: help", PRESET's, ALGO's (the track: on every page), SELECT's (the squares under the
     * header say a button has more pages) */
    for (int pass = 0; pass < 5 && !bot_fits(&p); pass++)
        for (int i = 0; i < p.cnt && !bot_fits(&p); i++) {
            const char *q = p.s[i];
            int drop = 0;
            if (pass == 0)
                for (int j = 0; j < i; j++)
                    drop |= p.n[j] == p.n[i] && s_pre(p.s[j], q, p.n[i]);
            if (pass == 1)
                drop = q[0] == 'h' && s_pre(q, "hold a button", 13);
            if (pass == 2)
                drop = i < ne && s_pre(q, "PRESET ", 7);
            if (pass == 3)
                drop = i < ne && s_pre(q, "ALGO ", 5);
            if (pass == 4)
                drop = i < ne && s_pre(q, "SELECT ", 7);
            if (drop) {
                for (int j = i; j + 1 < p.cnt; j++)
                    p.s[j] = p.s[j + 1], p.n[j] = p.n[j + 1], p.w[j] = p.w[j + 1];
                p.cnt--;
                ne -= i < ne;
                i--;
            }
        }
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
    if (!bot_fits(&p)) {
        printf("  FAIL hint lines too wide: %s / %s\n", e, t);
        msg_too_wide++;
    }
#endif
    cv_begin(240, BOT_H, K_BG);
    cv_rect(8, 0, 224, 1, K_LINE);
    for (int i = 0; i < p.cnt && i < 4; i++)         /* the grid: two columns, two rows (the key-caps' tops at 4, 20) */
        bot_part(BOT_X + (i % 2) * BOT_COL, 3 + (i / 2) * BOT_LH, p.s[i], p.n[i], 1);
    cv_blit(0, ENC_Y);
}

/* the help cards: a button held alone for a second says what it does on this page */
#define PG(p) (1u << (p))
#define PG_ANY 0xFFFFFFFFu
#define PG_STEPS (PG(P_STEPS) | PG(P_BAR))
#define PG_SOUND (PG(P_ENGINE) | PG(P_PLAY) | PG(P_PLAY2) | PG(P_SUB) | PG(P_ENV) | PG(P_ENV2) | PG(P_KITF) | PG(P_FILTER) | \
                  PG(P_CHARACTER) | PG(P_LFO) | PG(P_LFO2))
static const struct { uint8_t btn; uint32_t pages; const char *title, *l1, *l2; } CARD[] = {
    {B_SEQ, PG(P_BAR), "Bars", "Hold SEQ, black keys 1-4: jump to a bar at the next bar line.",
     "Two held: loop them. One twice: loop it."},
    {B_SEQ, PG_ANY, "Steps", "The bar's 16 steps on the white keys. Hold SEQ and play the notes,",
     "then press steps to write them. Hold several to edit them all."},
    {B_EDIT, PG_STEPS, "Lock", "With a step held, EDIT, ENV, FX or LFO show their knobs.",
     "Turn one to lock it. SELECT: pages. SEL held + knob: clear."},
    {B_EDIT, PG_ANY, "Engine", "The engine's four knobs. PRESETS: its next sound.",
     "Held: the keys play, ALGORITHM picks the engine. SELECT: pages."},
    {B_ENV, PG_ANY, "Envelope", "The amp's envelope, the filter, its envelope.",
     "PRESETS on Filter: its type. A kit: the key's, then the kit's."},
    {B_FX, PG_ANY, "Effects", "Character, sends, FX I, FX II, then Throws: keys fire effects.",
     "PRESETS: the type or effect. SEL: an FX's filter."},
    {B_LFO, PG_ANY, "LFO", "Type, then its first three knobs.", "Page 2 has the type's fourth and fifth."},
    {B_ARP, PG_ANY, "Players", "Arp, strum, hold or random; last page, Weave.",
     "Random: tap steps as active slots; SEQ picks the pool."},
    {B_ARP, PG_STEPS, "Tricks", "With a step held, ARP: KNOB 1 and 3 pick its two tricks,",
     "KNOB 2 and 4 their values. Let go of the step when done."},
    {B_GLO, PG_ANY, "Mix", "Sends, pan, level. Black: tap mute, hold solo, 2 taps: stays.",
     "SELECT: Master EQ, out, Setup. HOME's SEL: projects."},
    {B_HOME, PG_ANY, "Arrange", "Each track's pattern in the scene. White: scenes, black: mutes.",
     "SEL: projects, scene timing. + REC undoes, + PLAY redoes."},
    {B_SEL, PG_ANY, "Lists", "The page's actions, then its knobs. SEL runs the one picked.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SAVE, PG_STEPS, "Save", "Tap to save the project. A held step's SEL list clears locks.",
     "Hold SAVE and press white key 1 to 3 to save to that slot."},
    {B_SAVE, PG_SOUND, "Save", "Tap to save the project.",
     "Hold SAVE and press a white key: the sound as that user preset."},
    {B_SAVE, PG_ANY, "Save", "Saves now (LOOM-8 saves by itself when stopped and quiet).", "Hold SAVE and press white key 1 to 3 to save to that slot."},
    {B_REC, PG_ANY, "Recording", "Stopped: a tap, then each key writes a step. + PLAY: live.",
     "Playing: hold it and play. Held, the knobs set click, count-in, mode."},
    {B_PLAY, PG(P_SONG), "Play", "Plays the song from the slot picked. PLAY again stops.", "SEL's PLAY SONG: from the top."},
    {B_PLAY, PG_ANY, "Play", "Starts and stops. Twice quickly: from the top.", "HOME + PLAY redoes."},
    {B_OCTDN, PG_STEPS, "Bar", "Shows the bar before. A step held: nudges it earlier.",
     "Step record: the cursor back."},
    {B_OCTUP, PG_STEPS, "Bar", "Shows the next bar. A step held: nudges it later.",
     "Step record: the cursor on."},
    {B_OCTDN, PG_ANY, "Octave", "The keys an octave down.", 0},
    {B_OCTUP, PG_ANY, "Octave", "The keys an octave up.", 0},
};
#define NCARDS (sizeof CARD / sizeof CARD[0])
static int card_find(int btn)
{
    for (uint32_t i = 0; i < NCARDS; i++)
        if (CARD[i].btn == btn && (CARD[i].pages >> ui.page & 1u))
            return (int)i;
    return -1;
}
/* a card's band: the main band; its lines' pitch */
#define CARD_H MAIN_H
#define CARD_LH 17
/* where card i's text ends (the box's top in *top): both lines wrapped at 200 px */
static int32_t card_end(int i, int32_t *top)
{
    int32_t n = wrap_do(0, 0, 200, &FONT_S, CARD[i].l1, 0, 0) / CARD_LH +
                (CARD[i].l2 ? wrap_do(0, 0, 200, &FONT_S, CARD[i].l2, 0, 0) / CARD_LH : 0);
    int32_t bh = 42 + (n - 1) * CARD_LH + (CARD[i].l2 ? 4 : 0), t = 4 + (CARD_H - 8 - bh) / 2;
    if (t < 10)
        t = 10;
    *top = t;
    return t + 30 + n * CARD_LH + (CARD[i].l2 ? 4 : 0);
}
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
static void cards_audit(void)                  /* every card on every page it is shown: its text fits the box */
{
    for (uint32_t i = 0; i < NCARDS; i++) {
        int32_t top;
        if (card_end((int)i, &top) - 1 > CARD_H - 4) {
            printf("  FAIL card too long: %s (%s)\n", CARD[i].title, CARD[i].l1);
            msg_too_wide++;
        }
    }
}
#endif
static void card_paint(int i);
static void draw_card(void)
{
    int i = card_find(ui.card - 1);
    uint32_t h = hash_u(hash_u(2166136261u, 0xCAu), ui.card + ((uint32_t)ui.page << 8));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    for (int32_t off = 0; off < CARD_H; off += 120) {   /* (taller than the canvas, large type: in two goes) */
        int32_t hh = CARD_H - off < 120 ? CARD_H - off : 120;
        cv_begin(240, (uint32_t)hh, K_BG);
        cv_oy = -off, cv_y0 = 0, cv_y1 = hh;
        card_paint(i);
        cv_blit(0, (uint32_t)(MAIN_Y + off));
    }
}
static void card_paint(int i)                        /* a card on the paper: the button's key-cap (inked: held), the
                                                      * title in tracked capitals, a rule, what it does */
{
    int32_t x, y, top = 4 + (CARD_H - 8 - 20) / 2;
    cv_frame(8, 3, 224, CARD_H - 6, 3, K_TEXT, K_CARD);
    if (i >= 0)
        card_end(i, &top);
    else if (top < 10)
        top = 10;
    x = 18 + key_cap(18, top + 3, BTN_LABEL[(ui.card - 1) % NB], 1);
    y = top + 30;
    if (i >= 0) {
        char t[24];
        up_copy(t, CARD[i].title, sizeof t);
        tr_text(x + 9, cap_y(top, 20, &FONT_B), &FONT_B, t, K_TEXT, 1);
        cv_rect(18, y - 6, 204, 1, K_TEXT);
        y = wrap(20, y, 200, &FONT_S, CARD[i].l1, K_TEXT);
        if (CARD[i].l2)
            wrap(20, y + 4, 200, &FONT_S, CARD[i].l2, K_DIM);
    }
}

/* ------------------------------------------------------------- lists --- */
static void list_add(const char *name, const char *val, int kind, int id)
{
    if (lst.n >= LIST_ROWS)
        return;
    lst.row[lst.n].name = name;
    lst.row[lst.n].val[0] = 0;
    if (val)
        for (int i = 0; i < 23 && val[i]; i++)
            lst.row[lst.n].val[i] = val[i], lst.row[lst.n].val[i + 1] = 0;
    lst.row[lst.n].kind = (uint8_t)kind;
    lst.row[lst.n].id = (uint8_t)id;
    lst.n++;
}
static void proj_slot_text(int slot, char *t)     /* "Proj 2", "Proj 3 (open)", "Proj 1 (empty)": a project is its slot
                                                   * (no names) */
{
    char *e = num_s((uint32_t)slot + 1u, s_cat(t, "Proj "));
    if (slot == proj_slot)
        s_cat(e, " (open)");
    else if (!project_exists(slot))
        s_cat(e, " (empty)");
}
/* the kits (a kit track's SELECT list): the factory kits (Loom, then tools/gen_kits.py's), then the
 * user's (project.c user_kit_name) */
#include "kits.h"
#define NFKITS (1 + NGKITS)
#define NKITS (NFKITS + NUSER_KITS)
static const char *kit_name(int k) { return k == 0 ? "Loom" : GKIT_NAME[k - 1]; }
static void kit_label(int k, char *t)              /* "Loom", "K2 deep", "K3 (empty)" */
{
    if (k < NFKITS) {
        s_cat(t, kit_name(k));
        return;
    }
    k -= NFKITS;
    t = num_s((uint32_t)k + 1u, s_cat(t, "K"));
    if (user_kit_name[k][0])
        s_cat(s_cat(t, " "), user_kit_name[k]);
    else
        s_cat(t, " (empty)");
}
static void list_midi_out(int trk)                 /* the track's notes out, on its own channel (with Setup's NOTES) */
{
    list_add(ACTION_NAME[A_MIDI_OUT], proj.trk[trk].par[TP_CHAN] ? "on" : "off", LK_PICK, A_MIDI_OUT);
}
static void list_clip_step(void)                   /* a held step's list, after its notes: copy it, paste a step, its
                                                    * locks cleared */
{
    list_add(ACTION_NAME[A_COPY_STEP], 0, LK_ACTION, A_COPY_STEP);
    if (clip_ok && clip_kind == CK_STEP) {
        list_add(ACTION_NAME[A_PASTE_STEP], 0, LK_ACTION, A_PASTE_STEP);
        list_add(ACTION_NAME[A_PASTE_LOCKS], 0, LK_ACTION, A_PASTE_LOCKS);
    }
    list_add(ACTION_NAME[A_CLEAR_STEP_LOCKS], 0, LK_ACTION, A_CLEAR_STEP_LOCKS);
}
/* the Bluetooth lists (GLO's AUDIO DEVICE and MIDI DEVICE, SEL): a first line that says what to do, then the
 * devices (audio: the remembered ones first), each labelled */
#if BT_LISTS
static char bt_note[32];
static void list_note(const char *t)
{
    bt_note[0] = 0;
    s_cat(bt_note, t);
    list_add(bt_note, 0, LK_NOTE, 0);
}
#if LOOM_BT_AUDIO
static void list_bt_audio(void)
{
    int st = BTA(state)(), n = 0, i, k, look = st == PLAT_BTA_SCANNING;
    list_note("");
    for (i = 0; n < BT_ROWS && BTA(known)(i, bt_nm[n]); i++, n++) {
        bt_ix[n] = (uint8_t)(0x80 | i);
        list_add(bt_nm[n], i == bt.cur && st != PLAT_BTA_OFF && st != PLAT_BTA_SCANNING && st != PLAT_BTA_NONE &&
                           st != PLAT_BTA_FOUND ? bt_state_word(st) : "remembered", LK_ACTION, A_BT_DEV);
    }
    k = n;
    for (i = 0; n < BT_ROWS && BTA(found)(i, bt_nm[n]); i++) {
        int seen = 0;
        for (int j = 0; j < k; j++)                  /* (a remembered one found again: its row already) */
            seen |= s_eq(bt_nm[j], bt_nm[n]);
        if (seen)
            continue;
        bt_ix[n] = (uint8_t)i;
        list_add(bt_nm[n++], "new", LK_ACTION, A_BT_DEV);
    }
    s_cat(bt_note, look ? (n ? "still looking. SEL connects" : "looking for devices...")
                        : n ? "pick a device. SEL connects" : "none found. In pairing mode?");
    list_add(ACTION_NAME[A_BT_LOOK], look ? "looking" : "", LK_ACTION, A_BT_LOOK);
    if (k) {
        char nm[17];
        if (ui.pick_bt >= k)
            ui.pick_bt = 0;
        BTA(known)(ui.pick_bt, nm);
        list_add(ACTION_NAME[A_BT_FORGET], nm, LK_PICK, A_BT_FORGET);
    }
}
#endif
#if FM1B_BLE_CENTRAL
static void list_bt_dev(void)
{
    int n;
    if (btdev_busy()) {
        list_note(btdev_ready() ? "connected. SEL lets it go" : "connecting...");
        list_add(ACTION_NAME[A_BT_DISCONNECT], btdev_name(), LK_ACTION, A_BT_DISCONNECT);
        return;
    }
    list_note("");
    for (n = 0; n < BT_ROWS && btdev_found(n, bt_nm[n], 0); n++)
        list_add(bt_nm[n], "found", LK_ACTION, A_BT_DEV);
    s_cat(bt_note, n ? "pick a device. SEL connects" : plat_ms() - bt.t0 < 8000u ? "looking for MIDI devices..."
                                                     : "none found yet. Is it on?");
}
#endif
#endif
static int scene_first_empty(void)                /* NEW SECTION's scene: the first not in use, -1 none */
{
    for (int i = 0; i < NSCENE; i++)
        if (!pats.scene[i].used)
            return i;
    return -1;
}
/* the first empty pattern of a track (its steps all empty), not the one shown; -1 none */
static int pat_first_empty(int trk)
{
    for (int p = 0; p < NPATT; p++) {
        int empty = p != cur_pat(trk);
        for (int i = 0; i < NSTEP && empty; i++)
            empty = step_empty(&pats.pat[trk][p].st[i]);
        if (empty)
            return p;
    }
    return -1;
}
static void list_head(const char *t) { list_add(t, 0, LK_HEAD, 0); }
static void list_page_knobs(int p, uint32_t mask)  /* page P's knobs (bit i: knob i) not listed yet */
{
    knob_t k[4];
    uint8_t keep = ui.page;
    ui.page = (uint8_t)p;
    page_knobs(k);
    ui.page = keep;
    for (int i = 0; i < 4; i++) {
        int seen = 0;
        if (!(mask >> i & 1u) || !k[i].name)
            continue;
        for (int j = 0; j < lst.n; j++)          /* (a knob twice on a page, or on Steps and Bar: GROOVE; on two
                                                  * pages of one name, two things: the filter envelope's attack) */
            seen |= lst.row[j].kind == LK_KNOB && ((lst.row[j].id >> 2) == p || (steps_page(lst.row[j].id >> 2) &&
                    steps_page(p))) && s_eq(lst.row[j].name, k[i].name) && s_eq(lst.row[j].val, k[i].val);
        if (!seen)
            list_add(k[i].name, k[i].val, LK_KNOB, p << 2 | i);
    }
}
static void list_page_section(int p)              /* a page's knobs under its name (none new: no name) */
{
    int n = lst.n;
    list_head(page_name_of(p, sel_trk()));
    list_page_knobs(p, 15u);
    if (lst.n == n + 1)
        lst.n = (uint8_t)n;
}
static int pick_amount(int v) { return v ? v : 1; }   /* SHIFT NOTES', ROTATE's amount (never 0) */
/* Steps and Bar: the pattern's actions, then the bar's */
static void list_steps(int trk)
{
    static char empty_t[16], bar_t[8], rot_t[8], sh_t[8], all_t[8];
    int to = pat_first_empty(trk), paste = clip_ok && clip_kind != CK_STEP;   /* (a step pastes onto steps held) */
    list_head("pattern");
    if (to >= 0)
        num_s((uint32_t)to + 1u, s_cat(empty_t, "P"));
    else
        s_cat(empty_t, "none empty");
    list_add(ACTION_NAME[A_COPY_EMPTY], empty_t, LK_ACTION, A_COPY_EMPTY);
    list_add(ACTION_NAME[A_COPY_PAT], 0, LK_ACTION, A_COPY_PAT);
    if (paste && clip_kind == CK_PAT)
        list_add(ACTION_NAME[A_PASTE], ui.pick_paste & 1u ? "locks only" : "pattern", LK_PICK, A_PASTE);
    list_add(ACTION_NAME[A_CLEAR], CLEAR_WHAT[ui.pick_clear % 3u], LK_PICK, A_CLEAR);
    {
        int r = pick_amount(ui.pick_rot);
        num_s((uint32_t)(r < 0 ? -r : r), s_cat(rot_t, r < 0 ? "L" : "R"));
        list_add(ACTION_NAME[A_ROTATE], rot_t, LK_PICK, A_ROTATE);
    }
    if (!is_drum(trk) && trk != AUX_PUNCH) {         /* (a kit's notes are its sounds, THROW's its effects) */
        int v = pick_amount(ui.pick_shift);
        num_s((uint32_t)(v < 0 ? -v : v), s_cat(sh_t, v < 0 ? "-" : "+"));
        list_add(ACTION_NAME[A_SHIFT], sh_t, LK_PICK, A_SHIFT);
    }
    list_add(ACTION_NAME[A_DOUBLE], 0, LK_ACTION, A_DOUBLE);
    num_s((uint32_t)ui.bar + 1u, s_cat(bar_t, "bar "));
    list_head(bar_t);
    list_add(ACTION_NAME[A_COPY_BAR], 0, LK_ACTION, A_COPY_BAR);
    if (paste && clip_kind == CK_BAR)
        list_add(ACTION_NAME[A_PASTE], ui.pick_paste & 1u ? "locks only" : "bar", LK_PICK, A_PASTE);
    list_add(ACTION_NAME[A_COPY_ALL], 0, LK_ACTION, A_COPY_ALL);   /* (the bar on every track at once) */
    if (clipall_ok) {
        num_s((uint32_t)clipall_bar + 1u, s_cat(all_t, "bar "));
        list_add(ACTION_NAME[A_PASTE_ALL], all_t, LK_ACTION, A_PASTE_ALL);
    }
    list_add(ACTION_NAME[A_CLEAR_BAR], 0, LK_ACTION, A_CLEAR_BAR);
    list_add(ACTION_NAME[A_DUP_BAR], 0, LK_ACTION, A_DUP_BAR);
}
/* HOME: the page's own first (Song: the song; Arrange: the scenes), then the projects */
static void list_home(int keep)
{
    static char slot_t[24], save_t[24], scl_t[12], new_t[12];
    for (int i = 0; i < 2; i++)
        if ((i == 0) == (keep == P_SONG)) {
            list_head("song");
            list_add(ACTION_NAME[A_PLAY_SONG], 0, LK_ACTION, A_PLAY_SONG);
            list_add(ACTION_NAME[A_INSERT_SLOT], 0, LK_ACTION, A_INSERT_SLOT);
            list_add(ACTION_NAME[A_DUP_SLOT], 0, LK_ACTION, A_DUP_SLOT);
            list_add(ACTION_NAME[A_MOVE_SLOT], ui.pick_move & 1u ? "later" : "earlier", LK_PICK, A_MOVE_SLOT);
            list_add(ACTION_NAME[A_DELETE_SLOT], 0, LK_ACTION, A_DELETE_SLOT);
            list_add(ACTION_NAME[A_CLEAR_SONG], 0, LK_ACTION, A_CLEAR_SONG);
        } else {
            list_head("scenes");
            {                                    /* NEW SECTION: the scene it makes (the first empty one) */
                int ns = scene_first_empty();
                if (ns >= 0)
                    num_s((uint32_t)ns + 1u, s_cat(new_t, "S"));
                else
                    s_cat(new_t, "none empty");
                list_add(ACTION_NAME[A_NEW_SECTION], new_t, LK_ACTION, A_NEW_SECTION);
            }
            list_add(ACTION_NAME[A_SCENE_AT], proj.rsv[2] & SCENES_AT_END ? "pattern end" : "next bar", LK_PICK,
                     A_SCENE_AT);
            list_add(ACTION_NAME[A_COPY_SCENE], 0, LK_ACTION, A_COPY_SCENE);
            if (scene_clip && scene_clip - 1 != cur_scene() && pats.scene[scene_clip - 1].used) {
                num_s((uint32_t)scene_clip, s_cat(scl_t, "from S"));
                list_add(ACTION_NAME[A_PASTE_SCENE], scl_t, LK_ACTION, A_PASTE_SCENE);
            }
            list_add(ACTION_NAME[A_CLEAR_SCENE], 0, LK_ACTION, A_CLEAR_SCENE);
        }
    list_head("project");
    proj_slot_text(ui.pick_slot % NPROJ, slot_t);
    list_add(ACTION_NAME[A_LOAD_PROJ], slot_t, LK_PICK, A_LOAD_PROJ);
    proj_slot_text(ui.pick_save % NPROJ, save_t);
    list_add(ACTION_NAME[A_SAVE_TO], save_t, LK_PICK, A_SAVE_TO);
    list_add(ACTION_NAME[A_NEW_PROJ], 0, LK_ACTION, A_NEW_PROJ);
    list_add(ACTION_NAME[A_CLEAR_ALL], 0, LK_ACTION, A_CLEAR_ALL);   /* (the sounds and the mix stay) */
    list_add(ACTION_NAME[A_RANDOM_SOUNDS], 0, LK_ACTION, A_RANDOM_SOUNDS);
    list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
}
/* EDIT: the sound's actions (EDIT only: the other buttons' lists are their knobs) */
static void list_sound(int trk)
{
    static char slot_t[24], fol_t[24], kit_t[24], nm[NTRK][16], from_t[24], key_t[24];
    int sc = snd_clip ? snd_clip - 1 : -1, paste = sc >= 0 && sc != trk && sc < NTRK && is_drum(sc) == is_drum(trk);
    if (trk == AUX_BRAIN) {
        list_head("follow");
        for (int t = 0; t < NTRK; t++) {
            s_cat(num_s((uint32_t)t + 1u, s_cat(nm[t], "T")), " FOLLOWS");
            list_add(nm[t], FOLLOW_NAME[proj.trk[t].par[TP_BRAIN] % NBM], LK_PICK, A_FOLLOW);
        }
    } else if (is_fx(trk)) {
        list_add(ACTION_NAME[A_EFFECT], fx_label(ui.pick_fx % NFXPICK), LK_PICK, A_EFFECT);
    } else if (is_drum(trk)) {                     /* the key's play mode, then the kit's */
        list_head("key");
        list_add(ACTION_NAME[A_PLAYMODE], PM_NAME[proj.trk[trk].par[TP_DRUM(ui.dsound % DL_NSOUND, DP_MODE)] % NPM],
                 LK_PICK, A_PLAYMODE);
        list_add(ACTION_NAME[A_COPY_KEY], DL_SOUND_NAME[ui.dsound % DL_NSOUND], LK_ACTION, A_COPY_KEY);
        if (key_clip && key_clip - 1 < NTRK && is_drum(key_clip - 1) &&
            (key_clip - 1 != trk || key_clip_k != ui.dsound % DL_NSOUND)) {
            char *e = s_cat(key_t, "from ");
            if (key_clip - 1 != trk)
                e = s_cat(num_s((uint32_t)key_clip, s_cat(e, "T")), " ");
            s_cat(e, DL_SOUND_NAME[key_clip_k % DL_NSOUND]);
            list_add(ACTION_NAME[A_PASTE_KEY], key_t, LK_ACTION, A_PASTE_KEY);
        }
        list_head("kit");
        kit_label(ui.pick_kit % NKITS, kit_t);
        list_add(ACTION_NAME[A_KIT], kit_t, LK_PICK, A_KIT);
        kit_label(NFKITS + ui.pick_kslot % NUSER_KITS, slot_t);
        list_add(ACTION_NAME[A_SAVE_KIT], slot_t, LK_PICK, A_SAVE_KIT);
        list_add(ACTION_NAME[A_SCRAMBLE], 0, LK_ACTION, A_SCRAMBLE);
        list_add(ACTION_NAME[A_COPY_SOUND], 0, LK_ACTION, A_COPY_SOUND);
        if (paste) {
            num_s((uint32_t)sc + 1u, s_cat(from_t, "from T"));
            list_add(ACTION_NAME[A_PASTE_SOUND], from_t, LK_ACTION, A_PASTE_SOUND);
        }
        list_add(ACTION_NAME[A_ENGINE], EG_NAME[ui.pick_engine % NENGINE], LK_PICK, A_ENGINE);
        list_midi_out(trk);
    } else if (!is_aux(trk)) {
        list_head("sound");
        if (NPRESETS > 0)
            list_add(ACTION_NAME[A_PRESET], PRESETS[ui.pick_preset % NPRESETS].name, LK_PICK, A_PRESET);
        list_add(ACTION_NAME[A_ENGINE], EG_NAME[ui.pick_engine % NENGINE], LK_PICK, A_ENGINE);
        list_add(ACTION_NAME[A_SCRAMBLE], 0, LK_ACTION, A_SCRAMBLE);
        num_s((uint32_t)ui.pick_slot % NUSER_PRESETS + 1u, s_cat(slot_t, "U"));
        list_add(ACTION_NAME[A_SAVE_PRESET], slot_t, LK_PICK, A_SAVE_PRESET);
        list_add(ACTION_NAME[A_INIT], 0, LK_ACTION, A_INIT);
        list_add(ACTION_NAME[A_COPY_SOUND], 0, LK_ACTION, A_COPY_SOUND);
        if (paste) {
            num_s((uint32_t)sc + 1u, s_cat(from_t, "from T"));
            list_add(ACTION_NAME[A_PASTE_SOUND], from_t, LK_ACTION, A_PASTE_SOUND);
        }
        s_cat(fol_t, FOLLOW_NAME[proj.trk[trk].par[TP_BRAIN] % NBM]);
        list_add(ACTION_NAME[A_FOLLOW], fol_t, LK_PICK, A_FOLLOW);
        if (is_midi(trk)) {                          /* its knobs' CCs (its notes always go out: no MIDI OUT) */
            list_head("MIDI");
            for (int k = 0; k < 8; k++) {
                static char v[8][4];
                num_s(proj.trk[trk].par[midi_cc_par(k)] % NMIDI_CC, v[k]);
                list_add(MIDI_CC_ROW[k], v[k], LK_PICK, A_MIDI_CC);
            }
        } else
            list_midi_out(trk);
    }
}
/* GLO's pages but Setup: their knobs, page by page; Master out's settings with its knobs */
static void list_mix(int g)
{
    static char by[8], rel[12], lev[8];
    for (int gi = 0; gi < group_n(g); gi++) {
        int p = grp_page(g, gi);
        if (p == P_SETUP || p == P_STATUS)
            continue;
        list_page_section(p);
        if (p == P_MASTER3) {
            char *e = num_i((int32_t)(master_pump_ms(proj.master[MS_PUMP_REL]) + 0.5f), rel);
            s_cat(e, " ms");
            num_i(proj.master[MS_LEVEL], lev);
            list_add(ACTION_NAME[A_PUMP_BY], pump_by_text(proj.master[MS_PUMP_BY], by), LK_PICK, A_PUMP_BY);
            list_add(ACTION_NAME[A_PUMP_REL], rel, LK_PICK, A_PUMP_REL);
            list_add(ACTION_NAME[A_PUMP_ACC], proj.master[MS_PUMP_ACC] ? "on" : "off", LK_PICK, A_PUMP_ACC);
            list_add(ACTION_NAME[A_LEVEL], lev, LK_PICK, A_LEVEL);
        }
    }
}
/* GLO's Setup and Debug: the FM-1's own settings, Bluetooth first */
static void list_setup(int g)
{
#if FM1B_BLE_CENTRAL
    static char bmid_t[24];
#endif
#if LOOM_BT_AUDIO
    static char baud_t[24];
#endif
    list_head("Bluetooth");
    list_add(ACTION_NAME[A_BT_MIDI], bt.midi ? "on" : "off", LK_PICK, A_BT_MIDI);   /* (two pairs: on / off, then
                                                                                  * where devices are chosen) */
#if FM1B_BLE_CENTRAL
    bt_dev_text(bmid_t, "choose", "BT MIDI off");
    list_add(ACTION_NAME[A_BT_DEVICE], bmid_t, LK_ACTION, A_BT_DEVICE);
#endif
#if LOOM_BT_AUDIO
    list_add(ACTION_NAME[A_BT_AUDIO], bt.want ? "on" : "off", LK_PICK, A_BT_AUDIO);
    bt_adev_text(baud_t);
    list_add(ACTION_NAME[A_BT_ADEV], baud_t, LK_ACTION, A_BT_ADEV);
#endif
    list_head("USB and MIDI");
    list_add(ACTION_NAME[A_USB_AUDIO], USBA_NAME[proj.usb_full & 1u], LK_PICK, A_USB_AUDIO);
#if FM1B_USB_HOST
    list_add(ACTION_NAME[A_USB_HOST], plat_usb_host_state() ? "MIDI host" : "computer", LK_PICK, A_USB_HOST);
#endif
    list_page_knobs(P_SETUP, 6u);                    /* CLOCK, NOTES */
    list_add(ACTION_NAME[A_CC_IN], proj.rsv[3] & CC_IN_ENDLESS ? "endless" : "absolute", LK_PICK, A_CC_IN);
    list_add(ACTION_NAME[A_CC_OUT], proj.rsv[2] & CC_OUT_OFF ? "off" : "on", LK_PICK, A_CC_OUT);
    list_add(ACTION_NAME[A_SCENES_OUT], proj.rsv[2] & SCENES_OUT ? "on" : "off", LK_PICK, A_SCENES_OUT);
    list_head("LOOM-8");
    list_page_knobs(P_SETUP, 1u);                    /* LIGHTS */
    list_add(ACTION_NAME[A_BEAT_FLASH], proj.rsv[3] & BEAT_FLASH_OFF ? "off" : "on", LK_PICK, A_BEAT_FLASH);
    list_add(ACTION_NAME[A_AUTOSAVE], proj.rsv[2] & AUTOSAVE_OFF ? "off" : "on", LK_PICK, A_AUTOSAVE);
    list_add(ACTION_NAME[A_LOOP_FREE], proj.rsv[2] & LOOP_FREE ? "drift" : "in time", LK_PICK, A_LOOP_FREE);
    if (group_n(g) > 6)
        list_page_knobs(P_STATUS, 15u);
    list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
}
static int list_is_setup(int p) { return p == P_SETUP || p == P_STATUS; }
static void list_build(void)
{
    int keep = ui.page, g = page_group(keep), trk = sel_trk(), b = PAGE_BTN[g].btn, knobs = 1, heads, i, j;
    static char rm[NDRUM][16], keep_t[12];
    lst.n = 0;
#if BT_LISTS
    if (lst.bt) {
#if LOOM_BT_AUDIO
        if (lst.bt == 1)
            list_bt_audio();
        else
#endif
#if FM1B_BLE_CENTRAL
            list_bt_dev();
#else
            ;
#endif
        if (lst.sel >= lst.n)
            lst.sel = (uint8_t)(lst.n ? lst.n - 1 : 0);
        if (lst.row[lst.sel].kind == LK_NOTE && lst.n > 1)
            lst.sel = 1;                             /* (the note is never picked) */
        return;
    }
#endif
    if (steps_page(keep) && ui.held && is_seqt(trk)) {   /* a step held + SEL: its notes, to remove */
        const step_t *st = &tpat(trk)->st[step_shown(ui.held_first)];
        if (is_drum(trk)) {                           /* each sound on the step, by key */
            for (int i = 1, r = 0; i <= NDRUM; i++)
                if (kit_has(st, i)) {
                    s_cat(s_cat(rm[r], "REMOVE "), DL_SOUND_NAME[i - 1]);
                    list_add(rm[r++], 0, LK_ACTION, A_REMOVE_NOTE);
                }
            list_clip_step();
            return;
        }
        for (int i = 0; i < SN && st->note[i]; i++) {
            char *e = s_cat(rm[i], "REMOVE ");
            if (is_drum(trk))
                s_cat(e, DL_SOUND_NAME[(st->note[i] - 1) % DL_NSOUND]);
            else if (trk == AUX_PUNCH)
                s_cat(e, PX_VALID(st->note[i] - 1) ? PUNCH_NAME[(st->note[i] - 1) & 15] : "fx");
            else if (pl_type(trk) == PL_RAND)
                s_cat(e, "slot");
            else
                note_name(st->note[i], e);
            list_add(rm[i], 0, LK_ACTION, A_REMOVE_NOTE);
        }
        list_clip_step();
        return;
    }
    if (keep == P_STEPS && trk == AUX_WEAVE) {     /* WEAVE: its pattern cleared */
        list_add(ACTION_NAME[A_CLEAR_NOTES], 0, LK_ACTION, A_CLEAR_NOTES);
    } else if (steps_page(keep)) {
        if (is_seqt(trk))
            list_steps(trk);
    } else if (keep == P_WEAVE) {
        if (wv_from(trk)) {
            int to = pat_first_empty(trk);
            if (to >= 0)
                num_s((uint32_t)to + 1u, s_cat(keep_t, "P"));
            else
                s_cat(keep_t, "none empty");
            list_add(ACTION_NAME[A_WEAVE], keep_t, LK_ACTION, A_WEAVE);
        }
    } else if (keep == P_ARRANGE || keep == P_SONG) {
        list_home(keep);
    } else if (fx_page(keep)) {                     /* an effect: the type, its filter, the feed into FX II, the
                                                     * delay's PING WIDTH */
        int ft = keep == P_FX1 ? AUX_FX1 : AUX_FX2;
        static char hp[8], lp[8], fd[8], wd[8];
        num_s(proj.trk[ft].par[TP_FENV], hp);
        num_s(proj.trk[ft].par[TP_CUT], lp);
        list_add(ACTION_NAME[A_EFFECT], fx_label(ui.pick_fx % NFXPICK), LK_PICK, A_EFFECT);
        list_add(ACTION_NAME[A_FX_HP], hp, LK_PICK, A_FX_HP);
        list_add(ACTION_NAME[A_FX_LP], lp, LK_PICK, A_FX_LP);
        if (ft == AUX_FX1) {
            num_s(proj.trk[ft].par[TP_SEND2], fd);
            list_add(ACTION_NAME[A_FX_SEND], fd, LK_PICK, A_FX_SEND);
        }
        if (fx_pick_type(fx_pick_of(ft)) == FXT_DELAY) {
            num_s(proj.trk[ft].par[TP_FX_WIDTH], wd);
            list_add(ACTION_NAME[A_FX_WIDTH], wd, LK_PICK, A_FX_WIDTH);
        }
    } else if ((keep == P_ENV || keep == P_FILTER || keep == P_ENV2 || keep == P_KITF) && !is_aux(trk)) {
        int fq = ftype_param(trk, keep);
        const tp_info_t *fi = tp_info_par(fq, proj.trk[trk].par);
        if (is_drum(trk)) {                          /* the key's filter and the kit's bus (on: dynamic, or off), on
                                                      * all four pages */
            fq = TP_DRUM(ui.dsound % DL_NSOUND, DP_FTYPE);
            fi = tp_info_par(fq, proj.trk[trk].par);
            list_add("KEY FILTER", fi->names[proj.trk[trk].par[fq] % (fi->hi + 1u)], LK_PICK, A_FTYPE);
            list_add(ACTION_NAME[A_KIT_FILTER], proj.trk[trk].par[TP_FTYPE] == FT_DYNAMIC ? "on" : "off", LK_PICK,
                     A_KIT_FILTER);
        } else
            list_add(ACTION_NAME[A_FTYPE], fi->names[proj.trk[trk].par[fq] % (fi->hi + 1u)], LK_PICK, A_FTYPE);
    } else if (b == B_EDIT) {
        list_sound(trk);
    } else if (b == B_GLO) {
        if (list_is_setup(keep))
            list_setup(g);
        else
            list_mix(g);
        knobs = 0;
    }
    if (knobs) {                                     /* the button's knobs, page by page (FX: the page's own) */
        if (b == B_FX)
            list_page_section(keep);
        else
            for (int gi = 0; gi < group_n(g); gi++)
                list_page_section(grp_page(g, gi));
    }
    for (i = j = 0; i < lst.n; i++)
        j += lst.row[i].kind != LK_HEAD;
    heads = j > LIST_SHOWN;                          /* the sections' names only where the list is longer than the
                                                      * screen (and none with nothing under it) */
    for (i = j = 0; i < lst.n; i++)
        if (lst.row[i].kind != LK_HEAD || (heads && i + 1 < lst.n && lst.row[i + 1].kind != LK_HEAD))
            lst.row[j++] = lst.row[i];
    lst.n = (uint8_t)j;
    if (lst.sel >= lst.n)
        lst.sel = (uint8_t)(lst.n ? lst.n - 1 : 0);
}
/* the list's marks at a row's right: an action's return arrow (SEL carries it out), a setting's pointer (ALGORITHM
 * changes it at once) */
static void glyph_enter(int32_t x, int32_t y, uint16_t c)   /* (x, y: the box 8 x 8 it sits in) */
{
    cv_rect(x + 7, y, 1, 5, c);
    cv_rect(x + 1, y + 5, 7, 1, c);
    cv_tri((float)x - 0.5f, (float)y + 5.0f, (float)x + 2.8f, (float)y + 2.0f, (float)x + 2.8f, (float)y + 8.0f, c);
}
static void glyph_pick(int32_t x, int32_t y, uint16_t c)
{
    cv_tri((float)x + 1.0f, (float)y, (float)x + 6.0f, (float)y + 4.0f, (float)x + 1.0f, (float)y + 8.0f, c);
}
#define LIST_Y HDR_H
#define LIST_H (ENC_Y - LIST_Y)
#define LIST_VIS (LIST_H - 2)
static int list_rh(int i) { return lst.row[i].kind == LK_HEAD ? LIST_HH : LIST_RH; }
static int list_span(int a, int b)                /* rows a..b's height */
{
    int h = 0;
    for (int i = a; i <= b && i < lst.n; i++)
        h += list_rh(i);
    return h;
}
/* the row picked in view, with its section's name when it is the first of a section; open: from its section's
 * name, the list filling the screen (as far up as there is room) */
static void list_scroll(int open)
{
    int s = lst.sel, t = lst.top;
    if (open || t > s)
        t = s;
    if (t == s && s && lst.row[s - 1].kind == LK_HEAD)
        t--;
    if (open)
        while (t > 0 && list_span(t - 1, lst.n - 1) <= LIST_VIS)
            t--;
    while (t < s && list_span(t, s) > LIST_VIS)
        t++;
    lst.top = (uint8_t)t;
}
/* a list as a printed index: a section's name in small tracked capitals with a hairline after it; each row its name in
 * capitals, a dotted leader to its value (bold) or to the mark at the right; the row picked inked (paper on ink); a slim
 * scroll bar at the right when the list is longer than the screen */
#define LIST_VX 205                                  /* the values' right edge */
static void list_paint(int32_t oy)
{
    int32_t y = 1 - oy;
    for (int i = lst.top; i < lst.n && y + oy - 1 + list_rh(i) <= LIST_VIS; y += list_rh(i), i++) {
        int on = i == lst.sel, k = lst.row[i].kind;
        uint16_t fg = on ? K_BG : K_TEXT;
        const char *v = lst.row[i].val;
        if (k == LK_HEAD) {                          /* a section's name, a rule after it */
            int32_t e = tr_text(8, y + 3, F_LAB, lst.row[i].name, K_DIM, 1);
            if (e + 6 < 226)
                cv_rect(e + 6, y + 8, 226 - (e + 6), 1, K_LINE);
            continue;
        }
        if (k == LK_NOTE) {
            text_fit(13, cap_y(y, LIST_RH, &FONT_XS), &FONT_XS, lst.row[i].name, 212, K_DIM);
            continue;
        }
        if (on)
            cv_round(8, y, 218, LIST_RH, 2, K_TEXT);
        {   /* every row alike: its name in capitals; its value bold at the right; the mark */
            char nm[24];
            int now = k == LK_KNOB || (k == LK_PICK && pick_sets(lst.row[i].id));
            int32_t ne, room, vx = LIST_VX;
            up_copy(nm, lst.row[i].name, sizeof nm);
            ne = cv_text(13, cap_y(y, LIST_RH, &FONT_XS), &FONT_XS, nm, fg);
            room = LIST_VX - (ne + 8);
            if (*v && room > 8) {                    /* (too long bold: semibold, then cut) */
                const felucca_font_t *vf = text_w(F_XB, v) <= room ? F_XB : &FONT_XS;
                int32_t w = text_w(vf, v);
                vx = LIST_VX - (w < room ? w : room);
                text_fit(vx, cap_y(y, LIST_RH, vf), vf, v, room, fg);
            }
            if (!on && vx - 6 > ne + 6)
                cv_leader(ne + 5, vx - 6, y + 9, K_DIM);
            if (now)
                glyph_pick(213, y + 4, fg);
            else
                glyph_enter(211, y + 3, fg);
        }
    }
    {
        int32_t all = list_span(0, lst.n - 1);
        if (all > LIST_VIS) {
            int32_t th = LIST_VIS - 2, bh = th * LIST_VIS / all, by = lst.top ? th * list_span(0, lst.top - 1) / all : 0;
            cv_round(229, 1 - oy, 3, th, 1, K_PANEL);
            cv_round(229, 1 + by - oy, 3, bh, 1, K_TEXT);
        }
    }
}
static void draw_list(void)
{
    uint32_t h = hash_u(hash_u(2166136261u, 0x4Cu), lst.sel | (uint32_t)lst.top << 8), i;
    list_build();
    h = hash_u(h, lst.n);
    for (i = 0; i < lst.n; i++) {
        h = hash_u(h, lst.row[i].kind | (uint32_t)lst.row[i].id << 4);
        h = hash_s(h, lst.row[i].name);
        h = hash_s(h, lst.row[i].val);
    }
    ui.sig[2] = ui.sig[3] = ui.sig[5] = 0;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    {                                            /* two bands, split between rows (MAIN_H at most each) */
        int32_t h1 = 1;
        for (int i = lst.top; i < lst.n && h1 + list_rh(i) <= MAIN_H; i++)
            h1 += list_rh(i);
        cv_begin(240, h1, K_BG);
        list_paint(0);
        cv_blit(0, LIST_Y);
        cv_begin(240, LIST_H - h1, K_BG);
        list_paint(h1);
        cv_blit(0, LIST_Y + h1);
    }
}

/* ------------------------------------------------------- the pages --- */
static int begin_main(uint32_t h)
{
    if (h == ui.sig[1])
        return 0;
    ui.sig[1] = h;
    ui.sig[5] = 0;                                   /* (the step record strip over it: drawn again) */
    cv_begin(240, MAIN_H, K_BG);
    return 1;
}
static uint32_t hash_pat(uint32_t h, const tpat_t *p)
{
    const uint32_t *w = (const uint32_t *)(const void *)p;
    for (uint32_t i = 0; i < sizeof *p / 4u; i++)
        h = hash_u(h, w[i]);
    return h;
}

/* Arrange (the drawing AArr): the ten sequenced tracks as punched strips, each the 16 steps of the bar its playhead is
 * in (its own steps, in its ink), its pattern's name, a meter in its colour chip; one vermilion playhead across all
 * rows (a track whose own step is elsewhere, other lengths or speeds, marks it under its strip); a cropped loop
 * bracketed under the strip, its steps outside dimmed; a mute struck through and greyed, MUTED; a kit's sounds the
 * scene mutes: how many, their steps greyed. At the right: the scene, large; the scene cued next and the beats left,
 * in vermilion; the 32 scenes (used: a hollow square, this one inked, the cued one vermilion), the white keys' half
 * marked */
#define AR_MH 5                                      /* the chip's meter: pixels tall */
#define AR_Y0 12                                     /* the first row's top; a row AR_DY */
#define AR_DY 11
#define AR_SX 40                                     /* the strip: 16 cells AR_SP/2 px apart (a cell 4 wide) */
#define AR_SP 13
#define AR_ST 147                                    /* the status column (MUTED, 3 OFF) */
#define AR_DIV 181                                   /* the rule before the scenes */
#define AR_RC 211                                    /* the scenes' column's centre */
static int32_t ar_cx(int c) { return AR_SX + c * AR_SP / 2; }   /* a cell's left edge */
static void ar_hole(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* an outline (no audit box: cells are many) */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y + 1, 1, h - 2, c);
    cv_rect(x + w - 1, y + 1, 1, h - 2, c);
}
static void draw_arrange(void)
{
    static float a_db[NTRK];
    static uint32_t a_last;
    int sc = cur_scene(), cue = seq.cue, left = seq_cue_left(&seq), mh[NSEQT], ph = playing() ? seq.bar16 & 15u : -1,
        used = 0;
    uint32_t now = plat_ms(), dt = now - a_last;
    uint32_t h = hash_u(hash_u(2166136261u, 0xA0u), (uint32_t)sc | (uint32_t)cue << 8 | (uint32_t)(ph + 1) << 16 |
                                          (uint32_t)sel_trk() << 21 | (uint32_t)seq.cue_end << 25 |
                                          (uint32_t)ui.scene_pg << 26);
    char t[16];
    h = hash_u(h, (uint32_t)(left + 1));
    for (int k = 0; k < NKITM; k++)
        h = hash_u(h, seq.kmute[k]);
    a_last = now;
    for (int tr = 0; tr < NSEQT; tr++) {             /* each track's meter: what it plays now (Mix's fall; -45 to -9 dB) */
        if (tr < NTRK) {
            float db = lin_db((float)ui_peak[tr] * (1.0f / 16383.5f));
            a_db[tr] = dt > 250u ? db : fm_maxf(db, a_db[tr] - (float)dt * 0.024f);
            mh[tr] = (int)fm_clampf((a_db[tr] + 45.0f) * (AR_MH / 36.0f) + 0.5f, 0.0f, (float)AR_MH);
        } else                                   /* (COND and THROW make no sound: a step of theirs playing fills it) */
            mh[tr] = playing() && !step_empty(&tpat(tr)->st[eng_step[tr] % NSTEP]) ? AR_MH : 0;
        h = hash_u(h, (uint32_t)mh[tr] | (uint32_t)seq.ppat[tr] << 4 | (uint32_t)eng_step[tr] << 8 |
                          (uint32_t)(seq.mute >> tr & 1u) << 16 | (uint32_t)tpat(tr)->len << 17 |
                          (uint32_t)is_drum(tr) << 25);
        h = hash_pat(h, tpat(tr));
    }
    for (int i = 0; i < NSCENE; i++)
        h = hash_u(h, pats.scene[i].used);
    if (!begin_main(h))
        return;
    for (int c = 0; c < 16; c += 4) {                /* the steps' numbers over the strips */
        num_s((uint32_t)c + 1u, t);
        text_c(ar_cx(c) + 2, -2, F_LAB, t, K_DIM);
    }
    for (int tr = 0; tr < NSEQT; tr++) {
        int32_t y = AR_Y0 + tr * AR_DY;
        const tpat_t *tp = tpat(tr);
        int muted = seq.mute >> tr & 1u, len = tp->len > NSTEP ? NSTEP : tp->len, pos = eng_step[tr] % NSTEP, lf, le,
            wb, ks = tr < NTRK ? kit_slot(tr) : -1, koff = 0, notes = 0, any = 0;
        uint32_t km = ks >= 0 && is_drum(tr) ? seq.kmute[ks] : 0;
        uint16_t ink = trk_ink(tr), lab = muted ? K_LINE : tr == sel_trk() ? K_SEL : K_TEXT;
        if (len < 1)
            len = 1;
        for (int i = 0; i < len && !any; i++)
            any = !step_empty(&tp->st[i]);
        pat_loop(tp, &lf, &le);
        wb = (playing() ? pos % len : lf) / 16;      /* the bar shown: the one its playhead is in */
        if (tr < NTRK)
            num_s((uint32_t)tr + 1u, t);
        else
            s_cat(t, tr == AUX_BRAIN ? "CO" : "TH");
        text_r(14, y - 2, F_LAB, t, lab);
        cv_rect(17, y, 5, 7, muted ? K_LINE : trk_tint(tr));   /* its chip: the meter fills it */
        if (!muted && mh[tr] > 0)
            cv_rect(17, y + 7 - mh[tr] - 1, 5, mh[tr] + 1, ink);
        num_s((uint32_t)seq.ppat[tr] % NPATT + 1u, s_cat(t, "P"));
        cv_text(25, y - 2, F_LAB, t, muted ? K_LINE : K_DIM);
        {                                            /* the strip's base, as far as the pattern reaches in this bar */
            int n = len - wb * 16;
            n = n > 16 ? 16 : n < 1 ? 1 : n;
            cv_rect(AR_SX, y + 8, ar_cx(n - 1) + 5 - AR_SX, 1, K_LINE);
        }
        for (int c = 0; c < 16; c++) {
            int s = wb * 16 + c;
            const step_t *st;
            uint16_t ci;
            if (s >= len)
                break;
            st = &tp->st[s];
            if (step_empty(st)) {
                if (!any)                            /* nothing punched yet: the card's holes, unpunched */
                    ar_hole(ar_cx(c), y, 4, 7, K_PANEL);
                continue;
            }
            notes = 1;
            ci = muted ? K_LINE : ink;
            if (s < lf || s >= le)                   /* (outside the loop: not played) */
                ci = K_LINE;
            if (km && !(kit_mask(st) & ~km))         /* (a kit step whose sounds are all muted) */
                ci = K_LINE;
            cv_rect(ar_cx(c), y, 4, 7, ci);
        }
        if (lf > 0 || le < len) {                    /* a cropped loop: bracketed under its steps in this bar */
            int a = lf - wb * 16, b = le - 1 - wb * 16;
            if (b >= 0 && a <= 15) {
                int32_t xa = a < 0 ? AR_SX - 1 : ar_cx(a) - 1, xb = b > 15 ? ar_cx(15) + 5 : ar_cx(b) + 5;
                cv_rect(xa, y + 9, xb - xa, 1, K_TEXT);
                if (a >= 0)
                    cv_rect(xa, y + 6, 1, 4, K_TEXT);
                if (b <= 15)
                    cv_rect(xb - 1, y + 6, 1, 4, K_TEXT);
            }
        }
        if (muted) {                                 /* struck through, and said */
            cv_rect(AR_SX - 2, y + 3, ar_cx(15) + 7 - AR_SX, 1, K_DIM);
            cv_text(AR_ST, y - 2, F_LAB, "MUTED", K_DIM);
        } else {
            for (int k = 0; k < NDRUM; k++)
                koff += km >> k & 1u;
            if (koff) {
                s_cat(num_s((uint32_t)koff, t), " OFF");
                cv_text(AR_ST, y - 2, F_LAB, t, K_DIM);
            } else if (len > 16)                     /* more than a bar: its bars, the one shown filled */
                for (int b = 0; b < (len + 15) / 16; b++) {
                    if (b == wb)
                        cv_rect(AR_ST + b * 6, y, 4, 7, K_TEXT);
                    else
                        ar_hole(AR_ST + b * 6, y, 4, 7, K_DIM);
                }
        }
        if (ph >= 0 && !muted && notes && (pos - ph + 17) % 16 > 2 && pos < len)   /* its own step, away from the
                                                  * playhead (a step either side: the engine's look-ahead) */
            cv_rect(ar_cx(pos % 16) - 1, y + 9, 6, 2, K_REC);
    }
    if (ph >= 0) {                                   /* the playhead: one, across every row */
        float x = (float)ar_cx(ph) + 1.5f;
        cv_linef(x, (float)AR_Y0 - 2.0f, x, (float)(AR_Y0 + AR_DY * NSEQT - 2), K_REC, 1.4f);
        cv_tri(x, (float)AR_Y0 - 1.0f, x - 3.0f, (float)AR_Y0 - 5.5f, x + 3.0f, (float)AR_Y0 - 5.5f, K_REC);
    }
    cv_rect(AR_DIV, 2, 1, 114, K_LINE);
    tr_text(AR_RC - tr_w(F_LAB, "SCENE", 1) / 2, 0, F_LAB, "SCENE", K_DIM, 1);
    num_s((uint32_t)sc + 1u, t);                     /* the scene: a numeral stamped on a solid plate */
    cv_round(AR_RC - 22, 12, 44, 33, 4, K_TEXT);
    text_c(AR_RC, 13, &FONT_L, t, K_BG);
    if (cue != 0xFF && cue != sc) {                  /* the scene cued: when it comes, in beats, a bar running out */
        num_s((uint32_t)cue + 1u, s_cat(t, "NEXT "));
        text_c(AR_RC, 48, F_LAB, t, K_REC);
        if (left >= 0) {
            int span = seq.cue_end ? (int)seq.bar_len : 16, beats = (left + 4) / 4;
            s_cat(num_s((uint32_t)beats, t), beats == 1 ? " beat" : " beats");
            text_c(AR_RC, 57, &FONT_XS, t, K_REC);
            cv_rect(AR_RC - 24, 72, 48, 1, K_PANEL2);
            cv_rect(AR_RC - 24, 72, 48 * (left + 1) / (span > 0 ? span : 16), 1, K_REC);
        } else
            text_c(AR_RC, 57, &FONT_XS, seq.cue_end ? "at the end" : "next bar", K_REC);
    }
    for (int i = 0; i < NSCENE; i++) {               /* the 32 scenes */
        float x = 187.0f + (float)(i % 8) * 6.4f + 2.5f, y = 76.0f + (float)(i / 8) * 7.0f + 2.5f;
        used += pats.scene[i].used;
        if (i == cue && cue != sc)
            cv_rect((int32_t)(x - 2.5f), (int32_t)(y - 2.5f), 5, 5, K_REC);
        else if (i == sc)
            cv_rect((int32_t)(x - 2.5f), (int32_t)(y - 2.5f), 5, 5, K_TEXT);
        else if (pats.scene[i].used) {
            cv_rect((int32_t)(x - 2.5f), (int32_t)(y - 2.5f), 5, 5, K_TEXT);
            cv_rect((int32_t)(x - 1.5f), (int32_t)(y - 1.5f), 3, 3, K_BG);
        } else
            cv_rect((int32_t)(x - 0.5f), (int32_t)(y - 0.5f), 2, 2, K_LINE);
    }
    cv_rect(184, 74 + ui.scene_pg * 14, 1, 13, K_DIM);   /* (the white keys' scenes: 1-16 or 17-32) */
    s_cat(num_s((uint32_t)used, t), " used");
    text_c(AR_RC, 104, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}

/* the song as sections: a slot's letter is its scene's in the order the song first plays them (the first scene A, the
 * next new one B ...; a scene again: its letter again), nothing stored. song_bars: the song's bars, and the bar each
 * slot starts on (at[], from 1) and its letter (lt[]) for the slots up to n */
static int song_bars(int n, uint16_t *at, char *lt)
{
    const song_t *sg = &pats.song;
    uint8_t seen[NSCENE];
    int bars = 0, k = 0;
    memset(seen, 0xFF, sizeof seen);
    for (int i = 0; i < sg->len; i++) {
        int sc = sg->slot[i].scene % NSCENE;
        if (seen[sc] == 0xFF)
            seen[sc] = (uint8_t)k++;
        if (i < n) {
            at[i] = (uint16_t)(bars + 1);
            lt[i] = (char)(seen[sc] < 26 ? 'A' + seen[sc] : 'a' + seen[sc] - 26);
        }
        bars += sg->slot[i].rep ? sg->slot[i].rep : 1;
    }
    return bars;
}
/* the song's length as m:ss at the tempo (a bar: four beats) */
static char *song_time(int bars, char *t)
{
    uint32_t sec = ((uint32_t)bars * 2400u + proj.tempo_x10 / 2u) / (proj.tempo_x10 ? proj.tempo_x10 : 1200u);
    char *e = num_s(sec / 60u, t);
    *e++ = ':';
    *e++ = (char)('0' + sec % 60u / 10u);
    *e++ = (char)('0' + sec % 10u);
    *e = 0;
    return e;
}
/* the Song page (the drawing A2): the song as a route map. Each slot is a stop with its section's letter; the line
 * from a stop to the next is as long as the slot's bars, a tick a bar and the bar's number every few, in rows of a
 * fixed number of bars that wrap with chevrons. Two rows show, the selected slot's among them. The scale: as many bars
 * a row as fit the song in two rows (16 at least), but never so many that two stops come closer than SG_GAP px (the
 * shortest slot decides). The selected slot's stop and stretch in blue, a vermilion train where the song plays, the
 * end a loop back to the first stop or a terminus; the selected slot's details along the foot */
#define SG_X0 16
#define SG_W 208                                     /* a row's line: SG_X0 .. SG_X0 + SG_W */
#define SG_Y0 38                                     /* the first row's line; the second's SG_DY lower */
#define SG_DY 47
#define SG_GAP 20                                    /* two stops at least this far apart (a letter 16 px wide, air) */
static const uint16_t SG_BPR[] = {8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072,
                                  4096};
static void sg_chevron(int32_t x, int32_t y, uint16_t c)   /* a row break: ">" 4 px wide */
{
    cv_linef((float)x, (float)y - 4.0f, (float)x + 4.0f, (float)y, c, 1.4f);
    cv_linef((float)x + 4.0f, (float)y, (float)x, (float)y + 4.0f, c, 1.4f);
}
static void sg_stop(int32_t x, int32_t y, int sel)
{
    cv_disc((float)x, (float)y, sel ? 7.5f : 6.5f, K_TEXT);
    cv_disc((float)x, (float)y, sel ? 5.5f : 4.6f, sel ? K_SEL : K_BG);
    if (sel)
        cv_disc((float)x, (float)y, 2.2f, K_BG);
}
/* the train: where the song plays, a small vermilion carriage on the line (a roof, three windows, couplers, two
 * wheels on their axles); parked (stopped): drawn in ink, empty, waiting at the selected stop for PLAY */
static void sg_train(int32_t x, int32_t y, int parked)
{
    uint16_t body = parked ? K_BG : K_REC, ink = K_TEXT;
    cv_rect(x - 10, y - 1, 3, 2, ink);               /* the couplers, on the line */
    cv_rect(x + 8, y - 1, 3, 2, ink);
    cv_rect(x - 6, y - 7, 13, 1, ink);               /* the roof */
    if (parked)
        cv_frame(x - 8, y - 6, 17, 10, 2, ink, body);
    else
        cv_round(x - 8, y - 6, 17, 10, 2, body);
    for (int w = 0; w < 3; w++)                      /* the windows */
        cv_rect(x - 6 + w * 5, y - 4, 3, 3, parked ? ink : K_BG);
    for (int w = -1; w <= 1; w += 2) {               /* the wheels, a hub each */
        float cx = (float)x + 4.5f * (float)w + 0.5f, cy = (float)y + 5.5f;
        cv_disc(cx, cy, 2.5f, ink);
        cv_disc(cx, cy, 0.8f, K_BG);
    }
}
static void draw_song(void)
{
    const song_t *sg = &pats.song;
    int n = sg->len, cur = ui.name_slot <= n ? ui.name_slot : n, pp = seq.song_pos, total, minrep = 64, bpr, ri,
        tstep, nstep, re, wrap = 0, v0, rows, cur_row;
    uint16_t at[NSONG];
    char lt[NSONG], t[40];
    int32_t pend, xe, pt = -1, ps, pn, need;
    uint32_t h;
    total = song_bars(n, at, lt);
    for (int i = 0; i < n; i++)
        minrep = sg->slot[i].rep && sg->slot[i].rep < minrep ? sg->slot[i].rep : minrep;
    for (ri = 0; ri + 1 < (int)(sizeof SG_BPR / sizeof SG_BPR[0]); ri++)   /* the bars a row: the song in two rows */
        if (SG_BPR[ri] >= 16 && SG_BPR[ri] * 2 >= total)
            break;
    while (ri > 0 && SG_W * minrep < SG_GAP * SG_BPR[ri])   /* (stops apart) */
        ri--;
    bpr = SG_BPR[ri];
    for (tstep = 1; tstep * SG_W < 4 * bpr; tstep *= 2) {}    /* a tick every tstep bars (4 px apart at least) */
    for (nstep = 4; nstep * SG_W < 34 * bpr; nstep *= 2) {}   /* a number every nstep bars ("4096" and air) */
    pend = total * SG_W / bpr;
    re = pend / SG_W;
    xe = SG_X0 + pend % SG_W;
    need = sg->loop ? 38 + text_w(&FONT_XS, "back to A") : 18 + text_w(&FONT_XS, "end");
    if (n && xe + need > 234) {                      /* (no room left for the end: it starts the next row) */
        wrap = pend % SG_W != 0;
        re += wrap;
        xe = SG_X0;
    }
    rows = re + 1;
    ps = cur < n ? (at[cur] - 1) * SG_W / bpr : pend;
    pn = cur < n ? (at[cur] - 1 + (sg->slot[cur].rep ? sg->slot[cur].rep : 1)) * SG_W / bpr : pend;
    cur_row = cur < n ? ps / SG_W : re;
    v0 = cur_row + 1 >= rows && cur_row > 0 ? cur_row - 1 : cur_row;
    if (seq.song_on && playing() && pp < n) {        /* where the song is: its slot's bar, the bar's sixteenth */
        int reps = sg->slot[pp].rep ? sg->slot[pp].rep : 1, left = seq.song_rep ? seq.song_rep : 1;
        int32_t six = (int32_t)(at[pp] - 1 + reps - (left > reps ? reps : left)) * 16 + (int32_t)(seq.bar16 & 15u);
        pt = six * SG_W / (bpr * 16);
        if (pt >= pend)
            pt = pend - 1;
    }
    h = hash_u(hash_u(2166136261u, 0x50u), (uint32_t)cur | (uint32_t)n << 8 | (uint32_t)sg->loop << 16 |
                                          (uint32_t)ui.scene_pg << 17 | (uint32_t)(v0 & 0xFF) << 18);
    h = hash_u(h, (uint32_t)(pt + 1) | (uint32_t)bpr << 16);
    h = hash_u(h, proj.tempo_x10);
    for (int i = 0; i < n; i++)
        h = hash_u(h, sg->slot[i].scene | (uint32_t)sg->slot[i].rep << 8 | (uint32_t)i << 16);
    if (!begin_main(h))
        return;
    tr_text(8, 0, F_LAB, "ROUTE", K_DIM, 1);
    if (!n) {                                        /* no stops yet: a bare line, a buffer stop at its end, nowhere to go */
        cv_linef((float)SG_X0, (float)SG_Y0, (float)(SG_X0 + SG_W - 14), (float)SG_Y0, K_TEXT, 2.2f);
        cv_rect(SG_X0 + SG_W - 11, SG_Y0 - 7, 3, 15, K_TEXT);
        cv_disc((float)(SG_X0 + SG_W - 13), (float)SG_Y0 - 4.5f, 1.8f, K_TEXT);
        cv_disc((float)(SG_X0 + SG_W - 13), (float)SG_Y0 + 4.5f, 1.8f, K_TEXT);
        for (int b = 0; b * 13 <= SG_W - 14; b++)    /* (sleepers: the ticks, no numbers yet) */
            cv_rect(SG_X0 + b * 13, SG_Y0 + 5, 1, 3, K_LINE);
        text_r(232, 0, F_XB, "no stops yet", K_TEXT);
        text_r(232, 104, &FONT_XS, ui.scene_pg ? "white keys add scenes 17-32 as stops"
                                               : "white keys add scenes 1-16 as stops", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    {                                                /* the song's bars and time */
        char tm[12];
        int32_t x;
        song_time(total, tm);
        s_cat(num_s((uint32_t)total, t), total == 1 ? " bar" : " bars");
        x = cv_text(232 - text_w(F_XB, tm) - 10 - text_w(F_XB, t), 0, F_XB, t, K_TEXT);
        cv_disc((float)x + 5.0f, 6.5f, 1.3f, K_TEXT);
        text_r(232, 0, F_XB, tm, K_TEXT);
    }
    for (int vr = v0; vr < v0 + 2 && vr < rows; vr++) {
        int32_t y = SG_Y0 + (vr - v0) * SG_DY, r0 = vr * SG_W, lend;
        lend = pend - r0;                            /* the line on this row: to the row's end or the song's */
        lend = lend < 0 ? 0 : lend > SG_W ? SG_W : lend;
        if (lend > 0)
            cv_linef((float)SG_X0, (float)y, (float)(SG_X0 + lend), (float)y, K_TEXT, 2.2f);
        if (vr > 0)
            sg_chevron(2, y, K_TEXT);
        if (vr < re && lend > 0)
            sg_chevron(SG_X0 + lend + 3, y, K_TEXT);
        {                                            /* the selected slot's stretch */
            int32_t a = ps > r0 ? ps : r0, b = pn < r0 + lend ? pn : r0 + lend;
            if (cur < n && b > a)
                cv_rect(SG_X0 + a - r0, y - 2, b - a, 5, K_SEL);
        }
        for (int b = 0; b <= total; b += tstep) {    /* ticks, and the bars' numbers */
            int32_t p = b * SG_W / bpr, x = SG_X0 + p - r0;
            int big = b % nstep == 0;
            if (p < r0 || p > r0 + lend)
                continue;
            cv_rect(x, y + 5, 1, big ? 5 : 3, K_DIM);
            if (big && b < total && p < r0 + SG_W) {
                int32_t w;
                num_s((uint32_t)b + 1u, t);
                w = text_w(F_LAB, t);
                if (x - w / 2 >= 3 && x + w - w / 2 <= 236)
                    cv_text(x - w / 2, y + 10, F_LAB, t, K_DIM);
            }
        }
        for (int i = 0; i < n; i++) {                /* the stops and their letters */
            int32_t p = (at[i] - 1) * SG_W / bpr, x = SG_X0 + p - r0;
            if (p / SG_W != vr)
                continue;
            sg_stop(x, y, i == cur);
            t[0] = lt[i];
            t[1] = 0;
            text_c(x + 1, y - 27, F_VAL, t, i == cur ? K_SEL : K_TEXT);
        }
        if (vr == re) {                              /* the end: a loop back to A, or a terminus */
            int sel = cur == n;
            cv_disc((float)xe, (float)y, 6.5f, K_TEXT);
            cv_disc((float)xe, (float)y, 4.6f, sel ? K_SEL : K_BG);
            cv_disc((float)xe, (float)y, 2.4f, sel ? K_BG : K_TEXT);
            if (sg->loop) {                          /* a turning loop: the line runs round and back the way it came */
                float cx = (float)xe + 22.0f, cy = (float)y - 7.5f;
                cv_linef((float)xe + 6.0f, (float)y, cx, (float)y, K_TEXT, 2.2f);
                cv_ring(cx, cy, 7.5f, 2.0f, K_TEXT);
                cv_tri(cx - 4.5f, cy - 7.5f, cx + 0.5f, cy - 11.0f, cx + 0.5f, cy - 4.0f, K_TEXT);
                s_cat(t, "back to ");
                t[8] = lt[0];
                t[9] = 0;
                cv_text(xe + 36, y - 16, &FONT_XS, t, K_TEXT);
            } else {                                 /* a buffer stop */
                cv_rect(xe + 9, y - 7, 3, 15, K_TEXT);
                cv_disc((float)xe + 7.0f, (float)y - 4.5f, 1.8f, K_TEXT);
                cv_disc((float)xe + 7.0f, (float)y + 4.5f, 1.8f, K_TEXT);
                cv_text(xe + 16, y - 7, &FONT_XS, "end", K_TEXT);
            }
        }
        if (pt >= 0 && pt / SG_W == vr)
            sg_train(SG_X0 + pt - r0, y, 0);
        else if (pt < 0 && cur < n && pn - ps >= 34 && ps / SG_W == vr && (ps % SG_W) + 34 <= SG_W)
            sg_train(SG_X0 + ps - r0 + 19, y, 1);   /* (stopped: parked at the selected stop, ready for PLAY) */

    }
    {                                                /* the selected slot, in words */
        char *e;
        if (cur < n) {
            e = num_s((uint32_t)cur + 1u, s_cat(t, "slot "));
            e = num_s((uint32_t)sg->slot[cur].scene + 1u, s_cat(e, " | scene "));
            num_s((uint32_t)at[cur], s_cat(e, " | starts bar "));
        } else
            s_cat(t, "the end | a white key adds a stop");
        {
            const char *seg[3];
            int ns = 0;
            int32_t x = 232;
            char *q = t;
            seg[ns++] = q;
            for (; *q; q++)
                if (*q == '|') {
                    q[-1] = 0;
                    seg[ns++] = q + 2;
                }
            for (int k = ns - 1; k >= 0; k--) {     /* right to left, a dot between */
                x -= text_w(&FONT_XS, seg[k]);
                cv_text(x, 104, &FONT_XS, seg[k], K_SEL);
                if (k) {
                    cv_disc((float)x - 5.0f, 111.5f, 1.3f, K_SEL);
                    x -= 10;
                }
            }
        }
    }
    cv_blit(0, MAIN_Y);
}

/* Steps: what a press writes and the bars' squares, then the bar's 16 cells */
#define ST_X 4
#define ST_CW 29
#define ST_Y 22
#define ST_CH 46
#define ST_TW (ST_CW - 8)   /* a cell's line: at most this wide, clear of a held cell's two-pixel frame */
#define ST_MID ((ST_CW - 2) / 2)   /* a line's centre */
#define ST_NUM 0            /* the rows' text y in a cell (ink 3-11, 13-21, 23-31, 33-41) */
#define ST_L1 10
#define ST_L2 20
#define ST_MK 30
#define ST_TICK 26          /* a kit's ticks' row */
#define st_font(s) (&FONT_XS)                        /* a cell's line */
/* a cell's text: its notes or chord, the shown drum sound, a throw track's effect (draw_steps) */
static int step_on(const step_t *st, int trk)       /* the cell is "on" for the sound shown (drums) or any note */
{
    if (is_drum(trk))
        return kit_has(st, ui.dsound + 1);
    return !step_empty(st);
}
/* a chord from its notes (3 or more): its shape (CHORD_SHAPE's, +1; 0 = none) and its root's pitch class. The root is
 * a note whose intervals to the others match a shape (the lowest tried first: C E G A is C6, A C E G Am7). A cell
 * names it in full when that fits ("C", "Am", "G7", "Dm7", "Bdim7"), else by the short form ("CM7", "C+", "Co",
 * "Ds2"): draw_steps; the chord editor and the strum player name it in full (chord_name) */
static const struct { uint16_t mask; const char *suf, *sh; } CHORD_SHAPE[] = {
    {0x091, "", ""}, {0x089, "m", "m"}, {0x491, "7", "7"}, {0x891, "maj7", "M7"}, {0x489, "m7", "m7"},
    {0x049, "dim", "o"}, {0x111, "aug", "+"}, {0x085, "sus2", "s2"}, {0x0A1, "sus4", "s4"},
    {0x249, "dim7", "o7"}, {0x449, "m7b5", "m7b5"}, {0x291, "6", "6"}, {0x289, "m6", "m6"}, {0x095, "add9", "add9"},
    {0x08D, "madd9", "madd9"}, {0x4A1, "7sus4", "7s4"}, {0x889, "mMaj7", "mM7"}};
static int chord_shape(const uint8_t *notes, int n, int *root)
{
    uint16_t pcs = 0;
    int lo = 0;
    if (n < 3)
        return 0;
    for (int i = 0; i < n; i++) {
        pcs |= (uint16_t)(1u << (notes[i] % 12));
        if (notes[i] < notes[lo])
            lo = i;
    }
    for (int pass = 0; pass < 2; pass++)              /* the bass note as the root first, then the others */
        for (int i = 0; i < n; i++) {
            int r = notes[pass ? i : lo] % 12;
            uint16_t rot = (uint16_t)(((pcs >> r) | (pcs << (12 - r))) & 0xFFFu);
            for (uint32_t k = 0; k < sizeof CHORD_SHAPE / sizeof CHORD_SHAPE[0]; k++)
                if (rot == CHORD_SHAPE[k].mask) {
                    *root = r;
                    return (int)k + 1;
                }
            if (!pass)
                break;
        }
    return 0;
}
/* a chord's name ("Cm7", "Bdim7") after "chord: " and before its notes, or nothing when its notes make no shape */
static char *chord_name(const uint8_t *notes, int n, char *e)
{
    int root = 0, sh = chord_shape(notes, n, &root);
    return sh ? s_cat(s_cat(s_cat(e, NOTE_NAME[root]), CHORD_SHAPE[sh - 1].suf), "  ") : e;
}
static void cell_note(int n, char *b)               /* a note's name in a cell; the octave -1's "C#-1" too wide: "C#" */
{
    note_name(n, b);
    if (text_w(&FONT_XS, b) > ST_TW)
        b[b[1] == '#' || b[1] == 'b' ? 2 : 1] = 0;
}
/* how many steps a step's note covers (its length, rounded up; the pattern's note length is under a step) */
static int step_cover(const step_t *st) { return st->len ? (step_len_q(st->len) + 3) / 4 : 1; }
/* WEAVE's Steps: the bar of its pattern, each step the tracks it weaves (names; more: "+n") and how long (the bar) */
static void draw_weave_steps(void)
{
    const wvpat_t *wp = &pats.weave[pats.scene[cur_scene()].weave % NPATT];
    int len = wp->len < 1 ? 16 : wp->len, pos = playing() ? (int)((seq.step_tick + (uint32_t)len - 1u) % (uint32_t)len) : -1;
    uint32_t h = hash_u(hash_u(2166136261u, 0x5Bu), (uint32_t)ui.bar | (uint32_t)(pos + 1) << 4 | (uint32_t)ui.wv_step << 12 |
                                          (uint32_t)len << 20);
    char t[24];
    for (int i = 0; i < NSTEP; i++)
        h = hash_u(h, wv_mask(&wp->st[i]) | (uint32_t)wp->st[i].len << 16);
    if (!begin_main(h))
        return;
    num_s((uint32_t)pats.scene[cur_scene()].weave % NPATT + 1u, s_cat(t, "WEAVE P"));
    cv_text(ST_X + 2, 0, &FONT_XS, t, K_TEXT);
    for (int i = 0; i < 16; i++) {
        int s = ui.bar * 16 + i, in = s < len;
        const wv_step_t *w = &wp->st[s];
        uint32_t m = wv_mask(w);
        int32_t x = ST_X + (i % 8) * ST_CW, y = ST_Y + (i / 8) * (ST_CH + 4);
        uint16_t fr = s == ui.wv_step ? K_SEL : s == pos ? K_REC : m ? K_TEXT : K_LINE;
        char n[4];
        if (!in)                                     /* (past the pattern's end: a dashed outline) */
            cv_frame(x, y, ST_CW - 2, ST_CH, 3, K_PANEL, K_BG);
        else {                                       /* a card: inked where a weave starts, red where it plays */
            cv_frame(x, y, ST_CW - 2, ST_CH, 3, fr, K_CARD);
            if (s == ui.wv_step || (s == pos && m))
                cv_frame(x + 1, y + 1, ST_CW - 4, ST_CH - 2, 2, fr, K_CARD);
        }
        num_s((uint32_t)s + 1u, n);
        cv_text(x + 4, y + ST_NUM, &FONT_XS, n, s == pos ? K_REC : in ? K_DIM : K_LINE);
        if (m) {                                     /* the first track woven (its ink), and how many more */
            int first = __builtin_ctz(m), more = __builtin_popcount(m) - 1;
            text_c(x + ST_MID, y + ST_L1, &FONT_XS, first < NTRK ? TRACK_NAME[first] : first == AUX_BRAIN ? "CO" : "TH",
                   trk_ink(first));
            if (more > 0) {
                num_s((uint32_t)more, s_cat(t, "+"));
                text_c(x + ST_MID, y + ST_L2, &FONT_XS, t, K_DIM);
            }
        }
    }
    for (int i = 0; i < 16; i++) {                   /* the lengths, over the cells: a bar from each step on */
        const wv_step_t *w = &wp->st[ui.bar * 16 + i];
        int32_t y = ST_Y + (i / 8) * (ST_CH + 4);
        if (!wv_mask(w) || ui.bar * 16 + i >= len)
            continue;
        for (int j = 0; j < (w->len ? w->len : 1) && i + j < 16 && (i + j) / 8 == i / 8; j++)
            cv_rect(ST_X + ((i + j) % 8) * ST_CW + (j ? 0 : 3), y + ST_CH - 6, ST_CW - (j ? 0 : 3) - (j + 1 == (w->len ? w->len : 1) ? 3 : (i + j) % 8 == 7 ? 2 : 0), 2, trk_ink(__builtin_ctz(wv_mask(w))));
    }
    cv_blit(0, MAIN_Y);
}
/* the Steps page's head: an instrument's name (nm) and what a step takes (t: its sound, or the notes held); an aux
 * track's in t alone ("fx: ...", "chord: ...") */
static void steps_head(int trk, char *nm, char *t)
{
    char *e = t;
    nm[0] = t[0] = 0;
    if (!is_aux(trk)) {
        for (int i = 0; i < NAME_LEN; i++)
            nm[i] = proj.trk[trk].name[i];
        nm[NAME_LEN] = 0;
        if (!nm[0])
            s_cat(nm, EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE]);
        if (is_drum(trk)) {
            s_cat(t, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
            return;
        }
    } else if (trk == AUX_PUNCH) {
        e = s_cat(t, "fx: ");
        if (ui.rem_fx && PX_VALID(ui.rem_fx - 1))
            s_cat(s_cat(e, PUNCH_NAME[(ui.rem_fx - 1) & 15]), ui.rem_fx - 1 >= PX_MEL ? " (melodic)" : " (drums)");
        else
            s_cat(e, "play a key on EDIT first");
        return;
    } else
        e = s_cat(t, "chord: ");
    for (int i = 0; i < ui.nrem && i < SN && (is_aux(trk) || pl_type(trk) != PL_RAND); i++) {   /* (the random
                                                     * player: no notes held for its steps) */
        if (i)
            e = s_cat(e, " ");
        e = note_name(ui.rem[i], e);
    }
}
/* ==== p2 pages (Steps, Bar, Engine, Mix): shared helpers, used only by the functions below them ==== */
/* a card of the manual: paper a shade lighter, an ink hairline, its top-left corner cut off (the punched card's) */
static void p2_card(int32_t x, int32_t y, int32_t w, int32_t h, int32_t cut, uint16_t fill)
{
    float tri[6] = {(float)x - 0.5f, (float)y - 0.5f, (float)(x + cut) + 0.5f, (float)y - 0.5f, (float)x - 0.5f,
                    (float)(y + cut) + 0.5f};
    cv_rect(x, y, w, h, fill);
    cv_poly(tri, 3, K_BG, 256u, 0);
    cv_rect(x + cut, y, w - cut, 1, K_TEXT);
    cv_rect(x + w - 1, y, 1, h, K_TEXT);
    cv_rect(x, y + h - 1, w, 1, K_TEXT);
    cv_rect(x, y + cut, 1, h - cut, K_TEXT);
    cv_linef((float)x + 0.5f, (float)(y + cut), (float)(x + cut), (float)y + 0.5f, K_TEXT, 1.0f);
}
/* a box's outline in lines (not an audited box: a bracket over a figure, never a frame for text) */
static void p2_outline(float x0, float y0, float x1, float y1, uint16_t c, float w)
{
    cv_linef(x0, y0, x1, y0, c, w);
    cv_linef(x1, y0, x1, y1, c, w);
    cv_linef(x1, y1, x0, y1, c, w);
    cv_linef(x0, y1, x0, y0, c, w);
}
/* a lock's mark: a small ink diamond (vermilion is the playhead's and recording's alone) */
static void p2_lock(float cx, float cy, uint16_t c)
{
    float d[8] = {cx, cy - 3.0f, cx + 3.0f, cy, cx, cy + 3.0f, cx - 3.0f, cy};
    cv_poly(d, 4, c, 256u, 0);
}

/* the knob turned a moment ago (1..4), 0 none: its callout on a figure shows it in blue, the hand and the drawing tied */
static int p2_touch(void) { return plat_ms() < ui.touch_until ? ui.touched + 1 : 0; }
/* a figure's title: its name large (FONT_L where it fits in w, else smaller), what it is in tracked capitals under it */
static void p2_title(const char *name, const char *what, int32_t w)
{
    const felucca_font_t *f = text_w(&FONT_L, name) <= w ? &FONT_L : text_w(F_VAL, name) <= w ? F_VAL : &FONT_B;
    cv_text(8, f == &FONT_L ? 2 : f == F_VAL ? 10 : 14, f, name, K_TEXT);
    if (what && what[0]) {
        if (tr_w(F_LAB, what, 1) <= w)
            tr_text(9, 36, F_LAB, what, K_DIM, 1);
        else
            text_fit(9, 36, F_LAB, what, w, K_DIM);
    }
}
/* a callout: knob n's circled number at (x, cy), its name after it (XS); returns the name's end */
static int32_t p2_callout(int32_t x, int32_t cy, int n, const char *name, uint16_t c)
{
    char d[2] = {(char)('0' + n), 0};
    if (p2_touch() == n)                             /* the knob being turned: its number inked blue */
        cv_disc((float)x, (float)cy - 0.5f, 6.0f, K_SEL), text_c(x + 1, cy - 6, F_LAB, d, K_BG);
    else
        circ_num(x, cy, d, c, K_BG);
    return name ? cv_text(x + 9, cy - 8, &FONT_XS, name, c) : x + 6;
}
/* Steps: the punched card (the sheet's "Steps"). Over it a line: the engine and the sound the keys write (a step held:
 * which), the pattern and the one cued next (vermilion), the bars (the one shown inked, the one playing underlined in
 * vermilion; past the pattern's length dashed). The card: the bar's 16 steps as columns (beats shaded, their numbers
 * above), a row per sound (a kit: the sounds with hits in the bar and the one selected, at most PC_ROWS; a throw track:
 * its effects), or a punched piano roll (a synth, the conductor: each note a punch at its pitch, a chord stacked, its
 * length a slot to the right; the C of each octave a dotted line; the held step's notes named at the left). A punch:
 * its height (a kit) or its width (a note) the velocity, hollow when a chance rules it; a length a line on to the
 * steps it covers. Above the rows a lock's diamond, under them a trick's letter. The playhead: a vermilion bracket and
 * triangle; the step-record cursor: a dashed vermilion bracket; held steps: blue. Outside the loop: hatched */
#define PC_X 8
#define PC_Y 19
#define PC_W 224
#define PC_H 99
#define PC_GX 54                                     /* the first column's left */
#define PC_CW 11                                     /* a column's width */
#define PC_NUM_Y 21                                  /* the beats' numbers */
#define PC_LOCK_Y 37                                 /* the locks' diamonds (centre) */
#define PC_ROW0 46                                   /* the first row's centre */
#define PC_RP 13                                     /* rows apart */
#define PC_ROWS 5                                    /* rows at most (11 px names, 13 px apart) */
#define PC_MK_Y 104                                  /* the tricks' letters */
#define PC_TOP 34                                    /* the columns' shading and brackets: from here */
#define PC_BOT 116                                   /* to here */
static int32_t pk_cx(int c) { return PC_GX + c * PC_CW + PC_CW / 2; }   /* a column's centre */
/* the PL_RAND player's slot, a throw's effect, a kit's sound: is row key k on step s, at what velocity and chance */
static int pk_hit(int trk, int pt, int mode, int k, int s, int *vel, int *cond)
{
    const tpat_t *tp = &pats.pat[trk][pt];
    const step_t *st = &tp->st[s];
    hit_t o;
    *vel = st->vel;
    *cond = st->cond;
    if (mode == 0) {                                 /* a kit's sound k */
        int v;
        if (!kit_has(st, k + 1))
            return 0;
        v = step_vel(pats.lane, trk, pt, s, k);
        *vel = v ? v : st->vel;
        hit_get(trk, pt, s, k, &o);
        *cond = o.cond;
        return 1;
    }
    if (mode == 1)                                   /* a throw's effect k */
        return !step_empty(st) && st->note[0] - 1 == k;
    return step_slot_on(tp, s) ? (step_slot_set(tp, s) ? 1 : 2) : 0;   /* the random player (2: none set, all on) */
}
static void pk_head(int trk, int pt, int drum)
{
    char a[24], b[40];
    int32_t xr = 232, x;
    int nb, pb = eng_step[trk] / 16;
    const tpat_t *tp = tpat(trk);
    nb = (tp->len + 15) / 16;
    for (int k = NBAR - 1; k >= 0; k--) {           /* the bars, right to left */
        int32_t bx = xr - 13;
        uint16_t c = k < nb ? K_TEXT : K_LINE;
        if (k == ui.bar)
            cv_round(bx, 1, 13, 13, 2, K_TEXT);
        else if (k < nb)
            cv_frame(bx, 1, 13, 13, 2, K_TEXT, K_BG);
        else {                                       /* past the pattern: dashed */
            cv_dash((float)bx, 1.5f, (float)(bx + 12), 1.5f, 2.0f, 2.0f, 1.0f, K_LINE);
            cv_dash((float)bx, 13.5f, (float)(bx + 12), 13.5f, 2.0f, 2.0f, 1.0f, K_LINE);
            cv_dash((float)bx + 0.5f, 1.0f, (float)bx + 0.5f, 14.0f, 2.0f, 2.0f, 1.0f, K_LINE);
            cv_dash((float)(bx + 12) + 0.5f, 1.0f, (float)(bx + 12) + 0.5f, 14.0f, 2.0f, 2.0f, 1.0f, K_LINE);
        }
        num_s((uint32_t)k + 1u, a);
        text_c(bx + 7, cap_y(1, 13, F_LAB), F_LAB, a, k == ui.bar ? K_BG : c);
        if (playing() && k == pb)                    /* the bar playing: underlined in vermilion */
            cv_rect(bx, 15, 13, 2, K_REC);
        xr = bx - 2;
    }
    xr -= 6;
    if (is_seqt(trk)) {                              /* the pattern; cued: the next one in vermilion */
        int cue = playing() && seq.pcue[trk] < NPATT && seq.pcue[trk] != pt;
        if (cue) {
            num_s((uint32_t)seq.pcue[trk] + 1u, s_cat(a, "P"));
            xr -= text_w(F_XB, a);
            cv_text(xr, 0, F_XB, a, K_REC);
            xr -= 11;
            cv_tri((float)xr + 7.0f, 7.5f, (float)xr + 2.0f, 4.5f, (float)xr + 2.0f, 10.5f, K_REC);
        }
        num_s((uint32_t)pt + 1u, s_cat(a, "P"));
        xr -= text_w(F_XB, a);
        cv_text(xr, 0, F_XB, a, K_TEXT);
    }
    xr -= 8;                                         /* (the room at the left ends here) */
    /* the left: a step held (blue) and the sound its knobs reach, or the engine and what the keys write */
    if (ui.held) {
        int n = __builtin_popcount(ui.held);
        if (n > 1)
            s_cat(num_s((uint32_t)n, b), " STEPS");
        else
            num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(b, "STEP "));
        x = tr_text(8, 3, F_LAB, b, K_SEL, 1) + 7;
        b[0] = 0;
        if (ui.lockpg)
            s_cat(s_cat(b, "locks: "), page_name_of(ui.lockpg - 1, trk));
        else if (drum && n == 1 && hit_edits(trk, step_shown(ui.held_first)))
            s_cat(b, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        if (b[0] && text_w(F_XB, b) > xr - x && ui.lockpg)
            s_cat(b, page_name_of(ui.lockpg - 1, trk));   /* (no room for "locks:": the page alone) */
        if (b[0])
            text_fit(x, 0, F_XB, b, xr - x, K_TEXT);
        return;
    }
    {
        char nm[NAME_LEN + 2], t[40];
        const char *tag, *v0 = "", *v1 = "";
        steps_head(trk, nm, t);
        if (trk == AUX_PUNCH) {                      /* the throw track: the effect a key writes */
            tag = "FX";
            v0 = ui.rem_fx && PX_VALID(ui.rem_fx - 1) ? t + 4 : "EDIT: play one";
        } else if (is_aux(trk)) {                    /* the conductor: the chord a key writes */
            tag = "CHORD";
            v1 = t + 7;
        } else {
            tag = EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE];
            if (drum)
                v0 = t;
            else {
                v0 = nm[0] && !s_eq(nm, tag) ? nm : "";
                v1 = t;
            }
        }
        up_copy(a, tag, sizeof a);
        x = tr_text(8, 3, F_LAB, a, K_DIM, 1) + 7;
        /* the name, then the notes the keys write (dim); short of room, the notes alone */
        if (v0[0] && v1[0] && text_w(F_XB, v0) + 6 + text_w(&FONT_XS, v1) > xr - x)
            v0 = "";
        if (v0[0])
            x = text_fit(x, 0, F_XB, v0, xr - x, K_TEXT) + 6;
        if (v1[0])
            text_fit(x, 0, &FONT_XS, v1, xr - x, v0[0] ? K_DIM : K_TEXT);
    }
}
/* the card's frame, shading, numbers; then each column's marks over what pk_card_draw put in it */
static void pk_card_draw(int trk, int pt, int ph)
{
    const tpat_t *tp = tpat(trk);
    int drum = is_drum(trk), b0 = ui.bar * 16, len = tp->len < 1 ? 1 : tp->len, lf, le, rnd = pl_type(trk) == PL_RAND;
    int mode = drum ? 0 : trk == AUX_PUNCH ? 1 : rnd ? 2 : 3;   /* rows of sounds / effects / one / a piano roll */
    int rows[PC_ROWS], nrows = 0, hidden = 0, sel = -1;
    char t[24];
    pat_loop(tp, &lf, &le);
    pk_head(trk, pt, drum);
    p2_card(PC_X, PC_Y, PC_W, PC_H, 7, K_CARD);
    for (int c = 4; c < 16; c += 8)                  /* beats 2 and 4 shaded */
        cv_rect(PC_GX + c * PC_CW, PC_Y + 1, 4 * PC_CW, PC_H - 2, K_BG);
    for (int c = 0; c < 16; c++) {                   /* outside the pattern, outside its loop: hatched */
        int s = b0 + c;
        if (s >= len || s < lf || s >= le) {
            float q[8] = {(float)(PC_GX + c * PC_CW), (float)PC_TOP, (float)(PC_GX + c * PC_CW + PC_CW), (float)PC_TOP,
                          (float)(PC_GX + c * PC_CW + PC_CW), (float)PC_BOT, (float)(PC_GX + c * PC_CW), (float)PC_BOT};
            cv_poly(q, 4, s >= len ? K_LINE : K_PANEL2, 256u, 4);
        }
    }
    if (lf > 0 || le < len)                          /* a cropped loop: an ink bar on the card's edge over it */
        for (int c = 0; c < 16; c++)
            if (b0 + c >= lf && b0 + c < le)
                cv_rect(PC_GX + c * PC_CW, PC_Y - 1, PC_CW, 3, K_TEXT);
    for (int c = 0; c < 16; c += 4) {                /* the beats' numbers (the playhead's: vermilion) */
        int now = playing() && ph == b0 + c;
        num_s((uint32_t)(b0 + c) + 1u, t);
        text_c(pk_cx(c) + 1, PC_NUM_Y, F_LAB, t, now ? K_REC : (ui.held >> c & 1u) ? K_SEL : K_DIM);
    }
    if (mode <= 2) {                                 /* the rows: which */
        int cnt[32] = {0}, nk = mode == 0 ? DL_NSOUND : mode == 1 ? NPUNCH_FX : 1, v, cd;
        uint32_t want = 0;
        sel = mode == 0 ? ui.dsound % DL_NSOUND : mode == 1 ? (ui.rem_fx ? ui.rem_fx - 1 : -1) : 0;
        if (nk > 32)
            nk = 32;
        for (int c = 0; c < 16; c++)
            if (b0 + c < len)
                for (int k = 0; k < nk; k++)
                    if (pk_hit(trk, pt, mode, k, b0 + c, &v, &cd))
                        cnt[k]++, want |= 1u << k;
        if (sel >= 0 && sel < nk)
            want |= 1u << sel;
        /* more than fit: the selected sound, then those with the most hits in the bar (the lower key first) */
        while (__builtin_popcount(want) > PC_ROWS) {
            int worst = -1;
            for (int k = 0; k < nk; k++)
                if ((want >> k & 1u) && k != sel && (worst < 0 || cnt[k] <= cnt[worst]))
                    worst = k;
            want &= ~(1u << worst);
            hidden++;
        }
        for (int k = 0; k < nk; k++)
            if (want >> k & 1u)
                rows[nrows++] = k;
        for (int r = 0; r < nrows; r++) {
            int32_t y = PC_ROW0 + r * PC_RP, k = rows[r], on = k == sel;
            const char *nm;
            if (mode == 0)
                nm = DL_SOUND_NAME[k];
            else if (mode == 1)
                nm = PUNCH_NAME[k & 15];
            else
                nm = "on";
            up_copy(t, nm, 8);
            if (text_w(F_LAB, t) <= PC_GX - 21)
                cv_text(12, cap_y(y - 6, 13, F_LAB), F_LAB, t, on ? K_SEL : K_TEXT);
            else if (tr_w(F_LAB, t, -1) <= PC_GX - 21)   /* ("SHAKER", "STRING": a pixel closer) */
                tr_text(12, cap_y(y - 6, 13, F_LAB), F_LAB, t, on ? K_SEL : K_TEXT, -1);
            else
                text_fit(12, cap_y(y - 6, 13, F_LAB), F_LAB, t, PC_GX - 21, on ? K_SEL : K_TEXT);
            if (on && mode != 2)                     /* the sound selected: a blue marker */
                cv_tri((float)(PC_GX - 7), (float)y - 3.5f, (float)(PC_GX - 2), (float)y, (float)(PC_GX - 7), (float)y + 3.5f, K_SEL);
            for (int c = 0; c < 16; c++)
                if (b0 + c < len && !pk_hit(trk, pt, mode, k, b0 + c, &v, &cd))
                    cv_rect(pk_cx(c), y, 2, 2, K_LINE);
        }
        if (hidden) {                                /* sounds left out: how many */
            num_s((uint32_t)hidden, s_cat(t, "+"));
            s_cat(t + s_len(t), " MORE");
            cv_text(12, PC_MK_Y, F_LAB, t, K_DIM);
        }
        /* the lengths first (under the punches): from each step on across the steps it covers */
        for (int s0 = 0; s0 < len; s0++) {
            const step_t *st = &tp->st[s0];
            int cv = step_cover(st);
            if (step_empty(st) || cv < 2)
                continue;
            for (int r = 0; r < nrows; r++) {
                if (!pk_hit(trk, pt, mode, rows[r], s0, &v, &cd))
                    continue;
                for (int kk = 1; kk < cv && kk < len; kk++) {
                    int j = (s0 + kk) % len - b0;
                    if (j < 0 || j > 15)
                        continue;
                    cv_rect(pk_cx(j) - 6 + (kk == 1 ? 3 : 0), PC_ROW0 + r * PC_RP, PC_CW + (kk == 1 ? -3 : 0), 2,
                            rows[r] == sel ? K_SEL : K_TEXT);
                }
            }
        }
        for (int r = 0; r < nrows; r++)              /* the punches */
            for (int c = 0; c < 16; c++) {
                int s = b0 + c, hv, out = s < lf || s >= le;
                int32_t y = PC_ROW0 + r * PC_RP, ph2;
                uint16_t ink = rows[r] == sel ? K_SEL : K_TEXT;
                if (s >= len || !(hv = pk_hit(trk, pt, mode, rows[r], s, &v, &cd)))
                    continue;
                if (out || hv == 2)
                    ink = K_FAINT;
                ph2 = 3 + v * 7 / 127;                /* the velocity: its height, 3 .. 10 */
                ph2 += !(ph2 & 1);
                if (cd)                              /* a chance: hollow */
                    p2_outline((float)pk_cx(c) - 3.0f, (float)(y - ph2 / 2) - 0.5f, (float)pk_cx(c) + 4.0f,
                               (float)(y + ph2 / 2) + 0.5f, ink, 1.0f);
                else
                    cv_rect(pk_cx(c) - 3, y - ph2 / 2, 7, ph2 + 1, ink);
            }
    } else {                                         /* a piano roll */
        int lo = 127, hi = 0, held_n = 0, hn[SN];
        float pp, yb;
        for (int c = 0; c < 16; c++) {
            const step_t *st = &tp->st[(b0 + c) % NSTEP];
            if (b0 + c >= len)
                continue;
            for (int i = 0; i < SN; i++)
                if (st->note[i]) {
                    lo = st->note[i] < lo ? st->note[i] : lo;
                    hi = st->note[i] > hi ? st->note[i] : hi;
                }
        }
        if (lo > hi)                                 /* nothing in the bar: the octave over middle C */
            lo = 60, hi = 72;
        if (hi - lo < 12) {
            int m = (lo + hi) / 2;
            lo = m - 6, hi = m + 6;
        }
        pp = fm_minf(5.0f, 56.0f / (float)(hi - lo));   /* pixels a semitone */
        yb = (float)(PC_ROW0 + 2 * PC_RP) + (float)(hi - lo) * pp * 0.5f;   /* lo's row, the roll centred */
#define PC_NY(n) (yb - (float)((n) - lo) * pp)
        for (int n = lo; n <= hi; n++)               /* each C: a dotted line */
            if (n % 12 == 0)
                cv_leader(PC_GX, PC_GX + 16 * PC_CW - 2, (int32_t)(PC_NY(n) + 0.5f), K_LINE);
        if (ui.held) {                               /* the held step's notes, named at their heights */
            const step_t *st = &tp->st[step_shown(ui.held_first)];
            for (int i = 0; i < SN; i++)
                if (st->note[i])
                    hn[held_n++] = st->note[i];
            for (int i = 1; i < held_n; i++)          /* low to high */
                for (int j = i; j > 0 && hn[j] < hn[j - 1]; j--) {
                    int q = hn[j];
                    hn[j] = hn[j - 1], hn[j - 1] = q;
                }
        }
        if (held_n) {
            int32_t prev = 1000, cx = pk_cx(ui.held_first);
            for (int i = 0; i < held_n; i++) {
                int32_t y = (int32_t)(PC_NY(hn[i]) + 0.5f), ty;
                if (y > prev - 12)
                    y = prev - 12;
                ty = y - 7;
                if (ty < PC_TOP - 2)
                    break;
                prev = y;
                note_name(hn[i], t);
                {
                    int32_t e = cv_text(10, ty, &FONT_XS, t, K_SEL);
                    cv_leader(e + 5, cx - 8, (int32_t)(PC_NY(hn[i]) + 0.5f), K_SEL);
                }
            }
        } else {                                     /* the roll's top and foot named */
            note_name(hi, t);
            cv_text(10, (int32_t)PC_NY(hi) - 7 < PC_TOP ? PC_TOP : (int32_t)PC_NY(hi) - 7, &FONT_XS, t, K_DIM);
            note_name(lo, t);
            cv_text(10, (int32_t)PC_NY(lo) - 7, &FONT_XS, t, K_DIM);
            cv_rect(PC_GX - 12, (int32_t)PC_NY(hi), 1, (int32_t)(PC_NY(lo) - PC_NY(hi)) + 1, K_LINE);   /* the range */
            cv_rect(PC_GX - 14, (int32_t)PC_NY(hi), 3, 1, K_LINE), cv_rect(PC_GX - 14, (int32_t)PC_NY(lo), 3, 1, K_LINE);
        }
        for (int s0 = 0; s0 < len; s0++) {           /* the lengths: a slot from each note on */
            const step_t *st = &tp->st[s0];
            int cv = step_cover(st);
            if (step_empty(st) || cv < 2)
                continue;
            for (int kk = 1; kk < cv && kk < len; kk++) {
                int j = (s0 + kk) % len - b0;
                if (j < 0 || j > 15)
                    continue;
                for (int i = 0; i < SN; i++)
                    if (st->note[i]) {
                        float y = PC_NY(st->note[i]);
                        uint16_t c = (ui.held >> ((s0 - b0) & 15) & 1u) && s0 / 16 == ui.bar ? K_SEL : K_TEXT;
                        cv_rect(pk_cx(j) - 6, (int32_t)(y - 1.5f), PC_CW, 1, c);
                        cv_rect(pk_cx(j) - 6, (int32_t)(y + 1.5f), PC_CW, 1, c);
                        if (kk == cv - 1 || j == 15)
                            cv_rect(pk_cx(j) + 4, (int32_t)(y - 1.5f), 1, 4, c);
                    }
            }
        }
        for (int c = 0; c < 16; c++) {               /* the punches */
            const step_t *st = &tp->st[(b0 + c) % NSTEP];
            int s = b0 + c, out = s < lf || s >= le;
            uint16_t ink = (ui.held >> c & 1u) ? K_SEL : out ? K_FAINT : K_TEXT;
            int32_t pw = 3 + st->vel * 4 / 127;       /* the velocity: its width, 3 .. 7 (each side of 1) */
            if (s >= len)
                continue;
            if (step_empty(st))
                continue;
            for (int i = 0; i < SN; i++)
                if (st->note[i]) {
                    float y = PC_NY(st->note[i]);
                    if (st->cond)
                        p2_outline((float)(pk_cx(c) - pw / 2) - 0.5f, y - 2.0f, (float)(pk_cx(c) + pw / 2 + 1) + 0.5f, y + 2.0f, ink, 1.0f);
                    else
                        cv_rect(pk_cx(c) - pw / 2, (int32_t)(y - 2.0f + 0.5f), pw + 1 - (pw & 1 ? 0 : 0), 5, ink);
                }
        }
#undef PC_NY
    }
    for (int c = 0; c < 16; c++) {                   /* each column: its lock, its tricks */
        int s = b0 + c;
        const step_t *st = &tp->st[s % NSTEP];
        hit_t hm;
        if (s >= len)
            continue;
        if (step_has_lock(pats.lane, trk, pt, s))
            p2_lock((float)pk_cx(c) + 0.5f, (float)PC_LOCK_Y, (ui.held >> c & 1u) ? K_SEL : K_TEXT);
        hit_marks(trk, pt, s, st, &hm);
        if ((hm.comp || hm.comp2) && !(hidden && c < 1)) {
            char g[2] = {COMP_GLYPH[(hm.comp ? hm.comp : hm.comp2) % NSC][0], 0};
            text_c(pk_cx(c) + 1, cap_y(PC_MK_Y, 13, &FONT_XS), &FONT_XS, g, (ui.held >> c & 1u) ? K_SEL : K_TEXT);
            if (hm.comp && hm.comp2)                 /* two tricks: a dot after the first's letter */
                cv_rect(pk_cx(c) + 5, PC_MK_Y + 9, 1, 1, K_TEXT);
        }
    }
    for (int c = 0; c < 16; c++)                     /* the steps held: a blue bracket */
        if (ui.held >> c & 1u)
            p2_outline((float)(pk_cx(c) - 5) - 0.5f, (float)PC_TOP - 0.5f, (float)(pk_cx(c) + 6) + 0.5f, (float)PC_BOT + 0.5f, K_SEL, 1.4f);
    {   /* the step-record cursor (or the first step blinking, waiting for a note): a dashed vermilion bracket */
        int cur = ui.rec_armed && ui.cursor / 16 == ui.bar ? ui.cursor % 16 : seq.rec_wait && ui.bar == 0 && (ui.frame / 8 & 1u) ? 0 : -1;
        if (cur >= 0) {
            float x0 = (float)(pk_cx(cur) - 5) - 1.0f, x1 = (float)(pk_cx(cur) + 6) + 1.0f;
            cv_dash(x0, (float)PC_TOP - 1.0f, x1, (float)PC_TOP - 1.0f, 2.0f, 2.0f, 1.2f, K_REC);
            cv_dash(x0, (float)PC_BOT + 1.0f, x1, (float)PC_BOT + 1.0f, 2.0f, 2.0f, 1.2f, K_REC);
            cv_dash(x0, (float)PC_TOP - 1.0f, x0, (float)PC_BOT + 1.0f, 3.0f, 2.0f, 1.2f, K_REC);
            cv_dash(x1, (float)PC_TOP - 1.0f, x1, (float)PC_BOT + 1.0f, 3.0f, 2.0f, 1.2f, K_REC);
        }
    }
    {   /* an empty pattern: the card unpunched, a paper punch resting on it, three chads beside */
        int any = 0;
        for (int s = 0; s < len && !any; s++)
            any = !step_empty(&tp->st[s]);
        if (!any && !rnd) {
            float bx = 186.0f, by = 108.0f;
            cv_rect(176, 107, 50, 4, K_TEXT);            /* its base */
            cv_rect(177, 108, 48, 2, K_CARD);
            cv_rect((int32_t)bx, 92, 10, 15, K_TEXT);    /* its body */
            cv_rect((int32_t)bx + 1, 93, 8, 14, K_CARD);
            cv_rect((int32_t)bx + 4, 88, 2, 5, K_TEXT);  /* the pin */
            cv_linef(bx + 5.0f, 88.0f, 222.0f, 78.0f, K_TEXT, 2.0f);   /* the lever, raised */
            cv_disc(222.0f, 78.0f, 3.2f, K_TEXT);
            cv_disc(bx + 5.0f, 88.0f, 2.0f, K_TEXT);
            cv_ring(166.0f, by + 1.0f, 2.2f, 0.9f, K_DIM);
            cv_ring(160.0f, by - 3.0f, 2.2f, 0.9f, K_DIM);
            cv_ring(171.0f, by - 6.0f, 2.2f, 0.9f, K_DIM);
        }
    }
    if (playing() && ph / 16 == ui.bar && ph < len) {   /* the playhead: a vermilion bracket, its triangle above */
        int c = ph % 16;
        float cx = (float)pk_cx(c) + 0.5f;
        p2_outline(cx - 5.5f, (float)PC_TOP + 0.5f, cx + 5.5f, (float)PC_BOT - 0.5f, K_REC, 1.4f);
        if (c % 4)
            cv_tri(cx, (float)PC_TOP - 1.0f, cx - 3.4f, (float)PC_TOP - 6.0f, cx + 3.4f, (float)PC_TOP - 6.0f, K_REC);
    }
}
static void draw_steps(void)
{
    if (sel_trk() == AUX_WEAVE) {
        draw_weave_steps();
        return;
    }
    {
    int trk = sel_trk(), pt = cur_pat(trk), ph = eng_step[trk];
    const tpat_t *tp = tpat(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0x57u), (uint32_t)trk | (uint32_t)pt << 4 | (uint32_t)ui.bar << 8 |
                                          (uint32_t)ui.held << 10 | (uint32_t)ui.dsound << 26);
    h = hash_u(h, (uint32_t)ph | (uint32_t)playing() << 8 | (uint32_t)ui.rec_armed << 9 | (uint32_t)ui.cursor << 16 |
                      (uint32_t)ui.lockpg << 24 | (uint32_t)(seq.rec_wait != 0) << 30);
    h = hash_u(h, ui.trickpg | (uint32_t)ui.rem_fx << 16 | (uint32_t)(ui.frame / 8 & 1u) << 31);
    h = hash_pat(h, tp);
    h = hash_s(hash_u(h, proj.trk[trk].par[TP_ENGINE]), proj.trk[trk].name);   /* the sound's name, shown */
    for (int i = 0; i < 4; i++)
        h = hash_u(h, ui.rem[i]);
    h = hash_u(h, lane_count(pats.lane));
    if (!is_seqt(trk)) {
        h = hash_u(h, 0xFEu);
        if (!begin_main(h))
            return;
        text_c(120, 40, &FONT_B, TRACK_NAME[trk], K_TEXT);
        text_c(120, 62, &FONT_S, "has no steps: turn ALGORITHM", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    h = hash_u(h, (uint32_t)seq.pcue[trk] | (uint32_t)k_now << 8);
    for (int k = 0; k < NLANE; k++) {                /* its locks, its sounds' own velocities and chances, in this bar */
        const lane_t *l = &pats.lane[k];
        if (!l->used || l->trk != trk || l->pat != pt)
            continue;
        h = hash_u(h, l->param);
        for (int i = 0; i < 16; i++)
            h = hash_u(h, l->val[(ui.bar * 16 + i) % NSTEP]);
    }
    h = hash_u(h, lane_count(pats.lane) | (uint32_t)pl_type(trk) << 16);
    if (!begin_main(h))
        return;
    pk_card_draw(trk, pt, ph);
    cv_blit(0, MAIN_Y);
    }
}

/* Bar (the sheet's "Bar"): the pattern's four bars side by side, each a miniature of its steps (a punch a step, as
 * tall as its velocity); a bar past the pattern's length dashed, "empty"; the bar the keys write framed heavy; the bar
 * playing bracketed in vermilion under it with the playhead in it, or the loop's span, vermilion while it plays it (a
 * jump or a loop waiting for the bar line: dashed). Under them the spec rows, dotted leaders to their values */
static void draw_bar(void)
{
    int trk = sel_trk();
    const tpat_t *tp = tpat(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0xBBu), (uint32_t)trk | (uint32_t)cur_pat(trk) << 4 | (uint32_t)tp->len << 8 |
                                          (uint32_t)tp->scale << 16 | (uint32_t)eng_step[trk] << 20 | (uint32_t)playing() << 28);
    char t[48];
    h = hash_u(h, (uint32_t)tp->nlen | (uint32_t)k_now << 8 | (uint32_t)ui.bar << 24 | (uint32_t)is_drum(trk) << 28);
    h = hash_u(h, (uint32_t)tp->lstart | (uint32_t)tp->lend << 8 | (uint32_t)seq.t[trk].bq_go << 16 | (uint32_t)seq.t[trk].bq_f << 24);
    h = hash_u(h, seq.t[trk].bq_e);
    if (is_seqt(trk))
        h = hash_pat(h, tp);
    if (!begin_main(h))
        return;
    if (!is_seqt(trk)) {
        p2_title("no bars", "THIS TRACK HAS NO STEPS", 220);
        cv_blit(0, MAIN_Y);
        return;
    }
    static const char *const STEP_FRAC[NTS] = {"1/32 note", "1/16 note", "1/8 note", "3/16 note", "1/4 note", "5/16 note",
                                               "3/8 note", "1/2 note", "a bar"};
    int len = tp->len < 1 ? 1 : tp->len, nb = (len + 15) / 16, ph = eng_step[trk], lf, le, play = playing();
    int go = seq.t[trk].bq_go, pf = seq.t[trk].bq_f, pe = seq.t[trk].bq_e, cropped;
    num_s((uint32_t)cur_pat(trk) + 1u, s_cat(t, "PATTERN "));
    tr_text(8, 3, F_LAB, t, K_DIM, 1);
    num_s((uint32_t)ui.bar + 1u, s_cat(t, "keys write bar "));
    text_r(232, 0, F_XB, t, K_TEXT);
    pat_loop(tp, &lf, &le);
    cropped = lf > 0 || le < len;
#define BX(b) (8 + (b) * 57)
#define SX(s) (BX((s) / 16) + 4 + ((s) % 16) * 3)   /* a step's place in its bar's miniature */
    for (int b = 0; b < NBAR; b++) {
        int32_t x = BX(b), y = 18;
        if (b >= nb) {                               /* past the pattern: dashed, empty */
            float x0 = (float)x + 0.5f, x1 = (float)(x + 52) + 0.5f, y0 = (float)y + 0.5f, y1 = (float)(y + 38) + 0.5f;
            cv_dash(x0, y0, x1, y0, 3.0f, 2.5f, 0.9f, K_LINE), cv_dash(x0, y1, x1, y1, 3.0f, 2.5f, 0.9f, K_LINE);
            cv_dash(x0, y0, x0, y1, 3.0f, 2.5f, 0.9f, K_LINE), cv_dash(x1, y0, x1, y1, 3.0f, 2.5f, 0.9f, K_LINE);
            num_s((uint32_t)b + 1u, t);
            cv_text(x + 5, y + 2, &FONT_B, t, K_LINE);
            text_c(x + 27, y + 21, &FONT_XS, "empty", K_FAINT);
            continue;
        }
        cv_frame(x, y, 53, 39, 3, K_TEXT, K_CARD);
        if (b == ui.bar)                             /* the keys write it: a heavy frame */
            p2_outline((float)x + 1.8f, (float)y + 1.8f, (float)(x + 51) - 0.8f, (float)(y + 37) - 0.8f, K_TEXT, 1.8f);
        num_s((uint32_t)b + 1u, t);
        cv_text(x + 5, y + 2, &FONT_B, t, K_TEXT);
        for (int i = 0; i < 16; i++) {               /* the miniature */
            int s = b * 16 + i, out = s < lf || s >= le;
            const step_t *st = &tp->st[s];
            if (s >= len) {
                if (i && s == len)
                    cv_dash((float)SX(s) - 1.0f, (float)(y + 21), (float)SX(s) - 1.0f, (float)(y + 35), 2.0f, 2.0f, 0.8f, K_DIM);
                continue;
            }
            if (step_empty(st))
                cv_rect(SX(s), y + 33, 1, 2, K_LINE);
            else {
                int hh = 4 + st->vel * 8 / 127;
                cv_rect(SX(s), y + 35 - hh, 2, hh, out ? K_FAINT : K_NOW);
            }
        }
        if (play && ph / 16 == b && ph < len)        /* the playhead in it */
            cv_rect(SX(ph), y + 19, 2, 17, K_REC);
    }
    {   /* under the bars: what plays (vermilion while it does), or what waits for the bar line (dashed) */
        int32_t a, e, y = 60;
        const char *what;
        uint16_t c;
        if (go != 0xFF && (pf > 0 || pe > 0)) {      /* a loop waiting */
            a = SX(pf), e = SX((pe ? pe : len) - 1) + 2, what = "loops next bar", c = K_REC;
            cv_dash((float)a, (float)(y + 4), (float)e, (float)(y + 4), 2.5f, 2.0f, 1.3f, c);
        } else if (go != 0xFF) {                     /* a jump waiting */
            a = BX(go / 16), e = a + 53, what = "jumps next bar", c = K_REC;
            cv_dash((float)a, (float)(y + 4), (float)e, (float)(y + 4), 2.5f, 2.0f, 1.3f, c);
        } else {
            if (cropped)
                a = SX(lf), e = SX(le - 1) + 2, what = "loops these steps";
            else if (play)
                a = BX(ph / 16), e = a + 53, what = "plays this bar";
            else
                a = e = 0, what = "";
            c = play ? K_REC : K_TEXT;
            if (e > a) {
                cv_rect(a, y, 1, 5, c), cv_rect(e - 1, y, 1, 5, c);
                cv_rect(a, y + 4, e - a, 1, c);
            }
        }
        if (what[0]) {
            int32_t w = text_w(&FONT_XS, what);
            if (e + 5 + w > 232 && a - 5 - w < 8)    /* (no room either side: the short word) */
                what = cropped ? "loop" : "plays", w = text_w(&FONT_XS, what);
            if (e + 5 + w <= 232)
                cv_text(e + 5, y - 3, &FONT_XS, what, c);
            else if (a - 5 - w >= 8)
                cv_text(a - 5 - w, y - 3, &FONT_XS, what, c);
        }
    }
#undef BX
#undef SX
    s_cat(num_s((uint32_t)len, t), len == 1 ? " step" : " steps");
    spec_row(8, 232, 74, "length", t, K_TEXT);
    spec_row(8, 232, 88, "one step", STEP_FRAC[tp->scale % NTS], K_TEXT);
    s_cat(num_s((uint32_t)tp->nlen, t), "% of a step");
    spec_row(8, 232, 102, "new notes last", t, K_TEXT);
    cv_blit(0, MAIN_Y);
}

/* the engines' pages: each engine drawn as an imagined 1980s machine (original and generic: no real product's panel),
 * a different silhouette each, whose own four controls are the page's four knobs: each carries its circled number and
 * its pointer, slider cap or lit button sits at the knob's value (redrawn as it turns). The engine's name big at the
 * top left, the preset's role, place and name at the top right. knit: a wood-cheeked polysynth with a slider bank;
 * rayon: a flat digital slab with membrane buttons, a display and a cartridge half in; jacquard: a rack unit with a
 * wave display and a big table selector; bellows: a combo organ on legs, its drawbars out, a rotary cabinet beside;
 * cane: a long modular case with patch cables hanging; voile: a wedge case with sliders and a gooseneck microphone;
 * acid: a small box with a row of knobs and a one-octave button keyboard, its cable coiled; kit: a drum machine, its
 * eleven pads (the sounds, named) and sixteen step buttons (the selected sound's steps in this bar); sample: a rack
 * sampler with a floppy disk half in and the real sample in its window; slice: a turntable and the real loop cut into
 * its slices; MIDI: a plain box, its DIN cable coiling off the page */
static const uint8_t *par_now(int trk)
{
    static uint8_t now[NTP];
    const uint8_t *par = proj.trk[trk].par;
    for (int i = 0; i < NTP; i++) {
        int lv = engine_live(trk, i);
        now[i] = lv >= 0 ? (uint8_t)lv : par[i];
    }
    return now;
}
static int8_t smp_col_lo[200], smp_col_hi[200];
static int smp_col_for = -1, smp_col_w;
static uint32_t smp_col_bank;
static void smp_columns(int idx, const smp_t *sm, int w)
{
    smp_cache_t c;
    uint32_t bank = (uint32_t)smp_user_count() << 16 ^ smp_user_bytes();
    if (idx == smp_col_for && bank == smp_col_bank && w == smp_col_w)
        return;
    smp_col_for = idx;
    smp_col_w = w;
    smp_col_bank = bank;
    smp_cache_reset(&c);
    for (int x = 0; x < w; x++) {
        int32_t i0 = (int32_t)((uint32_t)x * sm->n / (uint32_t)w), i1 = (int32_t)((uint32_t)(x + 1) * sm->n / (uint32_t)w);
        float lo = 0.0f, hi = 0.0f;
        for (int32_t i = i0; i < i1 || i == i0; i++) {
            float v = smp_one(&c, sm, i);
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        smp_col_lo[x] = (int8_t)(lo * 127.0f);
        smp_col_hi[x] = (int8_t)(hi * 127.0f);
    }
}
/* what an empty sample's window says: a moved factory sample is installed from the samples page */
static const char *smp_missing(int idx)
{
    return idx >= smp_factory() ? "empty" : idx >= SMP_BREAK ? "install the breaks" : "install the sounds";
}
/* the track's sound as a preset: its role and place among its engine's ("PAD 3/16") and its name; 0: not a preset */
static int p2_preset(int trk, char *role, char *name)
{
    int eg = proj.trk[trk].par[TP_ENGINE], n = 0, at = -1, rl = 0;
    const char *nm = proj.trk[trk].name;
    char *e;
    for (int i = 0; i < NPRESETS; i++) {
        if (PRESETS[i].par[TP_ENGINE] != eg)
            continue;
        if (at < 0) {
            int k = 0;
            while (k < NAME_LEN && nm[k] && PRESETS[i].name[k] == nm[k])
                k++;
            if (nm[0] && (k == NAME_LEN || (!nm[k] && !PRESETS[i].name[k])))
                at = n, rl = PRESETS[i].role % NRL;
        }
        n++;
    }
    for (int k = 0; k < NAME_LEN; k++)
        name[k] = nm[k];
    name[NAME_LEN] = 0;
    *role = 0;
    if (at < 0) {
        s_cat(role, name[0] ? "OWN SOUND" : "");
        return 0;
    }
    up_copy(role, RL_NAME[rl], 12);
    e = num_s((uint32_t)at + 1u, s_cat(role + s_len(role), " "));
    num_s((uint32_t)n, s_cat(e, "/"));
    return 1;
}
/* ---- the machines' parts ---- */
static float p2_u(const uint8_t *p, int par)       /* a knob's value, 0 .. 1 of its range */
{
    const tp_info_t *in = tp_info_par(par, p);
    int lo = in->lo, hi = in->hi > in->lo ? in->hi : in->lo + 1;
    return fm_clampf((float)(p[par] - lo) / (float)(hi - lo), 0.0f, 1.0f);
}
static void p2_panel(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill)   /* a panel: filled, ink edge */
{
    cv_rect(x, y, w, h, fill);
    p2_outline((float)x + 0.5f, (float)y + 0.5f, (float)(x + w - 1) + 0.5f, (float)(y + h - 1) + 0.5f, K_TEXT, 1.2f);
}
/* a knob: a cap, its pointer from 7 o'clock (0) to 5 o'clock (1); n > 0: its callout at (cx + dx, cy + dy) */
static void p2_knob(float cx, float cy, float r, float u, int n, int32_t dx, int32_t dy)
{
    float t = 0.375f + 0.75f * u, ca = fm_sin_turns(t + 0.25f), sa = fm_sin_turns(t);
    cv_disc(cx, cy, r, K_CARD);
    cv_ring(cx, cy, r, 1.2f, K_TEXT);
    cv_linef(cx + ca * r * 0.2f, cy + sa * r * 0.2f, cx + ca * (r - 1.0f), cy + sa * (r - 1.0f), K_TEXT, r > 6.0f ? 2.0f : 1.5f);
    if (n)
        p2_callout((int32_t)cx + dx, (int32_t)cy + dy, n, 0, K_TEXT);
}
/* a slider: its slot h px down from y, its cap at u (1: the top); n > 0: its callout over the slot */
static void p2_vslider(int32_t x, int32_t y, int32_t h, float u, int n)
{
    int32_t cy = y + 3 + (int32_t)((1.0f - u) * (float)(h - 6) + 0.5f);
    cv_rect(x - 1, y, 2, h, K_TEXT);
    cv_rect(x - 5, cy - 3, 10, 7, K_TEXT);
    cv_rect(x - 4, cy - 2, 8, 5, K_CARD);
    cv_rect(x - 4, cy, 8, 1, K_TEXT);
    if (n)
        p2_callout(x, y - 8, n, 0, K_TEXT);
}
static void p2_hslider(int32_t x, int32_t y, int32_t w, float u, int n)
{
    int32_t cx = x + 3 + (int32_t)(u * (float)(w - 6) + 0.5f);
    cv_rect(x, y - 1, w, 2, K_TEXT);
    cv_rect(cx - 3, y - 5, 7, 10, K_TEXT);
    cv_rect(cx - 2, y - 4, 5, 8, K_CARD);
    cv_rect(cx, y - 4, 1, 8, K_TEXT);
    if (n)
        p2_callout(x - 9, y, n, 0, K_TEXT);
}
static void p2_screw(float x, float y)
{
    cv_ring(x, y, 2.2f, 0.9f, K_TEXT);
    cv_linef(x - 1.4f, y + 1.4f, x + 1.4f, y - 1.4f, K_TEXT, 0.8f);
}
/* a keyboard: nw white keys in w px from x, h tall, the black ones bh tall */
static void p2_keys(int32_t x, int32_t y, int32_t w, int32_t h, int nw, int32_t bh)
{
    float kw = (float)w / (float)nw;
    cv_rect(x, y, w, h, K_CARD);
    p2_outline((float)x + 0.5f, (float)y + 0.5f, (float)(x + w - 1) + 0.5f, (float)(y + h - 1) + 0.5f, K_TEXT, 1.0f);
    for (int k = 1; k < nw; k++)
        cv_rect(x + (int32_t)(kw * (float)k), y, 1, h, K_TEXT);
    for (int k = 0; k < nw - 1; k++)
        if (k % 7 != 2 && k % 7 != 6)
            cv_rect(x + (int32_t)(kw * ((float)k + 0.68f)), y, (int32_t)(kw * 0.64f + 0.5f), bh, K_TEXT);
}
/* a patch cable hanging between two jacks, sagging by s px */
static void p2_cable(float x0, float y0, float x1, float y1, float s, uint16_t c)
{
    pen_t pn = {0};
    pn.w = 1.6f;
    cv_ring(x0, y0, 2.4f, 1.0f, K_TEXT), cv_ring(x1, y1, 2.4f, 1.0f, K_TEXT);
    for (int i = 0; i <= 20; i++) {
        float t = (float)i / 20.0f;
        pen_to(&pn, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t + s * 4.0f * t * (1.0f - t), c);
    }
}
static void p2_coil(float x, float y, int n, float r, uint16_t c)   /* a coiled cable along x */
{
    pen_t pn = {0};
    for (int i = 0; i <= n * 12; i++) {
        float t = (float)i / 12.0f;
        pen_to(&pn, x + t * r * 1.2f + r * fm_sin_turns(t + 0.25f) * 0.6f, y + r * fm_sin_turns(t), c);
    }
}

/* knit: a polysynth with wooden cheeks; four big sliders (wave, detune, voices, width) and quiet small knobs on its
 * panel, two wheels and a keyboard below */
static void m_knit(const uint8_t *p)
{
    for (int s = 0; s < 2; s++) {                    /* the cheeks: wood (hatched), rounded at the front */
        float x = s ? 220.0f : 8.0f, q[8] = {x, 40.0f, x + 12.0f, 40.0f, x + 12.0f, 116.0f, x, 116.0f};
        cv_poly(q, 4, K_DIM, 200u, 3);
        p2_outline(x + 0.5f, 40.5f, x + 11.5f, 115.5f, K_TEXT, 1.2f);
    }
    p2_panel(20, 44, 200, 34, K_CARD);
    for (int k = 0; k < 4; k++)                      /* the four sliders: the knobs */
        p2_vslider(40 + k * 22, 54, 22, p2_u(p, TP_E1 + k), k + 1);
    cv_rect(124, 48, 1, 26, K_LINE);
    for (int k = 0; k < 6; k++)                      /* the rest of the panel: small knobs, quiet */
        p2_knob(138.0f + (float)(k % 3) * 18.0f, 54.0f + (float)(k / 3) * 14.0f, 4.5f, (float)((k * 5 + 2) % 7) / 6.0f, 0, 0, 0);
    p2_panel(194, 50, 20, 8, K_TEXT);                /* a patch memory's display, dark */
    cv_rect(197, 62, 5, 5, K_TEXT), cv_rect(205, 62, 5, 5, K_LINE);
    p2_panel(20, 80, 20, 36, K_PANEL);               /* the wheels */
    cv_round(24, 84, 5, 28, 2, K_TEXT), cv_round(32, 84, 5, 28, 2, K_TEXT);
    cv_rect(25, 96, 3, 2, K_CARD), cv_rect(33, 90, 3, 2, K_CARD);
    p2_keys(40, 80, 180, 36, 22, 22);
}
/* rayon: a long flat slab, its display (the ratio), membrane buttons, three data sliders (bright, edge, tine), a
 * cartridge half in its slot, a thin keyboard */
static void m_rayon(const uint8_t *p)
{
    char num[16], unit[8];
    cv_rect(14, 44, 22, 13, K_TEXT);                 /* the cartridge, half in its slot */
    cv_rect(17, 47, 16, 4, K_CARD);
    p2_panel(8, 56, 224, 28, K_CARD);
    cv_rect(10, 56, 30, 2, K_TEXT);
    p2_panel(46, 61, 58, 18, K_TEXT);                /* the display: the ratio */
    tp_text(TP_E2, p[TP_E2], EG_RAYON, num, unit);
    s_cat(num + s_len(num), unit);
    text_c(75, 62, &FONT_XS, num, K_BG);
    p2_callout(75, 48, 2, 0, K_TEXT);
    for (int r = 0; r < 2; r++)                      /* the membrane buttons, two rows */
        for (int k = 0; k < 9; k++) {
            int32_t x = 110 + k * 7, y = 62 + r * 9;
            cv_rect(x, y, 5, 6, (k + r * 3) % 8 == 0 ? K_NOW : (k % 4 == 3 ? K_TEXT : K_LINE));
        }
    p2_hslider(178, 65, 30, p2_u(p, TP_E1), 0);      /* bright and edge: data sliders */
    p2_callout(218, 64, 1, 0, K_TEXT);
    p2_hslider(178, 76, 30, p2_u(p, TP_E3), 0);
    p2_callout(170, 48, 3, 0, K_TEXT);
    cv_linef(170.0f, 54.0f, 178.0f, 73.0f, K_TEXT, 0.7f);
    p2_keys(8, 86, 224, 16, 32, 10);
    p2_hslider(18, 112, 60, p2_u(p, TP_E4), 0);      /* tine: a strip along the front */
    p2_callout(88, 112, 4, 0, K_TEXT);
}
/* jacquard: a rack unit, its ears and screws; a wave display with the table's wave, a big selector for the table (1),
 * three small knobs (scan, bend, ripple) */
static void m_jacquard(const uint8_t *p)
{
    float tb = p2_u(p, TP_E1), sc = (float)p[TP_E2] / 127.0f, bd = (float)p[TP_E3] / 127.0f, rp = (float)p[TP_E4] / 127.0f;
    int ntab = tp_info_par(TP_E1, p)->hi - tp_info_par(TP_E1, p)->lo + 1;
    pen_t pn = {0};
    p2_panel(8, 44, 224, 62, K_CARD);
    for (int s = 0; s < 2; s++) {                    /* the ears: two slots each, a screw missing from one */
        float x = s ? 225.0f : 14.0f;
        cv_rect(s ? 218 : 20, 44, 1, 62, K_TEXT);
        cv_round((int32_t)x - 3, 52, 6, 10, 3, K_TEXT), cv_round((int32_t)x - 2, 53, 4, 8, 2, K_BG);
        cv_round((int32_t)x - 3, 88, 6, 10, 3, K_TEXT), cv_round((int32_t)x - 2, 89, 4, 8, 2, K_BG);
        p2_screw(x, 57.0f);
        if (!s)
            p2_screw(x, 93.0f);
    }
    p2_panel(26, 52, 78, 46, K_TEXT);                /* the display: the wave, lit on its glass */
    cv_rect(28, 75, 74, 1, K_DIM);
    pn.w = 1.4f;
    for (int x = 0; x <= 72; x++) {
        float ph = (float)x / 72.0f * 2.0f, b = ph + fm_sin_turns(ph) * bd * 0.15f;
        float v = fm_sin_turns(b) * (1.0f - sc * 0.6f) + fm_sin_turns(b * (2.0f + tb * 6.0f) + rp * 0.3f) * sc * 0.6f;
        pen_to(&pn, 29.0f + (float)x, 75.0f - v * 17.0f, K_BG);
    }
    for (int k = 0; k < ntab && ntab <= 16; k++) {   /* the big selector: a tick a table */
        float t = 0.375f + 0.75f * (float)k / (float)(ntab > 1 ? ntab - 1 : 1);
        cv_linef(136.0f + 19.0f * fm_sin_turns(t + 0.25f), 76.0f + 19.0f * fm_sin_turns(t), 136.0f + 22.0f * fm_sin_turns(t + 0.25f),
                 76.0f + 22.0f * fm_sin_turns(t), K_TEXT, 1.0f);
    }
    p2_knob(136.0f, 76.0f, 15.0f, tb, 1, -26, -20);
    cv_ring(136.0f, 76.0f, 11.0f, 0.7f, K_LINE);
    for (int k = 0; k < 3; k++)
        p2_knob(176.0f + (float)k * 17.0f, 84.0f, 6.5f, (float)p[TP_E2 + k] / 127.0f, 2 + k, 0, -16);
    cv_rect(170, 52, 44, 8, K_TEXT);                 /* a row of lamps, quiet */
    for (int k = 0; k < 5; k++)
        cv_rect(173 + k * 8, 54, 4, 4, k == 0 ? K_NOW : K_BG);
}
/* bellows: a combo organ on its legs, its six drawbars out as far as the registration (stops) and pedal set them;
 * the rotary speaker's cabinet beside it, its horn at an angle, its two knobs (rotor, rate) */
static void m_bellows(const uint8_t *p)
{
    float bars[6];
    organ_bars(p, bars);
    p2_panel(8, 60, 140, 26, K_CARD);                /* the organ's top: the drawbars' plate */
    for (int k = 0; k < 6; k++) {                    /* each drawbar out by its level */
        int32_t x = 24 + k * 13, out = 3 + (int32_t)(bars[k] * 21.0f);
        cv_rect(x, 60 - out, 7, out + 20, k == 0 ? K_NOW : K_TEXT);
        cv_rect(x + 1, 60 - out + 1, 5, 4, K_CARD);  /* its handle */
    }
    p2_callout(110, 50, 1, 0, K_TEXT);
    p2_callout(14, 54, 2, 0, K_TEXT);
    cv_rect(104, 66, 38, 14, K_TEXT);                /* tabs, quiet */
    for (int k = 0; k < 4; k++)
        cv_rect(106 + k * 9, 68, 7, 4, k == 1 ? K_CARD : K_DIM);
    p2_keys(8, 86, 140, 14, 21, 8);
    cv_linef(20.0f, 100.0f, 32.0f, 118.0f, K_TEXT, 1.6f);   /* the legs, crossed */
    cv_linef(32.0f, 100.0f, 20.0f, 118.0f, K_TEXT, 1.6f);
    cv_linef(124.0f, 100.0f, 136.0f, 118.0f, K_TEXT, 1.6f);
    cv_linef(136.0f, 100.0f, 124.0f, 118.0f, K_TEXT, 1.6f);
    p2_panel(166, 36, 62, 82, K_CARD);               /* the cabinet */
    for (int k = 0; k < 5; k++)                      /* its louvres */
        cv_rect(172, 42 + k * 4, 50, 2, K_TEXT);
    p2_panel(174, 64, 46, 24, K_BG);                 /* the horn in its window, turned as far as the rotor goes */
    {
        float a = 0.08f + 0.35f * p2_u(p, TP_E3), ca = fm_sin_turns(a + 0.25f) * 18.0f, sa = fm_sin_turns(a) * 6.0f;
        cv_linef(197.0f - ca, 76.0f - sa, 197.0f + ca, 76.0f + sa, K_TEXT, 3.0f);
        cv_disc(197.0f, 76.0f, 3.0f, K_TEXT);
    }
    p2_knob(184.0f, 102.0f, 6.0f, p2_u(p, TP_E3), 3, -14, -2);
    p2_knob(210.0f, 102.0f, 6.0f, p2_u(p, TP_E4), 4, 0, -16);
    cv_rect(170, 116, 6, 2, K_TEXT), cv_rect(218, 116, 6, 2, K_TEXT);   /* its feet */
}
/* cane: a long modular case, its rails and screws, four modules: the model's rotary switch (1), timbre and color (2,
 * 3), damp (4) and a module of jacks; patch cables hanging across */
static void m_cane(const uint8_t *p)
{
    p2_panel(8, 40, 224, 70, K_PANEL);
    cv_rect(8, 44, 224, 1, K_TEXT), cv_rect(8, 105, 224, 1, K_TEXT);   /* the rails */
    for (int k = 0; k < 8; k++)
        p2_screw(16.0f + (float)k * 30.0f, 42.0f), p2_screw(16.0f + (float)k * 30.0f, 108.0f);
    p2_panel(12, 46, 58, 58, K_CARD);                /* the model: a rotary switch */
    {
        int n = tp_info_par(TP_E1, p)->hi + 1;
        for (int k = 0; k < n && n <= 12; k++) {
            float t = 0.375f + 0.75f * (float)k / (float)(n - 1);
            cv_disc(41.0f + 17.0f * fm_sin_turns(t + 0.25f), 72.0f + 17.0f * fm_sin_turns(t), 1.3f, K_TEXT);
        }
        p2_knob(41.0f, 72.0f, 11.0f, p2_u(p, TP_E1), 1, -20, -20);
    }
    p2_panel(72, 46, 40, 58, K_CARD);
    p2_knob(92.0f, 62.0f, 8.0f, p2_u(p, TP_E2), 2, -14, -10);
    p2_knob(92.0f, 88.0f, 8.0f, p2_u(p, TP_E3), 3, -14, -10);
    p2_panel(114, 46, 30, 58, K_CARD);
    p2_knob(129.0f, 62.0f, 6.5f, p2_u(p, TP_E4), 4, 0, 14);
    p2_panel(146, 46, 82, 58, K_CARD);               /* the jacks */
    for (int k = 0; k < 8; k++)
        cv_ring(158.0f + (float)(k % 4) * 20.0f, 60.0f + (float)(k / 4) * 22.0f, 3.2f, 1.2f, K_TEXT);
    p2_cable(129.0f, 92.0f, 158.0f, 60.0f, 26.0f, K_TEXT);
    p2_cable(178.0f, 82.0f, 218.0f, 60.0f, 22.0f, K_NOW);
    p2_cable(198.0f, 82.0f, 41.0f, 96.0f, 18.0f, K_DIM);
}
/* voile: a wedge case (its back higher than its front), four sliders (model, timbre, color, breath), a gooseneck
 * microphone rising from it, its lead patched into the front */
static void m_voile(const uint8_t *p)
{
    float q[8] = {30.0f, 116.0f, 30.0f, 72.0f, 190.0f, 50.0f, 190.0f, 116.0f};
    cv_poly(q, 4, K_CARD, 256u, 0);
    cv_linef(30.0f, 116.0f, 30.0f, 72.0f, K_TEXT, 1.2f);
    cv_linef(30.0f, 72.0f, 190.0f, 50.0f, K_TEXT, 1.2f);
    cv_linef(190.0f, 50.0f, 190.0f, 116.0f, K_TEXT, 1.2f);
    cv_rect(30, 115, 161, 2, K_TEXT);
    for (int k = 0; k < 4; k++)                      /* the four sliders, long */
        p2_vslider(56 + k * 32, 84 - k * 4, 26, p2_u(p, TP_E1 + k), k + 1);
    cv_rect(36, 106, 148, 1, K_LINE);
    for (int k = 0; k < 5; k++)
        cv_ring(50.0f + (float)k * 32.0f, 111.0f, 2.4f, 1.0f, K_TEXT);
    {   /* the gooseneck and its microphone */
        pen_t pn = {0};
        pn.w = 2.2f;
        for (int i = 0; i <= 16; i++) {
            float t = (float)i / 16.0f;
            pen_to(&pn, 182.0f + 22.0f * t, 52.0f - 22.0f * t + 6.0f * t * t, K_TEXT);
        }
        cv_disc(212.0f, 32.0f, 8.0f, K_TEXT);         /* the microphone's head, its grille */
        for (int k = -1; k <= 1; k++)
            cv_rect(206, 31 + k * 3, 13, 1, K_CARD);
        p2_cable(178.0f, 111.0f, 204.0f, 112.0f, 8.0f, K_DIM);
        cv_linef(204.0f, 112.0f, 232.0f, 112.0f, K_DIM, 1.6f);
    }
}
/* acid: a small silver box: knobs in a row (wave a switch, accent, slide, tune), a row of lamps, a one-octave button
 * keyboard; its power lead coiled off to the left */
static void m_acid(const uint8_t *p)
{
    p2_panel(80, 44, 140, 72, K_CARD);
    cv_rect(80, 50, 140, 1, K_LINE);
    p2_screw(86.0f, 47.0f), p2_screw(214.0f, 47.0f), p2_screw(86.0f, 112.0f), p2_screw(214.0f, 112.0f);
    {   /* wave: a two-way switch */
        int sq = p[TP_E1] & 1;
        cv_rect(92, 60, 12, 6, K_TEXT);
        cv_rect(sq ? 98 : 93, 61, 5, 4, K_CARD);
        p2_callout(98, 74, 1, 0, K_TEXT);
    }
    for (int k = 1; k < 4; k++)
        p2_knob(108.0f + (float)k * 18.0f, 63.0f, 6.0f, p2_u(p, TP_E1 + k), k + 1, 0, 14);
    p2_knob(198.0f, 63.0f, 6.0f, 0.6f, 0, 0, 0);     /* (a knob that is not this page's) */
    for (int k = 0; k < 8; k++)                      /* the lamps */
        cv_rect(94 + k * 15, 84, 4, 3, k == 0 ? K_NOW : K_LINE);
    for (int k = 0; k < 8; k++) {                    /* one octave of buttons */
        cv_rect(92 + k * 15, 98, 12, 12, K_TEXT);
        cv_rect(93 + k * 15, 99, 10, 10, K_BG);
    }
    for (int k = 0; k < 7; k++)
        if (k != 2 && k != 6)
            cv_rect(100 + k * 15, 90, 12, 6, K_TEXT);
    cv_rect(72, 76, 8, 6, K_TEXT);                   /* the lead, coiled away */
    p2_coil(18.0f, 79.0f, 7, 5.0f, K_TEXT);
}
/* kit: a drum machine. A display (the key's model), four knobs (the key's), eleven pads named by their sound (the
 * selected one blue), and sixteen step buttons: the selected sound's steps in the bar shown, the playhead vermilion */
static void m_kit(const uint8_t *p, int trk)
{
    int sel = ui.dsound % DL_NSOUND, sm = p[TP_DRUM(sel, DP_MODEL)] % NDM;
    const tpat_t *tp = tpat(trk);
    char t[24];
    p2_panel(8, 36, 224, 82, K_CARD);
    p2_panel(14, 41, 80, 17, K_TEXT);                /* the display: the model */
    text_fit(18, 41, &FONT_XS, DM_NAME[sm], 72, K_BG);
    for (int k = 0; k < 4; k++) {                    /* the key's knobs, as the page has them */
        int q = tp_page_param(ui.page, k, trk);
        p2_knob(124.0f + (float)k * 29.0f, 50.0f, 6.5f, q >= 0 ? p2_u(p, q) : 0.0f, q >= 0 ? k + 1 : 0, -14, 0);
    }
    for (int k = 0; k <= DL_NSOUND; k++) {           /* the pads */
        int32_t x = 14 + (k % 6) * 36, y = 63 + (k / 6) * 21;
        if (k == DL_NSOUND) {
            text_c(x + 16, y + 2, F_LAB, "SEL", K_DIM);
            continue;
        }
        if (k == sel)
            cv_frame(x, y, 33, 18, 3, K_SEL, K_SEL);
        else
            cv_frame(x, y, 33, 18, 3, K_TEXT, K_BG);
        if (text_w(&FONT_XS, DL_SOUND_NAME[k]) <= 29)
            text_c(x + 17, y + 1, &FONT_XS, DL_SOUND_NAME[k], k == sel ? K_BG : K_TEXT);
        else
            tr_text(x + 17 - tr_w(&FONT_XS, DL_SOUND_NAME[k], -1) / 2, y + 1, &FONT_XS, DL_SOUND_NAME[k], k == sel ? K_BG : K_TEXT, -1);
    }
    for (int i = 0; i < 16; i++) {                   /* the step buttons */
        int s = ui.bar * 16 + i, on = s < tp->len && kit_has(&tp->st[s], sel + 1);
        int32_t x = 14 + i * 13 + (i / 4) * 2;
        uint16_t c = playing() && s == eng_step[trk] ? K_REC : on ? K_SEL : K_BG;
        cv_rect(x, 107, 10, 7, K_TEXT);
        cv_rect(x + 1, 108, 8, 5, c);
    }
    (void)t;
}
/* sample: a rack sampler: a floppy disk half in its drive, the sample's name on a small display, its waveform in a
 * window (the part that plays bright, the loop's ends marked), four knobs */
static void m_sample(const uint8_t *p)
{
    int idx = p[TP_E1] < smp_slots() ? p[TP_E1] : smp_slots() - 1, mode = p[TP_E4] % NSM;
    const smp_t *sm = smp_get(idx);
    p2_panel(8, 46, 224, 70, K_CARD);
    cv_rect(14, 50, 2, 62, K_LINE), cv_rect(224, 50, 2, 62, K_LINE);
    cv_rect(20, 54, 56, 5, K_TEXT);                  /* the drive's slot, the disk half out of it */
    p2_panel(26, 34, 44, 22, K_CARD);
    cv_rect(36, 34, 24, 8, K_TEXT), cv_rect(52, 35, 5, 6, K_CARD);   /* its shutter */
    cv_rect(30, 47, 36, 1, K_LINE), cv_rect(30, 50, 36, 1, K_LINE);
    p2_panel(20, 64, 56, 16, K_TEXT);                /* the sample's name */
    text_fit(23, 64, &FONT_XS, smp_name(idx), 50, K_BG);
    p2_panel(84, 52, 138, 40, K_BG);                 /* the window */
    if (!sm)
        text_c(153, 64, &FONT_XS, smp_missing(idx), K_DIM);
    else {
        int32_t w = 134, cy = 72, hh = 17;
        float n = (float)sm->n, s0 = (float)p[TP_E2] * (1.0f / 127.0f) * n, s1;
        int32_t xa, xb;
        s0 = fm_minf(s0, 0.99f * n);
        s1 = s0 + (n - s0) * (float)p[TP_E3] * (1.0f / 127.0f);
        xa = (int32_t)(s0 * (float)w / n), xb = (int32_t)(fm_maxf(s1, s0 + 32.0f) * (float)w / n);
        smp_columns(idx, sm, w);
        cv_rect(86, cy, w, 1, K_LINE);
        for (int32_t x = 0; x < w; x++) {
            int32_t yl = cy - smp_col_hi[x] * hh / 127, yh = cy - smp_col_lo[x] * hh / 127;
            cv_rect(86 + x, yl, 1, yh - yl + 1, x >= xa && x < xb ? K_TEXT : K_LINE);
        }
        if ((mode == SM_LOOP || mode == SM_PINGPONG) && sm->le > sm->ls) {   /* the sample's loop */
            cv_rect(86 + (int32_t)((float)sm->ls * (float)w / n), 54, 1, 36, K_NOW);
            cv_rect(86 + (int32_t)((float)sm->le * (float)w / n) - 1, 54, 1, 36, K_NOW);
        }
        cv_tri((float)(86 + (mode == SM_REVERSE ? xb : xa)), 57.0f, (float)(86 + (mode == SM_REVERSE ? xb : xa)) - 3.0f, 53.0f,
               (float)(86 + (mode == SM_REVERSE ? xb : xa)) + 3.0f, 53.0f, K_TEXT);
    }
    for (int k = 0; k < 4; k++)
        p2_knob(110.0f + (float)k * 30.0f, 104.0f, 6.0f, p2_u(p, TP_E1 + k), k + 1, -13, 0);
    p2_knob(36.0f, 102.0f, 7.0f, 0.8f, 0, 0, 0);     /* (the volume: not this page's) */
}
/* slice: a turntable: the record with its slices marked round its edge (2), the label (1: the loop), the tonearm,
 * the pitch fader (3); beside it the real loop cut into its slices, what the gate leaves out faint (4), the slice
 * playing tinted */
static void m_slice(const uint8_t *p, int trk)
{
    int idx = p[TP_E1] < smp_slots() ? p[TP_E1] : smp_slots() - 1, ns = sl_count(p[TP_E2]), sl = -1, on;
    const smp_t *sm = smp_get(idx);
    int32_t pos = 0;
    int32_t gate = (p[TP_E4] > 127 ? 127 : p[TP_E4]) * 1000 / 127;
    gate = gate < 20 ? 20 : gate;
    p2_panel(8, 40, 112, 78, K_CARD);                /* the plinth */
    cv_disc(56.0f, 79.0f, 32.0f, K_TEXT);            /* the record, its grooves */
    for (int k = 0; k < 3; k++)
        cv_ring(56.0f, 79.0f, 15.0f + (float)k * 5.0f, 0.6f, K_DIM);
    for (int k = 0; k < ns; k++) {                   /* the slices round its edge */
        float t = (float)k / (float)ns;
        cv_linef(56.0f + 27.0f * fm_sin_turns(t), 79.0f - 27.0f * fm_sin_turns(t + 0.25f), 56.0f + 32.0f * fm_sin_turns(t),
                 79.0f - 32.0f * fm_sin_turns(t + 0.25f), K_BG, 1.2f);
    }
    cv_disc(56.0f, 79.0f, 9.0f, K_NOW);               /* the label */
    cv_disc(56.0f, 79.0f, 1.5f, K_BG);
    p2_callout(20, 48, 1, 0, K_TEXT);
    p2_callout(92, 48, 2, 0, K_TEXT);
    cv_disc(100.0f, 50.0f, 4.0f, K_TEXT);             /* the tonearm */
    cv_linef(100.0f, 50.0f, 96.0f, 88.0f, K_TEXT, 1.8f);
    cv_linef(96.0f, 88.0f, 84.0f, 96.0f, K_TEXT, 1.8f);
    p2_vslider(111, 70, 40, p2_u(p, TP_E3), 0);
    p2_callout(111, 62, 3, 0, K_TEXT);
    if (!sm) {
        text_c(178, 70, &FONT_XS, smp_missing(idx), K_DIM);
        return;
    }
    {
        int32_t x0 = 128, w = 102, wy = 52, wh = 44, cy = wy + wh / 2;
        on = engine_slice_now(trk, &sl, &pos) && sl >= 0 && sl < ns;
        smp_columns(idx, sm, w);
        if (on)
            cv_rect(x0 + sl * w / ns, wy, (sl + 1) * w / ns - sl * w / ns, wh, K_TINT);
        cv_rect(x0, cy, w, 1, K_LINE);
        for (int32_t x = 0; x < w; x++) {
            int k = x * ns / w;
            int32_t a = k * w / ns, b = (k + 1) * w / ns, in = (x - a) * 1000 < gate * (b - a);
            int32_t yl = cy - smp_col_hi[x] * (wh / 2 - 1) / 127, yh = cy - smp_col_lo[x] * (wh / 2 - 1) / 127;
            cv_rect(x0 + x, yl, 1, yh - yl + 1, !in ? K_LINE : on && k == sl ? K_NOW : K_TEXT);
        }
        for (int k = 1; k < ns; k++)                 /* the cuts: the razor's */
            cv_rect(x0 + k * w / ns, wy - 3, 1, wh + 6, K_TEXT);
        p2_outline((float)x0 - 0.5f, (float)wy - 3.5f, (float)(x0 + w) + 0.5f, (float)(wy + wh) + 3.5f, K_TEXT, 1.0f);
        if (on)                                      /* where it plays */
            cv_rect(x0 + (int32_t)((float)pos * (float)w / (float)sm->n), wy, 1, wh, K_REC);
        p2_callout(x0 + w - 8, wy + wh + 13, 4, 0, K_TEXT);
        cv_text(x0, wy + wh + 6, &FONT_XS, smp_name(idx), K_DIM);
    }
}
/* MIDI: a plain box: four knobs (the page's, their CCs under them), its channel, a lamp lit as a note goes out, a DIN
 * socket at its side and the cable from it coiling off the page */
static void m_midi(const uint8_t *p, int trk, int second, int hit)
{
    char t[12];
    p2_panel(14, 50, 140, 54, K_CARD);
    for (int k = 0; k < 4; k++) {
        int pk = second ? TP_X1 + k : TP_E1 + k;
        p2_knob(40.0f + (float)k * 30.0f, 72.0f, 7.0f, (float)p[pk] / 127.0f, k + 1, -14, 0);
        num_s(p[midi_cc_par(second ? k + 4 : k)] % NMIDI_CC, s_cat(t, "CC"));
        text_c(40 + k * 30, 84, &FONT_XS, t, K_DIM);
    }
    num_s((uint32_t)trk + 1u, s_cat(t, "CH "));
    tr_text(20, 53, F_LAB, t, K_TEXT, 1);
    cv_disc(146.0f, 57.0f, 2.6f, hit > 40 ? K_REC : K_LINE);
    cv_ring(154.0f, 77.0f, 7.0f, 1.6f, K_TEXT);       /* the socket on its side, the plug in it */
    cv_rect(154, 72, 10, 10, K_TEXT);
    p2_coil(166.0f, 77.0f, 4, 6.0f, K_TEXT);
    cv_linef(196.0f, 77.0f, 240.0f, 92.0f, K_TEXT, 1.8f);
}
static void draw_engine(void)
{
    int trk = sel_trk(), eg = proj.trk[trk].par[TP_ENGINE] % NENGINE;
    const uint8_t *p = par_now(trk);
    char role[24], name[NAME_LEN + 2];
    int pre;
    uint32_t h = hash_u(hash_u(2166136261u, 0xE2u), (uint32_t)trk | (uint32_t)eg << 4 | (uint32_t)ui.dsound << 8 |
                                                   (uint32_t)p2_touch() << 12 | (uint32_t)ui.page << 16);
    for (int i = 0; i < 4; i++)
        h = hash_u(h, p[TP_E1 + i] | (uint32_t)p[TP_X1 + i] << 8);
    if (eg == EG_MIDI) {                             /* (a note out: its lamp lit, easing out) */
        eng_midi_hit[trk] = (uint8_t)(eng_midi_hit[trk] > 20 ? eng_midi_hit[trk] - 20 : 0);
        h = hash_u(h, eng_midi_hit[trk] > 40);
        for (int k = 0; k < 8; k++)
            h = hash_u(h, p[midi_cc_par(k)]);
    }
    if (eg == EG_SAMPLE || eg == EG_SLICE)           /* (the bank: an upload changes what the window shows) */
        h = hash_u(h, (uint32_t)smp_user_count() << 16 ^ smp_user_bytes());
    if (eg == EG_SLICE) {                            /* the slice playing and where */
        int sl;
        int32_t pos;
        const smp_t *sm = smp_get(p[TP_E1]);
        if (engine_slice_now(trk, &sl, &pos) && sm)
            h = hash_u(h, (uint32_t)(sl + 1) | (uint32_t)((float)pos * 100.0f / (float)sm->n) << 8);
    }
    if (eg == EG_DRUMLINE) {
        int sel = ui.dsound % DL_NSOUND;
        const tpat_t *tp = tpat(trk);
        for (int k = 0; k < 4; k++) {
            int q = tp_page_param(ui.page, k, trk);
            h = hash_u(h, (q >= 0 ? p[q] : 0u) | (uint32_t)p[TP_DRUM(sel, DP_MODEL)] << 8);
        }
        for (int i = 0; i < 16; i++)
            h = hash_u(h, kit_mask(&tp->st[(ui.bar * 16 + i) % NSTEP]) | (uint32_t)tp->len << 16);
        h = hash_u(h, (uint32_t)(playing() ? eng_step[trk] + 1 : 0) | (uint32_t)ui.bar << 8);
    }
    if (eg == EG_ACID)
        h = hash_u(h, 0xAC);
    h = hash_s(h, proj.trk[trk].name);
    if (!begin_main(h))
        return;
    cv_text(8, 0, &FONT_L, EG_NAME[eg], K_TEXT);
    pre = p2_preset(trk, role, name);
    {
        int32_t lx = 8 + text_w(&FONT_L, EG_NAME[eg]) + 12;
        if (eg == EG_DRUMLINE) {                     /* a kit: the key selected */
            s_cat(role, "KEY");
            s_cat(name, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
            pre = 1;
        }
        if (role[0])
            text_r(232, 3, F_LAB, role, K_DIM);
        if (name[0])
            text_fit(232 - (text_w(F_XB, name) < 232 - lx ? text_w(F_XB, name) : 232 - lx), 16, F_XB, name, 232 - lx,
                     pre ? K_TEXT : K_DIM);
    }
    switch (eg) {
    case EG_KNIT: m_knit(p); break;
    case EG_RAYON: m_rayon(p); break;
    case EG_JACQUARD: m_jacquard(p); break;
    case EG_BELLOWS: m_bellows(p); break;
    case EG_CANE: m_cane(p); break;
    case EG_VOILE: m_voile(p); break;
    case EG_ACID: m_acid(p); break;
    case EG_SAMPLE: m_sample(p); break;
    case EG_SLICE: m_slice(p, trk); break;
    case EG_MIDI: m_midi(p, trk, ui.page == P_SUB, eng_midi_hit[trk]); break;
    default: m_kit(p, trk); break;
    }
    cv_blit(0, MAIN_Y);
}

/* the aux tracks' pages */
/* a page's title (FONT_B) and the line under it that says what it does. Returns the y under them */
static int32_t title_sub(const char *title, const char *sub, uint16_t sc)
{
    cv_text(12, 4, &FONT_B, title, K_TEXT);
    cv_text(12, 24, F_CAP, sub, sc);
    return 40;
}
/* the conductor (COND): its key and scale as a title, and drawn as an octave of keys with the scale's notes dotted (the
 * key's own ringed), the chord it holds now in vermilion; its knobs called out at the right (key, scale, link), or on
 * its Play and Follow pages what they do; along the foot the tracks that follow it (filled: diatonic or harmony;
 * outlined: transpose or chord degree; a bar under: the harmony modes; faint: not following; the FOLLOW track in its
 * ink) */
#define BR_KX 8                                      /* the keys: seven white keys BR_KW wide from BR_KX, BR_KY .. + BR_KH */
#define BR_KW 16
#define BR_KY 34
#define BR_KH 46
static const uint8_t BR_WHITE[7] = {0, 2, 4, 5, 7, 9, 11};
static int32_t br_key_x(int n)                       /* a semitone's key centre */
{
    static const uint8_t BX[12] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
    int black = (0x54Au >> n) & 1u;
    return BR_KX + BX[n] * BR_KW + (black ? 0 : BR_KW / 2);
}
static void draw_brain(void)
{
    const uint8_t *p = proj.trk[AUX_BRAIN].par;
    int key = seq.brain_key % 12, sc = seq.brain_scale % NBS, deg = (int8_t)seq.brain_deg, src = p[TP_E1] % 9u;
    uint32_t chord = seq.brain_chord, ct = 0;
    uint16_t mask = BRAIN_SCALE[sc];
    uint32_t h = hash_u(hash_u(hash_u(2166136261u, 0xB0u), (uint32_t)key | (uint32_t)sc << 4 |
                                          (uint32_t)(deg + 64) << 8 | (uint32_t)src << 16 | (uint32_t)ui.page << 20 |
                                          (uint32_t)p[TP_E4] << 26), chord ^ (uint32_t)playing() << 31);
    char t[48], *e;
    for (int i = 0; i < NTRK; i++)
        h = hash_u(h, proj.trk[i].par[TP_BRAIN] | (uint32_t)is_drum(i) << 4);
    if (!begin_main(h))
        return;
    for (int i = 0; i < SN; i++)                     /* the chord's tones, as semitones */
        if (chord >> (8 * i) & 0xFFu)
            ct |= 1u << ((chord >> (8 * i) & 0xFFu) % 12u);
    e = s_cat(s_cat(t, NOTE_NAME[key]), " ");
    s_cat(e, BS_NAME[sc]);
    cv_text(8, 0, &FONT_L, t, K_TEXT);
    e = s_cat(t, "degree ");
    if (deg > 0)
        e = s_cat(e, "+");
    num_i(deg, e);
    text_r(232, 12, F_XB, t, deg ? K_TEXT : K_DIM);
    /* the octave: white keys, then the black over them; the scale's notes dotted, the key's ringed, the chord red */
    cv_rect(BR_KX, BR_KY, 7 * BR_KW + 1, BR_KH, K_TEXT);
    for (int w = 0; w < 7; w++)                      /* (playing: the chord's keys held down, a shade deeper) */
        cv_rect(BR_KX + w * BR_KW + 1, BR_KY + 1, BR_KW - 1, BR_KH - 2,
                playing() && (ct >> BR_WHITE[w] & 1u) ? K_PANEL : K_CARD);
    for (int n = 1; n < 12; n += n == 3 || n == 10 ? 3 : 2)   /* C# D#, F# G# A# */
        cv_rect(br_key_x(n) - 5, BR_KY, 10, 28, K_TEXT);
    for (int n = 0; n < 12; n++) {
        int black = (0x54Au >> n) & 1u, s = (n - key + 12) % 12;
        float x = (float)br_key_x(n), y = black ? (float)BR_KY + 20.0f : (float)BR_KY + BR_KH - 9.0f;
        uint16_t bg = black ? K_TEXT : K_CARD, ink = black ? K_BG : K_TEXT;
        if (ct >> n & 1u)                            /* (red: sounding now) */
            cv_disc(x, y, 3.6f, playing() ? K_REC : ink);
        else if (mask >> s & 1u)
            cv_disc(x, y, 2.4f, ink);
        if (n == key) {
            cv_ring(x, y, 5.6f, 1.2f, ink);
            (void)bg;
        }
    }
    if (ui.page != P_PLAY && ui.page != P_SUB)
        circ_num(br_key_x(key), BR_KY + BR_KH + 9, "1", K_TEXT, K_BG);
    if (ui.page == P_PLAY || ui.page == P_SUB) {     /* Play: the routing; Follow: where the chords come from */
        const char *s;
        if (ui.page == P_PLAY)
            s = "black keys 1 to 8 route a track to the conductor";
        else if (!src)
            s = "off: the chords come from its own keys and steps";
        else if (is_drum(src - 1)) {
            s_cat(s_cat(t, TRACK_NAME[src - 1]), " is a kit: it has no notes to give");
            s = t;
        } else {
            s_cat(s_cat(t, TRACK_NAME[src - 1]), "'s notes set the chord; it follows nothing");
            s = t;
        }
        wrap(132, 32, 100, &FONT_XS, s, K_DIM);
    } else {                                         /* the knobs, called out */
        static const char *const NM[3] = {"key", "scale", "link"};
        const char *v[3];
        char cb[24];
        v[0] = NOTE_NAME[key];
        v[1] = BS_NAME[sc];
        v[2] = LINK_NAME[p[TP_E4] % 9];
        for (int i = 0; i < 3; i++) {
            char nn[2] = {(char)('1' + i), 0};
            circ_num(138, 40 + i * 16, nn, K_TEXT, K_BG);
            spec_row(147, 232, 33 + i * 16, NM[i], v[i], K_TEXT);
        }
        e = cb;
        *e = 0;
        for (int n = 0; n < 12; n++)                 /* the chord now, by name */
            if (ct >> n & 1u)
                e = s_cat(e == cb ? e : s_cat(e, " "), NOTE_NAME[n]);
        if (*cb)
            spec_row(132, 232, 81, "chord", cb, playing() ? K_REC : K_TEXT);
    }
    tr_text(8, 103, F_LAB, "FOLLOW", K_DIM, 1);
    for (int i = 0; i < NTRK; i++) {
        int32_t x = 60 + i * 22;
        int m = proj.trk[i].par[TP_BRAIN] % NBM;
        uint16_t c;
        num_s((uint32_t)i + 1u, t);
        if (src == i + 1) {                          /* the FOLLOW track: the chords come from it */
            cv_round(x, 99, 18, 18, 4, trk_ink(i));
            text_c(x + 9, 100, &FONT_B, t, K_BG);
            continue;
        }
        if (m == BM_DIATONIC || m == BM_HARMONY)
            cv_round(x, 99, 18, 18, 4, K_TEXT);
        else
            cv_frame(x, 99, 18, 18, 4, m ? K_TEXT : K_LINE, K_BG);
        c = m == BM_DIATONIC || m == BM_HARMONY ? K_BG : m ? K_TEXT : K_FAINT;
        if (m >= BM_HARMONY)                          /* the harmony modes: a bar under the number */
            cv_rect(x + 5, 113, 8, 1, c);
        text_c(x + 9, 100, &FONT_B, t, c);
    }
    cv_blit(0, MAIN_Y);
}
/* the throw track's page: the effects held now, large (vermilion: in effect now); the two octaves of keys drawn as a
 * keyboard, the drums' effects on the left octave, the melodic ones on the right, each key in effect now vermilion,
 * a key with no effect blank; the drum octave's initials under it; latch or while held */
static void draw_punch(void)
{
    uint32_t dm = eng_punch_drum, mm = eng_punch_mel;
    uint32_t h = hash_u(hash_u(2166136261u, 0xD1u), dm ^ (mm << 12) ^ (ui.keys << 3) ^ (ui.punch_latched << 7));
    char t[64], *e;
    int n = 0;
    h = hash_u(h, proj.trk[AUX_PUNCH].par[TP_E1]);
    if (!begin_main(h))
        return;
    e = t;
    *t = 0;
    for (int fx = 0; fx < NPUNCH_FX; fx++) {
        int on = fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u);
        if (!on || !PX_VALID(fx))
            continue;
        if (n++)
            e = s_cat(e, ", ");
        if (s_len(t) < 40)
            e = s_cat(e, PUNCH_NAME[fx & 15]);
    }
    if (!n)
        cv_text(8, 4, F_VAL, "hold a key", K_DIM);
    else {   /* (the effects held: smaller as more are held, cut only past the smallest) */
        const felucca_font_t *f = text_w(F_VAL, t) <= 224 ? F_VAL : text_w(&FONT_B, t) <= 224 ? &FONT_B : &FONT_XS;
        text_fit(8, f == F_VAL ? 4 : 8, f, t, 224, K_REC);
    }
    tr_text(8, 30, F_LAB, "DRUMS", K_DIM, 1);
    tr_text(232 - tr_w(F_LAB, "MELODIC", 1), 30, F_LAB, "MELODIC", K_DIM, 1);
    for (int k = 0; k < 16; k++) {                   /* the white keys */
        int32_t x = 8 + k * 14;
        int fx = punch_of_key(WHITE_K[k]);
        int on = fx >= 0 && (fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u));
        cv_rect(x, 44, 15, 52, K_TEXT);
        cv_rect(x + 1, 45, 13, 50, on ? K_REC : fx < 0 ? K_PANEL : K_CARD);
        if (fx >= 0 && k < 7) {                      /* the drum octave's initials: m s r e b f j */
            char ini[2] = {PUNCH_NAME[fx & 15][0], 0};
            text_c(x + 8, 98, &FONT_XS, ini, K_DIM);
        }
    }
    for (int k = 0; k < 11; k++) {                   /* the black keys */
        int key = BLACK_K[k], fx = punch_of_key(key);
        int on = fx >= 0 && (fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u));
        int32_t wi = 0;
        while (WHITE_K[wi] < key)
            wi++;
        cv_rect(8 + wi * 14 - 4, 44, 9, 30, on ? K_REC : K_TEXT);
        if (fx < 0)
            cv_rect(8 + wi * 14 - 3, 45, 7, 28, K_DIM);
    }
    cv_rect(8 + 7 * 14, 38, 2, 62, K_TEXT);          /* the octaves' divide */
    text_r(232, 100, &FONT_XS, proj.trk[AUX_PUNCH].par[TP_E1] ? "latch on" : "while held", K_DIM);
    cv_blit(0, MAIN_Y);
}
/* the reverb's figure (the sheet's "Reverb"): its mode's name large, what it is in small capitals; an isometric room
 * whose size follows knob 1 (its floor dimensioned, callout 1); rings spreading on its floor, as many and as lasting as
 * knob 2 lets them (the plate's: its damping, less damping more rings); callouts 2-4 at the left with their knobs'
 * names, 2's dashed leader to the rings. The source and the first ring vermilion while the effect sounds */
static const char *const RV_DESC[NRV] = {"BRIGHT METAL PLATE", "A SMALL ROOM", "SMALL STUDIO", "MEDIUM STUDIO", "LARGE STUDIO",
                                         "A CONCERT HALL", "ROOM WITH ECHO", "ECHOES IN SPACE", "ALMOST ENDLESS",
                                         "A SINGLE ECHO", "SMOOTH, WIDE PLATE"};
/* ---- P3 helpers (the fx, character, sends, LFO, players and setup pages): a fine dotted line, a dotted curve ---- */
static void dot_line(float x0, float y0, float x1, float y1, float step, uint16_t c)   /* a dot every step px (a fine leader) */
{
    float dx = x1 - x0, dy = y1 - y0, l = dx * dx + dy * dy, t;
    if (l < 1.0f)
        return;
    l = f_sqrt1(l);
    for (t = 0.0f; t <= l; t += step)
        cv_dotf(x0 + dx * t / l, y0 + dy * t / l, 0.62f, c);
}
static void dot_curve(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c)   /* a quadratic, dotted */
{
    float px = x0, py = y0, acc = 0.0f;
    for (int i = 1; i <= 40; i++) {
        float t = (float)i / 40.0f, u = 1.0f - t, x = u * u * x0 + 2.0f * t * u * x1 + t * t * x2,
              y = u * u * y0 + 2.0f * t * u * y1 + t * t * y2, dx = x - px, dy = y - py;
        acc += f_sqrt1(dx * dx + dy * dy + 1.0f) - 0.0f;
        if (acc >= 3.0f)
            cv_dotf(x, y, 0.62f, c), acc = 0.0f;
        px = x, py = y;
    }
}
/* ---- P4 helpers: the effect machines' parts. Copies of the engine machines' p2_* parts (agent 2's: p2_touch,
 * p2_outline, p2_panel, p2_callout, p2_knob, p2_vslider, p2_screw, p2_coil), the same line weights (hairline 0.8,
 * regular 1.2, heavy 2) and callouts; to be de-duplicated with them at the merge ---- */
#define FXU(v) ((float)(v) * (1.0f / 127.0f))
static int fxm_touch(void) { return plat_ms() < ui.touch_until ? ui.touched + 1 : 0; }   /* the knob turned (1..4) */
static void fxm_ln(float x0, float y0, float x1, float y1, float w) { cv_linef(x0, y0, x1, y1, K_TEXT, w); }
static void fxm_outline(float x0, float y0, float x1, float y1, float w)
{
    fxm_ln(x0, y0, x1, y0, w);
    fxm_ln(x1, y0, x1, y1, w);
    fxm_ln(x1, y1, x0, y1, w);
    fxm_ln(x0, y1, x0, y0, w);
}
static void fxm_panel(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill)   /* filled, an ink edge */
{
    cv_rect(x, y, w, h, fill);
    fxm_outline((float)x + 0.5f, (float)y + 0.5f, (float)(x + w) - 0.5f, (float)(y + h) - 0.5f, 1.2f);
}
static void fxm_num(int32_t x, int32_t cy, int n)   /* knob n's circled number; the knob being turned: blue */
{
    char d[2] = {(char)('0' + n), 0};
    if (fxm_touch() == n)
        cv_disc((float)x, (float)cy - 0.5f, 6.0f, K_SEL), text_c(x + 1, cy - 6, F_LAB, d, K_BG);
    else
        circ_num(x, cy, d, K_TEXT, K_BG);
}
/* a knob: its cap, its pointer from 7 o'clock (0) to 5 o'clock (1); n > 0: its number at (cx + dx, cy + dy) */
static void fxm_knob(float cx, float cy, float r, float u, int n, int32_t dx, int32_t dy)
{
    float t = 0.375f + 0.75f * u, ca = fm_sin_turns(t + 0.25f), sa = fm_sin_turns(t);
    cv_disc(cx, cy, r, K_CARD);
    cv_ring(cx, cy, r, 1.2f, K_TEXT);
    fxm_ln(cx + ca * r * 0.2f, cy + sa * r * 0.2f, cx + ca * (r - 1.0f), cy + sa * (r - 1.0f), r > 6.0f ? 2.0f : 1.5f);
    if (n)
        fxm_num((int32_t)cx + dx, (int32_t)cy + dy, n);
}
static void fxm_ticks(float cx, float cy, float r0, float r1, int n)   /* a knob's scale: n ticks, 7 to 5 o'clock */
{
    for (int k = 0; k < n; k++) {
        float t = 0.375f + 0.75f * (float)k / (float)(n - 1), c = fm_sin_turns(t + 0.25f), s = fm_sin_turns(t);
        cv_linef(cx + c * r0, cy + s * r0, cx + c * r1, cy + s * r1, K_TEXT, 0.8f);
    }
}
/* a slider: its slot h px down from y, its cap at u (1: the top); n > 0: its number over the slot */
static void fxm_vslider(int32_t x, int32_t y, int32_t h, float u, int n)
{
    int32_t cy = y + 3 + (int32_t)((1.0f - u) * (float)(h - 6) + 0.5f);
    cv_rect(x - 1, y, 2, h, K_TEXT);
    cv_rect(x - 5, cy - 3, 10, 7, K_TEXT);
    cv_rect(x - 4, cy - 2, 8, 5, K_CARD);
    cv_rect(x - 4, cy, 8, 1, K_TEXT);
    if (n)
        fxm_num(x, y - 8, n);
}
static void fxm_screw(float x, float y)
{
    cv_ring(x, y, 2.2f, 0.9f, K_TEXT);
    fxm_ln(x - 1.4f, y + 1.4f, x + 1.4f, y - 1.4f, 0.8f);
}
static void fxm_coil(float x, float y, int n, float r)   /* a coiled cable along x */
{
    pen_t pn = {0};
    for (int i = 0; i <= n * 12; i++) {
        float t = (float)i / 12.0f;
        pen_to(&pn, x + t * r * 1.2f + r * fm_sin_turns(t + 0.25f) * 0.6f, y + r * fm_sin_turns(t), K_TEXT);
    }
}
static void fxm_lead(float x0, float y0, float x1, float y1)   /* a cable from a jack, easing from y0 to y1 */
{
    pen_t pn = {0};
    pn.w = 1.6f;
    for (int i = 0; i <= 16; i++) {
        float t = (float)i / 16.0f;
        pen_to(&pn, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t * t * (3.0f - 2.0f * t), K_TEXT);
    }
}
static void fxm_jack(float x, float y)
{
    cv_disc(x, y, 3.2f, K_CARD);
    cv_ring(x, y, 3.2f, 1.2f, K_TEXT);
    cv_disc(x, y, 1.2f, K_TEXT);
}
static void fxm_foot(float x, float y)                /* a footswitch: its ring, its cap */
{
    cv_disc(x, y, 8.0f, K_PANEL2);
    cv_ring(x, y, 8.0f, 1.2f, K_TEXT);
    cv_disc(x, y, 4.5f, K_TEXT);
    cv_disc(x - 1.3f, y - 1.3f, 1.4f, K_PANEL);
}
static void fxm_lamp(int32_t x, int32_t y, int32_t w, uint16_t c)   /* a lamp w x 4, ink-rimmed */
{
    cv_rect(x, y, w, 5, K_TEXT);
    cv_rect(x + 1, y + 1, w - 2, 3, c);
}
/* a machine's title (the engines' layout): its name large at the left (smaller where the right column needs the
 * room); at the right what it does in tracked capitals (num: a knob's number before it) and a line of detail */
static void fxm_title(const char *nm, const char *desc, const char *det, int num, uint16_t c)
{
    int32_t dw = (desc ? tr_w(F_LAB, desc, 1) : 0) + (num ? 15 : 0), xw = det ? text_w(F_XB, det) : 0;
    int32_t room = 212 - (dw > xw ? dw : xw);
    const felucca_font_t *f = text_w(&FONT_L, nm) <= room ? &FONT_L : text_w(F_VAL, nm) <= room ? F_VAL : &FONT_B;
    cv_text(8, f == &FONT_L ? 0 : f == F_VAL ? 6 : 8, f, nm, c);
    if (desc) {
        int32_t x = 232 - tr_w(F_LAB, desc, 1);
        tr_text(x, 3, F_LAB, desc, K_DIM, 1);
        if (num)
            fxm_num(x - 10, 9, num);
    }
    if (det)
        text_r(232, 17, F_XB, det, K_TEXT);
}

/* ---- the send effects' machines (FX I, FX II). Each a 1980s archetype of its own; its four knobs are the page's,
 * numbered and drawn at the live values ---- */
/* delay: a tape echo seen from above, its tape loop round two rollers under a window; the record head (vermilion
 * while it sounds), the playback head as far along as the time (and fine) put it, a ghosted head for each further
 * repeat; the time a stepped knob (its eight divisions), fine, repeats and tone; a little VU; the ping width the
 * two outputs, L and R, their leads apart as far as it spreads them */
static const uint8_t DLY_D[8] = {14, 18, 21, 25, 28, 32, 35, 42};   /* the heads' spacing for each division */
__attribute__((noinline)) static void fxm_delay(const uint8_t *q, int wid, int snd)
{
    int idx = (q[0] * 8) >> 7, nrep = 1 + (int)(q[2] * 7u >> 7);
    float d = (float)DLY_D[idx] * (1.0f + ((float)q[1] - 64.0f) * (0.25f / 64.0f)), w = FXU(wid);
    fxm_panel(8, 40, 200, 76, K_CARD);
    fxm_panel(14, 45, 188, 36, K_PANEL);             /* the window over the tape */
    for (int s = 0; s < 2; s++) {                    /* the rollers, the tape round them */
        float x = s ? 186.0f : 30.0f;
        cv_disc(x, 63.0f, 9.0f, K_CARD);
        cv_ring(x, 63.0f, 10.0f, 2.0f, K_TEXT);
        cv_disc(x, 63.0f, 2.0f, K_TEXT);
    }
    fxm_ln(30.0f, 53.0f, 186.0f, 53.0f, 2.0f);
    fxm_ln(30.0f, 73.0f, 186.0f, 73.0f, 2.0f);
    cv_disc(174.0f, 77.0f, 3.5f, K_TEXT);            /* the pinch roller */
    cv_rect(41, 75, 6, 5, K_DIM);                    /* the erase head; the record head */
    cv_rect(52, 75, 8, 6, snd ? K_REC : K_TEXT);
    for (int k = 1; k <= nrep; k++) {                /* the playback head; the repeats', ghosted */
        int32_t x = 56 + (int32_t)(d * (float)k) - 4;
        if (x > 158)
            break;
        if (k == 1)
            cv_rect(x, 75, 8, 6, K_TEXT);
        else
            cv_rect(x, 75, 8, 6, K_DIM), cv_rect(x + 1, 76, 6, 4, K_PANEL);
    }
    fxm_ticks(34.0f, 100.0f, 10.5f, 13.0f, 8);       /* the time: a knob with a click a division */
    fxm_knob(34.0f, 100.0f, 8.0f, (float)idx / 7.0f, 1, -18, 0);
    for (int k = 1; k < 4; k++)
        fxm_knob(54.0f + 30.0f * (float)k, 100.0f, 6.0f, FXU(q[k]), k + 1, -13, 0);
    fxm_panel(152, 90, 30, 20, K_BG);                /* the VU */
    ellipse_arc(167.0f, 108.0f, 12.0f, 12.0f, 0.62f, 0.88f, 0.8f, K_DIM);
    {
        float a = snd ? 0.8f : 0.64f;
        fxm_ln(167.0f, 108.0f, 167.0f + 12.0f * fm_sin_turns(a + 0.25f), 108.0f + 12.0f * fm_sin_turns(a), 1.2f);
    }
    for (int s = 0; s < 2; s++) {                    /* the outputs, L and R, apart as far as the width */
        float y = s ? 108.0f : 94.0f, ye = 101.0f + (s ? 1.0f : -1.0f) * 18.0f * w;
        fxm_jack(200.0f, y);
        fxm_lead(203.5f, y, 240.0f, ye);
        cv_text(187, (int32_t)y - 6, F_LAB, s ? "R" : "L", K_DIM);
    }
}
/* chorus: a wide pedal with an ensemble's four knobs in a row, two lamps that show the rate (the slow one lit at a
 * slow rate, the fast one at a fast), two footswitches; its leads, one coiled */
__attribute__((noinline)) static void fxm_chorus(const uint8_t *q)
{
    cv_round(40, 38, 160, 78, 7, K_TEXT);
    cv_round(42, 40, 156, 74, 5, K_CARD);
    for (int k = 0; k < 4; k++)
        fxm_knob(66.0f + 36.0f * (float)k, 65.0f, 8.0f, FXU(q[k]), k + 1, 0, -16);
    cv_round(42, 81, 156, 33, 5, K_PANEL);           /* the lower half, a shade deeper, under the stripe */
    cv_rect(42, 81, 156, 4, K_TEXT);
    for (int s = 0; s < 2; s++) {
        fxm_lamp(s ? 145 : 77, 89, 12, col_mix(K_LINE, K_NOW, (uint32_t)(s ? q[0] : 127 - q[0]) >> 3));
        fxm_foot(s ? 151.0f : 83.0f, 104.0f);
    }
    fxm_jack(200.0f, 62.0f);
    fxm_lead(203.0f, 62.0f, 240.0f, 76.0f);
    fxm_ln(31.0f, 62.0f, 40.0f, 62.0f, 1.6f);
    fxm_coil(8.0f, 62.0f, 4, 4.5f);
}
/* lofi: a chunky early sampling drum box: the rate a slider, the bits on a two-digit display, big square pads
 * (the first vermilion while it sounds), smooth and width two knobs, a speaker's slots */
static void fxm_seg(int32_t x, int32_t y, int dig)   /* a seven-segment digit, 14 x 24 */
{
    static const uint8_t SEG7[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    static const uint8_t S[7][4] = {{2, 0, 10, 3}, {11, 2, 3, 9}, {11, 13, 3, 9}, {2, 21, 10, 3}, {0, 13, 3, 9},
                                    {0, 2, 3, 9}, {2, 10, 10, 3}};
    for (int s = 0; s < 7; s++)
        cv_rect(x + S[s][0], y + S[s][1], S[s][2], S[s][3], (SEG7[dig] >> s & 1) ? K_CARD : col_mix(K_TEXT, K_CARD, 3));
}
__attribute__((noinline)) static void fxm_lofi(const uint8_t *q, int snd)
{
    int bits = 2 + (14 * q[1] + 63) / 127;
    cv_round(14, 38, 212, 78, 9, K_TEXT);
    cv_round(18, 42, 204, 70, 6, K_PANEL);
    fxm_vslider(32, 60, 46, FXU(q[0]), 1);
    cv_rect(46, 50, 46, 32, K_TEXT);                 /* the display: the bits */
    fxm_seg(52, 54, bits / 10);
    fxm_seg(72, 54, bits % 10);
    fxm_num(100, 58, 2);
    for (int k = 0; k < 3; k++)                      /* the speaker */
        cv_rect(48, 91 + k * 6, 42, 2, K_DIM);
    for (int k = 0; k < 6; k++) {                    /* the pads */
        int32_t x = 108 + (k % 3) * 30, y = 50 + (k / 3) * 30;
        cv_round(x, y, 24, 24, 3, K_TEXT);
        cv_round(x + 2, y + 2, 20, 20, 2, k == 0 && snd ? K_REC : K_CARD);
    }
    fxm_knob(206.0f, 66.0f, 6.0f, FXU(q[2]), 3, 0, -14);
    fxm_knob(206.0f, 98.0f, 6.0f, FXU(q[3]), 4, 0, -14);
}
/* phaser: a wide floor pedal, its sides raked; a meter whose needle is the frequency and whose shaded arc the sweep
 * (the depth); six stage lamps, those inside the sweep lit; its four knobs; two footswitches */
__attribute__((noinline)) static void fxm_phaser(const uint8_t *q)
{
    static const float B[8] = {44.0f, 40.0f, 196.0f, 40.0f, 220.0f, 116.0f, 20.0f, 116.0f};
    float u0 = FXU(q[0]), dp = FXU(q[1]), a = 0.6f + 0.3f * u0, s = 0.15f * dp;
    cv_poly(B, 4, K_CARD, 256u, 0);
    for (int k = 0; k < 4; k++)
        fxm_ln(B[2 * k], B[2 * k + 1], B[(2 * k + 2) & 7], B[(2 * k + 3) & 7], 1.2f);
    fxm_panel(52, 46, 62, 40, K_BG);                 /* the meter */
    ellipse_arc(83.0f, 82.0f, 19.0f, 19.0f, fm_maxf(0.6f, a - s), fm_minf(0.9f, a + s), 6.0f, K_TINT);
    ellipse_arc(83.0f, 82.0f, 26.0f, 26.0f, 0.6f, 0.9f, 1.0f, K_TEXT);
    for (int k = 0; k < 7; k++) {
        float t = 0.6f + 0.05f * (float)k;
        cv_linef(83.0f + 26.0f * fm_sin_turns(t + 0.25f), 82.0f + 26.0f * fm_sin_turns(t), 83.0f + 22.0f * fm_sin_turns(t + 0.25f),
                 82.0f + 22.0f * fm_sin_turns(t), K_TEXT, 0.8f);
    }
    fxm_ln(83.0f, 82.0f, 83.0f + 25.0f * fm_sin_turns(a + 0.25f), 82.0f + 25.0f * fm_sin_turns(a), 1.6f);
    cv_disc(83.0f, 82.0f, 2.5f, K_TEXT);
    for (int k = 0; k < 6; k++) {                    /* the stages */
        float c = ((float)k + 0.5f) / 6.0f - u0;
        fxm_lamp(124 + k * 11, 47, 8, (c < 0.0f ? -c : c) <= dp * 0.5f + 0.09f ? K_NOW : K_BG);
    }
    for (int k = 0; k < 4; k++)
        fxm_knob((float)(146 + (k & 1) * 38), (float)(67 + (k >> 1) * 26), 7.0f, FXU(q[k]), k + 1, -14, 0);
    fxm_foot(70.0f, 103.0f);
    fxm_foot(112.0f, 103.0f);
}
/* the plate: a long wooden cabinet on legs, open at the front: the steel plate hung on four springs, as long as the
 * size makes it (dimensioned: 1); the driver at its left (its first ring vermilion while it sounds), a pickup at its
 * right; the damper's felt pad under it, raised to the plate the more it damps (4); the remote with motion and
 * tone (2, 3) on its lead */
static void fxm_spring(float x0, float y0, float x1, float y1)
{
    pen_t pn = {0};
    float dx = x1 - x0, dy = y1 - y0, l = f_sqrt1(dx * dx + dy * dy) + 0.01f, nx = -dy / l * 2.2f, ny = dx / l * 2.2f;
    pn.w = 0.9f;
    for (int i = 0; i <= 10; i++) {
        float t = (float)i / 10.0f, s = (i == 0 || i == 10) ? 0.0f : (i & 1) ? 1.0f : -1.0f;
        pen_to(&pn, x0 + dx * t + nx * s, y0 + dy * t + ny * s, K_TEXT);
    }
}
__attribute__((noinline)) static void fxm_plate(const uint8_t *q, int snd)
{
    float s = FXU(q[0]), pw = 84.0f + 50.0f * s, ph = 22.0f + 8.0f * s, cx = 88.0f, cy = 72.0f;
    float x0 = cx - pw * 0.5f, x1 = cx + pw * 0.5f, y0 = cy - ph * 0.5f, y1 = cy + ph * 0.5f, tx = cx - pw * 0.24f;
    int n = 1 + (127 - q[3]) * 3 / 127;
    fxm_panel(8, 40, 160, 66, K_PANEL);
    for (int k = 0; k < 2; k++) {                    /* the cheeks, wood; the legs */
        float x = k ? 158.0f : 8.0f, w[8] = {x, 40.0f, x + 10.0f, 40.0f, x + 10.0f, 106.0f, x, 106.0f};
        cv_poly(w, 4, K_DIM, 200u, 3);
        fxm_outline(x + 0.5f, 40.5f, x + 9.5f, 105.5f, 1.2f);
        cv_rect(k ? 156 : 16, 106, 4, 10, K_TEXT);
    }
    fxm_ln(18.0f, 46.0f, 158.0f, 46.0f, 0.8f);
    fxm_ln(18.0f, 100.0f, 158.0f, 100.0f, 0.8f);
    fxm_spring(18.0f, y0 - 2.0f, x0, y0 + 3.0f), fxm_spring(158.0f, y0 - 2.0f, x1, y0 + 3.0f);   /* (sideways, to the cheeks) */
    fxm_spring(18.0f, y1 + 2.0f, x0, y1 - 3.0f), fxm_spring(158.0f, y1 + 2.0f, x1, y1 - 3.0f);
    cv_rect((int32_t)x0, (int32_t)y0, (int32_t)pw, (int32_t)ph, K_CARD);
    fxm_outline((float)(int32_t)x0 + 0.5f, (float)(int32_t)y0 + 0.5f, (float)((int32_t)x0 + (int32_t)pw) - 0.5f,
                (float)((int32_t)y0 + (int32_t)ph) - 0.5f, 1.2f);
    for (int k = n - 1; k >= 0; k--)                 /* the driver's rings: more the less it damps */
        ellipse_arc(tx, cy, 5.0f + 5.0f * (float)k, 3.0f + 2.2f * (float)k, 0.0f, 1.0f, k ? 0.8f : 1.2f,
                    k ? col_mix(K_TEXT, K_CARD, (uint32_t)(4 + 3 * k)) : snd ? K_REC : K_TEXT);
    cv_disc(tx, cy, 2.6f, K_TEXT);
    cv_rect((int32_t)(cx + pw * 0.28f) - 3, (int32_t)cy - 3, 6, 6, K_TEXT);   /* the pickup */
    {   /* the damper: its pad, its two rods */
        int32_t py = (int32_t)y1 + 2 + (int32_t)(9.0f * (1.0f - FXU(q[3]))), pl = (int32_t)(cx - pw * 0.05f), pr = (int32_t)(cx + pw * 0.4f);
        float pd[8] = {(float)pl, (float)py, (float)pr, (float)py, (float)pr, (float)py + 4.0f, (float)pl, (float)py + 4.0f};
        cv_rect(pl + 4, py + 4, 1, 100 - py - 4, K_TEXT), cv_rect(pr - 5, py + 4, 1, 100 - py - 4, K_TEXT);
        cv_poly(pd, 4, K_DIM, 256u, 2);
        fxm_outline((float)pl + 0.5f, (float)py + 0.5f, (float)pr - 0.5f, (float)py + 3.5f, 1.0f);
        fxm_num(pr + 9, py + 2, 4);
    }
    dim_line(x0, 52.0f, x1, 52.0f, 3.0f, 3.0f, K_TEXT);   /* the plate's length */
    fxm_num((int32_t)cx, 53, 1);
    fxm_panel(184, 54, 46, 62, K_CARD);              /* the remote, on its lead */
    fxm_knob(214.0f, 72.0f, 7.0f, FXU(q[1]), 2, -18, 0);
    fxm_knob(214.0f, 98.0f, 7.0f, FXU(q[2]), 3, -18, 0);
    fxm_lead(168.0f, 94.0f, 184.0f, 108.0f);
}
/* lush: a 2U digital reverb rack: its ears, a display showing the tail's time, nudge buttons, its four knobs, a row
 * of program buttons, a level ladder (lit while it sounds), the power switch */
__attribute__((noinline)) static void fxm_lush(const uint8_t *q, int snd)
{
    static const uint8_t DT[5] = {0, 45, 64, 100, 127}, DS[5] = {6, 9, 14, 37, 72};   /* the tail, tenths of a s */
    char t[12], *e;
    int k = 0, ds;
    fxm_panel(8, 42, 224, 72, K_CARD);
    for (int s = 0; s < 2; s++) {                    /* the ears and their slots */
        int32_t x = s ? 226 : 14;
        cv_rect(s ? 219 : 20, 42, 1, 72, K_TEXT);
        for (int j = 0; j < 2; j++)
            cv_rect(x - 2, 50 + j * 44, 4, 10, K_TEXT), cv_rect(x - 1, 51 + j * 44, 2, 8, K_BG);
    }
    while (k < 3 && q[1] > DT[k + 1])
        k++;
    ds = DS[k] + (DS[k + 1] - DS[k]) * (q[1] - DT[k]) / (DT[k + 1] - DT[k]);
    e = num_s((uint32_t)ds / 10u, t);
    e = s_cat(e, ".");
    e = num_s((uint32_t)ds % 10u, e);
    s_cat(e, " s");
    cv_rect(28, 50, 78, 32, K_TEXT);                 /* the display */
    text_r(100, 55, F_VAL, t, K_CARD);
    for (int j = 0; j < 2; j++) {                    /* the nudge buttons */
        float y = (float)(51 + j * 16);
        fxm_panel(112, 50 + j * 16, 14, 14, K_PANEL);
        if (j)
            cv_tri(119.0f, y + 10.0f, 115.0f, y + 4.0f, 123.0f, y + 4.0f, K_TEXT);
        else
            cv_tri(119.0f, y + 3.0f, 115.0f, y + 9.0f, 123.0f, y + 9.0f, K_TEXT);
    }
    for (int j = 0; j < 4; j++)
        fxm_knob((float)(144 + j * 22), 70.0f, 7.0f, FXU(q[j]), j + 1, 0, -16);
    for (int j = 0; j < 8; j++)                      /* the programs */
        fxm_panel(28 + j * 11, 92, 9, 7, j == 2 ? K_TEXT : K_PANEL);
    for (int j = 0; j < 10; j++)                     /* the level */
        cv_rect(128 + j * 7, 94, 5, 4, snd && j < 7 ? K_REC : K_LINE);
    fxm_panel(202, 90, 10, 12, K_TEXT);              /* the power switch */
    cv_rect(204, 92, 6, 4, K_PANEL);
}
/* the echo modes: an old grey console, a cartridge in its slot; a lamp an echo (one, two, three; five for chaos);
 * half echo's cartridge half out, space echo an expansion bolted on its side, chaos echo with its lid off (the board
 * and its chips) and the lid leant beside it; size, decay, tone and damp its four knobs */
__attribute__((noinline)) static void fxm_console(const uint8_t *q, int mode, int snd)
{
    int ne = mode == RV_ECHO_ONE ? 1 : mode == RV_HALF_ECHO ? 2 : mode == RV_SPACE_ECHO ? 3 : 5;
    int chaos = mode == RV_CHAOS_ECHO, half = mode == RV_HALF_ECHO;
    if (mode == RV_SPACE_ECHO) {                     /* the expansion */
        fxm_panel(8, 66, 20, 48, K_PANEL);
        for (int k = 0; k < 4; k++)
            cv_rect(12, 72 + k * 6, 12, 2, K_DIM);
        fxm_screw(18.0f, 104.0f);
    }
    fxm_panel(28, 58, 184, 56, K_PANEL2);
    if (chaos) {                                     /* the lid off: the board, its chips; the lid leant at the side */
        static const float L[8] = {214.0f, 56.0f, 222.0f, 52.0f, 232.0f, 112.0f, 224.0f, 116.0f};
        cv_poly(L, 4, K_PANEL2, 256u, 0);
        for (int k = 0; k < 4; k++)
            fxm_ln(L[2 * k], L[2 * k + 1], L[(2 * k + 2) & 7], L[(2 * k + 3) & 7], 1.2f);
        fxm_panel(40, 44, 160, 15, K_TINT);
        for (int k = 0; k < 5; k++)
            cv_rect(48 + k * 30, 48, k == 2 ? 20 : 12, 7, K_TEXT);
        cv_dash(44.0f, 56.5f, 196.0f, 56.5f, 2.0f, 2.0f, 0.8f, K_TEXT);
    } else
        fxm_panel(40, 46, 160, 13, K_PANEL);
    cv_rect(92, 49, 56, 4, K_TEXT);                  /* the slot, the cartridge in it */
    fxm_panel(98, half ? 30 : 36, 44, half ? 21 : 15, K_CARD);
    cv_rect(104, half ? 34 : 39, 32, 6, K_LINE);
    for (int k = 0; k < ne; k++)                     /* the lamps: an echo each */
        fxm_lamp(38 + k * 13, 66, 10, k == 0 && snd ? K_REC : K_NOW);
    for (int k = 0; k < 2; k++) {                    /* power and reset; the two ports */
        fxm_panel(38 + k * 24, 82, 18, 8, K_CARD);
        cv_round(40 + k * 24, 98, 14, 9, 3, K_TEXT);
        cv_rect(43 + k * 24, 101, 8, 3, K_PANEL2);
    }
    for (int k = 0; k < 4; k++)
        fxm_knob((float)(118 + k * 26), 94.0f, 7.0f, FXU(q[k]), k + 1, 0, -16);
}

/* the rooms (room, the studios, the hall): an isometric room whose size follows knob 1 (its floor dimensioned),
 * rings on its floor as many and as lasting as the decay lets them (the first vermilion while the effect sounds), a
 * small person for scale; each room its own proportions: the room near a cube, the small studio low and shallow,
 * the large one wide and low, the hall tall. Callouts 2-4 at the left, 2's leader to the rings */
static const struct { uint8_t w, d, h; } RV_BOX[5] = {{42, 42, 30}, {58, 28, 17}, {64, 44, 22}, {80, 50, 18}, {62, 40, 38}};
__attribute__((noinline)) static void draw_room(const uint8_t *p, int mode, int snd)
{
    const int b = mode - RV_ROOM;
    const char *nm = RV_NAME[mode];
    float s = p[0] <= 50 ? 0.7f + 0.3f * (float)p[0] / 50.0f : 1.0f + 0.06f * (float)(p[0] - 50) / 77.0f;
    float w = (float)RV_BOX[b].w * s, d = (float)RV_BOX[b].d * s, hh = (float)RV_BOX[b].h * (s < 1.0f ? s : 1.0f), fx = 171.0f, fy = 68.0f;
    float ox = fx - 0.866f * (w - d) * 0.5f, oy = fy - 0.5f * (w + d) * 0.5f;
    int n = 2 + (p[1] * 5 + 63) / 127;
#define RP(a, b, c, i) (i ? oy + 0.5f * (a) + 0.5f * (b) - (c) : ox + 0.866f * (a) - 0.866f * (b))
#define RQ(a, b, c) RP(a, b, c, 0), RP(a, b, c, 1)
    {   /* the name (large where it fits beside the room), what the mode is */
        const felucca_font_t *f = text_w(&FONT_L, nm) <= 100 ? &FONT_L : text_w(F_VAL, nm) <= 100 ? F_VAL : &FONT_B;
        cv_text(8, f == &FONT_L ? 5 : f == F_VAL ? 13 : 17, f, nm, K_TEXT);
        if (tr_w(F_LAB, RV_DESC[mode], 1) <= 100)
            tr_text(9, 37, F_LAB, RV_DESC[mode], K_DIM, 1);
        else
            text_fit(9, 37, F_LAB, RV_DESC[mode], 100, K_DIM);
    }
    {   /* the room: two back walls (the left hatched), the floor, their edges */
        const float floor[8] = {RQ(0, 0, 0), RQ(w, 0, 0), RQ(w, d, 0), RQ(0, d, 0)};
        const float back[8] = {RQ(0, 0, 0), RQ(w, 0, 0), RQ(w, 0, hh), RQ(0, 0, hh)};
        const float side[8] = {RQ(0, 0, 0), RQ(0, d, 0), RQ(0, d, hh), RQ(0, 0, hh)};
        const float e[9][4] = {{RQ(0, 0, 0), RQ(0, 0, hh)}, {RQ(0, 0, hh), RQ(w, 0, hh)}, {RQ(0, 0, hh), RQ(0, d, hh)},
                               {RQ(w, 0, 0), RQ(w, 0, hh)}, {RQ(0, d, 0), RQ(0, d, hh)}, {RQ(0, 0, 0), RQ(w, 0, 0)},
                               {RQ(0, 0, 0), RQ(0, d, 0)}, {RQ(w, 0, 0), RQ(w, d, 0)}, {RQ(0, d, 0), RQ(w, d, 0)}};
        cv_poly(floor, 4, K_CARD, 256u, 0);
        cv_poly(back, 4, K_CARD, 256u, 0);
        cv_poly(side, 4, K_PANEL, 256u, 0);
        cv_poly(side, 4, K_LINE, 256u, 5);
        for (int i = 0; i < 9; i++)
            cv_linef(e[i][0], e[i][1], e[i][2], e[i][3], K_TEXT, 1.0f);
    }
    {   /* on the back wall: the room's door, a studio's control-room glass, the hall's three tall windows (in
         * hundredths of the wall: the first's start, the step, the width, the sill, the head) */
        static const uint8_t WIN[5][5] = {{62, 0, 14, 0, 66}, {50, 0, 36, 35, 75}, {50, 0, 36, 35, 75}, {50, 0, 36, 35, 75},
                                          {24, 26, 14, 22, 86}};
        for (int k = 0; k < (b == 4 ? 3 : 1); k++) {
            float a0 = w * (float)(WIN[b][0] + k * WIN[b][1]) * 0.01f, a1 = a0 + w * (float)WIN[b][2] * 0.01f;
            float c0 = hh * (float)WIN[b][3] * 0.01f, c1 = hh * (float)WIN[b][4] * 0.01f;
            const float g[8] = {RQ(a0, 0, c0), RQ(a1, 0, c0), RQ(a1, 0, c1), RQ(a0, 0, c1)};
            cv_poly(g, 4, K_PANEL, 256u, 0);
            for (int i = 0; i < 4; i++)
                cv_linef(g[2 * i], g[2 * i + 1], g[(2 * i + 2) & 7], g[(2 * i + 3) & 7], K_TEXT, 0.8f);
        }
    }
    {   /* the rings on the floor's middle: the first the source's, the rest thin and fainter the farther they go */
        float dm = (float)(RV_BOX[b].w < RV_BOX[b].d ? RV_BOX[b].w : RV_BOX[b].d) * s, rmax = 0.475f * dm, r0 = rmax / (float)n;
        for (int k = n - 1; k >= 0; k--) {
            float r = r0 * (float)(k + 1), op = (0.35f + 0.6f * (float)p[1] / 127.0f) * (1.0f - (float)k / ((float)n + 0.5f));
            uint16_t c = k ? col_mix(K_TEXT, K_CARD, (uint32_t)(16.0f - 15.0f * op)) : snd ? K_REC : K_TEXT;
            ellipse_arc(fx, fy, r * 1.2247f, r * 0.7071f, 0.0f, 1.0f, k ? 0.9f : 1.2f, c);
        }
        {   /* knobs 2-4 at the left; 2's leader (fine dots) to the outermost ring */
            int32_t e = 0;
            for (int k = 1; k < 4; k++) {
                int32_t y = 48 + k * 16, x = cv_text(22, y - 7, &FONT_XS, fx_knob_name(FXT_REVERB + mode, k), K_TEXT);
                fxm_num(13, y, k + 1);
                if (k == 1)
                    e = x;
            }
            if (fx - rmax * 1.2247f - 3.0f > (float)e + 9.0f)
                dot_curve((float)e + 5.0f, 64.0f, ((float)e + fx - rmax * 1.2247f) * 0.5f, 60.0f, fx - rmax * 1.2247f - 3.0f, fy - 1.0f, K_TEXT);
        }
    }
    {   /* a small person on the floor: the size knob dwarfs or crowds them */
        float px = RP(w * 0.16f, d * 0.82f, 0, 0), py = RP(w * 0.16f, d * 0.82f, 0, 1);
        cv_disc(px, py - 10.5f, 1.9f, K_TEXT);
        cv_linef(px, py - 8.0f, px, py - 3.5f, K_TEXT, 1.2f);
        cv_linef(px - 2.5f, py - 6.5f, px + 2.5f, py - 6.5f, K_TEXT, 1.0f);
        cv_linef(px, py - 3.5f, px - 1.8f, py, K_TEXT, 1.0f);
        cv_linef(px, py - 3.5f, px + 1.8f, py, K_TEXT, 1.0f);
    }
    {   /* knob 1: the floor's front edge, dimensioned; its callout under it: the number, then the word */
        float ax = RP(0, d, 0, 0) - 5.0f, ay = RP(0, d, 0, 1) + 9.0f, bx = RP(w, d, 0, 0) - 5.0f, by = RP(w, d, 0, 1) + 9.0f;
        int32_t cx = (int32_t)((ax + bx) * 0.5f - 22.0f), cy = (int32_t)((ay + by) * 0.5f + 12.0f);
        dim_line(ax, ay, bx, by, 3.0f, 3.0f, K_TEXT);
        fxm_num(cx, cy, 1);
        cv_text(cx + 9, cy - 7, &FONT_XS, fx_knob_name(FXT_REVERB + mode, 0), K_TEXT);
    }
#undef RP
#undef RQ
}
static void draw_fxtrack(int trk)                    /* AUX_FX1 / AUX_FX2: the effect, a machine of its own */
{
    int pick = fx_pick_of(trk), type = fx_pick_type(pick), mode = fx_pick_mode(pick), snd = ui_peak[trk] > 400;
    const uint8_t *q = proj.trk[trk].par + TP_E1;
    int wid = proj.trk[trk].par[TP_FX_WIDTH];
    uint32_t h = hash_u(hash_u(2166136261u, 0xF1u), (uint32_t)trk | (uint32_t)pick << 4 | (uint32_t)fxm_touch() << 12 |
                                                    (uint32_t)snd << 16 | (uint32_t)wid << 20);
    char t[24];
    for (int k = 0; k < 4; k++)
        h = hash_u(h, (uint32_t)q[k] << (8 * k));
    if (!begin_main(h))
        return;
    if (type == FXT_REVERB && mode >= RV_ROOM && mode <= RV_HALL) {
        draw_room(q, mode, snd);
        cv_blit(0, MAIN_Y);
        return;
    }
    switch (type) {
    case FXT_DELAY: {
        static const char *const DIV[8] = {"1/32", "1/32D", "1/16", "1/16D", "1/8", "1/8D", "1/4", "1/2"};
        char *e = s_cat(s_cat(t, DIV[(q[0] * 8) >> 7]), ", ");
        s_cat(num_s(1u + (q[2] * 7u >> 7), e), q[2] * 7u >> 7 ? " repeats" : " repeat");
        fxm_title("delay", "PING-PONG", t, 0, K_TEXT);
        fxm_delay(q, wid, snd);
        break;
    }
    case FXT_CHORUS: fxm_title("chorus", "DETUNED COPIES", 0, 0, K_TEXT), fxm_chorus(q); break;
    case FXT_LOFI: fxm_title("lofi", "BITS AND RATE", 0, 0, K_TEXT), fxm_lofi(q, snd); break;
    case FXT_PHASER: fxm_title("phaser", "SWEPT NOTCHES", 0, 0, K_TEXT), fxm_phaser(q); break;
    default:
        fxm_title(RV_NAME[mode], RV_DESC[mode], 0, 0, K_TEXT);
        if (mode == RV_PLATE)
            fxm_plate(q, snd);
        else if (mode == RV_LUSH)
            fxm_lush(q, snd);
        else
            fxm_console(q, mode, snd);
        break;
    }
    cv_blit(0, MAIN_Y);
}

/* Play: how the voices are dealt. Poly: six voice slots, those sounding inked in vermilion; mono and legato: one.
 * Beside them two notes, the second reached by the glide's slope (2: its length, dimensioned) and the bend's reach up
 * and down (3); the volume a ruled bar (4) */
static void draw_play(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int vm = p[TP_VMODE] % 3, nv = vm == VM_POLY ? 6 : 1, on = eng_voices;
    uint32_t h = hash_u(hash_u(2166136261u, 0x9Bu), (uint32_t)trk | (uint32_t)p[TP_VMODE] << 4 | (uint32_t)on << 12 |
                                                    (uint32_t)p[TP_PORTA] << 16 | (uint32_t)p[TP_BEND] << 24);
    h = hash_u(h, (uint32_t)p[TP_VOLUME] | (uint32_t)p2_touch() << 8);
    char t[24];
    if (!begin_main(h))
        return;
    p2_title(tp_info_par(TP_VMODE, p)->names[vm], p[TP_ENGINE] == EG_ACID ? "HELD NOTES SLIDE" : vm == VM_POLY ? "UP TO 6 VOICES"
                                                  : vm == VM_MONO ? "ONE, EVERY KEY ANEW" : "ONE, HELD KEYS GLIDE", 110);
    for (int v = 0; v < 6; v++) {                    /* the voices: dealt round, the sounding ones vermilion */
        float x = 16.0f + (float)v * 16.0f, y = 62.0f;
        if (v >= nv)
            cv_ring(x, y, 5.5f, 0.8f, K_LINE);
        else if (v < on)
            cv_disc(x, y, 6.0f, K_REC);
        else
            cv_ring(x, y, 5.5f, 1.4f, K_TEXT);
    }
    p2_callout(13, 82, 1, "voices", K_TEXT);
    num_s((uint32_t)on, s_cat(t, "sounding: "));
    cv_text(13, 92, &FONT_XS, t, K_DIM);
    {   /* the glide: two notes, the slope between them */
        float gl = 4.0f + 60.0f * (float)p[TP_PORTA] / 127.0f, x0 = 126.0f, y0 = 70.0f, y1 = 40.0f;
        int bend = tp_bend_semi(p[TP_BEND]);
        cv_grid(120, 22, 110, 66, 8, MAIN_Y, K_GRID);
        cv_rect(120, 70, 18, 5, K_TEXT);
        cv_linef(x0 + 12.0f, y0 + 2.5f, x0 + 12.0f + gl, y1 + 2.5f, K_TEXT, 1.6f);
        cv_rect((int32_t)(x0 + 12.0f + gl), (int32_t)y1, 222 - (int32_t)(x0 + 12.0f + gl), 5, K_TEXT);
        if (p[TP_PORTA]) {
            dim_line(x0 + 12.0f, 82.0f, x0 + 12.0f + gl, 82.0f, 3.0f, 3.0f, K_TEXT);
            circ_num((int32_t)(x0 + 12.0f + gl * 0.5f), 93, "2", K_TEXT, K_BG);
        } else
            p2_callout(150, 90, 2, "no glide", K_DIM);
        if (bend) {                                  /* the bend: up and down from the held note */
            float r = 2.0f + (float)bend * 1.4f;
            dim_line(226.0f, y1 + 2.5f - r, 226.0f, y1 + 2.5f + r, 0.0f, 0.0f, K_TEXT);
            circ_num(216, 26, "3", K_TEXT, K_BG);
        }
    }
    cv_rect(13, 108, 96, 3, K_PANEL2);
    cv_rect(13, 108, p[TP_VOLUME] * 96 / 127, 3, K_TEXT);
    circ_num(118, 110, "4", K_TEXT, K_BG);
    cv_blit(0, MAIN_Y);
}
/* Play 2: the sound's octave: five octaves of a keyboard, the one the sound plays from inked, an arrow from where the
 * keys are played (the middle) to it */
static void draw_play2(void)
{
    static const char *const WHAT[5] = {"TWO DOWN: A SUB", "ONE DOWN: A BASS", "AS PLAYED", "ONE UP", "TWO UP"};
    static const char *const LAB[5] = {"-2", "-1", "0", "+1", "+2"};
    int trk = sel_trk(), o = proj.trk[trk].par[TP_OCT] % 5;
    uint32_t h = hash_u(hash_u(2166136261u, 0x0Du), (uint32_t)trk | (uint32_t)o << 4 | (uint32_t)p2_touch() << 8);
    if (!begin_main(h))
        return;
    p2_title("octave", WHAT[o], 220);
    for (int i = 0; i < 5; i++) {                    /* five octaves, seven white keys each */
        int32_t x = 12 + i * 44;
        for (int k = 0; k < 7; k++) {
            cv_rect(x + k * 6, 60, 1, 30, K_TEXT);
            if (i == o)
                cv_rect(x + k * 6 + 1, 60, 5, 30, K_CARD);
        }
        cv_rect(x, 60, 42, 1, K_TEXT), cv_rect(x, 89, 42, 1, K_TEXT), cv_rect(x + 42, 60, 1, 30, K_TEXT);
        for (int k = 0; k < 7; k++)
            if (k != 2 && k != 6)
                cv_rect(x + k * 6 + 4, 60, 4, 17, i == o ? K_NOW : K_TEXT);
        if (i == o)
            p2_outline((float)x - 1.5f, 58.5f, (float)(x + 44) - 0.5f, 91.5f, K_TEXT, 1.6f);
        text_c(x + 21, 94, F_LAB, LAB[i], i == o ? K_TEXT : K_DIM);
    }
    if (o != 2) {                                    /* from the played octave to the sounding one */
        float a = 12.0f + 2.0f * 44.0f + 21.0f, b = 12.0f + (float)o * 44.0f + 21.0f;
        cv_linef(a, 54.0f, b + (b > a ? -4.0f : 4.0f), 54.0f, K_TEXT, 1.2f);
        arrow_head(b, 54.0f, b > a ? 1.0f : -1.0f, 0.0f, K_TEXT);
        cv_disc(a, 54.0f, 2.0f, K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}
/* rayon's Sub page: the FM index from the note's start over two seconds on graph paper: its fall (1, the time it
 * takes, dimensioned), its floor (2, a height), the keyscale's lower curve two octaves up (3, faint) */
static void draw_rayon_fall(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int fl = p[TP_X1], fo = p[TP_X2], ks = p[TP_X3];
    uint32_t h = hash_u(hash_u(2166136261u, 0x10u), (uint32_t)trk | (uint32_t)fl << 4 | (uint32_t)fo << 12 |
                                                    (uint32_t)ks << 20);
    float tau = 0.02f * fm_exp2f((float)fl * (7.64f / 127.0f)), floor_ = (float)fo / 127.0f;
    float hi = fm_exp2f(-2.0f * (float)ks / 127.0f);
    int32_t x0 = 110, y0 = 22, w = 120, hh = 64;
    pen_t pa = {0}, pb = {0};
    h = hash_u(h, (uint32_t)p2_touch());
    if (!begin_main(h))
        return;
    p2_title(fl ? "falls" : "holds", fl ? "BRIGHTNESS FALLS" : "THE INDEX HOLDS", 96);
    p2_callout(13, 60, 1, "fall", K_TEXT);
    p2_callout(13, 76, 2, "floor", K_TEXT);
    p2_callout(13, 92, 3, "keyscale", K_TEXT);
    cv_grid(x0, y0, w + 1, hh + 1, 8, MAIN_Y, K_GRID);
    cv_rect(x0, y0 + hh, w + 1, 1, K_TEXT);
    pa.w = 2.0f;
    for (int i = 0; i <= w; i++) {
        float t = 2.0f * (float)i / (float)w, e = fl ? floor_ + (1.0f - floor_) * fm_expf(-t / tau) : 1.0f;
        pen_to(&pb, (float)(x0 + i), (float)(y0 + hh) - e * hi * (float)hh, K_FAINT);
        pen_to(&pa, (float)(x0 + i), (float)(y0 + hh) - e * (float)hh, K_TEXT);
    }
    if (fl) {
        float xt = (float)x0 + fm_minf(tau / 2.0f, 1.0f) * (float)w;
        dim_line((float)x0, (float)(y0 + hh + 8), xt < (float)x0 + 12.0f ? (float)x0 + 12.0f : xt, (float)(y0 + hh + 8), 3.0f, 3.0f, K_TEXT);
        if (floor_ > 0.1f)
            dim_line((float)(x0 + w - 6), (float)(y0 + hh), (float)(x0 + w - 6), (float)(y0 + hh) - floor_ * (float)hh, 0.0f, 0.0f, K_TEXT);
    }
    tr_text(x0, y0 + hh + 14, F_LAB, "2 S", K_DIM, 1);
    cv_blit(0, MAIN_Y);
}
/* knit's Sub page: its layers stacked on a rule each, each as tall as its level: the wave (the first page's, faint),
 * osc 2 at its interval (1, its level 2), the sub an octave down (3), the noise (4) */
static void draw_knit_x(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int st = clampi((int)p[TP_X1] - 64, -24, 24);
    uint32_t h = hash_u(hash_u(2166136261u, 0xA5u), (uint32_t)trk | (uint32_t)p[TP_X1] << 4 | (uint32_t)p[TP_X2] << 12 |
                                                    (uint32_t)p[TP_X3] << 20);
    float wv = (float)p[TP_E1] / 127.0f, mix = wv < 0.5f ? wv * 2.0f : 1.0f, pw = wv < 0.5f ? 0.5f : 0.5f - (wv - 0.5f) * 0.8f;
    float ratio = fm_exp2f((float)st * (1.0f / 12.0f)), lv[4] = {1.0f, (float)p[TP_X2] / 127.0f, (float)p[TP_X3] / 127.0f,
                                                                   (float)p[TP_X4] / 127.0f};
    static const char *const NM[4] = {"WAVE", "OSC 2", "SUB", "NOISE"};
    static const uint8_t CN[4] = {0, 2, 3, 4};
    uint32_t r = 12345u;
    char t[24];
    h = hash_u(h, p[TP_X4] | (uint32_t)p[TP_E1] << 8 | (uint32_t)p2_touch() << 16);
    if (!begin_main(h))
        return;
    for (int l = 0; l < 4; l++) {
        pen_t pn = {0};
        float cy = (float)(14 + l * 27), amp = 10.0f * lv[l], sh = 0.0f;
        tr_text(8, (int32_t)cy - 6, F_LAB, NM[l], lv[l] > 0.0f ? K_TEXT : K_FAINT, 1);
        if (CN[l]) {
            char d[2] = {(char)('0' + CN[l]), 0};
            circ_num(66, (int32_t)cy + 1, d, K_TEXT, K_BG);
            if (l == 1)                              /* osc 2: its interval (1) beside its level (2) */
                circ_num(52, (int32_t)cy + 1, "1", K_TEXT, K_BG);
        }
        cv_rect(76, (int32_t)cy, 154, 1, K_LINE);
        pn.w = l ? 1.7f : 1.0f;
        for (int32_t x = 0; x <= 154; x++) {
            float ph = (float)x / 154.0f * 3.0f * (l == 1 ? ratio : l == 2 ? 0.5f : 1.0f) + sh, fr = ph - fm_floorf(ph), y;
            if (l == 2)
                y = fm_sin_turns(ph);
            else if (l == 3)
                r = r * 1664525u + 1013904223u, y = (float)(int32_t)r * (1.0f / 2147483648.0f);
            else
                y = (1.0f - 2.0f * fr) + ((fr < pw ? 1.0f : -1.0f) - (1.0f - 2.0f * fr)) * mix;
            pen_to(&pn, (float)(76 + x), cy - y * amp, l == 0 ? K_FAINT : K_NOW);
        }
    }
    if (st)
        num_i(st, s_cat(t, st > 0 ? "+" : "")), s_cat(t + s_len(t), " st");
    else
        s_cat(t, "unison");
    cv_text(8, 46, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}
/* acid's Sub page: its drive (TP_X1 amount, TP_X2 off / soft / rat; bass303.c drive_block) as a transfer curve on graph
 * paper (in across, out up) beside two cycles of a sine through it, the input faint, what comes out bright */
static void draw_acid_x(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int ty = p[TP_X2] % 3;
    float d = (float)p[TP_X1] / 127.0f, g = ty == 1 ? fm_exp2f(d * 24.0f * 0.166096405f) : 1.0f + d * 40.0f;
    float y[101], top = 0.0f;
    uint32_t h = hash_u(hash_u(2166136261u, 0xA8u), (uint32_t)trk | (uint32_t)p[TP_X1] << 4 | (uint32_t)ty << 12 |
                                                    (uint32_t)p[TP_X3] << 16);
    pen_t a = {0}, b = {0}, c = {0};
    char t[32];
    h = hash_u(h, (uint32_t)p2_touch());
    if (!begin_main(h))
        return;
    for (int x = 0; x <= 100; x++) {                 /* the curve over an input -1 .. 1 */
        float u = ((float)x / 50.0f - 1.0f) * g;
        if (ty == 1)
            u = fm_tanhf(u + 0.15f) - 0.14888503f;
        else if (ty == 2) {
            float x2 = u * u, sq = fm_rsqrtf(1.0f + x2 * x2);
            u *= sq * fm_rsqrtf(sq);
        } else
            u = (float)x / 50.0f - 1.0f;
        y[x] = u;
        top = fm_maxf(top, fm_fabsf(u));
    }
    if (!ty || d <= 0.0f)
        s_cat(t, "off");
    else
        s_cat(t, ty == 1 ? "soft" : "rat");
    p2_title(t, "THE 303'S DRIVE", 96);
    p2_callout(13, 66, 1, "amount", K_TEXT);
    p2_callout(13, 82, 2, "type", K_TEXT);
    {   /* the transfer: 60 x 60 */
        int32_t gx = 104, gy = 18, gw = 60;
        cv_grid(gx, gy, gw + 1, gw + 1, 6, MAIN_Y, K_GRID);
        p2_outline((float)gx + 0.5f, (float)gy + 0.5f, (float)(gx + gw) + 0.5f, (float)(gy + gw) + 0.5f, K_TEXT, 1.0f);
        cv_dash((float)gx, (float)(gy + gw), (float)(gx + gw), (float)gy, 2.0f, 2.0f, 0.8f, K_DIM);
        c.w = 2.0f;
        for (int x = 0; x <= 100; x++)
            pen_to(&c, (float)gx + (float)x * (float)gw / 100.0f, (float)gy + (float)gw * 0.5f - y[x] / (top > 0.0f ? top : 1.0f) * (float)gw * 0.5f,
                   ty && d > 0.0f ? K_NOW : K_DIM);
        tr_text(gx + gw / 2 - 6, gy + gw + 3, F_LAB, "IN", K_DIM, 1);
    }
    {   /* a sine through it */
        int32_t x0 = 172, w = 58, cy = 48, amp = 24;
        cv_rect(x0, cy, w + 1, 1, K_LINE);
        for (int x = 0; x <= w; x++) {
            float v = fm_sin_turns((float)x / (float)w * 2.0f), u = v * g;
            if (ty == 1)
                u = fm_tanhf(u + 0.15f) - 0.14888503f;
            else if (ty == 2) {
                float x2 = u * u, sq = fm_rsqrtf(1.0f + x2 * x2);
                u *= sq * fm_rsqrtf(sq);
            } else
                u = v;
            pen_to(&a, (float)(x0 + x), (float)cy - v * (float)amp, K_FAINT);
            pen_to(&b, (float)(x0 + x), (float)cy - u / (top > 0.0f ? top : 1.0f) * (float)amp, ty && d > 0.0f ? K_NOW : K_DIM);
        }
    }
    text_r(230, 102, &FONT_XS, "shaper: the square's edge", K_DIM);
    cv_blit(0, MAIN_Y);
}
/* the Sub page: the engine's second page of knobs (TP_X1..X4), knit's layers, rayon's fall or acid's drive */
static void draw_sub(void)
{
    int eg = proj.trk[sel_trk()].par[TP_ENGINE];
    if (eg == EG_RAYON)
        draw_rayon_fall();
    else if (eg == EG_ACID)
        draw_acid_x();
    else if (eg == EG_SAMPLE || eg == EG_MIDI)
        draw_engine();                           /* (the sample's loop page: the waveform, its loop marked; MIDI's
                                                  * knobs 5..8: its eight bars) */
    else
        draw_knit_x();
}
/* a drum key's own envelope: its lag (flat), the attack's rise, the hold, the decay's fall (or, off, the level
 * held to the right edge: the sound's own length rules), the key's colour under it */
/* the envelope figures (the sheet's FIG. "Envelope"): on graph paper, the curve's area hatched in the track's ink, the
 * curve in ink with its corners circled, the base line; under it a dimension line for each stage, broken by its knob's
 * circled number. A stage at its least is still drawn EF_MIN wide, so each callout has room: a schematic, not to
 * scale, as a manual's figure */
#define EF_X0 14.0f
#define EF_TOP 30.0f
#define EF_BASE 90.0f
#define EF_DIM 106
#define EF_MIN 26.0f
typedef struct { float x[64], y[64]; int n, dash; } curve_t;   /* (dash: the points from there on dashed) */
static void ef_pt(curve_t *c, float x, float y)
{
    if (c->n < 64)
        c->x[c->n] = x, c->y[c->n++] = y;
}
static void ef_head(const char *tag, const char *what)   /* "KICK  amplitude over time" */
{
    int32_t e = tr_text(8, 3, F_LAB, tag, K_DIM, 1);
    cv_text(e + 7, 0, &FONT_XS, what, K_TEXT);
}
static void ef_curve(const curve_t *c)
{
    float poly[2 * 66];
    pen_t pn = {0};
    int n = 0;
    cv_grid(8, 20, 224, 76, 8, MAIN_Y, K_GRID);
    for (int i = 0; i < c->n; i++)
        poly[2 * n] = c->x[i], poly[2 * n + 1] = c->y[i], n++;
    poly[2 * n] = c->x[c->n - 1], poly[2 * n + 1] = EF_BASE, n++;
    poly[2 * n] = c->x[0], poly[2 * n + 1] = EF_BASE, n++;
    cv_poly(poly, n, K_NOW, 150u, 6);                /* the area under it, hatched in the track's ink */
    cv_rect(8, (int32_t)EF_BASE, 224, 1, K_TEXT);
    pn.w = 2.0f;
    for (int i = 0; i < c->n; i++) {
        if (c->dash && i > c->dash && (i & 1))       /* (dashed: every other step left out) */
            pen_up(&pn);
        pen_to(&pn, c->x[i], c->y[i], K_TEXT);
    }
}
static void ef_node(float x, float y)
{
    cv_disc(x, y, 2.6f, K_BG);
    cv_ring(x, y, 2.8f, 1.3f, K_TEXT);
}
static void ef_dim(float a, float b, int knob)        /* a stage's dimension, its knob's number in the middle */
{
    char n[2] = {(char)('0' + knob), 0};
    int32_t m = (int32_t)((a + b) * 0.5f + 0.5f);
    dim_line(a, (float)EF_DIM, b, (float)EF_DIM, 8.0f, 4.0f, K_TEXT);
    cv_rect(m - 7, EF_DIM - 6, 15, 13, K_BG);
    circ_num(m, EF_DIM + 1, n, K_TEXT, K_BG);
}
/* the stages of a figure whose curve spans its full width (EF_X0 .. 222): each EF_MIN wide at least (room for its
 * callout), the rest shared by weight e[i] (the knobs' values, each stage a little even at 0) */
#define EF_SPAN 208.0f
static void ef_stages(float *w, const float *e, int n)
{
    float sum = 0.0f, rest = EF_SPAN - (float)n * EF_MIN;
    for (int i = 0; i < n; i++)
        sum += e[i];
    for (int i = 0; i < n; i++)
        w[i] = EF_MIN + rest * e[i] / sum;
}
/* a break mark across a line at x, y (two slanted strokes with a gap of paper between): "this runs on" */
static void ef_break(float x, float y)
{
    cv_rect((int32_t)x - 3, (int32_t)y - 4, 7, 9, K_BG);
    cv_linef(x - 2.5f, y + 4.0f, x + 0.5f, y - 4.0f, K_TEXT, 1.3f);
    cv_linef(x + 0.5f, y + 4.0f, x + 3.5f, y - 4.0f, K_TEXT, 1.3f);
}
/* a kit key's envelope: lag (4), attack (1), hold (2), decay (3); decay off: the sound's own length, dashed */
static void draw_key_env(void)
{
    int trk = sel_trk(), key = ui.dsound % DL_NSOUND;
    const uint8_t *p = par_now(trk) + TP_DRUM(key, 0);
    int open = p[DP_DECAY] >= 127;
    float ew[4] = {0.15f + (float)p[DP_LAG] / 127.0f, 0.25f + (float)p[DP_ATTACK] / 127.0f, 0.25f + (float)p[DP_HOLD] / 127.0f,
                   1.2f + (open ? 1.0f : (float)p[DP_DECAY] / 127.0f)}, sw[4];
    float x1, x2, x3, x4 = EF_X0 + EF_SPAN;
    uint32_t h = hash_u(hash_u(2166136261u, 0xE8u), (uint32_t)trk | (uint32_t)key << 4 | (uint32_t)p[DP_ATTACK] << 8 |
                                          (uint32_t)p[DP_HOLD] << 16 | (uint32_t)p[DP_DECAY] << 24);
    curve_t c = {{0}, {0}, 0, 0};
    h = hash_u(h, p[DP_LAG] | (uint32_t)p[DP_MODEL] << 8 | (uint32_t)k_now << 16);
    if (!begin_main(h))
        return;
    ef_stages(sw, ew, 4);
    x1 = EF_X0 + sw[0], x2 = x1 + sw[1], x3 = x2 + sw[2];
    ef_pt(&c, EF_X0, EF_BASE);
    ef_pt(&c, x1, EF_BASE);
    ef_pt(&c, x2, EF_TOP);
    if (open) {                                      /* decay off: the hold runs on, to a break mark and past it */
        ef_pt(&c, (x3 + x4) * 0.5f, EF_TOP);
    } else {
        ef_pt(&c, x3, EF_TOP);
        for (int i = 1; i <= 32; i++) {
            float u = (float)i / 32.0f;
            ef_pt(&c, x3 + (x4 - x3) * u, EF_BASE - (EF_BASE - EF_TOP) * ease_exp(u));
        }
    }
    ef_head(DL_SOUND_NAME[key], open ? "decay off: it runs to its own end" : "amplitude over time");
    ef_curve(&c);
    if (open) {
        float bx = (x3 + x4) * 0.5f;
        cv_linef(bx + 5.0f, EF_TOP, x4, EF_TOP, K_TEXT, 2.0f);   /* (the line past the break, nothing under it) */
        ef_break(bx, EF_TOP);
        ef_node(x1, EF_BASE), ef_node(x2, EF_TOP);
        text_c((int32_t)((bx + x4) * 0.5f), (int32_t)EF_TOP + 10, &FONT_XS, "plays on", K_TEXT);
        ef_dim(EF_X0, x1, 4), ef_dim(x1, x2, 1), ef_dim(x2, x4, 3);
    } else {
        ef_node(x1, EF_BASE), ef_node(x2, EF_TOP), ef_node(x3, EF_TOP), ef_node(x4, EF_BASE);
        ef_dim(EF_X0, x1, 4), ef_dim(x1, x2, 1), ef_dim(x2, x3, 2), ef_dim(x3, x4, 3);
    }
    cv_blit(0, MAIN_Y);
}
/* a filter's response in dB at x px from its cutoff (an octave every 22 px): a resonant two-pole section,
 * squared for the 24 dB types; hp mirrors it, bp is its band-pass with the peak at unity */
static float filt_db(float dx, float q, int hp, int bp, int steep)
{
    float wn = fm_exp2f(dx / 22.0f), m2;
    if (hp)
        wn = 1.0f / wn;
    m2 = (1.0f - wn * wn) * (1.0f - wn * wn) + wn * wn / (q * q);   /* 1 / |H|^2 */
    if (bp)
        m2 = m2 * q * q / (wn * wn > 1e-6f ? wn * wn : 1e-6f);
    return -3.0103f * fm_log2f(m2 > 1e-6f ? m2 : 1e-6f) * (steep ? 2.0f : 1.0f);
}
/* the y of db on a filter picture: 0 dB sits 22 px under the top, -48 dB at the bottom; a peak eases into the top
 * rather than running into it (a resonant 24 dB filter reaches +30 dB: cut off flat, it would look square) */
static float filt_y(float db, int32_t y0, int32_t hh)
{
    float s = (float)(hh - 22) / 48.0f;
    if (db > 0.0f)
        return (float)y0 + 22.0f - 22.0f * (1.0f - fm_exp2f(-db * s * (1.0f / 22.0f) * 1.4427f));
    return (float)y0 + 22.0f - db * s;
}
/* the filter figures (the sheet's language): a title (the type), a line of what it is, the four knobs' callouts at the
 * left; at the right a response on graph paper (frequency across, level up; an octave 22 px, 0 dB dashed): the curve
 * with its area tinted, the cutoff a dotted line (callout 1), the resonance the peak's height (callout 2), the
 * envelope's reach an arrow with the curve it moves dimmed (callout 3). Off: a flat line, and the way to turn it on */
#define FG_X 108
#define FG_W 120
#define FG_Y 8
#define FG_H 88
static void filt_fig(const char *title, const char *desc, const uint8_t *par, const int *tps, int cut, int res, int hp, int bp,
                     int steep, int env, int off, const char *offmsg, int band, int hi)
{
    float q = 0.7f + (float)res * 6.0f / 127.0f, ys[FG_W + 1], ye[FG_W + 1];
    int32_t fc = FG_X + 10 + cut * (FG_W - 20) / 127, y0 = FG_Y + 22, i;
    float reach = ((float)env - 64.0f) * (6.0f / 64.0f) * 22.0f;
    {   /* the left: the title, what it is, the four callouts */
        const felucca_font_t *f = text_w(&FONT_L, title) <= 92 ? &FONT_L : text_w(F_VAL, title) <= 92 ? F_VAL : &FONT_B;
        cv_text(8, f == &FONT_L ? 5 : f == F_VAL ? 13 : 17, f, title, K_TEXT);
        if (tr_w(F_LAB, desc, 1) <= 92)
            tr_text(9, 37, F_LAB, desc, K_DIM, 1);
        else
            text_fit(9, 37, F_LAB, desc, 92, K_DIM);
        for (int k = 0; k < 4; k++) {
            char c[2] = {(char)('1' + k), 0};
            const tp_info_t *in = tps[k] >= 0 ? tp_info_par(tps[k], par) : 0;
            if (!in || !in->name || !in->name[0])
                continue;
            circ_num(13, 58 + k * 15, c, off ? K_DIM : K_TEXT, K_BG);
            cv_text(22, 51 + k * 15, &FONT_XS, in->name, off ? K_DIM : K_TEXT);
        }
    }
    cv_frame(FG_X - 1, FG_Y - 1, FG_W + 2, FG_H + 2, 0, K_TEXT, K_BG);
    cv_grid(FG_X, FG_Y, FG_W, FG_H, 8, MAIN_Y, K_GRID);
    cv_dash((float)FG_X, (float)y0, (float)(FG_X + FG_W), (float)y0, 3.0f, 3.0f, 0.8f, K_LINE);
    tr_text(FG_X + FG_W / 2 - tr_w(F_LAB, "FREQ", 1) / 2, FG_Y + FG_H + 3, F_LAB, "FREQ", K_DIM, 1);
    if (off) {
        cv_rect(FG_X, y0, FG_W, 2, K_DIM);
        text_c(FG_X + FG_W / 2, y0 + 12, &FONT_XS, offmsg, K_DIM);
        return;
    }
    if (band) {   /* a bus filter: the band that passes, its low cut (1) and its high cut (4) */
        int32_t lo = fc, hh = FG_X + 10 + hi * (FG_W - 20) / 127 + 12;
        if (hh < lo + 6)
            hh = lo + 6;
        if (hh > FG_X + FG_W - 10)
            hh = FG_X + FG_W - 10;
        cv_rect(lo, y0, hh - lo, FG_H - 22, K_TINT);
        cv_rect(lo, y0, hh - lo, 2, K_NOW);
        cv_rect(lo, y0, 2, FG_H - 22, K_NOW);
        cv_rect(hh - 2, y0, 2, FG_H - 22, K_NOW);
        circ_num(lo, FG_Y + 9, "1", K_TEXT, K_BG);
        circ_num(hh, FG_Y + 9, "4", K_TEXT, K_BG);
        dot_line((float)lo, (float)FG_Y + 15.0f, (float)lo, (float)y0 - 2.0f, 3.0f, K_TEXT);
        dot_line((float)hh, (float)FG_Y + 15.0f, (float)hh, (float)y0 - 2.0f, 3.0f, K_TEXT);
        return;
    }
    for (i = 0; i <= FG_W; i++) {
        float y = filt_y(filt_db((float)(FG_X + i - fc), q, hp, bp, steep), FG_Y, FG_H);
        float z = filt_y(filt_db((float)(FG_X + i - fc) - reach, q, hp, bp, steep), FG_Y, FG_H);
        ys[i] = y < (float)FG_Y + 2.0f ? (float)FG_Y + 2.0f : y > (float)(FG_Y + FG_H - 1) ? (float)(FG_Y + FG_H - 1) : y;
        ye[i] = z < (float)FG_Y + 2.0f ? (float)FG_Y + 2.0f : z > (float)(FG_Y + FG_H - 1) ? (float)(FG_Y + FG_H - 1) : z;
    }
    for (i = 0; i < FG_W; i++) {
        int32_t yt = f_floor(ys[i] + 1.5f);
        if (yt < FG_Y + FG_H)
            cv_rect(FG_X + i, yt, 1, FG_Y + FG_H - yt, K_TINT);
    }
    if (env != 64) {
        pen_t pe = {0};
        for (i = 0; i <= FG_W; i += 2)
            pen_to(&pe, (float)(FG_X + i), ye[i], K_DIM), (i & 2) ? pen_up(&pe) : (void)0;
    }
    {
        pen_t pn = {0};
        pn.w = 2.0f;
        for (i = 0; i <= FG_W; i++)
            pen_to(&pn, (float)(FG_X + i), ys[i], K_NOW);
    }
    {   /* 1: the cutoff */
        int32_t cx = fc - FG_X < 0 ? 0 : fc - FG_X > FG_W ? FG_W : fc - FG_X;
        dot_line((float)fc, (float)FG_Y + 15.0f, (float)fc, ys[cx], 3.0f, K_TEXT);
        circ_num(fc, FG_Y + 9, "1", K_TEXT, K_BG);
    }
    {   /* 2: the resonance: the peak's height over 0 dB */
        float top = (float)y0;
        int32_t px = fc;
        for (i = 0; i <= FG_W; i++)
            if (ys[i] < top)
                top = ys[i], px = FG_X + i;
        if ((float)y0 - top >= 8.0f) {
            int32_t dx = hp ? px + 14 : px - 14;
            dx = dx < FG_X + 16 ? FG_X + 16 : dx > FG_X + FG_W - 16 ? FG_X + FG_W - 16 : dx;
            dim_line((float)dx, (float)y0, (float)dx, top, 3.0f, 3.0f, K_TEXT);
            circ_num(dx + (hp ? 9 : -9), (int32_t)((float)y0 + top) / 2 + 1, "2", K_TEXT, K_BG);
        } else
            circ_num(fc + (hp ? 12 : -12), y0 - 10, "2", K_TEXT, K_BG);
    }
    if (env != 64 && (reach > 5.0f || reach < -5.0f)) {   /* 3: the envelope's reach */
        float ay = (float)(FG_Y + FG_H - 12), xa = (float)fc, xb = (float)fc + reach;
        if (xb < (float)FG_X + 4.0f) xb = (float)FG_X + 4.0f;
        if (xb > (float)(FG_X + FG_W) - 4.0f) xb = (float)(FG_X + FG_W) - 4.0f;
        dim_line(xa, ay, xb, ay, 3.0f, 3.0f, K_TEXT);
        circ_num((int32_t)((xa + xb) * 0.5f), (int32_t)ay - 9, "3", K_TEXT, K_BG);
    }
}
static void draw_key_filter(void)
{
    int trk = sel_trk(), key = ui.dsound % DL_NSOUND;
    const uint8_t *p = par_now(trk) + TP_DRUM(key, 0);
    int ft = p[DP_FTYPE] % NDF;
    uint32_t h = hash_u(hash_u(2166136261u, 0xF2u), (uint32_t)trk | (uint32_t)key << 4 | (uint32_t)ft << 8 |
                                          (uint32_t)p[DP_CUT] << 12 | (uint32_t)p[DP_RES] << 20);
    int tps[4] = {TP_DRUM(key, DP_CUT), TP_DRUM(key, DP_RES), TP_DRUM(key, DP_FENV), TP_DRUM(key, DP_DRIVE)};
    char t[40];
    h = hash_u(h, p[DP_FENV] | (uint32_t)p[DP_DRIVE] << 8);
    if (!begin_main(h))
        return;
    s_cat(t, ft == DF_OFF ? "NO FILTER" : ft == DF_LP ? "LOW-PASS" : ft == DF_HP ? "HIGH-PASS" : "BAND-PASS");
    filt_fig(tp_info_par(TP_DRUM(key, DP_FTYPE), proj.trk[trk].par)->names[ft], t, par_now(trk), tps, p[DP_CUT], p[DP_RES],
             ft == DF_HP, ft == DF_BP, 0, p[DP_FENV], ft == DF_OFF, "PRESET picks a filter", 0, 0);
    cv_blit(0, MAIN_Y);
}
static void draw_env(int filter)
{
    int trk = sel_trk(), b = filter ? TP_FA : TP_A, drum = is_drum(trk) && filter;
    const uint8_t *p = par_now(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0xE7u), (uint32_t)trk | (uint32_t)filter << 4 | (uint32_t)p[b] << 8 |
                                          (uint32_t)p[b + 1] << 16 | (uint32_t)p[b + 2] << 24);
    float x0 = 16.0f, hh = 84.0f, base = 16.0f + hh;
    h = hash_u(h, p[b + 3] | (uint32_t)drum << 8);
    if (is_acid(trk) && !filter) {                   /* the 303's: over 3 s, the filter's opening (its attack's rise,
                                                      * its decay), an accented note's dimmer, the held note's level */
        const float ta = 0.3f * fm_exp2f(p[TP_A] * (13.28771238f / 127.0f)), td = 200.0f * fm_exp2f(p[TP_D] * (3.321928095f / 127.0f));
        const float tc = 30.0f + p[TP_S] * (2970.0f / 127.0f), tl = 30.0f + p[TP_R] * (2970.0f / 127.0f);
        pen_t pa = {0}, pc = {0}, pl = {0};
        if (!begin_main(hash_u(h, 0xAC1Du)))
            return;
        cv_rect(16, 16 + 84, 208, 1, K_LINE);
        for (int32_t xi = 0; xi <= 208; xi++) {
            float ms = (float)xi * (3000.0f / 208.0f);
            float rise = 1.0f - fm_expf(-ms / ta), fl = rise * fm_expf(-ms / td), ac = fm_expf(-ms / tc);
            pen_to(&pl, x0 + (float)xi, base - hh * 0.9f * fm_expf(-ms / tl), K_LINE);
            pen_to(&pc, x0 + (float)xi, base - hh * ac * (1.0f - fm_expf(-ms / 15.0f)), K_DIM);
            if (xi)
                cv_rect((int32_t)x0 + xi, (int32_t)(base - hh * fl + 1.5f), 1, (int32_t)(hh * fl), K_TINT);
            pen_to(&pa, x0 + (float)xi, base - hh * fl, K_NOW);
        }
        cv_text(16, 16 + 84 + 3, &FONT_XS, "filter; dim: accented; line: the note's level", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    if (!begin_main(hash_u(h, (uint32_t)k_now << 9)))
        return;
    if (drum) {                                      /* the kit's: lag (1), attack (2), the hit's length (4) */
        float ew[3] = {0.25f + (float)p[TP_A] / 127.0f, 0.25f + (float)p[TP_D] / 127.0f, 1.2f + (float)p[TP_R] / 127.0f}, sw[3];
        float x1, x2, x3 = EF_X0 + EF_SPAN;
        curve_t c = {{0}, {0}, 0, 0};
        ef_stages(sw, ew, 3);
        x1 = EF_X0 + sw[0], x2 = x1 + sw[1];
        ef_pt(&c, EF_X0, EF_BASE);
        ef_pt(&c, x1, EF_BASE);
        for (int i = 1; i <= 12; i++) {
            float t = (float)i / 12.0f;
            ef_pt(&c, x1 + (x2 - x1) * t, EF_BASE - (EF_BASE - EF_TOP) * (1.0f - (1.0f - t) * (1.0f - t)));
        }
        for (int i = 1; i <= 32; i++) {
            float t = (float)i / 32.0f;
            ef_pt(&c, x2 + (x3 - x2) * t, EF_BASE - (EF_BASE - EF_TOP) * ease_exp(t));
        }
        ef_head("KIT", "every key: lag, attack, length");
        ef_curve(&c);
        ef_node(x1, EF_BASE), ef_node(x2, EF_TOP), ef_node(x3, EF_BASE);
        ef_dim(EF_X0, x1, 1), ef_dim(x1, x2, 2), ef_dim(x2, x3, 4);
        cv_blit(0, MAIN_Y);
        return;
    }
    {   /* attack (1), decay (2) to the sustain's level (3, its height), the note held, release (4) */
        float a = EF_MIN + (float)p[b] * 28.0f / 127.0f, d = EF_MIN + (float)p[b + 1] * 28.0f / 127.0f;
        float r = EF_MIN + (float)p[b + 3] * 28.0f / 127.0f, s = (float)p[b + 2] / 127.0f;
        float x1 = EF_X0 + a, x2 = x1 + d, x4 = 222.0f, x3 = x4 - r, ys = EF_BASE - (EF_BASE - EF_TOP) * s;
        curve_t c = {{0}, {0}, 0, 0};
        ef_pt(&c, EF_X0, EF_BASE);
        for (int i = 1; i <= 12; i++) {
            float t = (float)i / 12.0f;
            ef_pt(&c, EF_X0 + a * t, EF_BASE - (EF_BASE - EF_TOP) * (1.0f - (1.0f - t) * (1.0f - t)));
        }
        for (int i = 1; i <= 16; i++) {
            float t = (float)i / 16.0f;
            ef_pt(&c, x1 + d * t, ys - (EF_BASE - EF_TOP) * (1.0f - s) * ease_exp(t) + 0.0f * t);
        }
        for (int i = 0; i <= 16; i++) {
            float t = (float)i / 16.0f;
            ef_pt(&c, x3 + r * t, EF_BASE - (EF_BASE - ys) * ease_exp(t));
        }
        ef_head(filter ? "FILTER" : "AMP", filter ? "cutoff over time" : "amplitude over time");
        ef_curve(&c);
        ef_node(x1, EF_TOP), ef_node(x2, ys), ef_node(x3, ys), ef_node(x4, EF_BASE);
        ef_dim(EF_X0, x1, 1), ef_dim(x1, x2, 2), ef_dim(x3, x4, 4);
        {   /* the sustain: a level, so a height, across the held part */
            int32_t xm = (int32_t)((x2 + x3) * 0.5f);
            if (EF_BASE - ys >= 18.0f) {
                dim_line((float)xm, EF_BASE, (float)xm, ys, 4.0f, 4.0f, K_TEXT);
                cv_rect(xm - 7, (int32_t)((EF_BASE + ys) * 0.5f) - 6, 15, 13, K_BG);
                circ_num(xm, (int32_t)((EF_BASE + ys) * 0.5f) + 1, "3", K_TEXT, K_BG);
            } else
                circ_num(xm, (int32_t)ys - 9, "3", K_TEXT, K_BG);
        }
        cv_blit(0, MAIN_Y);
    }
}
static void draw_filter(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int ft = p[TP_FTYPE] % NFT, cut = p[TP_CUT], res = p[TP_RES], dyn = ft == FT_DYNAMIC, drum = is_drum(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0xF1u), (uint32_t)trk | (uint32_t)ft << 4 | (uint32_t)cut << 8 | (uint32_t)res << 16 |
                                          (uint32_t)p[TP_KTRK] << 24);
    static const int TPS[4] = {TP_CUT, TP_RES, TP_FENV, TP_KTRK};
    char t[40];
    static const char *const DESC[NFT] = {"LOW-PASS, 24 DB", "LADDER LOW-PASS", "LOW-PASS, 12 DB", "HIGH-PASS, 12 DB",
                                          "BAND-PASS, 12 DB", "NO FILTER"};
    if (drum && ui.page == P_FILTER) {               /* a kit's Filter page: the key's own */
        draw_key_filter();
        return;
    }
    h = hash_u(h, p[TP_FENV] | (uint32_t)drum << 8);
    if (!begin_main(h))
        return;
    if (drum && dyn)
        filt_fig("bus filter", "LOW CUT TO HI CUT", p, TPS, p[TP_CUT], 0, 0, 0, 0, 64, 0, "", 1, p[TP_KTRK]);
    else if (drum || dyn)
        filt_fig(drum ? "bus filter" : "no filter", drum ? "BUS FILTER IS OFF" : "UNFILTERED", p, TPS, cut, res, 0, 0, 0, 64, 1,
                 drum ? "PRESET turns it on" : "PRESET picks a filter", 0, 0);
    else {
        s_cat(t, DESC[ft]);
        filt_fig(tp_info_par(TP_FTYPE, p)->names[ft], t, p, TPS, cut, res, ft == FT_HP12, ft == FT_BP12, ft == FT_LP24 || ft == FT_LADDER,
                 p[TP_FENV], 0, "", 0, 0);
    }
    cv_blit(0, MAIN_Y);
}
/* the instrument's character stage: a machine for each type, its amount, tone and mix (knobs 2-4) its own controls at
 * the live values, the type (1) the title. drive an overdrive stompbox; fold a wavefolder module patched between its
 * neighbours; crush a toy computer whose coarse screen shows a wave crushed as far as the bits and the rate go; ring
 * a ring modulator, two inputs and a carrier dial; tilt a hi-fi's tone strip, one big tilt knob, the tilt printed
 * small beside it about its pivot; tape a cassette deck, its meters as far over as it drives; sampler a small
 * keyboard sampler, its rate on its display; off the stompbox unplugged, its cable coiled beside it */
static void fxm_cable(float x0, float y0, float x1, float y1, float s, uint16_t c)   /* a patch cable, sagging s px */
{
    pen_t pn = {0};
    pn.w = 1.6f;
    cv_ring(x0, y0, 2.4f, 1.0f, K_TEXT), cv_ring(x1, y1, 2.4f, 1.0f, K_TEXT);
    for (int i = 0; i <= 20; i++) {
        float t = (float)i / 20.0f;
        pen_to(&pn, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t + s * 4.0f * t * (1.0f - t), c);
    }
}
static void fxm_keys(int32_t x, int32_t y, int32_t w, int32_t h, int nw, int32_t bh)   /* nw white keys */
{
    float kw = (float)w / (float)nw;
    fxm_panel(x, y, w, h, K_CARD);
    for (int k = 1; k < nw; k++)
        cv_rect(x + (int32_t)(kw * (float)k), y, 1, h, K_TEXT);
    for (int k = 0; k < nw - 1; k++)
        if (k % 7 != 2 && k % 7 != 6)
            cv_rect(x + (int32_t)(kw * ((float)k + 0.68f)), y, (int32_t)(kw * 0.64f + 0.5f), bh, K_TEXT);
}
__attribute__((noinline)) static void fxm_stomp(float a, float t, float m, int live)   /* drive's pedal; off's, unplugged */
{
    cv_round(86, 38, 68, 78, 7, live ? K_TEXT : K_DIM);
    cv_round(88, 40, 64, 74, 5, K_CARD);
    cv_rect(88, 70, 64, 2, live ? K_TEXT : K_DIM);
    fxm_knob(104.0f, 55.0f, 8.5f, a, live ? 2 : 0, -26, 0);
    fxm_knob(136.0f, 55.0f, 8.5f, t, live ? 3 : 0, 26, 0);
    fxm_knob(104.0f, 84.0f, 6.0f, m, live ? 4 : 0, -26, 0);
    fxm_lamp(124, 82, 14, live && m > 0.0f ? K_NOW : K_BG);
    fxm_foot(120.0f, 102.0f);
    fxm_jack(154.0f, 94.0f);
    if (live) {
        fxm_jack(86.0f, 94.0f);
        fxm_lead(157.0f, 94.0f, 240.0f, 108.0f);
        fxm_lead(83.0f, 94.0f, 0.0f, 108.0f);
    } else {                                         /* the cable coiled on the floor, its plug loose */
        fxm_coil(160.0f, 104.0f, 5, 5.0f);
        fxm_ln(193.0f, 104.0f, 204.0f, 98.0f, 1.6f);
        cv_rect(203, 95, 12, 6, K_TEXT);
        cv_rect(215, 97, 6, 2, K_TEXT);
    }
}
/* fold: the module between two neighbours, patched to them */
__attribute__((noinline)) static void fxm_ch_fold(float a, float t, float m, float tc)
{
    for (int s = 0; s < 2; s++) {
        int32_t x = s ? 164 : 28;
        fxm_panel(x, 42, 48, 72, K_PANEL);
        for (int k = 0; k < 3; k++)
            fxm_knob((float)(x + 24), (float)(56 + k * 18), 4.5f, (float)((k * 3 + s * 2 + 1) % 5) / 4.0f, 0, 0, 0);
        fxm_jack((float)(x + 14), 108.0f), fxm_jack((float)(x + 34), 108.0f);
    }
    fxm_panel(96, 38, 48, 80, K_CARD);
    fxm_screw(102.0f, 43.0f), fxm_screw(138.0f, 43.0f), fxm_screw(102.0f, 113.0f), fxm_screw(138.0f, 113.0f);
    {   /* the fold printed on its face */
        pen_t pn = {0};
        pn.w = 1.0f;
        for (int i = 0; i <= 24; i++)
            pen_to(&pn, 108.0f + (float)i, 48.0f - 3.5f * fm_sin_turns(fm_sin_turns((float)i / 24.0f) * 1.2f), K_TEXT);
    }
    fxm_ticks(120.0f, 67.0f, 12.0f, 14.5f, 11);
    fxm_knob(120.0f, 67.0f, 10.0f, a, 2, -32, 0);
    fxm_knob(110.0f, 90.0f, 5.5f, t, 3, -22, 0);
    fxm_knob(130.0f, 90.0f, 5.5f, m, 4, 22, 0);
    fxm_jack(108.0f, 106.0f), fxm_jack(120.0f, 106.0f), fxm_jack(132.0f, 106.0f);
    fxm_cable(108.0f, 106.0f, 62.0f, 108.0f, 7.0f, K_TEXT);
    fxm_cable(132.0f, 106.0f, 178.0f, 108.0f, 9.0f, K_NOW);
    (void)a, (void)t, (void)m, (void)tc;
}
/* crush: the toy: its handle, its screen of big pixels, its buttons */
__attribute__((noinline)) static void fxm_ch_crush(float a, float t, float m, float tc)
{
    float bits = 15.0f - a * fm_minf(1.0f, 1.0f + tc) * 13.0f;
    int lv = bits >= 6.0f ? 7 : (int)bits, hold = 1 + (int)(a * fm_minf(1.0f, 1.0f - tc) * 3.0f);
    ellipse_arc(120.0f, 42.0f, 26.0f, 9.0f, 0.5f, 1.0f, 3.0f, K_TEXT);
    cv_round(56, 40, 128, 76, 16, K_TEXT);
    cv_round(58, 42, 124, 72, 14, K_CARD);
    cv_rect(68, 49, 64, 39, K_TEXT);
    for (int c = 0; c < 12; c++) {   /* the wave: a pixel a column, held hold columns, its level to lv steps */
        float v = fm_sin_turns((float)(c - c % hold) / 12.0f);
        int r = (int)fm_floorf((v + 1.0f) * 0.5f * (float)(lv - 1) + 0.5f) * 6 / (lv - 1);
        for (int j = 0; j < 7; j++)
            cv_rect(71 + c * 5, 82 - j * 5, 4, 4, j == r ? K_CARD : col_mix(K_TEXT, K_CARD, 3));
    }
    for (int k = 0; k < 3; k++) {
        cv_disc((float)(78 + k * 20), 101.0f, 6.5f, k == 1 ? K_TEXT : K_PANEL2);
        cv_ring((float)(78 + k * 20), 101.0f, 6.5f, 1.2f, K_TEXT);
    }
    fxm_knob(150.0f, 58.0f, 6.0f, a, 2, 16, 0);
    fxm_knob(150.0f, 80.0f, 6.0f, t, 3, 16, 0);
    fxm_knob(150.0f, 102.0f, 6.0f, m, 4, 16, 0);
    (void)a, (void)t, (void)m, (void)tc;
}
/* ring: two inputs, the carrier's dial (the ratio), depth and mix; the output */
__attribute__((noinline)) static void fxm_ch_ring(float a, float t, float m, float tc)
{
    fxm_panel(56, 46, 128, 66, K_CARD);
    fxm_jack(68.0f, 62.0f), fxm_jack(68.0f, 96.0f);
    cv_text(76, 56, F_LAB, "X", K_DIM), cv_text(76, 90, F_LAB, "Y", K_DIM);
    fxm_lead(8.0f, 54.0f, 65.0f, 62.0f), fxm_lead(8.0f, 106.0f, 65.0f, 96.0f);
    fxm_ticks(114.0f, 79.0f, 16.0f, 19.0f, 16);
    fxm_knob(114.0f, 79.0f, 13.0f, t, 3, -20, -20);
    fxm_knob(158.0f, 66.0f, 6.0f, a, 2, 0, -13);
    fxm_knob(158.0f, 98.0f, 6.0f, m, 4, 0, -13);
    fxm_jack(184.0f, 79.0f);
    fxm_lead(187.0f, 79.0f, 240.0f, 90.0f);
    (void)a, (void)t, (void)m, (void)tc;
}
/* tilt: the strip: pivot, the printed tilt, the big tilt knob, mix, a lamp */
__attribute__((noinline)) static void fxm_ch_tilt(float a, float t, float m, float tc)
{
    float px = 64.0f + 40.0f * a, dy = tc * 9.0f;
    fxm_panel(8, 60, 224, 36, K_CARD);
    for (int k = 0; k < 4; k++)
        cv_rect(10, 65 + k * 8, 220, 1, K_PANEL);
    fxm_screw(15.0f, 66.0f), fxm_screw(15.0f, 90.0f), fxm_screw(225.0f, 66.0f), fxm_screw(225.0f, 90.0f);
    cv_rect(24, 96, 12, 3, K_TEXT), cv_rect(204, 96, 12, 3, K_TEXT);
    fxm_knob(42.0f, 78.0f, 7.0f, a, 2, 0, -24);
    fxm_outline(62.5f, 67.5f, 106.5f, 89.5f, 0.8f);
    cv_dash(63.0f, 78.5f, 106.0f, 78.5f, 2.0f, 2.0f, 0.8f, K_DIM);
    fxm_ln(64.0f, 78.0f + dy * (px - 64.0f) / 22.0f, 105.0f, 78.0f - dy * (105.0f - px) / 22.0f, 1.2f);
    cv_disc(px, 78.0f, 1.8f, K_TEXT);
    cv_disc(140.0f, 78.0f, 15.0f, K_PANEL2);
    fxm_ticks(140.0f, 78.0f, 16.0f, 19.0f, 11);
    fxm_knob(140.0f, 78.0f, 12.0f, t, 3, 0, -28);
    fxm_knob(184.0f, 78.0f, 7.0f, m, 4, 0, -24);
    fxm_lamp(206, 70, 10, m > 0.0f ? K_NOW : K_BG);
    fxm_panel(206, 80, 10, 10, K_TEXT);
    (void)a, (void)t, (void)m, (void)tc;
}
/* tape: the deck: the cassette in its well, the keys (play down), its knobs, two meters */
__attribute__((noinline)) static void fxm_ch_tape(float a, float t, float m, float tc)
{
    fxm_panel(16, 40, 208, 76, K_CARD);
    fxm_panel(26, 46, 92, 50, K_PANEL);
    fxm_panel(32, 52, 80, 38, K_CARD);
    cv_rect(46, 62, 52, 18, K_TEXT);
    cv_disc(58.0f, 71.0f, 7.5f, K_DIM), cv_disc(86.0f, 71.0f, 5.0f, K_DIM);   /* the tape on its spools */
    for (int s = 0; s < 2; s++)
        cv_disc(s ? 86.0f : 58.0f, 71.0f, 3.5f, K_CARD), cv_ring(s ? 86.0f : 58.0f, 71.0f, 3.5f, 1.0f, K_TEXT);
    cv_rect(38, 55, 68, 4, K_LINE);
    for (int k = 0; k < 6; k++)
        fxm_panel(26 + k * 15, 102, 13, 10, k == 2 ? K_TEXT : K_PANEL);
    for (int k = 0; k < 3; k++)
        fxm_knob((float)(146 + k * 33), 58.0f, 7.0f, k == 0 ? a : k == 1 ? t : m, k + 2, -16, 0);
    for (int s = 0; s < 2; s++) {
        float x = (float)(157 + s * 40), an = 0.64f + 0.22f * a;
        fxm_panel(140 + s * 40, 80, 34, 22, K_BG);
        ellipse_arc(x, 100.0f, 13.0f, 13.0f, 0.62f, 0.88f, 0.8f, K_DIM);
        fxm_ln(x, 100.0f, x + 13.0f * fm_sin_turns(an + 0.25f), 100.0f + 13.0f * fm_sin_turns(an), 1.2f);
    }
    (void)a, (void)t, (void)m, (void)tc;
}
/* sampler: the keyboard sampler: a disk slot, its rate on the display, its knobs, the keys */
__attribute__((noinline)) static void fxm_ch_sampler(float a, float t, float m, float tc)
{
    char r[12];
    s_cat(num_s((uint32_t)(26.04f * fm_exp2f(-a * 1.243f) + 0.5f), r), " kHz");
    fxm_panel(8, 42, 224, 36, K_CARD);
    cv_rect(16, 51, 42, 4, K_TEXT);
    cv_rect(16, 60, 6, 3, K_NOW), cv_rect(48, 60, 10, 5, K_TEXT);
    cv_rect(64, 48, 50, 22, K_TEXT);
    text_c(89, 52, &FONT_XS, r, K_CARD);
    for (int k = 0; k < 3; k++)
        fxm_knob((float)(146 + k * 31), 60.0f, 6.5f, k == 0 ? a : k == 1 ? t : m, k + 2, -15, 0);
    fxm_keys(8, 78, 224, 36, 22, 22);
    (void)a, (void)t, (void)m, (void)tc;
}
static void draw_character(void)
{
    static const char *const ABOUT[NCT] = {"VOICE AS IT IS", "SOFT SATURATION", "SINE WAVEFOLDER",
                                           "BITS AND RATE", "RING MODULATOR", "TILT EQ",
                                           "WORN TAPE", "OLD SAMPLER GRAIN"};
    int trk = inst_trk(), drum = is_drum(trk);
    const uint8_t *p = par_now(trk);
    int ct = p[TP_CTYPE] % NCT;
    float a = FXU(p[TP_CAMT]), t = FXU(p[TP_CTONE]), m = FXU(p[TP_CMIX]), tc = fm_clampf(((float)p[TP_CTONE] - 64.0f) * (1.0f / 63.0f), -1.0f, 1.0f);
    uint32_t h = hash_u(hash_u(2166136261u, 0xC5u), (uint32_t)trk | (uint32_t)ct << 4 | (uint32_t)p[TP_CAMT] << 8 |
                                          (uint32_t)p[TP_CTONE] << 16 | (uint32_t)p[TP_CMIX] << 24);
    char d[32];
    h = hash_u(h, (uint32_t)drum | (uint32_t)fxm_touch() << 4);
    if (!begin_main(h))
        return;
    s_cat(s_cat(s_cat(d, drum ? "on all " : "after "), TRACK_NAME[trk]), drum ? "'s keys" : "'s filter");
    fxm_title(CTYPE_NAME(p), ABOUT[ct], d, 1, K_TEXT);
    switch (ct) {
    case CT_DRIVE: fxm_stomp(a, t, m, 1); break;
    case CT_FOLD: fxm_ch_fold(a, t, m, tc); break;
    case CT_CRUSH: fxm_ch_crush(a, t, m, tc); break;
    case CT_RING: fxm_ch_ring(a, t, m, tc); break;
    case CT_TILT: fxm_ch_tilt(a, t, m, tc); break;
    case CT_TAPE: fxm_ch_tape(a, t, m, tc); break;
    case CT_SAMPLER: fxm_ch_sampler(a, t, m, tc); break;
    default: fxm_stomp(a, t, m, 0); break;
    }
    cv_blit(0, MAIN_Y);
}
/* the sends: a block diagram (the sheet's Sends): the voice (or the kit key) at the left, its dry path to the speaker
 * (thick), with pan (a ruler) and level (a wedge) on it; at each send's tap a line up (FX I) or down (FX II) into a
 * box that names the effect, and its return to the dry path: dashed and thin at 0, solid and thicker with the amount,
 * its percentage broken into the line by its knob's number */
static void snd_seg(float x0, float y0, float x1, float y1, int v)   /* a tap's line: dashed at 0, thickening with v */
{
    if (!v)
        cv_dash(x0, y0, x1, y1, 2.5f, 2.5f, 0.9f, K_TEXT);
    else
        cv_linef(x0, y0, x1, y1, K_TEXT, 1.0f + 2.2f * (float)v / 127.0f);
}
static void draw_sends(void)
{
    int trk = inst_trk(), key = ui.dsound % DL_NSOUND, drum = is_drum(trk);
    const uint8_t *p = par_now(trk), *sv = drum ? p + TP_DRUM(key, DP_SEND1) : p + TP_SEND1;
    uint32_t h = hash_u(hash_u(2166136261u, 0x5Eu), (uint32_t)trk | (uint32_t)sv[0] << 4 | (uint32_t)sv[1] << 12 |
                                          (uint32_t)p[TP_LEVEL] << 20);
    const int32_t DY = 66;
    char t[32], nm[2][24];
    h = hash_u(h, proj.trk[AUX_FX1].par[TP_ENGINE] | (uint32_t)proj.trk[AUX_FX2].par[TP_ENGINE] << 8 |
                      (uint32_t)(drum ? key + 1 : 0) << 16 | (uint32_t)p[TP_PAN] << 24);
    h = hash_u(h, (uint32_t)fxm_touch());
    if (!begin_main(h))
        return;
    for (int i = 0; i < 2; i++) {
        const char *l = fx_label(fx_pick_of(AUX_FX1 + i));
        uint32_t n;
        for (n = 0; n < 23u && l[n]; n++)
            nm[i][n] = l[n];
        nm[i][n] = 0;
    }
    {   /* the head: the voice (the key), what the page shows */
        int32_t e = tr_text(8, 3, F_LAB, drum ? DL_SOUND_NAME[key] : TRACK_NAME[trk], K_DIM, 1);
        cv_text(e + 7, 0, &FONT_XS, "where the sound goes", K_TEXT);
    }
    {   /* the source box */
        const char *sub = drum ? DL_SOUND_NAME[key] : EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE];
        cv_frame(8, DY - 16, 48, 32, 3, K_TEXT, K_CARD);
        text_c(32, DY - 14, F_XB, TRACK_NAME[trk], K_TEXT);
        text_c(32, DY - 1, &FONT_XS, sub, K_DIM);
    }
    cv_linef(56.0f, (float)DY, 195.0f, (float)DY, K_TEXT, 2.0f);   /* the dry path, to the speaker */
    arrow_head(199.0f, (float)DY, 1.0f, 0.0f, K_TEXT);
    {   /* the speaker */
        const float sp[12] = {200, DY - 6, 206, DY - 6, 214, DY - 13, 214, DY + 13, 206, DY + 6, 200, DY + 6};
        cv_poly(sp, 6, K_CARD, 256u, 0);
        for (int i = 0; i < 6; i++)
            cv_linef(sp[2 * i], sp[2 * i + 1], sp[2 * ((i + 1) % 6)], sp[2 * ((i + 1) % 6) + 1], K_TEXT, 1.0f);
        ellipse_arc(214.0f, (float)DY, 6.0f, 6.0f, -0.13f, 0.13f, 1.0f, K_TEXT);
        ellipse_arc(214.0f, (float)DY, 11.0f, 11.0f, -0.12f, 0.12f, 1.0f, K_TEXT);
    }
    for (int i = 0; i < 2; i++) {   /* the taps */
        int up = i == 0, v = sv[i];
        int32_t x = up ? 88 : 124, bx = x - 33, by = up ? 16 : 85, bw = 66, rx = up ? 162 : 188;
        int32_t ly = up ? DY - 10 : DY + 13, pc = v * 100 / 127;
        char pt[8];
        snd_seg((float)x, (float)DY, (float)x, up ? (float)(by + 32 + 3) : (float)(by - 3), v);
        arrow_head((float)x, up ? (float)(by + 33) : (float)(by - 1), 0.0f, up ? -1.0f : 1.0f, K_TEXT);
        cv_disc((float)x, (float)DY, 2.2f, K_TEXT);
        cv_frame(bx, by, bw, 32, 3, K_TEXT, K_CARD);
        text_c(bx + bw / 2, by + 2, F_XB, i ? "FX II" : "FX I", K_TEXT);
        text_c(bx + bw / 2, by + 14, &FONT_XS, text_w(&FONT_XS, nm[i]) <= bw - 6 ? nm[i] : fx_pick_name(fx_pick_of(AUX_FX1 + i)), K_DIM);
        snd_seg((float)(bx + bw), (float)(by + 16), (float)rx, (float)(by + 16), v);
        snd_seg((float)rx, (float)(by + 16), (float)rx, up ? (float)(DY - 4) : (float)(DY + 4), v);
        arrow_head((float)rx, up ? (float)(DY - 1) : (float)(DY + 1), 0.0f, up ? 1.0f : -1.0f, K_TEXT);
        s_cat(num_s((uint32_t)pc, pt), "%");
        fxm_num(x + 9, ly + 1, i + 1);
        cv_text(x + 18, ly - 6, F_LAB, pt, K_TEXT);
    }
    {   /* under the voice, off the path: pan (3) a ruler with its pointer, level (4) a wedge, taller the louder */
        float hh = 2.0f + 11.0f * (float)p[TP_LEVEL] / 127.0f;
        fxm_num(13, 98, 3);
        cv_text(22, 91, &FONT_XS, "pan", K_TEXT);
        ruler(52, 100, 30, p[TP_PAN], 127, K_TEXT);
        fxm_num(13, 113, 4);
        cv_text(22, 106, &FONT_XS, "level", K_TEXT);
        cv_tri(56.0f, 116.0f, 82.0f, 116.0f, 82.0f, 116.0f - hh, K_TINT);
        cv_linef(56.0f, 116.0f, 82.0f, 116.0f - hh, K_TEXT, 1.0f);
        cv_linef(82.0f, 116.0f, 82.0f, 116.0f - hh, K_TEXT, 1.0f);
        cv_linef(56.0f, 116.0f, 82.0f, 116.0f, K_TEXT, 1.0f);
    }
    (void)t;
    cv_blit(0, MAIN_Y);
}
/* an LFO shape at phase ph (0..1), -1..1: 0 sine, 1 saw (falling), 2 exponential, 3 square, 4 blip, 5 ramp (rising) */
static float lfo_shape_v(int shape, float ph)
{
    switch (shape) {
    case 1: return 1.0f - 2.0f * ph;
    case 2: return 2.0f * fm_exp2f(-8.0f * ph) - 1.0f;
    case 3: return ph < 0.5f ? 1.0f : -1.0f;
    case 4: return ph < 0.08f ? 1.0f : -1.0f;
    case 5: return 2.0f * ph - 1.0f;
    default: return fm_sin_turns(ph);
    }
}
/* the LFO page: a pendulum and one cycle of the real shape it draws on graph paper (the sheet's LFO): the bob swings as
 * the marker runs along the wave; the rate a dimension line under one cycle (its value beside it, callout 2), the
 * depth a vertical arrow at the crest (callout 3; a negative depth turns the wave over), the type the title
 * (callout 1), and what it moves a line of words (callout 4). Off: the pendulum hangs still, the wave a dashed
 * level line, and the words "LFO off" */
static void draw_lfo(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int lt = p[TP_LTYPE] % NLT, sh = p[TP_LX2];
    uint32_t h = hash_u(hash_u(2166136261u, 0x1Fu), (uint32_t)trk | (uint32_t)lt << 4 | (uint32_t)p[TP_LAMT] << 8 |
                                          (uint32_t)sh << 16);
    const int32_t GX = 118, GW = 112, GY = 10, GH = 84, CY = GY + GH / 2;
    const float PX = 46.0f, PY = 60.0f, PL = 40.0f, P = 64.0f;
    float v = fm_clampf(((float)p[TP_LAMT] - 64.0f) / 63.0f, -1.0f, 1.0f), A = 34.0f * (v < 0.0f ? -v : v);
    float ph = 0.0f;
    char t[48], num[12], unit[6], ttl[16];
    h = hash_u(h, (uint32_t)p[TP_LSPEED] << 8 | p[TP_LX] | (uint32_t)p[TP_LDEST] << 16);
    ph = 0.25f;                                       /* (a still: the marker at the first crest, the bob at its depth) */
    ph -= fm_floorf(ph);
    if (!begin_main(h))
        return;
#define LFOV(u) (lt == LT_WAVE || lt == LT_WOBBLE ? lfo_shape_v(lt == LT_WAVE ? (sh & 3) == 1 ? 0 : (sh & 3) == 0 ? 3 : (sh & 3) == 2 ? 5 : 1 : sh, \
                 (u) - fm_floorf(u)) : lt == LT_DRIFT ? fm_sin_turns((u) * 0.9f) * 0.8f + fm_sin_turns((u) * 2.3f + 0.3f) * 0.2f \
                 : 1.0f - 2.0f * ease_exp((u) - fm_floorf(u)))
    {   /* the title: the shape (wave, wobble), else the type */
        const char *nm = lt == LT_OFF ? "off" : lt == LT_WAVE ? tp_info_par(TP_LX2, p)->names ? tp_info_par(TP_LX2, p)->names[sh & 3] : "wave"
                         : lt == LT_WOBBLE ? tp_info_par(TP_LX2, p)->names ? tp_info_par(TP_LX2, p)->names[sh % 5] : "wobble"
                         : tp_info_par(TP_LTYPE, p)->names[lt];
        const felucca_font_t *f = text_w(&FONT_L, nm) <= 100 ? &FONT_L : text_w(F_VAL, nm) <= 100 ? F_VAL : &FONT_B;
        uint32_t i;
        for (i = 0; i < sizeof ttl - 1u && nm[i]; i++)
            ttl[i] = nm[i];
        ttl[i] = 0;
        cv_text(8, f == &FONT_L ? 5 : f == F_VAL ? 13 : 17, f, ttl, K_TEXT);
    }
    {   /* what it moves (callout 4) */
        char *e = t;
        *t = 0;
        if (lt == LT_OFF)
            s_cat(t, "NOTHING MOVES");
        else if (lt == LT_WAVE || lt == LT_DRIFT) {
            tp_text_par(TP_LDEST, p[TP_LDEST], p, num, unit);
            e = s_cat(t, num);
            tp_text_par(TP_LX, p[TP_LX], p, num, unit);
            s_cat(s_cat(e, " "), num);
            up_copy(t, t, sizeof t);
        } else
            s_cat(t, lt == LT_WOBBLE ? "A WOBBLE" : "DUCK AND SWELL");
        circ_num(13, 43, "4", lt ? K_TEXT : K_DIM, K_BG);
        if (tr_w(F_LAB, t, 1) <= 94)
            tr_text(22, 37, F_LAB, t, K_DIM, 1);
        else
            text_fit(22, 37, F_LAB, t, 94, K_DIM);
    }
    {   /* the pendulum */
        float th = lt ? 0.5f * LFOV(ph) * (v < 0.0f ? -1.0f : 1.0f) * (A > 1.0f ? A / 34.0f : 0.1f) : 0.0f;
        float lim = 0.5f * (A > 1.0f ? A / 34.0f : 0.1f);
        float bx = PX + PL * fm_sin_turns(th * 0.159155f), by = PY + PL * fm_sin_turns(0.25f - th * 0.159155f);
        cv_rect((int32_t)PX - 14, (int32_t)PY - 4, 28, 4, K_TEXT);
        circ_num(13, 62, "1", K_TEXT, K_BG);
        if (lt) {   /* the two extremes, dotted, and the arc between */
            for (int sd = -1; sd <= 1; sd += 2) {
                float ex = PX + (float)sd * PL * fm_sin_turns(lim * 0.159155f), ey = PY + PL * fm_sin_turns(0.25f - lim * 0.159155f);
                dot_line(PX, PY, ex, ey, 3.0f, K_DIM);
                cv_disc(ex, ey, 5.0f, K_BG);
                cv_ring(ex, ey, 5.0f, 0.9f, K_DIM);
            }
            ellipse_arc(PX, PY, PL, PL, 0.25f - lim * 0.159155f, 0.25f + lim * 0.159155f, 0.8f, K_LINE);
        }
        cv_linef(PX, PY, bx, by, K_TEXT, 1.2f);
        cv_disc(bx, by, 6.0f, K_NOW);
        cv_ring(bx, by, 6.0f, 1.1f, K_TEXT);
    }
    cv_frame(GX - 1, GY - 1, GW + 2, GH + 2, 0, K_TEXT, K_BG);
    cv_grid(GX, GY, GW, GH, 8, MAIN_Y, K_GRID);
    cv_rect(GX, CY, GW, 1, K_LINE);
    if (lt == LT_OFF) {
        cv_dash((float)GX, (float)CY, (float)(GX + GW), (float)CY, 3.0f, 3.0f, 1.2f, K_DIM);
        text_c(GX + GW / 2, CY - 22, &FONT_S, "LFO off", K_DIM);
    } else {
        pen_t pn = {0};
        float mk = 0.0f;
        pn.w = 2.0f;
        for (int32_t x = 0; x <= GW; x++) {
            float u = (float)x / P, val = (float)LFOV(u) * (v < 0.0f ? -1.0f : 1.0f);
            pen_to(&pn, (float)(GX + x), (float)CY - val * (A > 1.0f ? A : 3.0f), K_TEXT);
        }
        {
            float u = ph, val = (float)LFOV(u) * (v < 0.0f ? -1.0f : 1.0f);
            mk = (float)GX + ph * P;
            cv_disc(mk, (float)CY - val * (A > 1.0f ? A : 3.0f), 3.2f, K_NOW);
            cv_ring(mk, (float)CY - val * (A > 1.0f ? A : 3.0f), 3.4f, 1.0f, K_TEXT);
        }
        {   /* the depth: an arrow from the centre line to the shape's crest (callout 3) */
            int32_t bi = 0;
            float best = -2.0f, vv;
            for (int32_t x = 0; x < (int32_t)P; x++) {
                vv = (float)LFOV((float)x / P) * (v < 0.0f ? -1.0f : 1.0f);
                if (vv > best + 0.001f)
                    best = vv, bi = x;
            }
            if (bi < 10)
                bi = 10;
            if (A > 5.0f) {
                float ay = (float)CY - best * A;
                cv_linef((float)(GX + bi), (float)CY, (float)(GX + bi), ay + 3.0f, K_TEXT, 1.0f);
                arrow_head((float)(GX + bi), ay, 0.0f, best >= 0.0f ? -1.0f : 1.0f, K_TEXT);
            }
            circ_num(GX + bi + 11, CY - (int32_t)(best * A * 0.5f) - (int32_t)(A > 5.0f ? 0 : 8), "3", K_TEXT, K_BG);
        }
    }
    {   /* the rate: one cycle's width, dimensioned (callout 2), its value after it */
        float x1 = (float)GX, x2 = (float)GX + P;
        dim_line(x1, 104.0f, x2, 104.0f, 4.0f, 4.0f, K_TEXT);
        cv_rect(GX + 25, 98, 14, 13, K_BG);
        circ_num(GX + 32, 105, "2", K_TEXT, K_BG);
        tp_text_par(TP_LSPEED, p[TP_LSPEED], p, num, unit);
        s_cat(s_cat(s_cat(t, num), unit[0] ? " " : ""), unit);
        cv_text(GX + 71, 98, &FONT_XS, t, K_TEXT);
    }
#undef LFOV
    cv_blit(0, MAIN_Y);
}
#define BLACK_KEY(n) ((0x54Au >> ((n) % 12)) & 1)          /* C# D# F# G# A#: a black key's semitones */
/* the players page (the sheet's Players): the keys as they are under the hands (F3 .. G5 at this octave, small: down in the
 * track's ink, the notes the player sounds now in ink, a strum's chord dotted under its keys), an arrow, and what the
 * player makes of them. Arp: the held keys climb a staircase of notes (the pattern and the octaves real: each bar a
 * note, the sounding one vermilion, the +1 octave marked), the rate a dimension over one step; strum: the chord's
 * notes staggered by the roll; hold: the notes drawn long, to the next change; random: a grid of slots with notes
 * where the dice give them. No player: the keys carried straight across. A kit: no player (crossed keys) */
static void pl_keys(int32_t x0, int32_t y0, int trk, const player_t *pr, int pl, int dim)
{
    int base = KEY_BASE + ui.oct * 12, wi = 0;
    int32_t wx[27], wh = 28, bh = 17, ww = 5;
    uint16_t ink = dim ? K_DIM : trk_ink(trk);
    for (int k = 0; k < 27; k++)                 /* the white keys first, then the black on top */
        wx[k] = BLACK_KEY(base + k) ? x0 + wi * ww - 1 : x0 + (wi++) * ww;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < 27; k++) {
            int n = base + k, blk = BLACK_KEY(n), down = 0, snd = 0, ch = 0;
            if (blk != pass)
                continue;
            for (int i = 0; i < pr->nheld && i < 16; i++)
                down |= pr->held[i] == n;
            down |= n < 128 && (pr->phys[n >> 5] >> (n & 31) & 1u);
            for (int i = 0; i < pr->nsounding && i < 16; i++)
                snd |= pr->sounding[i] == n;
            snd |= pr->arp_note == n;
            for (int i = 0; i < pr->nchord && i < SN * 2; i++)
                ch |= pr->chord[i] == n;
            {
                uint16_t c = dim ? blk ? K_LINE : K_PANEL : snd ? K_TEXT : down ? ink : blk ? K_TEXT : K_CARD;
                if (blk)
                    cv_rect(wx[k], y0, 3, bh, c);
                else
                    cv_frame(wx[k], y0, ww, wh, 0, K_TEXT, c);
            }
            if (ch && pl == PL_STRUM)            /* the chord a key strums: a dot under each of its notes */
                cv_rect(wx[k] + (blk ? 0 : 1), blk ? y0 + bh + 2 : y0 + wh + 3, 3, 3, ink);
        }
    for (int i = 0; i < pr->nheld && i < 16; i++)    /* a held note off these keys (another octave): an arrow */
        if (pr->held[i] < base || pr->held[i] >= base + 27)
            cv_text(pr->held[i] < base ? x0 - 8 : x0 + 16 * ww + 2, y0 + 10, &FONT_XS, pr->held[i] < base ? "<" : ">", ink);
}
static void draw_players(void)
{
    int trk = sel_trk(), pl = pl_type(trk);
    const player_t *pr = &seq.player[trk < NTRK ? trk : 0];
    const uint8_t *pp = proj.trk[trk < NTRK ? trk : 0].par;
    uint32_t h = hash_u(hash_u(2166136261u, 0x9Cu), (uint32_t)trk | (uint32_t)pl << 4 | (uint32_t)pr->nheld << 8 |
                                          (uint32_t)pr->nchord << 12 | (uint32_t)pr->arp_note << 16 | (uint32_t)pr->chord[0] << 24);
    char t[64], *e;
    const int32_t KX = 12, KY = 48;
    for (int i = 0; i < 16; i++)
        h = hash_u(h, pr->held[i] | (uint32_t)(i < pr->nsounding ? pr->sounding[i] : 0) << 8);
    for (int i = 0; i < SN * 2; i++)
        h = hash_u(h, pr->chord[i]);
    for (int i = 0; i < 4; i++)
        h = hash_u(h, pr->phys[i]);
    h = hash_u(h, (uint32_t)ui.oct + 8u + ((uint32_t)fxm_touch() << 8));
    h = hash_u(h, pp[TP_P1] | (uint32_t)pp[TP_P2] << 8 | (uint32_t)pp[TP_P3] << 16 | (uint32_t)pp[TP_P4] << 24);
    if (!begin_main(h))
        return;
    if (is_drum(trk)) {
        fxm_title("no player", "KEYS ARE ITS SOUNDS", 0, 0, K_DIM);
        {   /* no player: a stool with nobody on it, the kit's keys (its sounds) beside */
            float sx = 52.0f, sy = 62.0f;
            ellipse_arc(sx, sy, 16.0f, 4.5f, 0.0f, 1.0f, 1.6f, K_TEXT);
            cv_linef(sx - 14.0f, sy + 2.0f, sx - 18.0f, sy + 28.0f, K_TEXT, 1.6f);
            cv_linef(sx + 14.0f, sy + 2.0f, sx + 18.0f, sy + 28.0f, K_TEXT, 1.6f);
            cv_linef(sx, sy + 4.5f, sx, sy + 28.0f, K_DIM, 1.2f);
            cv_linef(sx - 17.0f, sy + 19.0f, sx + 17.0f, sy + 19.0f, K_DIM, 1.0f);
            pl_keys(112, KY + 6, trk, pr, PL_OFF, 1);
        }
        cv_text(KX, 92, &FONT_XS, "no arp, no strum", K_DIM);
        cv_text(KX, 105, &FONT_XS, "WEAVE: last page", K_TEXT);
        cv_blit(0, MAIN_Y);
        return;
    }
    {   /* the title; the arp's and the strum's pattern beside it (callout 3), its octaves (4) */
        static const char *const DESC[NPL] = {"KEYS AS THEY ARE", "HOLD A CHORD", "ONE KEY, A CHORD", "NOTES STAY", "DICE ON THE POOL"};
        fxm_title(tp_info_par(TP_PLAYER, pp)->names[pl], DESC[pl], 0, 1, pl == PL_OFF ? K_DIM : K_TEXT);
        if (pl == PL_ARP || pl == PL_STRUM) {   /* the line under: the pattern (3), the arp's octaves (4) */
            char num[12], unit[6];
            int32_t x = 232;
            if (pl == PL_ARP) {
                s_cat(num_s((uint32_t)pp[TP_P3], t), pp[TP_P3] == 1 ? " octave" : " octaves");
                x -= text_w(F_XB, t);
                cv_text(x, 17, F_XB, t, K_TEXT);
                fxm_num(x - 10, 24, 4);
                x -= 24;
            }
            tp_text_par(TP_P2, pp[TP_P2], pp, num, unit);
            x -= text_w(F_XB, num);
            cv_text(x, 17, F_XB, num, K_TEXT);
            fxm_num(x - 10, 24, 3);
        }
    }
    pl_keys(KX, KY, trk, pr, pl, pl == PL_OFF && 0);
    e = s_cat(t, pl == PL_STRUM ? "chord: " : "held: ");
    if (pl == PL_STRUM) {
        e = chord_name(pr->chord, pr->nchord < SN * 2 ? pr->nchord : SN * 2, e);
        for (int i = 0; i < pr->nchord && i < SN * 2 && text_w(&FONT_XS, t) < 60; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(pr->chord[i], e);
        }
    } else {
        for (int i = 0; i < pr->nheld && i < 8 && text_w(&FONT_XS, t) < 66; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(pr->held[i], e);
        }
        if (!pr->nheld)
            e = s_cat(e, "-");
    }
    if (pl == PL_ARP || pl == PL_HOLD ? !pr->nheld : pl == PL_STRUM && !pr->nchord)   /* nothing yet: what to do */
        text_fit(KX, 84, &FONT_XS, pl == PL_HOLD ? "play keys to hold" : pl == PL_ARP ? "hold a chord" : "learn a chord", 88, K_DIM);
    else
        text_fit(KX, 84, &FONT_XS, t, 88, K_TEXT);
    if (pl == PL_OFF) {   /* the keys carried straight across */
        cv_linef(104.0f, 62.0f, 122.0f, 62.0f, K_TEXT, 1.4f);
        arrow_head(126.0f, 62.0f, 1.0f, 0.0f, K_TEXT);
        cv_text(134, 48, &FONT_XS, "the same notes", K_DIM);
        cv_text(134, 62, &FONT_XS, "sound, as played", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    cv_linef(104.0f, 62.0f, 114.0f, 62.0f, K_TEXT, 1.4f);
    arrow_head(118.0f, 62.0f, 1.0f, 0.0f, K_TEXT);
    {   /* the figure: notes by pitch rank, the staircase (arp), the stagger (strum), the long bars (hold), the dice (random) */
        int32_t sx = 126, base = 100, ntop = 58;
        uint8_t nt[24], ord[24];
        int nn = 0, n = 0, i, j, steps = 0, nu = 0, cur = -1;
        uint8_t uniq[24];
        const player_t *q = pr;
        if (pl == PL_STRUM)
            for (i = 0; i < q->nchord && i < SN * 2; i++)
                nt[nn++] = q->chord[i];
        else
            for (i = 0; i < q->nheld && i < 8; i++)
                nt[nn++] = q->held[i];
        if (!nn && pl != PL_RAND) {              /* nothing held: a ghost (C E G) and the words */
            nt[0] = 60, nt[1] = 64, nt[2] = 67, nn = 3;
            n = -1;
        }
        if (pl == PL_ARP && pp[TP_P2] != 4)
            for (i = 1; i < nn; i++)
                for (j = i; j > 0 && nt[j - 1] > nt[j]; j--) {
                    uint8_t v = nt[j];
                    nt[j] = nt[j - 1], nt[j - 1] = v;
                }
        if (pl == PL_ARP) {
            int range = clampi(pp[TP_P3], 1, 4), total = nn * range, pat = pp[TP_P2], cyc;
            cyc = pat == 2 ? (total > 1 ? 2 * total - 2 : 1) : pat == 3 ? 2 * total : total * (pat == 5 ? 1 : 1);
            steps = cyc < 12 ? cyc : 12;
            for (i = 0; i < steps; i++) {
                int k = i % (cyc ? cyc : 1), idx = pat == 1 ? total - 1 - k : pat == 2 ? (k < total ? k : 2 * total - 2 - k)
                          : pat == 3 ? (k < total ? k : 2 * total - 1 - k) : pat == 5 ? (int)((uint32_t)(k * 2654435761u) >> 16) % total : k;
                ord[i] = (uint8_t)(nt[idx % nn] + 12 * (idx / nn));
            }
        } else if (pl == PL_STRUM) {
            int down = pp[TP_P2] == 1;
            steps = nn;
            for (i = 0; i < steps; i++)
                ord[i] = nt[down ? nn - 1 - i : i];
            for (i = 1; i < steps && !down && pp[TP_P2] != 0; i++)
                ;
        } else if (pl == PL_HOLD) {
            steps = nn;
            for (i = 0; i < steps; i++)
                ord[i] = nt[i];
        } else
            steps = 0;
        for (i = 0; i < steps; i++) {            /* the distinct pitches, sorted: the ranks */
            for (j = 0; j < nu && uniq[j] != ord[i]; j++)
                ;
            if (j == nu)
                uniq[nu++] = ord[i];
        }
        for (i = 1; i < nu; i++)
            for (j = i; j > 0 && uniq[j - 1] > uniq[j]; j--) {
                uint8_t v = uniq[j];
                uniq[j] = uniq[j - 1], uniq[j - 1] = v;
            }
        if (pl == PL_ARP && pr->arp_note)
            for (i = 0; i < steps && cur < 0; i++)
                if (ord[i] == pr->arp_note)
                    cur = i;
        if (pl == PL_ARP) {
            int32_t sw = steps ? (228 - sx) / steps : 21, bw, rs = nu > 1 ? (base - ntop - 10) / (nu - 1) : 0;
            if (sw > 21)
                sw = 21;
            if (rs > 13)
                rs = 13;
            bw = sw * 3 / 4;
            for (i = 0; i < steps; i++) {
                int rk = 0;
                int32_t x = sx + i * sw, y;
                for (j = 0; j < nu; j++)
                    if (uniq[j] == ord[i])
                        rk = j;
                y = base - 10 - rk * rs;
                cv_frame(x, y, bw, 10, 2, n < 0 ? K_LINE : K_TEXT, i == cur ? K_REC : n < 0 ? K_BG : K_TINT);
                if (i + 1 < steps) {
                    int rk2 = 0;
                    for (j = 0; j < nu; j++)
                        if (uniq[j] == ord[i + 1])
                            rk2 = j;
                    dot_line((float)(x + bw), (float)y + 5.0f, (float)(x + sw), (float)(base - 10 - rk2 * rs) + 5.0f, 3.0f, K_DIM);
                }
                if (sw >= 15) {
                    char nm[6];
                    note_name(ord[i], nm);
                    nm[1 + (nm[1] == '#' || nm[1] == 'b')] = 0;
                    text_c(x + bw / 2, base + 3, &FONT_XS, nm, n < 0 ? K_FAINT : K_DIM);
                }
            }
            cv_rect(sx, base, 228 - sx, 1, K_TEXT);
            {   /* 2: the rate, one step dimensioned; 4: the octave (a dotted line where the keys' set repeats an octave up) */
                char num[12], unit[6];
                dim_line((float)sx, 46.0f, (float)(sx + sw), 46.0f, 3.0f, 3.0f, K_TEXT);
                fxm_num(sx + sw + 8, 46, 2);
                tp_text_par(TP_P1, pp[TP_P1], pp, num, unit);
                s_cat(s_cat(t, "rate "), num);
                cv_text(sx + sw + 17, 39, &FONT_XS, t, K_TEXT);
                if (pp[TP_P3] > 1 && nn < steps && n >= 0) {
                    int32_t ox = sx + nn * sw - 2;
                    dot_line((float)ox, (float)base - 3.0f, (float)ox, 56.0f, 3.0f, K_DIM);
                }
            }
        } else if (pl == PL_STRUM || pl == PL_HOLD) {
            int32_t dxr = pl == PL_STRUM ? 4 + (int32_t)pp[TP_P1] * 12 / 63 : 0, bw = pl == PL_STRUM ? 70 : 90;
            for (i = 0; i < steps; i++) {
                int rk = 0;
                int32_t y;
                for (j = 0; j < nu; j++)
                    if (uniq[j] == ord[i])
                        rk = j;
                y = base - 12 - rk * (nu > 1 ? (base - ntop - 14) / (nu - 1) > 14 ? 14 : (base - ntop - 14) / (nu - 1) : 0);
                if (pl == PL_STRUM) {
                    cv_frame(sx + i * dxr, y, bw, 9, 2, n < 0 ? K_LINE : K_TEXT, n < 0 ? K_BG : K_TINT);
                    cv_rect(sx + i * dxr - 1, y - 2, 2, 13, K_TEXT);
                } else {
                    cv_frame(sx, y, bw, 9, 2, n < 0 ? K_LINE : K_TEXT, n < 0 ? K_BG : K_TINT);
                    arrow_head((float)(sx + bw + 8), (float)y + 4.5f, 1.0f, 0.0f, K_TEXT);
                    cv_linef((float)(sx + bw), (float)y + 4.5f, (float)(sx + bw + 4), (float)y + 4.5f, K_TEXT, 1.0f);
                }
            }
            cv_rect(sx - 4, base, 106 + (pl == PL_STRUM ? steps * dxr : 0), 1, K_TEXT);
            if (pl == PL_STRUM) {   /* 2: the roll: the stagger between two notes */
                char num[12], unit[6];
                dim_line((float)sx, 46.0f, (float)(sx + dxr), 46.0f, 3.0f, 3.0f, K_TEXT);
                fxm_num(sx + dxr + 8, 46, 2);
                tp_text_par(TP_P1, pp[TP_P1], pp, num, unit);
                s_cat(s_cat(t, "roll "), num);
                cv_text(sx + dxr + 17, 39, &FONT_XS, t, K_TEXT);
            } else
                cv_text(sx, base + 3, &FONT_XS, "until the next change", K_DIM);
        } else {   /* random: 16 slots, a note where the dice give one (note %), its pitch among the octaves (4), time and rhythm */
            int32_t gx = sx - 4, gw = 108, rows = 6;
            int pct = clampi(pp[TP_P1] * 100 / 127, 0, 100), span = clampi(pp[TP_P4], 0, 3) + 1, sl;
            cv_rect(gx, base, gw, 1, K_TEXT);
            for (sl = 0; sl < 16; sl++) {
                uint32_t r = (uint32_t)(sl + 1) * 2654435761u;
                int on = (int)((r >> 8) % 100u) < pct, row = (int)((r >> 20) % (uint32_t)(rows * span > 12 ? 12 : rows * span));
                float x = (float)gx + 3.0f + (float)sl * 6.5f, y = (float)base - 6.0f - (float)row * 3.6f;
                if (on) {
                    cv_disc(x, y, 2.6f, K_NOW);
                    cv_ring(x, y, 2.8f, 1.0f, K_TEXT);
                } else
                    cv_disc(x, (float)base - 3.0f, 1.0f, K_LINE);
            }
            fxm_num(gx + gw - 8, 46, 2);
            s_cat(num_s((uint32_t)pct, t), "% of the slots");
            cv_text(gx + 4, 39, &FONT_XS, t, K_TEXT);
        }
    }
    cv_blit(0, MAIN_Y);
}
/* the pitch classes a random track may use at present (the pool picker's dots): its follow mode's notes */
static int rand_in_scale(int t, int pc)
{
    int mode = proj.trk[t].par[TP_BRAIN] % NBM;
    if (!((seq.brain_route >> t) & 1u) || mode == BM_NO)
        return 0;
    if (mode >= BM_HARMONY && seq.brain_chord) {
        for (int i = 0; i < SN; i++) {
            int n = (int)(seq.brain_chord >> (8 * i)) & 0xFF;
            if (n && n % 12 == pc)
                return 1;
        }
        return 0;
    }
    return (BRAIN_SCALE[seq.brain_scale % NBS] >> (((pc - seq.brain_key) % 12 + 12) % 12)) & 1u;
}
/* the random pool picker (chord mode on a PL_RAND track): the keys toggle pitch classes of the current
 * pattern's pool; a dot under a key marks the notes the track's follow mode allows */
static void draw_step_pool(void)
{
    int trk = sel_trk(), pat = ui.chord_pat % NPATT;
    uint16_t pool = lane_pool_get(pats.lane, trk, pat);
    uint32_t h = hash_u(hash_u(hash_u(2166136261u, 0xC4u),
                               (uint32_t)trk | (uint32_t)pat << 4 | (uint32_t)ui.oct << 12), pool);
    char t[48], *e;
    for (int i = 0; i < NKEYS; i++)
        h = hash_u(h, ui.kmode[i] == KM_STEPCHORD ? (uint32_t)ui.knote[i] + 1u : 0u);
    h = hash_u(hash_u(h, seq.brain_chord), (uint32_t)seq.brain_key | (uint32_t)seq.brain_scale << 8 |
                                                  (uint32_t)seq.brain_route << 16);
    if (!begin_main(h))
        return;
    num_s((uint32_t)pat + 1u, s_cat(t, "POOL P"));
    tr_text(8, 4, F_LAB, t, K_SEL, 1);
    text_r(232, 1, &FONT_XS, "SEQ: done", K_DIM);
    e = s_cat(t, "notes: ");
    if (!pool)
        e = s_cat(e, "all");
    else {
        int any = 0;
        for (int i = 0; i < 12; i++)
            if (pool >> i & 1u) {
                if (any)
                    e = s_cat(e, " ");
                e = note_name(60 + i, e);
                any = 1;
            }
    }
    text_fit(8, 20, F_XB, t, 224, K_TEXT);
    {
        int base = KEY_BASE + ui.oct * 12, wi = 0;
        int32_t wx[27], y0 = 44, wh = 52, bh = 32, ww = 13;
        uint16_t ink = trk_ink(trk);
        for (int k = 0; k < 27; k++)
            wx[k] = BLACK_KEY(base + k) ? 6 + wi * ww + 2 - 4 : 6 + (wi++) * ww + 2;
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < 27; k++) {
                int n = base + k, blk = BLACK_KEY(n), on = 0, down = 0, sc = rand_in_scale(trk, n % 12);
                if (blk != pass)
                    continue;
                on = (pool >> (n % 12)) & 1u;
                for (int i = 0; i < NKEYS; i++)
                    down |= ui.kmode[i] == KM_STEPCHORD && ui.knote[i] == n;
                {
                    uint16_t c = down ? K_REC : on ? ink : blk ? K_TEXT : K_CARD;
                    if (blk) {
                        cv_rect(wx[k] - 1, y0, 11, bh + 1, K_BG);
                        cv_rect(wx[k], y0, 9, bh, c);
                    } else {
                        cv_rect(wx[k] - 1, y0, ww + 1, wh, K_TEXT);
                        cv_rect(wx[k], y0 + 1, ww - 1, wh - 2, c);
                    }
                    if (sc)                           /* a note the follow mode allows: a dot under its key */
                        cv_rect(wx[k] + (blk ? 2 : 3), y0 + wh + 3, 5, 3, K_NOW);
                }
            }
    }
    cv_blit(0, MAIN_Y);
}
/* chord mode (a step held, SEQ tapped), on Steps: the keys are a keyboard (Players') writing the held step's chord
 * instead of playing it. The step named in blue (a held step's), its own notes lit in the track's ink, a key still down
 * vermilion, so the chord being built is always legible, not "in limbo" */
static void draw_step_chord(void)
{
    int trk = sel_trk(), first = 0;
    const step_t *st;
    uint32_t h;
    int s;
    for (int i = 0; i < 16; i++)
        if (ui.chord_held >> i & 1u) {
            first = i;
            break;
        }
    s = ui.chord_bar * 16 + first;
    st = chord_step(trk, first);
    h = hash_u(hash_u(2166136261u, 0xC2u), (uint32_t)trk | (uint32_t)s << 5 | (uint32_t)ui.oct << 13);
    char t[48], *e;
    uint8_t nt[SN];
    int any = 0;
    for (int i = 0; i < SN; i++)
        h = hash_u(h, st->note[i]);
    for (int i = 0; i < NKEYS; i++)
        h = hash_u(h, ui.kmode[i] == KM_STEPCHORD ? (uint32_t)ui.knote[i] + 1u : 0u);
    if (!begin_main(h))
        return;
    num_s((uint32_t)s + 1u, s_cat(t, "STEP "));
    tr_text(8, 4, F_LAB, t, K_SEL, 1);
    text_r(232, 1, &FONT_XS, "SEQ: done", K_DIM);
    for (int i = 0; i < SN; i++)
        if (st->note[i])
            nt[any++] = st->note[i];
    e = chord_name(nt, any, s_cat(t, "chord: "));
    any = 0;
    for (int i = 0; i < SN; i++)
        if (st->note[i]) {
            if (any)
                e = s_cat(e, " ");
            e = note_name(st->note[i], e);
            any = 1;
        }
    if (!any)
        e = s_cat(e, "-");
    text_fit(8, 20, F_XB, t, 224, K_TEXT);
    {
        int base = KEY_BASE + ui.oct * 12, wi = 0;
        int32_t wx[27], y0 = 44, wh = 52, bh = 32, ww = 13;
        uint16_t ink = trk_ink(trk);
        for (int k = 0; k < 27; k++)
            wx[k] = BLACK_KEY(base + k) ? 6 + wi * ww + 2 - 4 : 6 + (wi++) * ww + 2;
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < 27; k++) {
                int n = base + k, blk = BLACK_KEY(n), on = 0, down = 0;
                if (blk != pass)
                    continue;
                for (int i = 0; i < SN; i++)
                    on |= st->note[i] == n;
                for (int i = 0; i < NKEYS; i++)
                    down |= ui.kmode[i] == KM_STEPCHORD && ui.knote[i] == n;
                {
                    uint16_t c = down ? K_REC : on ? ink : blk ? K_TEXT : K_CARD;
                    if (blk) {
                        cv_rect(wx[k] - 1, y0, 11, bh + 1, K_BG);
                        cv_rect(wx[k], y0, 9, bh, c);
                    } else {
                        cv_rect(wx[k] - 1, y0, ww + 1, wh, K_TEXT);
                        cv_rect(wx[k], y0 + 1, ww - 1, wh - 2, c);
                    }
                }
            }
    }
    cv_blit(0, MAIN_Y);
}
/* the Mix page (the sheet's "Mix"): a small mixing desk. Each track a fader slot with its ticks and a cap at its level
 * (0 dB near the top, -36 at the foot), beside it a live meter in the track's ink (after the fader, eng_peak; it falls
 * 24 dB a second, its peak holds a second); the effects' returns a group of their own at the right; the scale's
 * hairlines across. The selected track: its cap blue, its number blue and underlined. Muted: the strip grey, an "M"
 * key-cap under it; soloed: an inked "S"; the others a solo silences: grey. The conductor and the throws make no sound:
 * no strip */
#define MIX_TOP 16
#define MIX_H 68
#define MIX_DB_HI 6.0f
#define MIX_DB_LO (-36.0f)
static int32_t mix_y(float db)                   /* a level in dB: its height in the slot, 0 .. MIX_H */
{
    db = db > MIX_DB_HI ? MIX_DB_HI : db < MIX_DB_LO ? MIX_DB_LO : db;
    return (int32_t)((db - MIX_DB_LO) * (float)MIX_H / (MIX_DB_HI - MIX_DB_LO) + 0.5f);
}
static float lin_db(float g) { return g > 1e-6f ? fm_log2f(g) * 6.0206f : -120.0f; }
static int lev_now(int t)                             /* a track's level now (a lock's, a motion's) */
{
    int lv = engine_live(t, TP_LEVEL);
    return lv >= 0 ? lv : proj.trk[t].par[TP_LEVEL];
}
static void draw_mix(void)
{
    static float m_db[NALLTRK], m_hold[NALLTRK];
    static uint32_t m_hold_ms[NALLTRK], m_last;
    static const char *const SHORT_NM[NALLTRK] = {"1", "2", "3", "4", "5", "6", "7", "8", "CO", "TH", "F1", "F2", "WV"};
    static const int8_t COL[NALLTRK] = {0, 1, 2, 3, 4, 5, 6, 7, -1, -1, 8, 9, -1};   /* the strips' places (the conductor and the
                                                                              * throws make no sound: none) */
    uint32_t now = plat_ms(), dt = now - m_last;
    int sel = sel_trk();
    uint32_t h = hash_u(hash_u(hash_u(2166136261u, 0x3Du), (uint32_t)sel | (uint32_t)seq.mute << 4), ui.solo | (uint32_t)ui.solo_lat << 16);
    float fall = (float)(dt > 1000u ? 1000u : dt) * 0.024f;
    m_last = now;
    for (int t = 0; t < NALLTRK; t++) {          /* the engine's loudest since the last look, then the fall */
        float db = lin_db((float)ui_peak[t] * (1.0f / 16383.5f));   /* (meters_take's) */
        if (t < NTRK && ui_ghost[t] && lev_now(t))   /* a muted track's steps: a meter in grey all the same (at its
                                                      * fader, less for a softer step), an empty track's none */
            db = fm_maxf(db, lin_db(tp_level_gain(lev_now(t)) * (float)ui_ghost[t] * (1.0f / 127.0f)) - 6.0f);
        if (dt > 250u)                           /* (back on the page: what piled up meanwhile is old) */
            m_db[t] = m_hold[t] = MIX_DB_LO - 1.0f;
        m_db[t] = fm_maxf(db, m_db[t] - fall);
        if (db >= m_hold[t]) {
            m_hold[t] = db;
            m_hold_ms[t] = now;
        } else if (now - m_hold_ms[t] > 1000u) {
            m_hold[t] -= fall;
        }
        h = hash_u(h, (uint32_t)(m_db[t] > MIX_DB_LO ? mix_y(m_db[t]) : 0) | (uint32_t)(m_hold[t] > MIX_DB_LO ? mix_y(m_hold[t]) : 0) << 8 |
                      (uint32_t)lev_now(t) << 16);
    }
    if (!begin_main(h))
        return;
    int32_t y0 = MIX_TOP + MIX_H;                /* the slots' foot */
    static const int8_t MARK[3] = {0, -12, -24};
    static const char *const MARK_NM[3] = {"0", "-12", "-24"};
    for (int i = 0; i < 3; i++) {                /* the scale: hairlines across, the dB at the right */
        int32_t y = y0 - mix_y((float)MARK[i]);
        cv_rect(8, y, 202, 1, K_PANEL2);
        text_r(234, cap_y(y - 6, 13, F_LAB), F_LAB, MARK_NM[i], K_DIM);
    }
    cv_rect(176, MIX_TOP - 4, 1, MIX_H + 30, K_LINE);   /* the returns' group: a rule, its name */
    tr_text(181, 0, F_LAB, "RETURNS", K_DIM, 1);
    for (int t = 0; t < NALLTRK; t++) {
        if (COL[t] < 0)
            continue;
        int32_t x = 16 + COL[t] * 19 + (COL[t] >= 8 ? 21 : 0);   /* the slot's line */
        int muted = t < NSEQT && (seq.mute >> t & 1u), solo = ui.solo >> t & 1u;
        int quiet = muted && !solo;
        int marked = muted && !ui.solo;          /* (under a solo the others go grey, unmarked) */
        uint16_t ink = quiet ? K_LINE : K_TEXT;
        int32_t lv = m_db[t] > MIX_DB_LO ? mix_y(m_db[t]) : 0, pk = m_hold[t] > MIX_DB_LO ? mix_y(m_hold[t]) : 0;
        int v = lev_now(t);
        int32_t fy = y0 - (v ? mix_y(lin_db(tp_level_gain(v))) : 0);
        cv_rect(x - 1, MIX_TOP, 2, MIX_H + 1, ink);  /* the slot */
        for (int k = 0; k <= 6; k++)             /* its ticks, at the left */
            cv_rect(x - 6, MIX_TOP + k * MIX_H / 6, k % 3 ? 2 : 3, 1, quiet ? K_LINE : K_DIM);
        cv_rect(x + 4, MIX_TOP, 4, MIX_H + 1, K_PANEL);   /* the meter's well: lit in segments from the foot */
        for (int32_t sy = 0; sy + 2 <= lv; sy += 3)
            cv_rect(x + 4, y0 - sy - 1, 4, 2, quiet ? K_FAINT : sy > mix_y(0.0f) ? K_TEXT : trk_ink(t));
        if (pk > 3)                                  /* the peak: a heavier tick that hangs a second */
            cv_rect(x + 3, y0 - pk - 1, 6, 2, quiet ? K_FAINT : K_TEXT);
        {                                        /* the fader's cap: a raised block with a grip, its line the level */
            int32_t cy = fy < MIX_TOP + 6 ? MIX_TOP + 6 : fy > y0 - 5 ? y0 - 5 : fy;
            uint16_t face = t == sel ? K_SEL : quiet ? K_BG : K_CARD, edge = t == sel ? K_SEL : quiet ? K_LINE : K_TEXT;
            uint16_t grip = t == sel ? K_BG : quiet ? K_LINE : K_DIM;
            cv_rect(x - 6, cy - 4, 14, 11, quiet ? K_BG : K_PANEL2);   /* (its shadow, a pixel down and right) */
            cv_rect(x - 7, cy - 5, 14, 11, edge);
            cv_rect(x - 6, cy - 4, 12, 9, face);
            cv_rect(x - 5, cy - 3, 10, 1, grip), cv_rect(x - 5, cy + 2, 10, 1, grip);
            cv_rect(x - 6, cy, 12, 1, t == sel ? K_BG : quiet ? K_LINE : K_TEXT);
        }
        text_c(x + 1, MIX_TOP + MIX_H + 4, F_XB, SHORT_NM[t], t == sel ? K_SEL : quiet ? K_LINE : K_TEXT);
        if (t == sel)
            cv_rect(x - 6, MIX_TOP + MIX_H + 18, 13, 2, K_SEL);
        if (solo || marked)                      /* soloed: an inked S; muted: an M */
            key_cap(x - 5, MIX_TOP + MIX_H + 21, solo ? "S" : "M", solo);
    }
    if (ui.solo_lat)                             /* a solo that stays (two taps) */
        tr_text(8, 0, F_LAB, "SOLO STAYS", K_TEXT, 1);
    cv_blit(0, MAIN_Y);
}
/* Master color: the colour stage drawn as what it is. Tape: two reels and the tape between them over the head, the
 * tape wavering as much as the amount's wow (the reels turn while it plays); sampler: a sine and what the sampler
 * keeps of it, held in steps as coarse as the amount; off: a plain wire. Callouts 2-4 at the left; the tone a
 * roll-off's corner under the figure, the mix the share of the wet line */
static void draw_master_color(void)
{
    static const char *const ABOUT[NMC] = {"THE MIX AS IT IS", "WARMTH AND WOW", "AN OLD SAMPLER'S GRAIN"};
    const uint8_t *m = proj.master;
    int ty = m[MS_CTYPE] % NMC;
    float amt = (float)m[MS_CAMT] / 127.0f, tone = (float)m[MS_CTONE] / 127.0f, mix = (float)m[MS_CMIX] / 127.0f;
    uint32_t h = hash_u(hash_u(2166136261u, 0xC1u), (uint32_t)ty | (uint32_t)m[MS_CAMT] << 8 | (uint32_t)m[MS_CTONE] << 16 |
                                                    (uint32_t)m[MS_CMIX] << 24);
    h = hash_u(h, (uint32_t)p2_touch() << 28);
    if (!begin_main(h))
        return;
    p2_title(MC_NAME[ty], ABOUT[ty], 120);
    if (ty == MC_OFF) {
        cv_text(9, 54, &FONT_XS, "on the whole mix: nothing", K_DIM);
        cv_text(9, 70, &FONT_XS, "KNOB 1: tape or sampler", K_DIM);
        cv_rect(140, 52, 84, 2, K_FAINT);
        cv_disc(140.0f, 52.5f, 3.0f, K_TEXT);
        cv_disc(224.0f, 52.5f, 3.0f, K_TEXT);
        cv_blit(0, MAIN_Y);
        return;
    }
    p2_callout(13, 62, 2, "amount", K_TEXT);
    p2_callout(13, 78, 3, "tone", K_TEXT);
    p2_callout(13, 94, 4, "mix", K_TEXT);
    if (ty == MC_TAPE) {
        float rx[2] = {150.0f, 210.0f}, ry = 30.0f, a0 = 0.08f;
        pen_t pn = {0};
        for (int r = 0; r < 2; r++) {                /* the reels, their spokes */
            cv_ring(rx[r], ry, 17.0f, 1.2f, K_TEXT);
            cv_ring(rx[r], ry, 11.0f + 4.0f * (r ? 1.0f - mix : mix), 0.8f, K_DIM);   /* (the tape wound on) */
            cv_disc(rx[r], ry, 3.0f, K_TEXT);
            for (int k = 0; k < 3; k++) {
                float a = a0 + (float)k / 3.0f;
                cv_linef(rx[r] + 4.0f * fm_sin_turns(a + 0.25f), ry + 4.0f * fm_sin_turns(a), rx[r] + 9.0f * fm_sin_turns(a + 0.25f),
                         ry + 9.0f * fm_sin_turns(a), K_TEXT, 1.2f);
            }
        }
        cv_round(173, 70, 14, 9, 2, K_TEXT);         /* the head */
        for (int i = 0; i <= 48; i++) {              /* the tape: reel to head to reel, wavering by the amount */
            float u = (float)i / 48.0f, x, y, wob = amt * 2.5f * fm_sin_turns(u * 6.0f);
            if (u < 0.5f)
                x = rx[0] - 17.0f + (180.0f - rx[0] + 17.0f) * u * 2.0f, y = ry + (69.0f - ry) * u * 2.0f;
            else
                x = 180.0f + (rx[1] + 17.0f - 180.0f) * (u - 0.5f) * 2.0f, y = 69.0f - (69.0f - ry) * (u - 0.5f) * 2.0f;
            pen_to(&pn, x, y + wob, K_TEXT);
        }
        {   /* the amount's leader to the tape */
            cv_dash(64.0f, 62.0f, 156.0f, 58.0f, 2.0f, 2.0f, 0.8f, K_TEXT);
        }
    } else {                                         /* the sampler: a sine and its held steps */
        int steps = 6 + (int)((1.0f - amt) * 18.0f), lv = 3 + (int)((1.0f - amt) * 10.0f);
        int32_t x0 = 136, w = 92, cy = 40, a = 24;
        pen_t pn = {0}, pq = {0};
        cv_grid(x0, cy - a - 2, w, 2 * a + 5, 6, MAIN_Y, K_GRID);
        for (int32_t x = 0; x <= w; x++)
            pen_to(&pn, (float)(x0 + x), (float)cy - (float)a * fm_sin_turns((float)x / (float)w), K_FAINT);
        for (int k = 0; k < steps; k++) {            /* held: each step the sine at its start, rounded to the levels */
            float v = fm_sin_turns((float)k / (float)steps), q = fm_floorf(v * (float)lv + 0.5f) / (float)lv;
            float xa = (float)x0 + (float)w * (float)k / (float)steps, xb = (float)x0 + (float)w * (float)(k + 1) / (float)steps;
            pen_to(&pq, xa, (float)cy - (float)a * q, K_TEXT);
            pen_to(&pq, xb, (float)cy - (float)a * q, K_TEXT);
        }
        cv_dash(66.0f, 62.0f, (float)(x0 + w / 4), (float)(cy + 2), 2.0f, 2.0f, 0.8f, K_TEXT);
    }
    {   /* the tone: a roll-off from its corner (further right, brighter), dimensioned under the figure */
        float xc = 140.0f + 70.0f * tone;
        pen_t pt = {0};
        for (int i = 0; i <= 40; i++) {
            float x = 136.0f + (float)i * 92.0f / 40.0f, d = x > xc ? (x - xc) * 0.5f : 0.0f;
            pen_to(&pt, x, 90.0f + fm_minf(d, 14.0f), K_TEXT);
        }
        cv_dash(52.0f, 78.0f, xc - 3.0f, 89.0f, 2.0f, 2.0f, 0.8f, K_TEXT);
        cv_rect((int32_t)xc, 86, 1, 8, K_DIM);
    }
    {   /* the mix: the wet share as a ruled bar */
        cv_rect(136, 110, 92, 3, K_PANEL2);
        cv_rect(136, 110, (int32_t)(92.0f * mix), 3, K_TEXT);
        cv_dash(44.0f, 94.0f, 132.0f, 111.0f, 2.0f, 2.0f, 0.8f, K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}
/* Master, EQ: the response over 20 Hz .. 20 kHz on graph paper (+-12 dB), the bands' callouts on the curve at their
 * frequencies (120 Hz shelf, 1 kHz peak, 6 kHz shelf), the area off flat hatched */
static float mst_eq_db(float oct, const uint8_t *m)  /* oct: octaves over 20 Hz */
{
    float lo = ((float)m[MS_EQ_LO] - 64.0f) * (12.0f / 64.0f), mid = ((float)m[MS_EQ_MID] - 64.0f) * (12.0f / 64.0f);
    float hi = ((float)m[MS_EQ_HI] - 64.0f) * (12.0f / 64.0f), d = (oct - 5.64f) / 1.3f;
    float sl = 1.0f / (1.0f + fm_expf((oct - 2.58f) * 3.0f)), sh = 1.0f / (1.0f + fm_expf(-(oct - 8.23f) * 3.0f));
    return lo * sl + mid * fm_expf(-d * d) + hi * sh;
}
/* Master, out: the two groups' levels (1, 2) as bars, the compressor's transfer curve (3: its threshold and ratio)
 * with its gain reduction now hanging at the graph's right in vermilion, and the pump's duck now (4) beside it */
static void draw_master(void)
{
    int pg = ui.page == P_MASTER ? 0 : 2;
    const uint8_t *m = proj.master;
    uint32_t h = hash_u(hash_u(2166136261u, 0x3Bu), (uint32_t)pg);
    float gr = pg ? master_gr_db(MST) : 0.0f, pump = pg ? eng_pump_db : 0.0f;
    char t[32];
    for (int i = 0; i < NMS; i++)
        h = hash_u(h, m[i]);
    h = hash_u(h, (uint32_t)(gr * 4.0f) | (uint32_t)(pump * 4.0f) << 12 | (uint32_t)p2_touch() << 28);
    if (!begin_main(h))
        return;
    if (pg == 0) {
        int32_t x0 = 12, w = 192, y0 = 6, hh = 72, cy = y0 + hh / 2;
        float poly[2 * 52], ppd = 2.5f;              /* pixels a dB */
        pen_t pn = {0};
        int n = 0;
        static const float BF[3] = {2.58f, 5.64f, 8.23f};
        static const char *const BN[3] = {"120", "1K", "6K"};
        cv_grid(x0, y0, w + 1, hh + 1, 8, MAIN_Y, K_GRID);
        cv_rect(x0, cy, w + 1, 1, K_TEXT);
        for (int i = 0; i < 3; i++) {                /* the scale: +12, 0, -12 dB */
            static const char *const DB[3] = {"+12", "0", "-12"};
            int32_t y = cy - (int32_t)(ppd * 12.0f) * (1 - i);
            cv_rect(x0 + w + 2, y, 4, 1, K_DIM);
            text_r(234, cap_y(y - 6, 13, F_LAB), F_LAB, DB[i], K_DIM);
        }
        for (int i = 0; i <= 48; i++) {
            float oct = 10.0f * (float)i / 48.0f, y = (float)cy - mst_eq_db(oct, m) * ppd;
            poly[2 * n] = (float)x0 + (float)w * (float)i / 48.0f, poly[2 * n + 1] = y, n++;
        }
        poly[2 * n] = (float)(x0 + w), poly[2 * n + 1] = (float)cy, n++;
        poly[2 * n] = (float)x0, poly[2 * n + 1] = (float)cy, n++;
        cv_poly(poly, n, K_DIM, 150u, 5);            /* off flat: hatched */
        pn.w = 2.0f;
        for (int i = 0; i < n - 2; i++)
            pen_to(&pn, poly[2 * i], poly[2 * i + 1], K_TEXT);
        for (int b = 0; b < 3; b++) {                /* the bands: a callout on the curve, its frequency under the graph */
            float db = mst_eq_db(BF[b], m);
            int32_t x = x0 + (int32_t)((float)w * BF[b] / 10.0f), y = cy - (int32_t)(db * ppd);
            int32_t c = db >= 0.0f ? y - 12 : y + 12;
            char d[2] = {(char)('1' + b), 0};
            cv_disc((float)x, (float)y, 2.4f, K_TEXT);
            cv_linef((float)x, (float)y, (float)x, (float)(db >= 0.0f ? c + 6 : c - 6), K_TEXT, 0.8f);
            circ_num(x, c < y0 + 6 ? y0 + 6 : c > y0 + hh - 6 ? y0 + hh - 6 : c, d, K_TEXT, K_BG);
            text_c(x + 1, y0 + hh + 4, F_LAB, BN[b], K_DIM);
        }
        tr_text(x0, y0 + hh + 4, F_LAB, "HZ", K_DIM, 1);
        text_r(x0 + w, y0 + hh + 21, &FONT_XS, "64 on a knob: flat", K_DIM);
    } else {
        int32_t gx = 140, gy = 16, g = 80;          /* the transfer graph: in -48 .. 0 dB across, out up */
        int v = m[MS_COMP], ri = v ? 1 + (int)((float)v * (4.0f / 127.0f) + 0.5f) : 0;
        static const float R[6] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
        float thr = -4.0f - 28.0f * (float)v / 127.0f, sc = (float)g / 48.0f;
        pen_t pn = {0};
        for (int i = 0; i < 2; i++) {                /* drums, melodic: their levels */
            int32_t y = 10 + i * 18, bw = m[MS_PERC + i] * 52 / 127;
            p2_callout(13, y, 1 + i, i ? "melodic" : "drums", K_TEXT);
            cv_rect(76, y - 1, 52, 3, K_PANEL2);
            cv_rect(76, y - 1, bw, 3, K_TEXT);
        }
        p2_callout(13, 56, 3, "comp", K_TEXT);
        if (ri)
            s_cat(num_i((int32_t)thr, s_cat(t, ri == 1 ? "1.5:1 from " : ri == 2 ? "2:1 from " : ri == 3 ? "3:1 from " :
                                                     ri == 4 ? "4:1 from " : "6:1 from ")), " dB");
        else
            s_cat(t, "off");
        cv_text(13, 66, &FONT_XS, t, ri ? K_TEXT : K_DIM);
        p2_callout(13, 92, 4, "pump", K_TEXT);
        if (m[MS_PUMP])
            s_cat(num_s((uint32_t)(24 * m[MS_PUMP] / 127), s_cat(t, "ducks ")), " dB");
        else
            s_cat(t, "off");
        cv_text(13, 102, &FONT_XS, t, m[MS_PUMP] ? K_TEXT : K_DIM);
        cv_grid(gx, gy, g + 1, g + 1, 8, MAIN_Y, K_GRID);
        cv_rect(gx, gy, g + 1, 1, K_TEXT), cv_rect(gx, gy + g, g + 1, 1, K_TEXT);
        cv_rect(gx, gy, 1, g + 1, K_TEXT), cv_rect(gx + g, gy, 1, g + 1, K_TEXT);
        cv_dash((float)gx, (float)(gy + g), (float)(gx + g), (float)gy, 2.5f, 2.5f, 0.8f, K_DIM);   /* unity */
        pn.w = 2.0f;
        for (int i = 0; i <= 48; i++) {
            float in = -48.0f + (float)i, k = 6.0f, o, d = in - thr;
            if (!ri || d <= -k * 0.5f)
                o = in;
            else if (d >= k * 0.5f)
                o = thr + d / R[ri];
            else                                     /* the knee */
                o = in + (1.0f / R[ri] - 1.0f) * (d + k * 0.5f) * (d + k * 0.5f) / (2.0f * k);
            pen_to(&pn, (float)gx + (in + 48.0f) * sc, (float)(gy + g) - fm_clampf(o + 48.0f, 0.0f, 48.0f) * sc, K_TEXT);
        }
        if (ri) {                                    /* the threshold, and callout 3's leader to it */
            float tx = (float)gx + (thr + 48.0f) * sc;
            cv_dash(tx, (float)(gy + g), tx, (float)(gy + g) - (thr + 48.0f) * sc, 2.0f, 2.0f, 0.8f, K_DIM);
            cv_dash(50.0f, 56.0f, tx - 3.0f, (float)(gy + g) - (thr + 48.0f) * sc, 2.0f, 2.0f, 0.8f, K_TEXT);
        }
        tr_text(gx + g / 2 - 6, gy + g + 3, F_LAB, "IN", K_DIM, 1);
        tr_text(gx, 0, F_LAB, "OUT", K_DIM, 1);
        tr_text(208, 0, F_LAB, "GR", K_DIM, 1);
        cv_rect(gx + g + 4, gy, 3, g + 1, K_PANEL);  /* the reduction now, and the pump's duck, hanging from the top */
        cv_rect(gx + g + 4, gy, 3, (int32_t)fm_clampf(gr * sc, 0.0f, (float)g), K_REC);
        cv_rect(gx + g + 9, gy, 3, g + 1, K_PANEL);
        cv_rect(gx + g + 9, gy, 3, (int32_t)fm_clampf(pump * sc, 0.0f, (float)g), K_REC);
    }
    cv_blit(0, MAIN_Y);
}
static void setup_row(int32_t y, const char *name, const char *val)
{
    cv_text(12, y, &FONT_S, name, K_DIM);
    text_r(228, y, &FONT_S, val, K_TEXT);
}
/* the shared LFO (the sheet's "LFO" for the whole instrument): its shape's name large, what it moves, the wave over
 * a cycle on graph paper with a dot where it is now (vermilion while it plays), its rate dimensioned (2), its amount
 * a height (3); the tracks it moves under it, in their inks. Off: said so, and how to start it */
static void draw_glfo(void)
{
    const uint8_t *g = proj.trk[AUX_BRAIN].par;
    int sh = g[TP_LTYPE] % (NGS + 1), now = (int)(engine_glfo_now() * 24.0f), route = (int)glfo_route();
    uint32_t h = hash_u(hash_u(2166136261u, 0x6F1u), (uint32_t)sh | (uint32_t)route << 4 | (uint32_t)g[TP_LDEST] << 12 |
                                           (uint32_t)(now + 32) << 16 | (uint32_t)g[TP_LAMT] << 24);
    int32_t x0 = 118, w = 104, cy = 40;
    float amp = 6.0f + 18.0f * (float)(g[TP_LAMT] > 64 ? g[TP_LAMT] - 64 : 64 - g[TP_LAMT]) / 64.0f;
    pen_t pn = {0};
    char t[32];
    h = hash_u(h, g[TP_P1] | (uint32_t)g[TP_LSPEED] << 8 | (uint32_t)playing() << 16 | (uint32_t)p2_touch() << 20);
    if (!begin_main(h))
        return;
    if (!sh) {
        p2_title("off", "THE SHARED LFO", 104);
        cv_text(9, 56, &FONT_XS, "KNOB 1: a shape starts it", K_DIM);
        cv_rect(x0, cy, w, 1, K_FAINT);
    } else {
        up_copy(t, GLFO_DEST[g[TP_LDEST] % NGD], 16);
        {
            char d[28];
            s_cat(s_cat(d, "MOVES "), t);
            p2_title(GLFO_SHAPE[sh], d, 104);
        }
        {                                         /* (on a kit: the sound it moves) */
            int ks = g[TP_P1];
            s_cat(s_cat(t, "kits: "), ks >= 1 && ks <= NDRUM ? DL_SOUND_NAME[ks - 1] : "every sound");
            text_fit(9, 52, &FONT_XS, t, 104, K_DIM);
        }
        cv_grid(x0, cy - 28, w + 1, 57, 8, MAIN_Y, K_GRID);
        cv_rect(x0, cy, w + 1, 1, K_DIM);
        pn.w = 2.0f;
        for (int i = 0; i <= 52; i++) {           /* (the random shape: a few steps, as it holds a value a cycle) */
            float ph = (float)i / 52.0f, v;
            switch (sh - 1) {
            case GS_TRI: v = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
            case GS_UP: v = 2.0f * ph - 1.0f; break;
            case GS_DOWN: v = 1.0f - 2.0f * ph; break;
            case GS_SQUARE: v = ph < 0.5f ? 1.0f : -1.0f; break;
            case GS_RANDOM: v = (float)((i / 13 * 5 + 3) % 7 - 3) / 3.0f; break;
            default: v = fm_sin_turns(ph); break;
            }
            pen_to(&pn, (float)x0 + ph * (float)w, (float)cy - v * amp * (g[TP_LAMT] < 64 ? -1.0f : 1.0f), K_TEXT);
        }
        cv_disc((float)(x0 + w + 7), (float)cy - (float)now * amp / 24.0f, 3.0f, playing() ? K_REC : K_TEXT);   /* now */
        dim_line((float)x0, (float)(cy + 34), (float)(x0 + w), (float)(cy + 34), 3.0f, 3.0f, K_TEXT);   /* the rate */
        cv_rect(x0 + w / 2 - 7, cy + 28, 15, 13, K_BG);
        circ_num(x0 + w / 2, cy + 35, "2", K_TEXT, K_BG);
        dim_line((float)(x0 - 6), (float)cy, (float)(x0 - 6), (float)cy - amp, 0.0f, 0.0f, K_TEXT);   /* the amount */
        circ_num(x0 + 8, cy - (int32_t)amp - 9 < 6 ? 6 : cy - (int32_t)amp - 9, "3", K_TEXT, K_BG);
    }
    for (int i = 0; i < NTRK; i++) {             /* the tracks it moves: black keys 1-8 */
        int32_t x = 8 + i * 28, on = route >> i & 1;
        if (on)
            cv_round(x, 100, 24, 16, 3, trk_ink(i));
        else
            cv_frame(x, 100, 24, 16, 3, K_LINE, K_BG);
        text_c(x + 12, cap_y(100, 16, F_LAB), F_LAB, TRACK_NAME[i], on ? K_BG : K_DIM);
    }
    cv_blit(0, MAIN_Y);
}
/* Setup: a specification sheet (the sheet's Setup): its rows with dotted leaders to their values in bold, and a small
 * drawing of the instrument: the USB port (a line), the Bluetooth link (dashed, solid while a device is joined), the
 * lights (the four pads filled when on), the version and the battery under it */
static void spec_row_fit(int32_t x1, int32_t x2, int32_t y, const char *label, const char *val)   /* a long value goes to XS */
{
    int32_t room = x2 - x1 - text_w(&FONT_XS, label) - 10;
    if (text_w(F_XB, val) <= room)
        spec_row(x1, x2, y, label, val, K_TEXT);
    else {
        int32_t e = cv_text(x1, y, &FONT_XS, label, K_TEXT);
        text_r(x2, y, &FONT_XS, val, K_TEXT);
        (void)e;
    }
}
static void draw_setup(void)
{
    char td[24], tb[24];
    uint32_t h = hash_u(2166136261u + proj.clock, 0x5E700u | (uint32_t)proj_slot | (uint32_t)proj.usb_full << 4 | (uint32_t)seq.ext << 8 |
                                          (uint32_t)proj.rsv[0] << 12 | (uint32_t)proj.send_notes << 17 |
                                          (uint32_t)(proj.rsv[2] & AUTOSAVE_OFF) << 26 | (uint32_t)(plat_battery() + 1) << 27);
    int bt_on, bat = plat_battery();
    const int32_t IX = 166, IY = 36;
    bt_dev_text(td, "on", "off");
    h = hash_s(h, td);
    h = hash_u(h, proj.lights);
    if (!begin_main(h))
        return;
    bt_on = td[0] != 'o' || td[1] != 'f';
    tr_text(9, 3, F_LAB, "SPECIFICATIONS", K_DIM, 1);
    spec_row_fit(8, 156, 19, "tempo from", seq.ext ? "MIDI clock in" : "LOOM-8");
    spec_row_fit(8, 156, 33, "clock", CLOCK_ROW[proj.clock % SEQ_NCLK]);
    spec_row_fit(8, 156, 47, "sends notes", proj.send_notes ? "yes" : "no");
    spec_row_fit(8, 156, 61, "USB audio", USBA_NAME[proj.usb_full & 1u]);
    spec_row_fit(8, 156, 75, "autosave", proj.rsv[2] & AUTOSAVE_OFF ? "off: SAVE saves" : "on");
    spec_row_fit(8, 156, 89, "BT MIDI", td);
    {   /* the instrument */
        cv_frame(IX, IY, 64, 42, 4, K_TEXT, K_CARD);
        cv_rect(IX + 5, IY + 5, 15, 15, K_TEXT);
        for (int i = 0; i < 4; i++) {
            float kx = (float)(IX + 28 + i * 9);
            cv_disc(kx, (float)IY + 9.5f, 3.2f, K_BG);
            cv_ring(kx, (float)IY + 9.5f, 3.4f, 0.9f, K_TEXT);
            cv_linef(kx, (float)IY + 9.5f, kx + 1.8f, (float)IY + 7.5f, K_TEXT, 0.8f);
            cv_frame(IX + 25 + i * 9, IY + 15, 7, 5, 1, K_TEXT, proj.lights ? K_TEXT : K_BG);
        }
        for (int i = 0; i < 16; i++)
            cv_rect(IX + 5 + i * 3 + (i * 5 >> 4), IY + 25, 3, 12, K_BG);
        cv_frame(IX + 4, IY + 24, 56, 14, 0, K_TEXT, K_BG);
        for (int i = 0; i < 14; i++)
            cv_rect(IX + 5 + i * 4, IY + 25, 1, 12, K_TEXT);
        for (int i = 0; i < 14; i++)
            if ((0x6B5u >> (i % 7)) & 1u && i % 7 != 2 && i % 7 != 6)
                cv_rect(IX + 5 + i * 4 + 3, IY + 25, 2, 7, K_TEXT);
        cv_linef((float)IX + 12.0f, (float)IY, (float)IX + 12.0f, (float)IY - 9.0f, K_TEXT, 1.0f);   /* USB */
        tr_text(IX + 12 - tr_w(F_LAB, "USB", 0) / 2, IY - 21, F_LAB, "USB", K_TEXT, 0);
        if (bt_on)
            cv_linef((float)IX + 52.0f, (float)IY, (float)IX + 52.0f, (float)IY - 9.0f, K_TEXT, 1.0f);   /* the link: joined = solid */
        else
            cv_dash((float)IX + 52.0f, (float)IY, (float)IX + 52.0f, (float)IY - 9.0f, 2.0f, 2.0f, 1.0f, K_TEXT);
        tr_text(IX + 52 - tr_w(F_LAB, "BT", 0) / 2, IY - 21, F_LAB, "BT", bt_on ? K_TEXT : K_DIM, 0);
        s_cat(s_cat(tb, "LOOM-8 "), APP_VERSION);
        text_c(IX + 32, IY + 47, &FONT_XS, tb, K_DIM);
        if (bat >= 0) {
            if (bat == 4)
                s_cat(tb, "on USB power");
            else
                s_cat(num_s((uint32_t)(bat > 3 ? 3 : bat), s_cat(tb, "battery ")), " of 3");
            text_c(IX + 32, IY + 60, &FONT_XS, tb, bat <= 1 ? K_REC : K_DIM);
        }
    }
    cv_blit(0, MAIN_Y);
}
static uint32_t status_peak;                          /* the highest peak shown since the screen reader said it (ui.c) */
static void draw_status(void)
{
    char t[24], *e;
    uint32_t cpu = plat_cpu_pct(), peak = plat_cpu_peak_pct(), xr = plat_xruns();
    if (peak > status_peak)
        status_peak = peak;
    uint32_t h = hash_u(2166136261u, 0x57A00u | cpu | peak << 8 | (uint32_t)eng_voices << 16 | (uint32_t)eng_voices_max << 24);
    h = hash_u(h, xr ^ eng_guard_count << 8 ^ (uint32_t)engine_core2_state() << 28 ^ (uint32_t)d909_res_ok() << 31);
    if (!begin_main(h))
        return;
    tr_text(9, 1, F_LAB, "DEBUG: THE MACHINE", K_DIM, 1);
    e = num_s(cpu, t);
    e = s_cat(e, "%, peak ");
    e = num_s(peak, e);
    s_cat(e, "%");
    spec_row_fit(8, 228, 15, "audio load", t);
    e = num_s((uint32_t)eng_voices, t);
    e = s_cat(e, " of ");
    e = num_s((uint32_t)eng_voices_max, e);
    spec_row_fit(8, 228, 28, "voices", t);
    spec_row_fit(8, 228, 41, "second core", CORE2_NAME[engine_core2_state() & 3]);
    num_s(eng_guard_count, t);
    spec_row_fit(8, 228, 54, "guard trips", t);
    num_s(xr, t);
    spec_row_fit(8, 228, 67, "dropouts", t);
    spec_row_fit(8, 228, 80, "version", APP_VERSION);
    spec_row_fit(8, 228, 93, "909 cymbals", d909_res_ok() ? "there" : "missing: reinstall");
    cv_blit(0, MAIN_Y);
}

/* step record on a play page: the cursor's bar as a strip of 16 cells along the main band's foot, so the
 * keys' writes are not blind */
#define RS_H 16
static void draw_rec_strip(void)
{
    int trk = sel_trk(), b = ui.cursor / 16;
    const tpat_t *tp;
    uint32_t h;
    if (!ui.rec_armed || playing() || !is_seqt(trk) || ui.card || lst.open || ui.page < P_ENGINE || ui.page > P_PLAYERS3)
        return;
    tp = tpat(trk);
    h = hash_u(hash_u(2166136261u, 0x55u), (uint32_t)ui.cursor | (uint32_t)trk << 8);
    for (int i = 0; i < 16; i++)
        h = hash_u(h, !step_empty(&tp->st[b * 16 + i]));
    if (h == ui.sig[5])
        return;
    ui.sig[5] = h;
    cv_begin(240, RS_H, K_BG);
    for (int i = 0; i < 16; i++) {
        int st = b * 16 + i;
        int32_t x = 8 + i * 14;
        uint16_t c = st >= tp->len ? K_BG : !step_empty(&tp->st[st]) ? K_TINT : K_PANEL;   /* (as Steps draws them) */
        cv_round(x, 3, 12, 11, 2, c);
        if (st == ui.cursor)
            cv_frame(x - 1, 2, 14, 13, 3, K_REC, c);
    }
    cv_blit(0, MAIN_Y + MAIN_H - RS_H);
}
static void ink_follow(void)                         /* another track: its ink, every band drawn again */
{
    int trk = sel_trk();
    if (k_now == trk_ink(trk))
        return;
    k_now = trk_ink(trk);
    k_tint = trk_tint(trk);
    k_mid = ink_mix(k_now, theme->mid);
    for (uint32_t i = 0; i < sizeof ui.sig / sizeof ui.sig[0]; i++)
        ui.sig[i] ^= 0x5A5A5A5Au;
}
static int flash_area(void)
{
    int trk = sel_trk();
    if (!is_seqt(trk))
        return 0;
    return ui.page == P_BAR ? 2 : ui.page == P_STEPS && trk != AUX_WEAVE && !ui.chordmode ? 1 : 0;
}
static void beat_flash(void)
{
    uint32_t now = plat_ms(), t = seq.step_tick, b = seq.bar16 & 15u, d = (uint8_t)(t - fl_six), q = 0, i = (now - fl_t0) >> FLASH_STEP_SHIFT;
    fl_six = (uint8_t)t;
    if (playing() && !(proj.rsv[3] & BEAT_FLASH_OFF) && flash_area() && !ui.card && !lst.open &&
        !ui.ask) {
        if (d > (b & 3u) && (!fl_pk || i >= 4u) && !((b & 4u) && (seq.ext ? seq.ext_bpm : seq.bpm) > (float)FLASH_MAX_BPM)) {
            fl_t0 = now;                             /* a beat came by (not within 256 ms of the last: a torn read) */
            fl_pk = (uint8_t)(b & 12u ? FLASH_BEAT : FLASH_DOWN);
            i = 0;
#ifdef PLAT_HOST
            fl_count++;
#endif
        }
        if (fl_pk && i < 3u)
            q = fl_pk * FLASH_DECAY[i] >> 3;
    } else
        fl_pk = 0;
    if (q != fl_pct) {                               /* a new level: the main band is drawn again, through the filter */
        fl_pct = (uint8_t)q;
        ui.sig[1] = 0;                               /* (the main band only) */
    }
}
/* ARP's Weave page: the track, what it weaves with and how, in words with their names, and its first bar three
 * ways: its own steps, the other pattern's, and what plays woven (each step in the ink of the pattern it came from) */
static void weave_row(int32_t y, const char *label, uint16_t lc, const tpat_t *p, const tpat_t *a,
                      const tpat_t *b, int ka, int kb, int rule, uint16_t ca, uint16_t cb, int bar)
{
    char lb[12];
    up_copy(lb, label, sizeof lb);
    cv_text(8, y, F_LAB, lb, lc);
    cv_rect(54, y + 11, 175, 1, K_LINE);         /* (a punched strip: its base line) */
    for (int j = 0; j < 16; j++) {               /* the bar shown (OCT): steps bar * 16 .., a shorter pattern round again */
        int32_t x = 54 + j * 11;
        int i = bar * 16 + j;
        step_t w;
        const step_t *st = &w, *own = &a->st[i % (a->len ? a->len : 1)];
        uint16_t c;
        if (p)
            st = &p->st[i % (p->len ? p->len : 1)];
        else
            weave_step(a, ka, b, kb, rule, (uint32_t)i, &w);
        if (step_empty(st)) {                    /* nothing: a dot, the beats a tick */
            cv_rect(x + 3, y + (j % 4 ? 8 : 6), 1, j % 4 ? 2 : 4, j % 4 ? K_LINE : K_FAINT);
            continue;
        }
        c = p ? (p == a ? ca : cb) : memcmp(&w, own, sizeof w) ? cb : ca;
        cv_rect(x + 1, y + 1, 6, 9, c);
    }
}
static void draw_weave(void)
{
    int trk = sel_trk(), f = wv_from(trk), r = wv_rule(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0x57u), (uint32_t)trk | (uint32_t)f << 4 | (uint32_t)wv_pat(trk) << 8 |
                                          (uint32_t)r << 12 | (uint32_t)cur_pat(trk) << 16 |
                                          (uint32_t)((seq.wv_live | seq.wv_rec) >> trk & 1u) << 24 |
                                          (uint32_t)wv_cued(trk) << 25 | (uint32_t)ui.bar << 26);
    char t[56], an[12], bn[12], *e;
    int32_t sw = 228;                            /* (the status line's room: less beside "bar 1 of 4") */
    h = hash_u(h, playing() ? eng_step[trk] % NSTEP + 1u : 0u);
    if (f)
        h = hash_pat(hash_pat(h, tpat(trk)), &pats.pat[f - 1][wv_pat(trk)]);
    if (!begin_main(h))
        return;
    if (!f) {
        s_cat(s_cat(t, TRACK_NAME[trk]), " weaves with nothing yet");
        cv_text(8, 2, &FONT_B, t, K_TEXT);
        for (int k = 0; k < 2; k++) {            /* the strips to be, dashed; what sets them, called out */
            cv_dash(54.0f, 40.0f + (float)k * 18.0f, 229.0f, 40.0f + (float)k * 18.0f, 3.0f, 3.0f, 1.0f, K_LINE);
            cv_text(8, 30 + k * 18, F_LAB, k ? "WITH" : TRACK_NAME[trk], K_DIM);
        }
        circ_num(14, 81, "1", K_TEXT, K_BG);
        cv_text(23, 74, &FONT_XS, "with: a black key, the track to weave in", K_TEXT);
        circ_num(14, 99, "2", K_TEXT, K_BG);
        cv_text(23, 92, &FONT_XS, "pattern: a white key, its pattern", K_TEXT);
        cv_blit(0, MAIN_Y);
        return;
    }
    num_s((uint32_t)cur_pat(trk) + 1u, s_cat(s_cat(an, TRACK_NAME[trk]), " P"));   /* "T2 P1", "T1 P3" */
    num_s((uint32_t)wv_pat(trk) + 1u, s_cat(s_cat(bn, TRACK_NAME[f - 1]), " P"));
    if (f - 1 == trk)                                /* its own patterns: "T2 weaves P1 with P3" */
        e = s_cat(s_cat(s_cat(s_cat(t, TRACK_NAME[trk]), " weaves "), an + s_len(TRACK_NAME[trk]) + 1), " with ");
    else
        e = s_cat(s_cat(t, TRACK_NAME[trk]), " weaves with ");
    s_cat(e, f - 1 == trk ? bn + s_len(TRACK_NAME[trk]) + 1 : TRACK_NAME[f - 1]);
    cv_text(8, 2, &FONT_B, t, K_TEXT);
    e = s_cat(s_cat(t, "by "), WEAVE_NAME[r]);
    e = s_cat(e, ": ");
    if (r == WV_BARS || r == WV_STEPS) {             /* in words, with their names */
        e = s_cat(s_cat(e, r == WV_BARS ? "a bar of " : "a step of "), an);
        s_cat(s_cat(e, ", then "), bn);
    } else
        s_cat(s_cat(s_cat(s_cat(e, r == WV_RHYTHM ? an : bn), "'s rhythm, "), r == WV_RHYTHM ? bn : an), "'s pitch");
    text_fit(8, 22, &FONT_XS, t, 228, K_DIM);
    {
        const tpat_t *a = tpat(trk), *b = &pats.pat[f - 1][wv_pat(trk)];
        uint16_t ca = trk_ink(trk), cb = f - 1 == trk || trk_ink(f - 1) == ca ? K_TEXT : trk_ink(f - 1);
        int ka = wv_kind(trk), kb = wv_kind(f - 1), la = a->len > b->len ? a->len : b->len;
        int nb = (la + 15) / 16, bar = ui.bar < nb ? ui.bar : nb - 1;
        for (int c = 0; c < 16; c += 4) {        /* the steps' numbers */
            num_s((uint32_t)(bar * 16 + c) + 1u, t);
            text_c(58 + c * 11, 38, F_LAB, t, K_DIM);
        }
        weave_row(51, an, K_DIM, a, a, b, ka, kb, r, ca, cb, bar);
        weave_row(65, bn, K_DIM, b, a, b, ka, kb, r, ca, cb, bar);
        weave_row(79, "plays", K_TEXT, 0, a, b, ka, kb, r, ca, cb, bar);
        cv_rect(54, 91, 175, 1, K_TEXT);
        if (playing()) {                         /* where the track plays, in the bar shown */
            int ps = eng_step[trk] % NSTEP;
            if (ps / 16 == bar) {
                float x = 58.0f + (float)(ps % 16) * 11.0f;
                cv_linef(x, 50.0f, x, 92.0f, K_REC, 1.4f);
            }
        }
        if (nb > 1) {                                /* more than a bar: which one, OCT for the others */
            char bt[16];
            num_s((uint32_t)nb, s_cat(num_s((uint32_t)bar + 1u, s_cat(bt, "bar ")), " of "));
            cv_text(232 - text_w(F_CAP, bt), 100, F_CAP, bt, K_DIM);
            sw = 220 - text_w(F_CAP, bt);
        }
    }
    if (!wv_cued(trk))
        s_cat(t, "on: it plays woven");
    else if ((seq.wv_live | seq.wv_rec) >> trk & 1u)
        s_cat(t, "weaving now");
    else
        num_s((uint32_t)trk + 1u, s_cat(t, "plays while WEAVE's key ")), s_cat(t + s_len(t), " is on");
    text_fit(8, 100, F_CAP, t, sw, (seq.wv_live | seq.wv_rec) >> trk & 1u && playing() ? K_REC : !wv_cued(trk) ? K_TEXT : K_DIM);
    cv_blit(0, MAIN_Y);
}
/* the WEAVE track's page: the ten tracks it weaves (white keys 1-10), each with its weave, lit while it weaves */
static void draw_weave_play(void)
{
    uint32_t on = (uint32_t)(seq.wv_live | seq.wv_rec | seq.wv_always);
    uint32_t h = hash_u(hash_u(2166136261u, 0x5Au), on | (uint32_t)proj.trk[AUX_WEAVE].par[TP_E1] << 16 |
                                          (uint32_t)playing() << 20);
    int any = 0;
    for (int t = 0; t < NSEQT; t++)
        h = hash_u(h, (uint32_t)wv_from(t) | (uint32_t)wv_pat(t) << 8 | (uint32_t)wv_rule(t) << 12);
    if (!begin_main(h))
        return;
    for (int t = 0; t < NSEQT; t++) {                /* two columns of five: its white key, the track, what with, how */
        int32_t x = 8 + (t / 5) * 116, y = 2 + (t % 5) * 19, e;
        int f = wv_from(t), lit = on >> t & 1u;
        char wt[16];
        key_cap(x, y, (num_s((uint32_t)t + 1u, wt), wt), lit);
        e = cv_text(x + 22, y + 1, F_LAB, t < NTRK ? TRACK_NAME[t] : t == AUX_BRAIN ? "CO" : "TH",
                    lit && playing() ? K_REC : f ? K_TEXT : K_FAINT);
        if (f) {                                 /* "T1 P3" (its own: "P3"), the rule */
            if (t == f - 1)
                num_s((uint32_t)wv_pat(t) + 1u, s_cat(wt, "P"));
            else
                num_s((uint32_t)wv_pat(t) + 1u, s_cat(s_cat(wt, TRACK_NAME[f - 1]), " P"));
            e = cv_text(e + 4, y, &FONT_XS, wt, f - 1 < NTRK ? trk_ink(f - 1) : K_TEXT);
            cv_text(e + 4, y, &FONT_XS, WEAVE_NAME[wv_rule(t)], K_DIM);
            any = 1;
        } else
            cv_leader(e + 5, x + 100, y + 9, K_LINE);
    }
    cv_text(8, 100, F_CAP, !any ? "no weaves yet: set one on a track's ARP"
                             : proj.trk[AUX_WEAVE].par[TP_E1] ? "latch: a tap on, a tap off" : "a key held: that track weaves",
            K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_page(void)
{
    int trk = sel_trk();
    if (ui.card) {
        draw_card();
        return;
    }
    if (lst.open) {
        draw_list();
        return;
    }
    switch (ui.page) {
    case P_ARRANGE: draw_arrange(); break;
    case P_SONG: draw_song(); break;
    case P_STEPS:
        if (ui.chordmode && is_seqt(trk) && !is_drum(trk))
            if (ui.chord_pool)
                draw_step_pool();
            else
                draw_step_chord();
        else
            draw_steps();
        break;
    case P_BAR: draw_bar(); break;
    case P_CHARACTER: draw_character(); break;
    case P_SENDS: draw_sends(); break;
    case P_FX1: draw_fxtrack(AUX_FX1); break;
    case P_FX2: draw_fxtrack(AUX_FX2); break;
    case P_ENGINE: case P_PLAY: case P_PLAY2: case P_SUB: case P_ENV: case P_ENV2: case P_KITF: case P_FILTER: case P_LFO:
    case P_LFO2: case P_PLAYERS: case P_PLAYERS2: case P_PLAYERS3:
        if (trk == AUX_BRAIN)
            draw_brain();
        else if (trk == AUX_PUNCH)
            draw_punch();
        else if (trk == AUX_WEAVE)
            draw_weave_play();
        else if (is_aux(trk))
            draw_fxtrack(trk);
        else if (ui.page == P_ENGINE || (is_drum(trk) && (ui.page == P_PLAY || ui.page == P_SUB)))
            draw_engine();
        else if (ui.page == P_PLAY)
            draw_play();
        else if (ui.page == P_PLAY2 && is_drum(trk))
            draw_sends();
        else if (ui.page == P_PLAY2)
            draw_play2();
        else if (ui.page == P_SUB)
            draw_sub();
        else if (ui.page == P_ENV && is_drum(trk))
            draw_key_env();
        else if (ui.page == P_ENV || ui.page == P_ENV2)
            draw_env(ui.page == P_ENV2);
        else if (ui.page == P_FILTER || ui.page == P_KITF)
            draw_filter();
        else if (ui.page == P_LFO || ui.page == P_LFO2)
            draw_lfo();
        else
            draw_players();
        break;
    case P_MIX: draw_mix(); break;
    case P_THROWS: draw_punch(); break;
    case P_WEAVE: draw_weave(); break;
    case P_MASTER: case P_MASTER3: draw_master(); break;
    case P_MASTER2: draw_master_color(); break;
    case P_GLFO: draw_glfo(); break;
    case P_SETUP: draw_setup(); break;
    default: draw_status(); break;
    }
}
