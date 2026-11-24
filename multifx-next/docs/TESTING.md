# Weryfikacja — 2026-11-17

Aktualizacja CAB/ARM64: [CAB_OPTIMIZATION.md](CAB_OPTIMIZATION.md), wyniki i komendy
w `evidence/cab-verification.json`. Poniższa sekcja zachowuje historyczne wyniki
pierwszego prototypu z commitu `c7c616e`; jej ograniczenie „brak cross-build” zostało
później usunięte. Fizyczne Pi4/HeadRush nadal pozostają nieweryfikowane.

## Środowisko i zakres

Linux x86_64, GCC 14.2.0, CMake 4.4.5, JACK2 1.9.22. CMake i pakiety deweloperskie
JACK rozpakowano lokalnie pod `/workspace/work`; nie instalowano usług systemowych,
nie zmieniano konfiguracji Raspberry Pi ani nie podłączano urządzenia USB.

**Nie było dostępu do Raspberry Pi 4 ani HeadRush Core.** Nie wykonano fizycznego
round-trip, testów na ARM64 ani porównania JACK/ALSA na tym samym urządzeniu.
W środowisku nie znaleziono `aarch64-linux-gnu-g++` ani `clang++`; nie raportujemy
cross-build jako przeprowadzonego. Natywne instrukcje ARM64 są w README.md.

## Wyniki pierwszego prototypu (c7c616e)

Debug **9/9**, Release **9/9**, ASan/UBSan/LSan **9/9**, TSan **9/9**.
Wszystkie buildy zakończyły się sukcesem przed uruchomieniem testów. NextJACK ładuje
efekt z rzeczywiście spakowanej i zainstalowanej paczki `.jfx` podczas działania hosta.
[verification.json](evidence/verification.json) zawiera skróty źródeł i zestawienie;
pełne logi są w evidence (także przebiegi RED oznaczone jako review-red).
Niezależny przegląd i rozstrzygnięcia: [REVIEW.md](REVIEW.md).

## Zestawy testów

| CTest | Rzeczywiście sprawdzane zachowanie |
|---|---|
| NextPlugins | natywne `.so`, stereo gain, sample-offset Note On/Off, velocity=0, CC7, PC; brak pliku/symbolu, niezgodny magic/major/rozmiar, null process, NaN, porty/limity/flags; biblioteka współdzielona; CAB wet względem starej konwolucji i wyrównanie dry/wet |
| NextEngine | niezależne L/R, rzeczywiste próbki serial/parallel/reorder; miks FX+instrument, routing MIDI, panic, cycle/overflow/channel validation, latency compensation, oversize; 2000 równoległych zmian i shutdown z pending |
| NextSession | ADD/REMOVE/MOVE i parametry; ACTIVE/BYPASS/UNLOADED; brak starych głosów po reaktywacji; preset save/load i rollback przy błędzie; parametry wyładowanego slotu, niepoprawne ID i zwolnienie ostatniego library owner |
| NextPackages | rzeczywiste pack/install, powtórna instalacja bez nadpisania, traversal/linki, błędny hash/ELF; PAX/GNU z ukrytym payloadem ponad 64 MiB |
| NextLegacy | ABI2 tremolo przez adapter i bezpośrednio: zgodność stereo próbek do 1e-7 |
| NextResources | IR 32768 próbek, stan przed/po unload, śledzona pojemność, RSS i mallinfo2 mmap; zwolnienie ponad 2 MiB zasobów CAB |
| NextReviewRegressions | Note Off po zmianie maski routingu, diagnostyka związana z instancją zamiast pozycją na liście |
| NextOverload | rzeczywisty, celowo kosztowny i ograniczony DSP; wykrycie przekroczenia budżetu 1 próbki / 192 kHz, skończone audio po przekroczeniu |
| NextJACK | osobny serwer dummy i drugi natywny klient; rzeczywisty stereo passthrough/gain, MIDI audio i Note Off, 200 zmian trybu, utrata Note Off w JACK mixdown przez duży SysEx i panic |

Testy C++ używają kontroli niezależnych od `NDEBUG`; Release nie wyłącza asercji testowych.
Próby błędnych ABI są celowo niepoprawnymi bibliotekami testowymi, nie atrapami produktu.

## Komendy

Z katalogu repozytorium, po dostarczeniu zależności JACK:

```sh
cmake -S multifx-next -B work/next-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build work/next-debug --parallel 2
ctest --test-dir work/next-debug --output-on-failure

cmake -S multifx-next -B work/next-release -DCMAKE_BUILD_TYPE=Release
cmake --build work/next-release --parallel 2
ctest --test-dir work/next-release --output-on-failure

cmake -S multifx-next -B work/next-asan -DNEXT_SANITIZER=address,undefined -DCMAKE_BUILD_TYPE=Debug
cmake --build work/next-asan --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir work/next-asan --output-on-failure

cmake -S multifx-next -B work/next-tsan -DNEXT_SANITIZER=thread -DCMAKE_BUILD_TYPE=Debug
cmake --build work/next-tsan --parallel 2
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir work/next-tsan --output-on-failure
```

Sanitizery to osobne buildy. Testy instrumentują także pluginy i host JACK; daemon JACK
pozostaje biblioteką/programem systemowym bez instrumentacji. Zaliczenie TSan nie dowodzi
braku race we wszystkich ścieżkach, a ASan/LSan nie dowodzi braku wszystkich wycieków.

W normalnym buildzie NextEngine ma testowe przechwytywanie glibc
malloc/calloc/realloc/free/aligned allocation, obejmujące również C++ new/delete także dla kodu w `.so`; odnotowano zero
wywołań alokacji/zwalniania w obserwowanym wątku audio. Kalibracja wykonuje rzeczywiste
C++ new/delete i wymaga niezerowych liczników przed ich wyzerowaniem do pomiaru. Te hooki są wyłączone w buildach
sanitizowanych, żeby nie zastępować interceptorów sanitizera. Nie są częścią hosta.
To nie jest ogólny audyt wszystkich syscalli ani dowolnych zewnętrznych pluginów.

## Wykryte błędy i poprawki

- Graniczny indeks kanału `UINT32_MAX` omijał walidację przez overflow dodawania.
  Test RED, następnie sprawdzanie zakresu przed działaniem arytmetycznym i GREEN.
- UNLOADED wymaga zachowania zakresów parametrów niezależnie od biblioteki.
  Test RED dla SET poza zakresem, zachowanie metadanych zakresu i GREEN.
- ASan wykrył alloc-dealloc-mismatch w testowym nadpisaniu delete przy nothrow new
  pochodzącym z interceptora ASan. Rozdzielono build pomiaru alokacji i sanitizerów;
  nie wyłączano kontroli błędu przez alloc_dealloc_mismatch=0.
- Niezależny przegląd: zmiana maski MIDI zostawiała głos bez Note Off. Test RED;
  nowy plan wysyła All Sound Off na usuwane kanały przed dalszymi zdarzeniami.
- Przegląd: PAX/GNU headers były przetwarzane przed limitami instalatora. Test RED
  dla skompresowanego ukrytego payloadu >64 MiB; parser odrzuca extension headers
  przed odczytem ich treści, a strumień zdekompresowany ma limit.
- Przegląd: brak jack_midi_get_lost_event_count. Test z realnym JACK mixdown zgubił
  Note Off i odnotował tail_variation=0.0375839 (RED). Po uwzględnieniu licznika
  straty uruchamia się panic, wyjście wraca do stałego sygnału (GREEN).
- Diagnostyka po MOVE była przypisana do pozycji. Test RED; liczniki przeniesiono
  do instancji, STATS publikuje slot_ids; GREEN.

Logi RED nie są końcowymi wynikami. Wczesny przebieg CTest uruchomiony po błędzie
kompilacji testu sanitizowanego nie został uznany za dowód; końcowe komendy zatrzymują
się na błędzie budowy przed uruchomieniem CTest.

## Pamięć i czas

`owned_bytes_estimate` opisuje pojemności znanych obiektów/buforów, a nie dokładny
rozmiar alokacji procesu. Przy pending może liczyć współdzielone zasoby obu planów;
nie używać go jako miary peak RSS. Legacy DSP nie raportuje pełnej pamięci.
RSS odczytujemy z `/proc/self/statm` poza callbackiem; allocator może zatrzymywać pamięć.
UNLOADED zachowuje metadane oraz hostowe bufory routingu/kompensacji, nie oznacza zera RAM.

W jednym wykonanym przebiegu Debug NextResources: zwolniono 2 100 912 bajtów śledzonej
pojemności; RSS 4 804 608 -> 2 568 192 B, mmap heap 2 113 536 -> 0 B.
Ten sam długi IR w Debug przekroczył deadline 64/44100; licznik to wykrył.
W jednym wcześniejszym przebiegu Release callback trwał maksymalnie 225 875 ns,
bez przekroczenia deadline w tym krótkim teście. To pomiary jednostkowego przebiegu
x86_64, nie benchmark Pi, p99 ani obietnica czasu scenicznego. Końcowy log zasobów
jest zachowany oddzielnie w evidence.

JACK dummy pracuje bez gwarancji RT. Pod obciążeniem kompilacji/sanitizerów występują
XRUN, które są raportowane; test nie wymaga zera XRUN i nie dowodzi stabilności przy 64
próbkach na sprzęcie. Nie wyprowadzamy round-trip z rozmiaru bloku i liczby okresów.

## Regresja starego projektu

Po dostarczeniu lokalnych nagłówków/lib JACK wykonano istniejący build CMake z
`JERZY_BUILD_PLUGIN=OFF`, `JERZY_BUILD_MULTIFX=ON`, `JERZY_BUILD_MULTIFX2=ON`,
`JERZY_BUILD_MULTIFX4=ON` oraz CTest: 4/4 (MemoryDelayDSP, MultiFXDSP,
MultiFX2Modules, MultiFX4DualSynthIR). Nie budowano VST3/GUI/SDK.
Poprzedni etap audytu zawiera też wyniki 13 modułów, metadanych, sześciu testów
Python i trzech starych zestawów ASan/UBSan: evidence/baseline-results.json i
reproduced-results.json. Odtwarzanie: `evidence/run_baseline.py --output KATALOG`.

## Ograniczenia / zadania pozostałe

- Fizyczne Pi4/HeadRush: ARM64 wykonanie, round-trip, CPU/RAM, długie sesje i obciążenie USB.
- Bezpośredni backend ALSA i porównanie na tym samym urządzeniu; nie stwierdzono jego przewagi.
- PFFFT/NEON zostało później wdrożone i przetestowane pod QEMU — zobacz aktualizację CAB.
  Pozostaje fizyczny benchmark ARM64 oraz ewentualne porównanie FFTW.
- Bezklikowe zmiany topologii, ogony przy bypass, odtwarzanie głosów, SysEx i MIDI output.
- Pełny opaque DSP state i automatyczny importer całych historycznych presetów;
  obecnie sesja zapisuje konfigurację/parametry, migracja ABI2 przez jawny adapter.
- Niezaufane pluginy nie są izolowane procesowo; dowolny natywny kod może uszkodzić host.
- Historyczny nagłówek IR emituje ostrzeżenie misleading-indentation na linii 87;
  nie modyfikowano go w ramach tej gałęzi.
