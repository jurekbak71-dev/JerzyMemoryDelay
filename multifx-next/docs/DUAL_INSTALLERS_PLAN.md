# JACK2 and direct ALSA installers

Authorized by user: two separately installable variants for A/B testing.

1. Preserve shared DSP, plugins and session protocol. Factor controller loop.
2. Add direct ALSA duplex worker with preallocated buffers, explicit hardware channel
   mapping and exact sample rate; bounded shutdown, XRUN recovery and truthful diagnostics.
3. Native integration through ALSA null PCM; preserve JACK dummy integration.
4. Ship two source installers with native compilation on the target. This avoids assuming
   an OS version: repo confirms Pi OS 64-bit but not Bullseye/Bookworm/Trixie.
   No apt, services, JACK configuration or device opening during installation.
5. Test installation in separate temporary prefixes, build both, run test suites.
6. Review, commit locally, provide archives. No push or release without approval.

Physical HeadRush latency and ALSA sequencer availability remain hardware tests.
