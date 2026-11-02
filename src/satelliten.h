#pragma once
// Ueberfluege heller Satelliten: ISS, chinesische Raumstation Tiangong und Hubble.
// Die Bahndaten (TLE) holt die Station einmal am Tag kostenlos von CelesTrak und rechnet daraus selbst (SGP4),
// wann sie ueber dem eingestellten Ort zu sehen sind - weltweit, ohne Anmeldung.
// Zu sehen = bei uns dunkel (Sonne mind. 6 Grad unter dem Horizont), der Satellit oben aber noch angestrahlt.

#include <Arduino.h>

const int SATELLITEN = 3;
const char *satName(int i);

struct SatPunkt {
  int16_t az10, el10;  // Richtung (0 = Nord, 90 = Ost) und Hoehe ueber dem Horizont, in Zehntelgrad
  bool sichtbar;       // in diesem Moment angestrahlt und bei uns dunkel
};

const int SAT_SCHRITT = 10;  // Sekunden zwischen zwei Bahnpunkten

struct Ueberflug {
  bool gueltig = false;
  uint8_t sat = 0;                   // welcher Satellit (satName)
  time_t start = 0, ende = 0;        // ueber dem Horizont
  time_t sichtAb = 0, sichtBis = 0;  // davon zu sehen
  float maxHoehe = 0;                // hoechster Punkt, solange er zu sehen ist (Grad)
  float azMitte = 0;                 // Blickrichtung fuer die Ansicht (Mitte des sichtbaren Teils)
  float azAb = 0, azBis = 0;         // Richtung, in der er auftaucht / verschwindet
  int punkte = 0;                    // Bahn nur beim naechsten Ueberflug (sonst 0)
  SatPunkt bahn[120];                // alle SAT_SCHRITT Sekunden ab start (reicht fuer 20 min)
};

const int SAT_LISTE = 4;

// Aus der Hintergrund-Aufgabe aufrufen: Bahndaten taeglich, gerechnet wird nur bei Bedarf
void satellitenAktualisieren(float lat, float lon);
// Die naechsten sichtbaren Ueberfluege aller Satelliten (in 2 Tagen), nach Zeit sortiert; der erste mit Bahn.
// Ergebnis: Anzahl (0 = keiner)
int satellitenHolen(Ueberflug &naechster, Ueberflug *weitere, int max);
