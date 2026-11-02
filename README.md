# Himmelsstation / Sky Station

Live-Infos vom Himmel und aus dem All für das **LILYGO T-Display-S3** (ESP32-S3, 1,9″, 320 × 170).
Das Geschwister-Projekt zur [Wetterstation](https://github.com/ProD-byte/Wetterstation): gleiches Gerät, gleiche Bedienung.

**Online-Flasher:** https://prod-byte.github.io/Himmelsstation/ (Chrome oder Edge, Board per USB-C anschließen)

> 🚧 Im Aufbau. Was geplant ist, steht in [KONZEPT.md](KONZEPT.md).

## Was sie schon kann

| | |
|---|---|
| 🌌 **Heute Nacht** | wann es richtig dunkel wird (astronomische Dämmerung), Zeitleiste mit Dämmerungsstufen, Mond am Himmel und Wolken je Stunde, dazu eine Note fürs Beobachten. Sonne und Mond rechnet das Gerät selbst (gegen skyfield geprüft: Sonne auf Sekunden, Mond auf ca. 3 min) |
| 🪐 **Planeten** | Merkur, Venus, Mars, Jupiter und Saturn: welche heute Nacht zu sehen sind, von wann bis wann, wo sie am höchsten stehen (Höhe, Richtung) und wie hell. Am Südhimmel eingezeichnet. Selbst gerechnet aus den JPL-Bahnelementen, gegen skyfield auf wenige Bogenminuten geprüft |
| 🛰️ **ISS & Satelliten** | der nächste sichtbare Überflug von ISS, chinesischer Raumstation Tiangong oder Hubble: Bahn am Himmel, kleine animierte Station, Uhrzeit, Dauer, Richtung, Höhe, dazu der Überflug danach. Die Seite erscheint nur, wenn in den nächsten 20 h einer kommt; 5 min vorher springt die Station dorthin und zeigt live, wohin du schauen musst. Bahndaten täglich von [CelesTrak](https://celestrak.org/), gerechnet mit SGP4 auf dem Gerät |
| 🌌 **Weltraumwetter** | Kp-Index jetzt und als Verlauf (24 h gemessen + 3 Tage Vorhersage), **Polarlicht-Chance für deinen Ort** (aus Kp und geomagnetischer Breite, z. B. Cottbus ab Kp 7), Sonnenwind und stärkste Sonneneruptionen (24 h / 7 Tage). Bei guter Chance wabert Polarlicht über die Seite; reicht der Kp gerade und ist es dunkel, springt die Station von selbst dorthin. Daten: [NOAA SWPC](https://www.swpc.noaa.gov/) |
| 🚀 **Raketenstarts** | der nächste Start weltweit mit Countdown (T- …, in der letzten Stunde sekundengenau), Status (Go, unsicher, angehalten, im Flug, erfolgreich), Rakete, Betreiber, Mission, Umlaufbahn und Startplatz, dazu die zwei Starts danach. 10 min vor einem Start mit „Go“ springt die Station hin, die kleine Rakete zittert und hebt zum Start ab. Daten: [Launch Library 2](https://thespacedevs.com/llapi) (The Space Devs) |
| 🌙 **Mond** | Mondphase als Bild, beleuchteter Teil, nächster Voll- und Neumond. Das Gerät rechnet das selbst aus dem Datum aus |
| ✨ **Nachthimmel** | funkelnde Sterne als Hintergrund, Morgen- und Abendrot um Sonnenauf- und -untergang |
| 📶 **WLAN** | Einrichtung per Handy (WLAN „Himmelsstation-Setup“), merkt sich bis zu 5 Netze |
| ⚙️ **Einstellungen** | im Browser unter **http://himmelsstation.local**: Sprache, Ausrichtung, Ort, Name im Netz, Helligkeit, Seiten, Blättern, Nachtmodus, Updates |
| 🌐 **Weltweit** | Uhrzeit, Nachtmodus, Sonnenauf- und -untergang passend zur Zeitzone des Orts |
| 🔃 **Ausrichtung** | quer oder hochkant, jeweils in beide Richtungen |
| 🔄 **Updates** | von selbst aus dem Internet (neue Builds von GitHub), über WLAN (OTA) oder mit dem Online-Flasher |

## Was als Nächstes kommt (Version 1)

Seeing & Durchsicht · Starlink-Ketten · Meteorströme · Finsternisse · die Sonne live (SDO)

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
| `src/himmel.cpp` | Hintergrund-Aufgabe: Ort, Zeitzone, Wolken je Stunde, Updates; Mond-Rechnungen |
| `src/satelliten.cpp` | Bahndaten laden, Überflüge mit SGP4 berechnen |
| `src/weltraum.cpp` | Weltraumwetter von NOAA, Polarlicht-Schwelle für den Ort |
| `src/starts.cpp` | Raketenstarts von Launch Library 2 (sparsam: alle 30 min, kurz vor einem Start alle 10 min) |
| `src/astro.cpp` | Sonne, Mond und Planeten selbst gerechnet: Höhe, Richtung, Auf-/Untergang, Dämmerung, Helligkeit |
| `src/bilder.cpp` | Himmel, Mondphase, Text mit Umlauten |
| `src/wlan.cpp`, `src/portal_html.h` | WLAN mit Einrichtung per Handy (inkl. Ortswahl) |
| `src/web.cpp`, `src/web_html.h` | Einstellungsseite |
| `src/ort.cpp` | Standort: speichern, über die Internet-Adresse ermitteln, Orte suchen |
| `src/netzupdate.cpp` | Update aus dem Internet |
| `simulation/himmelsstation.html` | Simulation im Browser |
| `docs/` | Online-Flasher (GitHub Pages) |

## Lizenz

MIT, siehe [LICENSE](LICENSE). Schrift Lato: SIL Open Font License ([lizenzen/OFL-Lato.txt](lizenzen/OFL-Lato.txt)).
Daten: [Open-Meteo](https://open-meteo.com/) (CC BY 4.0), Bahndaten: [CelesTrak](https://celestrak.org/), Weltraumwetter: [NOAA SWPC](https://www.swpc.noaa.gov/), Raketenstarts: [The Space Devs](https://thespacedevs.com/).
