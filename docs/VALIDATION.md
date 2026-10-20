# Walidacja 0.1.1 — Tempo Sync

Lokalnie: 104 przypadki DSP, wszystkie 47 kontroli walidatora VST3, test hosta oraz GUI zakończone sukcesem. Nowe kontrole obejmują 21 podziałów przy 60/120/180 BPM, rzeczywisty impuls 600 ms we wszystkich sześciu modelach (bez przycinania do zakresów Free), BPM 100→150 przekazywane przez VST3 ProcessContext, odczyt starych sesji v1/v2 i klikanie Sync wraz z blokadą gałki ręcznego czasu.

Nie wykonywano testu w zainstalowanym FL Studio; sprawdzono standardowy interfejs VST3 host tempo używany przez DAW. Natywne kompilacje Windows x64 i Ubuntu 24.04, testy DSP i hosta, walidator VST3 oraz test GUI na Linux przeszły w [GitHub Actions 39471773350](https://github.com/jurekbak71-dev/JerzyMemoryDelay/actions/runs/39471773350), commit `91b7f82eec4fafbfecfcdea24960d65c6200914c`. Pakiety publikuje zadanie Release po zaliczeniu obu platform.

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
