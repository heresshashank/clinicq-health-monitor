/*
 * ClinicQ Health Monitor - ESP32 Firmware
 * ========================================
 * 
 * This code reads sensor data from:
 * - AD8232 ECG Module (ECG waveform)
 * - MAX30102 Pulse Oximeter (Heart Rate & SpO2)
 * 
 * And sends it via WebSocket to the ClinicQ cloud server.
 * 
 * WIRING:
 * -------
 * AD8232 ECG:
 *   - VCC → 3.3V
 *   - GND → GND
 *   - OUTPUT → GPIO 34 (Analog)
 *   - LO+ → GPIO 32
 *   - LO- → GPIO 33
 * 
 * MAX30102:
 *   - VIN → 3.3V
 *   - GND → GND
 *   - SDA → GPIO 21
 *   - SCL → GPIO 22
 * 
 * LIBRARIES REQUIRED:
 * -------------------
 * 1. WiFi.h (built-in)
 * 2. WebSocketsClient by Markus Sattler
 * 3. ArduinoJson by Benoit Blanchon
 * 4. MAX30105 (SparkFun MAX3010x library)
 */

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// ============== CONFIGURATION ==============
// TODO: Change these values before uploading!

// WiFi Credentials
const char* WIFI_SSID = "YOUR_WIFI_NAME";        // ← Change this!
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; // ← Change this!

// Server Configuration - UPDATE THIS AFTER DEPLOYMENT!
const char* SERVER_HOST = "clinicq-health-monitor.onrender.com";  // ← Update after Render deployment
const uint16_t SERVER_PORT = 443;  // HTTPS port
const bool USE_SSL = true;         // Set to true for HTTPS (Render uses HTTPS)

// ============== PIN DEFINITIONS ==============
#define ECG_PIN 34       // AD8232 OUTPUT pin
#define ECG_LO_PLUS 32   // AD8232 LO+ pin (leads off detection)
#define ECG_LO_MINUS 33  // AD8232 LO- pin (leads off detection)

// ============== OBJECTS ==============
SocketIOclient socketIO;
MAX30105 particleSensor;

// ============== VARIABLES ==============
// Heart Rate calculation
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;

// SpO2 calculation
long irValue = 0;
long redValue = 0;

// Timing
unsigned long lastVitalsSend = 0;
unsigned long lastECGSend = 0;
const unsigned long VITALS_INTERVAL = 1000;  // Send vitals every 1 second
const unsigned long ECG_INTERVAL = 50;        // Send ECG at 20Hz (50ms)

// Connection status
bool isConnected = false;

// ============== SETUP ==============
void setup() {
    Serial.begin(115200);
    Serial.println("\n🏥 ClinicQ Health Monitor Starting...");
    
    // Initialize ECG pins
    pinMode(ECG_LO_PLUS, INPUT);
    pinMode(ECG_LO_MINUS, INPUT);
    
    // Connect to WiFi
    connectToWiFi();
    
    // Initialize MAX30102 sensor
    initializeMAX30102();
    
    // Connect to Socket.IO server
    connectToServer();
    
    Serial.println("✅ Setup complete! Starting monitoring...");
}

// ============== MAIN LOOP ==============
void loop() {
    // Handle Socket.IO events
    socketIO.loop();
    
    // Read and process MAX30102 (Heart Rate & SpO2)
    readMAX30102();
    
    // Send data at regular intervals
    unsigned long currentTime = millis();
    
    // Send vitals (BPM, SpO2) every second
    if (currentTime - lastVitalsSend >= VITALS_INTERVAL) {
        sendVitals();
        lastVitalsSend = currentTime;
    }
    
    // Send ECG at 20Hz for smooth waveform
    if (currentTime - lastECGSend >= ECG_INTERVAL) {
        sendECG();
        lastECGSend = currentTime;
    }
}

// ============== WIFI CONNECTION ==============
void connectToWiFi() {
    Serial.print("📶 Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi Connected!");
        Serial.print("📍 IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n❌ WiFi Connection Failed!");
        Serial.println("Restarting in 5 seconds...");
        delay(5000);
        ESP.restart();
    }
}

// ============== MAX30102 INITIALIZATION ==============
void initializeMAX30102() {
    Serial.println("🔧 Initializing MAX30102 sensor...");
    
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println("❌ MAX30102 was not found. Check wiring!");
        // Continue anyway - ECG will still work
    } else {
        // Configure sensor
        particleSensor.setup();
        particleSensor.setPulseAmplitudeRed(0x0A);  // Red LED
        particleSensor.setPulseAmplitudeGreen(0);   // Turn off Green LED
        Serial.println("✅ MAX30102 initialized!");
    }
}

// ============== SERVER CONNECTION ==============
void connectToServer() {
    Serial.print("🔌 Connecting to server: ");
    Serial.println(SERVER_HOST);
    
    if (USE_SSL) {
        socketIO.beginSSL(SERVER_HOST, SERVER_PORT, "/socket.io/?EIO=4");
    } else {
        socketIO.begin(SERVER_HOST, SERVER_PORT, "/socket.io/?EIO=4");
    }
    
    socketIO.onEvent(socketIOEvent);
}

// ============== SOCKET.IO EVENT HANDLER ==============
void socketIOEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            Serial.println("❌ Disconnected from server");
            isConnected = false;
            break;
            
        case sIOtype_CONNECT:
            Serial.println("✅ Connected to server!");
            isConnected = true;
            // Join the socket.io namespace
            socketIO.send(sIOtype_CONNECT, "/");
            break;
            
        case sIOtype_EVENT:
            Serial.printf("📨 Event: %s\n", payload);
            break;
            
        case sIOtype_ACK:
        case sIOtype_ERROR:
        case sIOtype_BINARY_EVENT:
        case sIOtype_BINARY_ACK:
            break;
    }
}

// ============== READ MAX30102 SENSOR ==============
void readMAX30102() {
    irValue = particleSensor.getIR();
    redValue = particleSensor.getRed();
    
    // Check if finger is on sensor
    if (irValue > 50000) {
        if (checkForBeat(irValue)) {
            long delta = millis() - lastBeat;
            lastBeat = millis();
            
            beatsPerMinute = 60 / (delta / 1000.0);
            
            if (beatsPerMinute < 255 && beatsPerMinute > 20) {
                rates[rateSpot++] = (byte)beatsPerMinute;
                rateSpot %= RATE_SIZE;
                
                // Calculate average
                beatAvg = 0;
                for (byte x = 0; x < RATE_SIZE; x++) {
                    beatAvg += rates[x];
                }
                beatAvg /= RATE_SIZE;
            }
        }
    }
}

// ============== CALCULATE SpO2 ==============
int calculateSpO2() {
    // Simple SpO2 estimation
    // In real implementation, use the library's spo2 calculation
    if (irValue < 50000) return 0;  // No finger detected
    
    // Basic calculation (for demonstration)
    // Real SpO2 needs proper calibration
    float ratio = (float)redValue / (float)irValue;
    int spo2 = 110 - 25 * ratio;  // Simplified formula
    
    // Clamp to realistic range
    if (spo2 > 100) spo2 = 100;
    if (spo2 < 0) spo2 = 0;
    
    return spo2;
}

// ============== SEND VITALS DATA ==============
void sendVitals() {
    if (!isConnected) return;
    
    // Create JSON document
    StaticJsonDocument<256> doc;
    JsonArray array = doc.to<JsonArray>();
    
    // First element is the event name
    array.add("sensor_data");
    
    // Second element is the data object
    JsonObject data = array.createNestedObject();
    data["bpm"] = beatAvg;
    data["spo2"] = calculateSpO2();
    data["temp"] = 36.6;  // Placeholder - add temperature sensor if needed
    
    // Read ECG value
    int ecgValue = 0;
    if (digitalRead(ECG_LO_PLUS) == 0 && digitalRead(ECG_LO_MINUS) == 0) {
        ecgValue = analogRead(ECG_PIN);
        // Map to -150 to 150 range for display
        ecgValue = map(ecgValue, 0, 4095, -150, 150);
    }
    data["ecg"] = ecgValue;
    
    // Serialize and send
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
    
    // Debug output
    Serial.printf("📤 Sent: BPM=%d, SpO2=%d, ECG=%d\n", 
                  beatAvg, calculateSpO2(), ecgValue);
}

// ============== SEND ECG DATA (High Frequency) ==============
void sendECG() {
    if (!isConnected) return;
    
    // Check if leads are connected
    if (digitalRead(ECG_LO_PLUS) == 1 || digitalRead(ECG_LO_MINUS) == 1) {
        return;  // Leads are off, don't send garbage
    }
    
    int ecgValue = analogRead(ECG_PIN);
    // Map to -150 to 150 range for display
    ecgValue = map(ecgValue, 0, 4095, -150, 150);
    
    // Create JSON for ECG stream
    StaticJsonDocument<128> doc;
    JsonArray array = doc.to<JsonArray>();
    array.add("sensor_data");
    
    JsonObject data = array.createNestedObject();
    data["bpm"] = beatAvg;
    data["spo2"] = calculateSpO2();
    data["temp"] = 36.6;
    data["ecg"] = ecgValue;
    
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
}
