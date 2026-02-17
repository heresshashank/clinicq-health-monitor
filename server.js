const express = require('express');
const http = require('http');
const { Server } = require("socket.io");
const cors = require('cors');
const path = require('path');
const mongoose = require('mongoose');

const app = express();
app.use(cors());
app.use(express.json());

// --- 1. SERVE FILES ---
app.use(express.static(__dirname));

app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

app.get('/history', (req, res) => {
    res.sendFile(path.join(__dirname, 'history.html'));
});

// --- 2. DATABASE CONNECTION ---
const mongoURI = "mongodb+srv://admin:clinicq123@cluster0.rfvgwaq.mongodb.net/?appName=Cluster0";

mongoose.connect(mongoURI)
    .then(() => console.log("✅ Connected to MongoDB Atlas"))
    .catch(err => console.log("❌ MongoDB Connection Error:", err));

// Define the shape of the data we will store
const VitalsSchema = new mongoose.Schema({
    patientId: String,
    bpm: Number,
    spo2: Number,
    ecg: Number,
    gsr: Number,
    timestamp: { type: Date, default: Date.now }
});

// Index for fast time-range queries
VitalsSchema.index({ timestamp: -1 });
VitalsSchema.index({ patientId: 1, timestamp: -1 });

const Vitals = mongoose.model('Vitals', VitalsSchema);

// --- 3. REST API ENDPOINTS ---

// GET /api/vitals — Query historical vitals with date range
app.get('/api/vitals', async (req, res) => {
    try {
        const { from, to, limit = 500 } = req.query;
        const filter = { bpm: { $gt: 0 } };

        if (from || to) {
            filter.timestamp = {};
            if (from) filter.timestamp.$gte = new Date(from);
            if (to) filter.timestamp.$lte = new Date(to);
        }

        const vitals = await Vitals.find(filter)
            .sort({ timestamp: -1 })
            .limit(parseInt(limit))
            .select('-__v')
            .lean();

        res.json({ success: true, count: vitals.length, data: vitals });
    } catch (err) {
        console.error("API Error (vitals):", err);
        res.status(500).json({ success: false, error: err.message });
    }
});

// GET /api/vitals/latest — Get the most recent reading
app.get('/api/vitals/latest', async (req, res) => {
    try {
        const latest = await Vitals.findOne({ bpm: { $gt: 0 } })
            .sort({ timestamp: -1 })
            .select('-__v')
            .lean();

        res.json({ success: true, data: latest });
    } catch (err) {
        console.error("API Error (latest):", err);
        res.status(500).json({ success: false, error: err.message });
    }
});

// GET /api/stats — Aggregated statistics for a time range
app.get('/api/stats', async (req, res) => {
    try {
        const { from, to } = req.query;
        const match = { bpm: { $gt: 0 } };

        if (from || to) {
            match.timestamp = {};
            if (from) match.timestamp.$gte = new Date(from);
            if (to) match.timestamp.$lte = new Date(to);
        }

        const result = await Vitals.aggregate([
            { $match: match },
            {
                $group: {
                    _id: null,
                    count: { $sum: 1 },
                    bpmMin: { $min: '$bpm' },
                    bpmMax: { $max: '$bpm' },
                    bpmAvg: { $avg: '$bpm' },
                    spo2Min: { $min: '$spo2' },
                    spo2Max: { $max: '$spo2' },
                    spo2Avg: { $avg: '$spo2' },
                    gsrMin: { $min: '$gsr' },
                    gsrMax: { $max: '$gsr' },
                    gsrAvg: { $avg: '$gsr' },
                    firstRecord: { $min: '$timestamp' },
                    lastRecord: { $max: '$timestamp' }
                }
            }
        ]);

        if (result.length === 0) {
            return res.json({
                success: true,
                data: { count: 0, bpm: null, spo2: null, duration: null }
            });
        }

        const s = result[0];
        const durationMs = new Date(s.lastRecord) - new Date(s.firstRecord);

        res.json({
            success: true,
            data: {
                count: s.count,
                bpm: {
                    min: Math.round(s.bpmMin),
                    max: Math.round(s.bpmMax),
                    avg: Math.round(s.bpmAvg)
                },
                spo2: {
                    min: Math.round(s.spo2Min),
                    max: Math.round(s.spo2Max),
                    avg: Math.round(s.spo2Avg)
                },
                duration: {
                    ms: durationMs,
                    text: formatDuration(durationMs)
                },
                firstRecord: s.firstRecord,
                lastRecord: s.lastRecord
            }
        });
    } catch (err) {
        console.error("API Error (stats):", err);
        res.status(500).json({ success: false, error: err.message });
    }
});

function formatDuration(ms) {
    if (!ms || ms <= 0) return '0s';
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);

    if (days > 0) return `${days}d ${hours % 24}h`;
    if (hours > 0) return `${hours}h ${minutes % 60}m`;
    if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
    return `${seconds}s`;
}

// --- 4. REAL-TIME SOCKET SERVER ---
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
                    patientId: "Patient_001",
                    bpm: parsedData.bpm,
                    spo2: parsedData.spo2,
                    ecg: parsedData.ecg,
                    gsr: parsedData.gsr || 0
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