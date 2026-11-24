# Reused local implementation

Imported multifx-next/ from local commit6988a104fc5d3f994e962d1fbd48e963c4f20616,
previously built in this task's repository (not upstream sourceffadec1).
This brings explicitJFX3, controller-owned pending/ack graph, legacy2 bridge,
single CAB PFFFT, real MIDI instrument/gain, backends and their existing tests.
No claim that this baseline fulfills Nov23 acceptance: mode ramps, full dualCAB,
old preset/CC migration and repeatable hardware benchmark remain new work.
Fresh Debug imported baseline:19/19; source baseline4/4 and Python3/3,
13 module metadata validations. Initial SDK configure failed because extracted
JACK headers were not under /usr/include; locally combined official SDK extraction
and PKG_CONFIG_SYSROOT_DIR fixed it. This was an environment path problem, not DSP.
Historical documentation/evidence retains its dates, superseded by REFACTOR_AUDIT.
