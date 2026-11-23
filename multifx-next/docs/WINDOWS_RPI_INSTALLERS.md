# Instalacja RPi uruchamiana z Windowsa

WYKONANO: dwa ZIP-y Jerzy-RPi-jack2-Windows.zip i Jerzy-RPi-alsa-Windows.zip.
START.cmd otwiera okno Windows PowerShell/WinForms: adres, użytkownik, ukryte hasło,
przycisk instalacji, status i log. Dane domyślne: jerzy@10.42.0.1. Narzędzia PuTTY
są w ZIP, więc Windows nie musi pobierać ich po przełączeniu na hotspot.
Połączenie SSH/SFTP kopiuje pełne źródła i uruchamia instalację natywną bez roota
oraz bez internetu na Pi. Instalacja dodaje ikonę panelu w menu pulpitu RPi.
Nie jest to nowy panel WWW dostępny na Windowsie. Do uruchomienia audio używa się
ikony na pulpicie/menu RPi; istniejące historyczne panele WWW są bez zmian.

ZWERYFIKOWANO: git fetch origin i przegląd całej lokalnie dostępnej historii/refów.
W deploy/multifx4/README.md (sekcja Installation — offline RPi4) oraz v0.3 opisano
Windows -> hotspot Jerzy-MultiFX -> scp/ssh -> jerzy@10.42.0.1 -> kompilacja offline.
Nie znaleziono wcześniejszego ps1/cmd/bat w historii repo; wcześniejsze instrukcje
wymagały komend. Nowe okno automatyzuje tę procedurę, zamiast wymagać otwierania .deb
na Windowsie. Hasło pochodzi z okna, nie z repo. Klucz SSH jest pokazywany do
potwierdzenia i przypinany do każdego uwierzytelnionego połączenia. Hasło przekazuje
się przez plik tymczasowy z ACL bieżącego użytkownika, nigdy przez argument -pw.
Plik jest usuwany w finally; nie deklarujemy usunięcia po awarii/zabiciu procesu.
Nie zmienia się systemowej polityki PowerShell (ExecutionPolicy tylko procesu).

Oficjalne plink.exe/pscp.exe PuTTY 0.83 x64 pobrano z serwera autora i porównano
z jego sha256sums. Licencja pochodzi ze źródeł putty-0.83.tar.gz również sprawdzonych
SHA256. W ZIP są licencja i plik pochodzenia/hashów. Nie weryfikowano podpisów PGP
ani Authenticode — potwierdzenie dotyczy SHA256 z oficjalnego HTTPS.

Bootstrap sprawdza typy i rozmiar archiwum, zakazuje ścieżek poza katalogiem,
sprawdza checksumy przed wykonaniem instalatora. Instalacja sprawdza system 64-bit,
Tk, istniejące narzędzia kompilacji i SDK audio. Nie pobiera zależności przez APT.
Brak zależności kończy instalację błędem; nie ma fikcyjnego sukcesu.
Niekompletna poprzednia instalacja zostaje zachowana w .incomplete-* i odbudowana.
Sukces wymaga hosta, launchera, wszystkich pluginów, ich metadanych, licencji CAB
oraz kompletnego, zweryfikowanego źródłowego panelu. Nie zmienia usług/autostartu,
działającego JACK-a, starego MultiFX ani hotspotu; nie wykonuje aktywacji audio.

TESTY: packaging test — oba ZIP-y, launchery, manifesty, pliki PE32+ x64 i checksumy.
PowerShell 7.4.6 pod Linuksem — parser obu ps1, odrzucenie braku klucza, parsowanie
fingerprintu, rzeczywiste procesy i przekazywanie argumentów ze spacjami/cudzysłowami,
wykonanie zakodowanego polecenia Python. To nie jest uruchomienie WinForms/ACL/EXE
w Windows PowerShell 5.1. Obie zdalne części z ZIP uruchomiono lokalnie na x86_64,
w odizolowanym katalogu użytkownika: rzeczywiste bootstrap/hashe, kompilacja, wpis
menu, host --help; ALSA dodatkowo null start/STATS/QUIT. Test niekompletnej instalacji
wykrył fałszywy sukces przed poprawką i przeszedł po niej. Niezależny przegląd
potwierdził usunięcie uwagi. Regresja CTest 19/19. Logi w evidence/windows/.

NIEZWERYFIKOWANE: pełne uruchomienie WinForms, ACL i PuTTY EXE na rzeczywistym
Windows 10/11 x64/PowerShell 5.1, logowanie SSH/SFTP do fizycznego Pi, faktyczne
wydanie OS i zależności Pi, HeadRush i latencja. Nie ma dostępu do tych maszyn.
Sposób sprawdzenia: rozpakować ZIP na Windowsie, połączyć z hotspotem, kliknąć
START.cmd i zainstalować; sprawdzić wynik w oknie, ikonę oraz dźwięk na Pi.

NIEWYKONANE: automatyczne przełączenie działającego audio, nowy panel WWW,
pomiary sprzętowe. Te paczki są przygotowane do próby, nie potwierdzonym wdrożeniem
na docelowym sprzęcie. Nie wykonano push, merge ani release.

ŹRÓDŁA faktycznie odczytane:
- deploy/multifx4/README.md i deploy/multifx3/README.md; deploy/multifx4/activate.sh.
- installer/install.py, installer/click_app.py, src/jack_backend.cpp, src/session.cpp.
- https://the.earth.li/~sgtatham/putty/0.83/htmldoc/Chapter7.html
- https://the.earth.li/~sgtatham/putty/0.83/htmldoc/Chapter3.html
- https://the.earth.li/~sgtatham/putty/0.83/sha256sums
- https://the.earth.li/~sgtatham/putty/0.83/putty-0.83.tar.gz (LICENCE)
