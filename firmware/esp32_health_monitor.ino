/*
 * ClinicQ Health Monitor - ESP32 Cloud Firmware
 * ==============================================
 * 
 * Sensors: AD8232 (ECG) + MAX30100 (SpO2/HR)
 * Streams live data to ClinicQ cloud dashboard.
 * 
 * WIRING:
 * -------
 * AD8232 ECG:
 *   VCC → 3.3V, GND → GND
 *   OUTPUT → GPIO 34, LO+ → GPIO 32, LO- → GPIO 33
 * 
 * MAX30100:
 *   VIN → 3.3V, GND → GND, SDA → GPIO 21, SCL → GPIO 22
 * 
 * LIBRARIES: "WebSockets" by Markus Sattler, "MAX30100lib"
 * Board: ESP32 Dev Module
 */

// ===== INCLUDES =====
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"

// ================================================================
//  ★★★ EDIT THESE TWO LINES WITH YOUR WIFI CREDENTIALS ★★★
// ================================================================
const char* WIFI_SSID     = "ADI's_A55";       // ← Change this!
const char* WIFI_PASSWORD = "galaxy@123";    // ← Change this!
// ================================================================

// ===== CLOUD SERVER (DO NOT CHANGE) =====
const char* SERVER_HOST = "clinicq-health-monitor.onrender.com";
const uint16_t SERVER_PORT = 443;

// ===== PIN DEFINITIONS =====
const int PIN_ECG_INPUT = 34;
const int PIN_LO_PLUS  = 32;
const int PIN_LO_MINUS = 33;

// ===== OBJECTS =====
SocketIOclient socketIO;
PulseOximeter pox;

// ===== MAX30100 GLOBALS =====
bool maxSensorFound = false;    // ★ Skip sensor if not found
float global_SpO2 = 0.0;
float global_HR = 0.0;
unsigned long lastBeatDetected = 0;
bool beatDetected = false;

// ===== MAX30100 BEAT CALLBACK =====
void onBeatDetected() {
    beatDetected = true;
    lastBeatDetected = millis();
    Serial.println("💓 Beat detected!");
}

// ===== ECG FILTER VARIABLES =====
float hp_y = 0, hp_x_prev = 0;
float lp_y = 0;

// ===== ECG BEAT DETECTION =====
const int MAX_RR_HIST = 30;
unsigned long rrIntervals[MAX_RR_HIST];
int rrCount = 0;
unsigned long lastBeatTime = 0;
float ecgBPM = 0;
float ecgHRV = 0;
float adaptiveThresh = 0;
float lastFilteredECG = 0;

// ===== TIMING =====
unsigned long lastECGSend = 0;
unsigned long lastVitalsPrint = 0;
const unsigned long ECG_SEND_INTERVAL = 40;       // 25Hz → cloud
const unsigned long VITALS_PRINT_INTERVAL = 1000; // 1Hz → serial debug

// ===== CONNECTION STATE =====
bool isConnected = false;


// ================================================================
//  BANDPASS FILTER (0.5Hz - 40Hz)
// ================================================================
float bandpassFilter(float x) {
    float alpha_hp = 0.995;
    hp_y = alpha_hp * (hp_y + x - hp_x_prev);
    hp_x_prev = x;

    float alpha_lp = 0.15;
    lp_y = lp_y + alpha_lp * (hp_y - lp_y);

    return lp_y;
}


// ================================================================
//  SOCKET.IO EVENT HANDLER
// ================================================================
void socketIOEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            Serial.println("❌ Disconnected from cloud");
            isConnected = false;
            break;

        case sIOtype_CONNECT:
            Serial.println("✅ Connected to ClinicQ Cloud!");
            Serial.println("🌐 Dashboard: https://clinicq-health-monitor.onrender.com");
            isConnected = true;
            socketIO.send(sIOtype_CONNECT, "/");
            break;

        case sIOtype_EVENT:
        case sIOtype_ACK:
        case sIOtype_ERROR:
        case sIOtype_BINARY_EVENT:
        case sIOtype_BINARY_ACK:
            break;
    }
}


// ================================================================
//  WIFI CONNECTION
// ================================================================
void connectToWiFi() {
    Serial.print("📶 Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi Connected!");
        Serial.print("📍 IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n❌ WiFi Failed!");
        delay(5000);
        ESP.restart();
    }
}


// ================================================================
//  MAX30100 INITIALIZATION
// ================================================================
void initMAX30100() {
    Serial.println("🔧 Initializing MAX30100 sensor...");

    Wire.begin(21, 22);
    Wire.setClock(400000);  // 400kHz I2C

    if (!pox.begin()) {
        Serial.println("⚠️  MAX30100 not found! SpO2/HR will show 0.");
        Serial.println("    ECG + Cloud will still work.");
        maxSensorFound = false;
    } else {
        // Same LED current as your working local code
        pox.setIRLedCurrent(MAX30100_LED_CURR_24MA);
        pox.setOnBeatDetectedCallback(onBeatDetected);
        maxSensorFound = true;
        Serial.println("✅ MAX30100 initialized! Place finger on sensor.");
    }
}


// readMAX30100() is no longer needed — pox.update() is called
// directly in loop() every iteration (like the working local code)

// ================================================================
//  SEND DATA TO CLOUD
// ================================================================
void sendToCloud(float ecgValue) {
    if (!isConnected) return;

    int mappedECG = map(constrain((int)ecgValue, -500, 1500), -500, 1500, -60, 150);

    // Use MAX30100 HR if available, otherwise use ECG-derived BPM
    int bestBPM = (global_HR > 20) ? (int)global_HR : (int)ecgBPM;
    int bestSpO2 = (int)global_SpO2;

    String packet = "[\"sensor_data\",{";
    packet += "\"bpm\":";   packet += bestBPM;
    packet += ",\"spo2\":"; packet += bestSpO2;
    packet += ",\"ecg\":";  packet += mappedECG;
    packet += ",\"temp\":36.6";
    packet += "}]";

    socketIO.sendEVENT(packet);
}


// ================================================================
//  SETUP
// ================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n🏥 ClinicQ Health Monitor Starting...");

    // Pin setup
    pinMode(PIN_ECG_INPUT, INPUT);
    pinMode(PIN_LO_PLUS, INPUT);
    pinMode(PIN_LO_MINUS, INPUT);
    analogReadResolution(12);

    // 1. Connect to WiFi
    connectToWiFi();

    // 2. Initialize MAX30100
    initMAX30100();

    // 3. Connect to Cloud Server
    Serial.print("🔌 Connecting to cloud: ");
    Serial.println(SERVER_HOST);
    socketIO.beginSSL(SERVER_HOST, SERVER_PORT, "/socket.io/?EIO=4");
    socketIO.onEvent(socketIOEvent);

    Serial.println("✅ Setup complete!\n");
}


// ================================================================
//  MAIN LOOP
//  ★ KEY FIX: pox.update() runs EVERY loop (like working local code)
//  ★ socketIO.loop() runs only every 100ms (SSL is slow, was starving pox)
// ================================================================
void loop() {
    unsigned long now = millis();

    // ★ MAX30100: call pox.update() EVERY loop iteration (like your working code!)
    // This is the #1 priority — must run at maximum speed
    if (maxSensorFound) {
        pox.update();
        global_HR = pox.getHeartRate();
        global_SpO2 = pox.getSpO2();
    }

    // ★ Socket.IO: only process every 100ms (SSL is slow, was blocking pox.update)
    // 100ms is still fast enough for WebSocket heartbeats
    static unsigned long lastSocketLoop = 0;
    if (now - lastSocketLoop >= 100) {
        lastSocketLoop = now;
        socketIO.loop();
    }

    // --- ECG Sampling at 250Hz (every 4ms) ---
    static unsigned long lastSampleMicros = 0;
    if (micros() - lastSampleMicros >= 4000) {
        lastSampleMicros = micros();

        float ecgFiltered = 0;

        if (digitalRead(PIN_LO_PLUS) == LOW && digitalRead(PIN_LO_MINUS) == LOW) {
            int raw = analogRead(PIN_ECG_INPUT);
            ecgFiltered = bandpassFilter(raw);
        }

        lastFilteredECG = ecgFiltered;

        // QRS Beat Detection
        adaptiveThresh = 0.9 * adaptiveThresh + 0.1 * ecgFiltered;

        if (ecgFiltered > adaptiveThresh + 50 && now - lastBeatTime > 400) {
            if (lastBeatTime != 0) {
                long rr = now - lastBeatTime;

                if (rrCount < MAX_RR_HIST)
                    rrIntervals[rrCount++] = rr;
                else {
                    for (int i = 0; i < MAX_RR_HIST - 1; i++)
                        rrIntervals[i] = rrIntervals[i + 1];
                    rrIntervals[MAX_RR_HIST - 1] = rr;
                }

                ecgBPM = 60000.0 / rr;

                if (rrCount > 2) {
                    long sumSq = 0;
                    for (int i = 0; i < rrCount - 1; i++) {
                        long d = rrIntervals[i + 1] - rrIntervals[i];
                        sumSq += d * d;
                    }
                    ecgHRV = sqrt((float)sumSq / (rrCount - 1));
                }
            }
            lastBeatTime = now;
        }
    }

    // --- Send ECG to cloud at 25Hz ---
    if (now - lastECGSend >= ECG_SEND_INTERVAL) {
        lastECGSend = now;
        sendToCloud(lastFilteredECG);
    }

    // --- Debug print every 1 second ---
    if (now - lastVitalsPrint >= VITALS_PRINT_INTERVAL) {
        lastVitalsPrint = now;
        Serial.printf("📤 ECG_BPM: %.0f | SpO2: %.0f%% | HR: %.0f | HRV: %.1f | MAX: %s | Cloud: %s\n",
                      ecgBPM, global_SpO2, global_HR, ecgHRV,
                      maxSensorFound ? "✅" : "❌",
                      isConnected ? "✅" : "❌");
    }
}