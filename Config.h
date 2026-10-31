#pragma once
#include <Arduino.h>

// Reglages persistants (flash LittleFS, fichier /config.txt)
struct AppConfig
{
  char ssid[33] = "";
  char pass[65] = "";
  char city[64] = "Saint-Martin-de-Crau";                // nom de la ville
  char region[128] = "Provence-Alpes-Côte d'Azur";       // region, sans le pays
  double lat = 43.636;
  double lon = 4.830;
  bool locationSet = false;                              // true une fois la ville detectee ou choisie
};

// Retourne true si un SSID Wi-Fi est enregistre
bool loadConfig(AppConfig& cfg);
bool saveConfig(const AppConfig& cfg);
