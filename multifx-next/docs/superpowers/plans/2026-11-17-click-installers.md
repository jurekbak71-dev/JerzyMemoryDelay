# Clickable Installers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Two mouse-installable Debian packages plus graphical installation/start controls, no terminal instructions for the user.
**Architecture:** Separate architecture-independent .deb packages carry the same trusted source installer and a Tk panel. Debian dependencies supply build tools/Tk/audio SDK. First GUI launch prepares a native build into user-local versioned storage, then Start/Stop controls the real host. No service or running audio configuration is changed automatically.
**Tech Stack:** Python/Tk, Debian package metadata, existing C++ host and source installer.
**Spec:** User explicitly rejects terminal use, asks same mouse-based installation as previously. Repo's historical install.sh is terminal-based; no historical .desktop/.deb found. Use native .deb package installer rather than .desktop bootstrap with file-manager trust prompts.

## Global Constraints
- Only multifx-next/ repository changes; no push/release without approval.
- Different JACK2/ALSA icons, dependency metadata and install directories; can coexist.
- UI must remain responsive during build and host I/O. Do not claim ready before successful process exit.
- Package manager can ask password/install dependencies, as normal for .deb installation. No privileged build.
- JACK must already run; do not silently stop/start it or change routing. ALSA device-busy failure explained in Polish.
- No full DSP editor promised: minimal live Start/Stop, real gain FX test, status, diagnostics.

## Review Focus
- Paths with spaces: quoted .desktop Exec and argv subprocess, tested.
- Build failure or missing JACK/device: visible error, not false success, tested.
- Closing window during child process: bounded terminate/reap, tested.
- Existing installation: reuse only complete version marker, no overwrite, tested.
- Both packages installed together: disjoint package files/desktop names, tested.

### Task 1: Debian packaging
- [x] Add failing test: dpkg-deb metadata, dependencies and extraction; validate desktop entries, execute launcher's --help without display.
- [x] Implement tools/build_click_installers.py using existing source builder. Include variant-specific /opt payload and /usr/share/applications entry, desktop icon.
- [x] Test both .deb packages extraction and coinstallation file-set disjointness.

### Task 2: GUI build + host panel
- [x] Add real Tk/Xvfb integration test for async native install, failure reporting, already installed reuse, start/null audio, gain control, stats and stop.
- [x] Implement installer/click_app.py: installation state machine, worker->queue UI updates, SourceInstaller subprocess log, explicit host commands, Polish errors and graphical controls. No shell command entry.
- [x] Add clickable .deb dependencies and Polish pictorial/short text instructions. Existing source installer remains usable for developers.
- [x] Test actual extracted .deb on x86 with real ALSA and JACK dummy; distinguish from physical Pi/package-manager install.

### Task 3: Verify and deliver
- [x] Run regression CTest, GUI tests, package metadata checks and fresh independent review.
- [x] Commit locally, build final .deb files and checksums, provide direct artifact links with 3 mouse steps. Explicitly mark physical Pi and OS compatibility unverified.
