#include "starts.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static SemaphoreHandle_t sperre = xSemaphoreCreateMutex();
static Starts aktuell;
static uint32_t naechster = 0;

// "2026-11-04T01:28:00Z" -> Unix-Zeit
static time_t utc(const char *s) {
  if (!s || strlen(s) < 16) return 0;
  int j = atoi(s), m = atoi(s + 5), t = atoi(s + 8), h = atoi(s + 11), mi = atoi(s + 14);
  j -= m <= 2;  // Tage seit 1970 (Howard Hinnant, days_from_civil)
  int era = j / 400, yoe = j - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t - 1;
  long tage = era * 146097L + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  return (time_t)tage * 86400 + h * 3600 + mi * 60;
}

static bool abrufen(Starts &st) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent("Himmelsstation github.com/ProD-byte/Himmelsstation");
  // die letzte Stunde mitnehmen, damit ein gerade gestarteter noch "im Flug"/"erfolgreich" zeigt
  if (!http.begin(client, "https://ll.thespacedevs.com/2.3.0/launches/upcoming/?limit=5&mode=normal")) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("Raketenstarts: HTTP %d\n", code);
    http.end();
    return false;
  }
  String text = http.getString();  // ca. 40 KB, liegt im PSRAM
  http.end();
  JsonDocument filter, doc;
  JsonObject f = filter["results"][0].to<JsonObject>();
  f["net"] = true;
  f["status"]["abbrev"] = true;
  f["name"] = true;
  f["launch_service_provider"]["name"] = true;
  f["pad"]["location"]["name"] = true;
  f["pad"]["country"]["alpha_2_code"] = true;
  f["mission"]["name"] = true;
  f["mission"]["orbit"]["abbrev"] = true;
  f["rocket"]["configuration"]["name"] = true;
  if (deserializeJson(doc, text, DeserializationOption::Filter(filter))) return false;
  int n = 0;
  for (JsonObject r : doc["results"].as<JsonArray>()) {
    if (n >= STARTS) break;
    Start &s = st.s[n];
    s = Start();
    s.net = utc(r["net"]);
    strlcpy(s.status, r["status"]["abbrev"] | "", sizeof(s.status));
    strlcpy(s.rakete, r["rocket"]["configuration"]["name"] | "", sizeof(s.rakete));
    // Mission: eigenes Feld, sonst der Teil hinter "Rakete | " im Namen
    const char *mi = r["mission"]["name"] | "";
    if (!*mi) {
      const char *name = r["name"] | "";
      const char *strich = strstr(name, "| ");
      mi = strich ? strich + 2 : name;
    }
    strlcpy(s.mission, mi, sizeof(s.mission));
    strlcpy(s.betreiber, r["launch_service_provider"]["name"] | "", sizeof(s.betreiber));
    strlcpy(s.land, r["pad"]["country"]["alpha_2_code"] | "", sizeof(s.land));
    // Startplatz kurz: "Jiuquan Satellite Launch Center, People's Republic of China" -> "Jiuquan Satellite Launch Center · CN"
    String ort = r["pad"]["location"]["name"] | "";
    int komma = ort.indexOf(',');
    if (komma > 0) ort = ort.substring(0, komma);
    if (*s.land) ort += String(" · ") + s.land;
    strlcpy(s.ort, ort.c_str(), sizeof(s.ort));
    strlcpy(s.bahn, r["mission"]["orbit"]["abbrev"] | "", sizeof(s.bahn));
    n++;
  }
  st.n = n;
  st.stand = time(nullptr);
  st.gueltig = n > 0;
  return st.gueltig;
}

void startsAktualisieren() {
  if (time(nullptr) < 1700000000 || (int32_t)(millis() - naechster) < 0) return;
  static Starts neu;  // nicht auf den Stapel
  bool ok = abrufen(neu);
  if (ok) {
    xSemaphoreTake(sperre, portMAX_DELAY);
    aktuell = neu;
    xSemaphoreGive(sperre);
  }
  // sparsam fragen: alle 30 min, steht ein Start in der naechsten Stunde an alle 10 min
  bool bald = false;
  for (int i = 0; ok && i < neu.n; i++) {
    long d = neu.s[i].net - time(nullptr);
    if (d > -1800 && d < 3600) bald = true;
  }
  naechster = millis() + (bald ? 10 : 30) * 60 * 1000UL;
}

bool startsHolen(Starts &s) {
  xSemaphoreTake(sperre, portMAX_DELAY);
  s = aktuell;
  xSemaphoreGive(sperre);
  return s.gueltig;
}
