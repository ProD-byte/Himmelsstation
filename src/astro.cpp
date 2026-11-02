#include "astro.h"
#include "einstellungen.h"
#include <math.h>

static const double R = M_PI / 180;

// Tage seit 1.1.2000 12:00 UTC
static double tage(time_t t) { return t / 86400.0 - 10957.5; }

static double hoehe(double ra, double dec, double d, double lat, double lon) {
  double lst = (280.46061837 + 360.98564736629 * d + lon) * R;  // Sternzeit am Ort
  double h = lst - ra;
  return asin(sin(lat * R) * sin(dec) + cos(lat * R) * cos(dec) * cos(h)) / R;
}

double sonneHoehe(time_t t, double lat, double lon) {
  double d = tage(t);
  double g = (357.529 + 0.98560028 * d) * R;
  double q = 280.459 + 0.98564736 * d;
  double l = (q + 1.915 * sin(g) + 0.020 * sin(2 * g)) * R;
  double e = (23.439 - 0.00000036 * d) * R;
  return hoehe(atan2(cos(e) * sin(l), cos(l)), asin(sin(e) * sin(l)), d, lat, lon);
}

double mondHoehe(time_t t, double lat, double lon) {
  double d = tage(t), T = d / 36525;
  auto s = [&](double a, double b) { return sin((a + b * T) * R); };
  auto c = [&](double a, double b) { return cos((a + b * T) * R); };
  double lam = (218.32 + 481267.881 * T + 6.29 * s(134.9, 477198.85) - 1.27 * s(259.2, -413335.38) +
                0.66 * s(235.7, 890534.23) + 0.21 * s(269.9, 954397.70) - 0.19 * s(357.5, 35999.05) -
                0.11 * s(186.6, 966404.05)) * R;
  double bet = (5.13 * s(93.3, 483202.03) + 0.28 * s(228.2, 960400.87) - 0.28 * s(318.3, 6003.18) -
                0.17 * s(217.6, -407332.20)) * R;
  double hp = 0.9508 + 0.0518 * c(134.9, 477198.85) + 0.0095 * c(259.2, -413335.38) +
              0.0078 * c(235.7, 890534.23) + 0.0028 * c(269.9, 954397.70);  // Parallaxe
  double e = 23.439 * R;
  double x = cos(bet) * cos(lam);
  double y = cos(e) * cos(bet) * sin(lam) - sin(e) * sin(bet);
  double z = sin(e) * cos(bet) * sin(lam) + cos(e) * sin(bet);
  double h = hoehe(atan2(y, x), asin(z), d, lat, lon);
  return h - hp * cos(h * R);  // vom Erdmittelpunkt auf die Erdoberflaeche
}

// ---------- Planeten ----------
// a, e, I, L, Laenge des Perihels, Knoten - Wert und Aenderung pro Jahrhundert (Standish, JPL)
static const double BAHN[6][6][2] = {
    {{0.38709927, 0.00000037}, {0.20563593, 0.00001906}, {7.00497902, -0.00594749}, {252.25032350, 149472.67411175}, {77.45779628, 0.16047689}, {48.33076593, -0.12534081}},
    {{0.72333566, 0.00000390}, {0.00677672, -0.00004107}, {3.39467605, -0.00078890}, {181.97909950, 58517.81538729}, {131.60246718, 0.00268329}, {76.67984255, -0.27769418}},
    {{1.52371034, 0.00001847}, {0.09339410, 0.00007882}, {1.84969142, -0.00813131}, {-4.55343205, 19140.30268499}, {-23.94362959, 0.44441088}, {49.55953891, -0.29257343}},
    {{5.20288700, -0.00011607}, {0.04838624, -0.00013253}, {1.30439695, -0.00183714}, {34.39644051, 3034.74612775}, {14.72847983, 0.21252668}, {100.47390909, 0.20469106}},
    {{9.53667594, -0.00125060}, {0.05386179, -0.00050991}, {2.48599187, 0.00193609}, {49.95424423, 1222.49362201}, {92.59887831, -0.41897216}, {113.66242448, -0.28867794}},
    {{1.00000261, 0.00000562}, {0.01671123, -0.00004392}, {-0.00001531, -0.01294668}, {100.46457166, 35999.37244981}, {102.93768193, 0.32327364}, {0.0, 0.0}},  // Erde
};

const char *planetName(int i) {
  static const char *DE[] = {"Merkur", "Venus", "Mars", "Jupiter", "Saturn"};
  static const char *EN[] = {"Mercury", "Venus", "Mars", "Jupiter", "Saturn"};
  return TX(DE[i], EN[i]);
}

// Ort um die Sonne (AE, Ekliptik J2000)
static void helio(int k, double T, double &x, double &y, double &z) {
  double el[6];
  for (int j = 0; j < 6; j++) el[j] = BAHN[k][j][0] + BAHN[k][j][1] * T;
  double a = el[0], e = el[1], I = el[2] * R, w = (el[4] - el[5]) * R, O = el[5] * R;
  double M = fmod(el[3] - el[4], 360) * R;
  double E = M + e * sin(M);
  for (int n = 0; n < 6; n++) E -= (E - e * sin(E) - M) / (1 - e * cos(E));  // Kepler-Gleichung
  double xp = a * (cos(E) - e), yp = a * sqrt(1 - e * e) * sin(E);
  x = (cos(w) * cos(O) - sin(w) * sin(O) * cos(I)) * xp + (-sin(w) * cos(O) - cos(w) * sin(O) * cos(I)) * yp;
  y = (cos(w) * sin(O) + sin(w) * cos(O) * cos(I)) * xp + (-sin(w) * sin(O) + cos(w) * cos(O) * cos(I)) * yp;
  z = sin(w) * sin(I) * xp + cos(w) * sin(I) * yp;
}

void planetOrt(int i, time_t t, double lat, double lon, float &az, float &h, float &mag) {
  double d = tage(t), T = d / 36525;
  double px, py, pz, ex, ey, ez;
  helio(i, T, px, py, pz);
  helio(5, T, ex, ey, ez);
  double gx = px - ex, gy = py - ey, gz = pz - ez;
  // Praezession seit 2000 (Fruehlingspunkt wandert 1,397 Grad pro Jahrhundert), dann auf den Aequator drehen
  double p = 1.39697 * T * R, gx2 = gx * cos(p) - gy * sin(p), gy2 = gx * sin(p) + gy * cos(p);
  double eps = 23.43928 * R;
  double xq = gx2, yq = gy2 * cos(eps) - gz * sin(eps), zq = gy2 * sin(eps) + gz * cos(eps);
  double ra = atan2(yq, xq), dec = atan2(zq, sqrt(xq * xq + yq * yq));
  double H = (280.46061837 + 360.98564736629 * d + lon) * R - ra;
  h = hoehe(ra, dec, d, lat, lon);
  az = fmod(atan2(sin(H), cos(H) * sin(lat * R) - tan(dec) * cos(lat * R)) / R + 180 + 360, 360);
  // Helligkeit (Mallama): Abstand zur Sonne r, zur Erde g, Phasenwinkel i
  double r = sqrt(px * px + py * py + pz * pz), g = sqrt(gx * gx + gy * gy + gz * gz), re = sqrt(ex * ex + ey * ey + ez * ez);
  double w = acos(constrain((r * r + g * g - re * re) / (2 * r * g), -1.0, 1.0)) / R;
  double l = 5 * log10(r * g);
  switch (i) {
    case 0: mag = -0.613 + w * (6.3280e-2 + w * (-1.6336e-3 + w * (3.3644e-5 + w * (-3.4265e-7 + w * (1.6893e-9 - w * 3.0334e-12))))) + l; break;
    case 1: mag = -4.384 + w * (-1.044e-3 + w * (3.687e-4 + w * (-2.814e-6 + w * 8.938e-9))) + l; break;
    case 2: mag = -1.601 + w * (2.267e-2 - w * 1.302e-4) + l; break;
    case 3: mag = -9.395 + w * (-3.7e-4 + w * 6.16e-4) + l; break;
    default: mag = -8.88 + 0.044 * w + l; break;  // Saturn ohne Ringe (auf ca. 0,5 mag)
  }
}

// Wann geht f zwischen t0 und t0 + schritt durch 'schwelle'? (halbieren bis auf ein paar Sekunden)
static time_t genau(double (*f)(time_t, double, double), double schwelle, time_t t0, int schritt, double lat, double lon) {
  time_t lo = t0, hi = t0 + schritt;
  bool unten = f(lo, lat, lon) < schwelle;
  while (hi - lo > 5) {
    time_t m = (lo + hi) / 2;
    if ((f(m, lat, lon) < schwelle) == unten) lo = m;
    else hi = m;
  }
  return (lo + hi) / 2;
}

void nachtBerechnen(Nacht &n, time_t jetzt, int32_t abw, double lat, double lon) {
  // vor 8 Uhr morgens zaehlt noch die laufende Nacht (Mittag gestern bis Mittag heute)
  long tag = (long)floor((jetzt + abw - 8 * 3600) / 86400.0);
  n = Nacht();
  n.mittag = (time_t)tag * 86400 + 12 * 3600 - abw;

  // Hoehen alle 10 min; dabei die Uebergaenge finden (Sonnenrand mit Lichtbrechung: -0,833 Grad)
  const double SONNE[4] = {-0.833, -6, -12, -18};
  time_t *runter[4] = {&n.unter, &n.buergEnde, &n.nautEnde, &n.astroEnde};
  time_t *hoch[4] = {&n.auf, &n.buergAnf, &n.nautAnf, &n.astroAnf};
  double s0 = 0, m0 = 0;
  for (int i = 0; i < NACHT_SCHRITTE; i++) {
    time_t t = n.mittag + i * 600;
    double s = sonneHoehe(t, lat, lon), m = mondHoehe(t, lat, lon);
    n.sonne[i] = (int8_t)constrain(lround(s), -90L, 90L);
    n.mond[i] = (int8_t)constrain(lround(m), -90L, 90L);
    if (i) {
      for (int k = 0; k < 4; k++) {
        if (s0 >= SONNE[k] && s < SONNE[k] && !*runter[k]) *runter[k] = genau(sonneHoehe, SONNE[k], t - 600, 600, lat, lon);
        if (s0 < SONNE[k] && s >= SONNE[k] && !*hoch[k]) *hoch[k] = genau(sonneHoehe, SONNE[k], t - 600, 600, lat, lon);
      }
      if (m0 < -0.833 && m >= -0.833 && !n.mondAuf) n.mondAuf = genau(mondHoehe, -0.833, t - 600, 600, lat, lon);
      if (m0 >= -0.833 && m < -0.833 && !n.mondUnter) n.mondUnter = genau(mondHoehe, -0.833, t - 600, 600, lat, lon);
    }
    s0 = s, m0 = m;
  }

  // dunkelste Zeit der Nacht: astronomisch, sonst nautisch (z. B. Juni in Deutschland), sonst buergerlich
  if (n.astroEnde && n.astroAnf > n.astroEnde) n.dunkelVon = n.astroEnde, n.dunkelBis = n.astroAnf, n.dunkelArt = 3;
  else if (n.nautEnde && n.nautAnf > n.nautEnde) n.dunkelVon = n.nautEnde, n.dunkelBis = n.nautAnf, n.dunkelArt = 2;
  else if (n.buergEnde && n.buergAnf > n.buergEnde) n.dunkelVon = n.buergEnde, n.dunkelBis = n.buergAnf, n.dunkelArt = 1;
  // Polarnacht: die Sonne geht gar nicht auf - dann ist die ganze Zeit dunkel
  else if (!n.unter && !n.auf && n.sonne[0] < -18) n.dunkelVon = n.mittag, n.dunkelBis = n.mittag + 86400, n.dunkelArt = 3;

  if (n.dunkelArt) {
    n.dunkelMin = (n.dunkelBis - n.dunkelVon) / 60;
    for (time_t t = n.dunkelVon; t < n.dunkelBis; t += 300)  // alle 5 min: steht der Mond am Himmel?
      if (mondHoehe(t, lat, lon) < -0.833) n.ohneMondMin += 5;
    n.ohneMondMin = min(n.ohneMondMin, n.dunkelMin);
  }

  // Planeten: wann sind sie zu sehen (mind. 8 Grad hoch, Sonne unter -6 Grad)?
  for (int k = 0; k < PLANETEN; k++) {
    Planet &pl = n.planet[k];
    float besteH = -90;
    for (int i = 0; i < NACHT_SCHRITTE; i++) {
      if (n.sonne[i] >= -6) continue;
      time_t t = n.mittag + i * 600;
      float az, h, mag;
      planetOrt(k, t, lat, lon, az, h, mag);
      if (h < 8) continue;
      if (!pl.sichtbar) pl.ab = t, pl.sichtbar = true;
      pl.bis = t;
      if (h > besteH) besteH = h, pl.beste = t, pl.hoehe = h, pl.az = az, pl.mag = mag;
    }
  }

  // beleuchteter Teil des Mondes um Mitternacht (Neumond 6.1.2000 18:14 UTC, synodischer Monat)
  double p = fmod((n.mittag + 12 * 3600 - 947182440.0) / 86400.0 / 29.530588853, 1.0);
  n.mondLicht = (1 - cos(p * 2 * M_PI)) / 2;
  n.gueltig = true;
}
