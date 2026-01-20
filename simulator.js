const io = require('socket.io-client');

// Connect to your own local server
const socket = io('http://localhost:3000');

console.log("Simulator starting... Attempting to connect to server.");

let angle = 0;

setInterval(() => {
    // 1. Generate Fake ECG Wave (Sine wave with noise)
    // In real life, this comes from AD8232
    angle += 0.2;
    const ecgValue = Math.sin(angle) * 100 + (Math.random() * 10); 

    // 2. Generate Fake Vitals (Heart rate fluctuates slightly)
    const fakeBPM = 72 + Math.floor(Math.random() * 5);
    const fakeSpO2 = 95 + Math.floor(Math.random() * 2);
    const fakeTemp = 36.6;

    // 3. Send Packet to Server
    socket.emit('sensor_data', {
        bpm: fakeBPM,
        spo2: fakeSpO2,
        temp: fakeTemp,
        ecg: ecgValue
    });

}, 100); // Send data every 100ms (10 times a second)