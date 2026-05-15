#pragma once
#include "types.h"

// Lecture /ajax_data — remplit RouterData (puissance routée, énergie, état forcé)
// et optionnellement GridData si le Shelly EM du routeur est en ligne
bool f1atb_fetch(const char *host, RouterData &out, ShellyEMData *grid_out = nullptr);

// Commande marche forcée — URL à confirmer (config.h)
bool f1atb_set_forced(const char *host, const char *url_on, const char *url_off, bool on);
