# Walidacja 0.1.0 — 20.10.2026

Potwierdzone w bieżącym środowisku Debian 13 x86_64, GCC 14.2, CMake 3.31.6:

- Kompilacja Release VST3 z właściwym edytorem VSTGUI i zasobem `memory.uidesc`.
- Walidator Steinberg: **47 passed, 0 failed**.
- `MemoryDelayDSP`: **28 przypadków**, m.in. impulsy sześciu profili przy 44.1/48/96 kHz, czas echa, izolacja kanałów, bypass, gain, wysokie sprzężenie, zmiany czasu, NaN/Inf i reset.
- Te same 28 przypadków DSP z AddressSanitizer i UndefinedBehaviorSanitizer: bez zgłoszeń.
- `MemoryDelayHost`: stan procesora i kontrolera, odrzucenie uciętego stanu bez częściowej zmiany parametrów, zgodne ustawienia domyślne, mono/stereo, float/double, przetwarzanie in-place i automatyka z offsetem próbki.
- `MemoryDelayGUI`: prawdziwy edytor w Xvfb, render panelu, dwa kliknięcia bypassu z wysłaniem automatyki do hosta, rozwinięcie szczegółów, aktywny Boost EH7500, nieaktywny Boost Deluxe oraz usunięcie timerów i handlerów przy zamknięciu.
- `ctest`: **3/3 suites passed**. Zrzuty `gui-deluxe.png` i `gui-details.png` pochodzą z tego testu, a nie z makiety.
- Ponowne wykonanie `scripts/setup_cloud.sh` oraz pełnego `scripts/build_cloud.sh` zakończyło się powodzeniem.

## Granice walidacji

Nie wykonano odsłuchu porównawczego ani pomiarów oryginalnych kostek. Boy/Toy i część parametrów vintage są profilami projektowymi — patrz MODELLING.md.

Windows x64: natywna kompilacja Visual Studio 2022 na GitHub Actions zakończyła się powodzeniem. Przeszły etap budowania (z walidatorem Steinberga), testy DSP i hosta, kontrola obecności zasobu GUI oraz pakowanie kompletnego bundle VST3. Dowód: [przebieg 39388814659](https://github.com/jurekbak71-dev/JerzyMemoryDelay/actions/runs/39388814659), commit `7a660a5e1c836b5efb074164b50824d6db4bc23a`. Także świeża kompilacja Ubuntu 24.04 i wszystkie trzy zestawy testów przeszły w tym przebiegu. Test klikania i renderowania GUI wykonano na Linux; Windows GUI skompilowano i spakowano, lecz nie uruchamiano tam osobnego testu interakcji. Nie wykonano jeszcze testu w konkretnym DAW użytkownika. Próba kompilacji krzyżowej MinGW była nieudana z powodu zależności SDK od MSVC; gotowy pakiet Windows pochodzi z natywnego runnera Microsoftu.

Pakiet Linux jest zbudowany na Debianie 13 i korzysta z systemowych bibliotek X11, Cairo, Pango, Fontconfig oraz XCB. Nie jest uniwersalnym pakietem dla wszystkich dystrybucji Linux.
