# Warianty JACK2 i ALSA do testów

WYKONANO: niezależne backendy z jednym silnikiem DSP, pluginami i protokołem
sterującym. ALSA korzysta z własnego wątku duplex, I/O nonblocking, prealokowanych
buforów, formatu float lub signed PCM16/24/32, mapowania kanałów oraz ALSA sequencer.
MIDI przechodzi przez stałą kolejkę SPSC z osobnego wątku do DSP. Przepełnienie
odrzuca zaległą kolejkę i wywołuje panic. Zdarzenia trafiają na granicę bloku.
Próba połączenia capture/playback przez snd_pcm_link, a przy braku obsługi dwa
jawne starty. XRUN/suspend: drop/prepare/prime/start obu strumieni; błąd trwały
kończy host. Nie ma nieograniczonej pętli ponawiania uszkodzonego urządzenia.

Dwa archiwa źródłowe zawierają cały potrzebny kod i zależne historyczne nagłówki.
Kompilacja natywna zapobiega instalowaniu binariów dla niewłaściwego glibc.
Instalator sprawdza checksumy, uruchamia CMake/pkg-config, buduje w katalogu tymczasowym,
a dopiero po sukcesie kopiuje host, pluginy, narzędzia, licencje i launcher do wersji
w katalogu użytkownika. Nie nadpisuje istniejącej instalacji. Sam nic nie pobiera.
Wymaga dostępnego CMake >=3.25, kompilatora C++20 i właściwych nagłówków audio.
Nie jest obietnicą działania na każdym wydaniu Pi OS bez przygotowania zależności.

ZWERYFIKOWANO: deploy/README.md potwierdza Pi OS 64-bit, Core hw:CARD=Core,DEV=0,
44100 Hz, 6 capture / 4 playback, 64 próbki / 3 okresy. Repo nie potwierdza wydania
OS na faktycznym Pi. Trixie w scripts/setup_cloud.sh jest konfiguracją chmury.

TESTY: NextALSA używa prawdziwego libasound/null (start, bloki DSP, STATS, QUIT,
błędne urządzenie). NextALSAAudio używa file PCM, rzeczywistych danych float6→4,
mapowania wejść 3/5 na wyjścia 2/4, gain 0.5 i zerowania innych kanałów.
NextALSAFaults wstrzykuje krótkie odczyty, jeden -EPIPE, albo -ENODEV przez testowy
LD_PRELOAD; potwierdza wznowienie DSP/XRUN=1 lub zakończenie kodem 1. Nie jest
symulacją sterownika USB ani fizycznym odłączeniem. Shim nie trafia do hosta.
PCM i kolejka mają osobne testy, w tym 100000 zdarzeń między dwoma wątkami.
Testy archiwów wykrywają zmianę źródła przed kompilacją. Oba archiwa niezależnie
skompilowano i zainstalowano w testowym prefixie bez dostępu do repo źródłowego.
Wyniki końcowe: Debug 18/18, ASan/UBSan 16/16, TSan 16/16,
ARM64 pod QEMU cortex-a72 12/12. Dodatkowo backend ARM64 przeszedł pod QEMU
integracje ALSA null/file. Instalowane warianty JACK2 i ALSA przeszły integracje
na x86_64. JACK dummy zgłaszał XRUN podczas obciążenia środowiska; nie deklarujemy
pracy bez XRUN ani wyniku CPU/latencji sprzętu na podstawie tych testów.
Polecenia CTest: jak README, dodatkowo -DNEXT_ALSA=ON. Logi w evidence/dual/.
Niezależny przegląd: brak krytycznych/istotnych uwag; sześć odpowiednich testów
ponownie uruchomionych przez recenzenta przeszło.

NIEZWERYFIKOWANE: fizyczny Pi4/HeadRush, stabilność/latencja USB, MIDI sequencer
na docelowym Pi, round-trip, recovery po rzeczywistym suspend/odłączeniu USB,
porównanie CPU JACK/ALSA na tym samym urządzeniu. Null/file nie potwierdzają tych rzeczy.
Nie deklarujemy przewagi ALSA. W chmurze brak /dev/snd/seq; testowany był tor kolejki,
a nie zewnętrzny port MIDI ALSA.

NIEWYKONANE: pomiary sprzętowe (do A/B użytkownika), rozbudowane GUI. Nie zmieniano
systemu Pi, usług ani gałęzi historycznych; nie wykonano push/release.

ŹRÓDŁA: własny kod repo, dokumentacja API z oficjalnych nagłówków alsa-lib 1.2.8
https://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html oraz seq.html; JACK2 1.9.22.
Bez kopiowania obcego kodu backendu. Pochodzenie PFFFT bez zmian, w vendor/pffft.
Instrukcja użytkownika: installer/README-PL.txt.
