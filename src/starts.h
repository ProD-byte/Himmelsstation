#pragma once
// Raketenstarts weltweit von Launch Library 2 (The Space Devs, kostenlos; ohne Konto 15 Abfragen pro Stunde -
// deshalb nur alle 30 min, kurz vor einem Start alle 10 min)

#include <Arduino.h>

struct Start {
  time_t net = 0;          // geplante Startzeit (UTC)
  char status[20] = "";    // Go, TBC, TBD, Hold, In Flight, Success, Failure, Partial Failure ...
  char rakete[32] = "";    // z. B. "Falcon 9"
  char mission[40] = "";   // z. B. "USSF-432"
  char betreiber[32] = ""; // z. B. "SpaceX"
  char ort[48] = "";       // Startplatz kurz, z. B. "Vandenberg SFB · US"
  char land[4] = "";       // Laendercode, z. B. "US"
  char bahn[8] = "";       // Umlaufbahn, z. B. "LEO"
};

const int STARTS = 4;

struct Starts {
  bool gueltig = false;
  int n = 0;
  Start s[STARTS];
  time_t stand = 0;
};

void startsAktualisieren();  // aus der Hintergrund-Aufgabe
bool startsHolen(Starts &s);
bool startsVersucht();       // schon mindestens einmal versucht (fuer "nicht erreichbar" statt "hole ...")
