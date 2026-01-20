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

// Store the latest health data to send to new connections immediately
let currentVitals = {
    bpm: 0,
    spo2: 0,
    temp: 0,
    status: "Waiting for device..."
};

io.on('connection', (socket) => {
    console.log('A user connected:', socket.id);

    // 1. If it's a browser (Doctor/Patient), send them the latest data immediately
    socket.emit('vitals_update', currentVitals);

    // 2. If it's the ESP32 (or Simulator) sending data
    socket.on('sensor_data', (data) => {
        // Update our storage
        currentVitals = {
            bpm: data.bpm,
            spo2: data.spo2,
            temp: data.temp,
            status: "Monitoring Active"
        };

        // Broadcast updates to all connected browsers
        // We send 'ecg_point' separately because it's high-speed
        io.emit('vitals_update', currentVitals);

        if (data.ecg) {
            io.emit('ecg_stream', data.ecg); // Stream the waveform point
        }
    });

    socket.on('disconnect', () => {
        console.log('User disconnected');
    });
});

// Use environment port for cloud deployment (Render, Railway, etc.)
const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
    console.log(`🚀 ClinicQ Health Monitor Server running on port ${PORT}`);
    console.log(`📊 Dashboard: http://localhost:${PORT}`);
}); 
