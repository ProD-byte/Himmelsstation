#pragma once
// WLAN mit Einrichtung per Handy (wie bei der ESPCam)
//
// Bekannte WLANs liegen im Speicher des ESP32 und ueberstehen Updates. Ist keins erreichbar,
// macht die Himmelsstation das WLAN "Himmelsstation-Setup" auf. Das Handy oeffnet darin die
// Einrichtungsseite von selbst.

// Name im Netz: einst.name (Standard "himmelsstation", auf der Einstellungsseite aenderbar)
#define WLAN_SETUP_NAME "Himmelsstation-Setup"

enum WlanPhase {
  WLAN_SUCHE,        // verbindet mit einem bekannten WLAN
  WLAN_EINRICHTUNG,  // Setup-WLAN ist offen, wartet aufs Handy
  WLAN_TESTE,        // probiert das am Handy gewaehlte WLAN
  WLAN_ORT,          // verbunden, wartet auf die Ortswahl am Handy
  WLAN_OK,           // fertig eingerichtet (netz = gewaehlter Ort)
  WLAN_FALSCH        // Passwort falsch oder Netz nicht erreichbar
};

// Wird waehrend der Einrichtung laufend aufgerufen, damit das Display etwas zeigen kann.
// netz = Name des WLANs, um das es gerade geht (kann leer sein).
typedef void (*WlanAnzeige)(WlanPhase phase, const char *netz);

// Kehrt erst zurueck, wenn eine Verbindung besteht.
void wlanVerbinden(bool einrichtungErzwingen, WlanAnzeige anzeige);

// Startet neu direkt in die Einrichtung (bekannte WLANs bleiben erhalten).
void wlanEinrichtungAnfordern();
