#pragma once
// Standort fuer die Himmelsdaten
//
// Reihenfolge: am Handy gewaehlter Ort > grob ueber die Internet-Adresse ermittelt > Cottbus.
// Der Ort liegt im Speicher des ESP32 und uebersteht Updates.

#include <Arduino.h>

struct Ort {
  float lat = 51.7563f, lon = 14.3329f;
  char name[40] = "Cottbus";
  bool vomNutzer = false;  // am Handy gewaehlt (dann nicht mehr automatisch ueberschreiben)
};

void ortLaden();
Ort ortAktuell();
void ortSpeichern(const Ort &o);

// Grob ueber die Internet-Adresse (ipapi.co, ersatzweise ipwho.is). Braucht WLAN.
bool ortPerIp(Ort &o);

// Treffer fuer die Einrichtungsseite als JSON-Liste [{"name","info","lat","lon"}, ...].
// Ortsname oder deutsche Postleitzahl. Braucht WLAN.
String ortSuchen(const String &suche);
