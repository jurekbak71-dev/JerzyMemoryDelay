# JACK2 i ALSA: zakres porównania

Wspólny Engine/JFX3 ma dwa rzeczywiste backendy. JACK używa istniejącego serwera,
portów audio/MIDI oraz transportu; bezpośredni ALSA obsługuje przechwytywanie,
odtwarzanie, recovery XRUN i własny wątek. Usunięcie serwera nie stanowi dowodu
mniejszej latencji: znaczenie mają negocjowane bufory, planowanie, USB i sterownik.
Pozostawiamy Core 44100/64/3,6 wejść/4 wyjścia jako domyślne ustawienia testu.

Odczytane referencje:
- PiPedal: osobne AudioDriverHost, JACK i ALSA; dokumentacja
  https://github.com/rerdavies/pipedal/blob/main/docs/AudioLatency.md opisuje
  bezpośrednie ALSA i fizyczny pomiar. Wyniki MOTU/PiPedal nie są wynikami Core/Jerzy.
  Konkretne źródła/licencje są wskazane w AUDIT.md.
- MOD mod-host: LV2/Lilv i callback JACK, szerszy protokół portów/stanu;
  https://github.com/mod-audio/mod-host/blob/12335c2653740be7fe9ce5b4e017f4086bd5c071/src/effects.c .
  Nie kopiowano kodu GPL do tego silnika.
- JerzyAudioPlatform: odczytany 2026-11-24 plik
  https://github.com/jurekbak71-dev/JerzyAudioPlatform/blob/main/core/main.cpp
  rzeczywiście otwiera JACK z JackNoStartServer, rejestruje callback i zbiera CPU/XRUN.
  To potwierdza istnienie kodu, nie wdrożenie ani osiągi urządzenia użytkownika.
- JACK wymagania callbacku: https://jackaudio.org/api/group__ClientCallbacks.html .

Pomiar w tym środowisku obejmuje DSP, sterownik dummy/null i funkcjonalne testy
obu hostów. Nie obejmuje USB, HeadRush, priorytetu RT dostępnego na Pi ani fizycznej
latencji. Narzędzia i zakres histogramów opisuje BENCHMARK_RPI.md. Bez identycznych
pomiarów na Pi nie ma podstaw do uznania JACK2 lub ALSA za szybsze/lepsze.
