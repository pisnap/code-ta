#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include "DHTesp.h"

// ===== Sensor Setup =====
const int DHT_PIN = 0;
DHTesp dhtSensor;
const int trigPin = 2;
const int echoPin = 1;
float suhu, kelembaban, V, pressure;

// setup hx710b
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

const long offset_raw_value = 103200;       // example baseline raw reading (no pressure or reference pressure)
const float offset_pressure_bar = 1.01325;  // baseline pressure in bar (approx atmospheric pressure, 1013.25 hPa = 1.01325 bar)
const float scale_factor = 0.00001;         // bar per raw count (example value, you need to adjust based on calibration)

// ===== WiFi Setup =====
const char* ssid = "Pisnap";           // Ganti dengan SSID WiFi kamu
const char* password = "qwerty12345";  // Ganti dengan Password WiFi kamu

// ===== Web Server and WebSocket =====
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ===== Firebase Setup =====
String firebaseHost = "https://monitoring-alat-pengisap-madu-default-rtdb.asia-southeast1.firebasedatabase.app/";
String firebasePath = "/monitoring.json";  // endpoint
String firebaseAuth = "";                  // Jika project butuh token, isi di sini

// ===== Fungsi Deklarasi =====
void handleRoot();
void handleSaveData();
void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t* payload, size_t length);
void bacaSensor();

void setup() {
  Serial.begin(115200);
  sensor.begin();

  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSaveData);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
}

void loop() {
  server.handleClient();
  webSocket.loop();

  bacaSensor();
  long rawValue = sensor.read();
  // Serial.print("Raw HX710B reading: ");
  // Serial.println(rawValue);
  pressure = (rawValue - offset_raw_value) * scale_factor + offset_pressure_bar;
  // Serial.print("Calibrated Pressure (bar): ");
  // Serial.println(pressure, 5);

  if (!isnan(suhu) && !isnan(kelembaban) && V < 10000) {
    String json = "{\"suhu\":" + String(suhu, 2) + ",\"hum\":" + String(kelembaban, 2) + ",\"vol\":" + String(V, 2) + ",\"tekanan\":" + String(pressure, 5) + "}";
    webSocket.broadcastTXT(json);
  } else {
    Serial.println("Sensor error, skip...");
  }

  delay(1000);  // delay 1 detik
}

void bacaSensor() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  suhu = data.temperature;
  kelembaban = data.humidity;

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  float h = ((duration / 2.0) / 29.1) - 10.5;
  V = 3.14 * 4.5 * h;  // Estimasi volume silinder, jari-jari 3cm
}

// ===== WebPage HTML =====
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">

<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Monitoring Realtime</title>
  <style>
    :root {
      --primary: #4CAF50;
      --accent: #2ecc71;
      --bg-dark: #1c1f2b;
      --bg-light: rgba(255, 255, 255, 0.05);
      --white: #ffffff;
      --border: #444;
    }

    body {
      background-color: var(--bg-dark);
      color: var(--white);
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      margin: 0;
      padding: 30px;
    }

    h1,
    h2 {
      margin-bottom: 10px;
      font-weight: 600;
      font-size: 24px;
      text-align: center;
    }

    .container {
      display: flex;
      flex-wrap: wrap;
      gap: 20px;
      margin-bottom: 30px;
    }

    .card {
      background: var(--bg-light);
      padding: 20px 25px;
      border-radius: 20px;
      flex: 1 1 45%;
      min-width: 300px;
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
      backdrop-filter: blur(6px);
    }

    .card p {
      font-size: 20px;
      text-align: center;
      margin: 10px 0;
    }

    .card h1 {
      font-size: 28px;
      text-align: center;
      margin-bottom: 50px;
    }

    .card button {
      display: block;
      margin: 50px auto 0 auto;
    }

    button {
      margin-top: 20px;
      padding: 12px 24px;
      border: none;
      background: var(--primary);
      color: var(--white);
      font-size: 16px;
      border-radius: 8px;
      cursor: pointer;
      transition: background 0.3s ease;
    }

    button:hover {
      background: var(--accent);
    }

    table {
      width: 100%;
      margin-top: 20px;
      background: var(--bg-light);
      border-collapse: collapse;
      border-radius: 12px;
      overflow: hidden;
      box-shadow: 0 3px 8px rgba(0, 0, 0, 0.2);
    }

    th,
    td {
      padding: 12px;
      border-bottom: 1px solid var(--border);
      text-align: center;
    }

    th {
      background: var(--primary);
      color: var(--white);
    }

    tr:hover {
      background-color: rgba(255, 255, 255, 0.1);
    }

    .btn-delete {
      background: #e74c3c;
      color: white;
      border: none;
      padding: 6px 12px;
      cursor: pointer;
      border-radius: 6px;
      transition: background 0.3s ease;
    }

    .btn-delete:hover {
      background: #c0392b;
    }

    .pagination {
      display: flex;
      justify-content: center;
      margin-top: 20px;
    }

    .pagination button {
      margin: 0 5px;
      padding: 5px 10px;
      cursor: pointer;
      background: #4CAF50;
      color: white;
      border: none;
      border-radius: 5px;
    }

    .pagination button:hover {
      background: var(--accent);
    }

    .pagination button.active {
      background: #2ecccc;
    }

    #myChart {
      background-color: #ffffff0d;
      border-radius: 12px;
      padding: 10px;
    }
  </style>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>

<body>

  <div class="container">
    <div class="card">
      <h1>📟 Monitoring Realtime</h1>
      <p>Suhu: <span id="suhu">--</span> °C</p>
      <p>Kelembaban: <span id="hum">--</span> %</p>
      <p>Volume: <span id="vol">--</span> ml</p>
      <p>Tekanan: <span id="pressure">--</span> bar</p>
      <button onclick="saveData()">💾 Simpan Data</button>
    </div>

    <div class="card">
      <h2>📊 Grafik Riwayat Data</h2>
      <canvas id="myChart" height="150"></canvas>
    </div>
  </div>

  <div class="card">
    <h2>📄 Riwayat Data</h2>
    <table id="data-table">
      <thead>
        <tr>
          <th>Waktu</th>
          <th>Suhu (°C)</th>
          <th>Kelembaban (%)</th>
          <th>Volume (ml)</th>
          <th>Aksi</th>
        </tr>
      </thead>
      <tbody></tbody>
    </table>

    <div class="pagination">
      <button onclick="changePage(5)" class="active">5</button>
      <button onclick="changePage(10)">10</button>
      <button onclick="changePage(20)">20</button>
      <button onclick="changePage('all')">All</button>
    </div>
  </div>

  <script>
    var gateway = `ws://${window.location.hostname}:81/`;
    var websocket;
    var latestData = {};
    const firebaseUrl = "https://monitoring-alat-pengisap-madu-default-rtdb.asia-southeast1.firebasedatabase.app/monitoring.json";
    var currentPage = 1;
    var dataPerPage = 5;
    let chart;

    window.addEventListener('load', onLoad);

    function onLoad() {
      websocket = new WebSocket(gateway);
      websocket.onmessage = function (event) {
        try {
          var data = JSON.parse(event.data);
          document.getElementById('suhu').textContent = data.suhu.toFixed(2);
          document.getElementById('hum').textContent = data.hum.toFixed(2);
          document.getElementById('vol').textContent = data.vol.toFixed(2);
          document.getElementById('pressure').textContent = data.tekanan.toFixed(5);

          latestData = {
            suhu: data.suhu,
            hum: data.hum,
            vol: data.vol,
            tekanan: data.tekanan,
            waktu: new Date().toISOString()
          };
        } catch (e) {
          console.error("WebSocket error:", e);
        }
      };

      loadSavedData();
      loadChartData();
    }

    function saveData() {
      if (!latestData.suhu) {
        alert("Data belum tersedia!");
        return;
      }

      fetch('/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(latestData)
      })
        .then(response => response.text())
        .then(result => {
          console.log(result);
          loadSavedData();
          loadChartData();
        })
        .catch(error => {
          console.error('Error save:', error);
        });
    }

    function loadSavedData() {
      fetch(firebaseUrl)
        .then(response => response.json())
        .then(data => {
          const tbody = document.getElementById('data-table').querySelector('tbody');
          tbody.innerHTML = "";
          if (data) {
            const entries = Object.entries(data).sort((a, b) => new Date(b[1].waktu) - new Date(a[1].waktu));
            const pageData = paginateData(entries, currentPage, dataPerPage);
            pageData.forEach(([key, value]) => {
              const row = `
              <tr>
                <td>${new Date(value.waktu).toLocaleString()}</td>
                <td>${value.suhu.toFixed(2)}</td>
                <td>${value.hum.toFixed(2)}</td>
                <td>${value.vol.toFixed(2)}</td>
                <td><button class="btn-delete" onclick="deleteData('${key}')">Hapus</button></td>
              </tr>
            `;
              tbody.innerHTML += row;
            });
          }
        })
        .catch(error => {
          console.error('Error load data:', error);
        });
    }

    function deleteData(key) {
      fetch("https://monitoring-alat-pengisap-madu-default-rtdb.asia-southeast1.firebasedatabase.app/monitoring/" + key + ".json", {
        method: 'DELETE',
      })
        .then(response => {
          console.log('Data deleted');
          loadSavedData();
          loadChartData();
        })
        .catch(error => {
          console.error('Error deleting data:', error);
        });
    }

    function paginateData(data, page, perPage) {
      const start = (page - 1) * perPage;
      const end = page * perPage;
      return data.slice(start, end);
    }

    function changePage(itemsPerPage) {
      dataPerPage = itemsPerPage === 'all' ? 1000 : itemsPerPage;
      currentPage = 1;
      document.querySelectorAll('.pagination button').forEach(button => button.classList.remove('active'));
      document.querySelector(`.pagination button:nth-child(${dataPerPage === 1000 ? 4 : dataPerPage / 5})`).classList.add('active');
      loadSavedData();
    }

    function loadChartData() {
      fetch(firebaseUrl)
        .then(response => response.json())
        .then(data => {
          if (data) {
            const entries = Object.entries(data)
              .sort((a, b) => new Date(b[1].waktu) - new Date(a[1].waktu))
              .slice(0, 10)
              .reverse();

            const waktu = entries.map(([_, v]) => {
              const date = new Date(v.waktu);
              const day = String(date.getDate()).padStart(2, '0');
              const month = String(date.getMonth() + 1).padStart(2, '0');
              return `${day}-${month}`;
            });
            const suhu = entries.map(([_, v]) => v.suhu);
            const hum = entries.map(([_, v]) => v.hum);
            const vol = entries.map(([_, v]) => v.vol);

            const ctx = document.getElementById('myChart').getContext('2d');
            if (chart) chart.destroy(); // Hapus chart lama

            chart = new Chart(ctx, {
              type: 'line',
              data: {
                labels: waktu,
                datasets: [
                  {
                    label: 'Suhu (°C)',
                    data: suhu,
                    borderColor: 'rgba(255,99,132,1)',
                    backgroundColor: 'rgba(255,99,132,0.2)',
                    fill: false,
                    tension: 0.3
                  },
                  {
                    label: 'Kelembaban (%)',
                    data: hum,
                    borderColor: 'rgba(54,162,235,1)',
                    backgroundColor: 'rgba(54,162,235,0.2)',
                    fill: false,
                    tension: 0.3
                  },
                  {
                    label: 'Volume (ml)',
                    data: vol,
                    borderColor: 'rgba(255,206,86,1)',
                    backgroundColor: 'rgba(255,206,86,0.2)',
                    fill: false,
                    tension: 0.3
                  }
                ]
              },
              options: {
                responsive: true,
                plugins: {
                  legend: { position: 'top' },
                  title: { display: true, text: 'Grafik Suhu, Kelembaban & Volume per Waktu' }
                },
                scales: {
                  x: { title: { display: true, text: 'Waktu' } },
                  y: { beginAtZero: true }
                }
              }
            });
          }
        })
        .catch(error => {
          console.error('Error loading chart data:', error);
        });
    }
  </script>
</body>
</html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// ===== WebSocket Event =====
void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("Client %u connected\n", client_num);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("Client %u disconnected\n", client_num);
  }
}

// ===== Save Data ke Firebase =====
void handleSaveData() {
  if (server.hasArg("plain") == false) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }

  String payload = server.arg("plain");
  Serial.println("Saving to Firebase: " + payload);

  if ((WiFi.status() == WL_CONNECTED)) {
    HTTPClient http;
    http.begin(firebaseHost + "monitoring.json");
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
      String response = http.getString();
      server.send(200, "text/plain", "Data saved");
      Serial.println(response);
    } else {
      server.send(500, "text/plain", "Failed to send data");
      Serial.println("Error on sending POST");
    }
    http.end();
  } else {
    server.send(500, "text/plain", "WiFi Disconnected");
  }
}
