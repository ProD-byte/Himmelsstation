// LILYGO T-Display-S3 - Himmelsstation (ESP32-S3, 1,9" ST7789, 320 x 170, 8-Bit-Parallelbus)
// Standort: am Handy gewaehlt (Einrichtung), sonst grob ueber die Internet-Adresse, sonst Cottbus
//   Taster GPIO 0:  kurz = naechste Seite, 2 s halten = Sprache Deutsch/Englisch (zeigt auch Adresse + Build)
//   Taster GPIO 14: kurz = Helligkeit, 3 s halten = WLAN und Ort neu einrichten
// Live-Infos vom Himmel und aus dem All (Konzept: KONZEPT.md). Seiten: Mond, Demnaechst - weitere folgen
// Zeitzone, Sonne und Wolken: open-meteo.com
// WLAN: Einrichtung per Handy ueber das WLAN "Himmelsstation-Setup" (siehe wlan.cpp)
// Einstellungen: http://himmelsstation.local (siehe web.cpp)
// Updates: von selbst aus dem Internet (netzupdate.cpp) oder ueber WLAN mit "env:tdisplay_s3_wlan"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "himmel.h"
#include "bilder.h"
#include "wlan.h"
#include "ort.h"
#include "einstellungen.h"
#include "netzupdate.h"
#include "web.h"

#ifndef OTA_PASSWORT
#define OTA_PASSWORT ""
#endif

// Pins (siehe INFO.md) - das Display selbst steht in Setup206_LilyGo_T_Display_S3.h
const int TASTER_SEITE = 0;   // BOOT-Taste
const int TASTER_LICHT = 14;
const int AKKU_PIN     = 4;   // Akkuspannung ueber Teiler 1:2 (nur ohne USB aussagekraeftig)
const int STROM_EIN    = 15;  // muss HIGH sein, sonst bleibt das Display im Akkubetrieb dunkel
const int LICHT_PIN    = 38;  // Hintergrundlicht: Treiberchip mit 16 Stufen, gesteuert ueber Pulse

const float AKKU_FAKTOR = 2.0f;        // Teiler 1:2 - mit Multimeter nachmessen und anpassen
const uint32_t WECHSEL_DAUER = 380;    // Schiebe-Animation zwischen den Seiten

// Bildschirm: quer 320 x 170, hochkant 170 x 320 (Einstellung "Ausrichtung", siehe lageSetzen)
int B = 320, H = 170;
inline bool hoch() { return B < H; }
// hochkant: oben Uhr + Akku, darunter der Titel, Inhalt ab TOP
const int HOCH_TITEL_Y = 44, HOCH_TOP = 54;

const uint16_t WEISS = TFT_WHITE;
const uint16_t HELL  = 0xDF7E;  // helles Blaugrau

TFT_eSPI tft;
TFT_eSprite bild(&tft);  // ganzer Bildschirm im Speicher (PSRAM) - so flackert nichts

int seite = 0, alteSeite = 0;
bool wechsel = false;
uint32_t wechselStart = 0;
uint32_t seiteSeit = 0;
uint32_t letzterTastendruck = 0;

const int HELLIGKEIT[] = {16, 10, 5, 2};  // Stufen des Lichtchips (16 = hellste)
const int HELLIGKEIT_NACHT = 1;
uint32_t lichtAnzeigeBis = 0;
uint32_t infoBis = 0;

float akkuVolt = 0;
uint32_t akkuZeit = 0;

static Himmel himmel;
void zeitzoneSetzen(int32_t abweichung);

float akkuSpannung() {
  uint32_t summe = 0;
  for (int i = 0; i < 16; i++) summe += analogReadMilliVolts(AKKU_PIN);
  return summe / 16.0f * AKKU_FAKTOR / 1000.0f;
}

// Hintergrundlicht: der Chip zaehlt Pulse (wie im Beispiel von LILYGO), 0 = aus, 16 = hellste Stufe
void lichtStufe(int wert) {
  static int stufe = 0;
  const int STUFEN = 16;
  wert = constrain(wert, 0, STUFEN);
  if (wert == 0) {
    digitalWrite(LICHT_PIN, LOW);
    delay(3);
    stufe = 0;
    return;
  }
  if (stufe == 0) {
    digitalWrite(LICHT_PIN, HIGH);
    stufe = STUFEN;
    delayMicroseconds(30);
  }
  int von = STUFEN - stufe, bis = STUFEN - wert;
  int pulse = (STUFEN + bis - von) % STUFEN;
  for (int i = 0; i < pulse; i++) {
    digitalWrite(LICHT_PIN, LOW);
    digitalWrite(LICHT_PIN, HIGH);
  }
  stufe = wert;
}

// Farbe hinter (x, y) fuer Kaesten, Balken und Kanten - unempfindlich gegen Regen/Schnee (siehe grundFarbe)
uint16_t hintergrund(int x, int y) { return grundFarbe(bild, x, y); }

// Text auf eine Breite kuerzen (mit der gerade eingestellten Schrift), lange Ortsnamen z. B.
String kuerzen(String text, int maxBreite) {
  if (breite(bild, text.c_str()) <= maxBreite) return text;
  while (text.length() > 1 && breite(bild, (text + ".").c_str()) > maxBreite) {
    int n = text.length() - 1;
    while (n > 0 && ((uint8_t)text[n] & 0xC0) == 0x80) n--;  // keinen Umlaut halb abschneiden
    text.remove(n);
  }
  return text + ".";
}

// Ortszeit, false solange die Uhr noch nicht gestellt ist. Ersetzt getLocalTime(&z, 0): das meldet
// manchmal ohne Grund "keine Zeit", wenn waehrend des Aufrufs die Millisekunde wechselt - dann
// stand fuer ein Bild "--:--" da und Abendrot/Sonne/Mond sprangen (das Display zuckte)
bool ortszeit(struct tm &z) {
  time_t jetzt = time(nullptr);
  if (jetzt < 1700000000) return false;  // vor Nov. 2023: noch nicht per NTP gestellt
  localtime_r(&jetzt, &z);
  return true;
}

// Minuten seit Mitternacht aus "06:56"
int minuten(const char *hhmm) { return atoi(hhmm) * 60 + atoi(hhmm + 3); }

// Morgen-/Abendrot: 1 genau bei Sonnenauf- oder -untergang, 0 ab 50 Minuten Abstand
float daemmerung() {
  struct tm zeit;
  if (!ortszeit(zeit) || !himmel.sonneAuf[0] || himmel.sonneAuf[0] == '-') return 0;
  int jetzt = zeit.tm_hour * 60 + zeit.tm_min;
  int abstand = min(abs(jetzt - minuten(himmel.sonneAuf)), abs(jetzt - minuten(himmel.sonneUnter)));
  return abstand < 50 ? 1 - abstand / 50.0f : 0;
}

// Titel oben links. Hochkant steht der Titel in einer eigenen Zeile unter Uhr und Akku.
void titel(int ox, const char *text, uint32_t t) {
  schrift(bild, S_MF);
  int y = hoch() ? HOCH_TITEL_Y : 18;
  int platz = hoch() ? B - 16 : 210;
  schreibe(bild, kuerzen(text, platz).c_str(), ox + 8, y, WEISS, LINKS);
}

// Text in Schrift s, ist er breiter als 'platz', eine Nummer kleiner (bis S_K)
void passend(const char *txt, int x, int y, int platz, uint16_t f, Schrift s, Ausrichtung a) {
  schrift(bild, s);
  if (breite(bild, txt) > platz && s != S_K) schrift(bild, s == S_GF || s == S_G ? S_MF : S_M);
  if (breite(bild, txt) > platz) schrift(bild, S_M);
  if (breite(bild, txt) > platz) schrift(bild, S_K);
  schreibe(bild, kuerzen(txt, platz).c_str(), x, y, f, a);
}

// ------------------------------------------------------------
// Seite 1: Mond (rechnet das Geraet selbst aus dem Datum)
// ------------------------------------------------------------
void seiteMond(int ox, uint32_t t) {
  titel(ox, TX("Mond", "Moon"), t);
  const bool hk = hoch();
  float phase = mondphase(time(nullptr));
  // quer: Mond links, Werte rechts - hochkant: Mond oben, Werte darunter
  const int cx = hk ? ox + B / 2 : ox + 82, cy = hk ? 132 : 96, r = hk ? 54 : 50;
  float puls = 0.5f + 0.5f * sinf(t * 0.0015f);
  bild.fillSmoothCircle(cx, cy, r + 7 + 2 * puls, bild.alphaBlend(30 + 15 * puls, farbe(200, 210, 255), hintergrund(cx, cy - r - 14)));
  zeichneMondphase(bild, cx, cy, r, phase);

  const int x = hk ? ox + B / 2 : ox + 160, platz = hk ? B - 12 : B - 168;
  const Ausrichtung a = hk ? MITTE : LINKS;
  int y = hk ? 218 : 62;
  char z[40];
  passend(mondName(phase), x, y, platz, WEISS, S_MF, a);
  snprintf(z, sizeof(z), TX("%d %% beleuchtet", "%d %% lit"), (int)lroundf(mondBeleuchtet(phase) * 100));
  passend(z, x, y + 26, platz, farbe(255, 214, 120), S_M, a);
  float voll = tageBisPhase(phase, 0.5f), neu = tageBisPhase(phase, 0.0f);
  auto tage = [&](float d, const char *heute, const char *morgen, const char *sonst) {
    if (d < 0.6f || d > 29) strlcpy(z, heute, sizeof(z));
    else if (d < 1.6f) strlcpy(z, morgen, sizeof(z));
    else snprintf(z, sizeof(z), sonst, (int)lroundf(d));
  };
  tage(voll, TX("Heute Vollmond", "Full moon today"), TX("Morgen Vollmond", "Full moon tomorrow"), TX("Vollmond in %d Tagen", "Full moon in %d days"));
  passend(z, x, y + 50, platz, HELL, S_K, a);
  tage(neu, TX("Heute Neumond", "New moon today"), TX("Morgen Neumond", "New moon tomorrow"), TX("Neumond in %d Tagen", "New moon in %d days"));
  passend(z, x, y + 70, platz, HELL, S_K, a);
}

// ------------------------------------------------------------
// Seite 2: Demnaechst - was die Himmelsstation bald kann
// ------------------------------------------------------------
void seiteDemnaechst(int ox, uint32_t t) {
  titel(ox, TX("Demnächst", "Coming soon"), t);
  static const char *DE[] = {"Heute Nacht: Dunkelheit & Wolken", "Planeten am Himmel", "ISS & Satelliten", "Polarlicht-Alarm", "Raketenstarts"};
  static const char *EN[] = {"Tonight: darkness & clouds", "Planets in the sky", "ISS & satellites", "Aurora alert", "Rocket launches"};
  const bool hk = hoch();
  const int y0 = hk ? HOCH_TOP + 30 : 48, dy = hk ? 40 : 24, x = ox + (hk ? 26 : 30);
  for (int i = 0; i < 5; i++) {
    int y = y0 + i * dy;
    // funkelnder Stern als Aufzaehlungspunkt, jeder in seinem eigenen Takt
    float an = 0.55f + 0.45f * sinf(t * 0.004f + i * 1.3f);
    uint16_t gelb = bild.alphaBlend(255 * an, farbe(255, 214, 120), hintergrund(x, y - 5));
    bild.drawFastHLine(x - 5, y - 5, 11, gelb);
    bild.drawFastVLine(x, y - 10, 11, gelb);
    bild.fillSmoothCircle(x, y - 5, 2, gelb);
    passend(TX(DE[i], EN[i]), x + 14, y, B - (x - ox) - 20, WEISS, hk ? S_K : S_M, LINKS);
  }
}

void zeichneSeite(int s, int ox, uint32_t t) {
  if (ox <= -B || ox >= B) return;
  switch (s) {
    case 0: seiteMond(ox, t); break;
    case 1: seiteDemnaechst(ox, t); break;
  }
}

// ------------------------------------------------------------
// Immer sichtbar: Uhrzeit, Akku, Seitenpunkte, Helligkeit, Info
// ------------------------------------------------------------
void statusLeiste() {
  struct tm zeit;
  char uhr[8] = "--:--";
  if (ortszeit(zeit)) strftime(uhr, sizeof(uhr), "%H:%M", &zeit);
  schrift(bild, S_MF);
  if (hoch()) schreibe(bild, uhr, 8, 18, WEISS, LINKS);  // hochkant: Uhr links, Titel eine Zeile tiefer
  else schreibe(bild, uhr, B - 37, 18, WEISS, RECHTS);


  // Akku-Symbol
  int x = B - 31, y = 6;
  bild.drawRoundRect(x, y, 22, 12, 2, WEISS);
  bild.fillRect(x + 22, y + 4, 2, 4, WEISS);
  // haengt am USB? Mit Abstand zwischen Ein- und Ausschalten, sonst springt das Symbol hin und her,
  // wenn die Spannung knapp an der Schwelle liegt
  static bool usb = false;
  if (akkuVolt > 4.3f) usb = true;
  else if (akkuVolt < 4.15f) usb = false;
  if (usb) {
    uint16_t g = farbe(255, 214, 60);
    bild.fillTriangle(x + 12, y + 1, x + 6, y + 7, x + 12, y + 7, g);
    bild.fillTriangle(x + 10, y + 5, x + 16, y + 5, x + 10, y + 11, g);
  } else {
    int prozent = constrain((int)((akkuVolt - 3.3f) / 0.9f * 100), 0, 100);
    uint16_t f = prozent > 50 ? farbe(90, 220, 110) : prozent > 20 ? farbe(255, 210, 60) : farbe(255, 80, 70);
    bild.fillRect(x + 2, y + 2, 18 * prozent / 100, 8, f);
  }
}

bool seiteAn(int s) { return einst.seiten >> s & 1; }

void seitenPunkte() {
  int n = 0;
  for (int i = 0; i < SEITEN_ANZAHL; i++) n += seiteAn(i);
  if (n < 2) return;
  int x = B / 2 - (n - 1) * 5;
  for (int i = 0; i < SEITEN_ANZAHL; i++) {
    if (!seiteAn(i)) continue;
    bild.fillSmoothCircle(x, H - 5, 2, i == seite ? WEISS : bild.alphaBlend(90, WEISS, hintergrund(x, H - 5)));
    x += 10;
  }
}

void lichtAnzeige(uint32_t t) {
  if ((int32_t)(lichtAnzeigeBis - t) <= 0) return;
  int x = B / 2 - 62, y = H - 44;
  bild.fillSmoothRoundRect(x, y, 124, 26, 13, bild.alphaBlend(200, TFT_BLACK, hintergrund(B / 2, y + 13)));
  for (int k = 0; k < 4; k++) {
    bool an = k < 4 - einst.licht;
    bild.fillSmoothRoundRect(x + 12 + k * 26, y + 8, 22, 10, 4, an ? farbe(255, 210, 70) : farbe(70, 74, 84));
  }
}

// Taste IO0 lange halten: neue Sprache, dazu wo die Einstellungsseite ist
void infoAnzeige(uint32_t t) {
  if ((int32_t)(infoBis - t) <= 0) return;
  int bb = min(260, B - 10), y0 = H / 2 - 62;  // Kasten in der Mitte, quer wie hochkant
  bild.fillSmoothRoundRect(B / 2 - bb / 2, y0, bb, 124, 14, bild.alphaBlend(225, TFT_BLACK, hintergrund(B / 2, H / 2)));
  // oben die (gerade gewaehlte) Sprache, darunter wo die Einstellungsseite ist
  schrift(bild, S_GF);
  schreibe(bild, TX("Deutsch", "English"), B / 2, y0 + 28, farbe(255, 198, 44), MITTE);
  schrift(bild, S_K);
  schreibe(bild, kuerzen(TX("Einstellungen im Browser:", "Settings in your browser:"), bb - 12).c_str(), B / 2, y0 + 50, HELL, MITTE);
  String adresse = String(einst.name) + ".local";
  schrift(bild, S_M);
  if (breite(bild, adresse.c_str()) > bb - 12) schrift(bild, S_K);
  schreibe(bild, kuerzen(adresse, bb - 12).c_str(), B / 2, y0 + 72, WEISS, MITTE);
  schrift(bild, S_K);
  schreibe(bild, (String(TX("oder ", "or ")) + WiFi.localIP().toString()).c_str(), B / 2, y0 + 92, WEISS, MITTE);
  char z[40];
  if (FIRMWARE_BUILD) snprintf(z, sizeof(z), "Build %d", FIRMWARE_BUILD);
  else strcpy(z, TX("eigener Build", "own build"));
  if (updateNeuerBuild()) snprintf(z, sizeof(z), TX("Build %d - neu: %d", "Build %d - new: %d"), FIRMWARE_BUILD, updateNeuerBuild());
  schreibe(bild, z, B / 2, y0 + 112, HELL, MITTE);
}

// ------------------------------------------------------------
// Startbild, solange noch keine Daten da sind
// ------------------------------------------------------------
// Schlafende Wolke (Datendienst macht Pause): schwebt sanft auf und ab, kleine "z" steigen auf.
// keinNetz: wach, grau und rot durchgestrichen
void schlafendeWolke(int cx, int cy, uint32_t t, bool keinNetz) {
  cy += lroundf(3 * sinf(t / 700.0f));
  const uint16_t hell = keinNetz ? farbe(176, 184, 198) : farbe(240, 244, 250);
  const uint16_t schatten = keinNetz ? farbe(110, 118, 134) : farbe(168, 182, 210);
  const uint16_t gesicht = farbe(60, 66, 84);
  for (int o = 3; o >= 0; o -= 3) {  // erst der Schatten leicht versetzt, dann die Wolke
    uint16_t f = o ? schatten : hell;
    bild.fillSmoothRoundRect(cx - 40 + o, cy - 2 + o, 80, 26, 13, f);
    bild.fillSmoothCircle(cx - 18 + o, cy - 2 + o, 17, f);
    bild.fillSmoothCircle(cx + 8 + o, cy - 10 + o, 22, f);
    bild.fillSmoothCircle(cx + 30 + o, cy + 4 + o, 13, f);
  }
  if (keinNetz) {
    bild.fillSmoothCircle(cx - 9, cy + 5, 3, gesicht);
    bild.fillSmoothCircle(cx + 13, cy + 5, 3, gesicht);
    bild.drawWideLine(cx - 4, cy + 15, cx + 8, cy + 15, 2, gesicht);
    bild.drawWideLine(cx - 36, cy + 26, cx + 42, cy - 28, 5, farbe(240, 80, 70));
    return;
  }
  // geschlossene Augen, rote Baeckchen, kleiner offener Mund
  bild.drawSmoothArc(cx - 9, cy + 3, 6, 4, 290, 70, gesicht, hell, true);
  bild.drawSmoothArc(cx + 13, cy + 3, 6, 4, 290, 70, gesicht, hell, true);
  const uint16_t rosa = bild.alphaBlend(110, farbe(255, 140, 150), hell);
  bild.fillSmoothCircle(cx - 20, cy + 12, 4, rosa);
  bild.fillSmoothCircle(cx + 24, cy + 12, 4, rosa);
  bild.fillSmoothCircle(cx + 2, cy + 15, 3, gesicht);
  // drei "z" steigen nacheinander schraeg nach oben und werden dabei groesser und blasser
  bild.setTextDatum(L_BASELINE);
  for (int i = 0; i < 3; i++) {
    float p = ((t + i * 900) % 2700) / 2700.0f;
    int zx = cx + 32 + p * 24 + 3 * sinf(p * 6.3f + i), zy = cy - 22 - p * 30;
    float sichtbar = min(1.0f, min(p * 5, (1 - p) * 2.5f));
    schrift(bild, p < 0.4f ? S_K : S_M);
    bild.setTextColor(bild.alphaBlend(255 * sichtbar, WEISS, hintergrund(zx + 4, zy - 5)));
    bild.drawString("z", zx, zy);
  }
}

// Zeile mittig, bei Bedarf in kleinerer Schrift, damit sie auf den Bildschirm passt
static void zeileMittig(const char *txt, int y, uint16_t f, Schrift s) {
  schrift(bild, s);
  if (breite(bild, txt) > B - 12) schrift(bild, S_M);
  if (breite(bild, txt) > B - 12) schrift(bild, S_K);
  schreibe(bild, kuerzen(txt, B - 8).c_str(), B / 2, y, f, MITTE);
}

void startBild(uint32_t t) {
  zeichneHimmel(bild, KLAR, false, t, false);
  const int dy = hoch() ? 54 : 0;  // hochkant alles etwas tiefer in die Mitte

  // Datendienst nicht erreichbar: schlafende Wolke, was los ist und wann der naechste Versuch kommt
  if (himmelStatus() == FEHLER) {
    HimmelFehler f = himmelFehler();
    schlafendeWolke(B / 2 - 6, 66 + dy, t, f == KEIN_INTERNET);
    const char *titel, *grund;
    String netz = String(TX("WLAN ok: ", "Wi-Fi OK: ")) + WiFi.SSID();
    if (f == UEBERLASTET) {
      titel = TX("Datendienst überlastet", "Data service overloaded");
      grund = TX("Er macht gerade Pause", "It's taking a short break");
    } else if (f == KEIN_INTERNET) {
      titel = TX("Keine Internetverbindung", "No internet connection");
      grund = netz.c_str();
    } else {
      titel = TX("Datendienst nicht erreichbar", "Data service unreachable");
      grund = TX("Open-Meteo antwortet nicht", "Open-Meteo is not responding");
    }
    zeileMittig(titel, 128 + dy, f == KEIN_INTERNET ? farbe(255, 120, 110) : WEISS, hoch() ? S_MF : S_GF);
    char versuch[40];
    static const char *punkte[] = {"", ".", "..", "..."};
    int s = himmelNaechsterVersuch();
    if (s > 0) snprintf(versuch, sizeof(versuch), TX("neuer Versuch in %d:%02d", "retrying in %d:%02d"), s / 60, s % 60);
    else snprintf(versuch, sizeof(versuch), TX("versuche es gerade%s", "trying now%s"), punkte[(t / 400) % 4]);
    schrift(bild, S_K);
    schreibe(bild, kuerzen(grund, B - 8).c_str(), B / 2, 149 + dy, HELL, MITTE);
    schreibe(bild, versuch, B / 2, 165 + dy, farbe(150, 160, 190), MITTE);
    return;
  }

  zeichneMondphase(bild, B / 2, 62 + dy, 34, mondphase(time(nullptr)));

  schrift(bild, hoch() ? S_MF : S_GF);
  schreibe(bild, kuerzen(String(TX("Himmel über ", "Sky over ")) + ortAktuell().name, B - 20).c_str(), B / 2, 128 + dy, WEISS, MITTE);

  char zeile1[40], zeile2[40] = "";
  const char *punkte[] = {"", ".", "..", "..."};
  const char *p = punkte[(t / 400) % 4];
  switch (himmelStatus()) {
    case VERBINDE:
      snprintf(zeile1, sizeof(zeile1), TX("Verbinde mit WLAN%s", "Connecting to Wi-Fi%s"), p);
      snprintf(zeile2, sizeof(zeile2), "%s", WiFi.SSID().c_str());
      break;
    case ORTUNG:
      snprintf(zeile1, sizeof(zeile1), TX("Suche Standort%s", "Finding location%s"), p);
      break;
    case LADE:
      snprintf(zeile1, sizeof(zeile1), TX("Hole Himmelsdaten%s", "Loading sky data%s"), p);
      break;
    default:
      zeile1[0] = 0;
      break;
  }
  schrift(bild, S_K);
  schreibe(bild, kuerzen(zeile1, B - 8).c_str(), B / 2, 149 + dy, HELL, MITTE);
  schreibe(bild, kuerzen(zeile2, B - 8).c_str(), B / 2, 165 + dy, farbe(150, 160, 190), MITTE);
}

// ------------------------------------------------------------
// WLAN-Einrichtung: Anleitung auf dem Display
// ------------------------------------------------------------
void wlanSymbol(int cx, int cy, uint32_t t) {
  // Wellen laufen nacheinander an
  for (int i = 0; i < 3; i++) {
    float an = 0.5f + 0.5f * sinf(t * 0.006f - i * 1.1f);
    uint16_t f = bild.alphaBlend(80 + 175 * an, farbe(255, 198, 44), hintergrund(cx, cy - 10));
    int r = 13 + i * 12;
    bild.drawArc(cx, cy, r + 3, r - 1, 135, 225, f, hintergrund(cx, cy - r));  // 180 = oben
  }
  bild.fillSmoothCircle(cx, cy, 5, farbe(255, 198, 44));
}

// Standort-Nadel, die sanft auf und ab huepft
void ortSymbol(int cx, int cy, uint32_t t) {
  float hub = fabsf(sinf(t * 0.004f)) * 8;
  uint16_t gelb = farbe(255, 198, 44);
  bild.fillSmoothCircle(cx, cy + 26, 12 - hub / 2, bild.alphaBlend(70, TFT_BLACK, hintergrund(cx, cy + 26)));  // Schatten
  int y = cy - hub;
  bild.fillTriangle(cx - 13, y - 2, cx + 13, y - 2, cx, y + 24, gelb);
  bild.fillSmoothCircle(cx, y - 8, 14, gelb);
  bild.fillSmoothCircle(cx, y - 8, 5, hintergrund(cx, cy - 40));
}

void wlanBild(WlanPhase phase, const char *netz) {
  static uint32_t zuletzt = 0;
  uint32_t t = millis();
  if (t - zuletzt < 33 && phase != WLAN_SUCHE) return;  // reicht fuer fluessige Animation
  zuletzt = t;

  zeichneHimmel(bild, KLAR, false, t, false);
  const bool hk = hoch();
  const int dy = hk ? 54 : 0;  // hochkant: Bild + Text tiefer in die Mitte
  schrift(bild, hk ? S_MF : S_GF);

  if (hk && (phase == WLAN_EINRICHTUNG || phase == WLAN_ORT)) {
    // hochkant: Symbol oben, Anleitung in kurzen Zeilen darunter
    const uint16_t gelb = farbe(255, 198, 44);
    bool ort = phase == WLAN_ORT;
    schreibe(bild, ort ? TX("WLAN verbunden!", "Wi-Fi connected!") : TX("WLAN einrichten", "Wi-Fi setup"), B / 2, 30,
             ort ? farbe(90, 220, 110) : WEISS, MITTE);
    if (ort) ortSymbol(B / 2, 88, t);
    else wlanSymbol(B / 2, 112, t);
    struct Zeile { const char *de, *en; bool gelb; };
    static const Zeile EINR[] = {{"1. Mit dem Handy", "1. Connect your phone", false}, {"ins WLAN", "to the Wi-Fi", false},
                                 {WLAN_SETUP_NAME, WLAN_SETUP_NAME, true}, {"2. Die Seite öffnet sich", "2. A page opens", false},
                                 {"selbst: dein WLAN", "by itself: choose", false}, {"wählen, Passwort", "your Wi-Fi, enter", false},
                                 {"eingeben", "the password", false}};
    static const Zeile ORT[] = {{"3. Jetzt am Handy:", "3. Now on your phone:", false}, {"Wo steht die", "Where is the", true},
                                {"Station?", "station?", true}, {"Ort oder PLZ suchen", "Search town or", false},
                                {"oder Vorschlag", "postcode or tap", false}, {"antippen", "the suggestion", false}};
    const Zeile *z = ort ? ORT : EINR;
    int n = ort ? 6 : 7;
    for (int i = 0; i < n; i++) {
      const char *txt = TX(z[i].de, z[i].en);
      schrift(bild, z[i].gelb ? S_MF : S_K);
      if (breite(bild, txt) > B - 8) schrift(bild, S_M);  // "Himmelsstation-Setup" ist fett zu breit
      if (breite(bild, txt) > B - 8) schrift(bild, S_K);
      schreibe(bild, txt, B / 2, 162 + i * 21, z[i].gelb ? gelb : HELL, MITTE);
    }
  } else if (phase == WLAN_SUCHE) {
    zeichneMondphase(bild, B / 2, 62 + dy, 34, mondphase(time(nullptr)));
    schreibe(bild, kuerzen(String(TX("Himmel über ", "Sky over ")) + ortAktuell().name, B - 20).c_str(), B / 2, 128 + dy, WEISS, MITTE);
    schrift(bild, S_K);
    schreibe(bild, TX("Verbinde mit WLAN ...", "Connecting to Wi-Fi ..."), B / 2, 149 + dy, HELL, MITTE);
    schreibe(bild, kuerzen(netz, B - 8).c_str(), B / 2, 165 + dy, farbe(150, 160, 190), MITTE);
  } else if (phase == WLAN_EINRICHTUNG || phase == WLAN_ORT) {
    // quer: Symbol links, Anleitung rechts daneben; zu breite Zeilen in kleinerer Schrift
    auto zeile = [](const char *txt, int y, uint16_t f, Schrift s) {
      schrift(bild, s);
      if (breite(bild, txt) > B - 98) schrift(bild, s == S_MF ? S_M : S_K);
      if (breite(bild, txt) > B - 98) schrift(bild, S_K);
      schreibe(bild, txt, 94, y, f, LINKS);
    };
    const uint16_t gelb = farbe(255, 198, 44);
    if (phase == WLAN_EINRICHTUNG) {
      schreibe(bild, TX("WLAN einrichten", "Wi-Fi setup"), B / 2, 27, WEISS, MITTE);
      wlanSymbol(46, 110, t);
      zeile(TX("1. Handy ins WLAN:", "1. Connect your phone to:"), 60, HELL, S_M);
      zeile(WLAN_SETUP_NAME, 82, gelb, S_MF);
      zeile(TX("2. Seite öffnet sich selbst:", "2. A page opens by itself:"), 112, HELL, S_M);
      zeile(TX("dein WLAN wählen und", "choose your Wi-Fi and"), 134, HELL, S_M);
      zeile(TX("Passwort eingeben", "enter the password"), 156, HELL, S_M);
    } else {
      schreibe(bild, TX("WLAN verbunden!", "Wi-Fi connected!"), B / 2, 27, farbe(90, 220, 110), MITTE);
      ortSymbol(46, 100, t);
      zeile(TX("3. Jetzt am Handy:", "3. Now on your phone:"), 62, HELL, S_M);
      zeile(TX("Wo steht die Station?", "Where is the station?"), 86, gelb, S_MF);
      zeile(TX("Ort oder PLZ suchen", "Search town or postcode"), 118, HELL, S_M);
      zeile(TX("oder Vorschlag antippen", "or tap the suggestion"), 140, HELL, S_M);
    }
  } else if (phase == WLAN_TESTE) {
    zeichneMondphase(bild, B / 2, 62 + dy, 34, mondphase(time(nullptr)));
    schreibe(bild, TX("Verbinde ...", "Connecting ..."), B / 2, 128 + dy, WEISS, MITTE);
    schrift(bild, S_K);
    schreibe(bild, kuerzen(netz, B - 8).c_str(), B / 2, 152 + dy, farbe(255, 198, 44), MITTE);
  } else if (phase == WLAN_OK) {
    zeichneBild(bild, KLAR, true, B / 2, 62 + dy, 100, t);
    schreibe(bild, TX("Fertig!", "Done!"), B / 2, 128 + dy, farbe(90, 220, 110), MITTE);
    schrift(bild, S_K);
    String z = *netz ? String(TX("Himmel über ", "Sky over ")) + netz : String(TX("Handy wieder ins eigene WLAN", "Phone back to your Wi-Fi"));
    schreibe(bild, kuerzen(z, B - 8).c_str(), B / 2, 152 + dy, HELL, MITTE);
  } else {
    zeichneBild(bild, GEWITTER, true, B / 2, 58 + dy, 92, t);
    schreibe(bild, TX("Hat nicht geklappt", "That did not work"), B / 2, 128 + dy, farbe(255, 110, 100), MITTE);
    schrift(bild, S_K);
    if (hk) {
      schreibe(bild, TX("Passwort falsch?", "Wrong password?"), B / 2, 152 + dy, HELL, MITTE);
      schreibe(bild, TX("Nochmal versuchen", "Try again"), B / 2, 172 + dy, HELL, MITTE);
    } else {
      schreibe(bild, TX("Passwort falsch? Nochmal versuchen", "Wrong password? Try again"), B / 2, 152, HELL, MITTE);
    }
  }
  bild.pushSprite(0, 0);
}

// ------------------------------------------------------------
// Updates (ueber WLAN aus VS Code oder selbst aus dem Internet)
// ------------------------------------------------------------
// Waehrend des Updates laeuft loop() nicht weiter - deshalb wird hier selbst gezeichnet.
void updateBild(int prozent, const char *text) {
  uint32_t t = millis();
  zeichneHimmel(bild, KLAR, false, t, false);
  const int dy = hoch() ? 75 : 0, bx = hoch() ? 16 : 40, bb = B - 2 * bx;  // hochkant: tiefer und schmaler
  schrift(bild, S_GF);
  schreibe(bild, TX("Neue Version", "New version"), B / 2, 56 + dy, WEISS, MITTE);

  bild.fillSmoothRoundRect(bx, 74 + dy, bb, 16, 8, farbe(40, 48, 80));
  if (prozent > 0) bild.fillSmoothRoundRect(bx, 74 + dy, max(16, bb * prozent / 100), 16, 8, farbe(255, 198, 44));

  char z[16];
  snprintf(z, sizeof(z), "%d %%", prozent);
  schrift(bild, S_M);
  schreibe(bild, z, B / 2, 120 + dy, WEISS, MITTE);
  schrift(bild, S_K);
  schreibe(bild, kuerzen(text, B - 8).c_str(), B / 2, 146 + dy, HELL, MITTE);
  bild.pushSprite(0, 0);
}

void updateStart() {
  ArduinoOTA.setHostname(einst.name);  // meldet auch name.local im Netz an (mDNS)
  if (strlen(OTA_PASSWORT) > 0) ArduinoOTA.setPassword(OTA_PASSWORT);
  ArduinoOTA.onStart([]() {
    Serial.println("Update startet");
    lichtStufe(16);
    updateBild(0, TX("nicht ausschalten", "do not switch off"));
  });
  ArduinoOTA.onProgress([](unsigned int fertig, unsigned int gesamt) {
    static int zuletzt = -1;
    int p = fertig * 100 / gesamt;
    if (p != zuletzt) {
      zuletzt = p;
      updateBild(p, TX("nicht ausschalten", "do not switch off"));
    }
  });
  ArduinoOTA.onEnd([]() { updateBild(100, TX("fertig - starte neu", "done - restarting")); });
  ArduinoOTA.onError([](ota_error_t fehler) {
    Serial.printf("Update-Fehler %u\n", fehler);
    updateBild(0, TX("Fehler - nochmal versuchen", "Error - please try again"));
    delay(2000);
  });
  ArduinoOTA.begin();
  Serial.printf("Update ueber WLAN bereit: %s.local (%s)\n", einst.name, WiFi.localIP().toString().c_str());
}

// ------------------------------------------------------------
// Seiten, Licht, Tasten
// ------------------------------------------------------------
void naechsteSeite(uint32_t t) {
  int neu = seite;
  for (int i = 0; i < SEITEN_ANZAHL; i++) {
    neu = (neu + 1) % SEITEN_ANZAHL;
    if (seiteAn(neu)) break;
  }
  if (neu == seite) return;
  alteSeite = seite;
  seite = neu;
  wechsel = true;
  wechselStart = t;
  seiteSeit = t;
}

// Nachtmodus: zwischen "von" und "bis" sehr dunkel. Ein Tastendruck macht es fuer 20 s hell.
bool istNacht() {
  struct tm zeit;
  if (!einst.nacht || einst.nachtVon == einst.nachtBis || !ortszeit(zeit)) return false;
  int h = zeit.tm_hour;
  return einst.nachtVon < einst.nachtBis ? h >= einst.nachtVon && h < einst.nachtBis
                                         : h >= einst.nachtVon || h < einst.nachtBis;
}

static int lichtZuletzt = -1;  // -1 = beim naechsten Mal sicher neu setzen

void lichtSetzen(uint32_t t) {
  int &zuletzt = lichtZuletzt;
  static uint32_t geprueft = 0;
  static bool nacht = false;
  if (t - geprueft > 1000) {
    nacht = istNacht();
    geprueft = t;
  }
  int soll = nacht && t - letzterTastendruck > 20000 ? HELLIGKEIT_NACHT : HELLIGKEIT[einst.licht];
  if (soll != zuletzt) {
    lichtStufe(soll);
    zuletzt = soll;
  }
}

void tasten(uint32_t t, bool hatDaten) {
  // IO0: kurz = naechste Seite (beim Loslassen), 2 s halten = Sprache Deutsch/Englisch wechseln
  // (zeigt die neue Sprache zusammen mit Adresse der Einstellungsseite und Build)
  static uint32_t seiteSeitDruck = 0;
  static bool sprachGewechselt = false;
  bool s = digitalRead(TASTER_SEITE) == LOW;
  if (s && !seiteSeitDruck) seiteSeitDruck = t ? t : 1;
  if (s && t - seiteSeitDruck > 2000 && !sprachGewechselt) {
    einst.sprache ^= 1;
    einstellungenSpeichern();
    infoBis = t + 6000;
    sprachGewechselt = true;
  }
  if (!s && seiteSeitDruck) {
    sprachGewechselt = false;
    bool warDunkel = istNacht() && t - letzterTastendruck > 20000;
    if (t - seiteSeitDruck < 1000 && !warDunkel) {  // nachts weckt der erste Druck nur auf (wie IO14)
      if ((int32_t)(infoBis - t) > 0) infoBis = t;  // Info wieder zu
      else if (hatDaten && !wechsel) naechsteSeite(t);
    }
    seiteSeitDruck = 0;
    letzterTastendruck = t;
  }

  // IO14: kurz = Helligkeit (beim Loslassen), 3 s halten = WLAN und Ort neu einrichten
  static uint32_t lichtSeitDruck = 0;
  bool l = digitalRead(TASTER_LICHT) == LOW;
  if (l && !lichtSeitDruck) lichtSeitDruck = t ? t : 1;
  if (l && t - lichtSeitDruck > 3000) {
    wlanBild(WLAN_EINRICHTUNG, "");
    wlanEinrichtungAnfordern();
  }
  if (!l && lichtSeitDruck) {
    bool warDunkel = istNacht() && t - letzterTastendruck > 20000;
    if (t - lichtSeitDruck < 1000 && !warDunkel) {  // nachts weckt der erste Druck nur auf
      einst.licht = (einst.licht + 1) % 4;
      einstellungenSpeichern();
      lichtAnzeigeBis = t + 1500;
    }
    lichtSeitDruck = 0;
    letzterTastendruck = t;
  }
}

// Ausrichtung uebernehmen: Display drehen, bei quer <-> hochkant den Bildspeicher neu anlegen.
// Lage 0 = quer (USB rechts), 1 = quer gedreht, 2 = hochkant (USB unten), 3 = hochkant (USB oben)
void lageSetzen() {
  static int gesetzt = -1;
  if (einst.lage == gesetzt) return;
  gesetzt = einst.lage;
  static const uint8_t DREHUNG[4] = {1, 3, 0, 2};  // TFT_eSPI setRotation
  tft.setRotation(DREHUNG[einst.lage]);
  tft.fillScreen(TFT_BLACK);
  int nb = einst.lage >= 2 ? 170 : 320, nh = einst.lage >= 2 ? 320 : 170;
  if (bild.created() && nb == B) return;  // nur gedreht - gleiche Groesse
  B = nb, H = nh;
  bild.deleteSprite();
  // Bildspeicher fuer den ganzen Bildschirm (liegt im PSRAM)
  if (!bild.createSprite(B, H)) {
    Serial.println("Zu wenig Speicher - nehme 8 Bit Farbe");
    bild.setColorDepth(8);
    bild.createSprite(B, H);
  }
  bild.setTextWrap(false, false);  // zu lange Zeilen abschneiden statt links neu anfangen
  wechsel = false;  // laufende Schiebe-Animation passt nicht mehr
}

void setup() {
  // Zuerst Strom fuers Display einschalten (im Akkubetrieb sonst dunkel)
  pinMode(STROM_EIN, OUTPUT);
  digitalWrite(STROM_EIN, HIGH);

  Serial.begin(115200);
  Serial.printf("\nHimmelsstation T-Display-S3, Build %d\n", FIRMWARE_BUILD);

  pinMode(TASTER_SEITE, INPUT_PULLUP);
  pinMode(TASTER_LICHT, INPUT_PULLUP);
  analogSetPinAttenuation(AKKU_PIN, ADC_11db);
  akkuVolt = akkuSpannung();

  einstellungenLaden();

  tft.init();
  lageSetzen();  // Ausrichtung (quer/hochkant) und Bildspeicher

  pinMode(LICHT_PIN, OUTPUT);
  lichtStufe(HELLIGKEIT[einst.licht]);

  // GPIO 14 beim Einschalten gedrueckt halten = WLAN neu einrichten
  bool einrichten = digitalRead(TASTER_LICHT) == LOW;
  ortLaden();
  wlanVerbinden(einrichten, wlanBild);

  // bis die ersten Daten da sind: mitteleuropaeische Zeit, danach die Zeitzone des Orts
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.cloudflare.com");
  updateStart();
  webStart();
  himmelStart();
  if (!seiteAn(seite)) naechsteSeite(millis());
}

void loop() {
  uint32_t t = millis();

  ArduinoOTA.handle();
  webLoop();

  bool hatDaten = himmelHolen(himmel);
  if (hatDaten) zeitzoneSetzen(himmel.utcAbweichung);
  tasten(t, hatDaten);
  lichtSetzen(t);

  // Ausrichtung wurde auf der Einstellungsseite umgeschaltet -> sofort uebernehmen
  lageSetzen();

  // Neue Version aus dem Internet: auf Wunsch sofort, sonst wenn gerade niemand die Tasten benutzt
  bool gewuenscht = updateInstallierenGewuenscht();
  if (updateNeuerBuild() && (gewuenscht || (einst.autoUpdate && t - letzterTastendruck > 60000))) {
    if (gewuenscht || !istNacht()) lichtStufe(16);  // nachts bleibt das Display beim Selbst-Update dunkel
    updateInstallieren(updateBild);
    lichtZuletzt = -1;  // nur bei Fehler: Helligkeit wieder richtig setzen
  }

  if (t - akkuZeit > 2000) {
    akkuVolt = akkuVolt * 0.7f + akkuSpannung() * 0.3f;  // geglaettet, damit der Balken nicht zappelt
    akkuZeit = t;
  }

  if (!hatDaten) {
    startBild(t);
  } else {
    if (!seiteAn(seite) && !wechsel) naechsteSeite(t);  // Seite wurde gerade abgeschaltet
    // immer Nachthimmel mit Sternen - nur um Sonnenauf- und -untergang mit Morgen-/Abendrot
    zeichneHimmel(bild, KLAR, false, t, false, daemmerung());

    if (wechsel) {
      float p = (t - wechselStart) / float(WECHSEL_DAUER);
      if (p >= 1) {
        wechsel = false;
      } else {
        int weg = (1 - powf(1 - p, 3)) * B;
        zeichneSeite(alteSeite, -weg, t);
        zeichneSeite(seite, B - weg, t);
      }
    }
    if (!wechsel) zeichneSeite(seite, 0, t);

    statusLeiste();
    seitenPunkte();

    if (einst.wechsel && !wechsel && t - seiteSeit > einst.wechsel * 1000UL && t - letzterTastendruck > 30000)
      naechsteSeite(t);
  }
  lichtAnzeige(t);
  infoAnzeige(t);

  bild.pushSprite(0, 0);

  // hoechstens ca. 30 Bilder pro Sekunde: sieht gleich aus, spart Strom und haelt das Board kuehl
  static uint32_t bildStart = 0;
  int32_t rest = 33 - (int32_t)(millis() - bildStart);
  if (rest > 0) delay(rest);
  bildStart = millis();
}

// Zeitzone des Orts (von Open-Meteo, mit Sommerzeit) als feste Abweichung von UTC uebernehmen.
// Wird mit jedem Abruf (alle 30 min) aktualisiert - so stimmt die Uhr auf der ganzen Welt.
void zeitzoneSetzen(int32_t abweichung) {
  static int32_t gesetzt = INT32_MIN;
  if (abweichung == gesetzt) return;
  gesetzt = abweichung;
  int32_t a = abs(abweichung);
  char tz[24];
  // POSIX zaehlt andersherum: "STD-1" heisst UTC+1
  snprintf(tz, sizeof(tz), "STD%c%d:%02d", abweichung >= 0 ? '-' : '+', a / 3600, a % 3600 / 60);
  setenv("TZ", tz, 1);
  tzset();
  Serial.printf("Zeitzone: UTC%+.2g h (%s)\n", abweichung / 3600.0f, tz);
}
