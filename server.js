const express = require('express');
const http = require('http');
const { Server } = require("socket.io");
const cors = require('cors');
const path = require('path');
const mongoose = require('mongoose');

const app = express();
app.use(cors());

// --- 1. SERVE FILES (Fixes "Cannot GET" error) ---
// This forces the server to look in the main folder for dashboard.html
app.use(express.static(__dirname));

app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'dashboard.html'));
});

// --- 2. DATABASE CONNECTION ---
// 👇 PASTE YOUR CONNECTION STRING BELOW 👇
// REPLACE <db_password> with your actual password (remove the < > symbols too!)
const mongoURI = "mongodb+srv://admin:Password@123@cluster0.rfvgwaq.mongodb.net/?appName=Cluster0";

mongoose.connect(mongoURI)
    .then(() => console.log("✅ Connected to MongoDB Atlas"))
    .catch(err => console.log("❌ MongoDB Connection Error:", err));

// Define the shape of the data we will store
const VitalsSchema = new mongoose.Schema({
    patientId: String,
    bpm: Number,
    spo2: Number,
    ecg: Number, // Storing raw ECG point
    timestamp: { type: Date, default: Date.now }
});
const Vitals = mongoose.model('Vitals', VitalsSchema);

// --- 3. REAL-TIME SOCKET SERVER ---
const server = http.createServer(app);
const io = new Server(server, { cors: { origin: "*" } });

io.on('connection', (socket) => {
    console.log('User connected:', socket.id);

    socket.on('sensor_data', (data) => {
        try {
            // A. Convert incoming data to JSON
            const parsedData = typeof data === 'string' ? JSON.parse(data) : data;

            // B. Broadcast to Dashboard (Live Graph)
            io.emit('sensor_data', parsedData);
            io.emit('vitals_update', parsedData);

            // C. Save to Database (The "Black Box" Recording)
            // We only save if there is a valid Heart Rate
            if (parsedData.bpm > 0) {
                const newRecord = new Vitals({
                    patientId: "Patient_001", // Hardcoded for now
                    bpm: parsedData.bpm,
                    spo2: parsedData.spo2,
                    ecg: parsedData.ecg
                });
                newRecord.save(); // Save silently in background
            }
        } catch (e) {
            console.error("Data processing error:", e);
        }
    });
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});