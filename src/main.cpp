#include <Arduino.h>
#include <Connectivity.h>
#include <Sensors.h>
#include <LiquidCrystal.h>
// #include "esp_task_wdt.h"

// LCD config (20x4, 4-bit mode)
const int lcdRS = 33;  // Register Select
const int lcdEN = 32;  // Enable
const int lcdD4 = 25;  // Data 4
const int lcdD5 = 26;  // Data 5
const int lcdD6 = 27;  // Data 6
const int lcdD7 = 14;  // Data 7

int getSignalLevel(long value, long min, long max);

byte signal0[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // empty

byte signal1[8] = {
    0b00000,
    0b00000,
    0b00000,   
    0b00000,
    0b00000,
    0b10000,
    0b10000,
    0b10000
}; // 1 bar

byte signal2[8] = {
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b01000,
    0b11000,
    0b11000,
    0b11000
}; // 2 bars

byte signal3[8] = {
    0b00000,
    0b00000,
    0b00000,
    0b00100,
    0b01100,
    0b11100,
    0b11100,
    0b11100
}; // 3 bars

byte signal4[8] = {
    0b00000,
    0b00000,
    0b00010,
    0b00110,
    0b01110,
    0b11110,
    0b11110,
    0b11110
}; // 4 bars

byte signal5[8] = {
    0b00001,
    0b00001,
    0b00011,
    0b00111,
    0b01111,
    0b11111,
    0b11111,
    0b11111
}; // 5 bars

byte no_signal[] = {
    0b00000,
    0b10001,
    0b01010,
    0b00100,
    0b00100,
    0b01010,
    0b10001,
    0b00000
};

LiquidCrystal lcd(lcdRS, lcdEN, lcdD4, lcdD5, lcdD6, lcdD7);

void setup(){
    // esp_task_wdt_init(10, true); // 10 seconds, panic on timeout
    // Serial.begin(9600);
    Serial.begin(115200);
    lcd.begin(20, 4); // Initialize 20x4 LCD
    lcd.clear();
    lcd.setCursor(4,0);
    lcd.print("CleanEnv v0.1");
    lcd.setCursor(2,1);
    lcd.print("Initializing...");
    vTaskDelay(2000 / portTICK_PERIOD_MS); // Wait for 2 seconds

    lcd.createChar(0, signal0);
    lcd.createChar(1, signal1);
    lcd.createChar(2, signal2);
    lcd.createChar(3, signal3);
    lcd.createChar(4, signal4);
    lcd.createChar(5, signal5);
    lcd.createChar(6, no_signal);

    xTaskCreatePinnedToCore(
        monitorConnectivityTask,   // Task function
        "MonitorConnectivity",     // Name of the task
        10000,                      // Stack size in bytes
        NULL,                      // Task input parameter
        1,                         // Priority of the task
        NULL,                      // Task handle
        0                          // Core where the task should run (0 or 1)
    );

    xTaskCreatePinnedToCore(
        monitorSensorsTask,        // Task function
        "MonitorSensors",          // Name of the task
        10000,                      // Stack size in bytes
        NULL,                      // Task input parameter
        1,                         // Priority of the task
        NULL,                      // Task handle
        1                          // Core where the task should run (0 or 1)
    );
}

void loop(){
    lcd.clear();
    lcd.setCursor(0,0);
    int signalLevel = 0;
    if(activeConnection == "WiFi"){
        signalLevel = getSignalLevel(wifiRssi, -100, -70);
        lcd.print(activeConnection);
        lcd.setCursor(activeConnection.length() + 1, 0);
        lcd.write(byte(signalLevel)); // Show signal bar
        // lcd.print(" RSSI:" + String(wifiRssi));
    } else if(activeConnection == "Cellular"){
        signalLevel = getSignalLevel(cellularCsq, 0, 31);
        lcd.print(activeConnection);
        lcd.setCursor(activeConnection.length() + 1, 0);
        lcd.write(byte(signalLevel)); // Show signal bar
        // lcd.print(" CSQ:" + String(cellularCsq));
    } else {
        lcd.print(activeConnection);
        lcd.setCursor(activeConnection.length() + 1, 0);
        lcd.write(6); // Show no signal icon
        // lcd.print("No Network");
    }
    String sensor = String("T:") + String(doc["temp"].as<float>(), 1) + "C" + String(" V:") + String(doc["voltage"].as<float>(), 2) + "V" + String(" I:") + String(doc["current"].as<float>(), 2) + "A";
    // String sensor = "";
    // If 'doc' is an ArduinoJson::JsonDocument, iterate using JsonObject
    // for (JsonPair kv : doc.as<JsonObject>()) {
    //     sensor += " " + String(kv.key().c_str()) + ":" + String(kv.value().as<String>());
    // }
    lcd.setCursor(0,1);
    lcd.print(sensor);
    if(sensor.length() > 20) {
        lcd.setCursor(0, 2);
        lcd.print(sensor.substring(20));
    }
    lcd.setCursor(0,3);
    lcd.print("MQTT: " + String(mqttConnected ? "Connected" : "Disconnected"));

    String data;
    doc["uptime"] = String(millis() / 1000) + "s";
    serializeJson(doc, data);
    sendDataToMQTT(data);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

int getSignalLevel(long value, long min, long max) {
    if (value <= min) return 0;
    if (value >= max) return 5;
    return (int)((value - min) * 5 / (max - min));
}

