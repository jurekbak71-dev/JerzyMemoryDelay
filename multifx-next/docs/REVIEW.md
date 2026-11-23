# Niezależny przegląd i poprawki

Przegląd zakresu `76ebf1d..5510128` wykonał niezależny agent z zadaniem read-only.
Werdykt początkowy: request changes, trzy Important, zero Critical, jedna uwaga Minor.
Po przeglądzie wykonano jedną serię poprawek i testów RED -> GREEN; nie wykonywano
ponownego przeglądu przez agenta. Zaliczone testy są dowodem poprawek konkretnych
przypadków, nie gwarancją braku wszystkich błędów.

| Uwaga | Poprawka | Dowód |
|---|---|---|
| Note Off filtrowany po zmianie kanałów MIDI | nowy plan wysyła All Sound Off na usuwane kanały | NextReviewRegressions; RED utrzymany głos, GREEN cisza |
| PAX/GNU omijają limity przed iteracją plików | ograniczony strumień, odrzucenie extension headers przed ich payloadem, USTAR pack | NextPackages; RED instalacja PAX >64 MiB, GREEN odrzucenie PAX i GNU |
| JACK lost-event count pomijany | jack_midi_get_lost_event_count uruchamia panic | NextJACK; realny mixdown dwóch portów i utrata Note Off, RED tail_variation=0.0375839, GREEN końcowa cisza instrumentu |
| Liczniki przypisane do pozycji, nie instancji | atomowe statystyki w Instance, slot_ids w STATS | NextReviewRegressions; RED brak ID/pomyłka historii, GREEN prawidłowa historia po MOVE |

Ostatnią uwagę podniesiono do Important przy kwalifikacji: użytkownik wymaga pomiarów
instancji, więc błędna identyfikacja nie jest wyłącznie kosmetyką.

Rozstrzygnięcia pozycji „Declined to judge”:

- Nieaktualne README/TESTING: zastąpione opisem rzeczywistego prototypu i wyników.
- Brak sandboxu native plugins: zgodny z zatwierdzonym modelem zaufania; błąd pluginu może
  przerwać host. Nie deklarujemy odporności na dowolny kod natywny.
- Click-free/tails/held-note replay: pozostają jawnymi ograniczeniami pierwszego prototypu;
  zmiana planu lub bypass może uciąć ogon lub kliknąć. Nie wznowimy starych nut po bypassie.
- Opaque pamięć/latencja ABI2 i optymalizacja: ograniczenia starego ABI pozostają;
  adapter nie może dostarczyć brakujących danych bez natywnego portu DSP.
- Pi/HeadRush performance: brak sprzętu, brak wniosku o latencji lub gotowości scenicznej.

Nie pozostały odłożone drobne uwagi z tego przeglądu. Nie wykonano merge/push/release.
