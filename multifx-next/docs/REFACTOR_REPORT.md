# Raport refaktoryzacji — 2026-11-24

Status: implementacja i weryfikacja programowa; **akceptacja sprzętowa NIEZWERYFIKOWANA**.
To eksperymentalny silnik obok historycznych produktów, nie wydanie produkcyjne.

## WYKONANO

- Audyt rzeczywistego MultiFX4, korekta zarzutu UAF grafu SC oraz osobna analiza
  protokołu CAB i niespójnego snapshotu MIDI: REFACTOR_AUDIT.md.
- Jawny JFX3/block processing, RAII bibliotek, adapter niezmienionego ABI2,
  FX/instrument/generator i walidacja manifestów/ELF. Wykorzystano wcześniejszą
  lokalną implementację6988a10, z jawnym pochodzeniem w IMPORT_PROVENANCE.md.
- Publikacja pending/ack na granicy bloku, kontroler jako właściciel, odroczona
  destrukcja po ostatniej próbce rampy/ogona. ACTIVE/BYPASS/UNLOADED, rampy64,
  ograniczone ogony, rozgrzanie i crossfade podmiany IR. STATE_TRANSITIONS.md.
- Pełny cab_dual: A/B, mix/blend, poziom ±18dB, low/high-cut, cached coefficients,
  zatrzymanie nieużywanych konwolucji, jawny aligned/legacy dry. DUAL_CAB.md.
- Spójne mapowania CC, zmiany parametrów w czasie zdarzenia, master i BPM,
  snapshot stanu MIDI, trwała kolejność parametrów wyładowanego pluginu.
- Migracja gitarowych presetów v0.4, zachowanie13 efektów i jawne ostrzeżenia
  o wykluczeniu BassAmbient/synth. MIGRATION_V04.md i LEGACY_CATALOG.md.
- Pomiar histogramów DSP/backend, CPU/RSS/PSS/XRUN i analiza synchronicznego WAV
  loopback; brak fizycznego pomiaru oznacza null, nie liczbę zastępczą.
- Opcjonalny root target JERZY_BUILD_MULTIFX_RT, CI x64/ARM64 Debug/Release
  i osobne sanitizery, bez wdrożenia. Testowe ZIP-y Windows→RPi JACK2 i ALSA,
  pełny katalog13 ABI2, kontrola integralności, izolowana instalacja użytkownika.

## ZWERYFIKOWANO

- Własność i współbieżność: przegląd kodu, stress grafu/IR/parametrów/stop-start,
  ASan/UBSan/TSan, skalibrowane obserwowanie malloc/free w badanych callbackach.
- Faktyczne DSP13 modułów: bezpośredni ABI2 kontra adapter, trzy fazy parametrów
  i BPM, ponowne zimne ładowanie; wszystkie próbki identyczne w badanej trajektorii.
- CAB:162 przypadki dualCAB; mono/stereo, krótkie/maks32768, 24 warianty kodowania
  i resamplingu,7 punktów DFT, wet/dry/latencja, brak NaN/Inf. IR_FORMAT_TESTS.md.
- Naprawy zgłoszone w końcowym przeglądzie: ALSA nie maskuje BPM sesji jako
  poprawny transport zewnętrzny; UNLOADED przechowuje jawny porządek parametrów;
  benchmark odrzuca NaN/Inf i zapisuje minimum2 rzeczywiste próbki;
  instalator sprawdza nazwane moduły+JSON i licencje obu CAB.
- Sprawdzone źródła nowego src/include/plugins nie zawierają zależności BassAmbient.
  Zachowano historyczne silniki; nie zmieniono usług ani domyślnego Core44100/64/3.

## TESTY

Środowisko: Linux x86_64 Debian13, GCC14.2, CMake4.4.5, JACK1.9.22,
ALSA SDK, sterowniki dummy/null. ARM64: cross GCC14 + QEMU cortex-a72;
to emulacja funkcjonalna, nie Raspberry Pi i nie pomiar NEON na urządzeniu.

| Konfiguracja | Rzeczywisty wynik |
|---|---|
| Historyczny root Debug przed zmianami | 4/4 |
| Next Debug po poprawkach przeglądu | 32/32 |
| Root Release, stare+nowe targety | 36/36 |
| Next ASan + UBSan | 30/30, bez zgłoszonych błędów |
| Next TSan | 30/30, bez zgłoszonych wyścigów |
| Next ARM64/QEMU | 22/22 |
| Historyczny Python panel | 3/3 |
| Historyczne metadane pluginów | 13/13 |
| Migracja | 14 przypadków, włączone w CTest |
| Narzędzia benchmarku | 4 przypadki, włączone w CTest |
| ZIP/PE/checksums i izolowana instalacja natywna | oba backendy; nie Windows/SSH/Pi |

Polecenia: `cmake --build BUILD -j2`, `ctest --test-dir BUILD --output-on-failure`;
root z `-DJERZY_BUILD_MULTIFX_RT=ON -DNEXT_ALSA=ON` oraz zachowanymi targetami;
Next sanitizery: `-DNEXT_SANITIZER=address,undefined` lub `thread`.
Pełne opcje toolchain są w cmake/arm64-linux.cmake; środowisko wymaga właściwego
sysroot i emulatora. Reprodukcja instalacji: tests/windows_remote_test.py ZIP.
Logi: docs/evidence/refactor/*.log; raporty JSON benchmarków obok.

Obserwowane wcześniejsze błędy: wymuszone120BPM ALSA, przestawienie parametrów
UNLOADED, jednopróbkowy krótki benchmark, brak źródeł katalogu/MemoryDelay w ZIP.
Dodane regresje najpierw odrzuciły poprzednie zachowanie, potem przeszły.
Pierwszy starszy przebieg TSan miał1 nieudany test narzędzia (za mało próbek),
nie raport wyścigu; po poprawce pełny przebieg30/30.

Pomiar offline x86, CAB32768, 10000 bloków64 po rozgrzaniu:

| Stan | CPU procesu na blok | Wywołania pluginu | Śledzona pojemność |
|---|---:|---:|---:|
| ACTIVE | 34562 ns | 10000 | 1058552 B |
| BYPASS | 895 ns | 0 | 1058552 B |
| UNLOADED | 862.5 ns | 0 | 4584 B |

Reload preparation+8 bloków:1.354ms w tym jednym przebiegu, nie gwarancja.
Osobny resource test: zwolniono1053968B śledzonej pojemności, mmap1064960→0.
Metadane i opóźniony dry pozostają. To nie zerowy RAM ani inwentaryzacja wszystkich
alokacji. FFT mediany stare/PFFFT128/1024/8192/32768:7.47/7.14/6.74/5.70.
Pomiary wykonano w dzielonym środowisku, równolegle z innymi testami: maksima i
czasy nie są gwarancją RT ani dowodem przewagi na Pi. Źródło harness: tests/mode_bench.cpp.

## NIEZWERYFIKOWANE

Pi4/HeadRush Core, faktyczna wersja systemu użytkownika, priorytet RT i limity
systemu, USB, temperatury/throttling,30/60min stabilność, fizyczny loopback,
rzeczywisty p95/p99 na urządzeniu i przewaga JACK2/ALSA. Rzeczywiste okno Windows,
SSH do Pi i jego zależności również nie były uruchamiane. GitHub Actions wymaga
osobnego potwierdzenia uruchomienia w GitHub; plik workflow nie jest wynikiem CI.

## NIEWYKONANE

Akceptacja sprzętowa i wdrożenie produkcyjne: brak dostępu, zakaz automatycznego
wdrożenia. Pełny GUI MultiFX4 nie został przeniesiony: istniejący launcher testowy
na Pi kontroluje gain; zaawansowane nowe funkcje mają interfejs hosta. Brak nowego
WWW/Vercel zgodnie z zakresem. Narzędzie loopback analizuje rzeczywisty WAV,
ale nie podłącza przewodu i nie wykonuje automatycznie fizycznego nagrania.

Ryzyka: dowolny obcy plugin może naruszyć kontrakt RT; testy nie dowodzą całego
C++ dla wszystkich pluginów. ABI2 przywraca parametry, nie wnętrze linii delay.
Rampy zmniejszają skok, nie gwarantują niesłyszalności dla dowolnego sygnału;
REMOVE/CLEAR nie zachowuje ogona. MIDI-capable plugin wymaga nowej instancji przy
wznowieniu. Historię uśpionego IR zachowujemy, nie resetujemy jej magicznie.

## ŹRÓDŁA

Analizowany kod i wskazania linii: REFACTOR_AUDIT.md; własność/ABI: ARCHITECTURE.md,
PLUGIN_FORMAT.md, include/jfx.h i STATE_TRANSITIONS.md. Źródła porównawcze/licencje: AUDIT.md oraz
BACKEND_COMPARISON.md. Pobrane źródła, adresy i SHA256: evidence/refactor/reference-sources.json.
Wymagania: załączony tekst użytkownika oraz
https://app.notion.com/p/41a5fed1901b81e6879eefcdd61169d0 .

## GIT

Repo: https://github.com/jurekbak71-dev/JerzyMemoryDelay .
Baza: feature/jerzy-multifx-v04-dual-ir-midi, ffadec107af878424639c40bee979a838af4ca97.
Gałąź robocza: codex/multifx-rt-engine-refactor.
Etapy:52623ae audyt,51b60cf ponowne użycie jawnego JFX3,9ed6939 rampy/dualCAB.
Dalsze commity, synchronizacja i adres Draft PR są podane w końcowym raporcie
sesji po rzeczywistym wykonaniu publikacji. Bez merge, force-push i wdrożenia.
