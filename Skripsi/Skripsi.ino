#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "DHTesp.h"

// ==== Sensor DHT22 ====
const int DHT_PIN = 0;
DHTesp dhtSensor;
float suhu, kelembaban;

// ==== Sensor Ultrasonik ====
const int trigPin = 2;
const int echoPin = 1;
long duration;
float V = 0;

// ==== WiFi ====
const char* ssid = "Pisnap";
const char* password = "qwerty12345";

// ==== Web server & WebSocket ====
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

void setup() {
  Serial.begin(115200);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Koneksi WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());

  // Web server
  server.on("/", handleRoot);
  server.begin();

  // WebSocket server
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
}

void loop() {
  server.handleClient();
  webSocket.loop();

  // Baca sensor DHT22
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  suhu = data.temperature;
  kelembaban = data.humidity;

  // Baca sensor ultrasonik
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH, 30000); // timeout 30ms

  float h = (duration / 2.0) / 29.1;  // Jarak dalam cm
  V = 3.14 * 3 * h;  // volume madu estimasi silinder (π * r^2 * h, r=3cm)

  if (!isnan(suhu) && !isnan(kelembaban) && V > 0 && V < 10000) {
    String json = "{\"suhu\":" + String(suhu, 1) +
                  ",\"hum\":" + String(kelembaban, 1) +
                  ",\"vol\":" + String(V, 1) + "}";
    webSocket.broadcastTXT(json);
    // Serial.println(json);  // Debug ke Serial Monitor
  } else {
    Serial.println("Sensor data tidak valid, dilewati...");
  }

  delay(1000); // refresh setiap detik
}

// ==== HTML Web Page ====
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>Monitoring Real-Time</title>
    <meta charset="UTF-8">
    <style>
      body {
        background: linear-gradient(to right, #141e30, #243b55);
        color: #fff;
        font-family: Arial, sans-serif;
        text-align: center;
        padding: 50px;
      }
      .card {
        background: rgba(255,255,255,0.1);
        padding: 30px;
        border-radius: 20px;
        box-shadow: 0 8px 32px rgba(0,0,0,0.5);
        max-width: 400px;
        margin: auto;
      }
      h1 { font-size: 2em; }
      p { font-size: 1.2em; }
    </style>
  </head>
  <body>
    <div class="card">
      <h1>📟 Monitoring Sensor</h1>
      <p>Suhu: <span id="suhu">--</span> °C</p>
      <p>Kelembaban: <span id="hum">--</span> %</p>
      <p>Volume Madu: <span id="vol">--</span> ml</p>
    </div>

    <script>
      var gateway = `ws://${window.location.hostname}:81/`;
      var websocket;

      window.addEventListener('load', onLoad);

      function onLoad() {
        websocket = new WebSocket(gateway);
        websocket.onmessage = function(event) {
          try {
            var data = JSON.parse(event.data);
            document.getElementById('suhu').textContent = data.suhu.toFixed(1);
            document.getElementById('hum').textContent = data.hum.toFixed(1);
            document.getElementById('vol').textContent = data.vol.toFixed(1);
          } catch (e) {
            console.error("Gagal parsing JSON:", e, event.data);
          }
        };
      }
    </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// ==== WebSocket Event Handler ====
void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("Client %u connected\n", client_num);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("Client %u disconnected\n", client_num);
  }
}
