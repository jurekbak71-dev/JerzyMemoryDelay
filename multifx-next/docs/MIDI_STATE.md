# MIDI, master i presety

Spec grafu obejmuje niezmienne mapowania CC (maks128). Pola channel,cc,target,param,
min,max publikuje się jednym planem: audio nie może zobaczyć połowy nowej mapy.
Target>0 to parametr slotu,0 mastergain0..1.4,-1 tempo20..300; kanały1..16.

Audio trzyma prywatne wektory parametrów/tempo/master. Zdarzenia NoteOn/Off/CC/PC
z offsetem w bloku są sortowane; mapowane CC dzielą przetwarzanie na odcinki.
Wskaźniki audio i transport.frame oraz offsety MIDI są przesuwane dla segmentu.
Nie ma per-segment alokacji. Granica zdarzenia zmienia parametry/BPM przed
pierwszą próbką segmentu; transport z ważnym JACK BBT pozostaje nadrzędnym źródłem
BPM dla DSP. CC master jest zastosowane sample-accurate w końcowym miksie.
Nuty dla instrumentu nie wymagają dzielenia bloków, bo JFX3 dostaje ich offsety.
Bezpośredni ALSA sequencer nadal nadaje frame0 następnego bloku: nie deklarujemy
sprzętowego timestampowania ALSA z dokładnością próbki. Overflow resetuje nuty,
nie interpretuje wytworzonych panic jako edycji mapowań CC.

Kontroler odczytuje atomowe obserwacje parametrów/master/BPM. Sekwencja epoki i
każde pole obserwacji są SC: stabilna parzysta epoka nie może obejmować pól dwóch
aktualizacji. Czytelnik powtarza próbę wyłącznie poza RT. Pamięć parametrów audio
nigdy nie jest czytana przez kontroler. Zmiany ze sprzętu są zapisywane w presetach.
Jawne bity parameter_updates oraz flagi master_update/bpm_update odróżniają
SET od edycji routingu: niezmienione wartości są dziedziczone na granicy bloku
z najnowszego runtime, więc publikacja grafu nie nadpisuje nowszego MIDI.
LOAD jest autorytatywnym snapshotem. SET może także przywrócić wartość równą
starej deklaracji po zmianie MIDI. MASTER operuje faktycznym gainem (stary preset
normalized master jest migrowany przez mnożnik1.4). Usunięcie slotu usuwa jego
bindingi; CLEAR pozostawia tempo/master i globalne bindingi.

Test NextCCBinding sprawdza offset16 parametru,32 mastera, zapis/restore,
niezależną edycję i powrót do poprzedniej wartości oraz odrzucenie wadliwej mapy.
NextPluginExtensions sprawdza offset31 BPM, realny generator bez wejść oraz błąd
prawidłowego ABI zwracającego nullptr z create. NextCCConcurrency równocześnie
wymienia1000 generacji dwóch bindingów i odczytuje20000 spójnych obserwacji;
nie należy utożsamiać tego z pełnym przeszukaniem wszystkich harmonogramów.
