# Propozycja architektury Jerzy MultiFX Next

**Projekt zatwierdzony przez użytkownika 2026-11-17; stan implementacji opisuje README.md.**
Autor propozycji: agent; wymagania użytkownika pochodzą z załączonej specyfikacji.
Użytkownik zatwierdził wariant 1 i rozpoczęcie implementacji: „Zatwierdzam, START”.
Projekt przeszedł niezależny przegląd wykonalności; jego uwagę o reaktywacji
BYPASS rozstrzygnięto poniżej jako propozycję agenta.

## Cel i ograniczenia

Host do grania na żywo na Raspberry Pi 4 / Linux ARM64 / HeadRush Core USB.
C++20, stereo, blokowy DSP, zewnętrzne FX i instrumenty, niezależne pliki wtyczek,
wspólny routing audio/MIDI. Bazowy backend JACK2; punktem pomiarowym pozostaje
44,1 kHz / 64 frames / 3 periods. Nie oznacza to osiągniętej ani obiecanej latencji.
Nie zmieniamy działającego Pi ani usług. Brak push/merge/release bez zgody.
Nowe pliki wyłącznie w multifx-next/. BassAmbient, pady, Krell, ich routing i GUI
nie są częścią nowego produktu. Stary kod pozostaje nienaruszony.

Callback nie wykonuje alokacji/dealokacji, blokad, plików, sieci, ładowania bibliotek,
logowania ani oczekiwania na kontroler. Kontroler jest pojedynczym autorem konfiguracji;
jedna instancja silnika ma jeden niereentrantny wątek audio. Wielu klientów sterujących
musi być serializowanych poza DSP. Wtyczki są zaufanym kodem natywnym — kontrola ABI
nie izoluje uszkodzonego lub złośliwego .so w osobnym procesie.

## Rozważane podejścia

1. **Rekomendowane przez agenta:** niezmienne plany grafu, publikacja i potwierdzenie na
   granicy bloków. Wszystkie zasoby przygotowywane poza audio, stary plan zwalniany
   przez kontroler po potwierdzeniu. Pozwala edytować graf bez zatrzymywania JACK.
   Większy koszt projektu lifetime i pamięci podczas przejścia.
2. Dezaktywacja JACK na czas każdej edycji: łatwiejszy lifetime, ale przerwy audio.
   Niezgodna z intencją płynnego sterowania podczas koncertu; odradzana.
3. Rozszerzenie per-sample ABI v2 jako podstawy hosta: odrzucone w propozycji,
   ponieważ nie deklaruje instrumentów/MIDI/latencji i utrwala narzut z audytu A09–A10.
   ABI v2 pozostaje ścieżką migracji przez osobny adapter, a nie interfejsem rdzenia.

Poniższy projekt opisuje zatwierdzony wariant 1.

## Podział systemu

- `Backend`: lifecycle, porty urządzenia, bufor audio, timestamped MIDI, tempo/transport,
  licznik XRUN; brak zależności od klas efektów.
- `Engine::process(BlockView) noexcept`: stały limit frames i zdarzeń; wykonanie planu.
- `GraphCompiler`: walidacja węzłów/portów, sortowanie topologiczne, bufory, MIDI merge,
  miksery i kompensacja opóźnień. Tylko kontroler.
- `PluginRegistry/Library/Instance`: walidacja ABI, współdzielona biblioteka i lifetime.
- `Session`: konfiguracja slotów, parametry po kluczach, połączenia, preset i assets.
- `Diagnostics`: atomowe liczniki i odczyt poza audio; brak drukowania w callbacku.

## Plan grafu i routing

Pierwszy zakres: maksymalnie 16 slotów, po dwa kanały audio na węzeł, maksymalnie
64 edges audio, 64 MIDI edges, 256 krótkich zdarzeń MIDI na blok i port. Limity są
propozycją agenta i jawnie walidowanym budżetem; w ABI pola liczbowe, bez zaszywania
założenia że instrument ma wejście. Brak feedback edges w pierwszej wersji;
cykl odrzucany z błędem, bez naruszania aktywnego grafu.

Graf DAG obejmuje wejścia/wyjścia urządzenia, wtyczki, miksery audio i routingi MIDI.
Audio edge wybiera źródło/kanał/cel/gain. Suma wielu źródeł nie wykonuje ukrytego
normalizowania. GraphCompiler wstawia opóźnienia wyrównujące wejścia miksera do
najdłuższej ścieżki; opóźnienie deklarowane przez wtyczkę jest częścią ścieżki.
Nie zakładać, że dowolna wtyczka ma zerową latencję. Bypass FX przechodzi przez
przygotowany delay odpowiadający latencji slotu; bypass instrumentu daje ciszę.
Zmiana ACTIVE/BYPASS nie zmienia latencji planu. UNLOADED zachowuje ostatnie
metadane latencji; zmiana tej polityki wymaga przebudowy i pomiaru przejścia.

MIDI edge jest niezależny od audio edge. Event ma frame offset, długość 1–3 i bajty
MIDI1; Note On/Off, CC i PC obsługiwane wraz z kanałem. Note On velocity=0 jako Note Off.
Merge zachowuje kolejność po timestampie i deterministyczny porządek źródeł przy remisie.
SysEx i generowanie MIDI przez pluginy poza pierwszym zakresem; zadeklarować unsupported,
nie ucinać ich do pozornie poprawnych wiadomości. Przepełnienie zwiększa licznik; aby
nie zgubić Note Off, silnik oznacza panic i w kolejnym dostępnym bloku kieruje do
instrumentów All Sound Off na wszystkich kanałach. Test musi dowodzić braku stuck notes.
CC/PC są zdarzeniami dostarczanymi wtyczkom; zmiana presetu sesji przez PC jest
oddzielną mapą kontrolera, nie odczytem pliku z callbacku.

Pusty graf jest dokładnym stereofonicznym passthrough (oddzielne L/R). Przy zmianie
kolejności serial chain kontroler przebudowuje edges, nie tylko kolejność wykonania.
Dla grafu równoległego zmiana kolejności wykonania nie może samowolnie zmienić routingu.

## Wymiana planu i odzyskiwanie pamięci

Controller posiada plany przez unique_ptr. Plan zawiera współdzielonych właścicieli
instancji; callback używa wyłącznie surowych wskaźników z planu. Żadne zwiększanie
ani zmniejszanie refcount nie odbywa się na audio. Instancja obecna w dwóch kolejnych
planach nie jest przetwarzana równolegle, jej stan DSP należy wyłącznie do audio.
Parametry, tryby, edges i transport control są snapshotami planu, a nie mutacją instancji
przez kontroler.

Protokół pierwszej wersji ma najwyżej jedną oczekującą transakcję:

1. Kontroler konstruuje i waliduje cały plan/nowe instancje/bufory; błędy nie zmieniają live.
2. Publikuje pointer przez release do atomika pending. Bez nadpisywania oczekującego planu.
3. Callback na początku bloku pobiera pending przez acquire-exchange. Zmienia swój
   active pointer i umieszcza poprzedni plan w ograniczonej kolejce SPSC retired.
4. Kontroler odbiera retired przez acquire, dopiero wtedy niszczy stary plan i zasoby,
   które nie mają właściciela w nowym. Zanim opublikuje następną transakcję, konsumuje
   poprzednie potwierdzenie. Kolejka ma pojemność co najmniej 2; protokół zapewnia miejsce,
   callback nigdy nie czeka. Atomiki pointer/indeks muszą być lock-free na celu.
5. Publikacja zwraca token; API kontrolera rozróżnia queued, applied i busy. Zatrzymane
   audio nie potwierdzi transakcji; timeout nie upoważnia do usunięcia planu używanego
   przez callback. Zwalnianie po shutdown dopiero po zakończeniu/dezaktywacji backendu.

Pierwszy pusty active plan istnieje przed aktywacją. Kontroler nie odczytuje mutable
active pointer; stan applied poznaje z potwierdzenia. Pamięć najwyżej aktywnego,
oczekującego i oczekującego na odbiór planu, plus współdzielone zasoby. Nie ma
bezterminowej listy emerytowanych grafów. Testy obejmują audio zatrzymane w różnych
miejscach protokołu, shutdown z pending i szybkie publikacje/busy.

Zmiana topologii na granicy bloku nie gwarantuje bezklikowego przejścia. Pierwszy
prototyp dokumentuje tę granicę; płynny crossfade między niezależnymi stanami grafów
jest dalszą funkcją wymagającą budżetu CPU i testów. Nie przetwarzać tej samej
mutable instancji dwa razy dla crossfade starego i nowego planu.

## ABI JFX Next v3 — projekt kontraktu

Osobny nagłówek C, extern C, typy uint32/uint64/float i opaque pointer. Bez STL,
wyjątków, bool C++ i zależności od układu klas przez granicę ABI.
Entry `jfx_entry_v3` zwraca descriptor ze stałym prefixem magic, ABI major/minor,
struct_size. Major musi się zgadzać; minor i tail przez struct_size są rozszerzalne.
Host przed odczytem kolejnych pól sprawdza prefix i wielkość struktury.

Descriptor: stabilny plugin ID/version/name; audio input/output count (0 dla wejść
instrumentu); MIDI capabilities; tablica parametrów ze stabilnymi kluczami, min/max/default;
latency_frames; state schema version; capabilities. Pierwszy host obsługuje 0 lub 2 wejścia,
2 wyjścia i MIDI input; inne deklaracje jawnie odrzuca jako nieobsługiwane.
Funkcje create(sample_rate,max_frames,asset_path), destroy, process i opcjonalne state
size/save/load. Process otrzymuje planar audio, frames, niezmienną tablicę parametrów,
stałe wejściowe eventy i transport (bpm, sample position, playing, valid flags).
Dostępne buffers/state należą do hosta, plugin nie może zachować pointera eventów po bloku.
Nie ma alokowania/niszczenia stanu DSP przez callback.

Create/destroy/save/load poza audio; wyjątki nie mogą przekroczyć ABI. Wtyczka raportuje
błąd kodem/null. Host waliduje parametry i skończone wartości. Proces sandboxu nie jest
zapewniony: uszkodzony pointer lub segfault w .so może przerwać host. Trusted malformed
metadata odrzucamy; nie obiecujemy bezpiecznego wykonania dowolnego pliku natywnego.

State save/load wymaga braku równoległego process dla tej instancji. Parametry i routing
można utrwalać bez dotykania mutable DSP. Pełny opaque state zapisuje się dopiero po
potwierdzonym odsunięciu instancji od audio (chwilowy bypass/odłączenie), potem ewentualnie
przywraca instancję nowym planem. To może przerwać ogony; pierwszy prototyp gwarantuje
przywracanie konfiguracji/parametrów, nie bitowo identycznego przebiegu ogonów ani głosów.

## Paczka instalowana z pliku — projekt

Paczka `.jfx` jako archiwum tar z `manifest.json`, `plugin.so` oraz opcjonalnym `assets/`.
Manifest: format_version, plugin_id, plugin_version, abi_major/minor, arch (`aarch64` lub
`x86_64` dla testów), ELF class/endianness, biblioteka, ports, parameters, state schema,
lista plików i ich SHA256, pochodzenie/licencje. Przykłady treści dopiero razem z walidatorem.
Hash sprawdza integralność paczki, nie tożsamość wydawcy.

Instalator poza audio: ogranicza rozmiar/ilość plików, odrzuca absolute/../symlink/hardlink,
sprawdza metadata/schema/hashes i ELF architecture przed dlopen, instaluje atomowo do
wersjonowanego katalogu użytkownika. Nie nadpisuje .so używanego przez live instancje.
Registry kluczuje canonical path + wersję; `Library` wywołuje dlclose dopiero po ostatnim
właścicielu Instance/Descriptor. Unload pluginu nie oznacza usunięcia plików paczki.
Biblioteki obu architektur nie są wymienne. Zależności runtime muszą być udokumentowane.

## Sloty i presety

- ACTIVE: wykonuje blok DSP. Instrument bez audio input generuje audio z MIDI.
- BYPASS: FX kopiuje opóźnione dry, instrument cisza, zero wywołań process.
  Ogony są ucinane w pierwszej wersji; kontrolowany tail to późniejsza jawna polityka.
  Decyzja agenta po przeglądzie: BYPASS -> ACTIVE tworzy świeżą instancję poza audio
  z zachowaniem parametrów, ale bez przywracania głosów/ogonów. Starą instancję niszczy
  kontroler po potwierdzeniu nowego planu. Instrument wymaga nowego Note On; nuty
  trzymane podczas bypassu nie są odtwarzane automatycznie. To zapobiega ponownemu
  odegraniu starego ogona lub głosu, który przegapił Note Off. Błąd create pozostawia
  BYPASS. ACTIVE -> ACTIVE przy edycji samego routingu zachowuje instancję DSP.
- UNLOADED: po potwierdzonym odłączeniu destroy instancji poza audio i zwolnienie właściciela
  Library. Pozostaje ID paczki, porty, latencja, parametry i opcjonalny ograniczony stan.
  Reaktywacja create/load state poza audio, dopiero potem publikacja. Błąd pozostawia
  dotychczasowy live plan. Samo przejście UNLOADED nie kasuje ustawień.

Presety JSON: schema_version, graph, plugin ID/version, parametry po kluczach, slot state,
MIDI mappings, bpm i asset IDs. Brak BassAmbient. Walidacja/resolve wszystkich aktywów
przed publikacją; błąd nie zaczyna od CLEAR. Zapis tmp/fsync/rename poza audio.
Migracja v2/v4 zachowuje znane keys/parametry i enabled->ACTIVE/BYPASS; odrzuca lub
raportuje sekcje synth i historyczny synth routing, nie przekierowuje ich po cichu.
Adapter ABI2 w osobnym module deleguje per próbkę; nie gwarantuje poprawy CPU i nie
udaje instrumentu. Port natywny blokowy zaczynać od prostych FX i porównania próbek.

## CAB/IR

Osobny plugin; brak CabinetIR w klasie Engine. Najpierw wykorzystać porównywalny algorytm
partycjonowany z ir_cab.h (po ustaleniu pochodzenia), zachować skalowanie, resampling i
filtry jako jawne funkcje wtyczki. Latencja 64 frames deklarowana i wyrównana w miksie
wewnętrznym dry/wet oraz przez GraphCompiler między gałęziami. Nieaktywny IR nie wykonuje FFT.
Współczynniki filtrów i FFT twiddle obliczane przy prepare/zmianie, nie każdą próbkę.

PFFFT/NEON rekomendowane do eksperymentu, FFTW do porównania po kontroli licencji.
Nie wybierać szybszego na podstawie nazwy: ten sam IR/impuls/blok, tolerancje i profile
max/p99, RAM, round-trip na ARM64. Oryginalny wet i nowy wet porównać z wyrównaniem;
poprawa dry/wet alignment jest zamierzoną korektą fazy i wymaga osobnego testu, nie
może być przedstawiona jako bitowo niezmienione brzmienie całego starego miksu.

## JACK i porównanie ALSA

JACK: dwa wejścia i dwa wyjścia audio, MIDI input, process/xrun/shutdown, transport,
latency callback. max_frames ustalane przed activate, bufory przygotowane i dotknięte
poza audio. Zmiana sample rate lub buffer size wymagająca prepare: sygnał do kontrolera,
bezpieczna cisza i jawne przeuruchomienie poza callbackiem; brak resize wewnątrz process.
Nie uruchamiać automatycznie JACK ani nie przepinać fizycznych portów bez polecenia.

Backend offline wywołuje to samo Engine::process i służy testom; nie udaje sprzętu.
ALSA pozostaje oddzielnym kolejnym backendem. Porównanie na tym samym Pi/USB/cabling,
ta sama częstotliwość, okresy możliwie równe, to samo DSP, governor, RT settings i obciążenie.
Raportować faktycznie wynegocjowane parametry ALSA/JACK, nie tylko żądane.
Fizyczny loopback + korelacja impulsu lub sweep: mediana/p95/max round-trip,
XRUN, CPU, deadline miss i RAM. Pomiar przez co najmniej 30 min po rozgrzaniu,
kilka powtórzeń i test pod obciążeniem. Przy braku sprzętu brak rankingu latencji.

## Diagnostyka i testy pierwszego prototypu

Callback/instancje: liczba wywołań, czas ostatni/max/suma, deadline misses, overflow,
nieprawidłowe próbki, XRUN. Monotonic timing wymaga weryfikacji ścieżki vDSO i narzutu
na docelowym Pi; opcja wyłączenia pomiarów per-slot. Atomiki liczbowe tylko lock-free.
Odczyt i formatowanie poza audio. RSS z /proc, PSS opcjonalnie oraz znana liczba
instancji/buforów/bytes; RSS nie jest licznikiem live allocations. UNLOADED raportować
przed/po: bajty należące do zasobów pluginu, RSS/PSS, library owners i czas destroy.

Prototyp musi zawierać prawdziwy efekt stereo gain i prosty zewnętrzny instrument MIDI,
bez BassAmbient. Testy nie mogą ograniczać się do statusu sukcesu: sprawdzają próbki,
moment Note On/Off, CC/PC, sumę równoległych źródeł, zmiany kolejności nieprzemiennych FX,
serial/parallel, bypass/unload, odrzucenie ABI/paczek i żywotność wspólnych bibliotek.
Szczegółowa macierz w TESTING.md. Dopiero implementacja, kompilacja i dostępne testy
pozwolą nazwać to prototypem; testy x86/dummy JACK nie potwierdzą parametrów scenicznych Pi.

## Zakres pozostawiony implementacji

Nazwy plików/klas i CLI; wybór małego parsera JSON po weryfikacji licencji; realizacja
liczników i SPSC; dokładne tolerancje porównań DSP ustalone z sygnału referencyjnego;
model prostego instrumentu demonstracyjnego. Brak zgody na dodanie GUI, kopiowanie
obcych DSP lub zakup interfejsu. Bez mierzalnych wyników nie przyjmować hipotez za fakty.

## Stan wdrożenia i jawne odstępstwa prototypu

Implementacja: `include/jfx.h`, `src/plugin.*`, `src/engine.*`, `src/session.*`,
`src/jack_backend.*`, `src/main.cpp`, cztery pluginy i narzędzie `.jfx`.
Szczegółowe wyniki/ograniczenia w TESTING.md; ten projekt nie stanowi dowodu wykonania.

- Zamiast pierścienia retired są dwa ograniczone atomowe mailboxy: pending pointer
  i acknowledgement. Przy jednej oczekującej transakcji old plan jest jednoznaczny;
  kontroler zastępuje właściciela dopiero po ack. Audio nie niszczy żadnego planu.
- Metryki instancji są atomikami w Instance; slot ID jest publikowany w diagnostyce.
- Zmniejszenie maski MIDI wysyła panic na usuwane kanały przed nowym routingiem.
  Globalna utrata MIDI (także JACK mixdown) powoduje All Sound Off na wszystkich kanałach.
- `.jfx` przyjmuje tylko zwykłe nagłówki tar USTAR; PAX/GNU extensions są odrzucane.
  Limit zdekompresowanego strumienia obejmuje nagłówki, nie tylko pliki widoczne po parsowaniu.
- ABI ma opcjonalne callbacki opaque state, ale Session prototypu zachowuje wyłącznie
  konfigurację i parametry. Nie zapisuje stanu głosów/ogonów ani nie wywołuje save/load DSP.
- CAB jest wtyczką z referencyjną konwolucją i dry/wet alignment; starych filtrów
  low/high-cut nie przeniesiono. Test porównuje wet convolver, nie kompletny stary tor CAB.
- Aktualizacja CAB 1.1.0: PFFFT real FFT i NEON wdrożone; cross-build ARM64 i testy
  QEMU wykonane. Szczegóły w CAB_OPTIMIZATION.md. ALSA i porównania sprzętowe pozostają dalszą pracą.
