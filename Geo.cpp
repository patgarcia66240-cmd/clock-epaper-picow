#include "Geo.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static String httpGet(const char* url)
{
  String body;
  WiFiClientSecure client;
  client.setInsecure();  // API publique non sensible : certificat non verifie
  HTTPClient http;
  http.setTimeout(8000);

  if (!http.begin(client, url))
  {
    Serial.println("Geo: initialisation HTTP impossible");
    return body;
  }
  const int code = http.GET();
  if (code == 200)
  {
    body = http.getString();
  }
  else
  {
    Serial.print("Geo: HTTP ");
    Serial.println(code);
  }
  http.end();
  return body;
}

// Valeur d'une cle JSON texte : "cle":"valeur"
static String jsonString(const String& body, const char* key, int from = 0)
{
  const String pat = String("\"") + key + "\"";
  int p = body.indexOf(pat, from);
  if (p < 0) return "";
  p = body.indexOf(':', p + pat.length());
  if (p < 0) return "";
  p = body.indexOf('"', p);
  if (p < 0) return "";
  const int e = body.indexOf('"', p + 1);
  if (e < 0) return "";
  return body.substring(p + 1, e);
}

// Valeur d'une cle JSON numerique : "cle": 12.3
static bool jsonNumber(const String& body, const char* key, double& out, int from = 0)
{
  const String pat = String("\"") + key + "\"";
  int p = body.indexOf(pat, from);
  if (p < 0) return false;
  p = body.indexOf(':', p + pat.length());
  if (p < 0) return false;
  p++;
  while (p < (int)body.length() && body[p] == ' ') p++;
  if (p >= (int)body.length() || body[p] == 'n') return false;  // null
  out = body.substring(p).toDouble();
  return true;
}

static String urlEncode(const String& s)
{
  String o;
  char buf[4];
  for (size_t i = 0; i < s.length(); ++i)
  {
    const unsigned char c = s[i];
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) o += (char)c;
    else
    {
      snprintf(buf, sizeof(buf), "%%%02X", c);
      o += buf;
    }
  }
  return o;
}

bool geoAutoDetect(AppConfig& cfg)
{
  const String body = httpGet("https://ipapi.co/json/");
  if (body.length() == 0) return false;
  if (body.indexOf("\"error\"") >= 0) return false;  // ipapi renvoie une cle "error" en cas d'echec

  const String city = jsonString(body, "city");
  double lat = 0, lon = 0;
  if (city.length() == 0 || !jsonNumber(body, "latitude", lat) || !jsonNumber(body, "longitude", lon))
    return false;

  snprintf(cfg.city, sizeof(cfg.city), "%s", city.c_str());
  snprintf(cfg.region, sizeof(cfg.region), "%s", jsonString(body, "region").c_str());
  cfg.lat = lat;
  cfg.lon = lon;
  cfg.locationSet = true;
  Serial.print("Ville detectee : "); Serial.println(cfg.city);
  return true;
}

int geoSearchPlaces(const String& query, GeoPlace* out, int maxCount)
{
  const String url = "https://geocoding-api.open-meteo.com/v1/search?count=" + String(maxCount) +
                     "&language=fr&format=json&name=" + urlEncode(query);
  const String body = httpGet(url.c_str());
  const int start = body.indexOf("\"results\"");
  if (start < 0) return 0;

  int count = 0;
  int pos = start;
  while (count < maxCount)
  {
    const int np = body.indexOf("\"name\"", pos);
    if (np < 0) break;
    int next = body.indexOf("\"name\"", np + 6);
    const String obj = body.substring(np, next < 0 ? body.length() : next);

    double la = 0, lo = 0;
    if (jsonNumber(obj, "latitude", la) && jsonNumber(obj, "longitude", lo))
    {
      const String name = jsonString(obj, "name");
      const String admin = jsonString(obj, "admin1");
      const String country = jsonString(obj, "country");

      String label = name;
      if (admin.length()) label += ", " + admin;
      if (country.length()) label += " (" + country + ")";

      out[count].label = label;
      out[count].name = name;
      out[count].region = admin;
      out[count].lat = la;
      out[count].lon = lo;
      ++count;
    }
    pos = np + 6;
  }
  return count;
}

String utf8ToLatin1(const String& in)
{
  String out;
  for (size_t i = 0; i < in.length(); ++i)
  {
    const unsigned char c = in[i];
    if ((c == 0xC2 || c == 0xC3) && i + 1 < in.length())
    {
      const unsigned char n = in[i + 1];
      if (n >= 0x80 && n <= 0xBF)
      {
        out += (char)(c == 0xC2 ? n : n + 0x40);
        ++i;
        continue;
      }
    }
    out += (c < 0x80) ? (char)c : '?';
  }
  return out;
}
