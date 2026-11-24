# Własność i przejścia stanów

Jeden kontroler tworzy immutable Spec/Plan i wszystkie bufory/instancje. Release
pending -> acquire audio na granicy bloku. Tylko audio zmienia bufory i liczniki
ramp. Old Plan pozostaje własnością kontrolera aż do release ack, odczytanego przez
acquire collect. Audio nie wraca do poprzednika po ack. Destrukcja backendu musi
zatrzymać/join audio przed destrukcją Engine. Nie ma shared_ptr refcount w callbacku.

ACTIVE->BYPASS: 64 próbki crossfade wet/dry, później zero wywołań DSP. Instancja
pozostaje. ACTIVE->UNLOADED: stare DSP jest wypożyczone z poprzedniego planu do
ostatniej próbki rampy; dopiero collect niszczy plan/instancję poza audio.
BYPASS->ACTIVE dla FX ponownie używa przygotowanej instancji z rampą dry/wet.
Instrument MIDI odtwarza instancję z parametrami, aby także plugin ignorujący
panic nie przywrócił zatrzymanych nut; może to wymagać czasu ładowania samplera.
Instrument zamiast dry ma zero; panic trafia na ostatnią używaną próbkę rampy,
żeby nie wyciąć nut przed fade. Opóźniony dry ring przechodzi przez swap buforów
na wątku audio bez alokacji; nigdy nie jest czytany/kopiowany przez kontroler.
Podmiana działającego IR/instancji o tej samej tożsamości: nowe DSP rozgrzewa się
przez deklarowaną latencję, potem 64 próbki crossfade; oba plany żyją do ack.

MODE id BYPASS|UNLOADED [tail_frames] pozwala na opcjonalny, skończony spill FX,
maksymalnie10s. Wejście starego DSP opada do zera przez64 próbki, dry się pojawia,
astępnie ogon wybrzmiewa i schodzi do zera na końcowych64 próbkach. To nie detektor
ciszy. Instrumenty nie używają trybu spill. Kontroler raportuje busy do końca tego
przejścia; nie publikuje nieograniczonej kolejki zmian. QUIT/shutdown może zakończyć
wcześniej po zatrzymaniu audio. Nie obiecujemy zachowania ogona po natychmiastowym
REMOVE/CLEAR; do kontrolowanego wybrzmienia użyj najpierw MODE z tail_frames.

Testy Transition/Tail sprawdzają próbki, skok rampy<.02 na badanym stałym sygnale,
retirement przy blokach16 i realne wybrzmiewanie filtra z pamięcią. To nie dowód
niesłyszalności dla każdego arbitralnego sygnału/wtyczki. Opaque DSP state z ABI2
nie jest serializowany; odtwarzane są parametry, nie zawartość delayów.
