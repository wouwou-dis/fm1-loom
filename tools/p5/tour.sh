#!/bin/sh
# tools/p5/tour.sh NAME: tools/p5/NAME.xy -> app/build/p5/NAME/
cd "$(dirname "$0")/../.."
B=app/build; rm -rf $B/p5/$1; mkdir -p $B/p5/$1
$B/host/loom_host tools/p5/$1.xy $B/p5/$1 > $B/p5/$1/log.txt 2>&1; echo "rc=$?"; grep -E "LAYOUT|FAIL|error|die" $B/p5/$1/log.txt | head -20
