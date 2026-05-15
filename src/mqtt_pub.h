#pragma once
#include "types.h"

struct MQTTConfig {
    bool     enabled       = false;
    char     host[64]      = "";
    uint16_t port          = 1883;
    char     user[32]      = "";
    char     pass[32]      = "";
    char     base_topic[32]= "dashenergy";
    bool     ha_discovery  = true;
};

extern MQTTConfig g_mqtt;

void mqtt_config_load();
void mqtt_config_save(const MQTTConfig &cfg);
void mqtt_init();
void mqtt_loop(const AppData &d);  // appeler depuis poll_task
