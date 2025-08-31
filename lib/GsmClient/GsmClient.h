#ifndef GSM_CLIENT_H
#define GSM_CLIENT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h> // ESP32 watchdog timer
#define GSM_DEBUG true

class GsmClient : public Client {
public:
    GsmClient(Stream& serialPort);
    int connect(IPAddress ip, uint16_t port) override;
    int connect(const char* host, uint16_t port) override;
    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buf, size_t size) override;
    int available() override;
    int read() override;
    int read(uint8_t* buf, size_t size) override;
    int peek() override;
    void flush() override;
    void stop() override;
    uint8_t connected() override;
    operator bool();
    bool resetModem();
    String sendCustomAT(const char* command, unsigned long timeout = 5000);
    int csq();
    void beginWdt(uint32_t timeout = 10000); // Initialize WDT with timeout (ms)
    void feedWdt(); // Feed the WDT to prevent reset
    void disableWdt(); // Disable the WDT
private:
    Stream& serial;
    bool isConnected;
    void sendAT(const char* cmd, ...);
    bool waitResponse(const char* expected = "OK");
    String readResponse();
};

#endif // GSM_CLIENT_H