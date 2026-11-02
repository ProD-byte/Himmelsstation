// WLAN-Verbindung mit Einrichtungsportal (uebernommen von der ESPCam)
//
// Bekannte WLANs liegen im NVS (Preferences, Namespace "wifi"), das zuletzt eingerichtete
// steht vorn. Beim Start wird per WiFiMulti das staerkste davon gewaehlt. Ist keins erreichbar,
// oeffnet die Himmelsstation den Access Point WLAN_SETUP_NAME; das Handy landet ueber
// DNS-Umleitung automatisch auf der Einrichtungsseite.

#include "wlan.h"
#include "ort.h"
#include "einstellungen.h"
#include "portal_html.h"

#include <algorithm>
#include <vector>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

static const int MAX_NETZE = 5;
static const uint32_t VERBINDEN_MS = 15000;
// Sind bekannte WLANs gespeichert (z. B. Router war nur kurz aus), wird nach dieser Zeit
// ohne Eingabe neu gestartet und erneut verbunden.
static const uint32_t PORTAL_WARTEN_MS = 5 * 60 * 1000;

struct Netz {
  String ssid;
  String pass;
};

static WlanAnzeige anzeige = nullptr;

static void zeige(WlanPhase phase, const String &netz) {
  if (anzeige) anzeige(phase, netz.c_str());
}

// --- Speicher ---

static int netzeLaden(Netz *liste) {
  Preferences prefs;
  prefs.begin("wifi", false);
  int n = min((int)prefs.getInt("count", 0), MAX_NETZE);
  for (int i = 0; i < n; i++) {
    liste[i].ssid = prefs.getString(("s" + String(i)).c_str(), "");
    liste[i].pass = prefs.getString(("p" + String(i)).c_str(), "");
  }
  prefs.end();
  return n;
}

static void netzeSpeichern(const Netz *liste, int n) {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.putInt("count", n);
  for (int i = 0; i < n; i++) {
    prefs.putString(("s" + String(i)).c_str(), liste[i].ssid);
    prefs.putString(("p" + String(i)).c_str(), liste[i].pass);
  }
  prefs.end();
}

// Neues WLAN nach vorn; gleiche SSID wird ersetzt, das aelteste faellt raus.
static void netzMerken(const String &ssid, const String &pass) {
  Netz alt[MAX_NETZE];
  int n = netzeLaden(alt);
  Netz liste[MAX_NETZE];
  liste[0] = {ssid, pass};
  int anzahl = 1;
  for (int i = 0; i < n && anzahl < MAX_NETZE; i++) {
    if (alt[i].ssid != ssid) liste[anzahl++] = alt[i];
  }
  netzeSpeichern(liste, anzahl);
  Serial.printf("WLAN \"%s\" gespeichert (%d bekannt)\n", ssid.c_str(), anzahl);
}

static void einrichtungMerken(bool an) {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putBool("setup", an);
  prefs.end();
}

static bool einrichtungAbholen() {
  Preferences prefs;
  prefs.begin("wifi", false);
  bool an = prefs.getBool("setup", false);
  if (an) prefs.remove("setup");
  prefs.end();
  return an;
}

void wlanEinrichtungAnfordern() {
  einrichtungMerken(true);
  delay(300);
  ESP.restart();
}

// --- Verbinden ---

static bool bekannteVerbinden(const Netz *liste, int n) {
  WiFiMulti multi;
  for (int i = 0; i < n; i++) multi.addAP(liste[i].ssid.c_str(), liste[i].pass.c_str());
  Serial.printf("Suche %d bekannte WLANs\n", n);
  for (int versuch = 0; versuch < 3; versuch++) {
    zeige(WLAN_SUCHE, liste[0].ssid);
    if (multi.run(VERBINDEN_MS) == WL_CONNECTED) {
      Serial.printf("Verbunden mit \"%s\"\n", WiFi.SSID().c_str());
      return true;
    }
  }
  Serial.println("Kein bekanntes WLAN erreichbar");
  return false;
}

// --- Einrichtungsportal ---

enum class Versuch { Nichts, Verbinde, Ok, Falsch };

static WebServer *portal = nullptr;
static DNSServer dns;
static Versuch versuch = Versuch::Nichts;
static String versuchSsid, versuchPass;
static uint32_t versuchStart = 0;

static void seiteStart() {
  portal->sendHeader("Cache-Control", "no-store");
  portal->send(200, "text/html; charset=utf-8", PORTAL_HTML);
}

// Netzliste als JSON, nach Signalstaerke sortiert. Doppelte SSIDs (Mesh, Repeater)
// erscheinen nur einmal mit dem besten Signal.
static String jsonText(const String &s) {
  String aus;
  for (char c : s) {
    if (c == '"' || c == '\\') aus += '\\';
    if ((uint8_t)c >= 0x20) aus += c;
  }
  return aus;
}

static void seiteSuche() {
  int gefunden = max(0, (int)WiFi.scanNetworks());
  std::vector<int> reihe;
  for (int i = 0; i < gefunden; i++) {
    if (!WiFi.SSID(i).isEmpty()) reihe.push_back(i);
  }
  std::sort(reihe.begin(), reihe.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });

  String json = "[";
  for (size_t k = 0; k < reihe.size(); k++) {
    int i = reihe[k];
    String ssid = WiFi.SSID(i);
    bool doppelt = false;
    for (size_t j = 0; j < k; j++) {
      if (WiFi.SSID(reihe[j]) == ssid) doppelt = true;
    }
    if (doppelt) continue;
    if (json.length() > 1) json += ",";
    json += "{\"ssid\":\"" + jsonText(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
            ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  portal->sendHeader("Cache-Control", "no-store");
  portal->send(200, "application/json", json);
}

static void seiteSpeichern() {
  versuchSsid = portal->arg("ssid");
  versuchPass = portal->arg("pass");
  if (versuchSsid.isEmpty()) {
    portal->send(400, "text/plain", "ssid fehlt");
    return;
  }
  Serial.printf("Teste WLAN \"%s\"\n", versuchSsid.c_str());
  WiFi.disconnect();
  WiFi.begin(versuchSsid.c_str(), versuchPass.c_str());
  versuch = Versuch::Verbinde;
  versuchStart = millis();
  portal->send(200, "text/plain", "ok");
}

static void seiteStatus() {
  const char *s = "idle";
  switch (versuch) {
    case Versuch::Verbinde: s = "connecting"; break;
    case Versuch::Ok: s = "ok"; break;
    case Versuch::Falsch: s = "failed"; break;
    default: break;
  }
  portal->sendHeader("Cache-Control", "no-store");
  portal->send(200, "application/json", String("{\"state\":\"") + s + "\"}");
}

// --- Schritt 2: Standort. Die Himmelsstation ist jetzt selbst im Internet und sucht fuer das Handy. ---

static bool ortFertig = false;
static uint32_t ortFertigSeit = 0;
static String ortGewaehlt;

// Vorschlag: der zuletzt am Handy gewaehlte Ort, sonst grob ueber die Internet-Adresse
static void seiteOrt() {
  static bool geholt = false;
  static Ort vorschlag;
  if (!geholt) {
    vorschlag = ortAktuell();
    if (!vorschlag.vomNutzer && !ortPerIp(vorschlag)) vorschlag.name[0] = 0;
    geholt = true;
  }
  String json = "{}";
  if (vorschlag.name[0]) {
    String name = vorschlag.name;
    name.replace("\"", "'");
    json = "{\"name\":\"" + name + "\",\"lat\":" + String(vorschlag.lat, 4) + ",\"lon\":" + String(vorschlag.lon, 4) +
           ",\"gewaehlt\":" + (vorschlag.vomNutzer ? "true" : "false") + "}";
  }
  portal->sendHeader("Cache-Control", "no-store");
  portal->send(200, "application/json", json);
}

static void seiteOrteSuchen() {
  portal->sendHeader("Cache-Control", "no-store");
  portal->send(200, "application/json", ortSuchen(portal->arg("q")));
}

static void seiteOrtSpeichern() {
  if (portal->hasArg("lat")) {
    Ort o;
    o.lat = portal->arg("lat").toFloat();
    o.lon = portal->arg("lon").toFloat();
    strlcpy(o.name, portal->arg("name").c_str(), sizeof(o.name));
    o.vomNutzer = true;
    ortSpeichern(o);
    ortGewaehlt = o.name;
  }
  // "Ueberspringen": nichts speichern - dann ermittelt die Himmelsstation den Ort selbst

  // Sprache des Handys uebernehmen (die Einrichtungsseite erscheint schon darin)
  if (portal->hasArg("sprache")) {
    einst.sprache = portal->arg("sprache") == "en" ? 1 : 0;
    einstellungenSpeichern();
  }
  ortFertig = true;
  ortFertigSeit = millis();
  portal->send(200, "text/plain", "ok");
}

// Alles andere (Captive-Portal-Erkennung von Android/iOS/Windows) auf die Einrichtungsseite
// umleiten, damit das Handy sie von selbst oeffnet.
static void seiteUmleiten() {
  portal->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
  portal->send(302);
}

static void portalLaufen(bool bekannte) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WLAN_SETUP_NAME);
  delay(100);
  IPAddress apIp = WiFi.softAPIP();
  dns.start(53, "*", apIp);

  portal = new WebServer(80);
  portal->on("/", HTTP_GET, seiteStart);
  portal->on("/scan", HTTP_GET, seiteSuche);
  portal->on("/save", HTTP_POST, seiteSpeichern);
  portal->on("/status", HTTP_GET, seiteStatus);
  portal->on("/ort", HTTP_GET, seiteOrt);
  portal->on("/ort", HTTP_POST, seiteOrtSpeichern);
  portal->on("/orte", HTTP_GET, seiteOrteSuchen);
  portal->onNotFound(seiteUmleiten);
  portal->begin();

  Serial.printf("Einrichtung: mit WLAN \"%s\" verbinden, dann http://%s\n", WLAN_SETUP_NAME,
                apIp.toString().c_str());

  uint32_t portalStart = millis();
  uint32_t okSeit = 0, falschSeit = 0;
  while (true) {
    dns.processNextRequest();
    portal->handleClient();

    if (versuch == Versuch::Verbinde) {
      if (WiFi.status() == WL_CONNECTED) {
        netzMerken(versuchSsid, versuchPass);
        versuch = Versuch::Ok;
        okSeit = millis();
      } else if (millis() - versuchStart > VERBINDEN_MS) {
        Serial.println("Verbindung fehlgeschlagen");
        WiFi.disconnect();
        versuch = Versuch::Falsch;
        falschSeit = millis();
      }
    }

    // Display: "Falsch" ein paar Sekunden zeigen, dann wieder die Anleitung
    WlanPhase phase = WLAN_EINRICHTUNG;
    String text = versuchSsid;
    if (versuch == Versuch::Verbinde) phase = WLAN_TESTE;
    else if (versuch == Versuch::Ok && !ortFertig) phase = WLAN_ORT;
    else if (versuch == Versuch::Ok) {
      phase = WLAN_OK;
      text = ortGewaehlt;
    }
    else if (versuch == Versuch::Falsch && millis() - falschSeit < 6000) phase = WLAN_FALSCH;
    zeige(phase, text);

    // Nach der Ortswahl kurz warten, damit das Handy die Fertig-Meldung noch abholt.
    // Waehlt niemand einen Ort, geht es nach 3 Minuten ohne weiter.
    if (ortFertig && millis() - ortFertigSeit > 4000) break;
    if (versuch == Versuch::Ok && !ortFertig && millis() - okSeit > 3 * 60 * 1000) break;

    if (bekannte && versuch != Versuch::Verbinde && versuch != Versuch::Ok &&
        millis() - portalStart > PORTAL_WARTEN_MS) {
      Serial.println("Keine Eingabe, neuer Versuch mit bekannten WLANs");
      ESP.restart();
    }
    delay(2);
  }

  portal->stop();
  delete portal;
  portal = nullptr;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void wlanVerbinden(bool einrichtungErzwingen, WlanAnzeige a) {
  anzeige = a;
  WiFi.persistent(false);  // Zugangsdaten verwalten wir selbst
  WiFi.setHostname(einst.name);
  WiFi.mode(WIFI_STA);

  Netz liste[MAX_NETZE];
  int n = netzeLaden(liste);
  bool erzwingen = einrichtungAbholen() || einrichtungErzwingen;

  if (!erzwingen && n > 0 && bekannteVerbinden(liste, n)) return;
  portalLaufen(n > 0);
}
