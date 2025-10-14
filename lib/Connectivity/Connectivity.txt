#include <Connectivity.h>

// SIM900A config
#define GSM_RX_PIN 16  // ESP32 RX to SIM900A TX
#define GSM_TX_PIN 17  // ESP32 TX to SIM900A RX
#define GSM_BAUD 9600
HardwareSerial SerialAT(2);  // UART1 for SIM900A

GsmClient gsmClient(SerialAT);
// If you have your own subclass, use that instead.
const char* apn = "";  // Your carrier's APN
const char* gprsUser = "";                    // APN username
const char* gprsPass = "";                    // APN password

// MQTT config
const char* broker = "broker.hivemq.com";
const int mqttPort = 1883; // Non-SSL port
PubSubClient mqttClient;

WiFiClient wifiClient;

// Only define and initialize ONCE!
String activeConnection = "None";
long wifiRssi = -100;
int cellularCsq = 0;
bool mqttConnected = false;

// WiFi credentials (stored in NVS)
Preferences prefs;
String ssid = "";
String password = "";

// BLE config
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_SSID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHARACTERISTIC_UUID_PASS "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHARACTERISTIC_UUID_GPRS_APN "beb5483e-36e1-4688-b7f5-ea07361b26b7"
#define CHARACTERISTIC_UUID_GPRS_USER "beb5483e-36e1-4688-b7f5-ea07361b26b8"
#define CHARACTERISTIC_UUID_GPRS_PASS "beb5483e-36e1-4688-b7f5-ea07361b26b9"

BLEServer* pServer = NULL;
BLECharacteristic* pCharSSID = NULL;
BLECharacteristic* pCharPass = NULL;
BLECharacteristic* pCharGprsApn = NULL;
BLECharacteristic* pCharGprsUser = NULL;
BLECharacteristic* pCharGprsPass = NULL;

bool deviceConnected = false;
bool credentialsUpdated = false;
bool gprsCredentialsUpdated = false;
bool gsmActive = false;

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
      } else if (pCharacteristic->getUUID().equals(BLEUUID(CHARACTERISTIC_UUID_GPRS_APN))){
        apn = value.c_str();
        gprsCredentialsUpdated = true;
        saveGprsCredentials();
      } else if (pCharacteristic->getUUID().equals(BLEUUID(CHARACTERISTIC_UUID_GPRS_USER))){
        gprsUser = value.c_str();
        gprsCredentialsUpdated = true;
        saveGprsCredentials();
      } else if (pCharacteristic->getUUID().equals(BLEUUID(CHARACTERISTIC_UUID_GPRS_PASS))){
        gprsPass = value.c_str();
        gprsCredentialsUpdated = true;
        saveGprsCredentials();
      }
    }
}

void saveGprsCredentials() {
  prefs.begin("gprs", false);
  prefs.putString("apn", apn);
  prefs.putString("user", gprsUser);
  prefs.putString("pass", gprsPass);
  prefs.end();
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

  if (prefs.begin("wifi", true)) {
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    prefs.end();
    Serial.println("Loaded credentials: SSID=" + ssid + ", Pass=" + (password == "" ? "None" : "****"));
  }
}

void loadGprsCredentials() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  
  if (prefs.begin("gprs", true)) {
    apn = prefs.getString("apn", "").c_str();
    gprsUser = prefs.getString("user", "").c_str();
    gprsPass = prefs.getString("pass", "").c_str();
    prefs.end();
    Serial.println("Loaded GPRS credentials: APN=" + String(apn) + ", User=" + String(gprsUser) + ", Pass=" + (String(gprsPass) == "" ? "None" : "****"));
  } else {
    apn = "internet.ng.airtel.com";
    gprsUser = "internet";
    gprsPass = "internet";
  }
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
  pCharGprsApn = pService->createCharacteristic(CHARACTERISTIC_UUID_GPRS_APN, BLECharacteristic::PROPERTY_WRITE);
  pCharGprsApn->addDescriptor(new BLE2902());
  pCharGprsApn->setCallbacks(new MyCallbacks());
  pCharGprsUser = pService->createCharacteristic(CHARACTERISTIC_UUID_GPRS_USER, BLECharacteristic::PROPERTY_WRITE);
  pCharGprsUser->addDescriptor(new BLE2902());
  pCharGprsUser->setCallbacks(new MyCallbacks());
  pCharGprsPass = pService->createCharacteristic(CHARACTERISTIC_UUID_GPRS_PASS, BLECharacteristic::PROPERTY_WRITE);
  pCharGprsPass->addDescriptor(new BLE2902());
  pCharGprsPass->setCallbacks(new MyCallbacks());
  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();
}

bool connectWiFi() {
  if (ssid == "" || password == "") {
    wifiRssi = -100;
    Serial.println("WiFi: No credentials");
    return false;
  }
  esp_task_wdt_init((10000 / 1000), true); // Convert ms to seconds
  esp_task_wdt_add(NULL); // Add current task to WDT
  WiFi.begin(ssid.c_str(), password.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_task_wdt_reset(); // Feed the watchdog
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiRssi = WiFi.RSSI();
    Serial.println("WiFi connected, RSSI: " + String(wifiRssi));
    return true;
  }
  wifiRssi = -100;
  WiFi.disconnect();
  // Serial.println();
  Serial.println("WiFi connection failed");
  esp_task_wdt_deinit(); // Disable the WDT
  return false;
}

bool connectCellular() {
  if (!gsmActive) {
    SerialAT.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    String response;
    response = gsmClient.sendCustomAT("AT");
    Serial.println(response);
    bool found = false;
    if (response.indexOf("OK") >= 0) {
      found = true;
    }
    if (!found) {
      Serial.println("Cellular: Modem init failed");
      return false;
    }
    if (!gsmClient.resetModem()) {
      Serial.println("Cellular: Modem reset failed");
      return false;
    }
    // response = gsmClient.sendCustomAT("AT+CGMR");
    // if(response.length() > 0){
    //   Serial.println(response);
    // }

  }

  cellularCsq = gsmClient.csq();

  if (cellularCsq > 0 && cellularCsq <= 31 && cellularCsq != 99) {
    Serial.println("Cellular connected, CSQ: " + String(cellularCsq));
    gsmActive = true;
    return true;
  } else {
    Serial.println("Cellular connection failed");
    gsmActive = false;
    return false;
  }
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
    // cellularCsq = modem.getSignalQuality();
    // Serial.println("Cellular connected, CSQ: " + String(cellularCsq));
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

  if (deviceConnected && gprsCredentialsUpdated) {
    gprsCredentialsUpdated = false;
    gsmActive = false;
    gsmClient.resetModem();
    activeConnection = "None";
    mqttConnected = false;
    Serial.println("GPRS: GPRS Credentials updated, resetting connection");
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
  SerialAT.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  loadCredentials();
  loadGprsCredentials();
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
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void sendDataToMQTT(const String& data) {
  if (activeConnection != "None" && mqttClient.connected()) {
    static unsigned long lastPublish = 0;
    if (millis() - lastPublish >= 10000) {
      // String payload = "Uptime: " + String(millis() / 1000) + "s, Data: " + data;
      mqttClient.publish(publishTopic, data.c_str());
      Serial.println("Published to " + String(publishTopic) + ": " + data);
      lastPublish = millis();
    }
  } else {
    mqttConnected = false;
  }
}