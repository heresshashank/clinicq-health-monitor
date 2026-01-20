/**
 * ClinicQ Cloud Simulator
 * Tests your LIVE cloud server on Render
 * 
 * Run with: node simulator-cloud.js
 */

const io = require('socket.io-client');

// Connect to LIVE cloud server
const CLOUD_URL = 'https://clinicq-health-monitor.onrender.com';
const socket = io(CLOUD_URL, {
    transports: ['websocket', 'polling']
});

console.log("🌐 Cloud Simulator starting...");
console.log(`📡 Connecting to: ${CLOUD_URL}`);

socket.on('connect', () => {
    console.log("✅ Connected to cloud server!");
    console.log("📊 Sending simulated health data...\n");
});

socket.on('connect_error', (error) => {
    console.log("❌ Connection error:", error.message);
});

socket.on('disconnect', () => {
    console.log("🔌 Disconnected from server");
});

let angle = 0;
let packetCount = 0;

setInterval(() => {
    if (!socket.connected) return;

    // Generate Fake ECG Wave (Sine wave with noise)
    angle += 0.2;
    const ecgValue = Math.sin(angle) * 100 + (Math.random() * 10);

    // Generate Fake Vitals
    const fakeBPM = 72 + Math.floor(Math.random() * 5);
    const fakeSpO2 = 95 + Math.floor(Math.random() * 3);
    const fakeTemp = 36.5 + (Math.random() * 0.3);

    // Send Packet to Cloud Server
    socket.emit('sensor_data', {
        bpm: fakeBPM,
        spo2: fakeSpO2,
        temp: parseFloat(fakeTemp.toFixed(1)),
        ecg: ecgValue
    });

    packetCount++;
    if (packetCount % 10 === 0) {
        console.log(`📤 Packets sent: ${packetCount} | BPM: ${fakeBPM} | SpO2: ${fakeSpO2}%`);
    }

}, 100); // Send data every 100ms

console.log("\n💡 Tip: Open your dashboard in browser:");
console.log(`   ${CLOUD_URL}\n`);
console.log("Press Ctrl+C to stop the simulator.\n");
