#!/bin/sh
# run a tour: tools/p1/tour.sh NAME (tools/p1/NAME.xy -> app/build/p1/NAME/)
cd "$(dirname "$0")/../.."
B=app/build; rm -rf $B/p1/$1; mkdir -p $B/p1/$1
$B/host/loom_host tools/p1/$1.xy $B/p1/$1 > $B/p1/$1/log.txt 2>&1; echo "rc=$?"; grep -E "LAYOUT|FAIL|error" $B/p1/$1/log.txt | head -20
