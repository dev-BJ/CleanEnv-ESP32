#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <PubSubClient.h>
#define TINY_GSM_MODEM_SIM900
#include <TinyGsmClient.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer);
  void onDisconnect(BLEServer* pServer);
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic);
};

void saveCredentials();
void loadCredentials();
void setupBLE();
bool connectWiFi();
bool connectCellular();
void connectMQTT();
// void updateDisplay();
void monitorConnectivity();
void monitorConnectivityTask(void *pvParameters);
void sendDataToMQTT(const String& data);

// Only declare, do NOT initialize here!
extern String activeConnection;
extern long wifiRssi;
extern int cellularCsq;
extern bool mqttConnected;

#endif // CONNECTIVITY_H