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
#include <iostream>
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
void read_notify(int timeout_ms);

const int BUTTON_CHARACTERISTIC_INDEX = 4;
const int ALERT_LEVEL_CHARACTERISTIC_INDEX = 3;
const int ITAG_NODE = 7;

int notification_callback_handler(int node, int cticn, unsigned char* data, int datlen) {
    std::cout << "\n--- iTag Button Clicked! ---" << std::endl;
    return 0;
}

class iTagController {
private:
    int itag_node_id = -1;

public:
    ~iTagController() {
        if (itag_node_id != -1) {
            std::cout << "\nDisconnecting from iTag..." << std::endl;
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

        itag_node_id = ITAG_NODE;
        connect_node(itag_node_id, CHANNEL_LE,0);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (find_ctics(itag_node_id) == 0) {
            std::cerr << "Characteristic discovery failed." << std::endl;
            return false;
        }

        if (notify_ctic(itag_node_id, BUTTON_CHARACTERISTIC_INDEX, NOTIFY_ENABLE, notification_callback_handler) == 0) {
            std::cerr << "Notification subscription failed." << std::endl;
            return false;
        }

        std::cout << "Connected and listening for button notifications.\n";
        return true;
    }

    bool setAlertLevel(int level) {
        unsigned char dat[1] = { static_cast<unsigned char>(level) };
        return write_ctic(itag_node_id, ALERT_LEVEL_CHARACTERISTIC_INDEX, dat, 1) != 0;
    }

    void monitor() {
        le_scan_background_start();
        while (keep_running) {
            le_scan_background_read();
        }
    }
};

void signal_handler(int) { keep_running = false; }

int main() {
    signal(SIGINT, signal_handler);
    iTagController itag;
    if (itag.initializeAndConnect()) itag.monitor();
    return 0;
}
