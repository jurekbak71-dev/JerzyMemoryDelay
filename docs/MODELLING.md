# Zakres modelowania i odniesienia

Materiały dostarczone przez użytkownika:

- Zdjęcia i opis EHX Deluxe Memory Man: sześć kontrolek, analogowe echo do 550 ms, chorus/vibrato, Level, Blend, Feedback i bypass.
- David Morrin, „Electro Harmonix Memory Man”, „EH BBD ECHO ISSUE A” i „EH 7500 Memory Man”: różnice SAD1024/MN3005, brak kompandera w EH7500, bramka CA3094 we wczesnym Issue A, półkowy boost.
- Schemat Memory Man Economy EH7500, datowany 25.01.1978: trzy SAD1024A, filtr wejściowy, filtr rekonstrukcyjny, CD4047, tor boost i sprzężenia.
- Schemat Deluxe EH7550 (dwie części strony 1 i strona 2): sześć sekcji SAD1024, kompresor i ekspander NE570, filtracja oraz zegar CD4047.
- PDF „Electro-Harmonix-Deluxe-Memory-Man-Delay-Reissue-(Rev.A)-Schematic.pdf”: EC2000, MN3005, NE570, CD4047, zasilanie 24 V.
- Schemat „3rd Dimension Chorus” jest dodatkową referencją modulacji; nie został błędnie przypisany do rodziny delayów EHX.

Materiały odróżniają pokrewne wersje o tej samej nazwie handlowej. Profil Squelch nie reprezentuje wszystkich wariantów EH7550/EH1307; zachowanie bramki jest przybliżone.

## Tor cyfrowy

Wejście → Level / opcjonalny półkowy Boost → suma ze sprzężeniem → filtr górnoprzepustowy 65 Hz → dwa bieguny filtru wejściowego → kompresor sterowany obwiednią (wybrane modele) → miękkie nasycenie → komórki BBD z niezależnym zegarem → ekspander sterowany obwiednią → dwa bieguny filtru wyjściowego → opcjonalny Squelch → Blend.

Model komórek wykonuje przechwycenia w chwilach transferu ładunku, interpolując wejście między próbkami hosta. Rzeczywista częstotliwość dwufazowego zegara układu scalonego jest konwencją sprzętową; w kodzie „tick” oznacza jeden przesuw komórki. Liczba komórek i tempo transferu wyznaczają czas opóźnienia. Filtry tracą pasmo przy wolniejszym zegarze. LFO moduluje zegar, a zmiana czasu płynnie przestraja odczytywane audio.

Kompander jest parą przybliżonych komplementarnych charakterystyk z detektorami obwiedni, z atakiem 2 ms i powrotem 35 ms. Nie jest symulacją układu NE570 na poziomie elementów. Nasycenie jest charakterystyką tanh; nie ma oversamplingu nieliniowości. Zegar nie generuje celowego gwizdu, a układ nie dodaje sztucznego szumu do ciszy.

Rekonstrukcja nie obejmuje tolerancji poszczególnych kondensatorów, biasu każdego stopnia, starzenia, symulacji szyn zasilania, fizycznego clock bleed ani złączy direct out. Stereo jest rozszerzeniem użytkowym. Wzmocnienie normalnego wejścia znormalizowano do unity w DAW; Boost EH7500 zachowuje przybliżone proporcje LF 3.5/2 i HF 4.5/2 względem trybu normalnego, z umownym punktem półki 1.2 kHz.

## Co wymaga porównania ze sprzętem

Maksymalne czasy SAD, punkty filtrów, headroom, progi Squelch, charakterystyka LFO oraz współczesne profile Boy/Toy pozostają wartościami projektowymi. Nie należy opisywać ich jako pomierzonych. Wierność wymaga referencyjnych nagrań: sweep przy kilku czasach, impulsy, poziomy wejścia, przebiegi modulacji i narastanie sprzężenia. Kod i testy potwierdzają działanie wtyczki, nie historyczną zgodność brzmienia.
