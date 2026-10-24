#!/bin/sh
# If installed, a2jmidid exports ALSA sequencer controllers to JACK MIDI.
# We don't change the working JACK daemon or Wi-Fi settings.
if command -v a2jmidid >/dev/null 2>&1; then
  a2jmidid -e >/dev/null 2>&1 &
  bridge=$!
else
  bridge=""
  echo "Optional ALSA/JACK bridge a2jmidid not installed. Connect JACK MIDI input manually if needed."
fi
trap 'if [ -n "$bridge" ]; then kill "$bridge" 2>/dev/null || true; fi; exit 0' TERM INT
while :; do
  if command -v jack_lsp >/dev/null 2>&1; then
    jack_lsp -p -t 2>/dev/null | awk '
      /^[^ \t]/ {
        if (port!="" && output && midi) print port
        port=$0;output=0;midi=0
      }
      /properties:.*output/ {output=1}
      /type:.*raw midi/ {midi=1}
      END {if(port!="" && output && midi)print port}
    ' | while IFS= read -r port; do
      [ "$port" = "JerzyMultiFX4:midi_in" ] && continue
      jack_connect "$port" "JerzyMultiFX4:midi_in" >/dev/null 2>&1 || true
    done
  fi
  sleep 3
done
