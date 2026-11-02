#include "web.h"
#include "web_html.h"
#include "einstellungen.h"
#include "ort.h"
#include "netzupdate.h"
#include "himmel.h"
#include "wlan.h"
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>

static WebServer server(80);

extern TFT_eSprite bild;  // Bildspeicher des Displays (main.cpp)
extern int B, H;

// Bildschirmfoto: der Bildspeicher roh (RGB565, B x H, Zeile fuer Zeile) - zum Pruefen und fuer Videos
static void bildSenden() {
  const uint8_t *p = (const uint8_t *)bild.getPointer();
  if (!p) return server.send(503, "text/plain", "kein Bild");
  size_t n = (size_t)B * H * 2;
  server.sendHeader("X-Breite", String(B));
  server.sendHeader("X-Hoehe", String(H));
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(n);
  server.send(200, "application/octet-stream", "");
  WiFiClient c = server.client();
  for (size_t ab = 0; ab < n;) {
    size_t k = c.write(p + ab, min((size_t)4096, n - ab));
    if (!k) break;
    ab += k;
  }
}

static String jsonText(const String &s) {
  String aus;
  for (char c : s) {
    if (c == '"' || c == '\\') aus += '\\';
    if ((uint8_t)c >= 0x20) aus += c;
  }
  return aus;
}

static void seite() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", WEB_HTML);
}

static void status() {
  Ort o = ortAktuell();
  String j = "{\"ort\":\"" + jsonText(o.name) + "\",\"gewaehlt\":" + (o.vomNutzer ? "true" : "false") +
             ",\"build\":" + FIRMWARE_BUILD + ",\"neu\":" + updateNeuerBuild() + ",\"ip\":\"" +
             WiFi.localIP().toString() + "\",\"wlan\":\"" + jsonText(WiFi.SSID()) + "\",\"rssi\":" + WiFi.RSSI() +
             ",\"einst\":{\"licht\":" + einst.licht + ",\"seiten\":" + einst.seiten + ",\"wechsel\":" + einst.wechsel +
             ",\"nacht\":" + (einst.nacht ? "true" : "false") + ",\"von\":" + einst.nachtVon + ",\"bis\":" +
             einst.nachtBis + ",\"auto\":" + (einst.autoUpdate ? "true" : "false") + ",\"sprache\":" + einst.sprache +
             ",\"lage\":" + einst.lage + ",\"name\":\"" + einst.name + "\"}}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static void einstellungenSetzen() {
  auto zahl = [](const char *n, int alt) { return server.hasArg(n) ? server.arg(n).toInt() : alt; };
  einst.licht = constrain(zahl("licht", einst.licht), 0, 3);
  einst.seiten = zahl("seiten", einst.seiten) & 0xFF;
  if (!einst.seiten) einst.seiten = 1;
  einst.wechsel = constrain(zahl("wechsel", einst.wechsel), 0, 120);
  // Kontrollkaestchen kommen als "true"/"false" oder 1/0
  auto janein = [](const char *n, bool alt) {
    if (!server.hasArg(n)) return alt;
    String v = server.arg(n);
    return v == "1" || v == "true";
  };
  einst.nacht = janein("nacht", einst.nacht);
  einst.nachtVon = constrain(zahl("von", einst.nachtVon), 0, 23);
  einst.nachtBis = constrain(zahl("bis", einst.nachtBis), 0, 23);
  einst.autoUpdate = janein("auto", einst.autoUpdate);
  einst.sprache = constrain(zahl("sprache", einst.sprache), 0, 1);
  einst.lage = constrain(zahl("lage", einst.lage), 0, 3);
  einstellungenSpeichern();
  server.send(200, "text/plain", "ok");
}

// Ortssuche im Hintergrund (eigene kleine Aufgabe), damit die Animation nicht stehen bleibt:
// die Seite fragt so lange nach, bis das Ergebnis da ist (202 = suche noch).
static SemaphoreHandle_t sucheSperre = xSemaphoreCreateMutex();
static String sucheFrage, sucheFertig, sucheErgebnis;
static volatile bool sucheLaeuft = false;

static void sucheAufgabe(void *) {
  xSemaphoreTake(sucheSperre, portMAX_DELAY);
  String q = sucheFrage;
  xSemaphoreGive(sucheSperre);
  String e = ortSuchen(q);
  xSemaphoreTake(sucheSperre, portMAX_DELAY);
  sucheFertig = q, sucheErgebnis = e;
  xSemaphoreGive(sucheSperre);
  sucheLaeuft = false;
  vTaskDelete(nullptr);
}

static void orteSuchen() {
  String q = server.arg("q");
  server.sendHeader("Cache-Control", "no-store");
  xSemaphoreTake(sucheSperre, portMAX_DELAY);
  bool da = !sucheLaeuft && sucheFertig == q;
  String e = da ? sucheErgebnis : "";
  if (!da && !sucheLaeuft) {
    sucheFrage = q;
    sucheLaeuft = true;
    if (xTaskCreatePinnedToCore(sucheAufgabe, "suche", 12288, nullptr, 1, nullptr, 0) != pdPASS) sucheLaeuft = false;
  }
  xSemaphoreGive(sucheSperre);
  if (da) server.send(200, "application/json", e);
  else server.send(202, "application/json", "[]");
}

static void ortSetzen() {
  Ort o;
  o.lat = server.arg("lat").toFloat();
  o.lon = server.arg("lon").toFloat();
  strlcpy(o.name, server.arg("name").c_str(), sizeof(o.name));
  o.vomNutzer = true;
  ortSpeichern(o);
  himmelNeuLaden();
  server.send(200, "text/plain", "ok");
}

static void ortAutomatisch() {
  himmelOrtAutomatisch();  // erledigt die Himmel-Aufgabe im Hintergrund
  server.send(200, "text/plain", "ok");
}

static void nameSetzen() {
  String n = server.arg("name");
  n.trim();
  n.toLowerCase();
  if (!nameGueltig(n)) {
    server.send(400, "text/plain", "ungueltig");
    return;
  }
  server.send(200, "text/plain", "ok");
  if (n == einst.name) return;
  strlcpy(einst.name, n.c_str(), sizeof(einst.name));
  einstellungenSpeichern();
  delay(500);
  ESP.restart();  // der neue Name gilt im Netz erst nach dem Neustart
}

void webStart() {
  server.on("/", HTTP_GET, seite);
  server.on("/api/status", HTTP_GET, status);
  server.on("/api/einstellungen", HTTP_POST, einstellungenSetzen);
  server.on("/orte", HTTP_GET, orteSuchen);
  server.on("/api/ort", HTTP_POST, ortSetzen);
  server.on("/api/ort-auto", HTTP_POST, ortAutomatisch);
  server.on("/api/name", HTTP_POST, nameSetzen);
  server.on("/api/bild", HTTP_GET, bildSenden);
  server.on("/api/update/suchen", HTTP_POST, []() {
    updateSuchenLassen();
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/update/installieren", HTTP_POST, []() {
    updateInstallierenLassen();
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/wlan", HTTP_POST, []() {
    server.send(200, "text/plain", "ok");
    delay(300);
    wlanEinrichtungAnfordern();
  });
  server.onNotFound([]() { server.send(404, "text/plain", "nicht gefunden"); });
  server.begin();
  Serial.printf("Einstellungen: http://%s.local oder http://%s\n", einst.name, WiFi.localIP().toString().c_str());
}

void webLoop() { server.handleClient(); }
