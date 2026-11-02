#pragma once
// Animierte Wetterbilder, Himmel und Text

#include <TFT_eSPI.h>

enum Bild { KLAR, HEITER, BEDECKT, NEBEL, NIESEL, REGEN, SCHNEE, GEWITTER };
enum Ausrichtung { LINKS, MITTE, RECHTS };

uint16_t farbe(uint8_t r, uint8_t g, uint8_t b);

// Wettercode (WMO, wie open-meteo ihn liefert) -> Bild und Text
Bild bildFuerCode(int code);
const char *textFuerCode(int code, bool tag);

// Himmel mit Farbverlauf (nachts mit Sternen), ziehenden Wolken und Regen/Schnee/Nebel ueber alles.
// blitz = kurz aufhellen, daemmerung 0..1 = Morgen-/Abendrot rund um Sonnenauf- und -untergang
void zeichneHimmel(TFT_eSprite &s, Bild b, bool tag, uint32_t t, bool blitz, float daemmerung = 0);
bool blitzAktiv(uint32_t t);

// Farbe hinter (x, y) zum Mischen - Median aus 5 Punkten im X-Muster, damit ein einzelner Regenstreifen
// oder eine Schneeflocke nicht die Farbe eines ganzen Kastens umspringen laesst (sonst zuckt es)
uint16_t grundFarbe(TFT_eSprite &s, int x, int y);

// Wetterbild um (cx, cy), groesse = ungefaehre Breite in Pixeln, t = millis() fuer die Animation
void zeichneBild(TFT_eSprite &s, Bild b, bool tag, int cx, int cy, int groesse, uint32_t t);

// Mond mit beleuchtetem Teil: phase 0 = Neumond, 0.25 = erstes Viertel, 0.5 = Vollmond, 0.75 = letztes Viertel
void zeichneMondphase(TFT_eSprite &s, int cx, int cy, int r, float phase);
float mondphase(time_t jetzt);  // 0..1 aus dem Datum
const char *mondName(float phase);

// Warn-Dreieck mit Ausrufezeichen, g = Hoehe
void zeichneWarnzeichen(TFT_eSprite &s, int cx, int cy, int g, uint16_t f);

// Geglaettete Schriften (Lato, src/schriften.h). XG und RIESIG enthalten nur Ziffern, - + . , : % °
enum Schrift { S_K, S_M, S_MF, S_G, S_GF, S_XG, S_RIESIG };
void schrift(TFT_eSprite &s, Schrift welche);

// Text mit Schatten, y = Grundlinie, UTF-8 (Umlaute, ß, ° direkt). Schrift vorher mit schrift() waehlen.
void schreibe(TFT_eSprite &s, const char *text, int x, int y, uint16_t f, Ausrichtung a);
int breite(TFT_eSprite &s, const char *text);
// Temperatur mit Gradzeichen
void schreibeTemp(TFT_eSprite &s, float wert, int x, int y, uint16_t f, Ausrichtung a);
