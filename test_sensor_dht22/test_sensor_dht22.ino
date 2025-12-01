#include <DHT.h>

#define DHTPIN 2     // Pin digital tempat sensor DHT22 terhubung
#define DHTTYPE DHT22   // Tipe sensor DHT (DHT11, DHT22, DHT21, AM2302)

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(9600);
  Serial.println("DHTxx test!");
  dht.begin();
}

void loop() {
  // Delay a bit between measurements.
  delay(2000);

  // Read humidity and temperature
  float humidity = dht.readHumidity();
  float temperatureC = dht.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(humidity) || isnan(temperatureC)) {
    Serial.println("Gagal membaca dari sensor DHT!");
    return;
  }

  // Convert temperature to Fahrenheit
  float temperatureF = dht.readTemperature(true);

  // Compute heat index in Fahrenheit (the default)
  float hif = dht.computeHeatIndex(temperatureF, humidity);

  // Compute heat index in Celsius (isFahrenheit = false)
  float hic = dht.computeHeatIndex(temperatureC, humidity, false);

  Serial.print("Kelembaban: ");
  Serial.print(humidity);
  Serial.print(" %\t");
  Serial.print("Suhu: ");
  Serial.print(temperatureC);
  Serial.print(" *C ");
  Serial.print(temperatureF);
  Serial.print(" *F\t");
  Serial.print("Heat Index: ");
  Serial.print(hic);
  Serial.print(" *C, ");
  Serial.print(hif);
  Serial.println(" *F");
}