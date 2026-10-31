#include "Config.h"
#include <LittleFS.h>
#include <string.h>

static const char* CONFIG_PATH = "/config.txt";

bool loadConfig(AppConfig& cfg)
{
  if (!LittleFS.exists(CONFIG_PATH)) return false;

  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;
  bool hasRegion = false;

  while (f.available())
  {
    String line = f.readStringUntil('\n');
    line.replace("\r", "");
    const int eq = line.indexOf('=');
    if (eq < 0) continue;

    const String key = line.substring(0, eq);
    const String val = line.substring(eq + 1);

    if (key == "ssid") snprintf(cfg.ssid, sizeof(cfg.ssid), "%s", val.c_str());
    else if (key == "pass") snprintf(cfg.pass, sizeof(cfg.pass), "%s", val.c_str());
    else if (key == "city") snprintf(cfg.city, sizeof(cfg.city), "%s", val.c_str());
    else if (key == "region") { snprintf(cfg.region, sizeof(cfg.region), "%s", val.c_str()); hasRegion = true; }
    else if (key == "lat") cfg.lat = val.toDouble();
    else if (key == "lon") cfg.lon = val.toDouble();
    else if (key == "loc") cfg.locationSet = (val == "1");
  }
  f.close();

  // Ancienne configuration : "Ville, Region (Pays)" dans city -> on separe
  const String full(cfg.city);
  const int comma = full.indexOf(',');
  if (comma >= 0)
  {
    String r = full.substring(comma + 2);
    const int par = r.indexOf('(');
    if (par >= 0) r = r.substring(0, par);
    r.trim();
    snprintf(cfg.city, sizeof(cfg.city), "%s", full.substring(0, comma).c_str());
    if (!hasRegion) snprintf(cfg.region, sizeof(cfg.region), "%s", r.c_str());
  }

  return cfg.ssid[0] != '\0';
}

bool saveConfig(const AppConfig& cfg)
{
  char buf[480];
  snprintf(buf, sizeof(buf),
           "ssid=%s\npass=%s\ncity=%s\nregion=%s\nlat=%.6f\nlon=%.6f\nloc=%d\n",
           cfg.ssid, cfg.pass, cfg.city, cfg.region, cfg.lat, cfg.lon, cfg.locationSet ? 1 : 0);

  for (int attempt = 0; attempt < 2; ++attempt)
  {
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (f)
    {
      const size_t written = f.print(buf);
      f.close();
      if (written == strlen(buf))
      {
        Serial.println("Config enregistree");
        return true;
      }
      Serial.println("Config: ecriture incomplete");
    }
    else
    {
      Serial.println("Config: ouverture en ecriture impossible");
    }

    if (attempt == 0)
    {
      Serial.println("Formatage LittleFS puis nouvel essai");
      LittleFS.format();
      LittleFS.begin();
    }
  }
  return false;
}
