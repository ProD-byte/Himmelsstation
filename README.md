# Himmelsstation / Sky Station

Live-Infos vom Himmel und aus dem All für das **LILYGO T-Display-S3** (ESP32-S3, 1,9″, 320 × 170).
Das Geschwister-Projekt zur [Wetterstation](https://github.com/ProD-byte/Wetterstation): gleiches Gerät, gleiche Bedienung.

**Online-Flasher:** https://prod-byte.github.io/Himmelsstation/ (Chrome oder Edge, Board per USB-C anschließen)

> 🚧 Im Aufbau. Was geplant ist, steht in [KONZEPT.md](KONZEPT.md).

## Was sie schon kann

| | |
|---|---|
| 🌙 **Mond** | Mondphase als Bild, beleuchteter Teil, nächster Voll- und Neumond. Das Gerät rechnet das selbst aus dem Datum aus |
| ✨ **Nachthimmel** | funkelnde Sterne als Hintergrund, Morgen- und Abendrot um Sonnenauf- und -untergang |
| 📶 **WLAN** | Einrichtung per Handy (WLAN „Himmelsstation-Setup“), merkt sich bis zu 5 Netze |
| ⚙️ **Einstellungen** | im Browser unter **http://himmelsstation.local**: Sprache, Ausrichtung, Ort, Name im Netz, Helligkeit, Seiten, Blättern, Nachtmodus, Updates |
| 🌐 **Weltweit** | Uhrzeit, Nachtmodus, Sonnenauf- und -untergang passend zur Zeitzone des Orts |
| 🔃 **Ausrichtung** | quer oder hochkant, jeweils in beide Richtungen |
| 🔄 **Updates** | von selbst aus dem Internet (neue Builds von GitHub), über WLAN (OTA) oder mit dem Online-Flasher |

## Was als Nächstes kommt (Version 1)

Heute Nacht (Dunkelheit, Wolken, Seeing) · Planeten · ISS & Satelliten · Weltraumwetter mit Polarlicht-Alarm · Raketenstarts

## Tasten

| Taste | kurz | lang |
|---|---|---|
| **IO0** (BOOT) | nächste Seite | 2 s: Deutsch/Englisch, zeigt Adresse und Build |
| **IO14** | Helligkeit | 3 s: WLAN und Ort neu einrichten |

## Selbst bauen

PlatformIO (VS Code), Umgebung `tdisplay_s3` (USB) oder `tdisplay_s3_wlan` (über WLAN, Passwort in `geheim.ini`, Vorlage `geheim.beispiel.ini`).

| Datei | Inhalt |
|---|---|
| `src/main.cpp` | Seiten, Tasten, Startbild, Update über WLAN |
| `src/himmel.cpp` | Hintergrund-Aufgabe: Ort, Zeitzone, Sonne, Wolken, Updates; Mond-Rechnungen |
| `src/bilder.cpp` | Himmel, Mondphase, Text mit Umlauten |
| `src/wlan.cpp`, `src/portal_html.h` | WLAN mit Einrichtung per Handy (inkl. Ortswahl) |
| `src/web.cpp`, `src/web_html.h` | Einstellungsseite |
| `src/ort.cpp` | Standort: speichern, über die Internet-Adresse ermitteln, Orte suchen |
| `src/netzupdate.cpp` | Update aus dem Internet |
| `simulation/himmelsstation.html` | Simulation im Browser |
| `docs/` | Online-Flasher (GitHub Pages) |

## Lizenz

MIT, siehe [LICENSE](LICENSE). Schrift Lato: SIL Open Font License ([lizenzen/OFL-Lato.txt](lizenzen/OFL-Lato.txt)).
Daten: [Open-Meteo](https://open-meteo.com/) (CC BY 4.0).
