/*
  HX710B Air Pressure Measurement with ESP32-C3 Supermini

  This code reads data from the HX710B sensor (a load cell amplifier similar to HX711)
  and prints the raw ADC values and calibrated pressure in bar on the Serial Monitor.

  Wiring:
  HX710B Data (DOUT) pin -> GPIO 4 (example)
  HX710B Clock (SCK) pin -> GPIO 5 (example)

  Modify GPIO pins below as needed to match your wiring.

  Note:
  - This code outputs raw ADC values from HX710B.
  - Calibration and conversion to actual pressure units require additional steps
    which depend on your load cell and setup.
  - Pressure unit is bar in this example calibration.
*/

#include <Arduino.h>

// Pin definitions - change if needed
#define HX710B_DOUT_PIN 3
#define HX710B_SCK_PIN 4

class HX710B {
  private:
    uint8_t dout_pin;
    uint8_t sck_pin;
    long data;

  public:
    HX710B(uint8_t dout, uint8_t sck) {
      dout_pin = dout;
      sck_pin = sck;
    }

    void begin() {
      pinMode(sck_pin, OUTPUT);
      pinMode(dout_pin, INPUT);
      digitalWrite(sck_pin, LOW);
    }

    bool is_ready() {
      return digitalRead(dout_pin) == LOW;
    }

    long read() {
      // Wait for the sensor to become ready
      while (!is_ready()) {
        delayMicroseconds(10);
      }

      unsigned long count = 0;
      uint8_t i;

      noInterrupts();

      // Read 24 bits of data
      for (i = 0; i < 24; i++) {
        digitalWrite(sck_pin, HIGH);
        delayMicroseconds(1);
        count = count << 1;
        digitalWrite(sck_pin, LOW);
        delayMicroseconds(1);
        if (digitalRead(dout_pin)) {
          count++;
        }
      }

      // Set the channel and gain factor by extra clock pulses
      digitalWrite(sck_pin, HIGH);
      delayMicroseconds(1);
      digitalWrite(sck_pin, LOW);
      delayMicroseconds(1);

      interrupts();

      // Convert 24-bit two's complement to signed long
      if (count & 0x800000) {
        count |= 0xFF000000;
      }

      return (long)count;
    }
};

HX710B sensor(HX710B_DOUT_PIN, HX710B_SCK_PIN);

// Calibration constants (set these values after your calibration)
const long offset_raw_value = 103200; // example baseline raw reading (no pressure or reference pressure)
const float offset_pressure_bar = 1.01325; // baseline pressure in bar (approx atmospheric pressure, 1013.25 hPa = 1.01325 bar)
const float scale_factor = 0.00001; // bar per raw count (example value, you need to adjust based on calibration)

void setup() {
  Serial.begin(115200);
  sensor.begin();
  Serial.println("HX710B Pressure Sensor Starting...");
}

void loop() {
  long rawValue = sensor.read();
  Serial.print("Raw HX710B reading: ");
  Serial.println(rawValue);

  // Convert raw reading to pressure in bar
  float pressure = (rawValue - offset_raw_value) * scale_factor + offset_pressure_bar;

  Serial.print("Calibrated Pressure (bar): ");
  Serial.println(pressure, 5); // print with 5 decimal places

  delay(500);
}
