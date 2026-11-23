# Windows to RPi installers

User clarifies installation originates on Windows and asks to follow previous setup.
Confirmed deploy/multifx4/README.md: Windows downloads sources while online, joins
Jerzy-MultiFX hotspot, transfers via scp to jerzy@10.42.0.1, installs native offline.
No historical .cmd/.ps1/.exe installer found in all local refs.

Implement two offline ZIPs with START.cmd -> Windows PowerShell 5.1 WinForms.
Bundle official PuTTY 0.83 plink/pscp x64, license and SHA256 provenance. GUI takes
address/user/password, probes SSH key and explicitly confirms/pins it before auth.
Password temporary file: user-only Windows ACL, removed finally; never argv/log.
Upload trusted payload by SFTP, run Python bootstrap, verify hashes, native source
install on RPi, add user menu launcher. No root, apt/network dependency download,
service activation or changes to existing JACK/hotspot. Preflight actual OS/arch/tools.
No push/release. Windows runtime and physical Pi unavailable: report explicitly.
Test builder ZIP/PE metadata/hashes, PowerShell parser when obtainable, actual remote
bootstrap/local staging/build on Linux (hardware/SSH/UI claims separately).
