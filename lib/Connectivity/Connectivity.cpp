#include <Connectivity.h>

#define DEBUG 1

// SIM900A config
#define GSM_RX_PIN 16  // ESP32 RX to SIM900A TX
#define GSM_TX_PIN 17  // ESP32 TX to SIM900A RX
#define GSM_BAUD 9600

// BLE Service & Characteristics
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_SSID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHARACTERISTIC_UUID_PASS "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHARACTERISTIC_UUID_GPRS_APN "beb5483e-36e1-4688-b7f5-ea07361b26b7"
#define CHARACTERISTIC_UUID_GPRS_USER "beb5483e-36e1-4688-b7f5-ea07361b26b8"
#define CHARACTERISTIC_UUID_GPRS_PASS "beb5483e-36e1-4688-b7f5-ea07361b26b9"

// Signal strength thresholds
const int WIFI_RSSI_THRESHOLD = -70; // dBm
const int CELLULAR_CSQ_THRESHOLD = 10; // 0-31 scale

// --- Global Objects ---
HardwareSerial SerialAT(2);  // UART1 for SIM900A
// GsmClient gsmClient(SerialAT);
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);

PubSubClient mqttClient;
WiFiClient wifiClient;
Preferences prefs;

// Single instances for BLE callbacks to prevent memory leaks
MyServerCallbacks serverCallbacks;
MyCallbacks characteristicCallbacks;

// --- State Management Structs ---
struct Config {
    String ssid;
    String password;
    String apn;
    String gprsUser;
    String gprsPass;
    // const char* broker = "sb52131d.ala.eu-central-1.emqxsl.com";
    const char* broker = "broker.hivemq.com";
    const int mqttPort = 1883;
    const char* clientId = "CleanEnvClient";
    const char* mqttUsername = "cleanenv";
    const char* mqttPassword = "cleanenvpass";
    const char* subscribeTopic = "cleanenv/stdin";
    const char* publishTopic = "cleanenv/stdout";
} config;

struct BleHandles {
    BLEServer* pServer = nullptr;
    BLECharacteristic* pCharSSID = nullptr;
    BLECharacteristic* pCharPass = nullptr;
    BLECharacteristic* pCharGprsApn = nullptr;
    BLECharacteristic* pCharGprsUser = nullptr;
    BLECharacteristic* pCharGprsPass = nullptr;
} ble;

// Define the global status object here, as declared in the header
Status status;

// --- BLE Callback Implementations ---
void MyServerCallbacks::onConnect(BLEServer* pServer) { status.bleDeviceConnected = true; }
void MyServerCallbacks::onDisconnect(BLEServer* pServer) { status.bleDeviceConnected = false; }

void MyCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
  std::string value = pCharacteristic->getValue();
    if (value.length() > 0 && value.length() < 128) { // Limit input size
      BLEUUID uuid = pCharacteristic->getUUID();
      if (uuid.equals(BLEUUID(CHARACTERISTIC_UUID_SSID))) {
        config.ssid = value.c_str();
      } else if (uuid.equals(BLEUUID(CHARACTERISTIC_UUID_PASS))) {
        config.password = value.c_str();
        status.wifiCredentialsUpdated = true;
        saveCredentials();
      } else if (uuid.equals(BLEUUID(CHARACTERISTIC_UUID_GPRS_APN))){
        config.apn = value.c_str();
        status.gprsCredentialsUpdated = true;
        saveGprsCredentials();
      } else if (uuid.equals(BLEUUID(CHARACTERISTIC_UUID_GPRS_USER))){
        config.gprsUser = value.c_str();
        status.gprsCredentialsUpdated = true;
        saveGprsCredentials();
      } else if (uuid.equals(BLEUUID(CHARACTERISTIC_UUID_GPRS_PASS))){
        config.gprsPass = value.c_str();
        status.gprsCredentialsUpdated = true;
        saveGprsCredentials();
      }
    }
}

// --- NVS (Storage) Functions ---
void saveGprsCredentials() {
  prefs.begin("gprs", false);
  prefs.putString("apn", config.apn);
  prefs.putString("user", config.gprsUser);
  prefs.putString("pass", config.gprsPass);
  prefs.end();
}

void saveCredentials() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", config.ssid);
  prefs.putString("pass", config.password);
  prefs.end();
}

void loadCredentials() {
  if (prefs.begin("wifi", true)) {
    config.ssid = prefs.getString("ssid", "");
    config.password = prefs.getString("pass", "");
    prefs.end();
    if (DEBUG) Serial.println("Loaded credentials: SSID=" + config.ssid + ", Pass=" + (config.password == "" ? "None" : "****"));
  }
}

void loadGprsCredentials() {
  if (prefs.begin("gprs", true)) {
    config.apn = prefs.getString("apn", "internet.ng.airtel.com");
    config.gprsUser = prefs.getString("user", "internet");
    config.gprsPass = prefs.getString("pass", "internet");
    prefs.end();
    if (DEBUG) Serial.println("Loaded GPRS credentials: APN=" + config.apn + ", User=" + config.gprsUser + ", Pass=" + (config.gprsPass == "" ? "None" : "****"));
  }
}

// Helper to reduce BLE characteristic creation boilerplate
static BLECharacteristic* createCharacteristic(BLEService* pService, const char* uuid, BLECharacteristicCallbacks* callbacks) {
    BLECharacteristic* pCharacteristic = pService->createCharacteristic(
        uuid,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
    );
    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setCallbacks(callbacks);
    return pCharacteristic;
}

// --- Setup and Connection Logic ---
void setupBLE() {
  BLEDevice::init("CleanEnv ESP32 Provisioner");
  ble.pServer = BLEDevice::createServer();
  ble.pServer->setCallbacks(&serverCallbacks);
  BLEService* pService = ble.pServer->createService(SERVICE_UUID);

  ble.pCharSSID = createCharacteristic(pService, CHARACTERISTIC_UUID_SSID, &characteristicCallbacks);
  ble.pCharPass = createCharacteristic(pService, CHARACTERISTIC_UUID_PASS, &characteristicCallbacks);
  ble.pCharGprsApn = createCharacteristic(pService, CHARACTERISTIC_UUID_GPRS_APN, &characteristicCallbacks);
  ble.pCharGprsUser = createCharacteristic(pService, CHARACTERISTIC_UUID_GPRS_USER, &characteristicCallbacks);
  ble.pCharGprsPass = createCharacteristic(pService, CHARACTERISTIC_UUID_GPRS_PASS, &characteristicCallbacks);

  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();
}

bool connectWiFi() {
  if (config.ssid == "" || config.password == "") {
    status.wifiRssi = -100;
    if (DEBUG) Serial.println("WiFi: No credentials");
    return false;
  }
  
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  int attempts = 0;
  // Wait for connection, but not indefinitely. 30 * 200ms = 6 seconds.
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    vTaskDelay(200 / portTICK_PERIOD_MS);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    status.wifiRssi = WiFi.RSSI();
    if (DEBUG) Serial.println("WiFi connected, RSSI: " + String(status.wifiRssi));
    return true;
  }
  status.wifiRssi = -100;
  WiFi.disconnect();
  // Serial.println();
  if (DEBUG) Serial.println("WiFi connection failed");
  return false;
}

bool connectCellular() {
  if (!status.gsmActive) {
    // gsmClient.beginWdt(10000);
    
    // gsmClient.sendAT("AT");
    bool found = modem.init();
    esp_task_wdt_reset();
    // bool found = gsmClient.waitResponse("OK", 2000);
    if (DEBUG) Serial.println(found ? "Cellular: Modem detected" : "Cellular: No modem response");
    if (!found) {
      if (DEBUG) Serial.println("Cellular: Modem init failed");
      // gsmClient.disableWdt();
      return false;
    }
    // gsmClient.disableWdt();
    // !gsmClient.resetModem()
    if (modem.waitForNetwork(5000L) != 1) {
      esp_task_wdt_reset(); // Reset WDT after potentially long wait
      if (DEBUG) Serial.println("Cellular: Network connection failed");
      return false;
    }
    // status.cellularCsq = gsmClient.csq();
    status.cellularCsq = modem.getSignalQuality();


    // gsmClient.setGprsCredentials(config.apn.c_str(), config.gprsUser.c_str(), config.gprsPass.c_str());
    if(modem.gprsConnect(config.apn.c_str(), config.gprsUser.c_str(), config.gprsPass.c_str()) != 1) {
      esp_task_wdt_reset();
      if (DEBUG) Serial.println("Cellular: GPRS connection failed");
      return false;
    }
  }
  
    if (status.cellularCsq > 0 && status.cellularCsq <= 32 && status.cellularCsq != 99) {
      if (DEBUG) Serial.println("Cellular connected, CSQ: " + String(status.cellularCsq));
      status.gsmActive = true;
      return true;
    } else {
      if (DEBUG) Serial.println("Cellular connection failed");
      status.gsmActive = false;
      return false;
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (length >= sizeof(status.lastReceivedMessage)) {
    length = sizeof(status.lastReceivedMessage) - 1; // Prevent buffer overflow
  }
  strncpy(status.lastReceivedMessage, (char*)payload, length);
  status.lastReceivedMessage[length] = '\0'; // Null-terminate
  if (DEBUG) Serial.println("Received on " + String(topic) + ": " + String(status.lastReceivedMessage));
}

void connectMQTT() {
  mqttClient.setServer(config.broker, config.mqttPort);
  mqttClient.setCallback(mqttCallback);
  if (status.activeConnection == "WiFi") {
    status.wifiRssi = WiFi.RSSI();
    if (DEBUG) Serial.println("WiFi connected, RSSI: " + String(status.wifiRssi));
    mqttClient.setClient(wifiClient);
  } else if (status.activeConnection == "Cellular") {
    // cellularCsq = modem.getSignalQuality();
    // Serial.println("Cellular connected, CSQ: " + String(cellularCsq));
    mqttClient.setClient(gsmClient);
  }
  if (mqttClient.connect(config.clientId, config.mqttUsername, config.mqttPassword)) {
    status.mqttConnected = true;
    mqttClient.subscribe(config.subscribeTopic);
    if (DEBUG) Serial.println("MQTT Connected, Subscribed to: " + String(config.subscribeTopic));
  } else {
    status.mqttConnected = false;
    if (DEBUG) Serial.println("MQTT Connection Failed");
  }
}

void monitorConnectivity() {
  // Handle BLE updates
  if (status.bleDeviceConnected && status.wifiCredentialsUpdated) {
    status.wifiCredentialsUpdated = false;
    WiFi.disconnect();
    status.activeConnection = "None";
    status.mqttConnected = false;
    if (DEBUG) Serial.println("BLE: Credentials updated, resetting connection");
  }

  if (status.bleDeviceConnected && status.gprsCredentialsUpdated) {
    status.gprsCredentialsUpdated = false;
    status.gsmActive = false;
    // gsmClient.resetModem();
    modem.restart();
    status.activeConnection = "None";
    status.mqttConnected = false;
    if (DEBUG) Serial.println("GPRS: GPRS Credentials updated, resetting connection");
  }

  // Check signal strengths every 5 seconds
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 5000 || status.activeConnection == "None") {
    bool wifiAvailable = connectWiFi();
    esp_task_wdt_reset(); // Reset WDT after WiFi check
    bool cellularAvailable = !wifiAvailable ? connectCellular() : false;
    esp_task_wdt_reset(); // Reset WDT after Cellular check
    
    if (wifiAvailable && status.wifiRssi > WIFI_RSSI_THRESHOLD) {
      if (status.activeConnection != "WiFi") {
        status.activeConnection = "WiFi";
        status.switchNetwork = true;
        if (DEBUG) Serial.println("Switching to WiFi (stronger signal)");
      }
    } else if (cellularAvailable && status.cellularCsq > CELLULAR_CSQ_THRESHOLD) {
      if (status.activeConnection != "Cellular") {
        status.activeConnection = "Cellular";
        status.switchNetwork = true;
        if (DEBUG) Serial.println("Switching to Cellular (stronger signal)");
      }
    } else if (wifiAvailable) {
      if (status.activeConnection != "WiFi") {
        status.activeConnection = "WiFi";
        status.switchNetwork = true;
        if (DEBUG) Serial.println("Switching to WiFi (fallback)");
      }
    } else if (cellularAvailable) {
      if (status.activeConnection != "Cellular") {
        status.activeConnection = "Cellular";
        status.switchNetwork = true;
        if (DEBUG) Serial.println("Switching to Cellular (fallback)");
      }
    } else {
      status.activeConnection = "None";
      status.mqttConnected = false;
      if (DEBUG) Serial.println("No network available");
    }
    lastCheck = millis();
  }
}

void initNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

void monitorConnectivityTask(void *pvParameters) {
  // Initialize watchdog for this task. Timeout is 30 seconds.
  esp_task_wdt_init(150, true);
  esp_task_wdt_add(NULL);

  SerialAT.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  initNvs();
  loadCredentials();
  loadGprsCredentials();
  setupBLE();
  
  while (1) {
    // Reset the watchdog at the beginning of each loop iteration.
    esp_task_wdt_reset();

    Serial.println();
    Serial.println("Monitoring connectivity...");
    monitorConnectivity();

    if (status.activeConnection != "None") {
      if (status.switchNetwork || !mqttClient.connected()) {
        connectMQTT();
        status.switchNetwork = false;
      } else {
        mqttClient.loop();
      }
    } else {
      status.mqttConnected = false;
    }
    Serial.println();
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void sendDataToMQTT(const String& data) {
  // Guard: Only proceed if we have an active connection and MQTT is connected.
  if (status.activeConnection == "None" || !mqttClient.connected()) {
    return;
  }

  // Rate-limit publishing to once every 5 seconds.
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish < 5000) {
    return;
  }

  bool published = mqttClient.publish(config.publishTopic, data.c_str());
  if (published) {
    if (DEBUG) Serial.println("Published to " + String(config.publishTopic) + ": " + data);
    lastPublish = millis();
  } else {
    if (DEBUG) Serial.println("MQTT publish failed for topic " + String(config.publishTopic));
  }
}