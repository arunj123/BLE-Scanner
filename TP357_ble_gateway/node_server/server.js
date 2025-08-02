const express = require('express');
const app = express();
const port = 3000;

// Middleware to parse raw binary body
app.use(express.raw({ type: 'application/octet-stream', limit: '1mb' }));

// Function to decode the binary blob (matches C++ serialization)
function decodeSensorDataBlob(blobData) {
    const decodedEntries = [];
    let offset = 0;

    try {
        if (!blobData || blobData.length < 1) {
            console.warn("Warning: Received empty or too short blob data.");
            return [];
        }

        // 1. Total Number of Sensors (1 byte: uint8)
        const numSensors = blobData.readUInt8(offset);
        offset += 1;

        for (let i = 0; i < numSensors; i++) {
            const entry = {};

            // MAC Address (6 bytes: raw bytes)
            if (blobData.length < offset + 6) {
                console.error("Error: Blob too short for MAC address.");
                return [];
            }
            const macBytes = blobData.subarray(offset, offset + 6);
            entry.mac_address = Array.from(macBytes).map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(':');
            offset += 6;

            // Temperature (8 bytes: double)
            if (blobData.length < offset + 8) {
                console.error("Error: Blob too short for temperature.");
                return [];
            }
            entry.temperature = blobData.readDoubleLE(offset); // Little-endian double
            offset += 8;

            // Humidity (8 bytes: double)
            if (blobData.length < offset + 8) {
                console.error("Error: Blob too short for humidity.");
                return [];
            }
            entry.humidity = blobData.readDoubleLE(offset); // Little-endian double
            offset += 8;

            // RSSI (1 byte: int8)
            if (blobData.length < offset + 1) {
                console.error("Error: Blob too short for RSSI.");
                return [];
            }
            entry.rssi = blobData.readInt8(offset); // Signed 8-bit integer
            offset += 1;

            decodedEntries.push(entry);
        }

    } catch (e) {
        console.error(`Error decoding binary data: ${e.message}`);
        return [];
    }

    return decodedEntries;
}

// POST endpoint to receive sensor data
app.post('/sensor-data', (req, res) => {
    const timestamp = new Date().toISOString();
    console.log(`\n--- Received Data at ${timestamp} ---`);

    if (req.headers['content-type'] !== 'application/octet-stream') {
        console.warn('Received data with unexpected Content-Type:', req.headers['content-type']);
        return res.status(400).send('Content-Type must be application/octet-stream');
    }

    const binaryData = req.body; // req.body is a Buffer due to express.raw()

    if (!binaryData || binaryData.length === 0) {
        console.warn('Received empty binary data.');
        return res.status(400).send('Empty data received.');
    }

    console.log(`Received ${binaryData.length} bytes of binary data.`);

    const decodedSensors = decodeSensorDataBlob(binaryData);

    if (decodedSensors.length > 0) {
        console.log("Decoded Sensor Readings:");
        decodedSensors.forEach((sensor, index) => {
            console.log(`  Sensor ${index + 1}:`);
            console.log(`    MAC Address: ${sensor.mac_address}`);
            // Predefined Name and Decoded Device Name are not stored in the blob anymore
            console.log(`    Temperature: ${sensor.temperature.toFixed(1)} C`);
            console.log(`    Humidity: ${sensor.humidity.toFixed(1)} %`);
            console.log(`    RSSI: ${sensor.rssi} dBm`);
        });
        res.status(200).send('Data received and decoded successfully.');
    } else {
        console.error("Failed to decode received binary data.");
        res.status(400).send('Failed to decode data.');
    }
    console.log("-".repeat(40)); // Fixed: Use repeat() for string repetition
});

// Basic GET endpoint for health check
app.get('/', (req, res) => {
    res.send('Sensor Data API is running!');
});

app.listen(port, () => {
    console.log(`Sensor Data API listening at http://localhost:${port}`);
    console.log(`POST binary sensor data to http://localhost:${port}/sensor-data`);
});
