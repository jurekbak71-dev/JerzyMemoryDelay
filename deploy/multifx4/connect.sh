#!/bin/sh
set -eu
i=0
until /usr/bin/jack_lsp 2>/dev/null | /usr/bin/grep -qx JerzyMultiFX4:out_2; do
  i=$((i+1))
  [ "$i" -lt 40 ] || { echo "MultiFX4 audio ports unavailable" >&2; exit 1; }
  sleep 1
done
for pair in "system:capture_1 JerzyMultiFX4:in_1" \
            "JerzyMultiFX4:out_1 system:playback_1" \
            "JerzyMultiFX4:out_2 system:playback_2"
do
  set -- $pair
  /usr/bin/jack_connect "$1" "$2" >/dev/null 2>&1 || true
done
echo "HeadRush routed; guitar and BassAmbient mixed before output."
