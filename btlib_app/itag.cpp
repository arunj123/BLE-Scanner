// itag.cpp

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <cstdlib>
#include <cstdio> // For EOF
#include <mutex>
#include <condition_variable>
#include <queue>

// For select
// For select, STDIN_FILENO
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h> // For fcntl and F_SETFL, O_NONBLOCK

extern "C" {
#include "btlib.h"
}


// --- Global Variables and Function Declarations ---
// These need to be accessible to both threads
std::atomic<bool> keep_running{true}; // Use atomic for thread-safe boolean
void setAlertLevel(int level);
void read_notify(int timeout_ms); // This is from btlib.h, not directly used here for LE scan

const int BUTTON_CHARACTERISTIC_INDEX = 4;
const int ALERT_LEVEL_CHARACTERISTIC_INDEX = 3;
const int ITAG_NODE = 7; // This seems to be for a specific iTag, not relevant for general TP357 scanning

int notification_callback_handler(int node, int cticn, unsigned char* data, int datlen) {
    std::cout << "\n--- iTag Button Clicked! ---" << std::endl;
    // In a real application, you might want to queue this for processing in the main thread
    // to avoid complex synchronization issues if this callback is called from a different thread.
    return 0;
}

// New callback handler for LE advertisement reports with temperature/humidity
int tp357_adv_callback_handler(unsigned char* mac_address, float temperature, float humidity) {
    return 0;
    std::cout << "\n--- TP357 Device Data Received! ---\n";
    std::cout << "  MAC: " << std::hex << (int)mac_address[5] << ":" << (int)mac_address[4] << ":"
              << (int)mac_address[3] << ":" << (int)mac_address[2] << ":"
              << (int)mac_address[1] << ":" << (int)mac_address[0] << std::dec << std::endl;
    std::cout << "  Temperature: " << temperature << " C" << std::endl;
    std::cout << "  Humidity: " << humidity << " %" << std::endl;
    return 0;
}


class iTagController {
private:
    int itag_node_id = -1;

public:
    ~iTagController() {
        if (itag_node_id != -1) {
            std::cout << "\nDisconnecting from iTag..." << std::endl;
            // Only attempt to notify_ctic and disconnect if itag_node_id was actually connected
            // This part is specific to the iTag connection, not the general LE scan
            // You might want to separate iTag specific control from general scanning.
            notify_ctic(itag_node_id, BUTTON_CHARACTERISTIC_INDEX, NOTIFY_DISABLE, nullptr);
            disconnect_node(itag_node_id);
        }
        close_all();
        std::cout << "Cleanup complete." << std::endl;
    }

    bool initializeAndConnect() {
        char devices_file[] = "devices.txt";
        if (init_blue(devices_file) == 0) {
            std::cerr << "Failed to initialize Bluetooth library." << std::endl;
            return false;
        }

        // The following part is specific to connecting to a *single* iTag device.
        // If your goal is only to scan for TP357 devices, you might not need this.
        // I'm keeping it for now as it was in your original itag.cpp.
        itag_node_id = ITAG_NODE;
        connect_node(itag_node_id, CHANNEL_LE,0); // Attempting connection to ITAG_NODE

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (find_ctics(itag_node_id) == 0) {
            std::cerr << "Characteristic discovery failed for iTag (node " << itag_node_id << "). This might be expected if only scanning for TP357.\n";
            // return false; // Don't return false if we still want to scan for TP357
        } else {
            if (notify_ctic(itag_node_id, BUTTON_CHARACTERISTIC_INDEX, NOTIFY_ENABLE, notification_callback_handler) == 0) {
                std::cerr << "Notification subscription failed for iTag (node " << itag_node_id << ").\n";
            } else {
                std::cout << "Connected to iTag (node " << itag_node_id << ") and listening for button notifications.\n";
            }
        }
        return true; // Return true even if iTag connection failed, if we still want to scan
    }

    bool setAlertLevel(int level) {
        if (itag_node_id == -1) {
            std::cerr << "iTag not connected. Cannot set alert level.\n";
            return false;
        }
        unsigned char dat[1] = { static_cast<unsigned char>(level) };
        return write_ctic(itag_node_id, ALERT_LEVEL_CHARACTERISTIC_INDEX, dat, 1) != 0;
    }

    void monitor() {
        le_scan_background_start();
        while (keep_running) {
            // Pass the new callback handler to le_scan_background_read
            le_scan_background_read(tp357_adv_callback_handler);
            // Add a small sleep to prevent busy-waiting if no packets are found
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

void signal_handler(int) { keep_running = false; }

int main() {
    signal(SIGINT, signal_handler);
    iTagController itag;
    if (itag.initializeAndConnect()) {
        itag.monitor();
    } else {
        std::cerr << "Initialization failed. Exiting.\n";
    }
    return 0;
}
