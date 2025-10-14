#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <max6675.h>
#include <esp_task_wdt.h>

// Structure to hold sensor readings
struct SensorData {
  float voltage;
  float current;
  float temperature;
};

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

// Global object (shared across files)
extern SensorData sensorData;
extern JsonDocument doc;

// Setup and control functions
void setupSensors();
void monitorSensors();
void monitorSensorsTask(void *pvParameters);

#endif
