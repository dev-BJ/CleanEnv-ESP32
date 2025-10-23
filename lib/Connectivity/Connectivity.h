#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

#include <Arduino.h>
#include <WiFi.h>
// #include <WiFiClientSecure.h>
#include <nvs_flash.h>
#include <PubSubClient.h>
#define TINY_GSM_MODEM_SIM900
#include <TinyGsmClient.h>
// #include <SSLClient.h>
// #include <TinyGsmClientSecure.h>
#include <HardwareSerial.h>
#include <NimBLEDevice.h>
// #include <BLEServer.h>
// #include <BLEUtils.h>
// #include <BLE2902.h>
#include <Preferences.h>
// #include <GsmClient.h>
#include <esp_task_wdt.h>
// #include "CACerts.h"
// #include "esp32_cert_bundle.h"

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer);
  void onDisconnect(BLEServer* pServer);
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic);
};

void saveCredentials();
void loadCredentials();
void saveGprsCredentials();
void loadGprsCredentials();
void setupBLE();
bool connectWiFi();
bool connectCellular();
void connectMQTT();
// void updateDisplay();
void monitorConnectivity();
void monitorConnectivityTask(void *pvParameters);
void sendDataToMQTT(const char* data);

// Only declare, do NOT initialize here!
// extern String activeConnection;
// extern long wifiRssi;
// extern int cellularCsq;
// extern bool mqttConnected;

// extern WiFiClientSecure wifiClient;

struct Status {
    String activeConnection = "None";
    long wifiRssi = -100;
    int16_t cellularCsq = 0;
    bool mqttConnected = false;
    bool bleDeviceConnected = false;
    bool wifiCredentialsUpdated = false;
    bool gprsCredentialsUpdated = false;
    bool gsmActive = false;
    bool switchNetwork = false;
    char lastReceivedMessage[256] = "None";
};

struct Config {
    char ssid[32];
    char password[32];
    char apn[64];
    char gprsUser[16];
    char gprsPass[16];
    // const char* broker = "sb52131d.ala.eu-central-1.emqxsl.com";
    const char* broker = "broker.hivemq.com";
    const int mqttPort = 1883;
    // const int mqttPort = 8883;
    const char* clientId = "CleanEnvClient";
    const char* mqttUsername = "cleanenv";
    const char* mqttPassword = "cleanenvpass";
    const char* subscribeTopic = "cleanenv/stdin";
    const char* publishTopic = "cleanenv/stdout";
};

extern Config config;
extern Status status;
extern PubSubClient mqttClient;

#endif // CONNECTIVITY_H