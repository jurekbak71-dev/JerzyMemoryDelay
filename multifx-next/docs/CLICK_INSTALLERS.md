# Instalatory obsługiwane myszką

WYKONANO: dwa pakiety Debian `jerzy-multifx-next-{jack2,alsa}_0.2.0_all.deb`.
Każdy dodaje własną ikonę do menu Dźwięk i obraz. Pierwsze uruchomienie otwiera
okno z przyciskiem Zainstaluj, postępem, komunikatem wyniku i ukrytymi szczegółami.
Po przygotowaniu dostępne są Uruchom audio, Zatrzymaj, kanały wejść/wyjść,
suwak testowego efektu gain, włączenie/wyłączenie efektu i statystyki.
Nie trzeba używać terminala. GUI uruchamia prawdziwy istniejący host C++ i plugin.

Debian instaluje zależności (Python/Tk, CMake, kompilator, właściwe SDK audio)
przez systemowy menedżer pakietów. Kod kompiluje się później bez roota do osobnego
katalogu użytkownika. Architecture all oznacza kod źródłowy i Python, nie binarium
niezależne od architektury. Pakiety nie zmieniają usług, autostartu ani konfiguracji
serwera JACK. Można je zainstalować równocześnie, ale na HeadRushu uruchamiać osobno.

JACK startuje wyłącznie klienta istniejącego serwera i łączy własne porty z wybranymi
fizycznymi portami. Nie uruchamia/nie zatrzymuje serwera. ALSA domyślnie Core hw,
44,1 kHz, 64/3, 6 capture/4 playback; wybór testu null jest opisany jako bez dźwięku.
Mono wejście 1 trafia domyślnie na oba kanały. Wyjścia 1 i 2, zmiana przez pola.
To minimalny panel do A/B, nie pełny edytor efektów z historycznych wersji.

ZWERYFIKOWANO: pakiety mają rozłączne pliki, root:root i czytelne katalogi/pliki,
Terminal=false, poprawne .desktop i zależności z minimalnymi wersjami. Wersja g++
uwzględnia Debian epoch: >=4:11. Źródła i panel objęte checksumami; .deb ma md5sums.
Instalacja istniejącej kompletnej wersji jest ponownie wykrywana. Niekompletna
wersja nie jest uznawana za gotową: GUI zachowuje ją w katalogu .incomplete-* i
buduje ponownie bez usuwania jej danych. Zamknięcie okna kończy własne procesy.
Zdarzenia wątku są przypisane do sesji, więc wynik starego efektu nie wznawia panelu
po Stop. Błąd sterowania audio kończy host przed ponownym umożliwieniem Start.

TESTY: test pakietów buduje i rozpakowuje oba .deb przez dpkg-deb, odczytuje
zależności, waliduje .desktop i uruchamia --help GUI bez wyświetlacza. Testy okien
pod Xvfb używają Tk 8.6 z systemowego Python na x86_64, rzeczywistego procesu
instalacji, realnego JACK2 dummy i ALSA null; wywołują prawdziwe przyciski Tk.
Sprawdzają wadliwą instalację bez hosta, błąd kompilatora, naprawę, instalację,
ponowne wykrycie, start, gain, STATS, Stop w trakcie operacji efektu, błąd ADD oraz
zamknięcie okna podczas pracy audio. Oba testy GUI przeszły. Regresja CTest 19/19.
Niezależny przegląd potwierdził usunięcie obu istotnych uwag; brak pozostałych
istotnych uwag. Logi: docs/evidence/click/.
To testy automatyczne widgetów, nie ręczne
kliknięcia na fizycznym Pi. Sam build i --help nie otwierają urządzenia audio.

NIEZWERYFIKOWANE: instalacja przez graficzny menedżer pakietów na fizycznym Pi,
aktualne wydanie Pi OS użytkownika, fizyczny HeadRush, latencja round-trip,
zgodność zasobów zależności w docelowych repozytoriach APT. Menedżer pakietów
powinien odmówić instalacji przy niespełnionych Depends zamiast instalować niezgodny
host. Środowisko testowe nie pozwala instalować pakietów systemowo jako root;
sprawdzano zbudowane pakiety po rozpakowaniu, nie wpis w systemowej bazie dpkg.

NIEWYKONANE: pomiary sprzętowe i pełny edytor pluginów/presetów. Nie wykonano push,
merge, release ani zmian działającego Pi. Odinstalowanie .deb usuwa ikonę i źródła
z /opt, pozostawia osobiste skompilowane wersje/presety w ~/.local/opt.

ŹRÓDŁA: faktycznie odczytane deploy/multifx4/install.sh, installer/install.py,
tools/build_installers.py, src/jack_backend.cpp, src/session.cpp; narzędzia
systemowe dpkg-deb i desktop-file-validate; testy click_package_test.py oraz
click_gui_test.py. Nie znaleziono wcześniejszego .deb/.desktop w tym checkout.
Instrukcja użytkownika: installer/KLIKNIJ-PRZECZYTAJ.txt.
