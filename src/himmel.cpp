#include "himmel.h"
#include "ort.h"
#include "einstellungen.h"
#include "netzupdate.h"
#include "satelliten.h"
#include "weltraum.h"
#include "starts.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const uint32_t ABSTAND_OK     = 30 * 60 * 1000;  // Sonne/Wolken/Zeitzone alle 30 min
const uint32_t ABSTAND_UPDATE = 6 * 60 * 60 * 1000;
// nach Fehlern immer laengere Pausen (1, 2, 5, 10 min) plus ein paar zufaellige Sekunden
const uint32_t ABSTAND_FEHLER[] = {60 * 1000, 2 * 60 * 1000, 5 * 60 * 1000, 10 * 60 * 1000};
const float MONAT = 29.530589f;  // synodischer Monat in Tagen

static Himmel aktuell;
static SemaphoreHandle_t sperre;
static volatile HimmelStatus status = VERBINDE;
static volatile HimmelFehler fehler = KEIN_FEHLER;
static volatile bool neuLaden = false, ortAuto = false, versuchLaeuft = false;
static volatile uint32_t naechster = 0;

void himmelNeuLaden() { neuLaden = true; }
void himmelOrtAutomatisch() { ortAuto = true; }

float mondBeleuchtet(float phase) { return (1 - cosf(phase * 2 * PI)) / 2; }

float tageBisPhase(float phase, float ziel) {
  float d = ziel - phase;
  if (d <= 0) d += 1;
  return d * MONAT;
}

// HTTPS-Abruf und JSON lesen. Ergebnis: 200 = ok, sonst HTTP-Code, negativ = keine Verbindung, 0 = nicht lesbar
static int holen(const String &url, JsonDocument &doc) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("Himmelsstation github.com/ProD-byte/Himmelsstation");
  if (!http.begin(client, url)) return -1;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("Abruf %s: HTTP %d\n", url.substring(8, 40).c_str(), code);
    http.end();
    return code;
  }
  String text = http.getString();
  http.end();
  return deserializeJson(doc, text) ? 0 : 200;
}

int wolkenZwischen(const Himmel &h, time_t von, time_t bis) {
  if (!h.wolkenAb || bis <= von) return -1;
  int summe = 0, n = 0;
  for (int i = 0; i < 72; i++) {
    time_t t = h.wolkenAb + i * 3600;
    if (t + 3600 <= von || t >= bis || h.wolken[i] < 0) continue;
    summe += h.wolken[i], n++;
  }
  return n ? summe / n : -1;
}

// "2026-11-02T14:00" in Ortszeit -> Unix-Zeit
static time_t ortszeitZuUnix(const char *s, int32_t abw) {
  int j = atoi(s), m = atoi(s + 5), t = atoi(s + 8), h = atoi(s + 11);
  j -= m <= 2;  // Tage seit 1970 (Howard Hinnant, days_from_civil)
  int era = j / 400, yoe = j - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t - 1;
  long tage = era * 146097L + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  return (time_t)tage * 86400 + h * 3600 - abw;
}

// Sonne, Wolken und Zeitzone fuer den Ort (Open-Meteo, timezone=auto)
static int abrufen(Himmel &h, const Ort &ort) {
  JsonDocument doc;
  int code = holen("https://api.open-meteo.com/v1/forecast?latitude=" + String(ort.lat, 4) + "&longitude=" +
                       String(ort.lon, 4) + "&current=is_day,cloud_cover&daily=sunrise,sunset&hourly=cloud_cover" +
                       "&timezone=auto&past_days=1&forecast_days=2",
                   doc);
  if (code != 200) return code;
  h.tag = (doc["current"]["is_day"] | 1) == 1;
  h.wolkenJetzt = doc["current"]["cloud_cover"] | 0;
  const char *auf = doc["daily"]["sunrise"][1], *unter = doc["daily"]["sunset"][1];  // [0] = gestern
  if (auf) strlcpy(h.sonneAuf, auf + 11, sizeof(h.sonneAuf));
  if (unter) strlcpy(h.sonneUnter, unter + 11, sizeof(h.sonneUnter));
  h.utcAbweichung = doc["utc_offset_seconds"] | h.utcAbweichung;
  JsonArray zeiten = doc["hourly"]["time"], wolken = doc["hourly"]["cloud_cover"];
  const char *erste = zeiten[0];
  h.wolkenAb = erste ? ortszeitZuUnix(erste, h.utcAbweichung) : 0;
  for (int i = 0; i < 72; i++) h.wolken[i] = i < (int)wolken.size() && !wolken[i].isNull() ? (int8_t)(wolken[i] | 0) : -1;
  strlcpy(h.ort, ort.name, sizeof(h.ort));
  h.gueltig = true;
  h.stand = time(nullptr);
  return 200;
}

// Nacht neu rechnen (Sonne/Mond selbst, dauert nur Millisekunden)
static void nachtRechnen() {
  if (time(nullptr) < 1700000000 || !aktuell.gueltig) return;  // ohne Uhrzeit oder Zeitzone geht es nicht
  Ort o = ortAktuell();
  Nacht n;
  uint32_t t0 = millis();
  nachtBerechnen(n, time(nullptr), aktuell.utcAbweichung, o.lat, o.lon);
  static bool gemeldet = false;
  if (!gemeldet) Serial.printf("Nacht gerechnet in %lu ms\n", millis() - t0), gemeldet = true;
  xSemaphoreTake(sperre, portMAX_DELAY);
  aktuell.nacht = n;
  xSemaphoreGive(sperre);
}

static void aufgabe(void *) {
  uint32_t naechstesUpdate = 0, naechsteNacht = 0;
  bool ortGeprueft = false;
  int fehlversuche = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (!aktuell.gueltig) status = VERBINDE;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    // Kein Ort am Handy gewaehlt -> einmal pro Start grob ueber die Internet-Adresse bestimmen
    if (!ortGeprueft) {
      ortGeprueft = true;
      Ort o = ortAktuell();
      if (!o.vomNutzer) {
        if (!aktuell.gueltig) status = ORTUNG;
        if (ortPerIp(o)) ortSpeichern(o);
      }
    }
    if (ortAuto) {
      ortAuto = false;
      Ort o = ortAktuell();
      if (ortPerIp(o)) {
        ortSpeichern(o);
        neuLaden = true;
      }
    }
    if (neuLaden) {
      neuLaden = false;
      naechster = millis();
    }

    if ((int32_t)(millis() - naechster) >= 0) {
      if (!aktuell.gueltig && !fehlversuche) status = LADE;
      versuchLaeuft = true;
      Himmel neu;
      xSemaphoreTake(sperre, portMAX_DELAY);
      neu = aktuell;
      xSemaphoreGive(sperre);
      int code = abrufen(neu, ortAktuell());
      if (code == 200) {
        neu.nacht = aktuell.nacht;
        xSemaphoreTake(sperre, portMAX_DELAY);
        aktuell = neu;
        xSemaphoreGive(sperre);
        naechsteNacht = millis();  // Ort oder Zeitzone koennten neu sein
        status = FERTIG;
        fehler = KEIN_FEHLER;
        fehlversuche = 0;
        naechster = millis() + ABSTAND_OK;
      } else {
        IPAddress ip;
        if (code == 429 || code == 502 || code == 503 || code == 504) fehler = UEBERLASTET;
        else if (code < 0 && !WiFi.hostByName("api.open-meteo.com", ip)) fehler = KEIN_INTERNET;
        else fehler = NICHT_ERREICHBAR;
        if (!aktuell.gueltig) status = FEHLER;
        // Dienst weg, aber die Uhr stimmt: Mond und Daemmerung rechnet die Station selbst - also trotzdem
        // loslegen, nur ohne Wolken (Zeitzone bleibt die vom letzten Mal bzw. Mitteleuropa)
        if (!aktuell.gueltig && time(nullptr) > 1700000000 && fehlversuche >= 1) {
          xSemaphoreTake(sperre, portMAX_DELAY);
          strlcpy(aktuell.ort, ortAktuell().name, sizeof(aktuell.ort));
          aktuell.gueltig = true;
          xSemaphoreGive(sperre);
          status = FERTIG;
          naechsteNacht = millis();
        }
        naechster = millis() + ABSTAND_FEHLER[min(fehlversuche, 3)] + esp_random() % 15000;
        fehlversuche++;
      }
      versuchLaeuft = false;
    }

    if (aktuell.gueltig && (int32_t)(millis() - naechsteNacht) >= 0) {
      nachtRechnen();
      naechsteNacht = millis() + 10 * 60 * 1000;
    }

    // Satelliten-Ueberfluege (Bahndaten taeglich, gerechnet wird nur bei Bedarf)
    if (aktuell.gueltig) {
      Ort o = ortAktuell();
      satellitenAktualisieren(o.lat, o.lon);
    }

    // Weltraumwetter von NOAA (alle 15 min)
    if (aktuell.gueltig) weltraumAktualisieren();
    // Raketenstarts (alle 30 min, kurz vor einem Start alle 10 min)
    if (aktuell.gueltig) startsAktualisieren();

    // Neue Firmware auf GitHub? (erst wenn alles laeuft, dann alle 6 Stunden oder auf Wunsch)
    if (aktuell.gueltig && ((int32_t)(millis() - naechstesUpdate) >= 0 || updateSucheGewuenscht())) {
      updatePruefen();
      naechstesUpdate = millis() + ABSTAND_UPDATE;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void himmelStart() {
  sperre = xSemaphoreCreateMutex();
  // Kern 0, damit Kern 1 ungestoert zeichnen kann
  xTaskCreatePinnedToCore(aufgabe, "himmel", 20480, nullptr, 1, nullptr, 0);
}

bool himmelHolen(Himmel &h) {
  xSemaphoreTake(sperre, portMAX_DELAY);
  h = aktuell;
  xSemaphoreGive(sperre);
  return h.gueltig;
}

HimmelStatus himmelStatus() { return status; }
HimmelFehler himmelFehler() { return fehler; }

int himmelNaechsterVersuch() {
  if (versuchLaeuft) return 0;
  int32_t rest = (int32_t)(naechster - millis());
  return rest > 0 ? (rest + 999) / 1000 : 0;
}
