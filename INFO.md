# LILYGO T-Display-S3 – Spickzettel

Das Board der Himmelsstation (dasselbe wie bei der Wetterstation, je ca. 17 €).
Hersteller-Repo: https://github.com/Xinyuan-LilyGO/T-Display-S3

## Eckdaten

| | |
|---|---|
| Chip | ESP32-S3R8 (2 Kerne, 240 MHz), WLAN 2,4 GHz, Bluetooth 5 (BLE) |
| Speicher | 16 MB Flash, 8 MB PSRAM (OPI) |
| Display | 1,9″ IPS, ST7789, **170 × 320 Pixel** (quer: 320 × 170), **8-Bit-Parallelbus** (kein SPI) |
| USB | USB-C direkt am ESP32-S3 – kein USB-Seriell-Chip, kein Treiber nötig. Anschluss heißt „USB JTAG/serial debug unit“ |
| Akku | 3,7-V-LiPo an JST 1,25 mm, wird über USB geladen |
| Tasten | BOOT (GPIO 0) und GPIO 14 vorne, RST an der Seite |

## Pins

| Funktion | GPIO |
|---|---|
| Display D0–D7 | 39, 40, 41, 42, 45, 46, 47, 48 |
| Display WR / RD / DC / CS / RST | 8 / 9 / 7 / 6 / 5 |
| Hintergrundlicht | 38 (Treiberchip mit 16 Stufen, wird über Pulse gesteuert, nicht per PWM) |
| **Strom ein** | **15 – muss HIGH sein**, sonst bleibt das Display im Akkubetrieb dunkel |
| Taste BOOT | 0 (Seite) |
| Taste 2 | 14 (Licht) |
| Akkuspannung | 4 (Teiler 1:2, misst nur ohne USB richtig) |
| I2C SDA / SCL (Stecker) | 18 / 17 |
| Touch INT / RES (nur Touch-Version) | 16 / 21 |

Frei für eigene Sachen sind u. a. 1, 2, 3, 10, 11, 12, 13, 16, 17, 18, 21, 43, 44 (je nach Version teils am Stecker).

## Wichtig beim Programmieren

- **Arduino-Kern 2.0.14** (PlatformIO `espressif32 @ 6.5.0`). LILYGO schreibt, dass TFT_eSPI auf diesem Board mit Kern 3.x nicht läuft (TFT_eSPI-Issue #3329) – deshalb nutzt dieses Projekt die ältere Version.
- TFT_eSPI-Setup: `Setup206_LilyGo_T_Display_S3.h` (im Projekt über `build_flags` eingebunden, an der Bibliothek muss man nichts ändern).
- Der Bildspeicher (320 × 170 × 2 Byte = 109 KB) liegt im PSRAM.
- **Upload klappt nicht?** BOOT gedrückt halten, kurz RST drücken, BOOT loslassen → Download-Modus.
- Nach dem Upload startet das Board meist von selbst; sonst einmal RST.

## Strom und Akku

- Akkuspannung über GPIO 4 messen: `analogReadMilliVolts(4) * 2`. Mit Multimeter nachmessen und den Faktor anpassen.
- Solange USB steckt, ist der Wert nicht aussagekräftig.
- **Polung am JST-Stecker vor dem Anschließen prüfen** – nicht jeder Akku ist gleich belegt.

## Quellen

- Hersteller: https://github.com/Xinyuan-LilyGO/T-Display-S3
- TFT_eSPI-Setup: https://github.com/Bodmer/TFT_eSPI/blob/master/User_Setups/Setup206_LilyGo_T_Display_S3.h
- Hintergrundlicht über Pulse: Beispiel `factory.ino` im LILYGO-Repo
