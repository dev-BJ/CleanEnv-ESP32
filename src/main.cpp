#include <Arduino.h>
#include "Connectivity.h"
#include "Sensors.h"
#include <Update.h>
// #include "OTAUpdate.h"
#include <LiquidCrystal.h>
// #include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
// #include <ElegantOTA.h>
#include <ESPmDNS.h> // Include mDNS library
#include <StreamString.h>

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
AsyncWebServer server(443);

TaskHandle_t connectivityHandle;
TaskHandle_t sensorsHandle;

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

char username[16] = "cleanenv";
char password[16] = "admin";
const char* hostname = "cleanenv";
char deviceIP[16];
bool isUpdating = false;
bool taskRunning = true;
size_t current_size, content_length = 0;
char currentVersion[8] = "1.0.1";
   // OTA Update HTML Page (with progress bar)
static const char* serverIndex PROGMEM = R"(
        <!DOCTYPE html>
        <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>ESP32 OTA Update</title>
            <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.8/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-sRIl4kxILFvY47J16cr9ZwB07vP4J8+LH7qKQnuqkuIAvNWLzeN8tE5YBujZqJLB" crossorigin="anonymous">
            <style>
            body { font-family: Arial; background: #111; color: #0f0; text-align: center; padding: 50px; }
            h1, h2 { color: #0f0; }
            input[type=file] { background: #222; color: #0f0; padding: 10px; border-radius: 5px; }
            input[type=submit] { background: #0f0; color: #111; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; }
            progress { width: 80%; height: 20px; margin-top: 15px; }
            </style>
        </head>
        <body class="container d-flex flex-column justify-content-center align-items-center">
            <h1 class="display-2">CleanEnv OTA Update</h1>
            <h2>ESP32 OTA Firmware Update</h2>
            <!-- <p>Current Version: <span id="version">Loading...</span></p> -->
            <form id="upload_form" method="POST" action="/update" enctype="multipart/form-data">
            <div class="mt-3 mb-2">
                <input type="file" class="form-control" name="update" required>
            </div>
            <div class="mb-3">
                <button type="submit" class="btn btn-success">Upload & Update</button>
            </div>
            <div class="mt-3 mb-2 d-flex justify-content-center align-items-center">
                <progress id="progressBar" value="0" max="100"style="display: none;"></progress>
            </div>
            <div class="mt-3 mb-2">
                <p id="status"></p>
            </div>
            </form>

            <button id="backButton" class="btn btn-primary d-none">&#x2B88 Back</button>

            <script>
                const form = document.getElementById('upload_form');

                form.addEventListener('submit', async (e) => {
                e.preventDefault();

                const data = new FormData(form);
                const progressBar = document.getElementById('progressBar');
                const status = document.getElementById('status');
                const backButton = document.getElementById('backButton');

                // Create fetch-compatible upload with progress
                const xhr = new XMLHttpRequest();
                xhr.open('POST', '/update', true);

                xhr.upload.onprogress = (e) => {
                    if (e.lengthComputable) {
                    if (progressBar.style.display === 'none') {
                        progressBar.style.display = 'block';
                    }
                    const percent = (e.loaded / e.total) * 100;
                    progressBar.value = percent;
                    status.style.display = 'block';
                    status.innerText = Math.round(percent) + '% uploaded';
                    }
                };

                xhr.onload = () => {
                    if (xhr.status === 200) {
                    status.innerText = 'Update complete! Rebooting...';
                    } else {
                    status.className = 'text-danger';
                    status.innerText = xhr.statusText;
                    }
                    backButton.classList.remove('d-none');
                    backButton.addEventListener('click', function handler() {
                        progressBar.style.display = 'none';
                        progressBar.value = 0;
                        backButton.classList.add('d-none');
                        status.style.display = 'none';
                        status.className = '';
                        form.reset();
                        backButton.removeEventListener('click', handler);
                    });
                };

                xhr.send(data);
                });
            </script>

        </body>
        </html>
        )";

// ========== Forward Declarations ==========
void initLCD();
void displayHeader();
void displayConnectivity();
void displaySensorData();
void displayMQTTStatus();
int  getSignalLevel(long value, long min, long max);
void updateLCDLine(uint8_t row, const String &text);
void monitorTaskSetup();
void OTAUpdate();


// ========== Setup ==========
void setup() {
    Serial.begin(115200);
    initLCD();
    displayHeader();
    monitorTaskSetup();
    OTAUpdate();
}

// ========== Loop ==========
void loop() {
    if (status.activeConnection == F("WiFi") && WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    }
    // eSp free heap in kb
    Serial.println("Free heap: " + String(esp_get_free_heap_size() / 1024) + "kb");
    Serial.println("version: " + String(currentVersion));

    if (isUpdating) {
        return;
    }
    // Update LCD Display
    displayConnectivity();
    displaySensorData();
    displayMQTTStatus();

    // Prepare and send JSON data
    char payload[512];;
    data.set("uptime", String(millis() / 1000));
    data.set("active_conn", status.activeConnection == F("None") ? -1 : (status.activeConnection == F("WiFi") ? 0: (status.activeConnection == F("Cellular") ? 1 : -1)));
    if(status.activeConnection == F("WiFi")) {
        data.set("sig_rssi", String(status.wifiRssi).c_str());
    } else if(status.activeConnection == F("Cellular")) {
        data.set("sig_rssi", String(status.cellularCsq).c_str());
    }
    data.set("ble_status", status.bleDeviceConnected);
    data.set("ip", WiFi.localIP().toString().c_str());
    data.set("ver", String(currentVersion).c_str());

    data.toCharArray(payload, sizeof(payload));
    sendDataToMQTT(payload);

    vTaskDelay(500 / portTICK_PERIOD_MS);
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
    lcd.print("Firmware version");
    lcd.setCursor(7, 2);
    lcd.print(currentVersion);
    lcd.setCursor(2, 3);
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

    if (status.activeConnection == F("WiFi")) {
        signalLevel = getSignalLevel(status.wifiRssi, -100, -30);
    } else if (status.activeConnection == F("Cellular")) {
        signalLevel = getSignalLevel(status.cellularCsq, 0, 31);
    }

    lcd.print(status.activeConnection);
    lcd.setCursor(status.activeConnection.length() + 1, 0);
    lcd.write(status.activeConnection == F("None") ? 6 : byte(signalLevel));
}

void displaySensorData() {
    char line_data[32];
    String line1 = "T:" + String(data.getFloat("temp"), 1) + "C";
    line1 += " V:" + String(data.getFloat("b_V"), 2) + "V";
    line1 += " I:" + String(data.getFloat("b_c"), 2) + "A";

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
        3096,
        NULL,
        1,
        &connectivityHandle,
        0
    );

    xTaskCreatePinnedToCore(
        monitorSensorsTask,
        "MonitorSensors",
        2048,
        NULL,
        1,
        &sensorsHandle,
        1
    );
}

void OTAUpdate() {
 //  WiFi.begin(ssid, password);
  
  Serial.println("Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected! IP Address: ");
  Serial.println(WiFi.localIP());

  // Start mDNS
  if (MDNS.begin(hostname)) {
    Serial.println("mDNS responder started: http://" + String(hostname) + ".local");
    MDNS.addService("https", "tcp", 443); // Advertise HTTP service on port 80
  } else {
    Serial.println("mDNS failed to start");
  }

  // Root page (with authentication)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate(username, password))
      return request->requestAuthentication();

    if(status.activeConnection != F("WiFi")){
      request->send(200, "text/html", F("<!DOCTYPE html><html><head><title>WiFi Not Connected</title></head><body><h1>WiFi Not Connected</h1><p>Please connect to WiFi before accessing this page.</p></body></html>"));
      return;
    }
    request->send(200, "text/html", serverIndex);
  });

  // OTA Update handling
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    // This is the success handler, which is called after the upload is complete.
    if(!request->authenticate(username, password))
      return request->requestAuthentication();

    AsyncWebServerResponse *response = request->beginResponse((Update.hasError()) ? 400 : 200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    response->addHeader("Connection", "close"); // Recommended to close connection after OTA
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    
    // AsyncWebServerResponse *response;
    // This is the upload handler, which is called for each chunk of the file.
    if(!index){ // If index is 0, it's the first chunk
      Serial.printf("Update Start: %s\n", filename.c_str());
      current_size = 0;
      content_length = request->contentLength();

      lcd.clear();

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        StreamString str;
        Update.printError(str);
        Serial.println(str.c_str());
        AsyncWebServerResponse *response = request->beginResponse((Update.hasError()) ? 400 : 200, "text/plain", (Update.hasError()) ? str.c_str() : "OK");
        response->addHeader("Connection", "close"); // Recommended to close connection after OTA
    response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
      }
      updateLCDLine(0, "Updating...");
      updateLCDLine(1, "0%");
    }

    if(!isUpdating) {
        vTaskSuspend(sensorsHandle);
        vTaskSuspend(connectivityHandle);
        isUpdating = true;
    }

    if(len){
        if (Update.write(data, len) != len) {
            StreamString str;
            Update.printError(str);
            Serial.println(str.c_str());
            AsyncWebServerResponse *response = request->beginResponse((Update.hasError()) ? 400 : 200, "text/plain", (Update.hasError()) ? str.c_str() : "OK");
            response->addHeader("Connection", "close"); // Recommended to close connection after OTA
            response->addHeader("Access-Control-Allow-Origin", "*");
            isUpdating = false;
        }
        current_size += len;
        updateLCDLine(1, String(current_size * 100 / content_length) + "%");
        Serial.printf("Update Progress: %u%%\n", (current_size * 100 / content_length));
    }
    
    if (final) {
        AsyncWebServerResponse *response;

        if (Update.end(true)) {
            Serial.printf("Update Success: %u bytes\nRebooting...\n", index + len);
            response = request->beginResponse(200, "text/plain", "OK");
            updateLCDLine(2, "Update Successful! Rebooting...");
        } else {
            StreamString str;
            Update.printError(str);
            Serial.println(str.c_str());
            response = request->beginResponse(400, "text/plain", str.c_str());
            updateLCDLine(2, "Update Failed! Error: " + str);
        }

        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);

        isUpdating = false;

        // 🔥 Defer restart to a separate task or timer
        // This gives the TCP stack time to flush the response
        xTaskCreate([](void*){
            delay(1000);   // Give client 1s to receive the full response
            ESP.restart();
        }, "deferredRestart", 2048, NULL, 1, NULL);
    }

  });

  server.begin();
  Serial.println("HTTP server started");
}
