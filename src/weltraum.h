#pragma once
// Weltraumwetter von NOAA SWPC (kostenlos, ohne Anmeldung): Kp-Index (gemessen + Vorhersage fuer 3 Tage),
// Sonnenwind und Sonneneruptionen (Roentgen-Flares). Dazu die Polarlicht-Chance fuer den eigenen Ort.

#include <Arduino.h>

const int KP_WERTE = 88;  // 3-Stunden-Werte: 7 Tage zurueck + 3 Tage voraus (+ Reserve)

struct Weltraum {
  bool gueltig = false;
  time_t kpAb = 0;          // Zeit des ersten Werts (UTC, alle 3 h)
  int kpN = 0;
  float kp[KP_WERTE];
  uint8_t kpArt[KP_WERTE];  // 0 = gemessen, 1 = geschaetzt, 2 = Vorhersage
  int wind = 0;             // Sonnenwind in km/s (0 = unbekannt)
  char flare24[8] = "";     // staerkste Eruption der letzten 24 h, z. B. "M1.2"
  char flare7[8] = "";      // ... der letzten 7 Tage
  time_t flare7Zeit = 0;
  time_t stand = 0;
};

void weltraumAktualisieren();         // aus der Hintergrund-Aufgabe, holt alle 15 min neu
bool weltraumHolen(Weltraum &w);

float kpZu(const Weltraum &w, time_t t);                  // Kp zur Zeit t (-1 = unbekannt)
float kpMaxZwischen(const Weltraum &w, time_t von, time_t bis);
// Geomagnetische Breite des Orts (Dipol-Naeherung) und welcher Kp dort fuer Polarlicht am Nordhorizont noetig ist
float geomagBreite(float lat, float lon);
float kpFuerPolarlicht(float lat, float lon);
