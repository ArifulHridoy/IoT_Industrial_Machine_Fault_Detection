#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

//==================== CREDENTIALS ====================
const char* ssid = "KJAH_507";
const char* password = "13341763";

// Direct Access Point Backup Config
const char* ap_ssid = "ESP32-Dashboard";
const char* ap_pass = "12345678";

WebServer server(80);

//==================== PINS ====================
#define ONE_WIRE_BUS   4
#define MQ2_PIN        34
#define FLAME_PIN      14
#define VIBRATION_PIN  13
#define TRIG_PIN       26
#define ECHO_PIN       33
#define CURRENT_PIN    35
#define VOLTAGE_PIN    32
#define RELAY_PIN      23
#define IR_PIN         27

//==================== OBJECTS =================
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

//==================== SETTINGS =================
const float MAX_DISTANCE = 12.74;
const float MAX_FUEL = 1350.0;
const int GAS_THRESHOLD = 2500;
const float FAN_ON_TEMP = 45.0;

// Global metric storage
float temp = 0.0;
int smoke = 0;
int flame = HIGH;
int vibration = LOW;
int ir = HIGH;
int currentADC = 0;
int voltageADC = 0;
float distance = 0.0;
float fuelML = 0.0;
float fuelPercent = 0.0;
bool relayState = false;

// Non-blocking timers
unsigned long lastTempUpdate = 0;
unsigned long lastDistanceUpdate = 0;
unsigned long lastSerialPrint = 0;

//==================== FUNCTIONS =================
float getDistance() {
  // Clear trigger pin for a clean pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 10,000us (10ms) timeout provides accurate readings without locking up
  long duration = pulseIn(ECHO_PIN, HIGH, 10000);

  if (duration <= 0)
    return -1;

  return duration * 0.0343 / 2.0;
}

// Front-End Web Page Shell
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Industrial Dashboard</title>
    <style>
        body {
            background-color: #0f172a;
            color: #f8fafc;
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            text-align: center;
        }
        h1 {
            color: #00ff88;
            margin-bottom: 20px;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            max-width: 1000px;
            margin: 0 auto;
        }
        .card {
            background: #1e293b;
            padding: 20px;
            border-radius: 12px;
            border: 1px solid #334155;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
        }
        .label {
            font-size: 13px;
            color: #94a3b8;
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 8px;
        }
        .value {
            font-size: 26px;
            font-weight: bold;
        }
        .ok { color: #00ff88; }
        .alert { color: #ef4444; }
        .warn { color: #f59e0b; }
    </style>
</head>
<body>

<h1>INDUSTRIAL MONITORING SYSTEM</h1>

<div class="grid">
    <div class="card"><div class="label">Temperature</div><div class="value ok" id="temp">--°C</div></div>
    <div class="card"><div class="label">Gas / Smoke</div><div class="value" id="smoke">--</div></div>
    <div class="card"><div class="label">Flame</div><div class="value" id="flame">--</div></div>
    <div class="card"><div class="label">Vibration</div><div class="value" id="vibration">--</div></div>
    <div class="card"><div class="label">IR Sensor</div><div class="value" id="ir">--</div></div>
    <div class="card"><div class="label">Current ADC</div><div class="value" id="currentADC">--</div></div>
    <div class="card"><div class="label">Voltage ADC</div><div class="value" id="voltageADC">--</div></div>
    <div class="card"><div class="label">Fuel Level</div><div class="value" id="fuelPercent">--%</div></div>
    <div class="card"><div class="label">Fan / Relay</div><div class="value" id="relay">--</div></div>
    <div class="card"><div class="label">Object Status</div><div class="value" id="objStatus">--</div></div>
    <div class="card"><div class="label">Machine Status</div><div class="value" id="status">--</div></div>
</div>

<script>
function getData() {
    fetch('/data')
        .then(res => res.json())
        .then(d => {
            document.getElementById('temp').innerText = d.temp.toFixed(1) + " °C";
            document.getElementById('smoke').innerText = d.smoke + " (" + (d.smoke > 2500 ? "DETECTED" : "NORMAL") + ")";
            document.getElementById('smoke').className = "value " + (d.smoke > 2500 ? "alert" : "ok");

            let flameEl = document.getElementById('flame');
            flameEl.innerText = d.flame === 0 ? "FIRE DETECTED" : "NO FIRE";
            flameEl.className = "value " + (d.flame === 0 ? "alert" : "ok");

            let vibEl = document.getElementById('vibration');
            vibEl.innerText = d.vibration === 1 ? "HIGH" : "NORMAL";
            vibEl.className = "value " + (d.vibration === 1 ? "warn" : "ok");

            let irEl = document.getElementById('ir');
            irEl.innerText = d.ir === 0 ? "DETECTED" : "CLEAR";
            irEl.className = "value " + (d.ir === 0 ? "warn" : "ok");

            document.getElementById('currentADC').innerText = d.currentADC;
            document.getElementById('voltageADC').innerText = d.voltageADC;

            document.getElementById('fuelPercent').innerText = d.fuelPercent.toFixed(1) + "% (" + d.fuelML.toFixed(0) + " mL)";
            
            let relayEl = document.getElementById('relay');
            relayEl.innerText = d.relay ? "ON" : "OFF";
            relayEl.className = "value " + (d.relay ? "ok" : "alert");

            // Dedicated Object Status UI logic
            let objEl = document.getElementById('objStatus');
            if (d.ir === 0) {
                objEl.innerText = "STAY AWAY";
                objEl.className = "value alert";
            } else {
                objEl.innerText = "ALL CLEAR";
                objEl.className = "value ok";
            }

            let statusEl = document.getElementById('status');
            if(d.fault) {
                statusEl.innerText = "FAULT DETECTED";
                statusEl.className = "value alert";
            } else {
                statusEl.innerText = "NORMAL";
                statusEl.className = "value ok";
            }
        })
        .catch(e => console.error(e));
}
setInterval(getData, 1000);
window.onload = getData;
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

// Background JSON endpoint
void handleData() {
  // IR detection removed from machine fault calculation
  bool fault = (smoke > GAS_THRESHOLD) || (flame == LOW) || (vibration == HIGH);

  String json = "{";
  json += "\"temp\":" + String(temp, 1) + ",";
  json += "\"smoke\":" + String(smoke) + ",";
  json += "\"flame\":" + String(flame) + ",";
  json += "\"vibration\":" + String(vibration) + ",";
  json += "\"ir\":" + String(ir) + ",";
  json += "\"currentADC\":" + String(currentADC) + ",";
  json += "\"voltageADC\":" + String(voltageADC) + ",";
  json += "\"distance\":" + String(distance, 2) + ",";
  json += "\"fuelML\":" + String(fuelML, 0) + ",";
  json += "\"fuelPercent\":" + String(fuelPercent, 1) + ",";
  json += "\"relay\":" + String(relayState ? "true" : "false") + ",";
  json += "\"fault\":" + String(fault ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

//================================================
void setup() {
  Serial.begin(115200);

  sensors.begin();
  sensors.setWaitForConversion(false); // Asynchronous read mode

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(FLAME_PIN, INPUT);
  pinMode(VIBRATION_PIN, INPUT);
  pinMode(IR_PIN, INPUT);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Relay OFF (Active LOW)

  // Configure Dual Wi-Fi (Station + Direct Hotspot)
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);
  WiFi.softAP(ap_ssid, ap_pass);

  Serial.println();
  Serial.println("======================================");
  Serial.println(" INDUSTRIAL MONITORING SYSTEM ");
  Serial.println("======================================");

  Serial.print("Connecting to Wi-Fi Router...");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("1. Router Web Address  : http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Router connection failed. Using Direct AP Mode only.");
  }

  Serial.print("2. Direct Hotspot Wi-Fi: ");
  Serial.println(ap_ssid);
  Serial.print("   Direct Web Address  : http://");
  Serial.println(WiFi.softAPIP()); // 192.168.4.1

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("======================================");
  Serial.println("Web Server Started!");
}

void loop() {
  server.handleClient(); // Priority process incoming browser traffic

  // Instantaneous Analog & Digital Sensor Reads
  smoke = analogRead(MQ2_PIN);
  flame = digitalRead(FLAME_PIN);
  vibration = digitalRead(VIBRATION_PIN);
  ir = digitalRead(IR_PIN);
  currentADC = analogRead(CURRENT_PIN);
  voltageADC = analogRead(VOLTAGE_PIN);

  // Non-blocking temperature query every 1000ms
  if (millis() - lastTempUpdate >= 1000) {
    temp = sensors.getTempCByIndex(0);
    sensors.requestTemperatures();
    lastTempUpdate = millis();
  }

  // Non-blocking distance reading every 100ms
  if (millis() - lastDistanceUpdate >= 100) {
    float newDist = getDistance();
    
    // Only update measurements if a valid pulse returned
    if (newDist >= 0) {
      distance = newDist;
      
      if (distance > MAX_DISTANCE)
        distance = MAX_DISTANCE;

      fuelML = ((MAX_DISTANCE - distance) / MAX_DISTANCE) * MAX_FUEL;
      fuelPercent = (fuelML / MAX_FUEL) * 100.0;
    }
    lastDistanceUpdate = millis();
  }

  // Relay / Fan Control Logic
  if (temp >= FAN_ON_TEMP) {
    digitalWrite(RELAY_PIN, LOW); // ON
    relayState = true;
  } else {
    digitalWrite(RELAY_PIN, HIGH); // OFF
    relayState = false;
  }

  // Serial Monitor Output every 1000ms
  if (millis() - lastSerialPrint >= 1000) {
    Serial.println();
    Serial.println("==========================================");
    Serial.print("Temperature    : "); Serial.print(temp); Serial.println(" C");
    Serial.print("Smoke ADC      : "); Serial.println(smoke);
    Serial.print("Gas Status     : "); Serial.println(smoke > GAS_THRESHOLD ? "DETECTED" : "NORMAL");
    Serial.print("Flame          : "); Serial.println(flame == LOW ? "FIRE DETECTED" : "NO FIRE");
    Serial.print("Vibration      : "); Serial.println(vibration == HIGH ? "HIGH" : "NORMAL");
    Serial.print("IR Sensor      : "); Serial.println(ir == LOW ? "OBJECT DETECTED" : "CLEAR");

    if (distance < 0) {
      Serial.println("Distance       : ERROR");
    } else {
      Serial.print("Distance       : "); Serial.print(distance, 2); Serial.println(" cm");
      Serial.print("Fuel           : "); Serial.print(fuelML, 0); Serial.println(" mL");
      Serial.print("Fuel Level     : "); Serial.print(fuelPercent, 1); Serial.println("%");
    }

    Serial.print("Current ADC    : "); Serial.println(currentADC);
    Serial.print("Voltage ADC    : "); Serial.println(voltageADC);
    Serial.print("Relay          : "); Serial.println(relayState ? "ON" : "OFF");

    // IR detection excluded from fault trigger here as well
    bool fault = (smoke > GAS_THRESHOLD) || (flame == LOW) || (vibration == HIGH);
    Serial.print("Machine Status : "); Serial.println(fault ? "FAULT DETECTED" : "NORMAL");
    Serial.println("==========================================");

    lastSerialPrint = millis();
  }
}
