# Audyt źródła v0.4 — 2026-11-23

Źródło ffadec107af878424639c40bee979a838af4ca97; git fetch origin potwierdził HEAD.
Nowa gałąź codex/multifx-rt-engine-refactor utworzona od tego SHA. Poprzednia praca
Next jest na lokalnej feat/multifx-next-modular-audio-engine, 6988a10; nie jest
częścią źródłowej v0.4. Nie traktujemy opisów/Notion jako wykonanych zmian.

## Struktura i tor

Root CMake buduje opcjonalne historyczne hosty v0.1/v0.2/v0.4. v0.4 korzysta z
multifx2/api.h i 13 modułów, web_panel.py/panel.html oraz TCP localhost:9811.
Panel zapisuje chain/bpm/master, cab/synth i MIDI w konfiguracji użytkownika.
Audio: JACK mono in_1 -> gitara stereo -> sloty route=0 -> dwa stereo IR A/B
+ filtry -> miks z BassAmbient/slotami route=1 -> master/limiter -> out_1/out_2.
Callback multifx4/host.cpp:106-247. Stary GUI i usługi pozostają historyczne;
nowego WWW nie budujemy. Nie aktywujemy niczego na Pi.

## Potwierdzone defekty / koszty

| ID | Kod źródłowy | Ustalenie |
|---|---|---|
| R1 | host.cpp:210-217 | Bypass nadal wywołuje DSP; brak unload instancji. |
| R2 | host.cpp:223-237 | Dwa IR i filtry wykonują DSP bez względu na cabOn/mix/blend. |
| R3 | ir_cab.h:171-175 | Dwa exp na próbkę każdego filtra przy stałych ustawieniach bloku. |
| R4 | host.cpp:233-237, ir_cab.h:117-139 | Dry bieżący, wet opóźniony64: partial mix wymaga jawnej zmiany wyrównania i testu. |
| R5 | host.cpp:291-293 i224-227 | CAB publication/retirement miesza acq_rel/acquire z SC znacznikiem. Brak dowodu czasu życia w przenośnym modelu C++; dopuszczalny odczyt starego CAB po obserwacji idle. Nie odtworzono crasha sprzętowego. |
| R6 | host.cpp:161-167,430-439 | MIDI binding składa się z wielu atomików; czytelnik może zmieszać pola dwóch generacji. Atomiki nie oznaczają spójnego snapshotu. |
| R7 | host.cpp:148-158 | NoteOn/Off i PC są odrzucane; MIDI tylko CC sterowania. |
| R8 | api.h:23-26, reel_echo.cpp:10-20 | ABI2 per-sample, ponowne mapowanie pow/set i narzut dispatch; adapter zachowa kod, nie uczyni starego DSP blokowym. |
| R9 | host.cpp:538-549 | Pojedyncze recv/send nie gwarantują pełnej komendy/odpowiedzi; starej sieci nie przenosimy do RT. |
| R10 | tests.cpp:1-45 | Krótki monoIR, brak pełnych testów CAB A/B, współbieżności, unload i instrumentów. assert słabnie pod NDEBUG. |

## Korekta zarzutu grafu

current.exchange/load i processing.store/load w ścieżce grafu są SC.
Dla jednego serializowanego czytelnika i kontrolera: jeżeli audio odczyta stary
Graph przed exchange, jego true jest wcześniej w SC order. Odczyt false po
exchange oznacza zakończony odczyt; późniejszy callback nie odczyta starego Graph.
Nie wykazano UAF grafu/Slot w tych założeniach. Spin-wait kontrolera może jednak
czekać nieograniczenie przy ciągłym czytelniku, a publikacja jest w środku bloku.
Zmiana na pending/ack poprawia protokół i obejmuje CAB; nie nazywamy jej naprawą
odtworzonej awarii grafu. Dowód SC nie przenosi się na acquire odczyty CAB.

## Funkcje wymagające zachowania

13 efektów ABI2, kolejność, parametry/tempo/master, gitarowe presety, CAB dwa IR
mono/stereo, blend A/B, mix, ±18dB, low/high cut, MIDI CC oraz nowe instrumenty.
BassAmbient nie przechodzi do nowego targetu, lecz historyczny kod zostaje.
Migracja route=1/synth musi jawnie zgłosić wykluczenie, nie zmieniać cicho routingu.
Nowe dry latency64 jest zmianą kontraktu dla partial mix, nie bit-identycznym
zachowaniem starego miksu. Dwa IR nie mogą zniknąć jako koszt refaktoryzacji.

## Sprawdzenia bazowe

Linux x86_64, GCC14.2, CMake4.4.5, JACK SDK1.9.22. Root Debug z pluginOFF,
MULTIFX/MULTIFX2/MULTIFX4 ON: build i CTest4/4. Log evidence/refactor/baseline.log.
Brak Raspberry Pi/HeadRush; nie zmieniono 44100/64/3 ani usług.

Źródła odczytane: multifx4/{host.cpp,ir_cab.h,tests.cpp,web_panel.py,panel.html},
multifx2/{api.h,host.cpp,modules/*}, CMakeLists.txt, .github/workflows/multifx4-ci.yml,
deploy/systemd/jerzy-jack.service, Notion41a5fed1901b81e6879eefcdd61169d0.
Niezależne audyty pamięci i CAB potwierdziły rozróżnienie SC graf / mixed CAB
oraz brak dual CAB w prototypie6988a10. Kod PFFFT i referencje audytujemy oddzielnie.
