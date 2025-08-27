#include "Sensors.h" // or #include "Multiplexer.h"

float voltage = 0.0;
float current = 0.0;
float temperature = 0.0;

// If you don't have the library, you can implement manual SPI read below.

// Pin definitions
#define MAX_CS_PIN 5    // MAX6675 Chip Select
#define MAX_SCK_PIN 18  // MAX6675 Clock (SCK)
#define MAX_MISO_PIN 19 // MAX6675 Data Out (MISO)

#define SHIFT_DATA_PIN 23  // HC595 Serial Data Input (DS)
#define SHIFT_CLOCK_PIN 22 // HC595 Shift Clock (SHCP)
#define SHIFT_LATCH_PIN 21 // HC595 Storage Clock (STCP)

#define MUX_S0 4  // HC4067 Select pin 0
#define MUX_S1 0  // Select pin 1
#define MUX_S2 2  // Select pin 2
#define MUX_S3 15  // Select pin 3
#define MUX_SIG 34 // Common signal pin (to ESP32 ADC, GPIO34 is ADC1_CH6)

// Temperature threshold to turn on fans (in Celsius)
#define TEMP_THRESHOLD 100.0

// Number of fans controlled by shift register (assuming 4 for example, bits 0-3)
#define NUM_FANS 4

// MAX6675 instance
MAX6675 thermocouple(MAX_SCK_PIN, MAX_CS_PIN, MAX_MISO_PIN);

void setupSensors() {
  // Serial setup
//   Serial.begin(115200);
  Serial.println("ESP32 Temperature Control and Sensor Reading Started");

  // Pin modes for shift register
  pinMode(SHIFT_DATA_PIN, OUTPUT);
  pinMode(SHIFT_CLOCK_PIN, OUTPUT);
  pinMode(SHIFT_LATCH_PIN, OUTPUT);

  // Pin modes for multiplexer
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(MUX_SIG, INPUT);  // ADC input

  // Initialize shift register to all off
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, MSBFIRST, 0x00);
  digitalWrite(SHIFT_LATCH_PIN, LOW);
  digitalWrite(SHIFT_LATCH_PIN, HIGH);
}

void monitorSensors() {
  // Read temperature from MAX6675
  temperature = thermocouple.readCelsius();

  if (isnan(temperature)) {
    Serial.println("Error reading temperature!");
  } else {
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");

    // Control fans based on temperature
    if (temperature > TEMP_THRESHOLD) {
      // Turn on all fans (set bits 0 to NUM_FANS-1 high)
      uint8_t fanState = (1 << NUM_FANS) - 1;  // e.g., 0x0F for 4 fans
      shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, MSBFIRST, fanState);
      digitalWrite(SHIFT_LATCH_PIN, LOW);
      digitalWrite(SHIFT_LATCH_PIN, HIGH);
      Serial.println("Fans turned ON");
    } else {
      // Turn off all fans
      shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, MSBFIRST, 0x00);
      digitalWrite(SHIFT_LATCH_PIN, LOW);
      digitalWrite(SHIFT_LATCH_PIN, HIGH);
      Serial.println("Fans turned OFF");
    }
  }

  // Read analog sensors via HC4067
  // Example: Read channel 0 (voltage sensor)
  selectMuxChannel(0);
  int voltageRaw = analogRead(MUX_SIG);
  voltage = (voltageRaw / 4095.0) * 3.3;  // Assuming 3.3V reference, adjust scaling for your sensor
  Serial.print("Voltage Sensor: ");
  Serial.print(voltage);
  Serial.println(" V");

  // Read channel 1 (current sensor)
  selectMuxChannel(1);
  int currentRaw = analogRead(MUX_SIG);
  current = (currentRaw / 4095.0) * 3.3;  // Adjust scaling for your sensor (e.g., ACS712 might need different formula)
  Serial.print("Current Sensor: ");
  Serial.print(current);
  Serial.println(" A");

  // Delay for next reading
  vTaskDelay(100 / portTICK_PERIOD_MS);  // 2 seconds
}

// Function to select HC4067 channel (0-15)
void selectMuxChannel(int channel) {
  digitalWrite(MUX_S0, (channel & 0x01) ? HIGH : LOW);
  digitalWrite(MUX_S1, (channel & 0x02) ? HIGH : LOW);
  digitalWrite(MUX_S2, (channel & 0x04) ? HIGH : LOW);
  digitalWrite(MUX_S3, (channel & 0x08) ? HIGH : LOW);
}

void monitorSensorsTask(void *pvParameters) {
  setupSensors();  // Initialize sensors and pins

  while (1) {
    monitorSensors();  // Read and process sensor data
    vTaskDelay(500 / portTICK_PERIOD_MS);  // Check every second
  }
}