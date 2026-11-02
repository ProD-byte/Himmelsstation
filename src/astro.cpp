#include "astro.h"
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

  // beleuchteter Teil des Mondes um Mitternacht (Neumond 6.1.2000 18:14 UTC, synodischer Monat)
  double p = fmod((n.mittag + 12 * 3600 - 947182440.0) / 86400.0 / 29.530588853, 1.0);
  n.mondLicht = (1 - cos(p * 2 * M_PI)) / 2;
  n.gueltig = true;
}
