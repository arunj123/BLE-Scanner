#ifndef BLUETOOTH_SCANNER_H
#define BLUETOOTH_SCANNER_H

#include <string>
#include <atomic> // For std::atomic_bool
#include <bluetooth/bluetooth.h> // For ba2str
#include <bluetooth/hci.h>       // For HCI event types and structures
#include <bluetooth/hci_lib.h>   // For hci_open_dev, hci_close_dev, etc.

// --- Conditional definitions for potentially missing BlueZ symbols ---
// These are duplicated here to ensure BluetoothScanner.cpp compiles independently.

// Define EVT_LE_META if not already defined (it should be in bluetooth/hci.h)
#ifndef EVT_LE_META
#define EVT_LE_META 0x3E
#endif

// Define HCI_EVENT_HDR_SIZE if not already defined
#ifndef HCI_EVENT_HDR_SIZE
#define HCI_EVENT_HDR_SIZE 2 // Event code (1 byte) + Parameter Total Length (1 byte)
#endif

// Define evt_hdr if not already defined by bluetooth/hci.h
#ifndef EVT_HDR_DEFINED
#define EVT_HDR_DEFINED
typedef struct {
    uint8_t evt;
    uint8_t len;
} __attribute__((packed)) evt_hdr;
#endif

// --- Advertising Data Types (AD Types) based on Bluetooth Core Specification ---
#ifndef AD_TYPE_FLAGS
#define AD_TYPE_FLAGS 0x01
#define AD_TYPE_INCOMPLETE_LIST_16_BIT_SERVICE_UUIDS 0x02
#define AD_TYPE_COMPLETE_LIST_16_BIT_SERVICE_UUIDS 0x03
#define AD_TYPE_INCOMPLETE_LIST_32_BIT_SERVICE_UUIDS 0x04
#define AD_TYPE_COMPLETE_LIST_32_BIT_SERVICE_UUIDS 0x05
#define AD_TYPE_INCOMPLETE_LIST_128_BIT_SERVICE_UUIDS 0x06
#define AD_TYPE_COMPLETE_LIST_128_BIT_SERVICE_UUIDS 0x07
#define AD_TYPE_SHORT_LOCAL_NAME 0x08
#define AD_TYPE_COMPLETE_LOCAL_NAME 0x09
#define AD_TYPE_TX_POWER_LEVEL 0x0A
#define AD_TYPE_MANUFACTURER_SPECIFIC_DATA 0xFF
#endif
// --- End of conditional definitions ---

class BluetoothScanner {
public:
    BluetoothScanner();
    ~BluetoothScanner();

    // Initializes the Bluetooth HCI device and sets scan parameters.
    // Returns true on success, false on failure.
    bool init();

    // Starts the BLE scanning loop. This function is intended to be run in a separate thread.
    void startScan();

    // Stops the BLE scanning loop and cleans up the HCI device.
    void stopScan();

private:
    int dd_; // Device descriptor for the HCI device
    std::atomic<bool> keep_running_; // Flag to control the scanning loop

    // Private helper function to parse advertising data
    void parse_advertising_data(uint8_t *data, int len, std::string& device_name_out,
                                double& temperature_out, double& humidity_out, bool verbose_output);
};

#endif // BLUETOOTH_SCANNER_H

