#pragma once
// Update aus dem Internet: neue Builds liegen auf GitHub Pages (prod-byte.github.io/Himmelsstation).
// Selbst gebaute Firmware (FIRMWARE_BUILD = 0, z. B. aus VS Code) sucht nicht danach.

#ifndef FIRMWARE_BUILD
#define FIRMWARE_BUILD 0  // setzt GitHub beim Bauen (Build-Nummer)
#endif

// Hintergrund-Aufgabe: version.json lesen und merken, ob es eine neuere Firmware gibt
void updatePruefen();
bool updateSucheGewuenscht();   // Einstellungsseite: "Jetzt suchen" (fragt und loescht den Wunsch)
void updateSuchenLassen();

int updateNeuerBuild();          // 0 = keine neuere Firmware bekannt
bool updateInstallierenGewuenscht();
void updateInstallierenLassen(); // Einstellungsseite: "Installieren"

// Laedt firmware.bin und startet neu. Nur aus loop() aufrufen (zeichnet selbst den Fortschritt).
typedef void (*UpdateAnzeige)(int prozent, const char *text);
void updateInstallieren(UpdateAnzeige anzeige);
