# Himmelsstation / Sky Station – Konzept

Live-Infos vom Himmel und aus dem All auf dem LILYGO T-Display-S3.
Die Himmelsstation ist das Geschwister-Projekt zur Wetterstation: gleiches Gerät, gleiche Bedienung, gleiche Technik.

Stand: 2. November 2026. Das ist erst die Planung, gebaut ist noch nichts.

---

## 1. Idee

Für Menschen, die gern wissen, was gerade über ihnen los ist: Lohnt sich heute Nacht der Blick nach oben? Wann kommt die ISS? Welche Planeten sind zu sehen? Gibt es Polarlicht? Wann startet die nächste Rakete?

Das Gerät steht auf dem Schreibtisch oder im Regal und zeigt das von selbst an, schön animiert. Bei besonderen Ereignissen springt es von selbst auf die passende Seite.

**Zielgruppe:** Hobby-Astronomen, Raumfahrt-Fans, Familien mit Kindern, Planetariums-Besucher.

---

## 2. Was von der Wetterstation übernommen wird

| Baustein | aus der Wetterstation |
|---|---|
| Board, Display, Tasten, Akku | T-Display-S3, 320×170, IO0/IO14 |
| WLAN-Einrichtung per Handy | WLAN „Himmelsstation-Setup“, Ortswahl, Sprache vom Handy |
| Einstellungsseite | `http://himmelsstation.local`, Name im Netz änderbar |
| Updates | von selbst aus dem Internet (GitHub Pages), über WLAN, Online-Flasher |
| Darstellung | Lato-Schriften, Statusleiste, Blättern mit Animation, Seitenpunkte, quer/hochkant in 4 Lagen, Deutsch/Englisch |
| Robustheit | Ersatzdienste, schlafende Wolke bei Ausfall, Daten über Neustart retten, längere Pausen bei Fehlern |
| Zeitzone | aus dem Ort (weltweit) |
| Simulation | im Browser, auf der Flasher-Seite eingebettet |
| ISS-Berechnung | fertig im Wetterstation-Branch `iss-ueberflug` (SGP4, gegen skyfield geprüft) |

Grundstimmung: **dunkler Nachthimmel** mit funkelnden Sternen statt Wetterhimmel. Tagsüber dämmrig-blau, damit es zum Thema passt.

---

## 3. Version 1: sechs Seiten

### Seite 1 – Heute Nacht
- Wann es richtig dunkel wird (astronomische Dämmerung) und wann es wieder hell wird.
- Eine Zeitleiste über die Nacht: Dämmerung, Dunkelheit, Mond über/unter dem Horizont, Bewölkung je Stunde.
- **Note für die Nacht**, zum Beispiel „sehr gut zum Beobachten“ oder „lieber Planetarium“ 😉. Sie setzt sich zusammen aus Wolken, Durchsicht (Transparenz), Seeing und Mondlicht.
- Gibt es bei Abruf Wolken: Wolkenanimation wie bei der Wetterstation.

### Seite 2 – Mond
- Mondphase als Bild, mit Beleuchtung in %.
- Auf- und Untergang, Entfernung in km mit Hinweis „Supermond“ bzw. „Minimond“.
- Wie viele Tage bis zum nächsten Vollmond und Neumond.
- Hochkant: Mond groß oben, Werte darunter.

### Seite 3 – Planeten
- Merkur bis Saturn (später Uranus, Neptun): Was ist heute Nacht zu sehen, ab wann, in welcher Richtung, wie hoch, wie hell (Magnitude).
- Darstellung: Horizontleiste (O – S – W), die Planeten als farbige Punkte an ihrem Platz zur besten Beobachtungszeit, darunter eine Liste.
- Hinweis bei Begegnungen, zum Beispiel „Venus und Jupiter heute dicht beieinander“.

### Seite 4 – ISS & Satelliten
- Der nächste sichtbare Überflug: ISS, chinesische Raumstation Tiangong, Hubble und weitere helle Satelliten.
- Sind kürzlich Starlink-Satelliten gestartet, sieht man sie oft noch als „Perlenkette“ am Himmel. Das wird gesondert gemeldet.
- Ansicht wie im Branch `iss-ueberflug`: Himmel aus deiner Blickrichtung, Bahn, kleine animierte Station, Uhrzeit, Dauer, Richtung, Höhe.
- Bei mehreren Überflügen pro Abend: Liste der nächsten drei.

### Seite 5 – Weltraumwetter
- Kp-Index als Balken (0–9) mit Vorhersage für die nächsten 3 Tage.
- **Polarlicht-Chance für deinen Ort** (aus Kp und geomagnetischer Breite).
- Sonnenaktivität: letzte große Eruption (Flare-Klasse C/M/X), Sonnenwind-Geschwindigkeit.

### Seite 6 – Raketenstarts
- Nächster Start weltweit: Rakete, Mission, Startplatz, Countdown.
- Darunter die zwei folgenden Starts.
- Live-Status: „Go“, „verschoben“, „gestartet“, „erfolgreich“.

---

## 4. Hinweise: das Gerät springt von selbst

| Ereignis | Wann | Anzeige |
|---|---|---|
| ISS / heller Satellit kommt | 5 min vorher bis Ende | Live-Position und „Schau nach SW, 34° hoch“ |
| Polarlicht möglich | Kp ≥ Schwelle für den Ort | grün-violett wabernde Animation |
| Raketenstart | 10 min vorher | großer Countdown, Start-Animation |
| Meteorstrom-Höhepunkt | am Abend | Sternschnuppen-Animation, ZHR, Mond stört ja/nein |
| Finsternis (Sonne/Mond) | am Tag selbst | Ablauf mit Uhrzeiten |
| Supermond | am Abend | großer Mond |

Jeder Hinweis lässt sich auf der Einstellungsseite abschalten. Nachts im Nachtmodus bleibt es dunkel, außer man stellt es anders ein.

---

## 5. Woher die Daten kommen

Alles ist kostenlos, und mit einer Ausnahme braucht nichts eine Anmeldung.

| Was | Quelle | Wie oft | Bemerkung |
|---|---|---|---|
| Satellitenbahnen (TLE) | CelesTrak (Gruppen `stations`, `visual`, `last-30-days`) | 1× am Tag | Berechnung auf dem Gerät (SGP4) |
| Sonne, Mond, Planeten, Dämmerung | **rechnet das Gerät selbst** | – | Formeln nach Meeus / Schlyter, auf etwa 1 Bogenminute genau |
| Bewölkung je Stunde | Open-Meteo (`cloud_cover_low/mid/high`) | 1× pro Stunde | kennen wir von der Wetterstation |
| Seeing, Durchsicht | 7Timer (ASTRO) | alle 3 h | nur http, nicht immer erreichbar → dann nur Wolken |
| Kp-Index, Vorhersage, Flares, Sonnenwind | NOAA SWPC (JSON) | alle 15 min | sehr zuverlässig |
| Raketenstarts | Launch Library 2 (The Space Devs) | alle 30 min, vor Starts öfter | ohne Konto nur 15 Abfragen pro Stunde |
| Menschen im All (V2) | Open Notify | 1× am Tag | nur http |
| Meteorströme, Finsternisse | feste Tabelle in der Firmware (bis ca. 2035) | – | ändert sich nicht |

---

## 6. Einstellungen (himmelsstation.local)

Übernommen von der Wetterstation:
- Sprache, Ausrichtung, Ort, Name im Netz, Helligkeit, Seiten, Blättern, Nachtmodus, Updates

Neu:
- Satelliten: nur ISS / Raumstationen / alle hellen / Starlink-Ketten
- Mindesthöhe für Überflüge (10° / 20° / 30°)
- Polarlicht-Alarm ab Chance niedrig / mittel / hoch
- Raketenstarts: alle / nur Europa / nur bemannte
- Hinweise einzeln an/aus, nachts wecken ja/nein

---

## 7. Technik und Risiken

**Rechnen auf dem ESP32:** Der ESP32-S3 rechnet `double` ohne eigene Hardware dafür, also langsamer.
- Für die ISS haben wir das schon gemessen: ein Überflug ist in Sekundenbruchteilen berechnet.
- Für etwa 20 Satelliten und 7 Planeten rechnet das Gerät im Hintergrund (Kern 0), die Animation läuft auf Kern 1 weiter.

**Speicher:** 8 MB PSRAM reichen locker. Die Bahndaten von 20 Satelliten sind nur etwa 3 KB.

**Abfrage-Grenzen:** Launch Library hat 15 Abfragen pro Stunde. Deshalb sparsam abfragen und speichern. Ersatz bei Ausfall: die letzte bekannte Liste mit „Stand …“ zeigen.

**Genauigkeit prüfen:** Jede Berechnung wird wie bei der ISS gegen skyfield getestet: Sonne, Mond, Planeten, Dämmerung, Überflüge.

**Das sollten wir zuerst testen:**
1. Planeten-Positionen auf dem Gerät gegen skyfield (Genauigkeit, Rechenzeit).
2. Sind 7Timer und Launch Library über HTTPS bzw. HTTP vom ESP32 aus stabil erreichbar?

---

## 8. Fahrplan

1. **Repo anlegen** `ProD-byte/Himmelsstation`. Das machst du in GitHub Desktop, mit Pages-Quelle „GitHub Actions“.
2. **Grundgerüst** aus der Wetterstation übernehmen: WLAN, Einrichtung, Einstellungsseite, Updates, Flasher, CI. Dazu der Nachthimmel als Hintergrund.
3. **Rechenkern:** Sonne, Mond, Dämmerung, Planeten, SGP4 – mit Tests gegen skyfield.
4. **Seiten 1–6**, quer und hochkant.
5. **Datendienste:** NOAA, Open-Meteo, 7Timer, Launch Library, jeweils mit Ersatzlösung.
6. **Hinweise**, die das Gerät von selbst auf die Seite springen lassen.
7. **Simulation**, Flasher-Seite, Abschnitt auf daveslab.de/basteln.
8. **Video** (Trailer und Tutorial wie bei der Wetterstation).

---

## 9. Ideen für Version 2

- **Sonne heute:** aktuelles Bild der Sonne von NASA SDO (Sonnenflecken), als JPEG entpackt.
- **ISS live:** Weltkarte mit Position, Bahn, Tag-Nacht-Grenze und Menschen im All.
- **Himmelskalender:** die nächsten Ereignisse als Liste.
- **Sternbild im Zenit:** Was steht gerade genau über mir?
- **Planetarium-Ecke:** nächste Vorführungen, falls das Planetarium einen Veranstaltungs-Feed anbietet (vorher dort klären).
- **Beide Programme auf einem Gerät:** Wetterstation und Himmelsstation, umschaltbar.
