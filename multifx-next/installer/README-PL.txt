JERZY MULTIFX NEXT — DWIE WERSJE DO TESTOW

To instalator ze zrodel: kompiluje na Twoim Pi, wiec nie wymaga odgadywania
wersji Raspberry Pi OS. Zawiera host, cztery pluginy i ich zrodla/licencje.
Nie jest obrazem karty SD, usluga ani aplikacja z GUI.
Wymagania: Linux ARM64 (Pi OS 64-bit), Python >=3.10, CMake >=3.25,
kompilator C++20, make i pkg-config. Dodatkowo naglowki libjack-jackd2-dev
(wariant JACK2) lub libasound2-dev (ALSA). Instalator nic nie pobiera.
Nie uruchamia apt, nie zmienia JACK-a, uslug ani ustawien systemowych.
Jesli brakuje zaleznosci, konczy sie bledem przed instalacja.

Po rozpakowaniu w terminalu:
  python3 install.py
  python3 install.py --install
Pierwsza komenda tylko sprawdza paczke i zaleznosci. Druga kompiluje i zapisuje
wersje pod ~/.local/opt/jerzy-multifx-next/. Nie uruchamia audio.
Opcjonalnie: --prefix /wybrany/katalog --jobs 2 --cmake /sciezka/do/cmake
Zachowaj wydrukowana sciezke instalacji. start.sh --help nie otwiera audio.

JACK2: najpierw uruchom dotychczasowy serwer JACK2 ze swoim ustawieniem
44100 Hz / 64 probki / 3 okresy, potem start.sh. Polacz porty
JerzyMultiFXNext przez qjackctl/jack_connect z odpowiednimi portami HeadRush.
Host nie startuje serwera i sam nie zmienia jego routingu.

ALSA: recznie zatrzymaj host JACK i serwer JACK uzywajacy HeadRusha, zanim
uruchomisz start.sh --device hw:CARD=Core,DEV=0 --rate 44100 --period 64
--periods 3 --capture-channels 6 --playback-channels 4
(powyzej jedna komenda). Domyslne mapowanie wejsc 1/2 i wyjsc 1/2.
Mono gitarowe na obu kanalach: dodaj --in-left 1 --in-right 1.
Numeracje i sygnal sprawdz z wlasciwym kanalem HeadRusha.
Brak wsparcia ustawienia w sterowniku powoduje blad, bez cichej zmiany rate.
MIDI: port ALSA sequencer JerzyMultiFXNextALSA, podlacz przez aconnect.
--midi required wymusza obecnosc MIDI; --midi off wylacza.
Auto moze uruchomic audio bez MIDI: STATS pokazuje midi_available.
Zdarzenia ALSA MIDI maja czas na granicy kolejnego bloku, bez synchronizacji
sample-accurate z JACK. Watek MIDI jest oddzielny od DSP.

Obie wersje maja identyczny protokol tekstowy na stdin, np.:
  PING
  ADD "/pelna/sciezka/instalacji/plugins/gain/plugin.so"
  LIST
  SET 1 gain 0.5
  STATS
  SAVE "/pelna/sciezka/preset.json"
  QUIT
Komendy wysylaj pojedynczo, czekaj na OK i potwierdzenie grafu przez audio.
Pelny protokol opisany w source/multifx-next/README.md i docs.
Preset zapisuje absolutne sciezki pluginow. Do porownania obu backendow
wskazuj te same zaufane .so z jednej z tych instalacji.
Nie kasuj tej instalacji, dopoki presety odnosza sie do jej pluginow.
CAB potrzebuje pliku WAV IR; bridge legacy potrzebuje starego .so.

STATS: blocks, czas DSP, deadline_misses, xruns; ALSA takze format, buffer,
rate, mapowanie w opcjach i realtime (czy udalo sie SCHED_FIFO 20).
Brak realtime nie jest pomiarem stabilnosci; ustawien uprawnien instalator
nie zmienia. Uruchamianie jako root nie jest wymagane ani zalecane.
Test null nie uzywa karty ani nie mierzy opoznienia USB.
Brak potwierdzenia na fizycznym Pi/HeadRush: sprawdz glosnosc od niskiego poziomu.
Round-trip zmierz fizycznym loopbackiem przy tym samym routingu, IR i presetach.
Nie uruchamiaj obu backendow naraz na tym samym interfejsie.

Usuniecie: zakoncz host QUIT, zachowaj presety, usun jego katalog wersji.
Nie ma systemowej deinstalacji ani zmian do cofania w uslugach.
