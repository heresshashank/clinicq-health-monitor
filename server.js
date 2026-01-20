const express = require('express');
const http = require('http');
const { Server } = require("socket.io");
const cors = require('cors');

const app = express();
app.use(cors());

// Serve your static files (HTML/CSS) from a 'public' folder
app.use(express.static('public'));

const server = http.createServer(app);
const io = new Server(server, {
    cors: {
        origin: "*", // Allow connections from anywhere for now
        methods: ["GET", "POST"]
    }
});

// ============ DATA STORAGE ============
// Store vitals with NULL values initially (no fake data!)
let currentVitals = {
    bpm: null,      // NULL = no data received yet
    spo2: null,
    temp: null,
    ecg: null,
    status: "waiting",  // "waiting" | "active" | "stale"
    lastUpdate: null,   // Timestamp of last data received
    deviceId: null      // Which device sent the data
};

// Timeout for considering data "stale" (no updates for 5 seconds)
const STALE_TIMEOUT = 5000;

// ============ HELPER FUNCTIONS ============
function checkDataFreshness() {
    if (currentVitals.lastUpdate) {
        const timeSinceUpdate = Date.now() - currentVitals.lastUpdate;
        if (timeSinceUpdate > STALE_TIMEOUT && currentVitals.status === "active") {
            currentVitals.status = "stale";
            io.emit('vitals_update', getClientData());
            console.log('⚠️ Data stale - no updates for 5 seconds');
        }
    }
}

// Only send actual values, not nulls disguised as zeros
function getClientData() {
    return {
        bpm: currentVitals.bpm,
        spo2: currentVitals.spo2,
        temp: currentVitals.temp,
        status: currentVitals.status,
        lastUpdate: currentVitals.lastUpdate,
        hasData: currentVitals.lastUpdate !== null  // Flag to know if we ever received data
    };
}

// Check freshness every 2 seconds
setInterval(checkDataFreshness, 2000);

// ============ SOCKET CONNECTIONS ============
io.on('connection', (socket) => {
    console.log('👤 Browser/Device connected:', socket.id);

    // 1. Send current state to new connections
    //    If no data received yet, client will see null values
    socket.emit('vitals_update', getClientData());

    // 2. Handle incoming sensor data from ESP32
    socket.on('sensor_data', (data) => {
        // Validate incoming data
        if (typeof data !== 'object') {
            console.log('⚠️ Invalid data format received');
            return;
        }

        // Update storage with REAL values
        currentVitals = {
            bpm: data.bpm !== undefined ? data.bpm : currentVitals.bpm,
            spo2: data.spo2 !== undefined ? data.spo2 : currentVitals.spo2,
            temp: data.temp !== undefined ? data.temp : currentVitals.temp,
            ecg: data.ecg !== undefined ? data.ecg : currentVitals.ecg,
            status: "active",
            lastUpdate: Date.now(),
            deviceId: socket.id
        };

        // Broadcast to all connected browsers
        io.emit('vitals_update', getClientData());

        // Stream ECG separately for smooth waveform
        if (data.ecg !== undefined) {
            io.emit('ecg_stream', data.ecg);
        }
    });

    // 3. Handle device identification (optional)
    socket.on('device_info', (info) => {
        console.log(`📱 Device identified: ${info.name || socket.id}`);
    });

    // 4. Handle disconnection
    socket.on('disconnect', () => {
        console.log('👋 Disconnected:', socket.id);

        // If the disconnected socket was our data source, mark as stale
        if (socket.id === currentVitals.deviceId) {
            currentVitals.status = "stale";
            io.emit('vitals_update', getClientData());
            console.log('⚠️ Data source disconnected - status: stale');
        }
    });
});

// ============ API ENDPOINT (optional - for debugging) ============
app.get('/api/status', (req, res) => {
    res.json({
        status: currentVitals.status,
        hasData: currentVitals.lastUpdate !== null,
        lastUpdate: currentVitals.lastUpdate,
        connectedClients: io.engine.clientsCount
    });
});

// ============ START SERVER ============
const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
    console.log(`🚀 ClinicQ Health Monitor Server running on port ${PORT}`);
    console.log(`📊 Dashboard: http://localhost:${PORT}`);
    console.log(`📡 Waiting for ESP32 to connect...`);
});
