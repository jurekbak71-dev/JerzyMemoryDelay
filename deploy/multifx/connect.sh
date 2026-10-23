#!/bin/sh
# Only the new MultiFX owns system playback after migration.
set -eu
i=0
until /usr/bin/jack_lsp 2>/dev/null | /usr/bin/grep -qx 'JerzyMultiFX:out_2'; do
  i=$((i+1))
  [ "$i" -lt 40 ] || { echo "JerzyMultiFX JACK ports unavailable" >&2; exit 1; }
  sleep 1
done
/usr/bin/jack_connect system:capture_1 JerzyMultiFX:in_1
/usr/bin/jack_connect JerzyMultiFX:out_1 system:playback_1
/usr/bin/jack_connect JerzyMultiFX:out_2 system:playback_2
