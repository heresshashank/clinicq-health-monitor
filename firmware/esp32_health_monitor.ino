/*
 * ClinicQ Health Monitor - ESP32 Full Firmware
 * =============================================
 * 
 * Sensors:
 *   - MAX30100 (SpO2 + HR)           → OLED + cloud
 *   - AD8232  (ECG waveform)         → cloud only
 *   - GSR     (Galvanic Skin Resp)   → cloud only
 *   - DF Gravity Pulse (Heart Rate)  → cloud (fallback HR)
 *   - OLED    (128x64 SSD1306)       → local display
 * 
 * WIRING (ADC1 pins only — ADC2 blocked by WiFi):
 *   OLED:       SDA→18, SCL→19 (I2C Bus 1)
 *   MAX30100:   SDA→21, SCL→22 (I2C Bus 0)
 *   AD8232:     OUT→34, LO+→32, LO-→33
 *   GSR:        SIG→35
 *   DF Pulse:   SIG→36
 * 
 * ARCHITECTURE:
 *   Core 0: MAX30100 pox.update() tight loop
 *   Core 1: WiFi, SocketIO, ECG, GSR, Pulse, OLED
 * 
 * Board: ESP32 Dev Module
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ================================================================
//  WIFI CREDENTIALS
// ================================================================
const char* WIFI_SSID     = "ADI's_A55";
const char* WIFI_PASSWORD = "galaxy@123";

// ===== CLOUD SERVER =====
const char* SERVER_HOST = "clinicq-health-monitor.onrender.com";
const uint16_t SERVER_PORT = 443;

// ===== OLED CONFIG =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Pointers for heavy objects (created after WiFi)
TwoWire          *I2C_OLED = NULL;
Adafruit_SSD1306 *oled     = NULL;
SocketIOclient   *socketIO = NULL;
PulseOximeter pox;
bool oledFound = false;

// ===== AD8232 ECG PINS =====
const int PIN_ECG_INPUT = 34;
const int PIN_LO_PLUS   = 32;
const int PIN_LO_MINUS  = 33;

// ===== GSR SENSOR =====
const int PIN_GSR = 35;
const int GSR_FILTER_WINDOW = 10;
int gsrBuffer[GSR_FILTER_WINDOW];
int gsrBufIndex = 0;
long gsrSum = 0;
int filteredGSR = 0;  // Moving-average filtered GSR value (0-4095)

// ===== DF GRAVITY PULSE SENSOR =====
const int PIN_PULSE = 36;
// Peak detection for BPM
int pulseSignal = 0;
int pulsePrev = 0;
bool pulseRising = false;
unsigned long lastPulseBeatTime = 0;
float pulseBPM = 0;
int pulseThreshold = 2048;  // Dynamic threshold (mid-ADC)
int pulseMax = 0;
int pulseMin = 4095;
unsigned long lastPulseCalc = 0;

// ===== SENSOR DETECTION FLAGS =====
bool maxSensorFound = false;
bool ecgLeadsOn     = false;

// ===== MAX30100 STATE (volatile — shared between cores) =====
volatile float currentBPM  = 0;
volatile float currentSpO2 = 0;
volatile bool  fingerOn    = false;
volatile unsigned long lastFingerTime = 0;
const unsigned long FINGER_OFF_TIMEOUT = 3000;
const float ALPHA = 0.1;

// ===== ECG VARIABLES =====
float hp_y = 0, hp_x_prev = 0;
float lp_y = 0;
float adaptiveThresh = 0;
float lastFilteredECG = 0;
unsigned long lastECGBeatTime = 0;
float ecgBPM = 0;

// ===== TIMING =====
unsigned long lastECGSend      = 0;
unsigned long lastOLEDUpdate   = 0;
unsigned long lastVitalsPrint  = 0;
unsigned long lastReconnect    = 0;
unsigned long lastGSRSample    = 0;
unsigned long lastPulseSample  = 0;
const unsigned long ECG_SEND_INTERVAL     = 100;   // 10 Hz (was 40ms/25Hz — too aggressive for SSL)
const unsigned long OLED_UPDATE_INTERVAL  = 1000;
const unsigned long VITALS_PRINT_INTERVAL = 1000;
const unsigned long RECONNECT_INTERVAL    = 15000;
const unsigned long GSR_SAMPLE_INTERVAL   = 20;    // ~50 Hz
const unsigned long PULSE_SAMPLE_INTERVAL = 5;     // ~200 Hz

bool isConnected = false;
TaskHandle_t poxTaskHandle = NULL;

void onBeatDetected() {
    Serial.println("Beat!");
}


// ================================================================
//  ECG BANDPASS FILTER
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
            Serial.println("Cloud disconnected");
            isConnected = false;
            break;
        case sIOtype_CONNECT:
            Serial.println("Cloud connected!");
            isConnected = true;
            socketIO->send(sIOtype_CONNECT, "/");
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
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    Serial.flush();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi Failed! Restarting...");
        delay(5000);
        ESP.restart();
    }
}


// ================================================================
//  OLED INIT
// ================================================================
void initOLED() {
    Serial.println("Init OLED...");
    I2C_OLED = new TwoWire(1);
    I2C_OLED->begin(18, 19, 100000);
    oled = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, I2C_OLED, OLED_RESET);

    if (!oled->begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("OLED not found!");
        oledFound = false;
        delete oled; oled = NULL;
        delete I2C_OLED; I2C_OLED = NULL;
        return;
    }
    I2C_OLED->setClock(100000);
    oledFound = true;
    oled->clearDisplay();
    oled->setTextColor(WHITE);
    oled->setTextSize(1);
    oled->setCursor(10, 25);
    oled->println("ClinicQ Starting...");
    oled->display();
    Serial.println("OLED ready!");
}


// ================================================================
//  MAX30100 INIT
// ================================================================
void initMAX30100() {
    Serial.println("Init MAX30100...");
    Wire.begin(21, 22);
    delay(100);

    maxSensorFound = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.printf("  Attempt %d/3... ", attempt);
        if (pox.begin()) {
            maxSensorFound = true;
            Serial.println("OK!");
            break;
        }
        Serial.println("failed");
        delay(500);
    }

    if (maxSensorFound) {
        pox.setIRLedCurrent(MAX30100_LED_CURR_27_1MA);
        pox.setOnBeatDetectedCallback(onBeatDetected);
        Serial.println("MAX30100 ready!");
    } else {
        Serial.println("MAX30100 not found.");
    }
}


// ================================================================
//  MAX30100 TASK (Core 0) — unchanged
// ================================================================
void poxTask(void *parameter) {
    float smoothBPM  = 0;
    float smoothSpO2 = 0;

    while (true) {
        if (maxSensorFound) {
            pox.update();

            float hr = pox.getHeartRate();
            float sp = pox.getSpO2();

            if (hr > 30.0 && sp > 50.0 && sp <= 100.0) {
                fingerOn = true;
                lastFingerTime = millis();
                if (sp > 100.0) sp = 100.0;

                // Outlier rejection
                if (smoothBPM > 0) {
                    float bpmChange = abs(hr - smoothBPM) / smoothBPM;
                    float spo2Change = abs(sp - smoothSpO2) / smoothSpO2;
                    if (bpmChange > 0.25) hr = smoothBPM;
                    if (spo2Change > 0.15) sp = smoothSpO2;
                }

                // Smoothing
                if (smoothBPM == 0) {
                    smoothBPM  = hr;
                    smoothSpO2 = sp;
                } else {
                    smoothBPM  = (smoothBPM  * (1.0 - ALPHA)) + (hr  * ALPHA);
                    smoothSpO2 = (smoothSpO2 * (1.0 - ALPHA)) + (sp * ALPHA);
                }

                if (smoothBPM > 200.0) smoothBPM = 200.0;
                if (smoothBPM < 30.0)  smoothBPM = 30.0;
                if (smoothSpO2 > 100.0) smoothSpO2 = 100.0;
                if (smoothSpO2 < 50.0) smoothSpO2 = 50.0;

                currentBPM  = smoothBPM;
                currentSpO2 = smoothSpO2;
            } else {
                fingerOn = false;
                if (millis() - lastFingerTime > FINGER_OFF_TIMEOUT) {
                    smoothBPM  = 0;
                    smoothSpO2 = 0;
                    currentBPM  = 0;
                    currentSpO2 = 0;
                }
            }
        }
        vTaskDelay(1);
    }
}


// ================================================================
//  GSR READING (Moving Average Filter)
// ================================================================
void readGSR() {
    int rawGsr = analogRead(PIN_GSR);

    // Moving average filter (window = 10)
    gsrSum -= gsrBuffer[gsrBufIndex];
    gsrBuffer[gsrBufIndex] = rawGsr;
    gsrSum += rawGsr;
    gsrBufIndex = (gsrBufIndex + 1) % GSR_FILTER_WINDOW;

    filteredGSR = gsrSum / GSR_FILTER_WINDOW;
}


// ================================================================
//  DF GRAVITY PULSE READING + Peak Detection
//  Detects heartbeats from the analog pulse waveform and
//  calculates BPM from inter-beat intervals.
// ================================================================
void readPulse() {
    pulseSignal = analogRead(PIN_PULSE);

    // Track min/max for dynamic threshold (reset every 3s)
    if (pulseSignal > pulseMax) pulseMax = pulseSignal;
    if (pulseSignal < pulseMin) pulseMin = pulseSignal;

    unsigned long now = millis();

    // Reset min/max every 3 seconds for adaptive threshold
    if (now - lastPulseCalc > 3000) {
        lastPulseCalc = now;
        pulseThreshold = (pulseMax + pulseMin) / 2;
        pulseMax = 0;
        pulseMin = 4095;
    }

    // Detect rising edge crossing threshold → beat
    if (pulseSignal > pulseThreshold && pulsePrev <= pulseThreshold) {
        // Rising edge detected
        if (lastPulseBeatTime > 0 && now - lastPulseBeatTime > 300) {
            // At least 300ms between beats (max 200 BPM)
            unsigned long ibi = now - lastPulseBeatTime;
            float newBPM = 60000.0 / ibi;

            // Sanity check: 30-200 BPM
            if (newBPM >= 30 && newBPM <= 200) {
                // Smooth the BPM
                if (pulseBPM == 0) {
                    pulseBPM = newBPM;
                } else {
                    pulseBPM = pulseBPM * 0.8 + newBPM * 0.2;
                }
            }
        }
        lastPulseBeatTime = now;
    }

    pulsePrev = pulseSignal;

    // If no beat detected in 5 seconds, reset BPM
    if (now - lastPulseBeatTime > 5000) {
        pulseBPM = 0;
    }
}


// ================================================================
//  SEND TO CLOUD
//  Sends real data only. 0 = no sensor / not reading → "--" on site
// ================================================================
void sendToCloud(float ecgValue) {
    if (!isConnected || !socketIO) return;

    // ECG
    int mappedECG = 0;
    if (ecgLeadsOn) {
        mappedECG = map(constrain((int)ecgValue, -500, 1500), -500, 1500, -60, 150);
    }

    // HR: MAX30100 primary → DF Gravity Pulse fallback → 0
    int sendBPM = 0;
    if (fingerOn && currentBPM > 30) {
        sendBPM = (int)currentBPM;
    } else if (pulseBPM > 30) {
        sendBPM = (int)pulseBPM;
    }

    // SpO2: MAX30100 only
    int sendSpO2 = 0;
    if (fingerOn && currentSpO2 > 50) {
        sendSpO2 = (int)currentSpO2;
        if (sendSpO2 > 100) sendSpO2 = 100;
    }

    // GSR: always send current filtered value
    int sendGSR = filteredGSR;

    String packet = "[\"sensor_data\",{";
    packet += "\"bpm\":";   packet += sendBPM;
    packet += ",\"spo2\":"; packet += sendSpO2;
    packet += ",\"ecg\":";  packet += mappedECG;
    packet += ",\"gsr\":";  packet += sendGSR;
    packet += "}]";

    socketIO->sendEVENT(packet);
}


// ================================================================
//  UPDATE OLED (BPM + SpO2 only, as requested)
// ================================================================
void updateOLED() {
    if (!oledFound || !oled) return;

    oled->clearDisplay();
    oled->setTextSize(1);
    oled->setCursor(0, 0);
    oled->println("ClinicQ Monitor");
    oled->drawLine(0, 10, 128, 10, WHITE);

    // Determine best BPM source for display
    float displayBPM = 0;
    bool hasBPM = false;
    if (fingerOn && currentBPM > 30) {
        displayBPM = currentBPM;
        hasBPM = true;
    } else if (pulseBPM > 30) {
        displayBPM = pulseBPM;
        hasBPM = true;
    }

    // Heart Rate
    oled->setTextSize(2);
    oled->setCursor(0, 18);
    oled->print("HR: ");
    if (hasBPM) {
        oled->println((int)displayBPM);
    } else {
        oled->println("--");
    }

    // SpO2
    oled->setCursor(0, 42);
    oled->print("SpO2:");
    if (fingerOn && currentSpO2 > 50) {
        oled->print((int)currentSpO2);
        oled->println("%");
    } else {
        oled->println("--");
    }

    // Status
    oled->setTextSize(1);
    if (isConnected) {
        oled->setCursor(90, 56);
        oled->print("CLOUD");
    } else if (WiFi.status() == WL_CONNECTED) {
        oled->setCursor(96, 56);
        oled->print("WiFi");
    } else {
        oled->setCursor(78, 56);
        oled->print("OFFLINE");
    }
    oled->display();
}


// ================================================================
//  CONNECTION CHECK
// ================================================================
void checkConnection(unsigned long now) {
    if (now - lastReconnect < RECONNECT_INTERVAL) return;
    lastReconnect = now;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost, reconnecting...");
        WiFi.disconnect();
        delay(100);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi reconnected!");
        }
    }

    if (WiFi.status() == WL_CONNECTED && !isConnected && socketIO) {
        Serial.println("Cloud reconnecting...");
        socketIO->disconnect();
        delay(200);
        socketIO->beginSSL(SERVER_HOST, SERVER_PORT, "/socket.io/?EIO=4");
    }
}


// ================================================================
//  SETUP
// ================================================================
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    delay(3000);
    Serial.println("\n=== ClinicQ Health Monitor ===");

    // AD8232 + GSR + Pulse pin setup
    pinMode(PIN_ECG_INPUT, INPUT);
    pinMode(PIN_LO_PLUS, INPUT);
    pinMode(PIN_LO_MINUS, INPUT);
    pinMode(PIN_GSR, INPUT);
    pinMode(PIN_PULSE, INPUT);
    analogReadResolution(12);

    // Init GSR filter buffer
    for (int i = 0; i < GSR_FILTER_WINDOW; i++) gsrBuffer[i] = 0;

    // 1. WiFi
    connectToWiFi();
    delay(500);

    // 2. OLED
    initOLED();
    delay(200);

    if (oledFound && oled) {
        oled->clearDisplay();
        oled->setTextSize(1);
        oled->setCursor(0, 10);
        oled->println("WiFi OK!");
        oled->setCursor(0, 25);
        oled->print("IP:");
        oled->println(WiFi.localIP());
        oled->setCursor(0, 45);
        oled->println("Starting sensors...");
        oled->display();
    }

    // 3. MAX30100
    delay(200);
    initMAX30100();

    // 4. Launch MAX30100 on Core 0
    if (maxSensorFound) {
        xTaskCreatePinnedToCore(
            poxTask, "PoxTask", 4096, NULL, 1, &poxTaskHandle, 0
        );
        Serial.println("MAX30100 task on Core 0");
    }

    // 5. Cloud
    delay(300);
    Serial.print("Connecting to cloud: ");
    Serial.println(SERVER_HOST);
    socketIO = new SocketIOclient();
    socketIO->beginSSL(SERVER_HOST, SERVER_PORT, "/socket.io/?EIO=4");
    socketIO->onEvent(socketIOEvent);
    socketIO->setReconnectInterval(5000);

    // Print sensor status
    Serial.println("\n=== Sensor Status ===");
    Serial.printf("MAX30100 (HR/SpO2): %s\n", maxSensorFound ? "CONNECTED" : "NOT FOUND");
    Serial.println("AD8232 (ECG): GPIO 34 (checking leads...)");
    Serial.println("GSR Sensor:   GPIO 35");
    Serial.println("DF Pulse:     GPIO 36");
    Serial.println("=====================\n");
}


// ================================================================
//  MAIN LOOP (Core 1)
// ================================================================
void loop() {
    unsigned long now = millis();

    // 1. Socket.IO — must run FREQUENTLY for ping/pong keepalive
    static unsigned long lastSocketLoop = 0;
    if (now - lastSocketLoop >= 50) {   // Every 50ms (was 200ms — too slow, missed pings)
        lastSocketLoop = now;
        if (socketIO) socketIO->loop();
    }

    // 2. AD8232 ECG at 250 Hz
    static unsigned long lastSampleMicros = 0;
    if (micros() - lastSampleMicros >= 4000) {
        lastSampleMicros = micros();
        float ecgFiltered = 0;

        bool lo_plus  = digitalRead(PIN_LO_PLUS);
        bool lo_minus = digitalRead(PIN_LO_MINUS);
        ecgLeadsOn = (lo_plus == LOW && lo_minus == LOW);

        if (ecgLeadsOn) {
            int raw = analogRead(PIN_ECG_INPUT);
            ecgFiltered = bandpassFilter(raw);
        }
        lastFilteredECG = ecgFiltered;

        adaptiveThresh = 0.9 * adaptiveThresh + 0.1 * ecgFiltered;
        if (ecgFiltered > adaptiveThresh + 50 && now - lastECGBeatTime > 400) {
            if (lastECGBeatTime != 0) {
                ecgBPM = 60000.0 / (now - lastECGBeatTime);
            }
            lastECGBeatTime = now;
        }
    }

    // 3. GSR at ~50 Hz
    if (now - lastGSRSample >= GSR_SAMPLE_INTERVAL) {
        lastGSRSample = now;
        readGSR();
    }

    // 4. DF Gravity Pulse at ~200 Hz
    if (now - lastPulseSample >= PULSE_SAMPLE_INTERVAL) {
        lastPulseSample = now;
        readPulse();
    }

    // 5. Send to cloud at 25 Hz
    if (now - lastECGSend >= ECG_SEND_INTERVAL) {
        lastECGSend = now;
        sendToCloud(lastFilteredECG);
    }

    // 6. Update OLED at 1 Hz
    if (now - lastOLEDUpdate >= OLED_UPDATE_INTERVAL) {
        lastOLEDUpdate = now;
        updateOLED();
    }

    // 7. Connection check every 15s
    checkConnection(now);

    // 8. Serial debug at 1 Hz
    if (now - lastVitalsPrint >= VITALS_PRINT_INTERVAL) {
        lastVitalsPrint = now;
        Serial.printf("HR:%d | SpO2:%d%% | Finger:%s | Pulse_BPM:%d | GSR:%d | ECG:%s | Cloud:%s | RSSI:%d | Heap:%d\n",
                      (int)currentBPM, (int)currentSpO2,
                      fingerOn ? "YES" : "NO",
                      (int)pulseBPM,
                      filteredGSR,
                      ecgLeadsOn ? "ON" : "OFF",
                      isConnected ? "OK" : "NO",
                      WiFi.RSSI(),
                      ESP.getFreeHeap());
    }
}