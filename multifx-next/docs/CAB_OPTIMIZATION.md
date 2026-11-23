# CAB 1.1.0 — PFFFT, pamięć i ARM64

Data: 2026-11-17. Kontynuacja zatwierdzonej architektury po prototypie `c7c616e`.
Nowe źródła: src/pffft_cab.*, testy convolver/compatibility/failure, toolchain ARM64.
Nie zmieniono historycznego MultiFX, konfiguracji Pi ani sprzętu. Brak push/merge/release.

## Implementacja i zgodność audio

PFFFT real FFT: 128 punktów, partycje overlap-save 64, widma w natywnym układzie PFFFT,
normalizacja 1/128 przy mnożeniu i sumowaniu. Wszystkie plany, widma i bufory robocze
powstają przy prepare poza audio. Pamięć danych jest wyrównana do 64 bajtów.
Brak alokowania/zwalniania w obserwowanych wywołaniach tick. Latencja nadal wynosi
64 próbki, a wewnętrzny dry/wet pozostaje wyrównany. Brak zmian parametrów ABI.

WAV reader i resampling zachowują logikę istniejącego multifx4/ir_cab.h. Nie zmieniano
historycznego pliku. Filtry starego pełnego toru CAB nie są częścią tego porównania;
porównujemy referencyjną konwolucję wet i tor dry/wet prototypu.

- Pure convolver: 43 328 porównań, IR 0/1/63/64/65/127/128/129/511/1024/32768 taps,
  niezależny double FIR dla krótkich odpowiedzi, DC/Nyquist, impuls, NaN/Inf i clipping.
- Native x86: max_abs=2.16067e-7, RMS=4.28391e-8 wobec referencji.
- ARM64/QEMU Cortex-A72: max_abs=2.08616e-7, RMS=3.5711e-8; backend zgłasza NEON, 4 lanes.
- Plugin: 320 000 próbek stereo float WAV, 16 par częstotliwości źródła/celu
  (22050/44100/48000/96000 -> 8000/44100/48000/192000), wywołania po 1–256 próbek
  z nieregularnymi granicami. Native max_abs=5.06639e-7; próg testu 2e-5.

To porównanie numeryczne dla testowanych sygnałów, nie subiektywny odsłuch na HeadRush,
bitowa identyczność ani dowód zgodności dla dowolnego sygnału.

## Wyniki testów

| Build / środowisko | Wynik |
|---|---:|
| x86_64 Debug | 12/12 |
| x86_64 Release | 12/12 |
| x86_64 ASan/UBSan/LSan | 11/11 |
| x86_64 TSan | 11/11 |
| ARM64 Release, wykonanie QEMU Cortex-A72 | 10/10 |

W normalnych buildach dodatkowy NextConvolverFailure przechwytuje malloc/aligned
allocation i wymusza cztery awarie. Nie łączymy tych hooków z interceptorami sanitizerów,
więc tam ten jeden test nie jest budowany. ARM64 wykonuje testy offline; pomijane są
natywne Python install (host jest x86) i integracja JACK z serwerem hosta. Native test
JACK nadal uruchamia prawdziwy serwer dummy i klienty; nie jest testem urządzenia USB.

Końcowe logi, skróty źródeł, benchmark i zależności:
[evidence/cab-verification.json](evidence/cab-verification.json).

## Pomiar CPU i znanej pamięci — tylko x86_64

Release GCC 14.2, SSE1/4 lanes, stereo, 64 próbki, pięć prób po 512 bloków po rozgrzaniu.
To czas samego algorytmu w procesie offline, bez JACK/USB/round-trip; nie przypinano CPU
ani nie kontrolowano governor/pozostałego obciążenia. Outliery są zachowane w JSON.

| Taps / kanał | Referencja mediana ns | PFFFT mediana ns | Iloraz median |
|---:|---:|---:|---:|
| 128 | 6610 | 891 | 7.42 |
| 1024 | 10656 | 1482 | 7.19 |
| 8192 | 42793 | 6700 | 6.39 |
| 32768 | 161249 | 30846 | 5.23 |

Nie przenosimy tego ilorazu na Pi 4 i nie porównujemy go z FFTW bez pomiaru.
Dla 32768 taps znana pamięć dwóch convolverów spadła z 2 100 352 do 1 053 440 B.
Sama reprezentacja widm zajmuje połowę poprzedniej pamięci; bufory i obiekty dodają narzut.
Nie uwzględniamy prywatnego setup/twiddle biblioteki PFFFT w resource_bytes.

Pomiar pluginu UNLOADED zwolnił 1 053 968 B śledzonej pojemności; mmap heap w danym
przebiegu spadł z 1 064 960 do 0 B. RSS i pełne rekordy są w cab-memory.log.
Slot nadal ma parametry/metadane, a host ma bufory routingu/kompensacji; nie jest to zero RAM.

Reprodukcja po kompilacji Release:

```sh
work/next-release/next_convolver_bench
ctest --test-dir work/next-release --output-on-failure
```

## ARM64 — co wykonano, a czego nie

Lokalnie rozpakowano oficjalne pakiety Debian: GCC 14.2, glibc/sysroot 2.41, ARM64 JACK,
Berkeley DB; nie instalowano usług/systemu docelowego. Skompilowano host JACK i cztery
pluginy, testy i benchmark. ELF potwierdza AArch64. Disassembly CAB zawiera instrukcje
wektorowe NEON; wykonany kod pod QEMU zgłasza `SIMD=NEON lanes=4`.

QEMU używa modelu Cortex-A72 i właściwego loadera/bibliotek ARM64. Nie jest to fizyczne
Pi ani benchmark jego CPU. Nie testowano USB, HeadRush, schedulera RT, round-trip czy
XRUN na Pi. Host tego konkretnego builda importuje GLIBC_2.38 i GLIBCXX_3.4.32:
**nie zakładamy zgodności ze starszym Pi OS**. Dla rzeczywistego urządzenia kompiluj
natywnie lub użyj zgodnego sysroota i bibliotek. Instrukcja w README.

Pakowanie ARM64 odbywa się na hoście bez wykonywania pluginu:

```sh
python3 multifx-next/tools/jfx_package.py pack work/next-arm64/plugins/cab work/cab-aarch64.jfx
```

Install na x86 nie powinien przyjmować paczki ARM64. Plik toolchain z sysrootem
ustawia targetowy pkg-config; osobny test cross_configure_test.py z podsuniętym
hostowym jack.pc (wersja 999.0) odtwarzał błąd i po poprawce wybiera target JACK.

## Pochodzenie, licencje i poprawki przeglądu

PFFFT: https://github.com/marton78/pffft/tree/efc1dd2de6c3562fc464f905d37d9281f08a20dd.
Przed kopiowaniem sprawdzono LICENSE.txt i nagłówki źródeł. Zachowano notices i zakaz
wykorzystywania nazw autorów do sugerowania poparcia produktu. Zainstalowana paczka
CAB zawiera LICENSE i pełne notices; brak zadeklarowanej licencji powoduje błąd pack/install.

13 z 14 pobranych plików pozostało niezmienionych. Jeden plik ma jawną lokalną poprawkę:
[vendor/pffft/setup-allocation-failure.patch](../vendor/pffft/setup-allocation-failure.patch).
UPSTREAM.json rozróżnia oryginalne skróty i skrót zmodyfikowanego pliku. Poprawka została
nałożona na osobną kopię upstream i wynik porównano bajt po bajcie z lokalnym źródłem.

Niezależny przegląd wskazał jeden Important: pffft_new_setup dereferencjonował wynik
malloc bez sprawdzenia i nie sprawdzał kolejnej alokacji danych. Test kontrolowanego
NULL odtworzył SIGSEGV osobno w obu miejscach. Dwie kontrole NULL i cleanup częściowego
setupu usunęły błąd; arytmetyka FFT nie została zmieniona. Cztery punkty awarii
(setup, twiddle, dwa wektory widm) zachowują poprzedni stan konwolucji.
Nie jest to gwarancja przeżycia dowolnego systemowego OOM.

W pełnym Debug ujawniło się też zbyt wczesne oczekiwanie testu JACK: po ACTIVE nowa
instancja mogła mieć zero wywołań, bo ack jest publikowany przed końcem pierwszego bloku.
Test czeka teraz na mierzalne przetworzenie i brak pending z limitem czasu; nie dodano
stałego opóźnienia ani nie osłabiono asercji audio. Poprzedni nieudany log zachowano.

Jeden przegląd, poprawki potwierdzone reprodukcjami i regresją; bez drugiego przeglądu.
Brak odłożonych drobnych uwag z tej rundy. Pozostałe zadania: fizyczne testy Pi/HeadRush,
backend/porównanie ALSA na urządzeniu, pełny opaque state i importer starych presetów.
