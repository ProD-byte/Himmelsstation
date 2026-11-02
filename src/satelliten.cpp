#include "satelliten.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Sgp4.h>
#include <Preferences.h>

static const struct {
  const char *name;
  uint32_t norad;
} SAT[SATELLITEN] = {{"ISS", 25544}, {"Tiangong", 48274}, {"Hubble", 20580}};

const char *satName(int i) { return SAT[i].name; }

static SemaphoreHandle_t sperre = xSemaphoreCreateMutex();
static Ueberflug liste[SAT_LISTE];  // fuer die Anzeige (unter sperre), liste[0] mit Bahn
static int anzahl = 0;
static Sgp4 sgp;
static char tle[SATELLITEN][2][72];
static bool tleDa[SATELLITEN];
static uint32_t tleVersuch = 0;
static time_t gerechnetBis = 0;
static float ortLat = NAN, ortLon = NAN;

const uint32_t TLE_ABSTAND = 24UL * 3600 * 1000;  // CelesTrak bittet: nicht oefter als noetig
const uint32_t TLE_FEHLER = 60UL * 60 * 1000;

// Bahndaten eines Satelliten: drei Zeilen Text (Name, Zeile 1, Zeile 2)
static bool tleHolen(int i) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("Himmelsstation github.com/ProD-byte/Himmelsstation");
  if (!http.begin(client, "https://celestrak.org/NORAD/elements/gp.php?CATNR=" + String(SAT[i].norad) + "&FORMAT=TLE")) return false;
  int code = http.GET();
  String text = code == 200 ? http.getString() : "";
  http.end();
  int a = text.indexOf('\n'), b = text.indexOf('\n', a + 1);
  if (a < 0 || b < 0) {
    Serial.printf("Bahndaten %s: HTTP %d\n", SAT[i].name, code);
    return false;
  }
  String z1 = text.substring(a + 1, b), z2 = text.substring(b + 1);
  z1.trim();
  z2.trim();
  if (!z1.startsWith("1 ") || !z2.startsWith("2 ")) return false;
  strlcpy(tle[i][0], z1.c_str(), sizeof(tle[i][0]));
  strlcpy(tle[i][1], z2.c_str(), sizeof(tle[i][1]));
  return true;
}

// Bahndaten im Flash merken (einmal am Tag geschrieben): nach einem Neustart sind sie sofort da, auch wenn
// CelesTrak gerade nicht antwortet oder uns wegen zu vieler Abfragen sperrt. Bis 5 Tage alt sind sie gut genug.
static void tleSpeichern() {
  Preferences p;
  p.begin("tle", false);
  p.putBytes("daten", tle, sizeof(tle));
  p.putULong("zeit", (uint32_t)time(nullptr));
  p.end();
}

static void tleLaden() {
  Preferences p;
  p.begin("tle", true);
  uint32_t zeit = p.getULong("zeit", 0);
  if (zeit && time(nullptr) - (time_t)zeit < 5 * 86400 && p.getBytes("daten", tle, sizeof(tle)) == sizeof(tle))
    for (int i = 0; i < SATELLITEN; i++) tleDa[i] = tle[i][0][0] == '1';
  p.end();
  // gespeicherte Daten gelten als "heute geholt", wenn sie juenger als ein Tag sind - sonst gleich neu fragen
  if (zeit && time(nullptr) - (time_t)zeit < 86400) tleVersuch = millis() - (uint32_t)(time(nullptr) - zeit) * 1000;
  Serial.printf("Bahndaten aus dem Flash: %s\n", tleDa[0] ? "ja" : "nein");
}

static void satStarten(int i) {
  char z1[130], z2[130];
  strlcpy(z1, tle[i][0], sizeof(z1));
  strlcpy(z2, tle[i][1], sizeof(z2));
  sgp.site(ortLat, ortLon, 0);
  sgp.init(SAT[i].name, z1, z2);
}

// Einen Ueberflug abtasten: wo steht der Satellit, wann ist er zu sehen? mitBahn = Punkte speichern
static bool abtasten(Ueberflug &u, bool mitBahn) {
  float azAnfang = 0, azEnde = 0;
  u.punkte = 0;
  u.sichtAb = u.sichtBis = 0;
  u.maxHoehe = 0;
  int n = 0;
  for (time_t t = u.start; t <= u.ende && n < 120; t += SAT_SCHRITT, n++) {
    sgp.findsat((unsigned long)t);
    bool sichtbar = sgp.satVis >= 1000 && sgp.satEl > 0;
    if (mitBahn) {
      SatPunkt &q = u.bahn[u.punkte++];
      q.az10 = lroundf(sgp.satAz * 10);
      q.el10 = lroundf(max(0.0, sgp.satEl) * 10);
      q.sichtbar = sichtbar;
    }
    if (!sichtbar) continue;
    if (!u.sichtAb) u.sichtAb = t, azAnfang = sgp.satAz;
    u.sichtBis = t, azEnde = sgp.satAz;
    u.maxHoehe = max(u.maxHoehe, (float)sgp.satEl);
  }
  // kaum ueber dem Horizont oder nur ein paar Sekunden: lohnt nicht zum Rausgehen
  if (!u.sichtAb || u.maxHoehe < 10 || u.sichtBis - u.sichtAb < 60) return false;
  u.azAb = azAnfang;
  u.azBis = azEnde;
  float d = fmodf(azEnde - azAnfang + 540, 360) - 180;  // kuerzester Weg von Anfang zu Ende
  u.azMitte = fmodf(azAnfang + d / 2 + 360, 360);
  u.gueltig = true;
  return true;
}

// Fuer alle Satelliten die sichtbaren Ueberfluege der naechsten 2 Tage suchen, die fruehesten behalten
static void berechnen(time_t ab) {
  uint32_t t0 = millis();
  static Ueberflug neu[SAT_LISTE];  // nicht auf den Stapel (je ca. 800 Byte)
  int n = 0;
  for (int s = 0; s < SATELLITEN; s++) {
    if (!tleDa[s]) continue;
    satStarten(s);
    // 3 h frueher anfangen: die Suche springt zuerst eine ganze Erdumrundung (ca. 92 min) weiter - sonst
    // wuerde ein gerade laufender Ueberflug uebersprungen (vorbeigegangene werden unten aussortiert)
    sgp.initpredpoint((unsigned long)(ab - 3 * 3600), 0.0);
    passinfo p;
    for (int k = 0; k < 40 && sgp.nextpass(&p, 20); k++) {
      Ueberflug u;
      u.sat = s;
      u.start = (time_t)((p.jdstart - 2440587.5) * 86400);
      u.ende = (time_t)((p.jdstop - 2440587.5) * 86400);
      if (u.start > ab + 2 * 86400) break;
      if (u.ende < ab || u.ende - u.start > 20 * 60) continue;
      if (!abtasten(u, false)) continue;
      // nach Zeit einsortieren, nur die fruehesten SAT_LISTE behalten
      int pos = n;
      while (pos > 0 && neu[pos - 1].sichtAb > u.sichtAb) pos--;
      if (pos >= SAT_LISTE) break;
      for (int j = min(n, SAT_LISTE - 1); j > pos; j--) neu[j] = neu[j - 1];
      neu[pos] = u;
      n = min(n + 1, SAT_LISTE);
    }
  }
  // der naechste bekommt seine Bahn fuer die Anzeige
  if (n) {
    satStarten(neu[0].sat);
    abtasten(neu[0], true);
  }
  Serial.printf("Satelliten: %d Ueberfluege in %lu ms\n", n, millis() - t0);
  xSemaphoreTake(sperre, portMAX_DELAY);
  for (int i = 0; i < n; i++) liste[i] = neu[i];
  anzahl = n;
  xSemaphoreGive(sperre);
  // neu rechnen, wenn der naechste vorbei ist - sonst in 6 Stunden
  gerechnetBis = n ? min(neu[0].ende, ab + 6 * 3600) : ab + 6 * 3600;
}

void satellitenAktualisieren(float lat, float lon) {
  time_t jetzt = time(nullptr);
  if (jetzt < 1700000000) return;  // Uhr noch nicht gestellt
  static bool geladen = false;
  if (!geladen) geladen = true, tleLaden();
  bool neuerOrt = lat != ortLat || lon != ortLon;
  ortLat = lat, ortLon = lon;
  // Bahndaten taeglich erneuern; fehlt einer, stuendlich nur diesen nachholen (CelesTrak sperrt, wer zu oft fragt).
  // Klappt es nicht, reichen die alten noch ein paar Tage.
  uint32_t ms = millis();
  bool alle = true;
  for (bool d : tleDa) alle &= d;
  if (!tleVersuch || ms - tleVersuch > (alle ? TLE_ABSTAND : TLE_FEHLER)) {
    tleVersuch = ms;
    bool neu = false;
    for (int i = 0; i < SATELLITEN; i++)
      if ((alle || !tleDa[i]) && tleHolen(i)) tleDa[i] = neu = true;
    if (neu) gerechnetBis = 0, tleSpeichern();
  }
  bool irgendeiner = false;
  for (bool d : tleDa) irgendeiner |= d;
  if (!irgendeiner) return;
  if (neuerOrt || jetzt >= gerechnetBis) berechnen(jetzt);
}

int satellitenHolen(Ueberflug &naechster, Ueberflug *weitere, int max) {
  xSemaphoreTake(sperre, portMAX_DELAY);
  int n = anzahl;
  time_t jetzt = time(nullptr);
  int erster = 0;
  while (erster < n && liste[erster].ende < jetzt) erster++;  // schon vorbei
  if (erster < n) naechster = liste[erster];
  int k = 0;
  for (int i = erster + 1; i < n && k < max; i++) {
    weitere[k] = liste[i];
    weitere[k].punkte = 0;
    k++;
  }
  xSemaphoreGive(sperre);
  if (erster >= n) {
    naechster.gueltig = false;
    return 0;
  }
  if (erster > 0) naechster.punkte = 0;  // Bahn gehoert zum vorbeigegangenen - neu rechnen kommt gleich
  return 1 + k;
}
