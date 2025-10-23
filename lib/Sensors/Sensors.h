#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
// #include <ArduinoJson.h>
#include "SimpleJson.h"
#include <max6675.h>
#include <esp_task_wdt.h>
#include <vector>

enum SensorType {
  SENSOR_VOLTAGE,
  SENSOR_CURRENT,
  SENSOR_TEMPERATURE,
  SENSOR_GENERIC
};

struct SensorConfig {
  int channel;
  SensorType type;
  const char* name;
};

// struct SensorReadingsConfig {
//   const char* name;
//   char* variable;
// };

// Structure to hold sensor readings
// struct SensorData {
//   float voltage;
//   float current;
//   float temperature;
// };

// Global object (shared across files)
// extern SensorData sensorData;
// extern JsonDocument doc;
extern SimpleJson data;

// Setup and control functions
void setupSensors();
void monitorSensors();
void monitorSensorsTask(void *pvParameters);

#endif
