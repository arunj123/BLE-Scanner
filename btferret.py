import btlib.btfpy as btfpy
import time
import sys
import select # Import select for non-blocking input (Unix-specific)

# Define the iTag's fixed MAC address
ITAG_MAC_ADDRESS = "23:06:17:02:84:37"

# --- CHARACTERISTIC INDICES (DIRECTLY FROM YOUR BTFERRET.PY SCAN OUTPUT) ---
# Based on your btferret.py output:
#   index 4: FFE1 UUID (Permit 12 rn) - Most likely for button notifications
#   index 2: Alert Level UUID (Permit 0A rwa) - For setting immediate alerts
BUTTON_CHARACTERISTIC_INDEX = 4
ALERT_LEVEL_CHARACTERISTIC_INDEX = 3

# Global variable to store the connected node ID
current_itag_node_id = -1

# Flag to track if a prompt is currently displayed and needs a newline before next output
prompt_active = False

# Function to display the command prompt
def display_prompt():
    global prompt_active
    if not prompt_active: # Only print if not already active
        sys.stdout.write("\nEnter command (a0, a1, a2) or Ctrl+C to exit: > ")
        sys.stdout.flush()
        prompt_active = True

# Function to clear the prompt (print newline) if active before other output
def clear_prompt_if_active():
    global prompt_active
    if prompt_active:
        sys.stdout.write("\n") # Print newline to move cursor
        sys.stdout.flush()
        prompt_active = False # Prompt is no longer active

# Callback function to handle notifications
def my_notification_callback(node, cticn, dat, datlen):
    """
    This function is called when a characteristic notification is received.
    It now specifically checks for the button press data (b'\x01').
    """
    clear_prompt_if_active() # Clear any active prompt before printing notification
    characteristic_name = btfpy.Ctic_name(node, cticn) # Get characteristic name for better logging
    print(f"\nNotification received from iTag (Node {node}) on characteristic {characteristic_name} (Index {cticn}):")
    btfpy.Print_data(dat) # This will print the raw data bytes received
    
    # Check if the received data corresponds to a button press
    if dat == b'\x01':
        print("--- iTag Button Clicked! ---")
    else:
        print("Received other data from button characteristic.")
    
    display_prompt() # Re-prompt after notification

# Function to set the immediate alert level
def set_immediate_alert(node_id, level):
    """
    Writes the specified alert level (0, 1, or 2) to the Immediate Alert characteristic.
    Level 0: No Alert
    Level 1: Mild Alert
    Level 2: High Alert
    """
    # We remove Is_connected check here as it's not available.
    # We'll rely on Write_ctic's return value for success/failure.

    if level not in [0, 1, 2]:
        clear_prompt_if_active()
        print("Invalid alert level. Must be 0, 1, or 2.")
        display_prompt()
        return False

    # Convert the integer level to a single byte
    alert_data = bytes([level])
    
    clear_prompt_if_active() # Clear prompt before printing alert status
    print(f"Attempting to set Immediate Alert level to {level}...")
    # Write the alert level to the characteristic using its direct index
    if btfpy.Write_ctic(node_id, ALERT_LEVEL_CHARACTERISTIC_INDEX, alert_data, len(alert_data)) == 0:
        print(f"Failed to write alert level {level}. Ensure permissions are correct and device is connected.")
        display_prompt()
        return False
    else:
        print(f"Successfully set Immediate Alert level to {level}.")
        display_prompt()
        return True

def connect_and_monitor_itag():
    global current_itag_node_id

    # Initialize the btfpy library with the devices.txt file
    if btfpy.Init_blue("devices.txt") == 0:
        print("Failed to initialize Bluetooth. Ensure 'devices.txt' exists and permissions are correct.")
        return

    print("Performing LE scan to find iTag...")
    btfpy.Le_scan() 

    found_itag_node_id = -1
    for node_candidate in range(1, 10001):
        try:
            address = btfpy.Device_address(node_candidate)
            if address == ITAG_MAC_ADDRESS:
                found_itag_node_id = node_candidate
                print(f"Found iTag with MAC address {ITAG_MAC_ADDRESS} at current Node: {found_itag_node_id}")
                break
        except Exception:
            pass

    if found_itag_node_id == -1:
        print(f"iTag with MAC address {ITAG_MAC_ADDRESS} not found after scan.")
        print("Please ensure the iTag is powered on and advertising.")
        btfpy.Close_all()
        return

    current_itag_node_id = found_itag_node_id

    print(f"Attempting to connect to iTag device (Node: {current_itag_node_id})...")
    
    # Connect to the iTag device as an LE client with security level 2
    if btfpy.Connect_node(current_itag_node_id, btfpy.CHANNEL_LE, 2) == 0: # Added security parameter
        print("Failed to connect to iTag. Ensure the device is advertising and in range.")
        btfpy.Close_all()
        return

    print("Connection attempt successful. Waiting for connection to stabilize...")
    time.sleep(1) # Give the connection a moment to stabilize

    # Removed btfpy.Is_connected check as it's not available.
    # We will proceed and handle errors if subsequent operations fail.

    print("Connected to iTag. Discovering services and characteristics...")
    # Find_ctics will return 0 if the device is not connected or discovery fails.
    if btfpy.Find_ctics(current_itag_node_id) == 0:
        print("Failed to discover characteristics. Device might have disconnected or is not responding.")
        btfpy.Disconnect_node(current_itag_node_id)
        btfpy.Close_all()
        return

    # Enable notifications for the button characteristic using its direct index
    print(f"Attempting to enable notifications for button characteristic at index: {BUTTON_CHARACTERISTIC_INDEX} (UUID FFE1)...")
    if btfpy.Notify_ctic(current_itag_node_id, BUTTON_CHARACTERISTIC_INDEX, btfpy.NOTIFY_ENABLE, my_notification_callback) == 0:
        print(f"Failed to enable notifications for button characteristic at index {BUTTON_CHARACTERISTIC_INDEX}.")
        print("This might happen if the characteristic does not truly support notifications or indications, or if the index is wrong.")
    else:
        print(f"Notifications enabled for button characteristic at index {BUTTON_CHARACTERISTIC_INDEX}.")

    # Immediate Alert characteristic is ready to be written to via its direct index
    print(f"Immediate Alert characteristic at index: {ALERT_LEVEL_CHARACTERISTIC_INDEX} (UUID 2A06) is available for writing.")

    print("\nMonitoring for button clicks and awaiting commands (Ctrl+C to stop)...")
    print("To send commands, type 'a0', 'a1', or 'a2' and press Enter.")
    print("  'a0': No Alert")
    print("  'a1': Mild Alert")
    print("  'a2': High Alert")
    print("\nNote: The iTag's physical response to alerts (e.g., beeping) depends on its firmware and battery level.")
    
    display_prompt() # Initial prompt

    try:
        # set_immediate_alert(current_itag_node_id, 1) # Set initial alert level to 1 (Mild Alert)
        while True:
            # Read notifications with a short timeout.
            # This ensures notifications are processed frequently.
            btfpy.Read_notify(100) # Read for 100ms
            
            if False: # Placeholder for any other periodic tasks
                # Check for user input. This will block until Enter is pressed.
                try:
                    # Use select.select to check for input without blocking indefinitely
                    # This ensures notifications are processed even if no command is typed.
                    rlist, _, _ = select.select([sys.stdin], [], [], 0.01) # 0.0 timeout for non-blocking
                    if rlist: # If there's input available
                        command = sys.stdin.readline().strip()
                        if command: # Only process if input is not empty
                            if command.startswith('a'):
                                try:
                                    level = int(command[1:])
                                    set_immediate_alert(current_itag_node_id, level)
                                except ValueError:
                                    clear_prompt_if_active() # Clear prompt for error message
                                    print("Invalid alert command format. Use a0, a1, or a2.")
                                    display_prompt() # Re-prompt
                            else:
                                clear_prompt_if_active() # Clear prompt for unknown command
                                print("Unknown command.")
                                display_prompt() # Re-prompt
                        else: # User just pressed Enter without typing a command
                            display_prompt() # Re-prompt
                except EOFError: # Handle Ctrl+D
                    print("\nEOF detected. Exiting.")
                    break # Exit the loop
                except Exception as e:
                    print(f"Input error: {e}")
                    break # Exit on other input errors
            
            time.sleep(0.01) # Small delay to reduce CPU usage
            
    except KeyboardInterrupt:
        print("\nCtrl+C detected. Disconnecting from iTag.")
    finally:
        # Always attempt to disable notifications and disconnect cleanly
        print("Disabling notifications for button characteristic...")
        # Only try to disable if the node ID is valid and connection was established
        if current_itag_node_id != -1:
            btfpy.Notify_ctic(current_itag_node_id, BUTTON_CHARACTERISTIC_INDEX, btfpy.NOTIFY_DISABLE, my_notification_callback)
        else:
            print("No active node to disable notifications for.")
        
        print("Disconnecting from iTag...")
        if current_itag_node_id != -1:
            btfpy.Disconnect_node(current_itag_node_id)
        else:
            print("No active node to disconnect from.")
        btfpy.Close_all()
        print("Disconnected and exited.")

if __name__ == "__main__":
    connect_and_monitor_itag()
