# Testowa instalacja dwóch wariantów

To osobny, eksperymentalny silnik; nie zastępuje działającego MultiFX4.
Pakiety Windows buduje `python3 multifx-next/tools/build_windows_installers.py --output DIST`.
Powstają `Jerzy-RPi-jack2-Windows.zip` i `Jerzy-RPi-alsa-Windows.zip`.

Na Windows 10/11 rozpakuj cały wybrany ZIP, połącz komputer z siecią Pi i kliknij
`START.cmd`. Okno pyta o adres, użytkownika i hasło Pi. Domyślne 10.42.0.1/jerzy
pochodzą z wcześniejszej konfiguracji repozytorium, nie z wykrycia urządzenia.
Pakiet wysyła źródła i buduje je na Pi; potrzebuje istniejącego CMake, kompilatora,
Python/Tk, pkg-config oraz SDK wybranego backendu. Nie instaluje automatycznie
zależności systemowych. Bez nich pokaże błąd. Nie jest to obraz karty microSD.

Każdy wariant trafia do osobnego katalogu użytkownika i osobnej pozycji menu Pi.
Instalacja nie uruchamia audio ani nie zmienia usług. Aby testować ALSA, urządzenie
musi być wolne; produkcyjnego JACK-a nie zatrzymujemy automatycznie. JACK2 używa
istniejącego serwera. Nie uruchamiaj obu hostów na tym samym urządzeniu naraz.

Okno na Pi jest prostym launcherem i kontrolą testowego gain, nie pełnym panelem
MultiFX4. Zaawansowane pluginy, migracja, CAB i MIDI są dostępne przez interfejs
sterowania nowego hosta. Pełna integracja GUI nie jest częścią audytu silnika.
Nie przedstawiamy launchera jako kompletnego zamiennika obsługi starego produktu.

W katalogu wersji: `bin/`, `plugins/` (JFX3, w tym `cab_dual` i adapter `legacy`),
`legacy-catalog/` (13 rzeczywistych bibliotek ABI2 z manifestami), `tools/`.
Migrator `tools/migrate_v04.py --help` opisuje konwersję gitarowych presetów;
nie nadpisuje pliku wejściowego, zgłasza wykluczenie BassAmbient/routingu syntezatora.

Na komputerze deweloperskim testujemy ZIP, sumy kontrolne, zdalny bootstrap
uruchomiony lokalnie w izolowanym HOME, kompilację i uruchomienie hosta.
NIEZWERYFIKOWANE: rzeczywiste okno Windows, SSH do Twojego Pi, konfiguracja systemu
Pi, HeadRush Core i długotrwała praca audio. Instalację sprzętową wykonuje użytkownik.
