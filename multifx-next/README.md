# Jerzy MultiFX Next — eksperymentalny silnik RT

Rzeczywisty host C++20 z backendami JACK2 i bezpośrednim ALSA, stereofonicznym wejściem/wyjściem,
blokowym DSP i niezależnymi wtyczkami FX oraz instrumentami MIDI. Kod silnika znajduje się w `multifx-next/`; root CMake dodaje opcjonalny target,
a historyczne silniki pozostają zachowane.

Bieżący audyt: [REFACTOR_AUDIT.md](docs/REFACTOR_AUDIT.md).
Raport bieżących testów: [REFACTOR_REPORT.md](docs/REFACTOR_REPORT.md).
Historyczne wyniki prototypu: [TESTING.md](docs/TESTING.md).
Projekt nie jest jeszcze zweryfikowany scenicznie na Raspberry Pi 4 / HeadRush Core.
Testy dummy JACK na x86_64 nie są pomiarem latencji sprzętowej.

## Implementacja

- Pusty graf: oddzielny stereofoniczny passthrough L/R.
- Przygotowany poza audio graf DAG: 16 slotów, 64 połączenia audio i 64 routingi MIDI.
- Szeregowe i równoległe tory, miks wielu źródeł i mapowanie kanałów; wyrównanie latencji.
- Jedna oczekująca zmiana planu, przejęcie na granicy bloku i zwolnienie starego przez kontroler.
- ACTIVE: DSP; BYPASS: dry z wyrównaniem / cisza instrumentu; UNLOADED: brak instancji DSP.
- Rampy64 i odroczony unload; BYPASS FX zachowuje instancję, instrument MIDI odtwarza ją.
- Opcjonalne ograniczone ogony: [STATE_TRANSITIONS.md](docs/STATE_TRANSITIONS.md).
- Mapowania MIDI CC/master/BPM i spójny zapis: [MIDI_STATE.md](docs/MIDI_STATE.md).
- MIDI Note On/Off, CC7, All Sound Off/All Notes Off i Program Change we wtyczce instrumentu;
  routowanie według maski kanałów. Utrata MIDI wywołuje panic, także przy utracie w JACK mixdown.
- Tempo/transport JACK; BPM sesji jako fallback bez poprawnego JACK BBT.
- Presety JSON z kluczami parametrów, kolejnością, routingiem i stanami slotów; restore transakcyjny.
- Pomiary callbacku i instancji, stabilne ID slotów, deadline misses, XRUN, przepełnienia i RSS.
- `.jfx`: niezależnie pakowane biblioteki z metadanymi, SHA256 i kontrolą architektury ELF.
- Brak BassAmbient, jego padów/Krell, dedykowanego routingu i GUI.

Wtyczki demonstracyjne naprawdę przetwarzają/generują audio:

| ID | Działanie |
|---|---|
| `gain` | stereo `output = input * gain + offset`; parametry gain [0,2], offset [-1,1] |
| `synth` | 16 kanałów MIDI, jeden głos na kanał, wavetable sine / triangle przez Program Change, poziom [0,1] |
| `cab` | rzeczywista konwolucja partycjonowana IR WAV; 64 próbki opóźnienia, wewnętrznie wyrównany dry/wet |
| `generator` | niezależny generator110Hz bez wejścia i MIDI |
| `cab_dual` | dwa IR, blend/mix, level ±18dB, low/high-cut; [DUAL_CAB.md](docs/DUAL_CAB.md) |
| `legacy` | adapter FX ABI v2; `p0`…`p47` to stare parametry znormalizowane, asset to stare `.so` |

CAB 1.1.0 używa PFFFT real FFT (SSE na x86_64, NEON na ARM64), z przygotowanymi
poza audio buforami wyrównanymi do 64 bajtów. WAV i resampling zachowują zachowanie
`multifx4/ir_cab.h`; historycznego pliku nie zmieniono. Nie zawiera starych filtrów
low/high-cut ani podwójnego CAB w jednym module. Pełny gitarowy CAB realizuje
teraz osobny `cab_dual`; migracja v0.4 używa właśnie tego modułu.
Porównania próbek, CPU/RAM i ograniczenia: [CAB_OPTIMIZATION.md](docs/CAB_OPTIMIZATION.md).

Wersja 1.0.0 pozostaje osobną wersją paczki. Presety przypięte do 1.0.0 nie są po cichu
przekierowywane na 1.1.0; zachowaj starą paczkę albo świadomie utwórz nowy preset
z nową wersją i tymi samymi parametrami. Nie nadpisujemy zapisanych presetów.

Instalacja testowa z Windows: [TEST_INSTALLATION.md](docs/TEST_INSTALLATION.md).
Migracja presetów: [MIGRATION_V04.md](docs/MIGRATION_V04.md).
Benchmark Pi: [BENCHMARK_RPI.md](docs/BENCHMARK_RPI.md).

## Kompilacja Linux / natywna ARM64

Na oddzielnym środowisku testowym z kompilatorem C++20, CMake >=3.25, pkg-config,
JACK2 i jego nagłówkami; testy Python wymagają Python >=3.10. Typowe nazwy pakietów Debian:
`g++ cmake make pkg-config libjack-jackd2-dev jackd2 python3`.
Nie uruchamiaj instalacji ani skryptów deploy na działającym Raspberry Pi bez osobnej zgody.

```sh
cmake -S multifx-next -B work/next-release -DCMAKE_BUILD_TYPE=Release
cmake --build work/next-release --parallel 2
ctest --test-dir work/next-release --output-on-failure
```

Te same komendy służą natywnej kompilacji na ARM64; powstają wtedy wtyczki ARM64.
Binaria x86_64 nie działają na Pi. Dla cross-build trzeba dostarczyć prawdziwy toolchain,
sysroot oraz ARM64 JACK i ustawić `CMAKE_TOOLCHAIN_FILE`; sama flaga `-march` nie wystarcza.

Build bez JACK (ten sam rdzeń i pluginy, testy offline):

```sh
cmake -S multifx-next -B work/next-offline -DNEXT_JACK=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build work/next-offline --parallel 2
ctest --test-dir work/next-offline --output-on-failure
```

Test `NextJACK` jest dodawany, gdy CMake znajdzie `jackd`; uruchamia odrębny, nazwany
serwer dummy, bez połączeń z interfejsem USB. Gdy jackd nie jest znaleziony, testu nie ma —
nie należy wtedy raportować weryfikacji integracji JACK.

## Cross-build ARM64

Użyj świeżego katalogu budowy i sysroota pasującego do systemu docelowego. Przykład
z zainstalowanym toolchainem `aarch64-linux-gnu-gcc/g++`:

```sh
cmake -S multifx-next -B work/next-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/multifx-next/cmake/arm64-linux.cmake" \
  -DNEXT_ARM64_SYSROOT=/pełna/ścieżka/do/sysroota \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build work/next-arm64 --parallel 2
```

Dla lokalnie rozpakowanego toolchaina dostępne są `NEXT_ARM64_COMPILER_PREFIX`
(np. `/.../usr/bin/aarch64-linux-gnu-`) i `NEXT_ARM64_COMPILER_SUFFIX` (np. `-14`).
Gdy podasz sysroot, plik toolchain ustawia targetowe `PKG_CONFIG_LIBDIR` i
`PKG_CONFIG_SYSROOT_DIR` oraz czyści odziedziczony `PKG_CONFIG_PATH` w procesie CMake.
Bez sysroota ustaw te zmienne jawnie na katalogi ARM64; metadane JACK z hosta nie są
zależnościami targetu.

Wykonany cross-build użył GCC 14.2 i sysroota Debian/glibc 2.41. Host wynikowy wymaga
m.in. GLIBC 2.38 i GLIBCXX 3.4.32. **Nie zakładamy, że zadziała na starszym Pi OS.**
Kod ARM64 uruchomiono w testach QEMU Cortex-A72, co nie zastępuje fizycznego Raspberry Pi
ani testu interfejsu HeadRush. Benchmark QEMU nie służy do oceny wydajności Pi.

## Uruchomienie i sterowanie

Host wymaga istniejącego serwera JACK. Nie startuje go i nie przepina portów samodzielnie:

```sh
work/next-release/JerzyMultiFXNext
```

Po `READY` wpisuj komendy na stdin. Odpowiedzi: `OK ...` lub `ERR ...`.
`OK` po edycji oznacza zakolejkowany plan; `STATS` podaje `pending`.
Kolejna edycja przed potwierdzeniem może zwrócić `ERR busy: ...`; ponów ją po następnym bloku.
Porty: `JerzyMultiFXNext:in_1`, `in_2`, `out_1`, `out_2`, `midi_in`.
Wybierz rzeczywiste porty HeadRush/MIDI przez `jack_lsp` i połącz je jawnie przez `jack_connect`.

Przykład (dopasuj ścieżki, numery ID odczytaj z odpowiedzi ADD):

```text
ADD work/next-release/plugins/gain/plugin.so
SET 1 gain 0.5
ADD work/next-release/plugins/synth/plugin.so
MODE 1 BYPASS
MODE 1 UNLOADED
MODE 1 ACTIVE
MOVE 2 0
SAVE work/live.json
LOAD work/live.json
STATS
QUIT
```

Automatyczny tor łączy FX szeregowo; wszystkie instrumenty są miksowane do wejścia
pierwszego FX, a gdy go nie ma — do wyjścia. Instrument otrzymuje MIDI wejściowe na
wszystkich kanałach. MOVE przebudowuje taki tor. Dla własnego grafu:

```text
EDGES_CLEAR
CONNECT 0 1 1
CONNECT 1 -1 1
CONNECT 2 -1 1
ROUTE 0 2 1
```

`0` = wejście urządzenia, `-1` = wyjście, dodatnie liczby = ID slotów.
Tu instrument 2 trafia bezpośrednio do wyjścia, FX 1 obrabia tylko wejście audio,
a maska MIDI `1` wybiera kanał 1 (`2` kanał 2, `65535` wszystkie).
`CONNECT from to gain [source_channel target_channel channels]` pozwala mapować kanały
0/1; domyślnie obejmuje oba. `DISCONNECT from to` usuwa połączenia między parą węzłów.
`AUTO` przywraca tor automatyczny. W grafie własnym MOVE zmienia porządek listy slotów,
nie przepisuje jawnie zapisanych połączeń. Cykle są odrzucane.

Pozostałe komendy: `PING`, `LIST`, `REMOVE id`, `CLEAR`, `BPM 120`.
Ścieżki ze spacjami umieszczaj w podwójnych cudzysłowach.
Zmiana częstotliwości JACK lub przekroczenie maksymalnego bloku powoduje bezpieczną
ciszę i zakończenie hosta z informacją o potrzebie ponownego przygotowania.

## Instalowanie z pliku

```sh
python3 multifx-next/tools/jfx_package.py pack work/next-release/plugins/gain work/gain.jfx
python3 multifx-next/tools/jfx_package.py install work/gain.jfx work/installed-plugins
```

Instalator wypisuje ścieżkę `.so` do komendy ADD. Nie nadpisuje istniejącej wersji.
Działa poza audio. Szczegóły: [PLUGIN_FORMAT.md](docs/PLUGIN_FORMAT.md).
CAB wymaga dodatkowo ścieżki do prawdziwego IR WAV jako argumentu ADD:

```text
ADD work/next-release/plugins/cab/plugin.so /pełna/ścieżka/ir.wav
```

Migracja istniejącego modułu FX:

```text
ADD work/next-release/plugins/legacy/plugin.so /pełna/ścieżka/tremolo.so
SET 1 p0 0.33
SET 1 p1 0.55
```

Ustaw wszystkie używane parametry według starego manifestu/presetu, w tej samej kolejności;
default adaptera `.5` nie oznacza defaultu starego efektu. Stare sekcje synth/BassAmbient
nie są importowane. Adapter nie usuwa kosztu per-sample DSP, nie zna latencji ani
pełnego rozmiaru pamięci starego modułu. To ścieżka migracji, nie optymalizacja.

## Dokumentacja i pochodzenie

[ARCHITECTURE.md](docs/ARCHITECTURE.md), [AUDIT.md](docs/AUDIT.md),
[TESTING.md](docs/TESTING.md), [IMPLEMENTATION_LEDGER.md](docs/IMPLEMENTATION_LEDGER.md).
PFFFT: commit `efc1dd2de6c3562fc464f905d37d9281f08a20dd` + jawna poprawka obsługi braku
pamięci; licencja i notices w `vendor/pffft/` oraz paczce CAB.
JSON: nlohmann/json 3.11.3, [MIT](vendor/LICENSE.json.MIT), źródło
https://github.com/nlohmann/json/tree/v3.11.3. PiPedal i mod-host były materiałem
porównawczym; ich kodu nie kopiowano. Nie publikowano push/merge/release.

## Instalowanie RPi z Windowsa

Użyj ZIP **Jerzy-RPi-jack2-Windows** albo **Jerzy-RPi-alsa-Windows**. Rozpakuj cały
ZIP na Windowsie, połącz się z hotspotem Jerzy-MultiFX, otwórz START.cmd, wpisz
hasło do Pi i kliknij Zainstaluj. Adres 10.42.0.1 i użytkownik jerzy pochodzą
z historycznej instrukcji deploy/multifx4/README.md. Pi nie musi mieć internetu,
ale musi mieć zależności kompilacji i Tk. Po instalacji ikona panelu jest w menu RPi.
Nie dodano nowego panelu WWW. Pełna ścieżka z prawdziwego Windowsa na Pi pozostaje
NIEZWERYFIKOWANA; zakres testów i instrukcja: docs/WINDOWS_RPI_INSTALLERS.md oraz
installer/windows/CZYTAJ.txt. Pliki .deb z poniższej sekcji otwiera się na Linuksie.

## Instalacja myszką

Dwa pakiety `.deb` dodają osobne aplikacje **Jerzy MultiFX Next JACK2** i **ALSA**
do menu Dźwięk i obraz. Otwórz pobrany `.deb` dwuklikiem i użyj systemowego przycisku
Zainstaluj. Następnie uruchom ikonę z menu, kliknij Zainstaluj i po zakończeniu
Uruchom audio. Terminal nie jest potrzebny. W oknie jest także Zatrzymaj i test gain.
Instrukcja: `installer/KLIKNIJ-PRZECZYTAJ.txt`; stan testów: `docs/CLICK_INSTALLERS.md`.
Dla budującego paczki: `python3 multifx-next/tools/build_click_installers.py --output katalog`.
Instalacja przez menedżer pakietów na fizycznym Pi pozostaje NIEZWERYFIKOWANA.

## Dwie instalki do porównania JACK2 / ALSA

`python3 multifx-next/tools/build_installers.py --output /wybrany/katalog`
buduje dwa samodzielne archiwa `jerzy-multifx-next-{jack2,alsa}-source.tar.gz`.
Po rozpakowaniu: `python3 install.py` sprawdza integralność i zależności,
`python3 install.py --install` kompiluje wybrany backend natywnie i instaluje
w osobnym katalogu użytkownika. Instalacja nie otwiera audio ani nie zmienia usług.
Wymagane CMake >=3.25, C++20, Python >=3.10 i nagłówki JACK2 lub ALSA.
Szczegóły uruchomienia i A/B: `installer/README-PL.txt` (wewnątrz paczki `README-PL.txt`).

Bezpośrednie ALSA można także zbudować flagami `-DNEXT_ALSA=ON -DNEXT_JACK=OFF`.
Program `JerzyMultiFXNextALSA --help` pokazuje opcje urządzeń, rate/bloku/okresów,
mapowania kanałów (1-based) i MIDI. Domyślnie Core 44,1 kHz, 64/3, 6 capture/4 playback.
Dwa różne fizyczne urządzenia bez wspólnego zegara nie są obsługiwane: brak korekty driftu.
Formaty FLOAT_LE, S32_LE, S24_LE, S24_3LE i S16_LE negocjowane niezależnie.
Bez cichej zmiany rate/bloku/liczby kanałów; brak obsługi ustawienia kończy start błędem.
STATS podaje ustawienia, formaty, mapy, bufor, linked, realtime i dostępność MIDI.
Próba SCHED_FIFO 20 dotyczy wyłącznie własnego wątku; nie zmienia limitów systemu.

MIDI sequencer ma oddzielny wątek i stałą kolejkę SPSC. Engine nie wywołuje ALSA.
Note On/Off, CC, Program Change trafiają na początek kolejnego bloku; to nie jest
sample-accurate timestamping. `--midi required` blokuje start bez sequencera;
`auto` może uruchomić samo audio (`midi_available=false`). Przepełnienie wysyła panic.
Do HeadRusha z ALSA przejdź dopiero po ręcznym zatrzymaniu używającego go JACK-a.

Testy NextPCM, NextMidiQueue, NextALSA i NextALSAAudio sprawdzają konwersję,
współbieżność, start/stop, rzeczywiste file PCM ALSA, gain i mapowanie 6→4.
Żaden z nich nie potwierdza stabilności USB ani round-trip na fizycznym Pi.
