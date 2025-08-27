#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <max6675.h>  // Install via Library Manager: "MAX6675 library
#include <SPI.h>

void setupSensors();
void monitorSensors();
void monitorSensorsTask(void *pvParameters);
void selectMuxChannel(int channel);

extern float voltage;
extern float current;
extern float temperature;

#endif // SENSORS_H