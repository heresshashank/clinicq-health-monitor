# ClinicQ Health Monitor - Cloud Deployment

This document contains instructions for deploying the ClinicQ Health Monitor to the cloud.

## 🚀 Render Deployment (Recommended - Free Tier)

### Step 1: Push to GitHub
1. Create a new repository on GitHub
2. Push your code:
```bash
git init
git add .
git commit -m "Initial commit - ClinicQ Health Monitor"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/clinicq-health-monitor.git
git push -u origin main
```

### Step 2: Deploy on Render
1. Go to [render.com](https://render.com) and sign up/login
2. Click **"New +"** → **"Web Service"**
3. Connect your GitHub repository
4. Configure the service:
   - **Name**: `clinicq-health-monitor`
   - **Region**: Choose closest to you
   - **Branch**: `main`
   - **Runtime**: `Node`
   - **Build Command**: `npm install`
   - **Start Command**: `npm start`
5. Click **"Create Web Service"**

### Step 3: Get Your Public URL
After deployment, you'll get a URL like:
```
https://clinicq-health-monitor.onrender.com
```

This is the URL you'll put in the ESP32 firmware!

---

## 📋 Files Structure for Deployment

```
📁 clinicq-health-monitor/
├── 📁 public/
│   └── index.html          # Dashboard (served automatically)
├── 📁 firmware/
│   └── esp32_health_monitor.ino  # Arduino code for ESP32
├── server.js               # Main Socket.IO server
├── package.json            # Dependencies
├── simulator.js            # Local testing
└── README.md               # This file
```

---

## 🧪 Testing Locally

1. Install dependencies:
```bash
npm install
```

2. Start the server:
```bash
npm start
```

3. Open dashboard: http://localhost:3000

4. Run simulator (in another terminal):
```bash
npm run simulate
```

---

## 🔧 ESP32 Setup

### Required Libraries (Arduino IDE)
1. **WebSocketsClient** by Markus Sattler
2. **ArduinoJson** by Benoit Blanchon
3. **SparkFun MAX3010x** Pulse and Proximity Sensor Library

### Wiring Diagram

```
┌─────────────────────────────────────────────────────────┐
│                        ESP32                            │
│                                                         │
│   3.3V ──────┬────────────────────────────┬─────────   │
│              │                            │             │
│              │                            │             │
│   ┌──────────┴──────────┐    ┌────────────┴──────────┐ │
│   │      AD8232         │    │       MAX30102        │ │
│   │   (ECG Module)      │    │   (Pulse Oximeter)    │ │
│   │                     │    │                       │ │
│   │  VCC ─── 3.3V       │    │  VIN ─── 3.3V         │ │
│   │  GND ─── GND        │    │  GND ─── GND          │ │
│   │  OUTPUT ─ GPIO 34   │    │  SDA ─── GPIO 21      │ │
│   │  LO+ ─── GPIO 32    │    │  SCL ─── GPIO 22      │ │
│   │  LO- ─── GPIO 33    │    │                       │ │
│   └─────────────────────┘    └───────────────────────┘ │
│                                                         │
│   GND ──────┴────────────────────────────┴─────────    │
└─────────────────────────────────────────────────────────┘
```

### After Deployment, Update Firmware:
```cpp
const char* SERVER_HOST = "clinicq-health-monitor.onrender.com";  // Your Render URL
const uint16_t SERVER_PORT = 443;
const bool USE_SSL = true;
```

---

## 📡 API Reference

### Socket.IO Events

**From ESP32 to Server:**
```json
Event: "sensor_data"
Data: {
    "bpm": 72,
    "spo2": 98,
    "temp": 36.6,
    "ecg": 45
}
```

**From Server to Dashboard:**
```json
Event: "vitals_update"
Data: {
    "bpm": 72,
    "spo2": 98,
    "temp": 36.6,
    "status": "Monitoring Active"
}

Event: "ecg_stream"
Data: 45  // Single ECG value
```

---

## 🔒 Security Notes (Production)

For production deployment, consider:
1. Add CORS restrictions (replace `origin: "*"`)
2. Add device authentication tokens
3. Use environment variables for sensitive data
4. Enable rate limiting
5. Add HTTPS certificate validation

---

## 📊 Dashboard Features

- ✅ Real-time ECG waveform (Chart.js)
- ✅ Heart Rate (BPM) display
- ✅ Blood Oxygen (SpO2) display
- ✅ Body Temperature display
- ✅ Connection status indicator
- ✅ Vital status warnings (High/Low/Normal)
- ✅ Dark mode medical-grade UI
- ✅ Responsive design

---

## 🆘 Troubleshooting

### ESP32 not connecting?
1. Check WiFi credentials
2. Verify server URL is correct
3. Check if Render service is running
4. Look at Serial Monitor for debug messages

### Dashboard not updating?
1. Check browser console for errors
2. Verify Socket.IO connection
3. Try refreshing the page
4. Check if ESP32 is sending data (Serial Monitor)

### ECG flatline?
1. Check electrode connections
2. Verify AD8232 wiring
3. Check LO+/LO- pins (leads off detection)
4. Ensure electrodes are properly placed on skin
