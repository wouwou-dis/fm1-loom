# What changed from LOOM-8 0.15.1

This is the list of what this fork (branch `paper`) does differently from the LOOM-8 it was cut from: [Charles
Vestal's LOOM-8](https://github.com/charlesvestal/fm1-loom), version 0.15.1, upstream commit `ffc0cd1`. Everything else is
his work, unchanged. The fork is unofficial; see the [README](README.md).

Each item says what it is and why, and names the commit or merge that brought it in, so you can look at it
yourself:

```
git log --stat ffc0cd1..paper        # everything, commit by commit
git show 94e9528                     # the first commit: the bulk of the interface, song building and effects work
git diff ffc0cd1 paper -- app/       # the firmware's own source
```

One caution about the hashes: `94e9528` is a single large commit (about 8,000 lines added, mostly the interface in
`app/pages.c`). Items that name it cannot be told apart in the history any further. The later items each have their
own commits or merges. Items I could not check against the code or the log are left out rather than guessed at.

## Interface

The interface is redrawn from the ground up. The drawing code is in `app/pages.c` and `app/gfx_loom.c`; the page
bodies are in the `draw_*` functions.

- **One "paper" look, in the style of a printed service manual** (`94e9528`). Ink on paper, with a header, a knob strip
  whose knobs have circled numbers (1 to 4) and rulers, and a fixed two-column hint area at the bottom that always says
  which control does what. It replaces the three colour themes and the large-type mode, which are gone (Setup's THEME
  is removed; a project saved with a theme still loads, the byte is ignored). The always-visible hints were kept on
  purpose: the interface is meant to be learned from the screen.
- **Steps is a punched card; Song is a route map; Arrange is ten punched strips; Mix is a desk** (`94e9528`). On a
  drum track, the card shows every drum sound in the bar at once; on a synth track it is a piano-roll strip. The Song
  page draws sections as stops with letters (A, A, B, A) and a train at the playing position. Why: each page shows the
  thing it edits, not a list of numbers about it.
- **Every sound engine, send effect, reverb mode and character type is drawn as an imagined 1980s machine** (`94e9528`),
  whose own controls are the page's four knobs, so the circled number on the drawing is the knob under it. Later
  passes de-duplicated the drawing parts the effects share with the engines (`a481234`) and drew sequin's machine
  (`0552612`). See the sheets of [engines](docs/img/engines.png), [effects](docs/img/effects.png) and
  [character types](docs/img/characters.png).
- **The four echo reverbs are derelict Cold War signal stations** (`32bf335`): one echo a radar scope in a dead desk,
  half echo a horn facing a concrete sound mirror, space echo a dish and a satellite, chaos echo a search radar fallen
  off its cabin. Tone and damp are two dials on each.
- **Envelope, Filter, LFO and Players are drawn as instruments** (`8b9d64f`, `86a3114`, `bae0c55`). The envelope is a
  generator module with sliders beside a chart recorder; the filter has a faceplate for each type (their silhouettes
  differ: `f3355c6`); the LFO is a function generator with a round scope; the arp is a step box, strum a guitar neck,
  hold a sustain pedal, random a tumbler. Master EQ, Master out and Master color got the same treatment
  (`304a396`, `75ca06a`, `57c14ae`), the Bar page draws one step instead of three spec rows (`a003a33`), and the help
  card became a manual page with its corner turned (`13f20f2`).
- **The LFO page is animated** (`86a3114`, `53fc44d`): a dot runs along the drawn shape at the LFO's real rate, about
  12 pictures a second, while the LFO is acting. It is the one exception to "no animation" in this interface.
- **The sequencer card pulses coral on the beat** (`626bff6`, `711d816`; merged in `991518c`). The paper of Steps and Bar
  tints on every beat while playing. It is a setting, BEAT FLASH on Setup's list, on by default. The Hold player now
  shows the notes held under the pedal (`c505260`).
- **A new splash** (`94e9528`).

## Song building

- **NEW SECTION** (`94e9528`): makes a new scene with its own copy of each pattern, so changes there leave the scene
  you came from as it was.
- **COPY BAR, ALL TRACKS and PASTE BAR, ALL TRACKS** (`94e9528`): the bar you are on, on every track at once, locks
  included. (COPY BAR and PASTE BAR for one track are LOOM-8's own.)
- **MOVE SLOT**, **section letters and bar positions** on the Song page, and **CLEAR ALL PATTERNS** (`94e9528`).
- **DUPLICATE SECTION** (`24be170`): the selected slot's scene copied into the first empty scene, with the same number
  of bars, so you can vary the repeat. (DUPLICATE SLOT, which repeats the same scene, is LOOM-8's own.)
- **Two levels of undo** (`24be170`) where LOOM-8 0.15.1 has one. Undo twice to go back two changes; redo twice to bring
  them back.
- **EUCLID HITS, EUCLID ROTATE and VARY BAR** (`24be170`): spread 1 to 16 hits evenly over the bar, turn them forward,
  or vary the bar (off-beat hits may drop or move a step, velocities shift; steps 1, 5, 9 and 13 stay).
- **Lists never reopen on a destructive row** (`24be170`). A list opens on the row you used last, except one that
  clears, deletes or loads, so a second press of SEL cannot repeat the damage.

## Sound

- **A new chip-voice engine, sequin** (`27a699d`). A port of the LOFI engine of
  [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) by Leo Kuroshita (Hügelton Instruments, MIT):
  pulse, triangle, saw, noise and a 32-step 4-bit wave memory, with crush, a pitch sweep, and a Sub page (chip mode,
  arp, vibrato, tone). It has ten factory presets, and a new project's track 7 is a sequin track where it was a cane
  one (`27a699d`, `820cef8`).
- **The plate reverb is rebuilt** (`94e9528`) as a diffused feedback network instead of combs, and later polished
  (`6e272b0`): it is as bright as the old 12-bit plate at the defaults, each trip is damped less below the default size,
  and the input allpasses are thinned out at small sizes, where they rang. The unit test checks that a small plate's
  ringing stays down. Its tail no longer cuts off.
- **Half-band filters around the "lush" reverb and the delay** (`94e9528`), the same kind of FIR the PlayStation modes use for their
  resampling, to decimate and interpolate between the half rate those effects run at and the full rate (`app/dsp/fx.c`).
- **A three-voice chorus** (`94e9528`): three voices a side, a third of a turn apart, in place of one.
- **A fix to the PlayStation reverb modes' output interpolator** (`48f5d7e`). Its two outputs were taken in the wrong
  order, which put each tone's image at 22.05 kHz only 5 to 10 dB under it. Measured on a rendered ramp, the image
  energy above 12 kHz is now more than 50 dB under the signal (54 dB or more in the commit's own check, which the
  unit test now repeats).

## Removed

Removed to make room for the song, effects and interface work, and because the flash and RAM are small.

- **Bluetooth audio** and **Bluetooth MIDI devices** (a keyboard or controller the FM-1 connects to) (`94e9528`).
  Bluetooth MIDI *with a computer, phone or tablet* stays. Both are built out (`app/fm1.toml`: `bt_audio = false`,
  `ble_central = false`; `app/app.h`: `LOOM_BT_AUDIO 0`), not deleted: their source is still in `base/`.
- **The cane engine** (Braids' physical models: pluck, bow, reed, flute, bell, drum) (`820cef8`). Its engine number stays
  taken, so no other engine moved. A project or user preset that used it still loads, as the nearest sound another
  engine has: pluck and bow as knit, reed and flute as bellows, bell and drum as rayon (`app/project.c`, `engine_retire`).
  Voile, the other Braids engine, stays.
- **The reverb modes "studio S" and "studio L"** (`75f0d15`). "Studio M" is now just "studio" (room, studio, hall). A
  project that saved a small or large studio loads with the studio.
- **The three colour themes and large-type mode** (`94e9528`), as above.

## Under the hood

- Scenario scripts and unit tests for the new rows, undo, fills, sequin, beat flash and the retired engine and
  studios (`tests/scenarios/`, `tests/host/`; for example `94e9528`, `ef288b9`, `ca3b5a0`, `37055e5`, `626bff6`/`de72b97`,
  `68e7598`, `d1de458`, `bbf8d19`). Not run for this documentation.
- Credits for voile (Braids) and sequin (Felucca-Engines' LOFI) in the manual, the licence file and `app/fm1.toml`
  (`2f5962f`, `a87d6f4`).
- The screenshots in `docs/img/` are regenerated from the simulator by `docs/make_screens.sh`.

## Saved projects

The project format is unchanged, still LOOM-8's format 20, so a project moves between this build and LOOM-8 0.15.1,
with these exceptions:

- A project saved by 0.15.1 that uses cane or the small or large studio loads here with the substitutes above.
- A project saved here that uses **sequin** has an engine number that LOOM-8 0.15.1 does not have. Reading its
  `app/project.c`, the loader's validity check (`settings_valid`) refuses a project whose engine number is past its
  last engine, so such a project is expected not to load there, and a user preset using sequin is expected to be dropped.
  This has not been tried on a device. Keep a backup before taking projects to official firmware.
- Setup's BEAT FLASH is stored in a spare bit of the project's settings byte that 0.15.1 ignores.

## Not in this build

Two pieces of work are parked on other branches and are not part of `paper`: dotted and triplet delay times
(branch `r/fix`, which would change the saved-project format) and a processor-load change for lofi and phaser
(branch `r/perf`, which needs measuring on a device).
