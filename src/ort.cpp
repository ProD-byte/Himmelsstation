#include "ort.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static Ort ort;
static SemaphoreHandle_t sperre = xSemaphoreCreateMutex();

void ortLaden() {
  Preferences prefs;
  prefs.begin("ort", true);
  Ort o;
  if (prefs.isKey("lat")) {
    o.lat = prefs.getFloat("lat", o.lat);
    o.lon = prefs.getFloat("lon", o.lon);
    prefs.getString("name", o.name, sizeof(o.name));
    o.vomNutzer = prefs.getBool("nutzer", false);
  }
  prefs.end();
  xSemaphoreTake(sperre, portMAX_DELAY);
  ort = o;
  xSemaphoreGive(sperre);
  Serial.printf("Ort: %s (%.3f, %.3f)%s\n", o.name, o.lat, o.lon, o.vomNutzer ? " - gewaehlt" : "");
}

Ort ortAktuell() {
  xSemaphoreTake(sperre, portMAX_DELAY);
  Ort o = ort;
  xSemaphoreGive(sperre);
  return o;
}

void ortSpeichern(const Ort &o) {
  Preferences prefs;
  prefs.begin("ort", false);
  prefs.putFloat("lat", o.lat);
  prefs.putFloat("lon", o.lon);
  prefs.putString("name", o.name);
  prefs.putBool("nutzer", o.vomNutzer);
  prefs.end();
  xSemaphoreTake(sperre, portMAX_DELAY);
  ort = o;
  xSemaphoreGive(sperre);
  Serial.printf("Ort gespeichert: %s (%.3f, %.3f)\n", o.name, o.lat, o.lon);
}

// ------------------------------------------------------------
// Internet-Abfragen
// ------------------------------------------------------------

static bool holen(const String &url, JsonDocument &doc) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  http.setUserAgent("Himmelsstation");
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = code == 200 && !deserializeJson(doc, http.getString());
  if (!ok) Serial.printf("Ort: %s -> HTTP %d\n", url.c_str(), code);
  http.end();
  return ok;
}

static String urlText(const String &s) {
  String aus;
  const char *hex = "0123456789ABCDEF";
  for (uint8_t c : s) {
    if (isalnum(c) || c == '-' || c == '.' || c == '_') {
      aus += (char)c;
    } else {
      aus += '%';
      aus += hex[c >> 4];
      aus += hex[c & 15];
    }
  }
  return aus;
}

static void nameSetzen(Ort &o, const char *name) {
  strlcpy(o.name, name && *name ? name : "Unbekannt", sizeof(o.name));
}

bool ortPerIp(Ort &o) {
  JsonDocument doc;
  if (holen("https://ipapi.co/json/", doc) && doc["latitude"].is<float>()) {
    o.lat = doc["latitude"];
    o.lon = doc["longitude"];
    nameSetzen(o, doc["city"].as<const char *>());
  } else if (holen("https://ipwho.is/?fields=success,city,latitude,longitude", doc) && doc["success"] == true) {
    o.lat = doc["latitude"];
    o.lon = doc["longitude"];
    nameSetzen(o, doc["city"].as<const char *>());
  } else {
    return false;
  }
  o.vomNutzer = false;
  Serial.printf("Ort per IP: %s (%.3f, %.3f)\n", o.name, o.lat, o.lon);
  return true;
}

static String jsonText(const char *s) {
  String aus;
  for (const char *p = s ? s : ""; *p; p++) {
    if (*p == '"' || *p == '\\') aus += '\\';
    if ((uint8_t)*p >= 0x20) aus += *p;
  }
  return aus;
}

String ortSuchen(const String &suche) {
  String name = suche;
  name.trim();

  // Deutsche Postleitzahl -> erst den Ortsnamen holen (Open-Meteo kennt keine deutschen PLZ)
  bool plz = name.length() == 5;
  for (char c : name) plz &= isdigit(c);
  if (plz) {
    JsonDocument doc;
    if (!holen("https://api.zippopotam.us/de/" + name, doc)) return "[]";
    const char *n = doc["places"][0]["place name"];
    if (!n) return "[]";
    name = n;
  }

  JsonDocument doc;
  if (!holen("https://geocoding-api.open-meteo.com/v1/search?count=8&language=de&format=json" +
                 String(plz ? "&countryCode=DE" : "") + "&name=" + urlText(name),
             doc)) {
    return "[]";
  }

  String json = "[";
  for (JsonObject r : doc["results"].as<JsonArray>()) {
    // Zusatz zum Unterscheiden gleichnamiger Orte: Bundesland, Land
    String info = jsonText(r["admin1"] | "");
    const char *land = r["country"] | "";
    if (*land) info += String(info.length() ? ", " : "") + jsonText(land);
    if (json.length() > 1) json += ",";
    json += "{\"name\":\"" + jsonText(r["name"].as<const char *>()) + "\",\"info\":\"" + info + "\",\"lat\":" +
            String(r["latitude"].as<float>(), 4) + ",\"lon\":" + String(r["longitude"].as<float>(), 4) + "}";
  }
  return json + "]";
}
