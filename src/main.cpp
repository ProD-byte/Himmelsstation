// LILYGO T-Display-S3 - Himmelsstation (ESP32-S3, 1,9" ST7789, 320 x 170, 8-Bit-Parallelbus)
// Standort: am Handy gewaehlt (Einrichtung), sonst grob ueber die Internet-Adresse, sonst Cottbus
//   Taster GPIO 0:  kurz = naechste Seite, 2 s halten = Sprache Deutsch/Englisch (zeigt auch Adresse + Build)
//   Taster GPIO 14: kurz = Helligkeit, 3 s halten = WLAN und Ort neu einrichten
// Live-Infos vom Himmel und aus dem All (Konzept: KONZEPT.md). Seiten: Heute Nacht, Planeten, Satelliten, Weltraumwetter, Mond, Demnaechst
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
#include "satelliten.h"
#include "weltraum.h"
#include "astro.h"
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
// Seite 1: Heute Nacht - wann es dunkel wird, Mond, Wolken und eine Note fuers Beobachten
// ------------------------------------------------------------
// Uhrzeit als "18:27" (Ortszeit), "--:--" wenn es das Ereignis nicht gibt
static void uhrzeit(char *z, size_t n, time_t t) {
  if (!t) {
    strlcpy(z, "--:--", n);
    return;
  }
  struct tm a;
  localtime_r(&t, &a);
  snprintf(z, n, "%02d:%02d", a.tm_hour, a.tm_min);
}

// Farbe des Himmels je nach Sonnenhoehe: Tag, buergerliche, nautische, astronomische Daemmerung, Nacht
static uint16_t daemmerFarbe(float s) {
  if (s >= -0.833f) return farbe(96, 150, 220);
  if (s >= -6) return farbe(62, 98, 170);
  if (s >= -12) return farbe(36, 58, 120);
  if (s >= -18) return farbe(20, 30, 74);
  return farbe(6, 8, 22);
}

void seiteNacht(int ox, uint32_t t) {
  titel(ox, TX("Heute Nacht", "Tonight"), t);
  const Nacht &n = himmel.nacht;
  if (!n.gueltig) {
    schrift(bild, S_M);
    schreibe(bild, TX("Rechne ...", "Calculating ..."), ox + B / 2, H / 2, HELL, MITTE);
    return;
  }
  const bool hk = hoch();
  char z[48], a[8], b[8];

  // Note: Wolken waehrend der Dunkelheit und wie sehr der Mond stoert
  int wolken = wolkenZwischen(himmel, n.dunkelVon, n.dunkelBis);
  float mondAnteil = n.dunkelMin ? 1 - (float)n.ohneMondMin / n.dunkelMin : 0;
  float stoert = mondAnteil * n.mondLicht;
  float wert = (wolken < 0 ? 1 : 1 - wolken / 100.0f) * (1 - 0.7f * stoert);
  if (n.dunkelArt < 2) wert *= 0.5f;  // nie richtig dunkel (Hochsommer im Norden)
  const char *note;
  uint16_t noteFarbe;
  if (wert >= 0.75f) note = TX("Sehr gut", "Very good"), noteFarbe = farbe(110, 230, 130);
  else if (wert >= 0.5f) note = TX("Gut", "Good"), noteFarbe = farbe(190, 230, 110);
  else if (wert >= 0.25f) note = TX("Mäßig", "Fair"), noteFarbe = farbe(255, 200, 80);
  else note = TX("Schlecht", "Poor"), noteFarbe = farbe(255, 110, 100);
  const char *grundW = wolken < 0 ? TX("ohne Wolkendaten", "no cloud data") : wolken < 20 ? TX("klar", "clear") : wolken < 50 ? TX("teils bewölkt", "partly cloudy")
                       : wolken < 80 ? TX("meist bewölkt", "mostly cloudy") : TX("bedeckt", "overcast");
  const char *grundM = !n.dunkelArt ? TX("nicht richtig dunkel", "never really dark")
                       : stoert < 0.1f ? TX("kein Mondlicht", "no moonlight")
                       : stoert < 0.45f ? TX("etwas Mondlicht", "some moonlight") : TX("viel Mondlicht", "bright moon");
  char grund[48];
  snprintf(grund, sizeof(grund), "%s · %s", grundW, grundM);

  // Dunkelheit
  char dunkel[40], ohneMond[40];
  uhrzeit(a, sizeof(a), n.dunkelVon);
  uhrzeit(b, sizeof(b), n.dunkelBis);
  if (n.dunkelArt == 3) snprintf(dunkel, sizeof(dunkel), TX("dunkel %s – %s", "dark %s – %s"), a, b);
  else if (n.dunkelArt == 2) snprintf(dunkel, sizeof(dunkel), TX("halbdunkel %s – %s", "twilight %s – %s"), a, b);
  else strlcpy(dunkel, TX("keine echte Nacht", "no real night"), sizeof(dunkel));
  snprintf(ohneMond, sizeof(ohneMond), TX("%d:%02d h mondfrei", "%d:%02d h moonless"), n.ohneMondMin / 60, n.ohneMondMin % 60);

  // Zeitleiste: 1 h vor Sonnenuntergang bis 1 h nach Sonnenaufgang (sonst 16 bis 8 Uhr)
  time_t von = n.unter ? n.unter - 3600 : n.mittag + 4 * 3600, bis = n.auf ? n.auf + 3600 : n.mittag + 20 * 3600;
  if (bis <= von) bis = von + 16 * 3600;
  const int lx = ox + 10, lb = B - 20, ly = hk ? 158 : 82, lh = hk ? 22 : 20;
  auto xVon = [&](time_t z) { return lx + (int)((int64_t)(z - von) * lb / (bis - von)); };
  for (int x = 0; x < lb; x++) {
    time_t z = von + (int64_t)x * (bis - von) / lb;
    float i = (z - n.mittag) / 600.0f;
    int k = constrain((int)i, 0, NACHT_SCHRITTE - 2);
    float f = constrain(i - k, 0.0f, 1.0f);
    float sh = n.sonne[k] + (n.sonne[k + 1] - n.sonne[k]) * f, mh = n.mond[k] + (n.mond[k + 1] - n.mond[k]) * f;
    uint16_t c = daemmerFarbe(sh);
    // Wolken: grau darueberlegen, je dichter desto mehr
    int w = himmel.wolkenAb ? (z - himmel.wolkenAb) / 3600 : -1;
    if (w >= 0 && w < 72 && himmel.wolken[w] > 0) c = bild.alphaBlend(himmel.wolken[w] * 170 / 100, farbe(120, 126, 140), c);
    bild.drawFastVLine(lx + x, ly, lh, c);
    // Mond ueber dem Horizont: helles Band darueber, so hell wie er beleuchtet ist
    if (mh > -0.833f) bild.drawFastHLine(lx + x, ly - 5, 1, bild.alphaBlend(80 + 175 * n.mondLicht, farbe(255, 236, 170), hintergrund(lx + x, ly - 5)));
  }
  bild.drawRoundRect(lx - 1, ly - 1, lb + 2, lh + 2, 3, bild.alphaBlend(120, WEISS, hintergrund(lx, ly - 8)));
  // Stunden-Marken (gerade Stunden), quer jede 2., hochkant jede 4. beschriftet
  schrift(bild, S_K);
  struct tm vt;
  localtime_r(&von, &vt);
  time_t marke = von + (3600 - (vt.tm_min * 60 + vt.tm_sec)) % 3600;
  for (; marke < bis; marke += 3600) {
    struct tm m;
    localtime_r(&marke, &m);
    if (m.tm_hour % (hk ? 4 : 2)) continue;
    int x = xVon(marke);
    bild.drawFastVLine(x, ly + lh, 3, HELL);
    snprintf(z, sizeof(z), "%d", m.tm_hour);
    schreibe(bild, z, x, ly + lh + 15, HELL, MITTE);
  }
  // jetzt
  time_t jetzt = time(nullptr);
  if (jetzt > von && jetzt < bis) {
    int x = xVon(jetzt);
    float puls = 0.6f + 0.4f * sinf(t * 0.006f);
    uint16_t f = bild.alphaBlend(255 * puls, farbe(255, 214, 120), hintergrund(x, ly - 10));
    bild.drawFastVLine(x, ly - 2, lh + 4, f);
    bild.fillTriangle(x - 4, ly - 9, x + 4, ly - 9, x, ly - 3, f);
  }

  // Zeiten von Sonne und Mond
  char sonne[40], mond[64];
  uhrzeit(a, sizeof(a), n.unter);
  uhrzeit(b, sizeof(b), n.auf);
  snprintf(sonne, sizeof(sonne), TX("Sonne unter %s · auf %s", "Sunset %s · sunrise %s"), a, b);
  // Mond: nur was in der Nacht passiert (Auf-/Untergang am Nachmittag interessiert hier nicht)
  bool mAuf = n.mondAuf >= von && n.mondAuf <= bis, mUnter = n.mondUnter >= von && n.mondUnter <= bis;
  int mitte = constrain((int)(((von + bis) / 2 - n.mittag) / 600), 0, NACHT_SCHRITTE - 1);
  char mondA[40], mondB[24];
  uhrzeit(a, sizeof(a), n.mondAuf);
  uhrzeit(b, sizeof(b), n.mondUnter);
  if (mAuf && mUnter && n.mondAuf < n.mondUnter) snprintf(mondA, sizeof(mondA), TX("Mond auf %s · unter %s", "Moonrise %s · set %s"), a, b);
  else if (mAuf && mUnter) snprintf(mondA, sizeof(mondA), TX("Mond unter %s · auf %s", "Moonset %s · rise %s"), b, a);
  else if (mAuf) snprintf(mondA, sizeof(mondA), TX("Mond geht um %s auf", "Moonrise %s"), a);
  else if (mUnter) snprintf(mondA, sizeof(mondA), TX("Mond geht um %s unter", "Moonset %s"), b);
  else strlcpy(mondA, n.mond[mitte] > 0 ? TX("Mond die ganze Nacht", "Moon up all night") : TX("kein Mond in der Nacht", "no moon tonight"), sizeof(mondA));
  snprintf(mondB, sizeof(mondB), TX("%d %% beleuchtet", "%d %% lit"), (int)lroundf(n.mondLicht * 100));
  snprintf(mond, sizeof(mond), "%s · %s", mondA, mondB);

  if (hk) {
    passend(note, ox + B / 2, 82, B - 12, noteFarbe, S_GF, MITTE);
    passend(grund, ox + B / 2, 101, B - 12, HELL, S_K, MITTE);
    passend(dunkel, ox + B / 2, 124, B - 12, WEISS, S_M, MITTE);
    passend(ohneMond, ox + B / 2, 142, B - 12, HELL, S_K, MITTE);
    uhrzeit(a, sizeof(a), n.unter);
    uhrzeit(b, sizeof(b), n.auf);
    snprintf(z, sizeof(z), TX("Sonne unter %s", "Sunset %s"), a);
    passend(z, ox + B / 2, 222, B - 12, HELL, S_K, MITTE);
    snprintf(z, sizeof(z), TX("Sonne auf %s", "Sunrise %s"), b);
    passend(z, ox + B / 2, 240, B - 12, HELL, S_K, MITTE);
    passend(mondA, ox + B / 2, 262, B - 12, farbe(255, 236, 170), S_K, MITTE);
    passend(mondB, ox + B / 2, 280, B - 12, farbe(255, 236, 170), S_K, MITTE);
  } else {
    passend(note, ox + 8, 46, 120, noteFarbe, S_GF, LINKS);
    passend(grund, ox + 8, 64, 195, HELL, S_K, LINKS);
    passend(dunkel, ox + B - 8, 46, 170, WEISS, S_M, RECHTS);
    passend(ohneMond, ox + B - 8, 64, 105, HELL, S_K, RECHTS);
    passend(sonne, ox + B / 2, 140, B - 12, HELL, S_K, MITTE);
    passend(mond, ox + B / 2, 156, B - 12, farbe(255, 236, 170), S_K, MITTE);
  }
}

// ------------------------------------------------------------
// Seite 2: Planeten - welche heute Nacht zu sehen sind, wo am hoechsten und wie hell
// ------------------------------------------------------------
const char *richtung(float az) {
  static const char *DE[] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
  static const char *EN[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int i = (int)lroundf(fmodf(az + 360, 360) / 45) % 8;
  return TX(DE[i], EN[i]);
}

static uint16_t planetFarbe(int i) {
  static const uint8_t F[PLANETEN][3] = {{205, 195, 185}, {255, 246, 214}, {255, 128, 88}, {242, 216, 172}, {234, 204, 128}};
  return farbe(F[i][0], F[i][1], F[i][2]);
}

void seitePlaneten(int ox, uint32_t t) {
  titel(ox, TX("Planeten heute Nacht", "Planets tonight"), t);
  const Nacht &n = himmel.nacht;
  const bool hk = hoch();
  // Himmel von Sueden gesehen: links Osten, rechts Westen (Azimut 45 bis 315 Grad), Hoehe mit Wurzel
  const int hor = hk ? 190 : 96, hoehe = hk ? 112 : 62, cx = ox + B / 2, halb = hk ? 74 : 148;
  auto px = [&](float az) { return cx + (int)(constrain(az - 180, -135.0f, 135.0f) / 135 * halb); };
  auto py = [&](float h) { return hor - (int)(sqrtf(constrain(h, 0.0f, 90.0f) / 90) * hoehe); };
  bild.drawFastHLine(cx - halb - 4, hor, 2 * halb + 8, bild.alphaBlend(130, farbe(170, 180, 205), hintergrund(cx, hor)));
  schrift(bild, S_K);
  for (int k = 1; k < 8; k++) {
    if (hk && k % 2) continue;  // hochkant nur O, S, W
    int x = px(k * 45);
    schreibe(bild, richtung(k * 45), x, hor + 12, farbe(150, 160, 190), MITTE);
  }

  int anzahl = 0;
  for (int i = 0; i < PLANETEN; i++) anzahl += n.gueltig && n.planet[i].sichtbar;
  if (!anzahl) {
    passend(TX("Heute Nacht ist kein", "No planet is well placed"), ox + B / 2, hor - 34, B - 12, HELL, S_M, MITTE);
    passend(TX("Planet gut zu sehen", "tonight"), ox + B / 2, hor - 14, B - 12, HELL, S_M, MITTE);
    return;
  }

  // Planeten an ihrem hoechsten Punkt der Nacht: Groesse nach Helligkeit, leichtes Funkeln.
  // Namen ueber den Punkt - stoesst er an einen schon geschriebenen, darunter, sonst weglassen (steht ja in der Liste)
  int belegt[PLANETEN][4], nBelegt = 0;
  auto frei = [&](int x0, int y0, int x1, int y1) {
    for (int k = 0; k < nBelegt; k++)
      if (x0 < belegt[k][2] && x1 > belegt[k][0] && y0 < belegt[k][3] && y1 > belegt[k][1]) return false;
    return true;
  };
  for (int i = 0; i < PLANETEN; i++) {
    const Planet &p = n.planet[i];
    if (!p.sichtbar) continue;
    int x = px(p.az), y = py(p.hoehe);
    float r = constrain(4.5f - p.mag * 0.7f, 2.5f, 6.5f);
    float puls = 0.5f + 0.5f * sinf(t * 0.003f + i * 1.7f);
    bild.fillSmoothCircle(x, y, r + 4 + puls * 2, bild.alphaBlend(50 + 30 * puls, planetFarbe(i), hintergrund(x, y - 12)));
    bild.fillSmoothCircle(x, y, r, planetFarbe(i));
    if (i == 4) bild.drawEllipse(x, y, r + 4, 2, planetFarbe(i));  // Saturn mit Ring
    // Platz fuer den Namen: ueber, unter, rechts oder links vom Punkt
    int w = breite(bild, planetName(i)) / 2 + 2, ri = (int)r;
    const int platz[4][2] = {{x, y - ri - 6}, {x, y + ri + 14}, {x + ri + 6 + w, y + 4}, {x - ri - 6 - w, y + 4}};
    for (auto &p : platz) {
      int tx = p[0], ty = p[1];
      if (tx - w < ox || tx + w > ox + B || !frei(tx - w, ty - 11, tx + w, ty + 2)) continue;
      schreibe(bild, planetName(i), tx, ty, WEISS, MITTE);
      belegt[nBelegt][0] = tx - w, belegt[nBelegt][1] = ty - 11, belegt[nBelegt][2] = tx + w, belegt[nBelegt][3] = ty + 2, nBelegt++;
      break;
    }
  }

  // Liste: Name, wann zu sehen, wo am hoechsten, Helligkeit
  int zeile = 0;
  for (int i = 0; i < PLANETEN && zeile < (hk ? 5 : 4); i++) {
    const Planet &p = n.planet[i];
    if (!p.sichtbar) continue;
    char a[8], b[8], z[64], mag[12];
    uhrzeit(a, sizeof(a), p.ab);
    uhrzeit(b, sizeof(b), p.bis);
    snprintf(mag, sizeof(mag), "%+.1f", p.mag);
    if (!einst.sprache) for (char *c = mag; *c; c++) if (*c == '.') *c = ',';
    int y = hk ? 226 + zeile * 18 : 121 + zeile * 13;
    bild.fillSmoothCircle(ox + (hk ? 12 : 14), y - 4, 3, planetFarbe(i));
    if (hk) snprintf(z, sizeof(z), "%s %s – %s", planetName(i), a, b);
    else snprintf(z, sizeof(z), TX("%s  %s – %s · %d° im %s · %s mag", "%s  %s – %s · %d° %s · %s mag"), planetName(i), a, b,
                  (int)lroundf(p.hoehe), richtung(p.az), mag);
    passend(z, ox + (hk ? 20 : 22), y, B - (hk ? 26 : 30), WEISS, S_K, LINKS);
    zeile++;
  }
}

// ------------------------------------------------------------
// Seite 3: ISS & Satelliten - der naechste sichtbare Ueberflug, nur wenn einer in den naechsten 20 h kommt
// ------------------------------------------------------------
Ueberflug sat;                 // der naechste (mit Bahn)
Ueberflug satListe[SAT_LISTE]; // die danach
bool satDa = false;
int satWeitere = 0;

// Kleine Satelliten: ISS mit Gitter und je zwei goldenen Solarfluegeln, Tiangong als T mit blauen
// Fluegeln, Hubble als silberne Roehre mit zwei Fluegeln
void satZeichnen(int nr, int x, int y) {
  const uint16_t gold = farbe(217, 164, 65), linie = farbe(120, 86, 30), blau = farbe(90, 130, 210), silber = farbe(220, 226, 236);
  if (nr == 0) {
    bild.drawFastHLine(x - 12, y, 25, farbe(205, 214, 228));
    for (int sx : {-11, -7, 7, 11}) {
      bild.fillRect(x + sx - 1, y - 6, 3, 5, gold);
      bild.fillRect(x + sx - 1, y + 2, 3, 5, gold);
      bild.drawFastVLine(x + sx, y - 6, 5, linie);
      bild.drawFastVLine(x + sx, y + 2, 5, linie);
    }
    bild.fillRect(x - 3, y - 2, 7, 4, farbe(242, 245, 250));
    bild.drawFastVLine(x, y - 2, 4, farbe(154, 166, 187));
  } else if (nr == 1) {
    bild.fillRect(x - 6, y - 1, 13, 3, silber);  // Kernmodul
    bild.fillRect(x - 1, y - 6, 3, 13, silber);  // Labormodule quer
    for (int sx : {-10, 8}) bild.fillRect(x + sx, y - 2, 3, 5, blau);
    for (int sy : {-9, 7}) bild.fillRect(x - 2, y + sy, 5, 3, blau);
  } else {
    bild.fillRoundRect(x - 5, y - 2, 11, 5, 2, silber);
    bild.fillRect(x - 2, y - 8, 4, 5, blau);
    bild.fillRect(x - 2, y + 4, 4, 5, blau);
  }
}

void seiteSatelliten(int ox, uint32_t t) {
  char z[48];
  snprintf(z, sizeof(z), TX("Satelliten über %s", "Satellites over %s"), himmel.ort);
  titel(ox, z, t);
  const Ueberflug &iss = sat;
  if (!satDa) {
    schrift(bild, S_M);
    schreibe(bild, TX("Kein Überflug in Sicht", "No pass in sight"), ox + B / 2, H / 2, HELL, MITTE);
    return;
  }
  const bool hk = hoch();
  time_t jetzt = time(nullptr);
  const bool lauft = jetzt >= iss.start && jetzt <= iss.ende;

  // Countdown unter dem Titel
  int y0 = hk ? HOCH_TITEL_Y + 18 : 36;
  schrift(bild, S_K);
  if (jetzt < iss.sichtAb) {
    long s = iss.sichtAb - jetzt;
    if (s >= 3600) snprintf(z, sizeof(z), TX("in %ld h %ld min", "in %ld h %ld min"), s / 3600, s % 3600 / 60);
    else if (s >= 60) snprintf(z, sizeof(z), TX("in %ld min", "in %ld min"), (s + 59) / 60);
    else snprintf(z, sizeof(z), TX("in %ld s", "in %ld s"), s);
    char mit[56];
    snprintf(mit, sizeof(mit), "%s %s", satName(iss.sat), z);
    schreibe(bild, mit, ox + 8, y0, farbe(255, 190, 90), LINKS);
  } else if (jetzt <= iss.sichtBis) {
    float puls = 0.6f + 0.4f * sinf(t * 0.008f);
    snprintf(z, sizeof(z), TX("%s jetzt am Himmel!", "%s: look up now!"), satName(iss.sat));
    schreibe(bild, z, ox + 8, y0,
             bild.alphaBlend(255 * puls, farbe(110, 230, 130), hintergrund(ox + 40, y0 - 5)), LINKS);
  }

  // danach: der naechste Ueberflug eines Satelliten (quer rechts)
  if (!hk && satWeitere) {
    const Ueberflug &d = satListe[0];
    struct tm a2, h2;
    localtime_r(&d.sichtAb, &a2);
    localtime_r(&jetzt, &h2);
    static const char *WT_DE[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"}, *WT_EN[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *tag2 = a2.tm_yday == h2.tm_yday ? TX("heute", "today") : TX(WT_DE[a2.tm_wday], WT_EN[a2.tm_wday]);
    snprintf(z, sizeof(z), TX("danach %s %s %02d:%02d", "then %s %s %02d:%02d"), satName(d.sat), tag2, a2.tm_hour, a2.tm_min);
    schrift(bild, S_K);
    schreibe(bild, z, ox + B - 8, y0, HELL, RECHTS);
  }

  // Himmel von vorn: Blick in Richtung der Bahnmitte, links/rechts je 100 Grad, oben senkrecht.
  // Hoehe mit Wurzel, damit auch flache Ueberfluege (in Deutschland oft 10-30 Grad) gut zu sehen sind
  const int hor = hk ? 196 : 108, hoehe = hk ? 110 : 64;
  const int cx = ox + B / 2, halb = hk ? 74 : 140;
  auto px = [&](float az) { return cx + (int)(constrain(fmodf(az - iss.azMitte + 540, 360) - 180, -100.0f, 100.0f) / 100 * halb); };
  auto py = [&](float el) { return hor - (int)(sqrtf(max(0.0f, el) / 90) * hoehe); };

  const uint16_t grau = bild.alphaBlend(130, farbe(170, 180, 205), hintergrund(cx, hor));
  bild.drawFastHLine(cx - halb - 6, hor, 2 * halb + 12, grau);
  schrift(bild, S_K);
  for (int k = 0; k < 8; k++) {
    float rel = fmodf(k * 45 - iss.azMitte + 540, 360) - 180;
    if (fabsf(rel) > 96) continue;
    schreibe(bild, richtung(k * 45), px(k * 45), hor + 12, farbe(150, 160, 190), MITTE);
  }
  // Bahn: gepunktet, der sichtbare Teil hell
  for (int i = 0; i + 1 < iss.punkte; i++) {
    const SatPunkt &a = iss.bahn[i], &b = iss.bahn[i + 1];
    // ausserhalb der Ansicht (mehr als 100 Grad seitlich) nicht zeichnen
    if (fabsf(fmodf(a.az10 / 10.0f - iss.azMitte + 540, 360) - 180) > 100 ||
        fabsf(fmodf(b.az10 / 10.0f - iss.azMitte + 540, 360) - 180) > 100)
      continue;
    int xa = px(a.az10 / 10.0f), ya = py(a.el10 / 10.0f), xb = px(b.az10 / 10.0f), yb = py(b.el10 / 10.0f);
    if (a.sichtbar && b.sichtbar) bild.drawWideLine(xa, ya, xb, yb, 1.5f, bild.alphaBlend(150, farbe(255, 236, 190), hintergrund(xa, ya)));
    else if (i % 2 == 0) bild.drawPixel(xa, ya, grau);
  }

  if (iss.punkte < 2) return;
  // Wo steht der Satellit? Waehrend des Ueberflugs echt, sonst fliegt sie den sichtbaren Teil als Vorschau ab
  float pos;  // Index in bahn[] (mit Nachkommastellen)
  int i0 = (iss.sichtAb - iss.start) / SAT_SCHRITT, i1 = (iss.sichtBis - iss.start) / SAT_SCHRITT;
  if (lauft) pos = float(jetzt - iss.start) / SAT_SCHRITT + (t % 1000) / 1000.0f / SAT_SCHRITT;
  else pos = i0 + (t % 7000) / 7000.0f * max(1, i1 - i0);
  auto ort = [&](float p, int &x, int &y) {
    p = constrain(p, 0.0f, iss.punkte - 1.001f);
    int i = (int)p;
    float f = p - i;
    const SatPunkt &a = iss.bahn[i], &b = iss.bahn[i + 1];
    float daz = fmodf((b.az10 - a.az10) / 10.0f + 540, 360) - 180;
    x = px(a.az10 / 10.0f + daz * f);
    y = py((a.el10 + (b.el10 - a.el10) * f) / 10.0f);
  };
  for (int k = 14; k >= 1; k--) {  // Leuchtschweif
    int x, y;
    ort(pos - k * 0.12f, x, y);
    bild.fillSmoothCircle(x, y, 2, bild.alphaBlend(170 * (14 - k) / 14, farbe(255, 236, 190), hintergrund(x, y)));
  }
  int x, y;
  ort(pos, x, y);
  bild.fillSmoothCircle(x, y, 6, bild.alphaBlend(45, farbe(255, 240, 200), hintergrund(x, y)));
  satZeichnen(iss.sat, x, y);

  // Wann, wie lange, woher
  struct tm a, h;
  localtime_r(&iss.sichtAb, &a);
  localtime_r(&jetzt, &h);
  static const char *TAG_DE[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  static const char *TAG_EN[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char *tag = a.tm_yday == h.tm_yday ? TX("Heute", "Today")
                    : a.tm_yday == (h.tm_yday + 1) % (h.tm_year % 4 ? 365 : 366) ? TX("Morgen", "Tomorrow")
                                                                                  : TX(TAG_DE[a.tm_wday], TAG_EN[a.tm_wday]);
  int minuten = max(1L, (long)lroundf((iss.sichtBis - iss.sichtAb) / 60.0f));
  // vier Teile: wann, wie lange, woher/wohin, wie hoch - quer je zwei in einer Zeile, hochkant untereinander
  char wann[24], dauer[24], weg[24], gipfel[24];
  if (lauft && jetzt >= iss.sichtAb && jetzt <= iss.sichtBis) {  // gerade zu sehen: wohin schauen?
    const SatPunkt &p = iss.bahn[constrain((int)((jetzt - iss.start) / SAT_SCHRITT), 0, iss.punkte - 1)];
    snprintf(wann, sizeof(wann), TX("Schau nach %s", "Look %s"), richtung(p.az10 / 10.0f));
    snprintf(dauer, sizeof(dauer), TX("%d° hoch", "%d° up"), p.el10 / 10);
  } else {
    snprintf(wann, sizeof(wann), "%s %02d:%02d", tag, a.tm_hour, a.tm_min);
    snprintf(dauer, sizeof(dauer), TX("%d min sichtbar", "visible %d min"), minuten);
  }
  snprintf(weg, sizeof(weg), TX("von %s nach %s", "from %s to %s"), richtung(iss.azAb), richtung(iss.azBis));
  snprintf(gipfel, sizeof(gipfel), TX("bis %d° hoch", "up to %d°"), (int)lroundf(iss.maxHoehe));
  if (hk) {
    schrift(bild, S_MF);
    schreibe(bild, wann, ox + B / 2, 236, WEISS, MITTE);
    schrift(bild, S_K);
    schreibe(bild, dauer, ox + B / 2, 256, WEISS, MITTE);
    schreibe(bild, weg, ox + B / 2, 276, HELL, MITTE);
    schreibe(bild, gipfel, ox + B / 2, 294, HELL, MITTE);
  } else {
    char z1[52], z2[52];
    snprintf(z1, sizeof(z1), "%s · %s", wann, dauer);
    snprintf(z2, sizeof(z2), "%s · %s", weg, gipfel);
    schrift(bild, S_M);
    schreibe(bild, z1, ox + B / 2, 143, WEISS, MITTE);
    schrift(bild, S_K);
    schreibe(bild, z2, ox + B / 2, 158, HELL, MITTE);
  }
}

// ------------------------------------------------------------
// Seite 4: Weltraumwetter - Kp-Index, Polarlicht-Chance fuer den eigenen Ort, Sonnenwind, Eruptionen
// ------------------------------------------------------------
Weltraum welt;
bool weltDa = false;

static uint16_t kpFarbe(float kp) {
  if (kp < 4) return farbe(110, 220, 120);
  if (kp < 5) return farbe(240, 220, 90);
  if (kp < 6) return farbe(255, 160, 60);
  if (kp < 7) return farbe(255, 100, 60);
  return farbe(240, 70, 130);
}

static uint16_t flareFarbe(const char *k) {
  if (*k == 'X') return farbe(255, 80, 90);
  if (*k == 'M') return farbe(255, 160, 60);
  if (*k == 'C') return farbe(240, 220, 90);
  return HELL;
}

// Polarlicht: gruene Vorhaenge mit violettem Saum, die langsam wabern. staerke 0..1
void polarlicht(int ox, int oben, int hoehe, uint32_t t, float staerke) {
  for (int x = 0; x < B; x += 2) {
    int sx = ox + x;
    if (sx < 0 || sx >= B) continue;
    float basis = oben + hoehe * 0.75f + 6 * sinf(x * 0.031f + t * 0.0007f) + 4 * sinf(x * 0.071f - t * 0.0011f);
    float lang = hoehe * (0.45f + 0.25f * sinf(x * 0.047f + t * 0.0009f));
    float flackern = 0.55f + 0.45f * sinf(x * 0.21f + t * 0.0031f) * sinf(x * 0.05f - t * 0.0013f);
    for (int y = (int)(basis - lang); y < (int)basis; y++) {
      if (y < 0 || y >= H) continue;
      float p = (basis - y) / lang;  // 0 unten (gruen) .. 1 oben (violett, blass)
      uint16_t f = p < 0.6f ? farbe(70, 255, 150) : farbe(170, 90, 255);
      float a = staerke * flackern * (p < 0.6f ? 0.55f : 0.35f * (1 - p) * 2.5f);
      if (a <= 0.02f) continue;
      uint16_t alt = bild.readPixel(sx, y);
      uint16_t neu = bild.alphaBlend(constrain((int)(a * 255), 0, 255), f, alt);
      bild.drawPixel(sx, y, neu);
      bild.drawPixel(sx + 1, y, neu);
    }
  }
}

// Polarlicht-Chance heute Nacht: 0 = unwahrscheinlich, 1 = knapp, 2 = moeglich
static int polarlichtChance(float &kpMax, float &noetig) {
  const Nacht &n = himmel.nacht;
  Ort o = ortAktuell();
  noetig = kpFuerPolarlicht(o.lat, o.lon);
  time_t von = n.dunkelArt ? n.dunkelVon : n.mittag + 6 * 3600, bis = n.dunkelArt ? n.dunkelBis : n.mittag + 18 * 3600;
  kpMax = kpMaxZwischen(welt, max(von, time(nullptr) - 3 * 3600), bis);
  if (kpMax < 0) return -1;
  return kpMax >= noetig ? 2 : kpMax >= noetig - 1 ? 1 : 0;
}

void seiteWeltraum(int ox, uint32_t t) {
  titel(ox, TX("Weltraumwetter", "Space weather"), t);
  if (!weltDa) {
    passend(TX("Hole Daten von NOAA ...", "Loading NOAA data ..."), ox + B / 2, H / 2, B - 12, HELL, S_M, MITTE);
    return;
  }
  const bool hk = hoch();
  time_t jetzt = time(nullptr);
  char z[64];
  float kp = kpZu(welt, jetzt), kpMax, noetig;
  int chance = polarlichtChance(kpMax, noetig);
  if (chance >= 1) polarlicht(ox, hk ? 60 : 24, hk ? 90 : 60, t, chance == 2 ? 1.0f : 0.5f);

  // Kp jetzt und Zustand
  char kpText[16], zustand[24];
  snprintf(kpText, sizeof(kpText), "Kp %.1f", max(0.0f, kp));
  if (!einst.sprache) for (char *c = kpText; *c; c++) if (*c == '.') *c = ',';
  if (kp < 4) strlcpy(zustand, TX("ruhig", "quiet"), sizeof(zustand));
  else if (kp < 5) strlcpy(zustand, TX("unruhig", "unsettled"), sizeof(zustand));
  else snprintf(zustand, sizeof(zustand), TX("Sturm G%d", "storm G%d"), constrain((int)lroundf(kp) - 4, 1, 5));
  const char *chanceText = chance < 0 ? "?" : chance == 2 ? TX("möglich!", "possible!") : chance == 1 ? TX("knapp", "borderline") : TX("unwahrscheinlich", "unlikely");
  uint16_t chanceFarbe = chance == 2 ? farbe(110, 255, 160) : chance == 1 ? farbe(240, 220, 90) : HELL;
  char ab[32];
  snprintf(ab, sizeof(ab), TX("bei dir ab Kp %d", "needs Kp %d here"), (int)ceilf(noetig - 0.2f));

  // Verlauf: letzte 24 h gemessen, dann Vorhersage; gestrichelt die Polarlicht-Schwelle fuer den Ort
  const int gx = ox + 10, gb = B - 20, gy = hk ? 162 : 72, gh = hk ? 48 : 38;
  time_t von = jetzt - 24 * 3600, bis = welt.kpAb + welt.kpN * 3 * 3600;
  if (bis - von < 24 * 3600) bis = von + 4 * 86400;
  auto xVon = [&](time_t z2) { return gx + (int)((int64_t)(z2 - von) * gb / (bis - von)); };
  for (int i = 0; i < welt.kpN; i++) {
    time_t a = welt.kpAb + i * 3 * 3600;
    if (a + 3 * 3600 <= von || a >= bis) continue;
    int x0 = max(gx, xVon(a)), x1 = min(gx + gb, xVon(a + 3 * 3600)) - 1;
    int h = (int)(constrain(welt.kp[i], 0.0f, 9.0f) / 9 * gh);
    uint16_t f = kpFarbe(welt.kp[i]);
    if (welt.kpArt[i] == 2) f = bild.alphaBlend(120, f, hintergrund(x0, gy + gh - h));  // Vorhersage blasser
    if (h > 0 && x1 >= x0) bild.fillRect(x0, gy + gh - h, x1 - x0 + 1, h, f);
  }
  bild.drawFastHLine(gx, gy + gh, gb, bild.alphaBlend(140, WEISS, hintergrund(gx, gy + gh)));
  int sy = gy + gh - (int)(constrain(noetig, 0.0f, 9.0f) / 9 * gh);
  for (int x = gx; x < gx + gb; x += 6) bild.drawFastHLine(x, sy, 3, farbe(110, 255, 160));
  int jx = xVon(jetzt);
  bild.drawFastVLine(jx, gy - 2, gh + 4, farbe(255, 214, 120));
  // Tage beschriften (Mitte jedes Tages in Ortszeit)
  schrift(bild, S_K);
  static const char *WT_DE[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"}, *WT_EN[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  struct tm lt;
  localtime_r(&von, &lt);
  time_t mittag = von - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) + 12 * 3600;
  for (time_t m = mittag; m < bis; m += 86400) {
    if (m < von + 3600) continue;
    struct tm d;
    localtime_r(&m, &d);
    schreibe(bild, TX(WT_DE[d.tm_wday], WT_EN[d.tm_wday]), xVon(m), gy + gh + 13, HELL, MITTE);
  }

  // Sonnenwind und Eruptionen
  char wind[32], f24[40], f7[40];
  if (welt.wind) snprintf(wind, sizeof(wind), TX("Sonnenwind %d km/s", "Solar wind %d km/s"), welt.wind);
  else strlcpy(wind, TX("Sonnenwind ?", "Solar wind ?"), sizeof(wind));
  snprintf(f24, sizeof(f24), TX("Flare 24 h: %s", "Flare 24 h: %s"), *welt.flare24 ? welt.flare24 : "–");
  if (*welt.flare7) {
    struct tm d;
    localtime_r(&welt.flare7Zeit, &d);
    snprintf(f7, sizeof(f7), TX("7 Tage: %s (%s)", "7 days: %s (%s)"), welt.flare7, TX(WT_DE[d.tm_wday], WT_EN[d.tm_wday]));
  } else {
    strlcpy(f7, TX("7 Tage: –", "7 days: –"), sizeof(f7));
  }

  if (hk) {
    passend(kpText, ox + B / 2, 82, B - 12, kpFarbe(kp), S_GF, MITTE);
    passend(zustand, ox + B / 2, 100, B - 12, HELL, S_K, MITTE);
    passend(TX("Polarlicht heute Nacht", "Aurora tonight"), ox + B / 2, 122, B - 12, HELL, S_K, MITTE);
    passend(chanceText, ox + B / 2, 140, B - 12, chanceFarbe, S_MF, MITTE);
    passend(ab, ox + B / 2, 152 - 0, B - 12, HELL, S_K, MITTE);
    passend(wind, ox + B / 2, 242, B - 12, HELL, S_K, MITTE);
    passend(f24, ox + B / 2, 260, B - 12, flareFarbe(welt.flare24), S_K, MITTE);
    passend(f7, ox + B / 2, 278, B - 12, flareFarbe(welt.flare7), S_K, MITTE);
  } else {
    schrift(bild, S_GF);
    int kb = breite(bild, kpText);
    schreibe(bild, kpText, ox + 8, 50, kpFarbe(kp), LINKS);
    passend(zustand, ox + 14 + kb, 50, 80, HELL, S_K, LINKS);
    passend(TX("Polarlicht:", "Aurora:"), ox + B - 8, 36, 150, HELL, S_K, RECHTS);
    passend(chanceText, ox + B - 8, 52, 150, chanceFarbe, S_MF, RECHTS);
    passend(ab, ox + B - 8, 66, 150, HELL, S_K, RECHTS);
    snprintf(z, sizeof(z), "%s · %s", wind, f24);
    passend(z, ox + B / 2, 140, B - 12, HELL, S_K, MITTE);
    passend(f7, ox + B / 2, 156, B - 12, flareFarbe(welt.flare7), S_K, MITTE);
  }
}

// ------------------------------------------------------------
// Seite 5: Mond (rechnet das Geraet selbst aus dem Datum)
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
// Seite 6: Demnaechst - was die Himmelsstation bald kann
// ------------------------------------------------------------
void seiteDemnaechst(int ox, uint32_t t) {
  titel(ox, TX("Demnächst", "Coming soon"), t);
  static const char *DE[] = {"Raketenstarts", "Seeing & Durchsicht", "Starlink-Ketten", "Meteorströme & Finsternisse", "Die Sonne live (SDO)"};
  static const char *EN[] = {"Rocket launches", "Seeing & transparency", "Starlink trains", "Meteor showers & eclipses", "The sun live (SDO)"};
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
    case 0: seiteNacht(ox, t); break;
    case 1: seitePlaneten(ox, t); break;
    case 2: seiteSatelliten(ox, t); break;
    case 3: seiteWeltraum(ox, t); break;
    case 4: seiteMond(ox, t); break;
    case 5: seiteDemnaechst(ox, t); break;
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

// Seite "Satelliten" nur, wenn in den naechsten 20 h einer zu sehen ist
bool seiteAn(int s) {
  if (!(einst.seiten >> s & 1)) return false;
  if (s == 2) return satDa && sat.sichtAb - time(nullptr) < 20 * 3600;
  return true;
}

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
  satWeitere = satellitenHolen(sat, satListe, SAT_LISTE) - 1;
  satDa = satWeitere >= 0;
  weltDa = weltraumHolen(welt);
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

    // Satellit kommt: 5 min vorher einmal auf seine Seite springen und dort bleiben, bis er wieder weg ist
    time_t jetzt = time(nullptr);
    bool satGleich = satDa && seiteAn(2) && jetzt >= sat.sichtAb - 300 && jetzt <= sat.sichtBis;
    static time_t satGesprungen = 0;
    if (satGleich && satGesprungen != sat.start && !wechsel) {
      satGesprungen = sat.start;
      if (seite != 2) {
        alteSeite = seite;
        seite = 2;
        wechsel = true;
        wechselStart = t;
        seiteSeit = t;
      }
    }
    // Polarlicht-Alarm: Kp reicht jetzt fast fuer den Ort, und es ist dunkel -> hinspringen (hoechstens einmal pro Stunde)
    static uint32_t polarGesprungen = 0;
    Ort ort = ortAktuell();
    float kpJetzt = weltDa ? kpZu(welt, jetzt) : -1;
    bool polarAlarm = seiteAn(3) && kpJetzt >= kpFuerPolarlicht(ort.lat, ort.lon) - 0.67f && sonneHoehe(jetzt, ort.lat, ort.lon) < -12;
    if (polarAlarm && !satGleich && !wechsel && seite != 3 && (!polarGesprungen || t - polarGesprungen > 3600000UL)) {
      polarGesprungen = t;
      alteSeite = seite;
      seite = 3;
      wechsel = true;
      wechselStart = t;
      seiteSeit = t;
    }
    if (einst.wechsel && !wechsel && t - seiteSeit > einst.wechsel * 1000UL && t - letzterTastendruck > 30000 &&
        !(satGleich && seite == 2) && !(polarAlarm && seite == 3))
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
