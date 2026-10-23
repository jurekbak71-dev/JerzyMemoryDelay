# Raspberry Pi 4B — Jerzy Audio auto-start

Tested configuration target: Raspberry Pi OS 64-bit, HeadRush Core ALSA `hw:CARD=Core,DEV=0` (44.1 kHz, 6 capture, 4 playback), JACK at 64 samples, 3 periods.

1. Stop manual JACK, Memory Delay and web_panel.py sessions with Ctrl+C before enabling the services.
2. In `~/JerzyMemoryDelay` run: `git pull && bash deploy/install-autostart.sh`.
3. Run: `sudo systemctl enable --now jerzy-jack jerzy-delay jerzy-panel jerzy-routing`.
4. Check: `systemctl --no-pager status jerzy-jack jerzy-delay jerzy-panel jerzy-routing`.
5. Panel: `http://bassambient.local:8088/`.

The JACK process and engines share the `jerzy` system user. The routing service connects `capture_1` to delay input and delay stereo outputs to `playback_1/2`.

**Recovery**:
- `sudo systemctl restart jerzy-jack jerzy-delay jerzy-panel jerzy-routing`
- `journalctl -u jerzy-jack -u jerzy-delay -u jerzy-panel -u jerzy-routing -b --no-pager -n 80`
- `sudo systemctl disable --now jerzy-routing jerzy-panel jerzy-delay jerzy-jack` to return to manual control.

**Extending to more effects**: do not start a second JACK server. Add one service per effect client and a route manager to compose effect chains. Only one engine should own a given output chain unless parallel mixing is intentional. Current routing is a single Memory Delay chain, not a multi-effect host.

**Security**: Web panel binds LAN without authentication. Use only on trusted Wi-Fi; do not forward port 8088 publicly.

**Important**: Current presets are not persistent. HeadRush Core must be powered and in USB Audio mode during boot. Services retry on errors but USB reconnect behavior needs testing.
