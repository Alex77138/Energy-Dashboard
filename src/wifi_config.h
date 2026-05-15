#pragma once
#include <Arduino.h>

// Charge les credentials sauvegardés en mémoire NVS.
// Retourne true si un SSID existe.
bool wifi_config_load(String &ssid, String &pass);

// Affiche l'écran de sélection WiFi (scan + clavier tactile).
// Bloque jusqu'à ce que la connexion réussisse.
void wifi_config_run();
