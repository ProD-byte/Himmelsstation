#pragma once
// Daten fuer die Himmelsstation, als eigene Aufgabe im Hintergrund (wie das Wetter bei der Wetterstation):
// Ort (am Handy gewaehlt, sonst ueber die Internet-Adresse), Zeitzone, Sonne und Bewoelkung von Open-Meteo,
// dazu die Suche nach Updates. Mond, Planeten, Satelliten und Weltraumwetter kommen nach und nach dazu.

#include <Arduino.h>
#include "astro.h"

struct Himmel {
  bool gueltig = false;
  char ort[40] = "";               // Ortsname fuer die Anzeige
  bool tag = true;                 // Sonne ueber dem Horizont
  int wolkenJetzt = 0;             // Bewoelkung jetzt in %
  char sonneAuf[6] = "--:--", sonneUnter[6] = "--:--";
  int32_t utcAbweichung = 3600;    // Zeitzone des Orts in Sekunden (inkl. Sommerzeit)
  time_t stand = 0;                // Uhrzeit (Unix) des letzten erfolgreichen Abrufs
  // Bewoelkung je Stunde (%), ab gestern 0 Uhr fuer 3 Tage; -1 = unbekannt
  int8_t wolken[72];
  time_t wolkenAb = 0;
  Nacht nacht;                     // Daemmerung, Mond, Dunkelheit (selbst gerechnet, alle 10 min neu)
};

// Mittlere Bewoelkung zwischen von und bis (%), -1 = keine Daten
int wolkenZwischen(const Himmel &h, time_t von, time_t bis);

enum HimmelStatus { VERBINDE, ORTUNG, LADE, FERTIG, FEHLER };
// Warum der letzte Abruf gescheitert ist (fuer den Hinweis auf dem Display)
enum HimmelFehler { KEIN_FEHLER, UEBERLASTET, KEIN_INTERNET, NICHT_ERREICHBAR };

void himmelStart();
bool himmelHolen(Himmel &h);  // Kopie der neuesten Daten, false solange noch keine da sind
HimmelStatus himmelStatus();
HimmelFehler himmelFehler();
int himmelNaechsterVersuch();  // Sekunden bis zum naechsten Versuch nach einem Fehler (0 = laeuft gerade)
void himmelNeuLaden();         // z. B. nach Ortswechsel auf der Einstellungsseite
void himmelOrtAutomatisch();   // Ort wieder ueber die Internet-Adresse bestimmen (im Hintergrund)

// Mond aus dem Datum (ohne Internet): 0 = Neumond, 0.5 = Vollmond; beleuchteter Teil 0..1
float mondBeleuchtet(float phase);
float tageBisPhase(float phase, float ziel);  // Tage bis zur naechsten Phase 'ziel' (0 Neumond, 0.5 Vollmond)
