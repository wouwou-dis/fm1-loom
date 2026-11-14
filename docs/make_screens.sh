#!/bin/sh
# Regenerates every picture the README, CHANGES.md and the manual show, from the host simulator:
#
#   docs/make_screens.sh app/build/host/loom_host
#
# Build the simulator first with ./build.sh --host (it may rewrite firmware.json: `git checkout firmware.json`).
# Run from anywhere; the paths are the repository's. It needs python3 with Pillow.
#
#   docs/screens/manual.xy   -> docs/img/screen-NAME.png    the manual's pages (a small project built as the quick start says)
#   docs/screens/gallery.xy  -> docs/img/paper-NAME.png     the pages the README and CHANGES.md show
#                            -> docs/img/hero.png           the strip at the top of the README
#                            -> docs/img/engines.png, effects.png, characters.png   the machines, each on one sheet
#
# Every shot is the 240 x 240 screen enlarged to 480 x 480 with nearest-neighbour, so the pixels stay sharp. The
# simulator keeps its own clock and the scripts wait a fixed number of simulated milliseconds before each shot, so
# running this twice gives byte-identical files (LOOM_LOAD=0 takes the host's own timing out of the overload guard).
set -e
SIM="${1:?usage: docs/make_screens.sh PATH/TO/loom_host}"
PY="${PYTHON:-python3}"
cd "$(dirname "$0")/.."
SIM="$(cd "$(dirname "$SIM")" && pwd)/$(basename "$SIM")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/manual" "$TMP/gallery" docs/img
export LOOM_LOAD=0
"$SIM" docs/screens/manual.xy "$TMP/manual" >"$TMP/manual.log" 2>&1 || { cat "$TMP/manual.log"; exit 1; }
"$SIM" docs/screens/gallery.xy "$TMP/gallery" >"$TMP/gallery.log" 2>&1 || { cat "$TMP/gallery.log"; exit 1; }
grep -q "FAIL" "$TMP/manual.log" "$TMP/gallery.log" && { grep FAIL "$TMP/manual.log" "$TMP/gallery.log"; exit 1; }
"$PY" - "$TMP" docs/img <<'PY'
import sys
from pathlib import Path
from PIL import Image

tmp, out = Path(sys.argv[1]), Path(sys.argv[2])


def big(path):
    im = Image.open(path).convert("RGB")
    return im.resize((im.width * 2, im.height * 2), Image.NEAREST)


def save(im, path):
    im.save(path, optimize=True)


# the manual's screens: screen-NAME.png (NAME with "-" for "_")
n = 0
for f in sorted((tmp / "manual").glob("*.png")):
    save(big(f), out / f"screen-{f.stem.replace('_', '-')}.png")
    n += 1

# the gallery: paper-NAME.png
shots = {}
for f in sorted((tmp / "gallery").glob("*.png")):
    shots[f.stem] = big(f)
    save(shots[f.stem], out / f"paper-{f.stem.replace('_', '-')}.png")
n += len(shots)

PAPER = (236, 231, 217)      # (the page's own paper colour is close to this: a gutter that does not shout)


def sheet(names, cols, gap=12):
    ims = [shots[k] for k in names]
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    sh = Image.new("RGB", (cols * w + (cols + 1) * gap, rows * h + (rows + 1) * gap), PAPER)
    for i, im in enumerate(ims):
        sh.paste(im, (gap + (i % cols) * (w + gap), gap + (i // cols) * (h + gap)))
    return sh


save(sheet(["splash", "steps_playing", "song", "engine_sequin", "fx_delay", "echo_space", "mix"], 7, 8), out / "hero.png")
save(sheet(["engine_knit", "engine_rayon", "engine_jacquard", "engine_bellows", "engine_voile", "engine_acid",
            "engine_sequin", "engine_sample", "engine_slice", "engine_kit", "engine_midi"], 4), out / "engines.png")
save(sheet(["fx_chorus", "fx_delay", "fx_lofi", "fx_phaser", "fx_plate", "fx_lush", "fx_room", "fx_studio", "fx_hall",
            "echo_one", "echo_half", "echo_space", "echo_chaos"], 5), out / "effects.png")
save(sheet(["char_drive", "char_fold", "char_crush", "char_ring", "char_tilt", "char_tape", "char_sampler"], 4),
     out / "characters.png")
print(f"screens: {n} pictures and 4 sheets in {out}")
PY
