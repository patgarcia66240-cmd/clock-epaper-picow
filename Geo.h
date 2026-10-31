#pragma once
#include <Arduino.h>
#include "Config.h"

struct GeoPlace
{
  String label;   // ex. "Arles, Bouches-du-Rhone (France)"
  String name;    // ex. "Arles"
  String region;  // ex. "Région Provence-Alpes-Côte d'Azur"
  double lat = 0;
  double lon = 0;
};

// Detection automatique de la ville par IP (ipapi.co, HTTPS, sans cle). Met a jour cfg si succes.
bool geoAutoDetect(AppConfig& cfg);

// Recherche de villes (Open-Meteo geocoding). Retourne le nombre de resultats (<= maxCount).
int geoSearchPlaces(const String& query, GeoPlace* out, int maxCount);

// Convertit de l'UTF-8 vers le Latin-1 utilise par les polices GFX de l'ecran (accents).
String utf8ToLatin1(const String& in);
