#include <Arduino.h>
#include "Connectivity.h"
#include "Sensors.h"
#include <LiquidCrystal.h>
#include <ArduinoJson.h>

// ========== LCD Configuration ==========
const int LCD_RS = 33;
const int LCD_EN = 32;
const int LCD_D4 = 25;
const int LCD_D5 = 26;
const int LCD_D6 = 27;
const int LCD_D7 = 14;

#define LCD_COLS 20
#define LCD_ROWS 4

LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// ========== LCD Custom Characters ==========
byte lcdBars[6][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0 bars
    {0b00000,0b00000,0b00000,0b00000,0b00000,0b10000,0b10000,0b10000},
    {0b00000,0b00000,0b00000,0b00000,0b01000,0b11000,0b11000,0b11000},
    {0b00000,0b00000,0b00000,0b00100,0b01100,0b11100,0b11100,0b11100},
    {0b00000,0b00000,0b00010,0b00110,0b01110,0b11110,0b11110,0b11110},
    {0b00001,0b00001,0b00011,0b00111,0b01111,0b11111,0b11111,0b11111}
};

byte noSignal[8] = {
    0b00000,
    0b10001,
    0b01010,
    0b00100,
    0b00100,
    0b01010,
    0b10001,
    0b00000
};

// ========== Forward Declarations ==========
void initLCD();
void displayHeader();
void displayConnectivity();
void displaySensorData();
void displayMQTTStatus();
int  getSignalLevel(long value, long min, long max);
void updateLCDLine(uint8_t row, const String &text);
void monitorTaskSetup();

// ========== Setup ==========
void setup() {
    Serial.begin(115200);
    initLCD();
    displayHeader();
    monitorTaskSetup();
}

// ========== Loop ==========
void loop() {
    displayConnectivity();
    displaySensorData();
    displayMQTTStatus();

    // Prepare and send JSON data
    String payload;
    doc["uptime"] = String(millis() / 1000) + "s";
    serializeJson(doc, payload);
    sendDataToMQTT(payload);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// ========== Initialization Functions ==========
void initLCD() {
    lcd.begin(LCD_COLS, LCD_ROWS);
    lcd.clear();

    // Create custom characters
    for (int i = 0; i <= 5; i++) lcd.createChar(i, lcdBars[i]);
    lcd.createChar(6, noSignal);
}

void displayHeader() {
    lcd.setCursor(4, 0);
    lcd.print("CleanEnv v0.1");
    lcd.setCursor(2, 1);
    lcd.print("Initializing...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    lcd.clear();
}

// ========== Display Functions ==========
void displayConnectivity() {
    lcd.setCursor(0, 0);
    lcd.print("                    "); // clear line
    lcd.setCursor(0, 0);

    int signalLevel = 0;

    if (status.activeConnection == "WiFi") {
        signalLevel = getSignalLevel(status.wifiRssi, -100, -70);
    } else if (status.activeConnection == "Cellular") {
        signalLevel = getSignalLevel(status.cellularCsq, 0, 31);
    }

    lcd.print(status.activeConnection);
    lcd.setCursor(status.activeConnection.length() + 1, 0);
    lcd.write(status.activeConnection == "None" ? 6 : byte(signalLevel));
}

void displaySensorData() {
    String line1 = "T:" + String(doc["temp"].as<float>(), 1) + "C";
    line1 += " V:" + String(doc["voltage"].as<float>(), 2) + "V";
    line1 += " I:" + String(doc["current"].as<float>(), 2) + "A";

    updateLCDLine(1, line1);

    if (line1.length() > LCD_COLS) {
        updateLCDLine(2, line1.substring(LCD_COLS));
    } else {
        updateLCDLine(2, "");
    }
}

void displayMQTTStatus() {
    String mqttStatusText = "MQTT: " + String(status.mqttConnected ? "Connected" : "Disconnected");
    updateLCDLine(3, mqttStatusText);
}

// ========== Helper Functions ==========
int getSignalLevel(long value, long min, long max) {
    if (value <= min) return 0;
    if (value >= max) return 5;
    return (int)((value - min) * 5 / (max - min));
}

void updateLCDLine(uint8_t row, const String &text) {
    lcd.setCursor(0, row);
    lcd.print(String("                    ").substring(0, LCD_COLS)); // clear line
    lcd.setCursor(0, row);
    lcd.print(text);
}

void monitorTaskSetup() {
    xTaskCreatePinnedToCore(
        monitorConnectivityTask,
        "MonitorConnectivity",
        20000,
        NULL,
        1,
        NULL,
        0
    );

    xTaskCreatePinnedToCore(
        monitorSensorsTask,
        "MonitorSensors",
        10000,
        NULL,
        1,
        NULL,
        1
    );
}
