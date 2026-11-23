> Historyczny audyt prototypu (2026-11-17). Aktualny audyt: REFACTOR_AUDIT.md.
> Zarzut UAF grafu wymaga korekty: protokół SC jest bezpieczny przy jednym czytelniku;
> nie należy utożsamiać go z mieszanymi porządkami CAB.

# Audyt MultiFX v0.4 i v0.2

Data: 2026-11-17. Analiza źródeł z commitu
`ffadec107af878424639c40bee979a838af4ca97`.
Środowisko testów: Linux x86_64, GCC 14.2.0. Brak dostępu do Raspberry Pi 4 / HeadRush Core.
POTWIERDZONE oznacza obserwację kodu lub wykonanego testu; HIPOTEZA oznacza
możliwy skutek wymagający pomiaru. Audyt nie jest dowodem poprawności całego programu.

## Ustalenia

| ID / znaczenie | Dowód w kodzie | Ustalenie i konsekwencja |
|---|---|---|
| A01 / koszt bypass | [host4:206–218](../../multifx4/host.cpp#L206), [host2:100–110](../../multifx2/host.cpp#L100) | POTWIERDZONE: `process` wywoływany bezwarunkowo; enable steruje tylko crossfade. Wyłączenie efektu nie zwalnia pamięci ani nie pomija DSP. |
| A02 / koszt CAB | [host4:220–237](../../multifx4/host.cpp#L220) | POTWIERDZONE: obie istniejące instancje IR wykonują tick i filtry niezależnie od cabOn, wet oraz blend. Dotyczy też IR z zerową wagą. HIPOTEZA: duży wkład do XRUN przy długich IR; brak profilu Pi. |
| A03 / wyrównanie dry/wet | [ir_cab:117–139](../../multifx4/ir_cab.h#L117), [host4:233–237](../../multifx4/host.cpp#L233) | POTWIERDZONE: konwolucja oddaje poprzedni blok 64 próbek, dry w miksie pozostaje bieżący. Brak wyrównania opóźnienia. Dla impulsowego IR i częściowego wet oba impulsy są przesunięte; zmiana IR nie wymaga tego problemu, wystarczy sam tor. Test istniejący sprawdza wet, nie miks dry/wet. |
| A04 / filtr CAB | [ir_cab:169–176](../../multifx4/ir_cab.h#L169), [host4:223–227](../../multifx4/host.cpp#L223) | POTWIERDZONE: dwa exp na kanał/IR/próbkę, mimo stałych częstotliwości w bloku. Dwa stereo IR oznaczają osiem obliczeń exp na ramkę; współczynniki można przygotować raz na zmianę. |
| A05 / FFT | [ir_cab:20–39](../../multifx4/ir_cab.h#L20), [ir_cab:96–137](../../multifx4/ir_cab.h#L96) | POTWIERDZONE: własny radix-2 FFT 128 punktów, partycje 64, tablice complex<float>, sin/cos korzeni przy każdym FFT; sumowanie wszystkich partycji. Przy 32768 taps jest 512 partycji na kanał. Brak jawnego NEON. Brak pomiaru czasu, brak dowodu przewagi innej biblioteki. |
| A06 / wymiana grafu | [host4:106–109](../../multifx4/host.cpp#L106), [host4:246–254](../../multifx4/host.cpp#L246), [host2:77–80](../../multifx2/host.cpp#L77), [host2:120–125](../../multifx2/host.cpp#L120) | POTWIERDZONE: callback ustawia processing przed odczytem current; kontroler SC-exchange, czeka na processing=false, usuwa stary graf. Przy jednym niereentrantnym callbacku i jednym kontrolerze SC porządek chroni poprzedni graf: callback, który załadował stary graf, musi zakończyć się przed obserwacją false; callback rozpoczynający się po tej obserwacji odczyta nowy graf. Nie stwierdzono dowodu UAF tego schematu. To nie jest ogólne RCU i nie obsługuje wielu czytelników. |
| A07 / zasoby slotu | [host4:41–54](../../multifx4/host.cpp#L41), [host4:313–324](../../multifx4/host.cpp#L313) | POTWIERDZONE: remove publikuje nowy graf, czeka, dopiero usuwa unique_ptr i wywołuje destroy/dlclose poza callbackiem. UNLOADED nie istnieje. Każdy slot dlopen; systemowy refcount bibliotek przeciwdziała przedwczesnemu faktycznemu wyładowaniu tej samej biblioteki. Nie wolno utożsamiać dlclose ze spadkiem RSS. |
| A08 / CAB lifetime | [host4:280–294](../../multifx4/host.cpp#L280), [host4:357–363](../../multifx4/host.cpp#L357) | POTWIERDZONE: podobne oczekiwanie na spoczynek callbacku chroni stare IR. CAB jest ładowany wiele razy podczas jednego callbacku; konfiguracja/miks może więc zmienić się w połowie bloku. Brak spójnego snapshotu całego stanu, lecz nie udowodniono UAF. |
| A09 / ABI v2 | [api:9–31](../../multifx2/api.h#L9), [host4:256–277](../../multifx4/host.cpp#L256) | POTWIERDZONE: walidacja wersji, key, liczby parametrów i funkcji. ABI ma stereo wejście/wyjście jednej próbki; nie deklaruje portów, MIDI, latencji, stanu ani maksymalnego bloku. dlopen RTLD_NOW/LOCAL poza audio. JSON służy panelowi, nie jest niezależnie weryfikowaną paczką ABI. |
| A10 / koszt ABI | [host4:150–211](../../multifx4/host.cpp#L150), [tape_drive:16–22](../../multifx2/modules/tape_drive.cpp#L16), [tapedrive:50–108](../../multifx2/vendor/tapedrive/tapedrive_dsp.h#L50), [reel_echo:11–22](../../multifx2/modules/reel_echo.cpp#L11) | POTWIERDZONE: jeden pośredni process na slot/próbkę. Wrapper TapeDrive uruchamia istniejące blokowe DSP z n=1, ponawiając obliczenia współczynników przed jego pętlą. Reel przelicza parametry i set co próbkę. Wskazanie do migracji natywnej na blok; samo opakowanie starego ABI nie usuwa tych kosztów. |
| A11 / MIDI | [host4:148–196](../../multifx4/host.cpp#L148), [host4:434–457](../../multifx4/host.cpp#L434) | POTWIERDZONE: parser przepuszcza tylko CC >=3 B; brak Note On/Off oraz Program Change. Maksymalnie skanuje 128 bindingów i dla celu FX sloty. Liczba zdarzeń pochodzi z wejścia JACK; brak własnego budżetu. |
| A12 / spójność MIDI | [host4:85–90](../../multifx4/host.cpp#L85), [host4:160–167](../../multifx4/host.cpp#L160), [host4:443–450](../../multifx4/host.cpp#L443) | POTWIERDZONE: binding składa się z osobnych atomików. Wyzerowanie channel i ponowna publikacja nie gwarantują spójności, jeśli czytelnik zdążył odczytać stary channel przed zapisem. Brak data race nie oznacza transakcyjności. Parametry do DSP są skopiowane przed pętlą MIDI (117–123); CC do FX trafi do snapshotu dopiero w kolejnym callbacku. |
| A13 / presety | [panel4:186–218](../../multifx4/web_panel.py#L186), [panel4:437–446](../../multifx4/web_panel.py#L437) | POTWIERDZONE: pliki JSON zapisane przez tmp, fsync i replace. Restore wykonuje CLEAR, ADD, SET, ENABLE, ROUTE osobnymi RPC na aktywnym audio. Brak transakcji; błąd po CLEAR zostawia częściowy stan. Parametry zapisywane pozycyjnie; ABI nie ma save/load DSP. Brak utrwalania ogonów/opóźnień. |
| A14 / XRUN i diagnostyka | [host4:549–578](../../multifx4/host.cpp#L549) | POTWIERDZONE: process i shutdown są zarejestrowane; brak xrun callbacku, pomiarów per instancja, RSS, callback deadline, reakcji na zmianę sample rate/buffer size i raportowania własnej latencji. HIPOTEZA: długie IR, per-sample matematyka i bursts MIDI mogą przeciążyć callback; nie jest to diagnoza konkretnego sprzętowego XRUN. |
| A15 / mono | [host4:62](../../multifx4/host.cpp#L62), [host4:197–198](../../multifx4/host.cpp#L197), [connect:9–11](../../deploy/multifx4/connect.sh#L9) | POTWIERDZONE: jedno wejście audio skopiowane do L/R; stereo dotyczy wyjścia/DSP, nie fizycznego wejścia hosta. Next wymaga dwóch wejść. |
| A16 / pamięć i wyjątki | [oil_echo Engine:92–97](../../multifx2/vendor/oil_echo/Engine.h#L92), [reel TapeEngine:18–24](../../multifx2/vendor/reel/TapeEngine.cpp#L18), [host4:269–276](../../multifx4/host.cpp#L269) | POTWIERDZONE: duże bufory przygotowywane w konstruktorach poza audio. new(nothrow) nie powstrzymuje wyjątku z konstruktora alokującego vector; host nie ma bariery wyjątków przy create. Brak globalnego pomiaru alokacji we wszystkich ścieżkach DSP. |
| A17 / wdrożenie | [install:5–26](../../deploy/multifx4/install.sh#L5), [activate:10–15](../../deploy/multifx4/activate.sh#L10), [service:13–16](../../deploy/multifx4/jerzy-multifx4.service#L13) | POTWIERDZONE: staging także instaluje usługi i daemon-reload; activate zatrzymuje stare wersje. Tych skryptów nie uruchomiono. Next nie powinien używać ich jako neutralnej kompilacji. RTPRIO/MEMLOCK zadeklarowane nie dowodzą osiągnięcia RT na urządzeniu. |
| A18 / zakres testów | [module_tests:14–45](../../multifx2/module_tests.cpp#L14), [tests4:36–56](../../multifx4/tests.cpp#L36), [CMake:130–196](../../CMakeLists.txt#L130) | POTWIERDZONE: 13 modułów ładowanych raz, test skończonego audio; CAB testuje krótki impuls. Brak hostowych testów równoległego hot-swap, stress load/unload, MIDI Notes/PC, alokacji callbacku, niezgodnego ABI i fizycznego round-trip. Wiele testów używa assert; Release z NDEBUG osłabia ich kontrolę. |

## Rozdzielenie BassAmbient

Host4 bezpośrednio zawiera `bass_ambient.h` (4), instancję (71–77), parametry,
syntezę (128–140, 199–205), dedykowane routing/mix (208, 239–241), komendy SYNTH
oraz GUI. Niczego z tego nie należy przenosić do Next. Historyczny kod pozostaje.
Instrumenty Next mają być zwykłymi zewnętrznymi wtyczkami z zerem wejść audio.

## Materiały porównawcze i licencje

Źródła faktycznie pobrano i odczytano; nie skopiowano kodu do produktu.

- PiPedal commit `a0d9766e331a696b8450a74d8cc85c1c093a83d7`:
  [AudioDriver.hpp](https://github.com/rerdavies/pipedal/blob/a0d9766e331a696b8450a74d8cc85c1c093a83d7/src/AudioDriver.hpp#L40)
  oddziela AudioDriverHost::OnProcess od sterownika i expose buforów/lifecycle.
  [JackDriver.cpp](https://github.com/rerdavies/pipedal/blob/a0d9766e331a696b8450a74d8cc85c1c093a83d7/src/JackDriver.cpp#L213)
  rejestruje callback; [AlsaDriver.cpp](https://github.com/rerdavies/pipedal/blob/a0d9766e331a696b8450a74d8cc85c1c093a83d7/src/AlsaDriver.cpp#L1702)
  ma osobny kod readi/writei i obsługę urządzenia. Wniosek: separacja backendu jest realnym wzorcem,
  nie dowodem niższej latencji ALSA.
  [LICENSE.md](https://github.com/rerdavies/pipedal/blob/a0d9766e331a696b8450a74d8cc85c1c093a83d7/LICENSE.md)
  deklaruje MIT dla oryginalnego kodu, ale wymienia także ograniczone materiały T3K (modele/IR/presety).
  Nie przenosić całej zawartości jako rzekomo MIT.
- mod-host commit `12335c2653740be7fe9ce5b4e017f4086bd5c071`:
  [effects.c:4636](https://github.com/mod-audio/mod-host/blob/12335c2653740be7fe9ce5b4e017f4086bd5c071/src/effects.c#L4636)
  instancjuje LV2 przez Lilv, [5411](https://github.com/mod-audio/mod-host/blob/12335c2653740be7fe9ce5b4e017f4086bd5c071/src/effects.c#L5411)
  rejestruje ProcessPlugin; [5918](https://github.com/mod-audio/mod-host/blob/12335c2653740be7fe9ce5b4e017f4086bd5c071/src/effects.c#L5918)
  dezaktywuje/zwalnia instancję. ProcessPlugin (1707–1720) ma trylock przy niebezpiecznym restore.
  To szerszy model portów/feature/state, nie gotowy bezblokujący graf do skopiowania.
  [COPYING](https://github.com/mod-audio/mod-host/blob/12335c2653740be7fe9ce5b4e017f4086bd5c071/COPYING)
  zawiera GPLv3; brak podstaw do uznania kopiowania za zgodne z licencją docelowego Jerzy.
- [JACK callback API](https://jackaudio.org/api/group__ClientCallbacks.html): process nie może
  wykonywać malloc/free, I/O, mutex-lock, sleep itd. Opisuje także callbacki sample-rate i XRUN.
- [JACK latency API](https://jackaudio.org/api/group__LatencyFunctions.html): propagacja zakresów
  latencji przez porty. Deklarowana latencja nie zastępuje pomiaru fizycznego loopback.
- [PFFFT README](https://github.com/marton78/pffft/blob/master/README.md), odczyt 2026-11-17:
  float/double, NEON ARM64 i licencja opisana jako BSD-like. Kandydat do benchmarku;
  przed dołączeniem przypiąć commit i odczytać licencje faktycznie użytych plików.
- [FFTW FAQ](https://www.fftw.org/faq/section1.html): GPL i alternatywne licencjonowanie komercyjne.
  Dobór wymaga zgodności licencyjnej. FFT planować poza audio, ale wykonania i wydajności tu nie badano.

W repozytorium Jerzy nie znaleziono pliku LICENSE/COPYING przy przeglądzie listy plików.
Nagłówki vendor nie wystarczają do ustalenia pełnego pochodzenia i praw do redystrybucji.
To niewiadoma, a nie twierdzenie o naruszeniu. Przed migracją dostarczanych algorytmów
zachować wskazania pochodzenia i zweryfikować prawa do konkretnych plików.

## Wnioski z dostępnych testów

Szczegóły i pełne komendy: [TESTING.md](TESTING.md), [evidence](evidence/baseline-results.json).
Stary DSP i moduły przeszły dostępne testy offline. Nie testowano działającego hosta JACK,
mechanizmu hot-swap ani nowego silnika. Nie potwierdzono niskiej latencji, ograniczenia
CPU/RAM ani gotowości scenicznej. Sam audyt nie spełnia kryteriów akceptacji prototypu.
