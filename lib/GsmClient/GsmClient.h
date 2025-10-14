#ifndef GSMCLIENT_H
#define GSMCLIENT_H

#include <Arduino.h>
#include <Client.h>      // Arduino Client base class

class GsmClient : public Client {
public:
  explicit GsmClient(HardwareSerial &serialPort);

  // Set GPRS credentials (copies into internal buffers)
  void setGprsCredentials(const char* apn, const char* user = "", const char* pass = "");

  // Watchdog helpers (optional; used internally)
  void beginWdt(uint32_t timeoutMs = 30000);
  void feedWdt();
  void disableWdt();

  // Client interface (override)
  virtual int connect(IPAddress ip, uint16_t port) override;
  virtual int connect(const char *host, uint16_t port) override;

  virtual size_t write(uint8_t) override;
  virtual size_t write(const uint8_t *buf, size_t size) override;

  virtual int available() override;
  virtual int read() override;
  virtual int read(uint8_t *buf, size_t size) override;
  virtual int peek() override;
  virtual void flush() override;
  virtual void stop() override;
  virtual uint8_t connected() override;
  virtual operator bool() override;

  // Additional utilities
  void sendAT(const char *fmt, ...);
  bool waitResponse(const char *expected, uint32_t timeoutMs = 5000);
  String readResponse(uint32_t timeoutMs = 5000);
  bool resetModem(uint32_t timeoutMs = 15000);
  int csq();

private:
  HardwareSerial &serial;
  bool isConnected;
  bool wdtEnabled;

  // Internal safe copies of credentials
  char apnBuf[64];
  char userBuf[32];
  char passBuf[32];

  // Internal RX buffer to smooth reads
  static const int RX_BUF_SZ = 512;
  uint8_t rxBuf[RX_BUF_SZ];
  int rxHead; // next free position
  int rxTail; // next byte to read

  // Internal helpers
  void rxBufferClear();
  int rxBufferAvailable();
  int rxBufferRead(uint8_t *buf, int len);

  // Debug helper
  void debugLog(const char *fmt, ...);
};

#endif // GSMCLIENT_H
