#include "netzupdate.h"
#include "einstellungen.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

static const char *SEITE = "https://prod-byte.github.io/Himmelsstation/";

static volatile int neuerBuild = 0;
static volatile bool sucheGewuenscht = false, installierenGewuenscht = false;

void updatePruefen() {
  if (FIRMWARE_BUILD == 0) return;  // selbst gebaut - nicht ueberschreiben
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, String(SEITE) + "version.json?t=" + millis())) return;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      int build = doc["build"] | 0;
      neuerBuild = build > FIRMWARE_BUILD ? build : 0;
      Serial.printf("Update: online Build %d, hier Build %d\n", build, FIRMWARE_BUILD);
    }
  }
  http.end();
}

bool updateSucheGewuenscht() {
  bool w = sucheGewuenscht;
  sucheGewuenscht = false;
  return w;
}
void updateSuchenLassen() { sucheGewuenscht = true; }

int updateNeuerBuild() { return neuerBuild; }

bool updateInstallierenGewuenscht() {
  bool w = installierenGewuenscht;
  installierenGewuenscht = false;
  return w;
}
void updateInstallierenLassen() { installierenGewuenscht = true; }

static UpdateAnzeige anzeige = nullptr;

void updateInstallieren(UpdateAnzeige a) {
  anzeige = a;
  if (anzeige) anzeige(0, TX("lade neue Version", "downloading new version"));
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(false);  // erst "fertig" zeigen, dann selbst neu starten
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.onProgress([](int fertig, int gesamt) {
    static int zuletzt = -1;
    int p = gesamt > 0 ? (int64_t)fertig * 100 / gesamt : 0;
    if (p != zuletzt && anzeige) anzeige(p, TX("nicht ausschalten", "do not switch off"));
    zuletzt = p;
  });
  t_httpUpdate_return r = httpUpdate.update(client, String(SEITE) + "firmware.bin");
  if (r == HTTP_UPDATE_OK) {
    if (anzeige) anzeige(100, TX("fertig - starte neu", "done - restarting"));
    delay(1500);
    ESP.restart();
  }
  Serial.printf("Update fehlgeschlagen: %s\n", httpUpdate.getLastErrorString().c_str());
  if (anzeige) anzeige(0, TX("fehlgeschlagen", "failed"));
  delay(3000);
  neuerBuild = 0;  // nicht dauernd neu versuchen - in 6 Stunden wieder
}
