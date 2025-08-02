import sqlite3
import struct
import os

def decode_sensor_data_blob(blob_data):
    """
    Decodes the binary blob of aggregated sensor data.

    The blob format is now:
    1. Total Number of Sensors (1 byte: uint8)
    2. For each SensorData entry:
        - MAC Address (6 bytes: raw bytes) - Fixed length
        - Temperature (8 bytes: double)
        - Humidity (8 bytes: double)
        - RSSI (1 byte: int8)

    Args:
        blob_data (bytes): The binary data read from the 'DATA' column.

    Returns:
        list: A list of dictionaries, each representing a decoded SensorData entry.
              Returns an empty list if decoding fails or data is malformed.
    """
    decoded_entries = []
    offset = 0

    try:
        if not blob_data:
            print("Warning: Received empty blob data.")
            return []

        # 1. Total Number of Sensors (1 byte)
        if len(blob_data) < 1:
            print("Error: Blob too short for number of sensors.")
            return []
        num_sensors = struct.unpack('<B', blob_data[offset:offset+1])[0]
        offset += 1

        for _ in range(num_sensors):
            entry = {}

            # MAC Address (fixed 6 bytes)
            if len(blob_data) < offset + 6:
                print("Error: Blob too short for MAC address (expected 6 bytes).")
                return []
            mac_bytes = blob_data[offset:offset+6]
            entry['mac_address'] = ":".join(f"{b:02X}" for b in mac_bytes)
            offset += 6

            # Predefined Name and Decoded Device Name are no longer in the blob.
            # We can set them to N/A or derive them if a mapping is available elsewhere.
            entry['predefined_name'] = "N/A (Not stored in blob)"
            entry['decoded_device_name'] = "N/A (Not stored in blob)"

            # Temperature (double)
            if len(blob_data) < offset + 8:
                print("Error: Blob too short for temperature (8 bytes).")
                return []
            entry['temperature'] = struct.unpack('<d', blob_data[offset:offset+8])[0]
            offset += 8

            # Humidity (double)
            if len(blob_data) < offset + 8:
                print("Error: Blob too short for humidity (8 bytes).")
                return []
            entry['humidity'] = struct.unpack('<d', blob_data[offset:offset+8])[0]
            offset += 8

            # RSSI (int8)
            if len(blob_data) < offset + 1:
                print("Error: Blob too short for RSSI (1 byte).")
                return []
            entry['rssi'] = struct.unpack('<b', blob_data[offset:offset+1])[0]
            offset += 1

            decoded_entries.append(entry)

    except struct.error as e:
        print(f"Error decoding binary data: {e}")
        return []
    except IndexError as e:
        print(f"Error accessing blob data (IndexError): {e}")
        return []
    except Exception as e:
        print(f"An unexpected error occurred during decoding: {e}")
        return []

    return decoded_entries

def main():
    # Determine the database path.
    # Assuming the script is run from the project root (~/dev/tp357)
    # and the database is in build/TP357_ble_gateway/
    script_dir = os.path.dirname(__file__)
    db_path = os.path.join(script_dir, '.', '..', 'sensor_readings.db')

    if not os.path.exists(db_path):
        print(f"Error: Database file not found at {db_path}")
        print("Please ensure the C++ application has run and created the database.")
        print("You might need to adjust 'db_path' in this script if your setup is different.")
        return

    conn = None
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()

        # Query the latest 20 aggregated entries
        cursor.execute("SELECT TIMESTAMP, DATA FROM sensor_readings_aggregated ORDER BY ID DESC LIMIT 20;")
        rows = cursor.fetchall()

        if not rows:
            print("No aggregated sensor data found in the database.")
            return

        print(f"Found {len(rows)} latest aggregated entries:\n")

        for i, row in enumerate(rows):
            timestamp = row[0]
            blob_data = row[1]

            print(f"--- Entry {i+1} (Timestamp: {timestamp}) ---")
            decoded_sensors = decode_sensor_data_blob(blob_data)

            if decoded_sensors:
                for j, sensor in enumerate(decoded_sensors):
                    print(f"  Sensor {j+1}:")
                    print(f"    MAC Address: {sensor.get('mac_address', 'N/A')}")
                    print(f"    Predefined Name: {sensor.get('predefined_name', 'N/A')}") # Will be "N/A (Not stored in blob)"
                    print(f"    Decoded Device Name: {sensor.get('decoded_device_name', 'N/A')}") # Will be "N/A (Not stored in blob)"
                    print(f"    Temperature: {sensor.get('temperature', 'N/A'):.1f} C")
                    print(f"    Humidity: {sensor.get('humidity', 'N/A'):.1f} %")
                    print(f"    RSSI: {sensor.get('rssi', 'N/A')} dBm")
            else:
                print("  Failed to decode sensor data for this entry.")
            print("-" * 40) # Separator

    except sqlite3.Error as e:
        print(f"SQLite error: {e}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
    finally:
        if conn:
            conn.close()
            print("Database connection closed.")

if __name__ == "__main__":
    main()
