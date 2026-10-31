// =====================================================
// Horloge e-Paper - Raspberry Pi Pico W + Waveshare 3.7 pouces
// Brochage Pico W : VCC->VSYS, GND->GND, DIN->GP11, CLK->GP10,
// CS->GP9, DC->GP8, RST->GP12, BUSY->GP13. Bouton externe GP0->GND.
// =====================================================
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include "Fonts/Roboto15.h"
#include "Fonts/Roboto24.h"
#include "Fonts/Roboto64.h"
#include "Fonts/RobotoBold100.h"
#include "Fonts/RobotoBold15.h"
#include "Fonts/RobotoBold20.h"
#include "Fonts/RobotoBold26.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TimeLib.h>

#include "SaintCalendar.h"
// Pilote Waveshare Pico-ePaper-3.7 monochrome SSD1677.
#include "GxEPD2_display_selection_pico2w.h"
#include "Config.h"
#include "Geo.h"
#include "SetupPortal.h"
#include <LittleFS.h>
#include "bitmaps_3_7.h"
#include "MoonPhase.h"
#include "SunriseSunset.h"

PicoClockDisplay display(GxEPD2_370_TC1(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

WiFiUDP wifiUdp;
AppConfig cfg;


// =====================================================
// Constantes
// =====================================================
// VERSION DU FIRMWARE : incrementer a CHAQUE modification du code (MAJEUR.MINEUR.CORRECTIF)
// 2.0.0 = configuration Wi-Fi et ville ; 2.0.1 = numero de version ; 2.0.2 = police heure 130 px ; 2.0.3 = correction d'appel showInfoScreen ; 2.0.4 = diagnostic LittleFS (erreur affichee) ; 2.0.5 = adresse reelle du point d'acces ; 2.0.6 = sauvegarde robuste (formatage et nouvel essai) ; 2.0.7 = police heure corrigee (alignement des glyphes) ; 2.0.8 = heure en rafraichissement complet a chaque minute ; 2.0.9 = test police 84 px (mise en page d'origine) ; 2.0.10 = police heure 100 px ; 2.1.0 = ecran principal avec meteo, date, heure et saint ; 2.1.1 = rafraichissement partiel de l'heure remis ; 2.1.2 = espacement vertical date, heure, saint ; 2.1.3 = soleil en contour, pied de page avec region en petit ; 2.1.4 = heure descendue de 8 px ; 2.1.5 = lever et coucher du soleil sous le saint ; 2.1.6 = lignes meteo descendues de 5 px ; 2.1.7 = C de Celsius agrandi ; 2.1.8 = date en police 20 px ; 2.1.9 = region sans retour a la ligne, diagnostic serie ; 2.1.10 = region sur toute la largeur (police 26 px) ; 2.1.11 = page web de la ville embellie ; 2.1.12 = ecran de demarrage avec icones (ville et adresse) ; 2.1.13 = region sans le pays sur l'ecran ; 2.1.14 = nom de ville complet (buffer 128 octets) ; 2.1.15 = region en police 20 px ; 2.1.16 = choix de ville par indice (nom complet preserve) ; 2.1.17 = region en police 18 px ; 2.1.18 = region en police 15 px ; 2.1.19 = page web : region sur toute la largeur ; 2.1.20 = diagnostic de la ville (serie) ; 2.1.21 = ville et region separees dans la configuration ; 2.1.22 = migration de l'ancienne ville (virgule) ; 2.1.23 = icones meteo plus epaisses ; 2.1.24 = pages web : boutons en degrade et effet survol ; 2.1.25 = resultats de recherche en cartes (nom et region)
static constexpr char FIRMWARE_VERSION[] = "2.1.25";
static constexpr uint32_t SERIAL_BAUDRATE = 115200;
static constexpr unsigned long ONE_SECOND_MS = 1000UL;
static constexpr unsigned long NTP_RETRY_INTERVAL_MS = 30000UL;  // 30 s entre deux tentatives apres echec
static constexpr unsigned long NTP_SYNC_INTERVAL_MS = 3600000UL;       // 1h
static constexpr unsigned long FULL_REFRESH_INTERVAL_MS = 21600000UL;  // 6h
static constexpr uint32_t SPLASH_DURATION_MS = 10000;
static constexpr uint16_t PARTIAL_UPDATES_BEFORE_FULL = 30; // full refresh apres 30 fenetres partielles

static constexpr unsigned long SETUP_HOLD_MS = 5000UL;  // appui long GP0 = mode configuration
static constexpr int BUTTON_PIN = 0; // Bouton externe GP0 vers GND (ne pas utiliser un bouton BOOT non vérifié)
static constexpr unsigned long WEATHER_SHOW_MS = 10000UL;  // duree d'affichage de la meteo

// OTA désactivé sur le portage Pico 2 W : réactiver uniquement avec une implémentation compatible et sécurisée.

static constexpr int SCREEN_W = 480;
static constexpr int SCREEN_H = 280;

// Positions verticales : les coordonnées Y des textes sont des lignes de base.
// Les zones de rafraîchissement partiel englobent les glyphes réellement dessinés.
static constexpr int DATE_TOP = 34;
static constexpr int DATE_AREA_Y = 0;
static constexpr int DATE_AREA_H = 40;

// L'heure est centrée sur toute la largeur de l'écran.
static constexpr int TIME_Y = 35;
static constexpr int TIME_H = 91;
static constexpr int TIME_BASELINE_Y = 126;

static constexpr int SAINT_TOP = 170;
static constexpr int SAINT_AREA_Y = 114;
static constexpr int SAINT_AREA_H = 38;

static constexpr int H_MARGIN = 16;

static constexpr size_t DATE_BUFFER_SIZE = 64;
static constexpr size_t SAINT_BUFFER_SIZE = 80;

// =====================================================
// Meteo en memoire (affichee sur l'ecran principal)
// =====================================================
static constexpr unsigned long WEATHER_REFRESH_MS = 1200000UL;  // 20 min apres succes
static constexpr unsigned long WEATHER_RETRY_MS = 60000UL;      // 1 min apres echec

struct WeatherData
{
  bool valid = false;
  int tempC = 0;
  int code = 0;
  int humidity = -1;
  int windKmh = -1;
  int hour = 0;
  int minute = 0;
};

WeatherData weather;
unsigned long lastWeatherFetchMs = 0;

// =====================================================
// Etat global
// =====================================================
struct ClockState
{
  uint8_t lastMinute = 255;
  uint8_t lastDay = 255;

  bool ntpReady = false;
  bool screenReady = false;

  unsigned long previousTickMillis = 0;
  unsigned long lastNtpSyncMillis = 0;
  unsigned long lastNtpAttemptMillis = 0;
  unsigned long lastFullRefreshMillis = 0;
  uint16_t partialUpdates = 0;
  bool forceNtpSync = false;
  bool showingWeather = false;
  unsigned long weatherShownMillis = 0;
  bool skyPage = false;
};

ClockState state;

// =====================================================
// Mesures texte
// =====================================================
struct TextMetrics
{
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
};

// =====================================================
// SPI écran
// =====================================================
void setupDisplaySPI()
{
  // Configuration SPI validée sur le Pico W + Waveshare Pico-ePaper-3.7.
  SPI1.setSCK(10);  // GP10 -> CLK
  SPI1.setTX(11);   // GP11 -> DIN/MOSI
  SPI1.begin();
  display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
}

// =====================================================
// Outils affichage
// =====================================================
void clearFullScreen()
{
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
}

void showSplashBitmap(uint32_t durationMs)
{
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.drawInvertedBitmap(0, 0, epd_bitmap_3_7, EPD_BITMAP_37_WIDTH, EPD_BITMAP_37_HEIGHT, GxEPD_BLACK);
  } while (display.nextPage());

  delay(durationMs);
}

void print02d(uint8_t value)
{
  char buffer[3];
  snprintf(buffer, sizeof(buffer), "%02u", value);
  display.print(buffer);
}

// =====================================================
// Formatage texte
// =====================================================
void buildDateString(char* out, size_t outSize, time_t t)
{
  if (out == nullptr || outSize == 0)
  {
    return;
  }

  if (t == 0)
  {
    out[0] = '\0';
    return;
  }

  const uint8_t wd = weekday(t); // 1..7
  const uint8_t dd = day(t);
  const uint8_t mm = month(t);
  const int yyyy = year(t);

  const char* weekdayName = SaintCalendar::getWeekdayName(wd - 1); // 0..6
  const char* monthName = SaintCalendar::getMonthName(mm);         // 1..12

  snprintf(out, outSize, "%s %u %s %d", weekdayName, dd, monthName, yyyy);
  out[outSize - 1] = '\0';
}

bool buildSaintString(char* out, size_t outSize, time_t t)
{
  if (out == nullptr || outSize == 0)
  {
    return false;
  }

  if (t == 0)
  {
    out[0] = '\0';
    return false;
  }

  return SaintCalendar::getSaint(day(t), month(t), year(t), out, outSize);
}

// =====================================================
// Mesure / centrage
// =====================================================
TextMetrics measureText(const char* text, const GFXfont* font)
{
  TextMetrics m;
  display.setFont(font);
  display.getTextBounds(text != nullptr ? text : "", 0, 0, &m.x1, &m.y1, &m.w, &m.h);
  return m;
}

int computeCenteredX(uint16_t areaWidth, const TextMetrics& m)
{
  return ((int)areaWidth - (int)m.w) / 2 - m.x1;
}

// =====================================================
// Auto-fit saint sur 2 tailles
// Retourne la police choisie ET les métriques déjà calculées
// pour éviter un double appel à measureText() dans drawSaint().
// =====================================================
const GFXfont* chooseSaintFont(const char* saint, TextMetrics& outMetrics)
{
  if (saint == nullptr)
  {
    outMetrics = measureText("", &Roboto_Bold_18);
    return &Roboto_Bold_18;
  }

  TextMetrics m24 = measureText(saint, &Roboto_Bold_24);
  if ((int)m24.w <= (SCREEN_W - 2 * H_MARGIN))
  {
    outMetrics = m24;
    return &Roboto_Bold_24;
  }

  outMetrics = measureText(saint, &Roboto_Bold_18);
  return &Roboto_Bold_18;
}

// =====================================================
// Ecran principal : date, heure et saint a gauche ; meteo a droite ; pied de page
// =====================================================
static constexpr int LEFT_X = 16;
static constexpr int COL_DIV_X = 290;
static constexpr int WEATHER_X = 306;
static constexpr int FOOTER_LINE_Y = 220;
static constexpr int FOOTER_Y = 244;
static constexpr int FOOTER_REGION_Y = 272;

// Vrai si l'heure est fiable : Wi-Fi connecte et synchro NTP recente
bool clockHealthy()
{
  return WiFi.status() == WL_CONNECTED &&
         state.ntpReady &&
         (millis() - state.lastNtpSyncMillis) < 2 * NTP_SYNC_INTERVAL_MS;
}

// Petit "!" pres de l'heure si l'heure n'est plus fiable
void printStatusIcon()
{
  if (clockHealthy())
    return;

  display.setFont(&Roboto_Bold_18);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(COL_DIV_X - 18, 48);
  display.print("!");
}

// Zone de rafraichissement partiel : uniquement les chiffres de l'heure (x multiple de 8)
static constexpr int TIME_PARTIAL_X = 0;
static constexpr int TIME_PARTIAL_Y = 36;
static constexpr int TIME_PARTIAL_W = 288;
static constexpr int TIME_PARTIAL_H = 100;

// Ecrit l'heure HH:MM (police 100 px) et le "!" eventuel
void drawTimeText()
{
  char timeBuffer[6];
  snprintf(timeBuffer, sizeof(timeBuffer), "%02u:%02u",
           static_cast<unsigned>(hour(now())),
           static_cast<unsigned>(minute(now())));
  const TextMetrics tm = measureText(timeBuffer, &Roboto_Bold_100);
  display.setFont(&Roboto_Bold_100);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(LEFT_X - tm.x1, TIME_BASELINE_Y);
  display.print(timeBuffer);
  printStatusIcon();
}

// Rafraichissement partiel de l'heure seule (appele a chaque minute)
void drawTimePartial()
{
  if (!state.ntpReady || state.showingWeather)
    return;

  display.setPartialWindow(TIME_PARTIAL_X, TIME_PARTIAL_Y, TIME_PARTIAL_W, TIME_PARTIAL_H);
  display.firstPage();
  do
  {
    display.fillRect(TIME_PARTIAL_X, TIME_PARTIAL_Y, TIME_PARTIAL_W, TIME_PARTIAL_H, GxEPD_WHITE);
    drawTimeText();
  } while (display.nextPage());

  state.partialUpdates++;
}

void drawTimeBlock()
{
  // Date
  char dateBuffer[DATE_BUFFER_SIZE];
  buildDateString(dateBuffer, sizeof(dateBuffer), now());
  display.setFont(&Roboto_Bold_20);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(LEFT_X, 30);
  display.print(dateBuffer);

  drawTimeText();

  // Saint (police 24 px, ou 18 px si le nom est trop long pour la colonne)
  char saintBuffer[SAINT_BUFFER_SIZE];
  buildSaintString(saintBuffer, sizeof(saintBuffer), now());
  TextMetrics sm = measureText(saintBuffer, &Roboto_Bold_24);
  const GFXfont* saintFont = &Roboto_Bold_24;
  if (sm.w > COL_DIV_X - LEFT_X - 16)
  {
    saintFont = &Roboto_Bold_18;
    sm = measureText(saintBuffer, saintFont);
  }
  display.setFont(saintFont);
  display.setCursor(LEFT_X - sm.x1, SAINT_TOP);
  display.print(saintBuffer);
  drawSunTimes();
}

void drawWeatherBlock()
{
  display.drawFastVLine(COL_DIV_X, 40, 160, GxEPD_BLACK);

  // Icone et temperature
  if (weather.valid)
  {
    drawWeatherIcon(WEATHER_X + 28, 70, weather.code, 160);
  }

  display.setFont(&Roboto_Bold_64);
  display.setCursor(WEATHER_X + 60, 92);
  if (weather.valid)
  {
    display.print(weather.tempC);
    const int xDeg = display.getCursorX() + 4;
    display.drawCircle(xDeg + 4, 46, 5, GxEPD_BLACK);
    display.drawCircle(xDeg + 4, 46, 4, GxEPD_BLACK);
    display.setFont(&Roboto_Bold_24);
    display.setCursor(xDeg + 12, 92);
    display.print("C");
  }
  else
  {
    display.setFont(&Roboto_Bold_18);
    display.setCursor(WEATHER_X + 60, 84);
    display.print("--");
  }

  // Etat, humidite, vent
  char line[32];
  display.setFont(&Roboto_Bold_18);
  display.setCursor(WEATHER_X, 123);
  display.print(weather.valid ? weatherText(weather.code) : "Indisponible");

  display.setCursor(WEATHER_X, 151);
  if (weather.valid && weather.humidity >= 0)
    snprintf(line, sizeof(line), "Humidit\351 %d %%", weather.humidity);
  else
    snprintf(line, sizeof(line), "Humidit\351 --");
  display.print(line);

  display.setCursor(WEATHER_X, 179);
  if (weather.valid && weather.windKmh >= 0)
    snprintf(line, sizeof(line), "Vent %d km/h", weather.windKmh);
  else
    snprintf(line, sizeof(line), "Vent --");
  display.print(line);
}

void drawFooter()
{
  display.drawFastHLine(LEFT_X, FOOTER_LINE_Y, SCREEN_W - 2 * LEFT_X, GxEPD_BLACK);

  const String cityName = utf8ToLatin1(String(cfg.city));
  const String region = utf8ToLatin1(String(cfg.region));

  display.setFont(&Roboto_Bold_18);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(LEFT_X, FOOTER_Y);
  display.print(cityName);

  if (region.length())
  {
    // Region en police 15 px
    display.setTextWrap(false);
    display.setFont(&Roboto_Bold_15);
    const TextMetrics rm = measureText(region.c_str(), &Roboto_Bold_15);
    display.setCursor(LEFT_X - rm.x1, FOOTER_REGION_Y);
    display.print(region);
    display.setTextWrap(true);
    Serial.print("Region affichee : "); Serial.println(region);
  }

  char maj[32];
  if (weather.valid)
    snprintf(maj, sizeof(maj), "Maj m�t�o %02d:%02d", weather.hour, weather.minute);
  else
    snprintf(maj, sizeof(maj), "M�t�o indisponible");
  display.setFont(&Roboto_Bold_18);
  const TextMetrics fm = measureText(maj, &Roboto_Bold_18);
  display.setCursor(SCREEN_W - LEFT_X - (fm.x1 + fm.w), FOOTER_Y);
  display.print(maj);
}

void drawAll()
{
  if (!state.ntpReady)
    return;

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    drawTimeBlock();
    drawWeatherBlock();
    drawFooter();
  } while (display.nextPage());

  state.screenReady = true;
  state.lastFullRefreshMillis = millis();
  state.partialUpdates = 0;
}

// Met a jour la meteo en memoire. En cas d'echec, les anciennes donnees sont conservees.
void refreshWeather()
{
  lastWeatherFetchMs = millis();
  int t = 0, c = 0, h = -1, w = -1;
  if (WiFi.status() == WL_CONNECTED && fetchWeather(t, c, h, w))
  {
    weather.valid = true;
    weather.tempC = t;
    weather.code = c;
    weather.humidity = h;
    weather.windKmh = w;
    weather.hour = hour(now());
    weather.minute = minute(now());
  }
}

// Rafraichit la meteo periodiquement et redessine l'ecran principal
void handleWeatherRefresh()
{
  if (!state.ntpReady || state.showingWeather)
    return;

  const unsigned long interval = weather.valid ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS;
  if (millis() - lastWeatherFetchMs < interval)
    return;

  refreshWeather();
  drawAll();
}

// =====================================================
// WiFi / NTP
// =====================================================
bool connectWiFiAndInitNTP()
{
  // Les identifiants sont volontairement externalisés : aucun secret inventé.
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid, cfg.pass);
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 30000UL)
  {
    delay(250);
    yield();
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Connexion Wi-Fi impossible (verifier les identifiants)");
    return false;
  }
  Serial.print("Wi-Fi OK, IP: "); Serial.println(WiFi.localIP());
  wifiUdp.begin(2390);
  Serial.println("Wi-Fi initialise; synchronisation NTP UDP native");
  return true;
}

bool requestNtpUtc(time_t& utc)
{
  static const char* NTP_HOST = "pool.ntp.org";
  IPAddress ntpAddress;
  if (WiFi.hostByName(NTP_HOST, ntpAddress) != 1) return false;
  uint8_t packet[48] = {0};
  packet[0] = 0b11100011; packet[1] = 0; packet[2] = 6; packet[3] = 0xEC;
  packet[12] = 49; packet[13] = 0x4E; packet[14] = 49; packet[15] = 52;
  wifiUdp.beginPacket(ntpAddress, 123);
  wifiUdp.write(packet, sizeof(packet));
  if (!wifiUdp.endPacket()) return false;
  const unsigned long started = millis();
  while (millis() - started < 1800UL)
  {
    const int size = wifiUdp.parsePacket();
    if (size >= 48)
    {
      uint8_t response[48];
      if (wifiUdp.read(response, sizeof(response)) != sizeof(response)) return false;
      const uint32_t seconds1900 = (uint32_t)response[40] << 24 | (uint32_t)response[41] << 16 |
                                   (uint32_t)response[42] << 8 | response[43];
      if (seconds1900 < 2208988800UL) return false;
      utc = (time_t)(seconds1900 - 2208988800UL);
      return true;
    }
    delay(10);
  }
  return false;
}

time_t localTimeFromUtc(time_t utc);

bool syncTimeFromNTP()
{
  time_t utc = 0;
  if (!requestNtpUtc(utc) || utc == 0)
  {
    Serial.println("Heure NTP invalide ou serveur inaccessible");
    return false;
  }
  setTime(localTimeFromUtc(utc));

  state.lastMinute = minute(now());
  state.lastDay = day(now());
  state.lastNtpSyncMillis = millis();
  state.ntpReady = true;

  Serial.print("Synchro NTP OK: ");
  Serial.println((unsigned long)now());

  return true;
}

bool initialSync(uint32_t timeoutMs = 15000)
{
  const unsigned long start = millis();

  while (millis() - start < timeoutMs)
  {
    if (syncTimeFromNTP())
    {
      return true;
    }
    // Pompe les événements WiFi pendant l'attente
    // sans bloquer avec delay()
    const unsigned long retryStart = millis();
    while (millis() - retryStart < 500)
    {
      yield();
    }
  }

  return false;
}

// =====================================================
// Horloge locale
// =====================================================
// Redessine seulement ce qui a change : date et saint si le jour change, heure si la minute change
void redrawChanged(bool dayChanged, bool minuteChanged)
{
  // Meteo ou lune affichee : on ne touche pas a l'ecran
  if (state.showingWeather)
    return;

  // Changement de jour : date, saint et meteo changent, rafraichissement complet
  if (dayChanged)
  {
    drawAll();
    return;
  }

  // Changement de minute : seulement les chiffres de l'heure
  if (minuteChanged)
    drawTimePartial();
}

void tickOneSecond()
{
  if (!state.ntpReady || now() == 0)
    return;



  const uint8_t currentMinute = minute(now());
  const uint8_t currentDay = day(now());

  const bool minuteChanged = currentMinute != state.lastMinute;
  const bool dayChanged = currentDay != state.lastDay;

  state.lastMinute = currentMinute;
  state.lastDay = currentDay;
  redrawChanged(dayChanged, minuteChanged);
}

void handleTick()
{
  const unsigned long nowMs = millis();

  while (nowMs - state.previousTickMillis >= ONE_SECOND_MS)
  {
    state.previousTickMillis += ONE_SECOND_MS;
    tickOneSecond();
  }
}

bool shouldSyncNTP(unsigned long nowMs)
{
  if (state.forceNtpSync)
    return true;

  if (!state.ntpReady)
    return (nowMs - state.lastNtpAttemptMillis) >= NTP_RETRY_INTERVAL_MS;

  return (nowMs - state.lastNtpSyncMillis) >= NTP_SYNC_INTERVAL_MS;
}

// Lit la valeur numerique de la cle JSON "cle": a partir de la position from.
static bool jsonNumber(const String& body, const char* key, double& out, int from)
{
  const String pattern = String("\"") + key + "\":";
  const int p = body.indexOf(pattern, from);
  if (p < 0) return false;
  out = body.substring(p + pattern.length()).toFloat();
  return true;
}

// Recupere la temperature et le code meteo (Open-Meteo, bloc "current"). Retourne false en cas d'echec.
bool fetchWeather(int& tempC, int& code, int& humidity, int& windKmh)
{
  WiFiClientSecure client;
  client.setInsecure(); // certificat non verifie : suffisant pour une API publique non sensible
  HTTPClient http;
  http.setTimeout(8000);

  char url[300];
  snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m&timezone=Europe%%2FParis", cfg.lat, cfg.lon);
  if (!http.begin(client, url))
  {
    Serial.println("Meteo: initialisation HTTP impossible");
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != 200)
  {
    Serial.print("Meteo: HTTP ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();

  const int current = body.indexOf("\"current\"");
  if (current < 0) return false;

  double t = 0, c = 0, h = 0, w = 0;
  if (!jsonNumber(body, "temperature_2m", t, current) ||
      !jsonNumber(body, "weather_code", c, current) ||
      !jsonNumber(body, "relative_humidity_2m", h, current) ||
      !jsonNumber(body, "wind_speed_10m", w, current))
  {
    Serial.println("Meteo: reponse JSON inattendue");
    return false;
  }

  tempC = (int)round(t);
  code = (int)round(c);
  humidity = (int)round(h);
  windKmh = (int)round(w);

  Serial.print("Meteo OK: "); Serial.print(tempC); Serial.print(" C, code ");
  Serial.println(code);
  return true;
}

const char* weatherText(int code)
{
  if (code == 0) return "Ciel d\351gag\351";
  if (code <= 3) return "Nuageux";
  if (code == 45 || code == 48) return "Brouillard";
  if (code >= 51 && code <= 67) return "Pluie";
  if (code >= 71 && code <= 77) return "Neige";
  if (code >= 80 && code <= 82) return "Averses";
  if (code >= 95) return "Orage";
  return "Inconnu";
}

// =====================================================
// Icones dessinees a la main
// =====================================================
// Mise a l'echelle d'une dimension : s = 100 pour taille normale, 160 pour 1,6 fois plus grand
int sc(int v, int s)
{
  return (v * s + 50) / 100;
}

void drawCloudIcon(int cx, int cy, int s)
{
  display.fillCircle(cx + sc(-6, s), cy + sc(2, s), sc(5, s), GxEPD_BLACK);
  display.fillCircle(cx + sc(2, s), cy + sc(-1, s), sc(7, s), GxEPD_BLACK);
  display.fillCircle(cx + sc(9, s), cy + sc(3, s), sc(4, s), GxEPD_BLACK);
  display.fillRect(cx + sc(-6, s), cy + sc(2, s), sc(15, s), sc(5, s), GxEPD_BLACK);
}

// Trait epais (2 px) pour les icones meteo
void drawBoldLine(int x0, int y0, int x1, int y1, uint16_t color)
{
  display.drawLine(x0, y0, x1, y1, color);
  display.drawLine(x0 + 1, y0, x1 + 1, y1, color);
  display.drawLine(x0, y0 + 1, x1, y1 + 1, color);
}

void drawWeatherIcon(int cx, int cy, int code, int s)
{
  if (code == 0)
  {
    // Soleil en contour : deux cercles et rayons plus longs
    display.drawCircle(cx, cy, sc(7, s), GxEPD_BLACK);
    display.drawCircle(cx, cy, sc(6, s), GxEPD_BLACK);
    for (int k = 0; k < 8; ++k)
    {
      const double a = k * PI / 4.0;
      drawBoldLine(cx + (int)round(cos(a) * sc(10, s)), cy + (int)round(sin(a) * sc(10, s)),
                       cx + (int)round(cos(a) * sc(14, s)), cy + (int)round(sin(a) * sc(14, s)), GxEPD_BLACK);
    }
  }
  else if (code <= 3)
  {
    drawCloudIcon(cx, cy, s);
  }
  else if (code == 45 || code == 48)
  {
    for (int k = 0; k < 3; ++k)
      display.fillRect(cx - sc(10, s), cy + sc(-6 + k * 6, s), sc(20, s), 2, GxEPD_BLACK);
  }
  else if (code >= 71 && code <= 77)
  {
    drawCloudIcon(cx, cy - sc(4, s), s);
    display.fillCircle(cx + sc(-6, s), cy + sc(11, s), sc(2, s), GxEPD_BLACK);
    display.fillCircle(cx, cy + sc(13, s), sc(2, s), GxEPD_BLACK);
    display.fillCircle(cx + sc(6, s), cy + sc(11, s), sc(2, s), GxEPD_BLACK);
  }
  else if (code >= 95)
  {
    drawCloudIcon(cx, cy - sc(4, s), s);
    drawBoldLine(cx + sc(1, s), cy + sc(4, s), cx + sc(-3, s), cy + sc(10, s), GxEPD_BLACK);
    drawBoldLine(cx + sc(-3, s), cy + sc(10, s), cx + sc(2, s), cy + sc(10, s), GxEPD_BLACK);
    drawBoldLine(cx + sc(2, s), cy + sc(10, s), cx + sc(-2, s), cy + sc(15, s), GxEPD_BLACK);
  }
  else
  {
    drawCloudIcon(cx, cy - sc(4, s), s);
    for (int k = 0; k < 3; ++k)
      drawBoldLine(cx + sc(-6 + k * 6, s), cy + sc(9, s), cx + sc(-9 + k * 6, s), cy + sc(14, s), GxEPD_BLACK);
  }
}
// Soleil a l'horizon avec une fleche : haut = lever, bas = coucher
void drawSunIcon(int cx, int cy, int r, bool rising)
{
  for (int dy = 0; dy <= r; ++dy)
  {
    const int half = (int)sqrt((double)(r * r - dy * dy));
    display.drawFastHLine(cx - half, cy - dy, 2 * half + 1, GxEPD_BLACK);
  }
  display.drawFastHLine(cx - r - 3, cy + 1, 2 * r + 7, GxEPD_BLACK);

  const int ax = cx + r + 6;
  if (rising)
  {
    display.drawLine(ax, cy, ax, cy - 7, GxEPD_BLACK);
    display.drawLine(ax - 3, cy - 4, ax, cy - 7, GxEPD_BLACK);
    display.drawLine(ax + 3, cy - 4, ax, cy - 7, GxEPD_BLACK);
  }
  else
  {
    display.drawLine(ax, cy - 7, ax, cy, GxEPD_BLACK);
    display.drawLine(ax - 3, cy - 3, ax, cy, GxEPD_BLACK);
    display.drawLine(ax + 3, cy - 3, ax, cy, GxEPD_BLACK);
  }
}

// Disque de lune : phase 0 (nouvelle) a 7 (dernier croissant)
void drawMoonDisc(int cx, int cy, int r, int phase)
{
  display.drawCircle(cx, cy, r, GxEPD_BLACK);
  if (phase == 0)
    return;

  const double c = cos(2.0 * PI * phase / 8.0);
  for (int dy = -r; dy <= r; ++dy)
  {
    const double xr = sqrt((double)(r * r - dy * dy));
    const double xt = xr * c;

    // Un trait horizontal par ligne : moins d'appels graphiques que drawPixel().
    int xStart;
    int xEnd;
    if (phase <= 4)
    {
      xStart = (int)ceil(xt);
      xEnd = (int)floor(xr);
    }
    else
    {
      xStart = -(int)floor(xr);
      xEnd = (int)floor(-xt);
    }

    if (xEnd >= xStart)
      display.drawFastHLine(cx + xStart, cy + dy, xEnd - xStart + 1, GxEPD_BLACK);
  }
}
void drawWeather(int tempC, int code, int humidity, int wind, bool ok)
{
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    display.setFont(&Roboto_Bold_18);
    display.setCursor(H_MARGIN, DATE_TOP);
    display.print("M\351t\351o");
    display.drawFastHLine(H_MARGIN, 22, SCREEN_W - 2 * H_MARGIN, GxEPD_BLACK);

    if (ok)
    {
      drawWeatherIcon(225, 44, code, 160);

      display.setFont(&Roboto_Bold_64);
      display.setCursor(H_MARGIN, 96);
      display.print(tempC);

      // Symbole degre dessine a la main : anneau puis "C"
      const int xDeg = display.getCursorX() + 4;
      display.drawCircle(xDeg + 4, 54, 5, GxEPD_BLACK);
      display.drawCircle(xDeg + 4, 54, 4, GxEPD_BLACK);

      display.setFont(&Roboto_Bold_24);
      display.setCursor(xDeg + 12, 96);
      display.print("C");

      display.setFont(&Roboto_Bold_18);
      display.setCursor(170, 92);
      display.print(weatherText(code));

      display.setCursor(H_MARGIN, 120);
      if (humidity >= 0)
      {
        display.print("Humidit\351 ");
        display.print(humidity);
        display.print(" %");
      }
      else
      {
        display.print("Humidit\351 --");
      }

      display.setCursor(170, 120);
      display.print("Vent ");
      if (wind >= 0)
        display.print(wind);
      else
        display.print("--");
      display.print(" km/h");
    }
    else
    {
      display.setFont(&Roboto_Bold_18);
      display.setCursor(18, 76);
      display.print("M\351t\351o indisponible");
    }
  } while (display.nextPage());
}
void showWeather()
{
  int tempC = 0;
  int code = 0;
  int humidity = -1;
  int wind = -1;
  const bool ok = (WiFi.status() == WL_CONNECTED) && fetchWeather(tempC, code, humidity, wind);

  drawWeather(tempC, code, humidity, wind, ok);
  state.showingWeather = true;
  state.skyPage = false;
  state.weatherShownMillis = millis();
}

// Revient a l'horloge apres WEATHER_SHOW_MS
void handleWeatherTimeout()
{
  if (!state.showingWeather)
    return;

  if (millis() - state.weatherShownMillis < WEATHER_SHOW_MS)
    return;

  state.showingWeather = false;
  if (state.ntpReady)
    drawAll();
}

// =====================================================
// Page lune et soleil (bouton BOOT, deuxieme appui)
// =====================================================
SunriseSunset sun(43.636, 4.830, 1);  // remplace par cfg dans drawSkyPage
MoonPhase moon;

// Dimanche = 0 (formule de Sakamoto)
int dayOfWeekSunday0(int y, int m, int d)
{
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int lastSundayOf(int y, int m)
{
  for (int d = 31; d >= 25; --d)
    if (dayOfWeekSunday0(y, m, d) == 0) return d;
  return 25;
}

// Heure d'ete en Europe : du dernier dimanche de mars au dernier dimanche d'octobre
int utcOffsetFor(int y, int m, int d)
{
  if (m > 3 && m < 10) return 2;
  if (m < 3 || m > 10) return 1;
  if (m == 3) return (d >= lastSundayOf(y, 3)) ? 2 : 1;
  return (d < lastSundayOf(y, 10)) ? 2 : 1;
}

// Heure UTC -> heure locale (France) : le fuseau CET/CEST est appliqué ici.
// Heure d'ete : du dernier dimanche de mars 01:00 UTC au dernier dimanche d'octobre 01:00 UTC.
time_t localTimeFromUtc(time_t utc)
{
  const tm* g = gmtime(&utc);
  if (g == nullptr) return utc;

  const int y = g->tm_year + 1900;
  const int mo = g->tm_mon + 1;
  const int d = g->tm_mday;
  const int h = g->tm_hour;

  int off = 1;
  if (mo > 3 && mo < 10) off = 2;
  else if (mo == 3) off = (d > lastSundayOf(y, 3) || (d == lastSundayOf(y, 3) && h >= 1)) ? 2 : 1;
  else if (mo == 10) off = (d < lastSundayOf(y, 10) || (d == lastSundayOf(y, 10) && h < 1)) ? 2 : 1;

  return utc + off * 3600;
}
// Lever et coucher du soleil sous le saint (ecran principal)
void drawSunTimes()
{
  if (!state.ntpReady)
    return;

  const int y = year();
  const int mo = month();
  const int d = day();
  sun.setLocation(cfg.lat, cfg.lon);
  sun.setUtcOffset(utcOffsetFor(y, mo, d));

  const SunriseSunset::TimeHM rise = sun.getSunrise(y, mo, d);
  const SunriseSunset::TimeHM sunset = sun.getSunset(y, mo, d);
  char riseBuf[8] = "--:--";
  char setBuf[8] = "--:--";
  sun.formatTime(rise, riseBuf, sizeof(riseBuf));
  sun.formatTime(sunset, setBuf, sizeof(setBuf));

  display.setFont(&Roboto_Bold_18);
  display.setTextColor(GxEPD_BLACK);
  drawSunIcon(20, 194, 6, true);
  display.setCursor(44, 200);
  display.print(riseBuf);

  drawSunIcon(120, 194, 6, false);
  display.setCursor(144, 200);
  display.print(setBuf);
}

void drawSkyPage()
{
  const int y = year();
  const int mo = month();
  const int d = day();

  char riseBuf[8] = "--:--";
  char setBuf[8] = "--:--";
  char dayBuf[32] = "";
  const char* phaseName = "";
  int phase = 0;

  if (state.ntpReady)
  {
    sun.setLocation(cfg.lat, cfg.lon);
    sun.setUtcOffset(utcOffsetFor(y, mo, d));
    const SunriseSunset::TimeHM rise = sun.getSunrise(y, mo, d);
    const SunriseSunset::TimeHM sunset = sun.getSunset(y, mo, d);
    phaseName = moon.getMoonPhaseName(y, mo, d);
    phase = (int)moon.getPhase(y, mo, d);
    sun.formatTime(rise, riseBuf, sizeof(riseBuf));
    sun.formatTime(sunset, setBuf, sizeof(setBuf));

    if (rise.valid && sunset.valid)
    {
      const int minutes = (sunset.hour * 60 + sunset.minute) - (rise.hour * 60 + rise.minute);
      snprintf(dayBuf, sizeof(dayBuf), "Dur\351e du jour : %d h %02d", minutes / 60, minutes % 60);
    }
    else
    {
      snprintf(dayBuf, sizeof(dayBuf), "Dur\351e du jour : --");
    }
  }

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    display.setFont(&Roboto_Bold_18);
    display.setCursor(H_MARGIN, DATE_TOP);
    display.print("Lune et soleil");
    display.drawFastHLine(H_MARGIN, 22, SCREEN_W - 2 * H_MARGIN, GxEPD_BLACK);

    if (state.ntpReady)
    {
      drawMoonDisc(254, 62, 22, phase);

      display.setFont(&Roboto_Bold_18);
      display.setCursor(H_MARGIN, 50);
      display.print(phaseName);

      drawSunIcon(16, 76, 6, true);
      display.setCursor(36, 80);
      display.print(riseBuf);

      drawSunIcon(150, 76, 6, false);
      display.setCursor(172, 80);
      display.print(setBuf);

      display.setCursor(H_MARGIN, 104);
      display.print(dayBuf);
    }
    else
    {
      display.setFont(&Roboto_Bold_18);
      display.setCursor(H_MARGIN, 76);
      display.print("Heure indisponible");
    }
  } while (display.nextPage());
}
void showSky()
{
  drawSkyPage();
  state.showingWeather = true;
  state.skyPage = true;
  state.weatherShownMillis = millis();
}
// Appui court GP0 : meteo, puis lune et soleil, puis retour a l'horloge
void handleShortPress()
{
  if (!state.showingWeather)
  {
    Serial.println("Bouton : affichage meteo");
    showWeather();
  }
  else if (!state.skyPage)
  {
    Serial.println("Bouton : affichage lune et soleil");
    showSky();
  }
  else
  {
    Serial.println("Bouton : retour a l'horloge");
    state.showingWeather = false;
    state.skyPage = false;
    if (state.ntpReady)
    {
      state.forceNtpSync = true;
      drawAll();
    }
  }
}

// Appui maintenu SETUP_HOLD_MS = mode configuration Wi-Fi (ne revient pas)
void handleButton()
{
  static bool lastLevel = HIGH;
  static unsigned long changeMs = 0;
  static unsigned long pressMs = 0;
  static bool longDone = false;

  const bool level = digitalRead(BUTTON_PIN);
  const unsigned long nowMs = millis();

  if (level == LOW && lastLevel == HIGH && nowMs - changeMs > 50)
  {
    lastLevel = LOW;
    changeMs = nowMs;
    pressMs = nowMs;
    longDone = false;
    return;
  }

  if (level == LOW && !longDone && nowMs - pressMs >= SETUP_HOLD_MS)
  {
    longDone = true;
    enterSetupMode();
    return;
  }

  if (level == HIGH && lastLevel == LOW && nowMs - changeMs > 50)
  {
    lastLevel = HIGH;
    changeMs = nowMs;
    if (!longDone) handleShortPress();
  }
}

void handleNtpSync()
{
  const unsigned long nowMs = millis();
  if (!shouldSyncNTP(nowMs))
    return;

  state.lastNtpAttemptMillis = nowMs;
  state.forceNtpSync = false;

  // Pas de tentative NTP sans Wi-Fi : la reconnexion automatique du Wi-Fi
  // se fait en arriere-plan, et la prochaine tentative aura lieu apres le delai de nouvelle tentative
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Wi-Fi indisponible, synchro NTP reportee");
    return;
  }

  const bool wasReady = state.ntpReady;
  const time_t oldTime = now();

  if (!syncTimeFromNTP())
    return;

  if (!state.screenReady || !wasReady)
  {
    drawAll();
    return;
  }

  const bool dayChanged =
      day(oldTime) != day(now()) ||
      month(oldTime) != month(now()) ||
      year(oldTime) != year(now());

  const bool minuteChanged =
      hour(oldTime) != hour(now()) ||
      minute(oldTime) != minute(now());

  redrawChanged(dayChanged, minuteChanged);
}

// =====================================================
// Full refresh périodique
// =====================================================
bool shouldDoFullRefresh(unsigned long nowMs)
{
  if (!state.screenReady || state.showingWeather)
    return false;

  if (state.partialUpdates >= PARTIAL_UPDATES_BEFORE_FULL)
    return true;

  return (nowMs - state.lastFullRefreshMillis) >= FULL_REFRESH_INTERVAL_MS;
}

void handlePeriodicFullRefresh()
{
  const unsigned long nowMs = millis();
  if (!shouldDoFullRefresh(nowMs))
    return;

  Serial.println("Full refresh periodique de l'ecran");
  drawAll();
}

// =====================================================
// Message d'erreur
// =====================================================
void drawErrorMessage()
{
  display.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&Roboto_Bold_18);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(18, 46);
    display.print("Heure indisponible");
    display.setCursor(18, 76);
    display.print("V\351rifier Wi-Fi / NTP");
  } while (display.nextPage());
}

// =====================================================
// Setup / loop
// =====================================================
// Ecran d'information a 4 lignes
void showInfoScreen(const String& l1, const String& l2, const String& l3, const String& l4)
{
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&Roboto_Bold_24);
    display.setCursor(H_MARGIN, 60);
    display.print(l1);
    display.setFont(&Roboto_Bold_18);
    display.setCursor(H_MARGIN, 120);
    display.print(l2);
    display.setCursor(H_MARGIN, 160);
    display.print(l3);
    display.setCursor(H_MARGIN, 200);
    display.print(l4);
  } while (display.nextPage());
}

// Mode configuration : point d'acces + page HTML, redemarrage ensuite
void enterSetupMode()
{
  Serial.println("Mode configuration Wi-Fi");
  startWifiSetupAp(cfg);
  showInfoScreen("Configuration Wi-Fi",
                 String("Reseau : ") + AP_SSID,
                 String("Mot de passe : ") + AP_PASSWORD,
                 "Ouvrir http://" + WiFi.softAPIP().toString());
  waitWifiSetup(SETUP_TIMEOUT_MS);
  rp2040.restart();
}

// Icone epingle (ville)
void drawPinIcon(int cx, int cy)
{
  display.fillCircle(cx, cy, 10, GxEPD_BLACK);
  display.fillTriangle(cx - 10, cy + 3, cx + 10, cy + 3, cx, cy + 22, GxEPD_BLACK);
  display.fillCircle(cx, cy, 4, GxEPD_WHITE);
}

// Icone Wi-Fi (adresse de correction) : arcs pleins, moitie basse masquee
void drawWifiIcon(int cx, int cy)
{
  display.fillCircle(cx, cy, 18, GxEPD_BLACK);
  display.fillCircle(cx, cy, 14, GxEPD_WHITE);
  display.fillCircle(cx, cy, 11, GxEPD_BLACK);
  display.fillCircle(cx, cy, 7, GxEPD_WHITE);
  display.fillCircle(cx, cy, 4, GxEPD_BLACK);
  display.fillRect(cx - 20, cy + 1, 40, 22, GxEPD_WHITE);
  display.fillCircle(cx, cy + 6, 4, GxEPD_BLACK);
}

// Ecran de demarrage : ville detectee (avec region) et adresse pour la corriger
void drawCityInfoScreen(const String& url, const String& version)
{
  const String cityName = utf8ToLatin1(String(cfg.city));
  const String region = utf8ToLatin1(String(cfg.region));

  display.setFont(&Roboto_Bold_15);
  const TextMetrics vm = measureText(version.c_str(), &Roboto_Bold_15);

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    drawPinIcon(52, 62);
    display.setFont(&Roboto_Bold_24);
    display.setCursor(84, 74);
    display.print(cityName);
    if (region.length())
    {
      display.setFont(&Roboto_Bold_15);
      display.setCursor(84, 96);
      display.print(region);
    }

    display.drawFastHLine(40, 116, SCREEN_W - 80, GxEPD_BLACK);

    drawWifiIcon(52, 150);
    display.setFont(&Roboto_Bold_15);
    display.setCursor(84, 142);
    display.print("Corriger la ville sur :");
    display.setFont(&Roboto_Bold_24);
    display.setCursor(84, 170);
    display.print(url);

    display.setFont(&Roboto_Bold_15);
    display.setCursor(SCREEN_W - LEFT_X - (vm.x1 + vm.w), 262);
    display.print(version);
  } while (display.nextPage());
}

void setup()
{
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  Serial.println("================================");
  Serial.print("HORLOGE PICO W - version "); Serial.println(FIRMWARE_VERSION);
  Serial.println("================================");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  setupDisplaySPI();

  display.init(SERIAL_BAUDRATE);
  display.setRotation(1);

  bool fsOk = LittleFS.begin();
  if (!fsOk)
  {
    Serial.println("LittleFS absent : formatage");
    fsOk = LittleFS.format() && LittleFS.begin();
  }
  if (!fsOk)
  {
    Serial.println("ERREUR : LittleFS indisponible. Tools > Flash Size : choisir une option avec FS.");
    showInfoScreen("Erreur memoire", "LittleFS indisponible", "Tools > Flash Size :", "choisir une option avec FS");
    while (true) delay(1000);
  }
  const bool hasConfig = loadConfig(cfg);

  showSplashBitmap(SPLASH_DURATION_MS);

  // Pas de configuration ou Wi-Fi injoignable : page de configuration
  if (!(hasConfig && connectWiFiAndInitNTP()))
  {
    enterSetupMode();
  }

  webBeginStation(cfg);

  if (!cfg.locationSet && geoAutoDetect(cfg))
  {
    saveConfig(cfg);
  }

  if (initialSync())
  {
    refreshWeather();
    drawCityInfoScreen("http://" + WiFi.localIP().toString() + "/", String("Version ") + FIRMWARE_VERSION);
    const unsigned long t0 = millis();
    while (millis() - t0 < 8000UL)
    {
      webHandle();
      delay(5);
    }
    drawAll();
  }
  else
  {
    clearFullScreen();
    drawErrorMessage();
  }

  state.previousTickMillis = millis();
}

void loop()
{
  webHandle();
  handleButton();
  handleWeatherTimeout();
  handleWeatherRefresh();
  handleTick();
  handleNtpSync();
  handlePeriodicFullRefresh();
}
