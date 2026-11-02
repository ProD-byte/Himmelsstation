#include "weltraum.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static SemaphoreHandle_t sperre = xSemaphoreCreateMutex();
static Weltraum aktuell;
static uint32_t naechster = 0;
const uint32_t ABSTAND = 15 * 60 * 1000, ABSTAND_FEHLER = 5 * 60 * 1000;
static const char *NOAA = "https://services.swpc.noaa.gov/";

static int holen(const String &pfad, JsonDocument &doc, JsonDocument *filter = nullptr) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("Himmelsstation github.com/ProD-byte/Himmelsstation");
  if (!http.begin(client, String(NOAA) + pfad)) return -1;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("NOAA %s: HTTP %d\n", pfad.c_str(), code);
    http.end();
    return code;
  }
  String text = http.getString();
  http.end();
  DeserializationError f = filter ? deserializeJson(doc, text, DeserializationOption::Filter(*filter)) : deserializeJson(doc, text);
  return f ? 0 : 200;
}

// "2026-11-02T15:00:00" (UTC, auch mit "Z") -> Unix-Zeit
static time_t utc(const char *s) {
  if (!s || strlen(s) < 16) return 0;
  int j = atoi(s), m = atoi(s + 5), t = atoi(s + 8), h = atoi(s + 11), mi = atoi(s + 14);
  j -= m <= 2;  // Tage seit 1970 (Howard Hinnant, days_from_civil)
  int era = j / 400, yoe = j - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t - 1;
  long tage = era * 146097L + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  return (time_t)tage * 86400 + h * 3600 + mi * 60;
}

static bool abrufen(Weltraum &w) {
  // Kp: gemessen, geschaetzt und Vorhersage in einer Liste
  JsonDocument doc;
  if (holen("products/noaa-planetary-k-index-forecast.json", doc) != 200) return false;
  JsonArray a = doc.as<JsonArray>();
  int n = 0, start = max(0, (int)a.size() - KP_WERTE);
  for (int i = start; i < (int)a.size(); i++) {
    JsonObject o = a[i];
    time_t t = utc(o["time_tag"]);
    if (!n) w.kpAb = t;
    w.kp[n] = o["kp"] | 0.0f;
    String art = o["observed"] | "";
    w.kpArt[n] = art == "observed" ? 0 : art == "estimated" ? 1 : 2;
    n++;
  }
  w.kpN = n;

  // Sonnenwind (km/s)
  JsonDocument wind;
  if (holen("products/summary/solar-wind-speed.json", wind) == 200) w.wind = wind[0]["proton_speed"] | 0;

  // Eruptionen der letzten 7 Tage: die staerkste (nach Roentgenfluss), und die der letzten 24 h
  JsonDocument filter, flares;
  filter[0]["max_time"] = true;
  filter[0]["max_class"] = true;
  filter[0]["max_xrlong"] = true;
  if (holen("json/goes/primary/xray-flares-7-day.json", flares, &filter) == 200) {
    double max7 = 0, max24 = 0;
    time_t jetzt = time(nullptr);
    w.flare7[0] = w.flare24[0] = 0;
    for (JsonObject f : flares.as<JsonArray>()) {
      double fluss = f["max_xrlong"] | 0.0;
      time_t t = utc(f["max_time"]);
      if (fluss > max7) max7 = fluss, w.flare7Zeit = t, strlcpy(w.flare7, f["max_class"] | "", sizeof(w.flare7));
      if (jetzt - t < 86400 && fluss > max24) max24 = fluss, strlcpy(w.flare24, f["max_class"] | "", sizeof(w.flare24));
    }
  }
  w.stand = time(nullptr);
  w.gueltig = true;
  return true;
}

void weltraumAktualisieren() {
  if (time(nullptr) < 1700000000 || (int32_t)(millis() - naechster) < 0) return;
  static Weltraum neu;  // nicht auf den Stapel
  xSemaphoreTake(sperre, portMAX_DELAY);
  neu = aktuell;
  xSemaphoreGive(sperre);
  bool ok = abrufen(neu);
  if (ok) {
    xSemaphoreTake(sperre, portMAX_DELAY);
    aktuell = neu;
    xSemaphoreGive(sperre);
  }
  naechster = millis() + (ok ? ABSTAND : ABSTAND_FEHLER);
}

bool weltraumHolen(Weltraum &w) {
  xSemaphoreTake(sperre, portMAX_DELAY);
  w = aktuell;
  xSemaphoreGive(sperre);
  return w.gueltig;
}

float kpZu(const Weltraum &w, time_t t) {
  int i = (t - w.kpAb) / (3 * 3600);
  return w.gueltig && t >= w.kpAb && i < w.kpN ? w.kp[i] : -1;
}

float kpMaxZwischen(const Weltraum &w, time_t von, time_t bis) {
  float m = -1;
  for (int i = 0; i < w.kpN; i++) {
    time_t t = w.kpAb + i * 3 * 3600;
    if (t + 3 * 3600 > von && t < bis) m = max(m, w.kp[i]);
  }
  return m;
}

// Geomagnetischer Nordpol (Dipol, Stand 2025): 80,8 Grad N, 72,7 Grad W
float geomagBreite(float lat, float lon) {
  const float R = PI / 180, pLat = 80.8f * R, pLon = -72.7f * R;
  return asinf(sinf(lat * R) * sinf(pLat) + cosf(lat * R) * cosf(pLat) * cosf(lon * R - pLon)) / R;
}

// Polarlicht reicht bei Kp etwa bis zu dieser geomagnetischen Breite herunter (am Nordhorizont zu sehen):
// Kp 5 -> 56 Grad, Kp 7 -> 51 Grad, Kp 9 -> 45 Grad. Umgestellt: welcher Kp reicht fuer den Ort?
float kpFuerPolarlicht(float lat, float lon) {
  float g = fabsf(geomagBreite(lat, lon));
  return constrain((69.2f - g) / 2.65f, 0.0f, 9.0f);
}
