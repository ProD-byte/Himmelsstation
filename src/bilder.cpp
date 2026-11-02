#include "bilder.h"
#include "einstellungen.h"
#include "schriften.h"

struct RGB { uint8_t r, g, b; };

uint16_t farbe(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static uint16_t farbe(RGB c) { return farbe(c.r, c.g, c.b); }
static uint16_t mische(RGB a, RGB b, float f) {
  return farbe(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f);
}
static RGB dunkler(RGB c, float f) { return {uint8_t(c.r * f), uint8_t(c.g * f), uint8_t(c.b * f)}; }

// ------------------------------------------------------------
// Wettercodes
// ------------------------------------------------------------

Bild bildFuerCode(int code) {
  if (code == 0) return KLAR;
  if (code <= 2) return HEITER;
  if (code == 3) return BEDECKT;
  if (code == 45 || code == 48) return NEBEL;
  if (code >= 51 && code <= 57) return NIESEL;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return REGEN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return SCHNEE;
  if (code >= 95) return GEWITTER;
  return BEDECKT;
}

const char *textFuerCode(int code, bool tag) {
  switch (code) {
    case 0:  return tag ? TX("Sonnig", "Sunny") : TX("Klar", "Clear");
    case 1:  return TX("Heiter", "Mostly clear");
    case 2:  return TX("Teils bewölkt", "Partly cloudy");
    case 3:  return TX("Bedeckt", "Overcast");
    case 45: return TX("Nebel", "Fog");
    case 48: return TX("Reifnebel", "Freezing fog");
    case 51: case 53: case 55: return TX("Nieselregen", "Drizzle");
    case 56: case 57: return TX("Eisniesel", "Freezing drizzle");
    case 61: return TX("Leichter Regen", "Light rain");
    case 63: return TX("Regen", "Rain");
    case 65: return TX("Starkregen", "Heavy rain");
    case 66: case 67: return TX("Eisregen", "Freezing rain");
    case 71: return TX("Leichter Schnee", "Light snow");
    case 73: return TX("Schnee", "Snow");
    case 75: return TX("Starker Schnee", "Heavy snow");
    case 77: return TX("Schneegriesel", "Snow grains");
    case 80: case 81: return TX("Regenschauer", "Showers");
    case 82: return TX("Starke Schauer", "Heavy showers");
    case 85: case 86: return TX("Schneeschauer", "Snow showers");
    case 95: return TX("Gewitter", "Thunderstorm");
    case 96: case 99: return TX("Gewitter, Hagel", "Storm, hail");
  }
  return TX("Unbekannt", "Unknown");
}

// ------------------------------------------------------------
// Himmel
// ------------------------------------------------------------

static void verlauf(Bild b, bool tag, RGB &oben, RGB &unten) {
  if (!tag) {
    switch (b) {
      case KLAR: case HEITER: oben = {6, 10, 38};  unten = {28, 40, 92}; return;
      case GEWITTER:          oben = {18, 14, 34}; unten = {52, 44, 74}; return;
      default:                oben = {16, 20, 34}; unten = {48, 54, 72}; return;
    }
  }
  switch (b) {
    case KLAR:     oben = {24, 92, 206};  unten = {110, 178, 240}; break;
    case HEITER:   oben = {40, 104, 190}; unten = {126, 174, 222}; break;
    case BEDECKT:  oben = {72, 88, 110};  unten = {132, 144, 160}; break;
    case NEBEL:    oben = {108, 114, 122}; unten = {170, 174, 180}; break;
    case NIESEL:   oben = {62, 80, 104};  unten = {112, 128, 148}; break;
    case REGEN:    oben = {40, 56, 80};   unten = {84, 100, 124}; break;
    case SCHNEE:   oben = {96, 118, 150}; unten = {182, 196, 214}; break;
    case GEWITTER: oben = {30, 26, 52};   unten = {74, 64, 96}; break;
  }
}

// Blitz: zweimal kurz hintereinander, dann Pause
bool blitzAktiv(uint32_t t) {
  uint32_t p = t % 4200;
  return p < 90 || (p > 180 && p < 260);
}

static RGB misch(RGB a, RGB b, float f) {
  return {uint8_t(a.r + (b.r - a.r) * f), uint8_t(a.g + (b.g - a.g) * f), uint8_t(a.b + (b.b - a.b) * f)};
}

static void wolkeForm(TFT_eSprite &s, float cx, float cy, float w, uint16_t c);

// Hinter dem Inhalt: grosse Wolken ziehen langsam vorbei, Regen/Schnee/Nebel ueber den ganzen Himmel
static void himmelEffekte(TFT_eSprite &s, Bild b, bool tag, uint32_t t) {
  int w = s.width(), h = s.height();
  auto bei = [&](float x, float y) { return s.readPixel(constrain((int)x, 0, w - 1), constrain((int)y, 0, h - 1)); };

  int wolken = b == HEITER ? 2 : b == KLAR ? 0 : b == NEBEL ? 0 : 4;
  bool dunkel = b == REGEN || b == GEWITTER;
  RGB wf = !tag ? RGB{140, 150, 180} : dunkel ? RGB{150, 160, 185} : RGB{255, 255, 255};
  uint8_t deck = b == HEITER ? 30 : dunkel ? 46 : 40;
  for (int i = 0; i < wolken; i++) {
    float bw = 150 + 40 * (i % 3);
    float tempo = 0.0035f + 0.0012f * i;  // Pixel pro Millisekunde
    float x = fmodf(t * tempo + i * 157.0f, w + bw * 1.3f) - bw * 0.65f;
    float y = 38 + (i * 47) % (h - 80);
    wolkeForm(s, x, y, bw, s.alphaBlend(deck, farbe(wf.r, wf.g, wf.b), bei(x, y)));
  }

  if (b == REGEN || b == GEWITTER || b == NIESEL) {
    int n = b == NIESEL ? 24 : 48;
    uint32_t periode = b == NIESEL ? 1700 : 850;
    float l = b == NIESEL ? 6 : 13;
    for (int i = 0; i < n; i++) {
      float p = ((t + i * 7919u) % periode) / float(periode);
      float x = (i * 53) % (w + 40) - p * 22;
      float y = p * (h + 30) - 20;
      s.drawWideLine(x, y, x - l * 0.25f, y + l, 1, s.alphaBlend(80, farbe(180, 205, 240), bei(x, y)));
    }
  } else if (b == SCHNEE) {
    for (int i = 0; i < 44; i++) {
      float p = ((t + i * 6151u) % 5200u) / 5200.0f;
      float x = (i * 67) % w + sinf(t * 0.0012f + i) * 12;
      float y = p * (h + 12) - 6;
      s.fillSmoothCircle(x, y, 1 + i % 2, s.alphaBlend(170, TFT_WHITE, bei(x, y)));
    }
  } else if (b == NEBEL) {
    // Nebelbaender treiben quer ueber den Bildschirm (zeilenweise, der Himmel ist pro Zeile einfarbig)
    for (int k = 0; k < 4; k++) {
      int y0 = 26 + k * 38 + sinf(t * 0.0006f + k) * 6;
      int x0 = sinf(t * 0.0003f + k * 2) * 60 - 60;
      for (int y = y0; y < y0 + 24 && y < h; y++) {
        uint8_t a = 50 * sinf(PI * (y - y0) / 24.0f);
        s.drawFastHLine(x0, y, w + 120, s.alphaBlend(a, farbe(235, 238, 242), bei(10, y)));
      }
    }
  }
}

void zeichneHimmel(TFT_eSprite &s, Bild b, bool tag, uint32_t t, bool blitz, float daemmerung) {
  RGB oben, unten;
  verlauf(b, tag, oben, unten);
  // Morgen- und Abendrot: oben violett, unten orange-rosa
  if (daemmerung > 0) {
    float d = min(daemmerung, 1.0f) * (b == KLAR || b == HEITER ? 1.0f : 0.45f);
    oben = misch(oben, {72, 52, 118}, d * 0.6f);
    unten = misch(unten, {255, 146, 92}, d * 0.8f);
  }
  int w = s.width(), h = s.height();
  for (int y = 0; y < h; y++) {
    uint16_t c = mische(oben, unten, y / float(h - 1));
    if (blitz) c = s.alphaBlend(110, TFT_WHITE, c);
    s.drawFastHLine(0, y, w, c);
  }

  // Sterne, die funkeln (nur nachts und wenn nicht zu bewoelkt)
  if (tag || !(b == KLAR || b == HEITER)) {
    himmelEffekte(s, b, tag, t);
    return;
  }
  uint32_t z = 12345;
  int anzahl = b == KLAR ? 34 : 18;
  for (int i = 0; i < anzahl; i++) {
    z = z * 1103515245u + 12345u;
    int x = (z >> 8) % w;
    z = z * 1103515245u + 12345u;
    int y = (z >> 8) % (h - 20);
    float hell = 0.5f + 0.5f * sinf(t * 0.0015f + i * 1.7f);
    s.drawPixel(x, y, s.alphaBlend(60 + hell * 195, TFT_WHITE, s.readPixel(x, y)));
    if (i % 6 == 0) {  // ein paar grosse Sterne mit Kreuz
      uint8_t a = 30 + hell * 110;
      s.drawPixel(x - 1, y, s.alphaBlend(a, TFT_WHITE, s.readPixel(x - 1, y)));
      s.drawPixel(x + 1, y, s.alphaBlend(a, TFT_WHITE, s.readPixel(x + 1, y)));
      s.drawPixel(x, y - 1, s.alphaBlend(a, TFT_WHITE, s.readPixel(x, y - 1)));
      s.drawPixel(x, y + 1, s.alphaBlend(a, TFT_WHITE, s.readPixel(x, y + 1)));
    }
  }
  himmelEffekte(s, b, tag, t);
}

uint16_t grundFarbe(TFT_eSprite &s, int x, int y) {
  static const int8_t PUNKTE[5][2] = {{0, 0}, {-7, -7}, {7, -7}, {-7, 7}, {7, 7}};
  uint16_t c[5];
  int hell[5];
  for (int i = 0; i < 5; i++) {
    c[i] = s.readPixel(constrain(x + PUNKTE[i][0], 0, s.width() - 1), constrain(y + PUNKTE[i][1], 0, s.height() - 1));
    hell[i] = ((c[i] >> 11) & 31) * 2 + ((c[i] >> 5) & 63) + (c[i] & 31) * 2;  // grob die Helligkeit
  }
  // den Punkt mit der mittleren Helligkeit nehmen
  for (int i = 0; i < 5; i++) {
    int kleiner = 0, gleich = 0;
    for (int j = 0; j < 5; j++) kleiner += hell[j] < hell[i], gleich += hell[j] == hell[i];
    if (kleiner <= 2 && kleiner + gleich > 2) return c[i];
  }
  return c[0];
}

// ------------------------------------------------------------
// Bausteine
// ------------------------------------------------------------

static void sonne(TFT_eSprite &s, float cx, float cy, float g, uint32_t t) {
  float r = g * 0.2f;
  uint16_t gelb = farbe(255, 198, 44);
  uint16_t hg = grundFarbe(s, cx, cy);
  float puls = 0.5f + 0.5f * sinf(t * 0.0025f);

  // Schein in zwei Stufen (aussen ganz zart)
  uint16_t schein = s.alphaBlend(18 + 12 * puls, gelb, hg);
  s.fillSmoothCircle(cx, cy, r * (2.1f + 0.15f * puls), schein);
  s.fillSmoothCircle(cx, cy, r * (1.55f + 0.12f * puls), s.alphaBlend(45 + 25 * puls, gelb, schein));

  // Strahlen drehen sich langsam und werden mal laenger, mal kuerzer
  float dreh = t * 0.0004f;
  float r1 = r * 1.4f;
  for (int i = 0; i < 8; i++) {
    float a = dreh + i * PI / 4;
    float l = g * (0.09f + 0.03f * sinf(t * 0.004f + i * 1.3f));
    s.drawWideLine(cx + cosf(a) * r1, cy + sinf(a) * r1, cx + cosf(a) * (r1 + l), cy + sinf(a) * (r1 + l),
                   max(2.0f, g * 0.045f), gelb);
  }
  // Kern mit weichem Verlauf zur hellen Mitte
  s.fillSmoothCircle(cx, cy, r, farbe(255, 176, 30));
  s.fillSmoothCircle(cx - r * 0.08f, cy - r * 0.08f, r * 0.82f, gelb);
  s.fillSmoothCircle(cx - r * 0.2f, cy - r * 0.2f, r * 0.55f, farbe(255, 220, 100));
  s.fillSmoothCircle(cx - r * 0.3f, cy - r * 0.3f, r * 0.25f, farbe(255, 244, 190));
}

static void mond(TFT_eSprite &s, float cx, float cy, float g, uint32_t t) {
  float r = g * 0.21f;
  uint16_t hell = farbe(238, 234, 210);
  uint16_t hg = grundFarbe(s, cx, cy);
  float puls = 0.5f + 0.5f * sinf(t * 0.0018f);
  s.fillSmoothCircle(cx, cy, r * (1.5f + 0.1f * puls), s.alphaBlend(35 + 20 * puls, hell, hg));
  s.fillSmoothCircle(cx, cy, r, hell);
  uint16_t krater = farbe(212, 207, 182);
  s.fillSmoothCircle(cx - r * 0.35f, cy - r * 0.2f, r * 0.22f, krater);
  s.fillSmoothCircle(cx + r * 0.32f, cy + r * 0.3f, r * 0.16f, krater);
  s.fillSmoothCircle(cx + r * 0.12f, cy - r * 0.48f, r * 0.1f, krater);
}

// Wolke: cy ist die Oberkante des flachen Unterteils
static void wolkeForm(TFT_eSprite &s, float cx, float cy, float w, uint16_t c) {
  s.fillSmoothRoundRect(cx - w * 0.45f, cy, w * 0.9f, w * 0.2f, w * 0.1f, c);
  s.fillSmoothCircle(cx - w * 0.24f, cy + w * 0.02f, w * 0.18f, c);
  s.fillSmoothCircle(cx - w * 0.01f, cy - w * 0.08f, w * 0.26f, c);
  s.fillSmoothCircle(cx + w * 0.25f, cy + w * 0.03f, w * 0.17f, c);
}

static void wolke(TFT_eSprite &s, float cx, float cy, float w, RGB c) {
  wolkeForm(s, cx + w * 0.03f, cy + w * 0.05f, w, farbe(dunkler(c, 0.72f)));  // Schatten unten
  wolkeForm(s, cx, cy, w, farbe(c));
  // Lichtkanten oben links auf den Wolkenbuckeln - so wirkt die Wolke rund
  uint16_t licht = farbe(misch(c, {255, 255, 255}, 0.45f));
  s.fillSmoothCircle(cx - w * 0.05f, cy - w * 0.15f, w * 0.15f, licht);
  s.fillSmoothCircle(cx - w * 0.27f, cy - w * 0.04f, w * 0.09f, licht);
  s.fillSmoothCircle(cx + w * 0.21f, cy - w * 0.03f, w * 0.08f, farbe(misch(c, {255, 255, 255}, 0.25f)));
}

static void blitzZeichen(TFT_eSprite &s, float cx, float cy, float g, uint16_t c, bool leuchtet) {
  if (leuchtet) {  // Leuchten um den Blitz
    uint16_t hg = grundFarbe(s, cx, cy + g * 0.2f);
    uint16_t schein = s.alphaBlend(40, farbe(255, 240, 160), hg);
    s.fillSmoothCircle(cx, cy + g * 0.2f, g * 0.3f, schein);
    s.fillSmoothCircle(cx, cy + g * 0.2f, g * 0.18f, s.alphaBlend(70, farbe(255, 240, 160), schein));
  }
  s.fillTriangle(cx + g * 0.04f, cy, cx - g * 0.09f, cy + g * 0.2f, cx + g * 0.01f, cy + g * 0.2f, c);
  s.fillTriangle(cx - g * 0.03f, cy + g * 0.17f, cx + g * 0.08f, cy + g * 0.17f, cx - g * 0.07f, cy + g * 0.4f, c);
}

// Fallende Teilchen (Regen oder Schnee), blenden am Ende aus
static void teilchen(TFT_eSprite &s, float x0, float breite, float y0, float hoehe, int n, uint32_t periode,
                     float g, bool schnee, uint16_t c, uint32_t t) {
  for (int i = 0; i < n; i++) {
    uint32_t ph = (t + uint32_t(i * periode * 0.37f)) % periode;
    float p = ph / float(periode);
    float x = x0 + breite * (i + 0.5f) / n + sinf(i * 12.9f) * breite * 0.04f;
    float y = y0 + p * hoehe;
    uint8_t a = p > 0.75f ? (1 - p) / 0.25f * 255 : 255;
    if (schnee) {
      // Schneeflocke als sechsstrahliger Stern, der sich langsam dreht
      x += sinf(t * 0.002f + i * 2.1f) * g * 0.05f;
      float r = max(2.0f, g * 0.045f), dreh = t * 0.0015f + i;
      uint16_t f = s.alphaBlend(a, c, s.readPixel(x, y));
      for (int k = 0; k < 3; k++) {
        float wk = dreh + k * PI / 3;
        s.drawWideLine(x - cosf(wk) * r, y - sinf(wk) * r, x + cosf(wk) * r, y + sinf(wk) * r, max(1.0f, g * 0.012f), f);
      }
    } else {
      // Tropfen: Spur mit rundem Kopf, unten kleine Spritzer
      float l = g * (n > 6 ? 0.11f : 0.07f), dicke = max(1.5f, g * 0.03f);
      uint16_t f = s.alphaBlend(a, c, s.readPixel(x, y));
      s.drawWideLine(x, y, x - l * 0.3f, y + l, dicke * 0.7f, f);
      s.fillSmoothCircle(x - l * 0.3f, y + l, dicke * 0.9f, f);
      if (p > 0.8f) {
        float sp = (p - 0.8f) / 0.2f * g * 0.05f, yb = y0 + hoehe + l;
        uint16_t fs = s.alphaBlend(150 * (1 - p) / 0.2f, c, s.readPixel(x, yb));
        s.drawWideLine(x - sp * 0.3f, yb - sp * 0.2f, x - sp, yb - sp * 0.8f, 1, fs);
        s.drawWideLine(x + sp * 0.3f, yb - sp * 0.2f, x + sp, yb - sp * 0.8f, 1, fs);
      }
    }
  }
}

// ------------------------------------------------------------
// Wetterbilder
// ------------------------------------------------------------

void zeichneBild(TFT_eSprite &s, Bild b, bool tag, int cx, int cy, int groesse, uint32_t t) {
  float g = groesse;
  float d = sinf(t * 0.0007f) * g * 0.04f;  // Wolken treiben hin und her
  const RGB WEISS = {238, 242, 247};

  switch (b) {
    case KLAR:
      if (tag) sonne(s, cx, cy, g, t);
      else mond(s, cx, cy, g, t);
      break;

    case HEITER:
      if (tag) sonne(s, cx + g * 0.16f, cy - g * 0.14f, g * 0.78f, t);
      else mond(s, cx + g * 0.16f, cy - g * 0.14f, g * 0.8f, t);
      wolke(s, cx - g * 0.08f + d, cy + g * 0.12f, g * 0.74f, WEISS);
      break;

    case BEDECKT:
      wolke(s, cx + g * 0.18f - d, cy - g * 0.04f, g * 0.62f, {176, 186, 198});
      wolke(s, cx - g * 0.06f + d, cy + g * 0.08f, g * 0.8f, WEISS);
      break;

    case NEBEL:
      wolke(s, cx + d, cy - g * 0.1f, g * 0.66f, {205, 210, 216});
      for (int k = 0; k < 3; k++) {
        float bw = g * (0.74f - k * 0.08f);
        float x = cx + sinf(t * 0.0009f + k * 1.3f) * g * 0.07f;
        float y = cy + g * (0.14f + k * 0.1f);
        float h = max(3.0f, g * 0.05f);
        s.fillSmoothRoundRect(x - bw / 2, y, bw, h, h / 2, farbe(220, 224, 228));
      }
      break;

    case NIESEL:
      teilchen(s, cx - g * 0.3f, g * 0.6f, cy + g * 0.04f, g * 0.34f, 5, 1100, g, false, farbe(150, 200, 255), t);
      wolke(s, cx + d, cy - g * 0.1f, g * 0.8f, {214, 220, 228});
      break;

    case REGEN:
      teilchen(s, cx - g * 0.32f, g * 0.64f, cy + g * 0.02f, g * 0.38f, 8, 600, g, false, farbe(120, 180, 255), t);
      wolke(s, cx + d, cy - g * 0.12f, g * 0.84f, {188, 196, 210});
      break;

    case SCHNEE:
      teilchen(s, cx - g * 0.32f, g * 0.64f, cy + g * 0.02f, g * 0.4f, 7, 2400, g, true, TFT_WHITE, t);
      wolke(s, cx + d, cy - g * 0.12f, g * 0.8f, {228, 234, 242});
      break;

    case GEWITTER: {
      bool hell = blitzAktiv(t);
      teilchen(s, cx - g * 0.32f, g * 0.64f, cy + g * 0.02f, g * 0.38f, 5, 520, g, false, farbe(110, 160, 230), t);
      wolke(s, cx + g * 0.16f - d, cy - g * 0.2f, g * 0.6f, {92, 96, 116});
      wolke(s, cx - g * 0.04f + d, cy - g * 0.1f, g * 0.84f, {140, 146, 166});
      blitzZeichen(s, cx + g * 0.02f, cy + g * 0.06f, g, hell ? farbe(255, 255, 210) : farbe(255, 206, 60), hell);
      break;
    }
  }
}

// ------------------------------------------------------------
// Mond und Warnung
// ------------------------------------------------------------

float mondphase(time_t jetzt) {
  // Bekannter Neumond: 6.1.2000 18:14 UTC, ein Mondmonat dauert 29,53 Tage
  double tage = (jetzt - 947182440.0) / 86400.0;
  double p = fmod(tage / 29.530588853, 1.0);
  return p < 0 ? p + 1 : p;
}

const char *mondName(float p) {
  // kurz genug fuer die rechte Haelfte des Displays
  if (p < 0.03f || p > 0.97f) return TX("Neumond", "New moon");
  if (p < 0.22f) return TX("Junge Sichel", "Waxing crescent");
  if (p < 0.28f) return TX("Erstes Viertel", "First quarter");
  if (p < 0.47f) return TX("Zunehmend", "Waxing gibbous");
  if (p < 0.53f) return TX("Vollmond", "Full moon");
  if (p < 0.72f) return TX("Abnehmend", "Waning gibbous");
  if (p < 0.78f) return TX("Letztes Viertel", "Last quarter");
  return TX("Alte Sichel", "Waning crescent");
}

void zeichneMondphase(TFT_eSprite &s, int cx, int cy, int r, float phase) {
  uint16_t hell = farbe(238, 234, 210);
  uint16_t hg = grundFarbe(s, cx, cy);
  s.fillSmoothCircle(cx, cy, r + 4, s.alphaBlend(30, hell, hg));   // Schein
  s.fillSmoothCircle(cx, cy, r, s.alphaBlend(60, hell, TFT_BLACK)); // dunkle Seite
  // Beleuchteter Teil zeilenweise: zunehmend rechts hell, abnehmend links (Nordhalbkugel)
  float k = cosf(phase * 2 * PI);
  for (int y = -r + 1; y < r; y++) {
    float w = sqrtf(r * r - y * y);
    float a = phase < 0.5f ? w * k : -w;
    float b = phase < 0.5f ? w : -w * k;
    if (b > a) s.drawFastHLine(cx + lroundf(a), cy + y, lroundf(b - a), hell);
  }
  uint16_t krater = farbe(212, 207, 182);
  for (int i = 0; i < 3; i++) {
    static const float kx[] = {-0.35f, 0.32f, 0.12f}, ky[] = {-0.2f, 0.3f, -0.48f}, kr[] = {0.22f, 0.16f, 0.1f};
    int x = cx + kx[i] * r, y = cy + ky[i] * r;
    if (s.readPixel(x, y) == hell) s.fillCircle(x, y, max(1, (int)(kr[i] * r)), krater);
  }
}

void zeichneWarnzeichen(TFT_eSprite &s, int cx, int cy, int g, uint16_t f) {
  int h = g, b = g * 1.15f;
  s.fillTriangle(cx, cy - h / 2, cx - b / 2, cy + h / 2, cx + b / 2, cy + h / 2, f);
  int t = max(1, g / 7);
  s.fillRect(cx - t / 2, cy - h / 6, max(2, t), h / 3, TFT_BLACK);
  s.fillRect(cx - t / 2, cy + h / 4, max(2, t), max(2, t), TFT_BLACK);
}


// ------------------------------------------------------------
// Text: geglaettete Schriften (Lato, src/schriften.h) mit echten Umlauten, ß und °
// ------------------------------------------------------------

static const uint8_t *const SCHRIFT_DATEN[] = {SCHRIFT_K, SCHRIFT_M, SCHRIFT_MF, SCHRIFT_G, SCHRIFT_GF, SCHRIFT_XG, SCHRIFT_RIESIG};
static const uint8_t *geladen = nullptr;

void schrift(TFT_eSprite &s, Schrift welche) {
  const uint8_t *daten = SCHRIFT_DATEN[welche];
  if (daten == geladen) return;  // Laden kostet etwas - nur wenn sich die Schrift aendert
  if (geladen) s.unloadFont();
  s.loadFont(daten);
  geladen = daten;
}

int breite(TFT_eSprite &s, const char *text) { return s.textWidth(text); }

static void textMitSchatten(TFT_eSprite &s, const char *text, int x, int y, uint16_t f) {
  s.setTextDatum(L_BASELINE);
  s.setTextColor(TFT_BLACK);  // nur Vordergrund: Kanten werden mit dem Hintergrund verrechnet
  s.drawString(text, x + 1, y + 1);
  s.setTextColor(f);
  s.drawString(text, x, y);
}

void schreibe(TFT_eSprite &s, const char *text, int x, int y, uint16_t f, Ausrichtung a) {
  int w = s.textWidth(text);
  if (a == MITTE) x -= w / 2;
  else if (a == RECHTS) x -= w;
  textMitSchatten(s, text, x, y, f);
}

void schreibeTemp(TFT_eSprite &s, float wert, int x, int y, uint16_t f, Ausrichtung a) {
  char z[12];
  snprintf(z, sizeof(z), "%d°", (int)lroundf(wert));
  schreibe(s, z, x, y, f, a);
}