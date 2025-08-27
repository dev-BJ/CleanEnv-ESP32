#include <Connectivity.h>
#include "esp_task_wdt.h"

// Only define and initialize ONCE!
String activeConnection = "None";
long wifiRssi = -100;
int cellularCsq = 0;
bool mqttConnected = false;

// SIM900A config
#define GSM_RX_PIN 16  // ESP32 RX to SIM900A TX
#define GSM_TX_PIN 17  // ESP32 TX to SIM900A RX
#define GSM_BAUD 9600
HardwareSerial SerialAT(2);  // UART1 for SIM900A
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);
String apn = "internet.ng.airtel.com";  // Your carrier's APN
String user = "internet";                    // APN username
String pass = "internet";                    // APN password

// MQTT config
const char* broker = "test.mosquitto.org";
const int mqttPort = 1883;
PubSubClient mqttClient;

WiFiClient wifiClient;

// WiFi credentials (stored in NVS)
Preferences prefs;
String ssid = "";
String password = "";

// BLE config
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_SSID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHARACTERISTIC_UUID_PASS "beb5483e-36e1-4688-b7f5-ea07361b26a9"
BLEServer* pServer = NULL;
BLECharacteristic* pCharSSID = NULL;
BLECharacteristic* pCharPass = NULL;
bool deviceConnected = false;
bool credentialsUpdated = false;

// MQTT topics
const char* subscribeTopic = "esp32/input";
const char* publishTopic = "esp32/output";

// MQTT status and received message
char lastReceivedMessage[128] = "None"; // Fixed-size buffer
bool switchNetwork = false;

// Signal strength thresholds
const int WIFI_RSSI_THRESHOLD = -70; // dBm
const int CELLULAR_CSQ_THRESHOLD = 10; // 0-31 scale

void MyServerCallbacks::onConnect(BLEServer* pServer) { deviceConnected = true; }
void MyServerCallbacks::onDisconnect(BLEServer* pServer) { deviceConnected = false; }

void MyCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
  std::string value = pCharacteristic->getValue();
    if (value.length() > 0 && value.length() < 128) { // Limit input size
      if (pCharacteristic->getUUID().equals(BLEUUID(CHARACTERISTIC_UUID_SSID))) {
        ssid = value.c_str();
      } else if (pCharacteristic->getUUID().equals(BLEUUID(CHARACTERISTIC_UUID_PASS))) {
        password = value.c_str();
        credentialsUpdated = true;
        saveCredentials();
      }
    }
}

void saveCredentials() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
}

void loadCredentials() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");
  prefs.end();
  Serial.println("Loaded credentials: SSID=" + ssid + ", Pass=" + (password == "" ? "None" : "****"));
}

void setupBLE() {
  BLEDevice::init("CleanEnv ESP32 Provisioner");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharSSID = pService->createCharacteristic(CHARACTERISTIC_UUID_SSID, BLECharacteristic::PROPERTY_WRITE);
  pCharSSID->addDescriptor(new BLE2902());
  pCharSSID->setCallbacks(new MyCallbacks());
  pCharPass = pService->createCharacteristic(CHARACTERISTIC_UUID_PASS, BLECharacteristic::PROPERTY_WRITE);
  pCharPass->addDescriptor(new BLE2902());
  pCharPass->setCallbacks(new MyCallbacks());
  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();
}

bool connectWiFi() {
  if (ssid == "" || password == "") {
    wifiRssi = -100;
    Serial.println("WiFi: No credentials");
    return false;
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    // esp_task_wdt_reset(); // Feed the watchdog
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiRssi = WiFi.RSSI();
    Serial.println("WiFi connected, RSSI: " + String(wifiRssi));
    return true;
  }
  wifiRssi = -100;
  WiFi.disconnect();
  Serial.println("WiFi connection failed");
  return false;
}

bool connectCellular() {
  SerialAT.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);

  // Check if SIM900A responds to "AT"
  SerialAT.println("AT");
  unsigned long start = millis();
  bool found = false;
  while (millis() - start < 1000) { // 1 second timeout
    if (SerialAT.available()) {
      String resp = SerialAT.readString();
      if (resp.indexOf("OK") >= 0) {
        found = true;
        break;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
    esp_task_wdt_reset();
  }
  if (!found) {
    cellularCsq = 0;
    Serial.println("Cellular: SIM900A not detected");
    return false;
  }

  if (!modem.restart()) {
    cellularCsq = 0;
    Serial.println("Cellular: Modem restart failed");
    return false;
  }
  esp_task_wdt_reset(); // Feed the watchdog

  if (!modem.gprsConnect(apn.c_str(), user.c_str(), pass.c_str())) {
    cellularCsq = 0;
    Serial.println("Cellular: GPRS connection failed");
    return false;
  }
  esp_task_wdt_reset(); // Feed the watchdog

  cellularCsq = modem.getSignalQuality();
  Serial.println("Cellular connected, CSQ: " + String(cellularCsq));
  return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (length >= sizeof(lastReceivedMessage)) {
    length = sizeof(lastReceivedMessage) - 1; // Prevent buffer overflow
  }
  strncpy(lastReceivedMessage, (char*)payload, length);
  lastReceivedMessage[length] = '\0'; // Null-terminate
  Serial.println("Received on " + String(topic) + ": " + String(lastReceivedMessage));
}

void connectMQTT() {
  mqttClient.setServer(broker, mqttPort);
  mqttClient.setCallback(mqttCallback);
  if (activeConnection == "WiFi") {
    wifiRssi = WiFi.RSSI();
    Serial.println("WiFi connected, RSSI: " + String(wifiRssi));
    mqttClient.setClient(wifiClient);
  } else if (activeConnection == "Cellular") {
    cellularCsq = modem.getSignalQuality();
    Serial.println("Cellular connected, CSQ: " + String(cellularCsq));
    mqttClient.setClient(gsmClient);
  }
  if (mqttClient.connect("ESP32Client")) {
    mqttConnected = true;
    mqttClient.subscribe(subscribeTopic);
    Serial.println("MQTT Connected, Subscribed to: " + String(subscribeTopic));
  } else {
    mqttConnected = false;
    Serial.println("MQTT Connection Failed");
  }
}


void monitorConnectivity() {
  // Handle BLE updates
  if (deviceConnected && credentialsUpdated) {
    credentialsUpdated = false;
    WiFi.disconnect();
    activeConnection = "None";
    mqttConnected = false;
    Serial.println("BLE: Credentials updated, resetting connection");
  }

  // Check signal strengths every 5 seconds
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 5000 || activeConnection == "None") {
    lastCheck = millis();
    bool wifiAvailable = connectWiFi();
    bool cellularAvailable = !wifiAvailable ? connectCellular() : false;

    if (wifiAvailable && wifiRssi > WIFI_RSSI_THRESHOLD) {
      if (activeConnection != "WiFi") {
        activeConnection = "WiFi";
        switchNetwork = true;
        Serial.println("Switching to WiFi (stronger signal)");
      }
    } else if (cellularAvailable && cellularCsq > CELLULAR_CSQ_THRESHOLD) {
      if (activeConnection != "Cellular") {
        activeConnection = "Cellular";
        switchNetwork = true;
        Serial.println("Switching to Cellular (stronger signal)");
      }
    } else if (wifiAvailable) {
      if (activeConnection != "WiFi") {
        activeConnection = "WiFi";
        switchNetwork = true;
        Serial.println("Switching to WiFi (fallback)");
      }
    } else if (cellularAvailable) {
      if (activeConnection != "Cellular") {
        activeConnection = "Cellular";
        switchNetwork = true;
        Serial.println("Switching to Cellular (fallback)");
      }
    } else {
      activeConnection = "None";
      mqttConnected = false;
      Serial.println("No network available");
    }
  }
}

void monitorConnectivityTask(void *pvParameters) {
  loadCredentials();
  setupBLE();

  while (1) {
    // UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    // Serial.printf("MonitorConnectivity stack remaining: %d bytes\n", stackHighWaterMark * sizeof(StackType_t));
    // Serial.printf("MonitorConnecti priority: %d\n", uxTaskPriorityGet(NULL));
    monitorConnectivity();
    if (activeConnection != "None") {
      if (switchNetwork || !mqttClient.connected()) {
        connectMQTT();
        switchNetwork = false;
      } else {
        mqttClient.loop();
      }
    } else {
      mqttConnected = false;
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void sendDataToMQTT(const String& data) {
  if (activeConnection != "None" && mqttClient.connected()) {
    static unsigned long lastPublish = 0;
    if (millis() - lastPublish >= 10000) {
      String payload = "Uptime: " + String(millis() / 1000) + "s, Data: " + data;
      mqttClient.publish(publishTopic, payload.c_str());
      Serial.println("Published to " + String(publishTopic) + ": " + payload);
      lastPublish = millis();
    }
  } else {
    mqttConnected = false;
  }
}