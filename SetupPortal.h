#pragma once
#include <Arduino.h>
#include "Config.h"

// Point d'acces de configuration (mot de passe fixe : a changer ici si besoin)
static constexpr char AP_SSID[] = "Horloge-Setup";
static constexpr char AP_PASSWORD[] = "horloge2026";
static constexpr unsigned long SETUP_TIMEOUT_MS = 600000UL;  // 10 min, puis redemarrage

// Mode point d'acces : scanne les reseaux puis ouvre le point d'acces et le serveur web.
void startWifiSetupAp(AppConfig& cfg);
// Attend l'enregistrement. Retourne true si enregistre, false si delai depasse.
bool waitWifiSetup(unsigned long timeoutMs);

// Mode client (reseau domestique) : page de correction de la ville.
void webBeginStation(AppConfig& cfg);
void webHandle();
