# paper 1.0 (based on LOOM-8 0.15.1)

<!-- (Links are absolute because this text becomes a GitHub release body.) Release notes for the first release of the fork. Fill in the two placeholders below, then use this file as the
     release text (gh release create ... --notes-file docs/RELEASE_NOTES_paper-1.0.md). Nothing here was built or
     published by the person who wrote it. -->

> **Source commit:** `874a1854a691d901810571ce7692a74c0beae12a` (the firmware in the attached files was built from this commit; the commits after it only touch these notes)
> **Files:** `loom-1.0-paper.fwsc` (identity `FM-1_10010000`, version text `1.0-PAPER` on the device) and `loom-1.0-paper-res.bin`
> **SHA-256 of the .fwsc:** `71cf3870049801823b32f003de13d20aefc318ce9dc7f56645e13d8e032ed2ca`
> **SHA-256 of the -res.bin:** `22ed9d1562690b1c5700b18766830e6774059b90da9a05ea2dc5b812fc181473`

## What this is

An unofficial, personal fork of [LOOM-8](https://github.com/charlesvestal/fm1-loom), the groovebox firmware for the
M-VAVE FM-1 by Charles Vestal, cut from LOOM-8 0.15.1. It is not affiliated with or endorsed by him, M-VAVE or teenage
engineering. Almost all of it is his work; see the [README](https://github.com/wouwou-dis/fm1-loom/blob/paper/README.md) for the credits and the licences. If you
want LOOM-8 itself, use the official releases.

## What is different

The full list, with the commit for each item, is [CHANGES.md](https://github.com/wouwou-dis/fm1-loom/blob/paper/CHANGES.md). In short:

- A new "paper" interface in the style of a printed service manual: Steps a punched card, Song a route map, Arrange
  ten punched strips, Mix a desk, a fixed two-column hint area, and an ink drawing of an imagined 1980s machine for
  every engine, send effect, reverb mode and character type. The four echo reverbs are derelict Cold War signal
  stations. The LFO page is animated, and the sequencer card pulses coral on the beat (BEAT FLASH on Setup's list).
- Song building: NEW SECTION, COPY BAR and PASTE BAR for all tracks, MOVE SLOT, DUPLICATE SECTION, CLEAR ALL PATTERNS, two
  levels of undo, EUCLID HITS, EUCLID ROTATE and VARY BAR, and lists that never reopen on a destructive row.
- Sound: a new chip-voice engine, sequin (from Felucca-Engines' LOFI by Leo Kuroshita); the plate reverb rebuilt;
  half-band filters around the lush reverb and the delay; a three-voice chorus; a fix to the PlayStation reverb modes'
  output interpolator (image energy above 12 kHz went from about 5 to 10 dB under the signal to more than 50 dB under).
- Sequin's sweep now ends the note when the pitch falls below hearing (it used to motorboat at 8 Hz on held notes), and
  its knob is spread evenly from a slow glide of 8 semitones a second to the fastest zap, with no dead travel.
- Setup is drawn as the instrument's rear panel: a slide switch for the clock, a toggle for the notes, lamps for the
  lights, an antenna for Bluetooth MIDI and a rating plate.

## What is missing compared with LOOM-8

- Bluetooth audio, and Bluetooth MIDI devices (keyboards and controllers). Bluetooth MIDI with a computer, phone or
  tablet stays.
- The cane engine (old projects load with the nearest sound from another engine).
- The reverb modes studio S and studio L (studio M is now "studio"; old projects load with it).
- The three colour themes and large-type mode.

Saved projects keep LOOM-8's format 20, so they move between this build and LOOM-8 0.15.1, except that a project that
uses sequin is expected not to load on official LOOM-8 (it has no such engine; untested on a device). Back up first.

## Install

At your own risk. The firmware is written by one person and tested on one device.

1. Download the `.fwsc` and the `-res.bin` of this release into one folder (the script writes the `-res.bin`, the 909
   cymbals' samples, after the firmware). Check the SHA-256 sums above.
2. The upstream web installer cannot install this file (it only offers a local file for M-VAVE's stock firmware). Use the
   script in the source: `git clone --recursive https://github.com/wouwou-dis/fm1-loom`, `pip3 install mido python-rtmidi`,
   connect the FM-1 by USB, then `python3 base/tools/fm1_install.py PATH/TO/THE.fwsc`.
3. `python3 base/tools/fm1_install.py --info` shows which firmware the FM-1 now has.

Back up your projects first if you care about them (`python3 base/tools/fm1_backup.py backup FILE --magic LM8B`).

## Going back

- **To official LOOM-8:** open <https://charlesvestal.github.io/fm1-loom/install/> in Chrome or Edge, with the FM-1
  plugged in, and install from there.
- **To M-VAVE's firmware:** the same page has "Back to the stock firmware" (download M-VAVE's `FM-1.fwsc`, choose it), or
  use M-VAVE's own updater. An FM-1 that crashes twice in a row at start-up comes up in a safe mode with USB on. One that
  will not start at all can be recovered with the rescue script in the manual's Troubleshooting section.

Going back has not been tried from this build by the person who wrote these notes; it is how LOOM-8's own pages are
written to work.

## Known limits

- Tested on one FM-1.
- Processor headroom was measured only with a light project: about 66 percent average on the first core, the worst block
  82 to 93 percent. Dense songs may stutter. The phaser and lofi effects cost the most.
- The simulator used for the screenshots and the tutorial cannot judge how anything sounds.
- Dotted and triplet delay times and a processor-load change for lofi and phaser are in progress on other branches and
  are not in this release.

## Source

The source of this release is the commit named above, in
<https://github.com/wouwou-dis/fm1-loom>. Read the [manual](https://github.com/wouwou-dis/fm1-loom/blob/paper/docs/MANUAL.md) and
the [tutorial](https://github.com/wouwou-dis/fm1-loom/blob/paper/docs/tutorial/TUTORIAL.md).
