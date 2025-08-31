#include "GsmClient.h"

// GPRS settings (must be defined in user sketch)
extern const char* apn;
extern const char* gprsUser;
extern const char* gprsPass;

GsmClient::GsmClient(Stream& serialPort) : serial(serialPort), isConnected(false) {
  // WDT is not initialized here; user must call beginWdt()
}

void GsmClient::beginWdt(uint32_t timeout) {
  esp_task_wdt_init((timeout / 1000), true); // Convert ms to seconds
  esp_task_wdt_add(NULL); // Add current task to WDT
}

void GsmClient::feedWdt() {
  esp_task_wdt_reset(); // Feed the WDT
}

void GsmClient::disableWdt() {
  esp_task_wdt_deinit(); // Disable the WDT
}

int GsmClient::connect(IPAddress ip, uint16_t port) {
  return connect("", port); // IP address not used; use hostname instead
}

int GsmClient::connect(const char* host, uint16_t port) {
  beginWdt(10000); // Enable WDT with 10s timeout for connect operation
  // Initialize modem
  sendAT("AT"); if (!waitResponse()) { disableWdt(); return 0; }
  sendAT("AT+CPIN?"); if (!waitResponse("+CPIN: READY")) { disableWdt(); return 0; }
  sendAT("AT+CREG?"); if (!waitResponse("+CREG: 0,1")) { disableWdt(); return 0; } // Registered to network
  sendAT("AT+CGATT=1"); if (!waitResponse()) { disableWdt(); return 0; } // Attach to GPRS

  // Set up APN
  sendAT("AT+CSTT=\"%s\",\"%s\",\"%s\"", apn, gprsUser, gprsPass);
  if (!waitResponse()) { disableWdt(); return 0; }

  // Start GPRS
  sendAT("AT+CIICR"); if (!waitResponse()) { disableWdt(); return 0; }
  sendAT("AT+CIFSR"); if (!waitResponse()) { disableWdt(); return 0; } // Get IP address

  // Connect to MQTT broker
  sendAT("AT+CIPSTART=\"TCP\",\"%s\",%d", host, port);
  if (!waitResponse("+CIPSTART: 0, CONNECT OK")) { disableWdt(); return 0; }

  isConnected = true;
  disableWdt(); // Disable WDT after successful connection
  return 1;
}

size_t GsmClient::write(uint8_t data) {
  return write(&data, 1);
}

size_t GsmClient::write(const uint8_t* buf, size_t size) {
  beginWdt(5000); // Enable WDT for write operation
  char command[32];
  sprintf(command, "AT+CIPSEND=%d", size);
  sendAT(command);
  if (!waitResponse(">")) { disableWdt(); return 0; }
  serial.write((char*)buf, size);
  size_t result = waitResponse() ? size : 0;
  disableWdt(); // Disable WDT after write
  return result;
}

int GsmClient::available() {
  beginWdt(5000); // Enable WDT for available check
  sendAT("AT+CIPRXGET=4,0"); // Check available data
  String response = readResponse();
  int bytes = 0;
  if (sscanf(response.c_str(), "+CIPRXGET: 4,0,%d", &bytes) == 1) {
    disableWdt();
    return bytes;
  }
  disableWdt();
  return 0;
}

int GsmClient::read() {
  beginWdt(5000); // Enable WDT for read operation
  sendAT("AT+CIPRXGET=2,0,1"); // Read 1 byte
  String response = readResponse();
  if (response.startsWith("+CIPRXGET: 2,0,1,")) {
    String data = readResponse();
    disableWdt();
    return data.length() > 0 ? (uint8_t)data[0] : -1;
  }
  disableWdt();
  return -1;
}

int GsmClient::read(uint8_t* buf, size_t size) {
  beginWdt(5000); // Enable WDT for read operation
  char command[32];
  sprintf(command, "AT+CIPRXGET=2,0,%d", size);
  sendAT(command);
  String response = readResponse();
  if (response.startsWith("+CIPRXGET: 2,0")) {
    int readBytes;
    if (sscanf(response.c_str(), "+CIPRXGET: 2,0,%d", &readBytes) == 1) {
      String data = readResponse();
      for (int i = 0; i < readBytes && i < size; i++) {
        buf[i] = data[i];
      }
      disableWdt();
      return readBytes;
    }
  }
  disableWdt();
  return 0;
}

int GsmClient::peek() {
  // SIM800/SIM900 modems don't support peeking directly via AT commands
  return -1;
}

void GsmClient::flush() {
  // Clear serial buffer
  while (serial.available()) {
    serial.read();
  }
}

void GsmClient::stop() {
  beginWdt(5000); // Enable WDT for stop operation
  sendAT("AT+CIPCLOSE"); waitResponse();
  sendAT("AT+CIPSHUT"); waitResponse();
  isConnected = false;
  disableWdt();
}

uint8_t GsmClient::connected() {
  return isConnected;
}

GsmClient::operator bool() {
  return isConnected;
}

void GsmClient::sendAT(const char* cmd, ...) {
  char buffer[100];
  va_list args;
  va_start(args, cmd);
  vsnprintf(buffer, sizeof(buffer), cmd, args);
  va_end(args);
  if (GSM_DEBUG) {
    Serial.print("Sending AT: "); // Debug
    Serial.println(buffer);
    serial.println(buffer);
  }
}

bool GsmClient::waitResponse(const char* expected) {
  unsigned long start = millis();
  String response = "";
  while (millis() - start < 5000) {
    if (serial.available()) {
      response += serial.readStringUntil('\n') + "\n";
      if (response.indexOf(expected) >= 0) {
        if (GSM_DEBUG) {
          Serial.print("Response (success): "); // Debug
          Serial.println(response);
        }
        return true;
      }
    }
    feedWdt(); // Feed WDT to prevent reset during wait
    delay(1); // Non-blocking
  }
  if (GSM_DEBUG) {
    Serial.print("Response (failed): "); // Debug
    Serial.println(response);
  }
  return false;
}

String GsmClient::readResponse() {
  String response = "";
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (serial.available()) {
      response += serial.readStringUntil('\n') + "\n";
      if (response.endsWith("\r\n")) break;
    }
    feedWdt(); // Feed WDT to prevent reset during read
    delay(1); // Non-blocking
  }
  return response;
}

String GsmClient::sendCustomAT(const char* command, unsigned long timeout) {
  beginWdt(timeout); // Enable WDT for custom AT command
  Serial.print("Sending custom AT: "); // Debug
  Serial.println(command);
  serial.println(command);
  String response = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    if (serial.available()) {
      response += serial.readStringUntil('\n') + "\n";
    }
    feedWdt(); // Feed WDT to prevent reset
    delay(1); // Non-blocking
  }
  if (GSM_DEBUG) {
    Serial.print("Custom AT response: "); // Debug
    Serial.println(response);
  }
  disableWdt();
  return response;
}

bool GsmClient::resetModem() {
  beginWdt(15000); // Enable WDT for modem reset (longer timeout)
  sendCustomAT("AT+CFUN=1,1"); // Reset modem (SIM800L)
  vTaskDelay(3000 / portTICK_PERIOD_MS); // Wait for reset
  String response = sendCustomAT("AT"); // Reinitialize
  if(response.indexOf("OK") >= 0){
    return true;
  }
  disableWdt();
  return false;
}

int GsmClient::csq() {
  String response = sendCustomAT("AT+CSQ");
  int signalStrength = 99; // Default for unknown
  if (response.indexOf("+CSQ:") >= 0) {
    // Parse response, e.g., "+CSQ: 20,0"
    int commaIndex = response.indexOf(",");
    if (commaIndex > 0) {
      String signalPart = response.substring(response.indexOf("+CSQ: ") + 6, commaIndex);
      signalStrength = signalPart.toInt();
    }
  }
  return signalStrength; // Returns 0-31 or 99 (unknown)
}