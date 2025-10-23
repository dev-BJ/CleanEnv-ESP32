#include <Arduino.h>
#include <LiquidCrystal.h>

#define LCD_COLS 20

// ========================================
// HELPER FUNCTIONS
// ========================================
void updateLCDLine(LiquidCrystal& lcd, uint8_t row, const String &text, bool clear_line = true) {
    lcd.setCursor(0, row);
    lcd.print(text);
    if (clear_line) {
        // Print spaces to clear the rest of the line
        for (int i = text.length(); i < LCD_COLS; i++) {
            lcd.print(" ");
        }
    }
}

// void firmwareUpdate(LiquidCrystal& lcd) {
//     // 5. START UPDATE
//       if (!Update.begin(contentLength)) {
//         if (DEBUG) Serial.printf("❌ Update begin failed! Error: %u\n", Update.getError());
//         Update.printError(Serial);
//         updateLCDLine(lcd, 2, "Update Error: " + String(Update.getError()));
//         http.end();
//         return false;
//       }
// }

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
// #include <AsyncElegantOTA.h>
#include <ESPmDNS.h> // Include mDNS library

const char* ssid = "YOUR_WIFI_SSID"; // Replace with your WiFi SSID
const char* password = "YOUR_WIFI_PASSWORD"; // Replace with your WiFi password
const char* hostname = "esp32"; // mDNS hostname (access as esp32.local)

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  // Connect to WiFi with timeout
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 10000; // 10 seconds timeout
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed. Restarting...");
    ESP.restart();
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize mDNS
  if (MDNS.begin(hostname)) {
    Serial.println("mDNS started: http://" + String(hostname) + ".local");
    MDNS.addService("http", "tcp", 80); // Advertise HTTP service on port 80
  } else {
    Serial.println("mDNS failed to start");
  }

  // Route for root ("/")
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String response = "Hi! I am an ESP32. Go to /update to upload new firmware.\n";
    response += "Access me at: http://" + String(hostname) + ".local";
    request->send(200, "text/plain", response);
  });

  // Start ElegantOTA with authentication
//   AsyncElegantOTA.begin(&server, "admin", "myotapassword");

  // Start the server
  server.begin();
}

void loop() {
  // Optional: Check WiFi status and reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    delay(5000);
  }
}

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ESPmDNS.h>

WebServer server(80);

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* update_username = "admin";     // Change this
const char* update_password = "esp32";     // Change this

// OTA Update HTML Page (with progress bar)
const char* serverIndex = R"(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 OTA Update</title>
    <style>
      body { font-family: Arial; background: #111; color: #0f0; text-align: center; padding: 50px; }
      h2 { color: #0f0; }
      input[type=file] { background: #222; color: #0f0; padding: 10px; border-radius: 5px; }
      input[type=submit] { background: #0f0; color: #111; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; }
      progress { width: 80%; height: 20px; margin-top: 15px; }
    </style>
  </head>
  <body>
    <h2>ESP32 OTA Firmware Update</h2>
    <form id="upload_form" method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="update" required><br><br>
      <input type="submit" value="Upload & Update">
      <br><br>
      <progress id="progressBar" value="0" max="100"></progress>
      <p id="status"></p>
    </form>

    <script>
      var form = document.getElementById('upload_form');
      form.addEventListener('submit', function(e) {
        e.preventDefault();
        var data = new FormData(form);
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/update', true);

        xhr.upload.onprogress = function(e) {
          if (e.lengthComputable) {
            var percent = (e.loaded / e.total) * 100;
            document.getElementById('progressBar').value = percent;
            document.getElementById('status').innerText = Math.round(percent) + '% uploaded';
          }
        };

        xhr.onload = function() {
          if (xhr.status == 200) {
            document.getElementById('status').innerText = 'Update complete! Rebooting...';
          } else {
            document.getElementById('status').innerText = 'Upload failed!';
          }
        };

        xhr.send(data);
      });
    </script>
  </body>
</html>
)";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  
  Serial.println("Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected! IP Address: ");
  Serial.println(WiFi.localIP());

  // Start mDNS
  if (MDNS.begin("esp32")) {
    Serial.println("mDNS responder started: http://esp32.local");
  }

  // Root page (with authentication)
  // server.on("/", HTTP_GET, []() {
  //   if(!server.authenticate(update_username, update_password))
  //     return server.requestAuthentication();
  //   server.sendHeader("Connection", "close");
  //   server.send(200, "text/html", serverIndex);
  // });

  // OTA Update handling
  // server.on("/update", HTTP_POST, []() {
  //   if(!server.authenticate(update_username, update_password))
  //     return server.requestAuthentication();

  //   server.sendHeader("Connection", "close");
  //   server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  //   delay(500);
  //   ESP.restart();
  // }, []() {
  //   HTTPUpload& upload = server.upload();
  //   if (upload.status == UPLOAD_FILE_START) {
  //     Serial.printf("Update Start: %s\n", upload.filename.c_str());
  //     if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
  //       Update.printError(Serial);
  //     }
  //   } else if (upload.status == UPLOAD_FILE_WRITE) {
  //     if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
  //       Update.printError(Serial);
  //     }
  //   } else if (upload.status == UPLOAD_FILE_END) {
  //     if (Update.end(true)) {
  //       Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
  //     } else {
  //       Update.printError(Serial);
  //     }
  //   }
  // });

  // server.begin();
  // Serial.println("HTTP server started");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
  // server.handleClient();
}

// void OTAUpdate() {
//  //  WiFi.begin(ssid, password);
  
//   Serial.println("Connecting to Wi-Fi...");
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(1000);
//     Serial.print(".");
//   }

//   Serial.println();
//   Serial.print("Connected! IP Address: ");
//   Serial.println(WiFi.localIP());

//   // Start mDNS
//   if (MDNS.begin(hostname)) {
//     Serial.println("mDNS responder started: http://" + String(hostname) + ".local");
//     MDNS.addService("http", "tcp", 80); // Advertise HTTP service on port 80
//   } else {
//     Serial.println("mDNS failed to start");
//   }
//   // Root page (with authentication)
//   server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
//     if(!request->authenticate(username, password))
//       return request->requestAuthentication();

//     if(status.activeConnection != F("WiFi")){
//       request->send(200, "text/html", F("<!DOCTYPE html><html><head><title>WiFi Not Connected</title></head><body><h1>WiFi Not Connected</h1><p>Please connect to WiFi before accessing this page.</p></body></html>"));
//       return;
//     }
//     request->send(200, "text/html", serverIndex);
//   });

//   // OTA Update handling
//   server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
//     // This is the success handler, which is called after the upload is complete.
//     if(!request->authenticate(username, password))
//       return request->requestAuthentication();

//     AsyncWebServerResponse *response;
//     if(Update.hasError()) {
//       response = request->beginResponse(400, "text/plain", "FAIL");
//     } else{
//       response = request->beginResponse(200, "text/plain", "OK");
//     }
//     response->addHeader("Connection", "close"); // Recommended to close connection after OTA
//     request->send(response);
    
//     delay(500);
//     ESP.restart();
//   }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
//     // This is the upload handler, which is called for each chunk of the file.
//     if(!index){ // If index is 0, it's the first chunk
//       Serial.printf("Update Start: %s\n", filename.c_str());
//       if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
//         Update.printError(Serial);
//       }
//     } 

//     if (Update.write(data, len) != len) {
//         Update.printError(Serial);
//     }
    
//     if(final){ // If it's the last chunk
//       if(Update.end(true)){
//         Serial.printf("Update Success: %u bytes\nRebooting...\n", index + len);
//       } else {
//         Update.printError(Serial);
//       }
//     }
//   });

//   server.begin();
//   Serial.println("HTTP server started");
// }

// void OTAUpdate() {
//  //  WiFi.begin(ssid, password);
  
//   Serial.println("Connecting to Wi-Fi...");
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(1000);
//     Serial.print(".");
//   }

//   Serial.println();
//   Serial.print("Connected! IP Address: ");
//   Serial.println(WiFi.localIP());

//   // Start mDNS
//   if (MDNS.begin(hostname)) {
//     Serial.println("mDNS responder started: http://" + String(hostname) + ".local");
//     MDNS.addService("http", "tcp", 80); // Advertise HTTP service on port 80
//   } else {
//     Serial.println("mDNS failed to start");
//   }

//   // Route for root ("/")
//   server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
//     String response = "Hi! I am an ESP32. Go to /update to upload new firmware.\n";
//     response += "Access me at: http://" + String(hostname) + ".local";
//     request->send(200, "text/plain", response);
//   });

//   // Start ElegantOTA with authentication
//   ElegantOTA.begin(&server, username, password);
//   ElegantOTA.setAutoReboot(true);
//   ElegantOTA.onStart([]() {
//     // BLEDevice::deinit(true);
//     // vTaskSuspend(connectivityHandle);
//     // vTaskSuspend(sensorsHandle);
//   });
//   ElegantOTA.onEnd([](bool success) {
//     // vTaskResume(connectivityHandle);
//     // vTaskResume(sensorsHandle);
//   });

//   server.begin();
//   Serial.println("HTTP server started");
// }
