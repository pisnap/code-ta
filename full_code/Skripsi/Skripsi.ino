#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include "DHTesp.h"
#include <WiFiManager.h>       // Added WiFiManager library
#include <LiquidCrystal_I2C.h> // Added for LCD

// Sensor Setup
#define DHT_PIN 0
#define trigPin 1
#define echoPin 2
#define HX710B_SCK_PIN 3
#define HX710B_DOUT_PIN 4
#define PUMP_BUTTON_PIN 10    // Added for pump control
#define PUMP_10S_BUTTON_PIN 5 // Button for 10-second pump run
#define PUMP_20S_BUTTON_PIN 6 // Button for 20-second pump run
#define WIFI_RESET_PIN 7      // Button to reset WiFi settings
#define IN1 21
#define IN2 20

DHTesp dhtSensor;
float suhu, suhu_awal, kelembaban, V, pressure, jarak_sensor_ke_cairan;

// Global variables for timed pump control
unsigned long pumpRunStartTime = 0;
unsigned long pumpRunDuration = 0;
bool pumpRunningTimed = false;

// WiFiManager object
WiFiManager wifiManager;

class HX710B
{
private:
  uint8_t dout_pin;
  uint8_t sck_pin;
  long data;

public:
  HX710B(uint8_t dout, uint8_t sck)
  {
    dout_pin = dout;
    sck_pin = sck;
  }

  void begin()
  {
    pinMode(sck_pin, OUTPUT);
    pinMode(dout_pin, INPUT);
    digitalWrite(sck_pin, LOW);
  }

  bool is_ready()
  {
    return digitalRead(dout_pin) == LOW;
  }

  long read()
  {
    // Wait for the sensor to become ready
    while (!is_ready())
    {
      delayMicroseconds(10);
    }

    unsigned long count = 0;
    uint8_t i;

    noInterrupts();

    // Read 24 bits of data
    for (i = 0; i < 24; i++)
    {
      digitalWrite(sck_pin, HIGH);
      delayMicroseconds(1);
      count = count << 1;
      digitalWrite(sck_pin, LOW);
      delayMicroseconds(1);
      if (digitalRead(dout_pin))
      {
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
    if (count & 0x800000)
    {
      count |= 0xFF000000;
    }

    return (long)count;
  }
};

HX710B sensor(HX710B_DOUT_PIN, HX710B_SCK_PIN);

const long offset_raw_value = 103200;      // example baseline raw reading (no pressure or reference pressure)
const float offset_pressure_bar = 1.01325; // baseline pressure in bar (approx atmospheric pressure, 1013.25 hPa = 1.01325 bar)
const float scale_factor = 0.00001;        // bar per raw count (example value, you need to adjust based on calibration)

// ===== Web Server and WebSocket =====
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ===== Firebase Setup =====
String firebaseHost = "https://monitoring-alat-pengisap-madu-default-rtdb.asia-southeast1.firebasedatabase.app/";
String firebasePath = "/monitoring.json"; // endpoint
String firebaseAuth = "";                 // Jika project butuh token, isi di sini

// ===== Fungsi Deklarasi =====
// void handleRoot();
// void handleSaveData();
// void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t* payload, size_t length);
// void bacaSensor();

void bacaSensor()
{
  TempAndHumidity data = dhtSensor.getTempAndHumidity();

  suhu_awal = data.temperature;
  suhu = suhu_awal-2;
  kelembaban = data.humidity;

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);

  float tinggi_toples = 85; // mm
  float jarijari = 44.5;    // mm

  // h = tinggi cairan, hasil pengukuran sensor
  jarak_sensor_ke_cairan = ((0.34 * duration) / 2); // mm
  float tinggi_cairan = tinggi_toples - jarak_sensor_ke_cairan;
  if (tinggi_cairan < 0)
  {
    tinggi_cairan = 0;
  }

  float volume = 3.1416 * jarijari * jarijari * tinggi_cairan; // mm^3
  V = volume / 1000.0; // ml
  V = 0.9664*V+42.773;
}

// ===== WebPage HTML =====
void handleRoot()
{
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Monitoring Realtime</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.2/dist/css/bootstrap.min.css" rel="stylesheet">
    <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
    <style>
        body { background: #f8f9fa; }
        .container { max-width: 100%; margin-top: 30px; }
        .card { margin-bottom: 20px; }
        .sensor-card .card-body {
            min-height: 130px;
            padding: 28px 8px 18px 8px;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
        }
        .sensor-card h3 {
            font-size: 2.2rem;
            margin: 0;
            padding: 0;
        }
        .sensor-card canvas {
            display: block;
            margin: 0 auto;
            height: 48px !important;
            width: 90px !important;
        }
        .chart-table-row { display: flex; gap: 20px; }
        .chart-col, .table-col { flex: 1 1 0; min-width: 0; }
        .chart-card #sensorChart { height: 420px !important; max-height: 420px; }
        .chart-scroll-x {
            overflow-x: auto;
            -webkit-overflow-scrolling: touch;
            padding-bottom: 10px;
        }
        @media (max-width: 900px) {
            .chart-card #sensorChart { height: 340px !important; max-height: 340px; }
            .chart-table-row { flex-direction: column; gap: 0; }
            .chart-col, .table-col { width: 100%; }
        }
        @media (max-width: 600px) {
            .table-responsive { font-size: 0.9rem; }
            .sensor-card .card-body { min-height: 90px; padding: 14px 2px 10px 2px; }
            .sensor-card h3 { font-size: 1.3rem; }
            .sensor-card canvas { height: 32px !important; width: 60px !important; }
            .chart-card #sensorChart { height: 280px !important; max-height: 280px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h2 class="mb-4 text-center">Monitoring Realtime</h2>
        <!-- Live Sensor Data -->
        <div class="row mb-4">
            <div class="col-6 col-md-3">
                <div class="card text-center sensor-card">
                    <div class="card-body">
                        <h6>Suhu (&deg;C)</h6>
                        <h3 id="suhu">--</h3>
                    </div>
                </div>
            </div>
            <div class="col-6 col-md-3">
                <div class="card text-center sensor-card">
                    <div class="card-body">
                        <h6>Kelembaban (%)</h6>
                        <h3 id="hum">--</h3>
                    </div>
                </div>
            </div>
            <div class="col-6 col-md-3">
                <div class="card text-center sensor-card">
                    <div class="card-body">
                        <h6>Volume (ml)</h6>
                        <h3 id="vol">--</h3>
                    </div>
                </div>
            </div>
            <div class="col-6 col-md-3">
                <div class="card text-center sensor-card">
                    <div class="card-body">
                        <h6>Tekanan</h6>
                        <canvas id="pressureGauge" width="90" height="48"></canvas>
                    </div>
                </div>
            </div>
        </div>
        <div class="mb-4 text-center">
            <button class="btn btn-success" id="saveBtn">Simpan Data</button>
        </div>
        <!-- Chart and Table -->
        <div class="chart-table-row">
            <div class="chart-col">
                <div class="card mb-4 chart-card">
                    <div class="card-body">
                        <h5 class="card-title">Grafik 10 Data Terbaru</h5>
                        <div class="chart-scroll-x"><div id="sensorChart" style="min-width:600px;"></div></div>
                    </div>
                </div>
            </div>
            <div class="table-col">
                <div class="card">
                    <div class="card-body">
                        <h5 class="card-title">Data Tersimpan</h5>
                        <div class="d-flex justify-content-between align-items-center mb-2 flex-wrap">
                            <div>
                                <label for="rowsPerPage" class="form-label mb-0 me-2">Tampilkan</label>
                                <select id="rowsPerPage" class="form-select d-inline-block w-auto">
                                    <option value="5">5</option>
                                    <option value="10" selected>10</option>
                                    <option value="15">15</option>
                                    <option value="20">20</option>
                                </select>
                                <span>data</span>
                            </div>
                            <div class="d-flex justify-content-end flex-grow-1">
                                <button id="prevPage" class="btn btn-outline-secondary btn-sm me-1">Previous</button>
                                <button id="nextPage" class="btn btn-outline-secondary btn-sm">Next</button>
                            </div>
                        </div>
                        <div class="table-responsive">
                            <table class="table table-striped table-bordered align-middle">
                                <thead>
                                    <tr>
                                        <th>Waktu</th>
                                        <th>Suhu (&deg;C)</th>
                                        <th>Kelembaban (%)</th>
                                        <th>Volume (ml)</th>
                                        <th>Aksi</th>
                                    </tr>
                                </thead>
                                <tbody id="dataTable"></tbody>
                            </table>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>
    <script>
    // --- SENSOR DATA VIA WEBSOCKET ---
    var gateway = `ws://${window.location.hostname}:81/`;
    var websocket;
    var latestData = {};
    const firebaseUrl = "https://monitoring-alat-pengisap-madu-default-rtdb.asia-southeast1.firebasedatabase.app/monitoring.json";
    let chart;
    let allEntries = [], currentPage = 1, dataPerPage = 10;
    let pressureGaugeCtx;
    const MAX_PRESSURE_GAUGE = 5;
    window.addEventListener('load', onLoad);
    function onLoad() {
        websocket = new WebSocket(gateway);
        websocket.onmessage = function (event) {
            try {
                var data = JSON.parse(event.data);
                console.log('Data Sensor:', data);
                document.getElementById('suhu').textContent = (data.suhu === "error") ? "error" : parseInt(data.suhu);
                document.getElementById('hum').textContent = (data.hum === "error") ? "error" : parseInt(data.hum);
                document.getElementById('vol').textContent = (data.vol === "error") ? "error" : Number(data.vol).toFixed(2);
                updatePressureGauge(data.tekanan);
                latestData = data;
            } catch (e) { console.error("WebSocket error:", e); }
        };
        loadAllData();
        document.getElementById('saveBtn').onclick = saveData;
        document.getElementById('rowsPerPage').onchange = function() {
            dataPerPage = parseInt(this.value);
            currentPage = 1;
            renderTable();
            renderPagination();
        };
        document.getElementById('prevPage').onclick = function() {
            if (currentPage > 1) { currentPage--; renderTable(); renderPagination(); }
        };
        document.getElementById('nextPage').onclick = function() {
            const totalPages = Math.ceil(allEntries.length/dataPerPage);
            if (currentPage < totalPages) { currentPage++; renderTable(); renderPagination(); }
        };
        const canvas = document.getElementById('pressureGauge');
        if (canvas) {
            pressureGaugeCtx = canvas.getContext('2d');
            updatePressureGauge(0);
        }
    }
    function saveData() {
        if (!latestData.suhu || latestData.suhu === "error") { alert("Data belum tersedia!"); return; }
        // Only save suhu, hum, vol
        const dataToSave = {
            waktu: new Date().toISOString(),
            suhu: latestData.suhu,
            hum: latestData.hum,
            vol: latestData.vol
        };
        fetch('/save', {
            method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(dataToSave)
        })
        .then(response => response.text())
        .then(result => { alert('Data tersimpan!'); loadAllData(); })
        .catch(error => { alert('Gagal simpan data!'); });
    }
    function loadAllData() {
        fetch(firebaseUrl)
        .then(response => response.json())
        .then(data => {
            allEntries = data ? Object.entries(data).sort((a, b) => new Date(b[1].waktu) - new Date(a[1].waktu)) : [];
            renderTable();
            renderChart();
            renderPagination();
        })
        .catch(error => { console.error('Error load data:', error); });
    }
    function renderTable() {
        const tbody = document.getElementById('dataTable');
        tbody.innerHTML = "";
        const pageData = allEntries.slice((currentPage-1)*dataPerPage, currentPage*dataPerPage);
        pageData.forEach(([key, value]) => {
            const row = `<tr><td>${new Date(value.waktu).toLocaleString()}</td><td>${value.suhu === "error" ? "error" : parseInt(value.suhu)}</td><td>${value.hum === "error" ? "error" : parseInt(value.hum)}</td><td>${value.vol === "error" ? "error" : Number(value.vol).toFixed(2)}</td><td><button class='btn btn-danger btn-sm' onclick="deleteData('${key}')">Hapus</button></td></tr>`;
            tbody.innerHTML += row;
        });
    }
    function renderPagination() {
        document.getElementById('prevPage').disabled = currentPage === 1;
        const totalPages = Math.ceil(allEntries.length/dataPerPage);
        document.getElementById('nextPage').disabled = currentPage === totalPages || totalPages === 0;
    }
    function deleteData(key) {
        fetch("https://monitoring-alat-pengisap-madu-default-rtdb.asia-southeast1.firebasedatabase.app/monitoring/" + key + ".json", { method: 'DELETE' })
        .then(response => { loadAllData(); })
        .catch(error => { alert('Gagal hapus data!'); });
    }
    function renderChart() {
        const chartData = allEntries.slice(0,10).reverse();
        // Prepare data for Google Charts
        const dataArray = [['Waktu', 'Suhu (°C)', 'Kelembaban (%)', 'Volume (ml)']];
        chartData.forEach(([_, v]) => {
            const date = new Date(v.waktu);
            const day = String(date.getDate()).padStart(2, '0');
            const month = String(date.getMonth() + 1).padStart(2, '0');
            const waktuLabel = `${day}-${month}`;
            dataArray.push([
                waktuLabel,
                v.suhu === "error" ? null : Number(v.suhu),
                v.hum === "error" ? null : Number(v.hum),
                v.vol === "error" ? null : Number(v.vol)
            ]);
        });
        // Load Google Charts and draw
        google.charts.load('current', {'packages':['corechart']});
        google.charts.setOnLoadCallback(drawChart);
        function drawChart() {
            var data = google.visualization.arrayToDataTable(dataArray);
            var chartHeight = 420;
            if (window.innerWidth < 600) chartHeight = 280;
            else if (window.innerWidth < 900) chartHeight = 340;
            var options = {
                curveType: 'function',
                legend: { position: 'top', textStyle: { fontSize: 14, bold: true, color: '#333' } },
                chartArea: { left: 60, top: 50, width: '85%', height: '70%' },
                hAxis: {
                    title: 'Waktu (DD-MM)',
                    titleTextStyle: { fontSize: 13, italic: false, color: '#333' },
                    textStyle: { fontSize: 12 },
                    gridlines: { color: '#e0e7ef', count: 10 }
                },
                vAxis: {
                    minValue: 0,
                    title: '',
                    gridlines: { color: '#e0e7ef', count: 8 },
                    textStyle: { fontSize: 12 }
                },
                backgroundColor: { fill: 'transparent' },
                colors: ['#ff6384', '#36a2eb', '#ffce56'],
                pointSize: 7,
                lineWidth: 4,
                fontName: 'inherit',
                tooltip: { textStyle: { fontSize: 13 }, showColorCode: true },
                animation: { startup: true, duration: 700, easing: 'out' },
                width: '100%',
                height: chartHeight,
            };
            var chart = new google.visualization.LineChart(document.getElementById('sensorChart'));
            chart.draw(data, options);
        }
        // Redraw on window resize for responsiveness
        window.addEventListener('resize', () => {
            if (typeof google !== 'undefined' && google.visualization && google.visualization.LineChart) {
                drawChart();
            }
        }, { once: true });
    }
    function updatePressureGauge(pressure) {
        if (!pressureGaugeCtx) return;
        const canvas = pressureGaugeCtx.canvas;
        let ctx = pressureGaugeCtx;
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.beginPath();
        ctx.arc(canvas.width/2, canvas.height, canvas.width/2-5, Math.PI, 0);
        ctx.strokeStyle = '#cbd5e1';
        ctx.lineWidth = 8;
        ctx.stroke();
        let minP = -5, maxP = MAX_PRESSURE_GAUGE;
        let val = Math.max(minP, Math.min(maxP, parseFloat(pressure)||0));
        let angle = Math.PI * (1 - (val-minP)/(maxP-minP));
        let r = canvas.width/2-10;
        ctx.beginPath();
        ctx.moveTo(canvas.width/2, canvas.height);
        ctx.lineTo(canvas.width/2 + r*Math.cos(angle), canvas.height - r*Math.sin(angle));
        ctx.strokeStyle = '#4299e1';
        ctx.lineWidth = 4;
        ctx.stroke();
        // No number displayed
    }
    </script>
</body>
</html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// ===== WebSocket Event =====
void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t *payload, size_t length)
{
  if (type == WStype_CONNECTED)
  {
    Serial.printf("Client %u connected\n", client_num);
  }
  else if (type == WStype_DISCONNECTED)
  {
    Serial.printf("Client %u disconnected\n", client_num);
  }
}

// ===== Save Data ke Firebase =====
void handleSaveData()
{
  if (server.hasArg("plain") == false)
  {
    server.send(400, "text/plain", "Bad Request");
    return;
  }

  String payload = server.arg("plain");
  Serial.println("Saving to Firebase: " + payload);

  if ((WiFi.status() == WL_CONNECTED))
  {
    HTTPClient http;
    http.begin(firebaseHost + "monitoring.json");
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0)
    {
      String response = http.getString();
      server.send(200, "text/plain", "Data saved");
      Serial.println(response);
    }
    else
    {
      server.send(500, "text/plain", "Failed to send data");
      Serial.println("Error on sending POST");
    }
    http.end();
  }
  else
  {
    server.send(500, "text/plain", "WiFi Disconnected");
  }
}

LiquidCrystal_I2C lcd(0x27, 20, 4); // Ganti 0x27 jika alamat I2C LCD Anda berbeda

void setup()
{
  Serial.begin(115200);

  pinMode(WIFI_RESET_PIN, INPUT_PULLUP); // Initialize WiFi reset button pin

  // Check if WiFi reset button is pressed
  // Delay for a short period to allow the user to press the button during startup
  delay(100); // Small delay to catch button press
  if (digitalRead(WIFI_RESET_PIN) == LOW)
  {
    Serial.println("WiFi reset button pressed. Resetting WiFi settings...");
    wifiManager.resetSettings();
    Serial.println("WiFi settings have been reset. ESP32 will restart.");
    delay(1000);
    ESP.restart(); // Restart to apply reset and go into AP mode
  }

  sensor.begin();

  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(PUMP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PUMP_10S_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PUMP_20S_BUTTON_PIN, INPUT_PULLUP);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  lcd.init();
  lcd.backlight();

  // WiFiManager AutoConnect
  // Sets an AP name if it can't connect or has no saved credentials
  // You can set a custom AP name here, e.g., "SkripsiAP"
  wifiManager.setConnectTimeout(30); // Set connection timeout to 30 seconds
  if (!wifiManager.autoConnect("SkripsiSetupAP"))
  {
    Serial.println("Failed to connect and hit timeout");
    // ESP.restart(); // Optionally restart if connection fails after timeout
    // delay(1000);
  }

  Serial.println("WiFi connected!");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot); // Ensure HTTP_GET is specified for root
  server.on("/save", HTTP_POST, handleSaveData);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
}

void tampilkanDataLCD()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP:");
  lcd.print(WiFi.localIP());

  lcd.setCursor(0, 1);
  lcd.print("Tem:");
  lcd.print((int)suhu); // Tampilkan suhu sebagai integer
  lcd.print(" C");

  lcd.setCursor(12, 1);
  lcd.print("Hum:");
  lcd.print((int)kelembaban); // Tampilkan kelembaban sebagai integer
  lcd.print(" %");

  lcd.setCursor(0, 2);
  lcd.print("Vol:");
  lcd.print(V, 2);
  lcd.print(" ml");

  lcd.setCursor(0, 3);
  lcd.print("Press: ");
  if (pressure > 5)
  {
    lcd.print("Low");
  }
  else if (pressure < -5)
  {
    lcd.print("High");
  }
  else if (pressure >= -3 && pressure <= 3)
  {
    lcd.print("Medium");
  }
  else
  {
    lcd.print("-");
  }
}

void loop()
{
  server.handleClient();
  webSocket.loop();

  // --- Pump Control Logic ---
  bool manualPumpActive = (digitalRead(PUMP_BUTTON_PIN) == LOW);
  bool pump50ml = (digitalRead(PUMP_10S_BUTTON_PIN) == LOW);
  bool pump100ml = (digitalRead(PUMP_20S_BUTTON_PIN) == LOW);

  if (manualPumpActive)
  {
    // Manual pump operation overrides timed operation
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    pumpRunningTimed = false; // Cancel any ongoing timed operation
  }
  else
  {
    if (pump50ml && !pumpRunningTimed)
    {
      pumpRunStartTime = millis();
      pumpRunDuration = 84000;
      pumpRunningTimed = true;
      Serial.println("Starting 50ml pump run.");
    }
    else if (pump100ml && !pumpRunningTimed)
    {
      pumpRunStartTime = millis();
      pumpRunDuration = 168000;
      pumpRunningTimed = true;
      Serial.println("Starting 100ml pump run.");
    }

    if (pumpRunningTimed)
    {
      if (millis() - pumpRunStartTime < pumpRunDuration)
      {
        // Timed run is active and duration not yet elapsed
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, HIGH);
      }
      else
      {
        // Timed run duration has elapsed
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, LOW);
        pumpRunningTimed = false;
        Serial.println("Timed pump run finished.");
      }
    }
    else
    {
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, LOW);
    }
  }
  // --- End Pump Control Logic ---

  bacaSensor();
  long rawValue = sensor.read();
  // Serial.print("Raw HX710B reading: ");
  // Serial.println(rawValue);
  pressure = (rawValue - offset_raw_value) * scale_factor + offset_pressure_bar;
  // Serial.print("Calibrated Pressure (bar): ");
  // Serial.println(pressure, 5);

  if (!isnan(suhu) && !isnan(kelembaban))
  {
    String json = "{\"suhu\":" + String((int)suhu) +
                  ",\"hum\":" + String((int)kelembaban) +
                  ",\"vol\":" + String(V, 2) +
                  ",\"tekanan\":" + String(pressure, 5) +
                  ",\"jarak\":" + String(jarak_sensor_ke_cairan, 2) + "}";
    webSocket.broadcastTXT(json);
  }
  else
  {
    Serial.println("Sensor error, skip...");
  }

  tampilkanDataLCD();

  delay(500);
}
