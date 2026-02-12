const io = require('socket.io-client');

// Connect to your own local server
const socket = io('http://localhost:3000');

console.log("Simulator starting... Attempting to connect to server.");

let packetCount = 0;
let ecgSampleIndex = 0;

// ECG parameters - FAST with sharper peaks
const SAMPLES_PER_BEAT = 25;
const BASELINE = 0;

/**
 * Generates a sharp, realistic ECG sample (PQRST complex)
 */
function generateECGSample(t) {
    t = t % 1;
    let value = BASELINE;

    // P Wave (0.08 - 0.16)
    if (t >= 0.08 && t <= 0.16) {
        const pT = (t - 0.08) / 0.08;
        value = 12 * (pT < 0.5 ? pT * 2 : (1 - pT) * 2);
    }

    // QRS Complex (0.20 - 0.32) - SHARP spikes
    else if (t >= 0.20 && t <= 0.32) {
        const qrsT = (t - 0.20) / 0.12;

        if (qrsT < 0.20) {
            value = -15 * (qrsT / 0.20);
        }
        else if (qrsT < 0.45) {
            const rT = (qrsT - 0.20) / 0.25;
            value = -15 + (135 * rT);
        }
        else if (qrsT < 0.65) {
            const rT = (qrsT - 0.45) / 0.20;
            value = 120 - (155 * rT);
        }
        else {
            const sT = (qrsT - 0.65) / 0.35;
            value = -35 + (35 * sT);
        }
    }

    // ST Segment (0.32 - 0.42)
    else if (t > 0.32 && t < 0.42) {
        value = 1;
    }

    // T Wave (0.42 - 0.58)
    else if (t >= 0.42 && t <= 0.58) {
        const tT = (t - 0.42) / 0.16;
        value = 20 * Math.sin(Math.PI * tT);
    }

    // Baseline
    else {
        value = 0;
    }

    value += (Math.random() - 0.5) * 2;
    return value;
}

setInterval(() => {
    // Generate realistic ECG waveform
    const cyclePosition = (ecgSampleIndex % SAMPLES_PER_BEAT) / SAMPLES_PER_BEAT;
    const ecgValue = generateECGSample(cyclePosition);
    ecgSampleIndex++;

    // Generate Fake Vitals
    const fakeBPM = 72 + Math.floor(Math.random() * 5);
    const fakeSpO2 = 95 + Math.floor(Math.random() * 2);
    const fakeTemp = 36.6;

    // Send Packet to Server
    socket.emit('sensor_data', {
        bpm: fakeBPM,
        spo2: fakeSpO2,
        temp: fakeTemp,
        ecg: ecgValue
    });

    packetCount++;
    if (packetCount % 10 === 0) {
        console.log(`📤 Packets: ${packetCount} | BPM: ${fakeBPM} | SpO2: ${fakeSpO2}%`);
    }

}, 100); // Send data every 100ms