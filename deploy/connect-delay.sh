#!/bin/sh
# JACK autoconnect. Fail rather than silently routing the wrong audio port.
set -eu
i=0
until jack_lsp JerzyMemoryDelay:out_2 >/dev/null 2>&1; do
  i=$((i+1))
  [ "$i" -lt 40 ] || { echo "Memory Delay JACK ports unavailable" >&2; exit 1; }
  sleep 1
done
for pair in \
  "system:capture_1 JerzyMemoryDelay:in_1" \
  "JerzyMemoryDelay:out_1 system:playback_1" \
  "JerzyMemoryDelay:out_2 system:playback_2"
do
  set -- $pair
  jack_connect "$1" "$2" || true
done
