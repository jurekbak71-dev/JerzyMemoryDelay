# JFX Next ABI v3 i plik .jfx

## ABI

Kontrakt C w [include/jfx.h](../include/jfx.h). Symbol `jfx_entry_v3` zwraca
`JfxDescriptor` z magic `0x3346584a`, struct_size, major=3 i minor.
Host wymaga co najmniej rozmiaru bieżącej struktury i zgodnego major;
większy tail może być pominięty. Układ wykorzystuje natywne ELF64 little-endian,
uint32/uint64 i wskaźniki; brak STL i wyjątków przez ABI.

Wtyczka deklaruje 0 lub 2 wejścia audio, 2 wyjścia, flagę MIDI input, do 48 parametrów
z unikalnymi kluczami i zakresem, latencję do 131072 próbek oraz schema stanu.
Przetwarzanie: planar float, do uzgodnionego max_frames, niezmienne parametry bloku,
MIDI1 1–3 bajty z offsetem próbki i transport. W prototypie do DSP trafiają Note On/Off,
CC i Program Change. SysEx, MIDI output wtyczek i feedback audio nie są obsługiwane.

Create/destroy i dostęp do plików: tylko poza audio. Process: bez alokacji, blokad,
I/O i wyjątków. Nie wolno przechowywać wskaźników eventów/buforów po powrocie.
Plugin musi wypełnić wyjścia; host zeruje bufory przed wywołaniem. NaN/Inf są
usuwane z toru. Miks nie ma ukrytej normalizacji ani limitera.

ABI przewiduje opcjonalne callbacki state_size/save_state/load_state. Obecny Session
utrwala konfigurację i parametry; **nie wywołuje tych callbacków i nie gwarantuje
zapisu opaque DSP state**, ogonów, próbek samplera ani aktywnych głosów. Dostarczone
wtyczki deklarują brak takiego stanu. Wsparcie save/load dla dowolnej wtyczki wymaga
jawnego odsunięcia instancji od audio przed dostępem do mutable DSP.

Resource_bytes raportuje znany rozmiar obiektów/buforów, nie RSS i nie kompletny
narzut alokatora. Adapter ABI2 raportuje tylko własny obiekt; pełna pamięć starego
DSP jest nieznana. Instance/library owners żyją na kontrolerze, callback nie zmienia refcount.

## Paczka

`.jfx` to tar USTAR (opcjonalnie gzip), wyłącznie zwykłe pliki:

```text
manifest.json
plugin.so
assets/...       (opcjonalnie)
```

Format celowo odrzuca PAX/GNU extension headers, linki, katalogi jako osobne wpisy,
ścieżki absolutne, `.`/`..`, backslash, duplikaty. Limit: 128 plików oraz 64 MiB
rozpakowanego strumienia łącznie z nagłówkami; manifest do 1 MiB. Ograniczenie jest
nakładane przed parsowaniem ukrytych nagłówków i przed odczytaniem ich payloadu.

Manifest zawiera format_version=1, plugin_id/version, abi_major/minor, arch
(`x86_64` albo `aarch64`), library=`plugin.so`, ports, parameters, latency_frames,
state_schema, provenance i files (SHA256 każdego payloadu poza manifestem).
Przykłady: [gain.json](../plugins/gain.json), [synth.json](../plugins/synth.json).
Narzędzie pack uzupełnia architekturę z ELF i hashe. Nie wykonuje wtyczki, więc może
pakować binaria z cross-build. Install wymaga architektury bieżącej maszyny.

Zapis do tymczasowego katalogu i rename do `plugin_id-plugin_version`; bez podmiany
używanego pliku `.so`. Biblioteka może być współdzielona przez wiele instancji;
ostatni właściciel wykonuje dlclose poza audio. Usunięcie instancji nie usuwa paczki.
Nie ma automatycznego instalowania zależności systemowych `.so`.

SHA256 sprawdza integralność, nie podpis wydawcy. Native plugin jest zaufanym kodem:
ELF i ABI validation nie są sandboxem, a konstruktory biblioteki wykonują się przy
dl_open. Do uruchamiania nieznanego natywnego kodu potrzebna byłaby osobna izolacja.


CAB 1.1.0 dodaje do manifestu `licenses`: listę wymaganych plików pod
`assets/licenses/`. Pack/install odrzuca paczkę, jeśli brakuje zadeklarowanego pliku.
Paczka zawiera PFFFT.txt i PFFFT-NOTICES.txt, łącznie z notą o lokalnej poprawce OOM.
`resource_bytes` CAB obejmuje znane obiekty i pojemności widm, ale nie prywatną
strukturę i twiddle allocation biblioteki PFFFT; nie jest dokładnym pomiarem RAM.
