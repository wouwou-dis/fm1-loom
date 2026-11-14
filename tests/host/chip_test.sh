#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# sequin's host test (tests/host/chip_test.c): the chip voice bounded, in tune, its sweep, arpeggio, bits, hold, noise and
# step envelope as LOFI has them, its texts and picture, its presets, its cost against knit's. Built with the device's
# rules (C99, float only in the DSP, no fused multiply-add).
#   tests/host/chip_test.sh
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
"$PY" tools/gen_presets.py build/gen/presets.h
$CC -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wdouble-promotion -Werror -Wno-unused-function -DPLAT_HOST \
    -Iapp/dsp -Ibase/include -Ibuild/gen -o build/host/chip_test tests/host/chip_test.c app/dsp/drumline.c app/dsp/d909.c \
    app/dsp/digisophie.c app/dsp/sample.c app/dsp/bass303.c -lm
build/host/chip_test
