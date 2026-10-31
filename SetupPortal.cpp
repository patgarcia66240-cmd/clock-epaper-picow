#include "SetupPortal.h"
#include "Geo.h"
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);
static AppConfig* activeCfg = nullptr;
static bool apMode = false;
static bool routesReady = false;
static bool wifiSaved = false;
static String scannedSsids[20];
static int scannedCount = 0;
static GeoPlace lastPlaces[8];
static int lastPlaceCount = 0;

static const char ICON_PIN[] =
    "<svg width=\"30\" height=\"30\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M12 21s-7-6.2-7-11a7 7 0 0 1 14 0c0 4.8-7 11-7 11z\"/><circle cx=\"12\" cy=\"10\" r=\"2.5\"/></svg>";

static const char ICON_WIFI[] =
    "<svg width=\"30\" height=\"30\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\">"
    "<path d=\"M5 12.5a10 10 0 0 1 14 0\"/><path d=\"M8.5 16a5 5 0 0 1 7 0\"/><circle cx=\"12\" cy=\"19.5\" r=\"1\"/></svg>";

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Horloge</title><style>"
    "body{font-family:system-ui,sans-serif;margin:0;background:#f4f3ee;color:#1a1a18}"
    ".wrap{max-width:460px;margin:0 auto;padding:20px 16px}"
    ".card{background:#fff;border-radius:12px;padding:16px 18px;margin-bottom:14px;box-shadow:0 1px 2px rgba(0,0,0,.06);display:block}"
    ".head{display:flex;gap:14px;align-items:center}"
    ".head svg{flex:none;color:#185FA5}"
    ".head>div{flex:1;min-width:0}"
    ".name{font-size:20px;font-weight:600}"
    ".region{font-size:14px;color:#6b6a64;margin-top:2px;word-break:break-word}"
    ".coords{font-size:13px;color:#6b6a64;margin:10px 0 0}"
    "label{display:block;margin-top:12px;font-size:14px;color:#444}"
    "input,select{font-size:16px;width:100%;margin:6px 0;padding:10px;box-sizing:border-box;border:1px solid #d8d6ce;border-radius:8px;background:#fff}"
    "button{font-size:16px;width:100%;margin-top:10px;padding:11px;border:0;border-radius:8px;background:linear-gradient(135deg,#2b74c2,#185FA5);color:#fff;cursor:pointer;box-shadow:0 2px 6px rgba(24,95,165,.25);transition:transform .15s,box-shadow .15s,filter .15s}"
    "button:hover{transform:translateY(-1px);box-shadow:0 4px 12px rgba(24,95,165,.35);filter:brightness(1.06)}"
    "button:active{transform:translateY(0)}"
    "input:focus,select:focus{outline:none;border-color:#185FA5;box-shadow:0 0 0 3px rgba(24,95,165,.15)}"
    ".res{background:linear-gradient(180deg,#ffffff,#f7f9fc);color:#1a1a18;border:1px solid #d8d6ce;text-align:left;margin:6px 0;box-shadow:none}"
    ".res:hover{background:linear-gradient(180deg,#f3f8fe,#e3eefa);border-color:#185FA5;color:#185FA5;transform:translateY(-1px);box-shadow:0 3px 10px rgba(24,95,165,.18)}"
    ".res{padding:12px 16px;border-radius:12px}"
    ".res .name{display:block;font-size:17px;font-weight:600;color:#1a1a18}"
    ".res .region{display:block;font-size:14px;color:#6b6a64;margin-top:2px;font-weight:400}"
    ".back{margin:14px 0}"
    ".back a{color:#185FA5}"
    ".err{color:#a32d2d;font-size:14px}"
    "</style></head><body><div class=\"wrap\">";
static const char PAGE_TAIL[] = "</div></body></html>";

static String htmlEscape(const String& s)
{
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i)
  {
    const char c = s[i];
    switch (c)
    {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

static void sendPage(const String& body)
{
  String html = PAGE_HEAD;
  html += body;
  html += PAGE_TAIL;
  server.send(200, "text/html; charset=utf-8", html);
}

static void renderWifiForm(const String& msg)
{
  String b = "<div class=\"card\"><div class=\"head\">" + String(ICON_WIFI) +
             "<div><div class=\"name\">Configuration Wi-Fi</div>"
             "<div class=\"region\">Choisis ton réseau et saisis le mot de passe</div></div></div></div>";
  if (msg.length()) b += "<p class=\"err\">" + htmlEscape(msg) + "</p>";

  b += "<form method=\"POST\" action=\"/save\" class=\"card\">";
  b += "<label>Réseau détecté</label><select name=\"ssid\">";
  for (int i = 0; i < scannedCount; ++i)
  {
    const String s = htmlEscape(scannedSsids[i]);
    b += "<option value=\"" + s + "\">" + s + "</option>";
  }
  b += "</select>";
  b += "<label>Ou nom du réseau (s'il n'apparaît pas)</label>";
  b += "<input name=\"ssid_manual\" maxlength=\"32\">";
  b += "<label>Mot de passe</label><input type=\"password\" name=\"pass\" maxlength=\"64\">";
  b += "<button type=\"submit\">Enregistrer</button></form>";
  sendPage(b);
}

static void handleRoot()
{
  if (apMode)
  {
    renderWifiForm("");
    return;
  }

  const String name = String(activeCfg->city);
  const String region = String(activeCfg->region);

  String b = "<div class=\"card\"><div class=\"head\">" + String(ICON_PIN) + "<div>";
  b += "<div class=\"name\">" + htmlEscape(name) + "</div>";
  if (region.length()) b += "<div class=\"region\">" + htmlEscape(region) + "</div>";
  b += "</div></div>";
  b += "<p class=\"coords\">Coordonnées : " + String(activeCfg->lat, 3) + ", " + String(activeCfg->lon, 3) + "</p></div>";

  b += "<form method=\"GET\" action=\"/search\" class=\"card\"><label>Changer de ville</label>";
  b += "<input name=\"q\" placeholder=\"Ex. Arles\" required>";
  b += "<button type=\"submit\">Rechercher</button></form>";
  sendPage(b);
}

static void handleSave()
{
  if (!apMode || activeCfg == nullptr)
  {
    server.send(404, "text/plain", "Non disponible");
    return;
  }
  String ssid = server.arg("ssid_manual");
  ssid.trim();
  if (ssid.length() == 0) ssid = server.arg("ssid");
  const String pass = server.arg("pass");

  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64)
  {
    renderWifiForm("Nom de r\xC3\xA9seau ou mot de passe invalide.");
    return;
  }

  Serial.print("Config Wi-Fi recue, reseau : "); Serial.println(ssid);
  snprintf(activeCfg->ssid, sizeof(activeCfg->ssid), "%s", ssid.c_str());
  snprintf(activeCfg->pass, sizeof(activeCfg->pass), "%s", pass.c_str());
  if (!saveConfig(*activeCfg))
  {
    renderWifiForm("Echec de l'enregistrement.");
    return;
  }
  wifiSaved = true;
  sendPage("<h2>Enregistr\xC3\xA9</h2><p>Le Pico red\xC3\xA9marre et se connecte au Wi-Fi.</p>");
}

static void handleSearch()
{
  if (apMode || activeCfg == nullptr)
  {
    server.send(404, "text/plain", "Non disponible");
    return;
  }
  String q = server.arg("q");
  q.trim();
  if (q.length() < 2)
  {
    sendPage("<div class=\"card\"><p class=\"err\">Saisis au moins 2 caractères.</p><a href=\"/\">Retour</a></div>");
    return;
  }

  lastPlaceCount = geoSearchPlaces(q, lastPlaces, 8);

  String b = "<div class=\"card\"><div class=\"head\">" + String(ICON_PIN) +
             "<div><div class=\"name\">Résultats</div>"
             "<div class=\"region\">pour " + htmlEscape(q) + "</div></div></div></div>";

  if (lastPlaceCount == 0)
    b += "<div class=\"card\"><p class=\"err\">Aucun résultat. Essaie une autre orthographe.</p></div>";

  for (int i = 0; i < lastPlaceCount; ++i)
  {
    b += "<form method=\"POST\" action=\"/setcity\">";
    b += "<input type=\"hidden\" name=\"idx\" value=\"" + String(i) + "\">";
    b += "<button class=\"res\" type=\"submit\">";
    b += "<span class=\"name\">" + htmlEscape(lastPlaces[i].name) + "</span>";
    if (lastPlaces[i].region.length())
      b += "<span class=\"region\">" + htmlEscape(lastPlaces[i].region) + "</span>";
    b += "</button></form>";
  }
  b += "<p class=\"back\"><a href=\"/\">Retour</a></p>";
  sendPage(b);
}

static void handleSetCity()
{
  if (apMode || activeCfg == nullptr)
  {
    server.send(404, "text/plain", "Non disponible");
    return;
  }

  const int idx = server.arg("idx").toInt();
  if (idx < 0 || idx >= lastPlaceCount)
  {
    sendPage("<p>Recherche expirée. Refais la recherche.</p><p><a href=\"/\">Retour</a></p>");
    return;
  }

  const GeoPlace& pl = lastPlaces[idx];
  snprintf(activeCfg->city, sizeof(activeCfg->city), "%s", pl.name.c_str());
  snprintf(activeCfg->region, sizeof(activeCfg->region), "%s", pl.region.c_str());
  Serial.print("Label choisi, longueur "); Serial.print(pl.label.length());
  Serial.print(" / city en RAM : "); Serial.println(activeCfg->city);
  activeCfg->lat = pl.lat;
  activeCfg->lon = pl.lon;
  activeCfg->locationSet = true;
  Serial.print("Ville choisie : "); Serial.println(activeCfg->city);
  saveConfig(*activeCfg);

  sendPage("<h2>Ville enregistrée</h2>"
           "<p>La météo et le lever/coucher seront mis à jour.</p>"
           "<p><a href=\"/\">Retour</a></p>");
}

static void handleNotFound()
{
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

static void ensureServer()
{
  if (routesReady) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/search", HTTP_GET, handleSearch);
  server.on("/setcity", HTTP_POST, handleSetCity);
  server.onNotFound(handleNotFound);
  server.begin();
  routesReady = true;
}

void webBeginStation(AppConfig& cfg)
{
  activeCfg = &cfg;
  apMode = false;
  ensureServer();
}

void webHandle()
{
  if (routesReady) server.handleClient();
}

void startWifiSetupAp(AppConfig& cfg)
{
  activeCfg = &cfg;
  apMode = true;
  wifiSaved = false;

  // Scan avant d'ouvrir le point d'acces (scanner en mode AP est peu fiable)
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  scannedCount = 0;
  const int found = WiFi.scanNetworks();
  for (int i = 0; i < found && scannedCount < 20; ++i)
  {
    const String s = WiFi.SSID(i);
    bool dup = s.length() == 0;
    for (int j = 0; j < scannedCount && !dup; ++j) dup = (scannedSsids[j] == s);
    if (!dup) scannedSsids[scannedCount++] = s;
  }

  WiFi.beginAP(AP_SSID, AP_PASSWORD);
  ensureServer();
  Serial.print("Point d'acces actif, adresse : ");
  Serial.println(WiFi.softAPIP());
}

bool waitWifiSetup(unsigned long timeoutMs)
{
  const unsigned long start = millis();
  while (!wifiSaved && millis() - start < timeoutMs)
  {
    server.handleClient();
    delay(2);
  }

  // Laisse le temps d'envoyer la page de confirmation
  const unsigned long t = millis();
  while (millis() - t < 1500)
  {
    server.handleClient();
    delay(5);
  }
  return wifiSaved;
}
