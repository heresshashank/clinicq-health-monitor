/*
 * CLINICQ IOT FIRMWARE - ECG SIMULATOR MODE (FIXED)
 * Hardware Required: ESP32 Only (No sensors needed)
 * Connect to: https://clinicq-health-monitor.onrender.com
 * 
 * FIX: ECG values now mapped to -150 to 150 range for dashboard compatibility
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SocketIoClient.h> 

// --- WI-FI CONFIGURATION (EDIT THIS) ---
const char* ssid     = "ADI's_A55";
const char* password = "galaxy@123";

// --- CLOUD CONFIGURATION ---
char host[] = "clinicq-health-monitor.onrender.com"; 
int port = 443; 

// --- OBJECTS ---
SocketIoClient socket;

// --- VARIABLES ---
long lastMsg = 0;
const long interval = 40; // Send data every 40ms (25Hz speed for smooth drawing)
int ecgIndex = 0;

// --- THE MATHEMATICAL ECG WAVEFORM (One single heartbeat) ---
// This array represents the voltage levels of P-Q-R-S-T complex
// Original values: 0-4095 ADC range
const int ecgWave[] = {
  2000, 2000, 2000, 2000, 2000, 2010, 2020, 2040, 2060, 2040, 2020, 2000, // P-Wave (Atrial contraction)
  2000, 2000, 2000, 1950, 1900, // Q-Wave (Dip)
  2200, 2600, 3200, 3800, 4095, 3800, 3200, 2600, 2200, // R-Peak (Main heartbeat spike)
  1800, 1600, 1800, 2000, // S-Wave (Dip after spike)
  2000, 2000, 2000, 2000, 2020, 2050, 2080, 2100, 2080, 2050, 2020, 2000, // T-Wave (Ventricular Repolarization)
  2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000 // Resting baseline
};
const int waveLength = sizeof(ecgWave) / sizeof(ecgWave[0]);

// --- FUNCTION TO MAP ECG TO DASHBOARD RANGE ---
// Maps 0-4095 (ADC) to -150 to 150 (Dashboard range)
int mapECGtoDashboard(int rawValue) {
    // Using map() function: map(value, fromLow, fromHigh, toLow, toHigh)
    // Raw baseline is around 2000, peak is 4095, dip is 1600
    // We want: baseline = 0, peak = 150, dip = -150
    return map(rawValue, 1600, 4095, -150, 150);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== CLINICQ ECG SIMULATOR ===");
    
    // 1. Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // 2. Connect to Cloud Server (SSL)
    // IMPORTANT: The "/socket.io/?EIO=4" path is required for Render/Socket.io compatibility
    socket.beginSSL(host, port, "/socket.io/?EIO=4", "true"); 
    
    socket.on("connect", [](const char * payload, size_t length) {
        Serial.println("✅ CONNECTED TO CLINICQ CLOUD!");
        Serial.println("📊 Starting ECG Simulation...");
        Serial.println("🌐 Dashboard: https://clinicq-health-monitor.onrender.com");
    });
    
    socket.on("disconnect", [](const char * payload, size_t length) {
        Serial.println("❌ Disconnected from server");
    });
}

void loop() {
    socket.loop();

    long now = millis();
    if (now - lastMsg > interval) {
        lastMsg = now;

        // --- GENERATE SIGNAL ---
        // 1. Get raw value from our lookup table
        int rawSignal = ecgWave[ecgIndex];
        
        // 2. Add random "Noise" to make it look real (Biology isn't perfect)
        int noise = random(-20, 20);
        int noisyRaw = rawSignal + noise;

        // 3. *** FIX: Map to dashboard range (-150 to 150) ***
        int finalECG = mapECGtoDashboard(noisyRaw);

        // 4. Move to next point in the array
        ecgIndex++;
        if (ecgIndex >= waveLength) {
            ecgIndex = 0; // Loop back to start of heartbeat
        }

        // --- SIMULATE VITALS ---
        // We will drift the Heart Rate slightly to show the numbers updating
        int simulatedBPM = 72 + random(-2, 3); 
        int simulatedSpO2 = 97 + random(0, 2);
        float simulatedTemp = 36.5 + (random(0, 3) * 0.1);

        // --- PACKETIZE & SEND ---
        String json = "{";
        json += "\"bpm\":";
        json += simulatedBPM; 
        json += ", \"spo2\":";
        json += simulatedSpO2; 
        json += ", \"ecg\":";
        json += finalECG;  // Now in correct range!
        json += ", \"temp\":";
        json += simulatedTemp;
        json += "}";

        socket.emit("sensor_data", json.c_str());
        
        // Debug every 25 cycles (1 second)
        static int debugCounter = 0;
        debugCounter++;
        if (debugCounter >= 25) {
            Serial.printf("📤 BPM: %d | SpO2: %d%% | ECG: %d (raw: %d)\n", 
                          simulatedBPM, simulatedSpO2, finalECG, rawSignal);
            debugCounter = 0;
        }
    }
}
