# SPDX-License-Identifier: MPL-2.0
"""sequin's presets (2026-11): a home computer's sound chip (dsp/chip.h, after Felucca-Engines' LOFI). knobs: wave
(0 pulse, 1 tri, 2 saw, 3 noise, 4 table: the wave memory), duty (the pulse's width in eighths, duty >> 5: 1/8 1/4 3/8
1/2; on table the table, duty >> 3: 11 vox a, 14 buzz, 9 organ ...), crush (the sample hold, 1 + crush / 8 samples; past
64 the held value in 16 steps), sweep (the pitch falling from the note: 0 none, 1 8 semitones a
second .. 127 ~1290, x1.041 a step: chip.h CH_SWEEP_OCT).
x: chip (0 4 bit, 1 4 bit lo, 2 8 bit, 3 1 bit: the noise at the held rate, 4 step: the envelope in 16 steps), arp
(0 off, 1 octave, 2 major, 3 minor), vib (0..127: to +-27 cents at 5.4 Hz), tone (the chip's own low-pass, 127 open).
The first five are LOFI's own (PULSE LD, WAVE BASS, ARP 8BIT, WAVE LEAD, STEP LEAD), on LOOM-8's envelope."""
from lib import P

PULSE, TRI, SAW, NOISE, TABLE = range(5)
B4, B4LO, B8, B1, STEP = range(5)
OFF, OCT, MAJ, MIN = range(4)   # (the arp)

# its sound in a new project: a quarter pulse lead with the chip's vibrato (LOFI's PULSE LD)
P("sequin", "lead", "sequin", knobs=dict(wave=PULSE, duty=32, crush=0, sweep=0), mode="legato", glide=6, volume=96,
  amp=(0, 70, 104, 40), fenv=(0, 64, 0, 64), filter=("lp12", 127, 0, 64, 64), sends=(30, 16), x=(B4, OFF, 20, 127),
  genres=("chiptune", "synthpop"))
# a triangle bass at the half rate, its tone closed a little (LOFI's WAVE BASS)
P("wave bass", "bass", "sequin", knobs=dict(wave=TRI, duty=0, crush=0, sweep=0), mode="mono", volume=112,
  amp=(0, 66, 96, 30), fenv=(0, 64, 0, 64), filter=("lp12", 110, 0, 64, 64), x=(B4LO, OFF, 0, 90),
  genres=("chiptune",))
# the wave memory's buzz, held a little: a bass with a bite
P("buzz bass", "bass", "sequin", knobs=dict(wave=TABLE, duty=14 * 8, crush=8, sweep=0), mode="mono", volume=96,
  amp=(0, 72, 80, 30), fenv=(0, 60, 0, 40), filter=("lp24", 92, 20, 84, 40), x=(B4, OFF, 0, 110),
  genres=("chiptune", "synthpop"))
# a major chord broken at the chip's rate, an 8-bit square (LOFI's ARP 8BIT)
P("chip arp", "chord", "sequin", knobs=dict(wave=PULSE, duty=96, crush=0, sweep=0), volume=90,
  amp=(0, 76, 80, 50), fenv=(0, 64, 0, 64), filter=("lp12", 127, 0, 64, 64), sends=(24, 20), x=(B8, MAJ, 0, 110),
  genres=("chiptune",))
# the wave memory's "ah", a vibrato: a sung lead (LOFI's WAVE LEAD)
P("vox lead", "lead", "sequin", knobs=dict(wave=TABLE, duty=11 * 8 + 4, crush=0, sweep=0), mode="legato", glide=8,
  volume=104, amp=(4, 70, 110, 40), fenv=(0, 64, 0, 64), filter=("lp12", 120, 0, 64, 64), sends=(30, 24),
  x=(B4, OFF, 18, 110), genres=("chiptune", "lofi"))
# a console's pulse, its envelope in sixteen steps, falling: a pluck (LOFI's STEP LEAD)
P("step pluck", "pluck", "sequin", knobs=dict(wave=PULSE, duty=40, crush=0, sweep=0), volume=100,
  amp=(0, 84, 0, 60), fenv=(0, 64, 0, 64), filter=("lp12", 127, 0, 64, 64), sends=(30, 20), x=(STEP, OFF, 16, 127),
  genres=("chiptune",))
# a zap: a pulse falling fast from high up (sweep 114: 1.11 semitones a chunk, ~765 a second)
P("zap", "fx", "sequin", knobs=dict(wave=PULSE, duty=64, crush=0, sweep=114), volume=88,
  amp=(0, 80, 0, 50), fenv=(0, 64, 0, 64), filter=("lp12", 127, 0, 64, 64), sends=(40, 10), x=(B4, OFF, 0, 127),
  octave=1, genres=("chiptune",))
# a chip kick: a triangle swept down hard, short
P("chip kick", "drum", "sequin", knobs=dict(wave=TRI, duty=0, crush=0, sweep=127), volume=124,
  amp=(0, 70, 0, 50), fenv=(0, 64, 0, 64), filter=("lp12", 127, 0, 64, 64), x=(B4, OFF, 0, 127), octave=1,
  genres=("chiptune",))
# a noise snare: the shift register at a quarter of the rate, a short stepped tail
P("noise snare", "drum", "sequin", knobs=dict(wave=NOISE, duty=0, crush=24, sweep=0), volume=110,
  amp=(0, 72, 0, 54), fenv=(0, 64, 0, 64), filter=("lp12", 110, 0, 64, 64), sends=(0, 20), x=(B1, OFF, 0, 127),
  genres=("chiptune",))
# a noise hat: the shift register at the full rate, high-passed, very short
P("noise hat", "drum", "sequin", knobs=dict(wave=NOISE, duty=0, crush=0, sweep=0), volume=96,
  amp=(0, 52, 0, 40), fenv=(0, 64, 0, 64), filter=("hp12", 100, 10, 64, 64), x=(B1, OFF, 0, 127),
  genres=("chiptune",))
