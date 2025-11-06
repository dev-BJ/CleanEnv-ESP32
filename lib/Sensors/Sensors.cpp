#include "Sensors.h"

// -------- Pin definitions --------
#define MAX_CS_PIN 5
#define MAX_SCK_PIN 18
#define MAX_MISO_PIN 19

#define DEBUG 1

#define SHIFT_DATA_PIN 23
#define SHIFT_CLOCK_PIN 22
#define SHIFT_LATCH_PIN 21

#define MUX_S0 4
#define MUX_S1 0
#define MUX_S2 2
#define MUX_S3 15
#define MUX_SIG 35

// ADC and Voltage/Current Sensor Constants
#define ADC_RESOLUTION 4095.0
#define ADC_REF_VOLTAGE 3.9 // Full-scale voltage for ADC with 11dB attenuation

// This is the system voltage that corresponds to the ADC's max input voltage (ADC_REF_VOLTAGE)
// after passing through a voltage divider. (e.g. 16.8V -> 3.9V)
#define VOLTAGE_DIVIDER_MAX_IN 16.8 
#define VOLTAGE_SCALING_FACTOR (VOLTAGE_DIVIDER_MAX_IN / ADC_REF_VOLTAGE)
#define CURRENT_SENSOR_SENSITIVITY 0.066 // For ACS712 30A version (66mV/A)
// #define CURRENT_SENSOR_OFFSET (ADC_REF_VOLTAGE / 2) // ACS712 is a 5V sensor, so its 0A offset is 2.5V
#define CURRENT_SENSOR_OFFSET 2.5  // ACS712 outputs 2.5V at 0A when powered by 5V
#define VOLTAGE_MAP (v) ((v / ADC_REF_VOLTAGE) * 25)

#define R1 52000.0 // 52k ohm, added 22k to 30k on volatage sensor 25v > 3.15v
#define R2 7500.0  // 7.5k ohm

// #define NUM_SENSORS
#define NUM_FANS 4

#define HIGH_TEMP_1     90.0   // Stage 1: Fans 0 & 1
#define HIGH_TEMP_2     90.0   // Stage 2: All fans
#define FAN1_OFF_TEMP    70.0   // Fans 0 & 1 turn OFF below this
#define HYSTERESIS        5   // Hysteresis for downward transitions

#define FAN_MASK(x) (x & ((1 << NUM_FANS) - 1))

SensorConfig sensorMap[] = {
  {0, SENSOR_VOLTAGE, "b_v"}, // battery voltage
  {1, SENSOR_CURRENT, "b_c"}, // battery current
  {2, SENSOR_VOLTAGE, "t_v"}, // teg voltage
  {3, SENSOR_CURRENT, "t_c"}, // teg current
  {4, SENSOR_VOLTAGE, "c_v"}, // charg voltage
  {5, SENSOR_CURRENT, "c_c"}, // charg current
  // Add more sensors here
};

const int NUM_SENSORS = sizeof(sensorMap) / sizeof(sensorMap[0]);


// -------- Global Objects --------
MAX6675 thermocouple(MAX_SCK_PIN, MAX_CS_PIN, MAX_MISO_PIN);

SimpleJson data;

byte active_fans = 0x00;

// Fan bitmasks
const byte STAGE_1_FANS = (1 << 0) | (1 << 2);  // Fans 0,1
const byte STAGE_2_FANS = (1 << 1) | (1 << 3);  // Fans 2,3
const byte ALL_FANS     = STAGE_1_FANS | STAGE_2_FANS;

// === Persistent state for hysteresis ===
static int   current_state = 0;  // 0=off, 1=stage1, 2=stage2
static float state_entry_temp = 0.0;  // temp when we entered this state

byte desired_fans = active_fans;
int  desired_state = current_state;
bool should_update = false;

// -------- Helper Functions --------
static void selectMuxChannel(int channel) {
  digitalWrite(MUX_S0, (channel & 0x01));
  digitalWrite(MUX_S1, (channel & 0x02));
  digitalWrite(MUX_S2, (channel & 0x04));
  digitalWrite(MUX_S3, (channel & 0x08));
}

inline void customShiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t value){
  for(int8_t i = 7; i >= 0; --i){
    digitalWrite(dataPin, (value >> 1) & 1);
    digitalWrite(clockPin, HIGH);
    digitalWrite(clockPin, LOW);
  }
}

static void setFanState(uint8_t mask) {
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, MSBFIRST, mask);
  // customShiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, mask);
  digitalWrite(SHIFT_LATCH_PIN, LOW);
  digitalWrite(SHIFT_LATCH_PIN, HIGH);
}

// round floaat to 2 decimal places
static float round_float(float value, int places) {
  float factor = powf(10.0f, places);
  value = roundf(value * factor) / factor;
  return value;
}

static float getVoltage(float adcValue) {
  float vout = (adcValue * 3.15) / 3909;
  float vin = (vout + 0.18) / (R2 / (R1 + R2));
  // float vin = vout * (25.0 / 3.15);
  // vin = (vin / 4.95) * 24.75;
  if (DEBUG) Serial.printf("Raw ADC: %.0f, Vout: %.2f V, Vin: %.2f V\n", adcValue, vout, vin);
  return vin >= 5 ? round_float(vin, 2) : 0;
}

static float getCurrent(float adcValue, int channel) {
  float voltage = (adcValue / ADC_RESOLUTION) * 3.3;
  // voltage = (voltage / 5.0) * 3.3;
  float offset_voltage = (3.3 / 2); // ACS712 outputs 2.5V at 0A when powered by 5V
  offset_voltage = channel == 1 ? 0.39 : channel == 3 ? 0.14 : 0.24; // Different sensors can have different offsets
  float current = ((voltage < offset_voltage ? offset_voltage : voltage) - offset_voltage) / (CURRENT_SENSOR_SENSITIVITY);
  if (DEBUG) Serial.printf("Raw ADC: %.0f, Voltage: %.2f V, Current: %.2f A\n", adcValue, voltage, current);
  return current < 0.001 ? 0 : (round_float(current, 2)); // Clamp negative currents to 0
}

// -------- Setup --------
void setupSensors() {
  if (DEBUG) {
    // Serial.println();
    Serial.println("ESP32 Temperature Control and Sensor Reading Started");
  }

  // --- Shift Register Pins ---
  pinMode(SHIFT_DATA_PIN, OUTPUT);
  pinMode(SHIFT_CLOCK_PIN, OUTPUT);
  pinMode(SHIFT_LATCH_PIN, OUTPUT);

  // --- Multiplexer Pins ---
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(MUX_SIG, INPUT);

  // --- ADC Configuration ---
  analogReadResolution(12);          // 12-bit ADC (0–4095)
  analogSetAttenuation(ADC_11db);    // Full range (0–3.9V)

  // --- Initialize Fans Off ---
  setFanState(0x00);
}

// -------- Sensor Reading --------
void monitorSensors() {
  // doc.clear();
  // --- Temperature ---
  float temp = thermocouple.readCelsius();
  vTaskDelay(10 / portTICK_PERIOD_MS);
  if (isnan(temp)) {
    if (DEBUG) Serial.println("Error reading temperature!");
    return;
  }

  data.set("temp", temp);
  if (DEBUG) Serial.printf("Temperature: %.2f °C\n", temp);

  // === 1. Determine desired state based on temperature ===
  if (temp >= HIGH_TEMP_1) {
      desired_state = 2;
      desired_fans = ALL_FANS;
      should_update = true;
  }
  else if (temp >= HIGH_TEMP_1 || (temp >= FAN1_OFF_TEMP && desired_state != 0)) {
      desired_state = 1;
      desired_fans = ALL_FANS;
      should_update = true;
  }
  else {
      desired_state = 0;
      desired_fans = 0x00;
      should_update = true;
  }

  if (desired_state != current_state) {
    // --- Apply to physical fans ---
      setFanState(FAN_MASK(desired_fans));
      // setFanState(desired_fans);
      active_fans = desired_fans;
      // --- Debug ---
      if (DEBUG) {
          if (active_fans == ALL_FANS)
              Serial.println("All Fans ON - Critical Temp");
          else if (active_fans == STAGE_1_FANS)
              Serial.println("Stage 1 Fans ON (0,1)");
          else
              Serial.println("All Fans OFF");
      }
      current_state = desired_state;
  }

  // --- Update telemetry ---
  data.set("fan_state", (active_fans != 0x00));

  // --- Multiplexer Readings ---
 
 for (int i = 0; i < NUM_SENSORS; i++) {
  int channel = sensorMap[i].channel;
  selectMuxChannel(channel);
  vTaskDelay(10 / portTICK_PERIOD_MS);

  int adcValue = analogRead(MUX_SIG);
  vTaskDelay(10 / portTICK_PERIOD_MS);
  // Serial.println(adcValue); 
  // This is the actual voltage at the MUX_SIG pin
  // float muxVoltage = (adcValue / ADC_RESOLUTION) * ADC_REF_VOLTAGE;
  // if (DEBUG) Serial.printf("Channel %d: %.2f V\n", channel, muxVoltage);

  float reading = 0.0;

  switch (sensorMap[i].type) {
    case SENSOR_VOLTAGE: {
      // Scale the mux voltage back up to the original system voltage
      // This assumes a voltage divider that maps 0-16.8V to 0-3.9V
      reading = getVoltage(adcValue);
      break;
    }

    case SENSOR_CURRENT: {
      // The ACS712 output is directly read by the ADC, so use muxVoltage
      reading = getCurrent(adcValue, channel);
      break;
    }

    case SENSOR_TEMPERATURE:
      // Temperature is already read outside the loop.
      // If this case is for a different temp sensor, its logic should be here.
      reading = temp;
      break;

    case SENSOR_GENERIC:
    default:
      // reading = muxVoltage;
      break;
  }

  // Store in JSON for unified output
  // doc[sensorMap[i].name] = reading;
  data.set(sensorMap[i].name, reading);
  if (DEBUG) {
    Serial.printf("%s (Ch%d): %.2f\n", sensorMap[i].name, channel, reading);
  }
}
  // vTaskDelay(100 / portTICK_PERIOD_MS);
}

// -------- FreeRTOS Task --------
void monitorSensorsTask(void *pvParameters) {
  setupSensors();

  while (1) {
    // stack watermark
    // UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    // Serial.printf("MonitorSensors stack remaining: %d bytes\n", stackHighWaterMark * sizeof(StackType_t));
    // esp_task_wdt_reset();
    if(DEBUG) Serial.println("Reading sensors...");
    monitorSensors();
    if(DEBUG) Serial.println();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
