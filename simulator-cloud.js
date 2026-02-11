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

let packetCount = 0;
let ecgSampleIndex = 0;

/**
 * Realistic ECG Waveform Generator
 * Generates a PQRST complex that mimics real cardiac electrical activity
 * 
 * ECG Components:
 * - P Wave: Atrial depolarization (small positive bump)
 * - QRS Complex: Ventricular depolarization (sharp spike)
 *   - Q: Small negative deflection
 *   - R: Large positive spike (main peak)
 *   - S: Negative deflection after R
 * - T Wave: Ventricular repolarization (positive bump)
 * - Baseline: Isoelectric line between waves
 */

// ECG parameters - FAST with sharper peaks
const SAMPLES_PER_BEAT = 25;   // Faster cycle (more beats visible)
const BASELINE = 0;

/**
 * Generates a sharp, realistic ECG sample
 * Uses triangular/exponential shapes for crisp peaks
 */
function generateECGSample(t) {
    t = t % 1;
    let value = BASELINE;

    // P Wave (0.08 - 0.16) - Small bump, sharper
    if (t >= 0.08 && t <= 0.16) {
        const pT = (t - 0.08) / 0.08;
        // Triangular shape for sharper P wave
        value = 12 * (pT < 0.5 ? pT * 2 : (1 - pT) * 2);
    }

    // QRS Complex (0.20 - 0.32) - SHARP spikes
    else if (t >= 0.20 && t <= 0.32) {
        const qrsT = (t - 0.20) / 0.12;

        // Q wave - quick dip
        if (qrsT < 0.20) {
            value = -15 * (qrsT / 0.20);
        }
        // R wave - SHARP triangular spike up
        else if (qrsT < 0.45) {
            const rT = (qrsT - 0.20) / 0.25;
            value = -15 + (135 * rT);  // Goes from -15 to +120
        }
        // R wave down - sharp fall
        else if (qrsT < 0.65) {
            const rT = (qrsT - 0.45) / 0.20;
            value = 120 - (155 * rT);  // Goes from +120 to -35
        }
        // S wave recovery
        else {
            const sT = (qrsT - 0.65) / 0.35;
            value = -35 + (35 * sT);   // Goes from -35 to 0
        }
    }

    // ST Segment (0.32 - 0.42)
    else if (t > 0.32 && t < 0.42) {
        value = 1;
    }

    // T Wave (0.42 - 0.58) - Rounded bump
    else if (t >= 0.42 && t <= 0.58) {
        const tT = (t - 0.42) / 0.16;
        // Smoother T wave with sine
        value = 20 * Math.sin(Math.PI * tT);
    }

    // Baseline
    else {
        value = 0;
    }

    // Minimal noise
    value += (Math.random() - 0.5) * 2;

    return value;
}

setInterval(() => {
    if (!socket.connected) return;

    // Generate realistic ECG waveform sample
    const cyclePosition = (ecgSampleIndex % SAMPLES_PER_BEAT) / SAMPLES_PER_BEAT;
    const ecgValue = generateECGSample(cyclePosition);
    ecgSampleIndex++;

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
