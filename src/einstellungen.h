#pragma once
// Einstellungen (Einstellungsseite himmelsstation.local und Tasten), ueberstehen Updates.
// Eigener Speicherbereich "himmel": wer die Himmelsstation ueber die Wetterstation spielt, faengt hier frisch an
// (die gespeicherten WLANs bleiben).

#include <Arduino.h>

const int SEITEN_ANZAHL = 3;  // Heute Nacht, Mond, Demnaechst (weitere Seiten kommen dazu)

struct Einstellungen {
  uint8_t licht = 0;          // Helligkeitsstufe am Tag (0 = hellste)
  uint8_t seiten = 0xFF;      // welche Seiten gezeigt werden (Bit 0 = Heute Nacht ...)
  uint8_t wechsel = 12;       // Sekunden bis zur naechsten Seite, 0 = nur per Taste
  bool nacht = true;          // Nachtmodus: Display abends dunkler
  uint8_t nachtVon = 22;      // Uhrzeit (volle Stunde)
  uint8_t nachtBis = 6;
  bool autoUpdate = true;     // neue Builds von GitHub selbst installieren
  uint8_t sprache = 0;        // 0 = Deutsch, 1 = Englisch
  uint8_t lage = 0;           // Ausrichtung: 0 = quer (USB rechts), 1 = quer gedreht (USB links),
                              //              2 = hochkant (USB unten), 3 = hochkant (USB oben)
  char name[25] = "himmelsstation";  // Name im Netz (name.local) - verschieden, wenn mehrere Stationen im WLAN sind
};

// Name im Netz pruefen: 1-24 Zeichen a-z, 0-9 und "-" (nicht am Anfang/Ende)
bool nameGueltig(const String &n);

extern Einstellungen einst;

// Text in der eingestellten Sprache: TX("Heute", "Today")
#define TX(de, en) (einst.sprache ? (en) : (de))

void einstellungenLaden();
void einstellungenSpeichern();
