#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <Arduino.h>
#include "config.h"

void mqttInit();
void mqttReconfigure();
void mqttLoop();
bool mqttIsConnected();
void mqttPublishDiscovery();
void mqttPublishState();

#endif // MQTT_HA_H

