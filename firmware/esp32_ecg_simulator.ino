/*
 * CLINICQ IOT FIRMWARE - ECG SIMULATOR MODE
 * Hardware Required: ESP32 Only (No sensors needed)
 * Connect to: https://clinicq-health-monitor.onrender.com
 * 
 * Library Required: "WebSockets" by Markus Sattler (Install from Library Manager)
 * Board: ESP32 Dev Module
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>

// ===== WI-FI CONFIGURATION (EDIT THIS) =====
const char* ssid     = "YOUR_WIFI_NAME";       // ← Change this!
const char* password = "YOUR_WIFI_PASSWORD";    // ← Change this!

// ===== CLOUD SERVER =====
const char* serverHost = "clinicq-health-monitor.onrender.com";
const uint16_t serverPort = 443;

// ===== OBJECTS =====
SocketIOclient socketIO;

// ===== VARIABLES =====
unsigned long lastMsg = 0;
const unsigned long interval = 40; // 40ms = 25Hz for smooth ECG waveform
int ecgIndex = 0;
bool isConnected = false;

// ===== ECG WAVEFORM LOOKUP TABLE (One heartbeat PQRST cycle) =====
// Values represent ADC readings (0-4095 range)
const int ecgWave[] = {
  2000, 2000, 2000, 2000, 2000, 2010, 2020, 2040, 2060, 2040, 2020, 2000, // P-Wave (Atrial contraction)
  2000, 2000, 2000, 1950, 1900,                                             // Q-Wave (small dip)
  2200, 2600, 3200, 3800, 4095, 3800, 3200, 2600, 2200,                     // R-Peak (Main spike!)
  1800, 1600, 1800, 2000,                                                    // S-Wave (dip after spike)
  2000, 2000, 2000, 2000, 2020, 2050, 2080, 2100, 2080, 2050, 2020, 2000,  // T-Wave (repolarization)
  2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000                            // Resting baseline
};
const int waveLength = sizeof(ecgWave) / sizeof(ecgWave[0]);

// ===== MAP ECG TO DASHBOARD RANGE =====
int mapECGtoDashboard(int rawValue) {
    return map(rawValue, 1600, 4095, -60, 150);
}

// ===== SOCKET.IO EVENT HANDLER =====
void socketIOEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            Serial.println("❌ Disconnected from server");
            isConnected = false;
            break;

        case sIOtype_CONNECT:
            Serial.println("✅ CONNECTED TO CLINICQ CLOUD!");
            Serial.println("📊 Starting ECG Simulation...");
            Serial.println("🌐 Dashboard: https://clinicq-health-monitor.onrender.com");
            isConnected = true;
            // Join default namespace
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

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== CLINICQ ECG SIMULATOR ===");
    Serial.println("Using SocketIOclient (WebSockets by Markus Sattler)");

    // 1. Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("📶 Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ WiFi connected!");
    Serial.print("📍 IP: ");
    Serial.println(WiFi.localIP());

    // 2. Connect to Cloud Server via SSL (HTTPS/WSS)
    socketIO.beginSSL(serverHost, serverPort, "/socket.io/?EIO=4");
    socketIO.onEvent(socketIOEvent);

    Serial.println("🔌 Connecting to cloud server...");
}

// ===== MAIN LOOP =====
void loop() {
    // MUST call this every loop iteration
    socketIO.loop();

    // Only send data if connected
    if (!isConnected) return;

    unsigned long now = millis();
    if (now - lastMsg > interval) {
        lastMsg = now;

        // --- GENERATE ECG SIGNAL ---
        int rawSignal = ecgWave[ecgIndex];
        int noise = random(-20, 20);
        int noisyRaw = rawSignal + noise;
        int finalECG = mapECGtoDashboard(noisyRaw);

        ecgIndex++;
        if (ecgIndex >= waveLength) ecgIndex = 0;

        // --- SIMULATE VITALS ---
        int simulatedBPM = 72 + random(-2, 3);
        int simulatedSpO2 = 97 + random(0, 2);
        float simulatedTemp = 36.5 + (random(0, 3) * 0.1);

        // --- BUILD SOCKET.IO PACKET ---
        // Socket.IO event format: [event_name, {data}]
        String packet = "[\"sensor_data\",{";
        packet += "\"bpm\":"; packet += simulatedBPM;
        packet += ",\"spo2\":"; packet += simulatedSpO2;
        packet += ",\"ecg\":"; packet += finalECG;
        packet += ",\"temp\":"; packet += String(simulatedTemp, 1);
        packet += "}]";

        // Send as Socket.IO event
        socketIO.sendEVENT(packet);

        // Debug every 25 cycles (~1 second)
        static int debugCounter = 0;
        debugCounter++;
        if (debugCounter >= 25) {
            Serial.printf("📤 BPM: %d | SpO2: %d%% | ECG: %d (raw: %d)\n",
                          simulatedBPM, simulatedSpO2, finalECG, rawSignal);
            debugCounter = 0;
        }
    }
}
