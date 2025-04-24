#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>

// Wi-Fi & Telegram Config
const char* SSID     = "Deva";
const char* PASS     = "12345678";
const String BOT_TOK = "7774571503:AAHmXUZxbFPXxlIxT5XiRXqXXD2eDh4B34g";
const String CHAT_ID = "1305125457";
const String PASSWORD = "1234";

// Hardware Pins
const int LED_PIN = 2;

// Motion Sensor and States
Adafruit_MPU6050 mpu;
bool motionEnabled = false;
bool motionDetected = false;
bool vehicleLocked = true;

// Calibration
float ax_off, ay_off, az_off, gx_off, gy_off, gz_off;
const float ACC_T = 0.3, GYR_T = 0.05;

bool isStationary(float ax, float ay, float az, float gx, float gy, float gz) {
  return fabs(ax - ax_off - 9.81) < ACC_T &&
         fabs(ay - ay_off)       < ACC_T &&
         fabs(az - az_off - 2)   < ACC_T &&
         fabs(gx - gx_off)       < GYR_T &&
         fabs(gy - gy_off)       < GYR_T &&
         fabs(gz - gz_off)       < GYR_T;
}

void sendTelegram() {
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + BOT_TOK +
               "/sendMessage?chat_id=" + CHAT_ID +
               "&text=🚨%20Motion%20Detected!";
  http.begin(url);
  http.GET();
  http.end();
}

void calibrateSensor() {
  float sx=0, sy=0, sz=0, sgx=0, sgy=0, sgz=0;
  for(int i=0; i<100; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    sx  += a.acceleration.x;
    sy  += a.acceleration.y;
    sz  += a.acceleration.z;
    sgx += g.gyro.x;
    sgy += g.gyro.y;
    sgz += g.gyro.z;
    delay(10);
  }
  ax_off = sx/100 - 9.81;
  ay_off = sy/100;
  az_off = sz/100 - 2;
  gx_off = sgx/100;
  gy_off = sgy/100;
  gz_off = sgz/100;
}

AsyncWebServer server(80);

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { background: #eef2f5; font-family: 'Segoe UI', sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .card { background: #fff; padding: 2rem; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); text-align: center; max-width: 300px; width: 90%; }
    button { padding: 1rem; width: 100%; font-size: 1rem; margin-top: 1rem; border: none; border-radius: 8px; cursor: pointer; transition: 0.3s; }
    .unlock { background: #007bff; color: white; }
    .lock { background: #dc3545; color: white; }
    .detect { background: #28a745; color: white; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Vehicle Security</h2>
    <div id="controls"></div>
  </div>

  <script>
    async function fetchStatus() {
      const res = await fetch('/status');
      return await res.json();
    }

    async function renderUI() {
      const { locked, detection } = await fetchStatus();
      const controls = document.getElementById('controls');
      controls.innerHTML = '';

      if (locked) {
        controls.innerHTML += `<input type="password" id="pwd" placeholder="Enter password"><br><button class="unlock" onclick="unlock()">Unlock Vehicle</button>`;
        controls.innerHTML += `<button class="detect" onclick="toggleDetection()">${detection ? 'Disable' : 'Enable'} Detection</button>`;
      } else {
        controls.innerHTML += `<button class="lock" onclick="lock()">Lock Vehicle</button>`;
      }
    }

    async function unlock() {
      const pwd = document.getElementById('pwd').value;
      const res = await fetch(`/unlock?pwd=${pwd}`);
      const msg = await res.text();
      alert(msg);
      renderUI();
    }

    async function lock() {
      await fetch('/lock');
      renderUI();
    }

    async function toggleDetection() {
      await fetch('/toggle');
      renderUI();
    }

    renderUI();
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print('.');
  }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

  Wire.begin();
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found"); while (1);
  }
  calibrateSensor();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/unlock", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (req->hasParam("pwd")) {
      String pwd = req->getParam("pwd")->value();
      if (pwd == PASSWORD) {
        vehicleLocked = false;
        digitalWrite(LED_PIN, HIGH);
        req->send(200, "text/plain", "Vehicle Unlocked");
      } else {
        req->send(200, "text/plain", "Incorrect Password");
      }
    } else {
      req->send(400, "text/plain", "Missing Password");
    }
  });

  server.on("/lock", HTTP_GET, [](AsyncWebServerRequest *req) {
    vehicleLocked = true;
    motionEnabled = false;
    digitalWrite(LED_PIN, LOW);
    req->send(200, "text/plain", "Vehicle Locked");
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (vehicleLocked) {
      motionEnabled = !motionEnabled;
      req->send(200, "text/plain", motionEnabled ? "Detection Enabled" : "Detection Disabled");
    } else {
      req->send(403, "text/plain", "Cannot enable detection while vehicle is unlocked");
    }
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    String status = "{\"locked\": " + String(vehicleLocked ? "true" : "false") + ", \"detection\": " + String(motionEnabled ? "true" : "false") + "}";
    req->send(200, "application/json", status);
  });

  server.begin();
}

void loop() {
  float sx=0, sy=0, sz=0, sgx=0, sgy=0, sgz=0;
  for (int i = 0; i < 10; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    sx  += a.acceleration.x;
    sy  += a.acceleration.y;
    sz  += a.acceleration.z;
    sgx += g.gyro.x;
    sgy += g.gyro.y;
    sgz += g.gyro.z;
    delay(10);
  }
  float ax = sx/10, ay = sy/10, az = sz/10;
  float gx = sgx/10, gy = sgy/10, gz = sgz/10;

  motionDetected = !isStationary(ax, ay, az, gx, gy, gz);
  if (motionEnabled && motionDetected) {
    Serial.println("🚨 Motion Detected!");
    sendTelegram();
    delay(5000);
  }
}