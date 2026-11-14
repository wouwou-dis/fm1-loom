#!/bin/sh
# tools/p4/tour.sh NAME: tools/p4/NAME.xy -> app/build/p4/NAME/
cd "$(dirname "$0")/../.."
B=app/build; rm -rf $B/p4/$1; mkdir -p $B/p4/$1
$B/host/loom_host tools/p4/$1.xy $B/p4/$1 > $B/p4/$1/log.txt 2>&1; echo "rc=$?"; grep -E "LAYOUT|FAIL|error|die" $B/p4/$1/log.txt | head -20
