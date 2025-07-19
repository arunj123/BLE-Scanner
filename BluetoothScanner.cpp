#include "BluetoothScanner.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h> // Required for errno
#include <sys/select.h> // Required for select()

BluetoothScanner::BluetoothScanner() : dd_(-1), keep_running_(true) {
    // Constructor: Initializes members. Device is opened in init().
}

BluetoothScanner::~BluetoothScanner() {
    // Destructor: Ensures cleanup if not already done.
    stopScan();
}

bool BluetoothScanner::init() {
    int dev_id = hci_get_route(NULL); // Get ID of default HCI device
    if (dev_id < 0) {
        perror("HCI device not found");
        return false;
    }

    dd_ = hci_open_dev(dev_id); // Open HCI device
    if (dd_ < 0) {
        perror("HCI device open failed");
        return false;
    }

    std::cout << "Opened HCI device with ID: " << dev_id << std::endl;

    // Set LE scan parameters
    uint8_t scan_type = 0x01; // Active scanning (0x00 for Passive)
    uint16_t interval = htobs(0x0010); // 10 ms (0x0010 * 0.625ms = 10ms)
    uint16_t window = htobs(0x0010);   // 10 ms
    uint8_t own_address_type = 0x00; // Public address
    uint8_t scanning_filter_policy = 0x00; // Accept all advertising packets

    int ret = hci_le_set_scan_parameters(dd_, scan_type, interval, window, own_address_type, scanning_filter_policy, 1000);
    if (ret < 0) {
        perror("Failed to set scan parameters");
        hci_close_dev(dd_);
        dd_ = -1; // Mark as closed
        return false;
    }
    std::cout << "LE Scan parameters set." << std::endl;

    // Enable LE scan
    uint8_t enable = 0x01; // Enable scanning
    uint8_t filter_duplicates = 0x00; // Do not filter duplicates (report all advertisements)

    ret = hci_le_set_scan_enable(dd_, enable, filter_duplicates, 1000);
    if (ret < 0) {
        perror("Failed to enable scan");
        hci_close_dev(dd_);
        dd_ = -1; // Mark as closed
        return false;
    }
    std::cout << "LE Scan enabled. Waiting for advertisements..." << std::endl;

    // Set up a filter for HCI events
    struct hci_filter nf;
    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META, &nf); // Listen for LE Meta Events
    hci_filter_set_event(EVT_DISCONN_COMPLETE, &nf); // Also listen for disconnects for cleanup
    hci_filter_set_event(EVT_CMD_STATUS, &nf); // For command status events
    hci_filter_set_event(EVT_CMD_COMPLETE, &nf); // For command complete events

    if (setsockopt(dd_, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
        perror("HCI filter setup failed");
        hci_close_dev(dd_);
        dd_ = -1; // Mark as closed
        return false;
    }

    return true;
}

void BluetoothScanner::startScan() {
    if (dd_ < 0) {
        std::cerr << "BluetoothScanner not initialized. Call init() first." << std::endl;
        return;
    }

    uint8_t buf[HCI_MAX_EVENT_SIZE];
    char addr[18]; // For MAC address string

    fd_set fds;
    struct timeval tv;
    int ret;

    while (keep_running_.load()) { // Loop while keep_running flag is set
        // Check if the device descriptor is still valid before calling select
        if (dd_ < 0) {
            std::cerr << "Device descriptor became invalid. Exiting scan loop." << std::endl;
            break;
        }

        FD_ZERO(&fds);
        FD_SET(dd_, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100 ms timeout

        ret = select(dd_ + 1, &fds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, recheck keep_running_
                continue;
            }
            if (errno == EBADF) { // Bad file descriptor, likely closed by stopScan()
                std::cerr << "HCI device descriptor is invalid (EBADF). Exiting scan loop." << std::endl;
                break;
            }
            perror("select");
            break; // Exit loop on critical error
        }

        if (ret == 0) {
            // Timeout, no data, recheck keep_running_
            continue;
        }

        // Data is available to read
        int len = read(dd_, buf, sizeof(buf)); // Using raw read()
        if (len < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                // Non-blocking read, no data yet, or interrupted by signal
                continue;
            }
            if (errno == EBADF) { // Bad file descriptor, likely closed by stopScan()
                std::cerr << "HCI device descriptor is invalid (EBADF). Exiting scan loop." << std::endl;
                break;
            }
            perror("Error reading HCI event");
            break; // Exit loop on critical error
        }

        // Ensure we have at least the packet type byte + event header
        if (len < (1 + HCI_EVENT_HDR_SIZE)) { // 1 byte for packet type, HCI_EVENT_HDR_SIZE for event header
            std::cerr << "Received malformed HCI event (too short)" << std::endl;
            continue;
        }

        // The first byte is the packet type (e.g., HCI_EVENT_PKT). We expect it to be 0x04 for events.
        uint8_t packet_type = buf[0];
        if (packet_type != HCI_EVENT_PKT) {
            // Not an HCI event packet, skip or handle as error
            std::cerr << "Received non-HCI event packet (type: 0x" << std::hex << (int)packet_type << std::dec << ")" << std::endl;
            continue;
        }

        // Parse the event header, which starts at buf[1] after the packet type
        evt_hdr *hdr = (evt_hdr *)(buf + 1); // Cast the buffer to event header, offset by 1 for packet type
        uint8_t event_code = hdr->evt;
        uint8_t event_len = hdr->len;

        // Check if the received length matches the expected length
        // Expected total length = 1 (packet type) + HCI_EVENT_HDR_SIZE + event_len
        if (len != (1 + HCI_EVENT_HDR_SIZE + event_len)) {
            std::cerr << "Received HCI event with inconsistent length. Expected: "
                      << (1 + HCI_EVENT_HDR_SIZE + event_len) << ", Got: " << len << std::endl;
            continue;
        }

        if (event_code == EVT_LE_META) {
            // The meta event data starts after the HCI event header (event code + length byte)
            // Offset is 1 (packet type) + HCI_EVENT_HDR_SIZE
            evt_le_meta_event *meta = (evt_le_meta_event *)(buf + 1 + HCI_EVENT_HDR_SIZE);

            if (meta->subevent == EVT_LE_ADVERTISING_REPORT) {
                uint8_t reports_count = meta->data[0];
                // Pointer to the first advertising info struct.
                // It starts after the reports_count byte.
                void *ptr = meta->data + 1;

                for (int i = 0; i < reports_count; i++) {
                    le_advertising_info *info = (le_advertising_info *)ptr;

                    // Convert Bluetooth address to string
                    ba2str(&info->bdaddr, addr);

                    // RSSI is the last byte of the advertising data (info->data).
                    // info->length is the length of the AD data *excluding* RSSI.
                    // The RSSI byte is at info->data[info->length].
                    int8_t rssi = (int8_t)info->data[info->length];

                    std::string device_name;
                    double temperature = -999.0;
                    double humidity = -999.0;

                    // Always try to parse the device name and sensor data
                    parse_advertising_data(info->data, info->length, device_name, temperature, humidity, false);

                    // Check if the device name contains "TP357"
                    if (device_name.find("TP357") != std::string::npos) {
                        std::cout << "\n--- Detected TP357 Device ---" << std::endl;
                        std::cout << "Address: " << addr << std::endl;
                        std::cout << "RSSI: " << (int)rssi << std::endl;
                        // Now, print verbose AD types and decoded sensor data for TP357 devices
                        parse_advertising_data(info->data, info->length, device_name, temperature, humidity, true);
                        std::cout << "-----------------------------" << std::endl;
                    } else {
                        // For non-TP357 devices, do not print any output to filter them out
                        // If you want to see a concise summary for non-TP357 devices, uncomment the line below:
                        // std::cout << "\nAdvertisement from (non-TP357): " << addr << " (Name: \"" << device_name << "\", RSSI: " << (int)rssi << ")" << std::endl;
                    }

                    // Move pointer to the next advertising info struct.
                    // This is sizeof(le_advertising_info) + info->length + 1 (for RSSI).
                    ptr = (uint8_t *)ptr + sizeof(*info) + info->length + 1;
                }
            }
        }
    }
}

void BluetoothScanner::stopScan() {
    keep_running_.store(false); // Signal the scanning loop to stop

    if (dd_ >= 0) { // Only attempt cleanup if device was successfully opened
        std::cout << "Disabling LE scan..." << std::endl;
        // Explicitly disable scan first. Ignore errors as device might already be down.
        hci_le_set_scan_enable(dd_, 0x00, 0x00, 1000);

        std::cout << "Closing HCI device..." << std::endl;
        // Closing the device will unblock any pending read/select calls on it.
        hci_close_dev(dd_);
        dd_ = -1; // Mark as closed
    }
}

// Private helper function implementation
void BluetoothScanner::parse_advertising_data(uint8_t *data, int len, std::string& device_name_out,
                                            double& temperature_out, double& humidity_out, bool verbose_output) {
    device_name_out.clear(); // Clear previous name for each new advertisement
    temperature_out = -999.0; // Initialize with an invalid value
    humidity_out = -999.0;    // Initialize with an invalid value

    int offset = 0;
    while (offset < len) {
        uint8_t field_len = data[offset];
        if (field_len == 0) break; // End of advertising data

        uint8_t field_type = data[offset + 1];
        uint8_t *field_data = &data[offset + 2];
        int field_data_len = field_len - 1;

        if (verbose_output) {
            // Print all AD types and their raw data for debugging/completeness
            std::cout << "    AD Type: 0x" << std::hex << (int)field_type << std::dec << " (Len: " << (int)field_len << ") Raw Data: ";
            for (int i = 0; i < field_data_len; ++i) {
                printf("%02X ", field_data[i]);
            }
            std::cout << std::endl;
        }

        switch (field_type) {
            case AD_TYPE_COMPLETE_LOCAL_NAME:
            case AD_TYPE_SHORT_LOCAL_NAME: {
                // Extract device name
                device_name_out.assign((char*)field_data, field_data_len);
                if (verbose_output) {
                    std::cout << "      Decoded Device Name: \"" << device_name_out << "\"" << std::endl;
                }
                break;
            }
            case AD_TYPE_MANUFACTURER_SPECIFIC_DATA: {
                if (verbose_output) {
                    std::cout << "      Decoded Manufacturer Specific Data: ";
                }
                // Check if enough data for Company ID (2 bytes), Temperature (2 bytes), and Humidity (1 byte)
                // Based on ESP32 snippet, temperature is at index 1,2 (little-endian) and humidity at index 3.
                // So, we need at least 4 bytes of manufacturer data (field_data[0] to field_data[3])
                if (field_data_len >= 4) {
                    uint16_t company_id = field_data[0] | (field_data[1] << 8); // Little-endian Company ID
                    if (verbose_output) {
                        std::cout << "Company ID: 0x" << std::hex << company_id << std::dec << " ";
                    }

                    // Temperature: 16-bit little-endian from field_data[1] and field_data[2]
                    int16_t temp_raw = (field_data[1] | (field_data[2] << 8));
                    temperature_out = static_cast<double>(temp_raw) / 10.0; // Scaled by 10.0

                    // Humidity: 8-bit from field_data[3]
                    humidity_out = static_cast<double>(field_data[3]); // Direct percentage

                    if (verbose_output) {
                        std::cout << "Temperature: " << temperature_out << " C, ";
                        std::cout << "Humidity: " << humidity_out << " %";
                    }
                } else {
                    if (verbose_output) {
                        std::cout << "Not enough data for full decoding (expected at least 4 bytes, got " << field_data_len << ")";
                    }
                }
                if (verbose_output) {
                    std::cout << std::endl;
                }
                break;
            }
            // Add more cases for other AD types if specific decoding is needed
            // default: raw data already printed above (if verbose)
        }
        offset += field_len + 1; // Move to the next advertising data field
    }
}

