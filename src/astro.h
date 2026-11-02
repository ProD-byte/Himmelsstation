#pragma once
// Sonne und Mond selbst gerechnet (ohne Internet), fuer die Seite "Heute Nacht".
// Formeln: Sonne nach dem Astronomical Almanac (auf wenige Sekunden genau), Mond in der kurzen Form
// des Almanac (Position auf ca. 0,3 Grad, Auf-/Untergang auf ca. 3 Minuten) - gegen skyfield geprueft.

#include <Arduino.h>

// Hoehe ueber dem Horizont in Grad (Mittelpunkt, ohne Lichtbrechung); Mond von der Erdoberflaeche aus gesehen
double sonneHoehe(time_t t, double lat, double lon);
double mondHoehe(time_t t, double lat, double lon);

const int NACHT_SCHRITTE = 145;  // 24 h in 10-Minuten-Schritten (+1)

// Planeten mit blossem Auge: Merkur, Venus, Mars, Jupiter, Saturn
// (JPL-Bahnelemente 1800-2050, gegen skyfield geprueft: auf wenige Bogenminuten genau)
const int PLANETEN = 5;
const char *planetName(int i);  // in der eingestellten Sprache
struct Planet {
  bool sichtbar = false;  // heute Nacht mind. 8 Grad hoch, waehrend es dunkel ist (Sonne unter -6 Grad)
  time_t ab = 0, bis = 0; // so lange zu sehen
  time_t beste = 0;       // wann er am hoechsten steht
  float hoehe = 0, az = 0;  // dann: Hoehe und Richtung (0 = Nord, 90 = Ost)
  float mag = 0;          // Helligkeit (kleiner = heller)
};
// Richtung (Azimut 0 = Nord) und Hoehe eines Planeten zur Zeit t
void planetOrt(int i, time_t t, double lat, double lon, float &az, float &hoehe, float &mag);

// Eine Nacht von Mittag bis Mittag (Ortszeit)
struct Nacht {
  bool gueltig = false;
  time_t mittag = 0;  // Beginn (Mittag Ortszeit, als Unix-Zeit)
  // Sonne unter/auf und Ende/Beginn der Daemmerungen, 0 = gibt es in dieser Nacht nicht
  time_t unter = 0, buergEnde = 0, nautEnde = 0, astroEnde = 0;
  time_t astroAnf = 0, nautAnf = 0, buergAnf = 0, auf = 0;
  time_t mondAuf = 0, mondUnter = 0;  // erstes Auf-/Untergehen in diesen 24 h
  int8_t sonne[NACHT_SCHRITTE];       // Hoehe alle 10 min ab mittag (Grad)
  int8_t mond[NACHT_SCHRITTE];
  float mondLicht = 0;                // beleuchteter Teil um Mitternacht, 0..1
  time_t dunkelVon = 0, dunkelBis = 0;  // dunkelste Zeit (astronomisch, sonst nautisch, sonst buergerlich)
  int dunkelArt = 0;                  // 3 = astronomisch dunkel, 2 = nautisch, 1 = buergerlich, 0 = gar nicht
  int dunkelMin = 0, ohneMondMin = 0;
  Planet planet[PLANETEN];
};

// Die Nacht, die zu 'jetzt' passt: vor 8 Uhr morgens die gerade laufende, sonst die kommende
void nachtBerechnen(Nacht &n, time_t jetzt, int32_t utcAbweichung, double lat, double lon);
