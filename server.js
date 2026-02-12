const express = require('express');
const http = require('http');
const { Server } = require("socket.io");
const cors = require('cors');
const path = require('path');

const app = express();
app.use(cors());

// --- THE FIX IS HERE ---
// Your files are in the root directory, NOT inside 'public'
// So we tell express to serve static files from the root (__dirname)
app.use(express.static(__dirname));

const server = http.createServer(app);
const io = new Server(server, {
    cors: {
        origin: "*", 
        methods: ["GET", "POST"]
    }
});

io.on('connection', (socket) => {
    console.log('User connected:', socket.id);

    // Initial data for new users
    socket.emit('vitals_update', { bpm: '--', spo2: '--', temp: '--' });

    socket.on('sensor_data', (data) => {
        // Broadcast to everyone
        io.emit('sensor_data', data); 
        io.emit('vitals_update', data); 
        if(data.ecg) {
            io.emit('ecg_stream', data.ecg); 
        }
    });
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});