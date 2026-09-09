#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"

bool sensorInit();
void sensorLoop();
void sensorSetTare();
void sensorResetTare();
void sensorResetCalibration();
float getPitchDeg();
float getRollDeg();
String getCalStatusString();

#endif // SENSOR_H
