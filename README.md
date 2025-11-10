# VC-Master

ESP32-based CAN bus monitor with web interface and configuration portal.

## Features

- CAN bus monitoring over WiFi
- Real-time web interface showing:
  - Recent CAN messages
  - Latest state per CAN ID with change highlighting
- Configuration portal for WiFi setup (SoftAP mode)
  - Unique SSID based on device MAC address
  - Captive portal for easy configuration
  - Password visibility toggle for easier entry
- Automatic connection to configured WiFi network

## Hardware Requirements

- ESP32-C3 board (esp32-c3-devkitm-1)
- CAN transceiver connected to GPIO3 (TX) and GPIO4 (RX)
- Button connected to GPIO9 for configuration mode
- LED on GPIO8 for status indication

## Building and Flashing

This project uses PlatformIO for development. To build and flash:

1. Install [PlatformIO](https://platformio.org/)
2. Clone this repository
3. Build the project:
   ```bash
   platformio run
   ```
4. Upload to your device:
   ```bash
   platformio run --target upload
   ```

## Initial Setup

1. Power on the device while holding the GPIO9 button
2. Connect to the WiFi network named "RCLS-XXXXXX" (password: "configure")
3. Your device should automatically open the configuration portal
4. Enter your WiFi credentials and save
5. Power cycle the device - it will connect to your configured network

## Usage

1. After configuration, the device will connect to your WiFi network
2. Access the web interface at the device's IP address
3. View real-time CAN bus traffic:
   - Recent messages table shows latest messages received
   - Latest state table shows current value per CAN ID
   - Changed bytes are highlighted in yellow
   - Message age is color-coded (green/orange/red)

## Project Structure

- `src/`
  - `main.cpp` - Main application code
  - `softap_config.cpp` - WiFi configuration portal
  - `web_interface.cpp` - Web UI and message display
- `include/`
  - `can_messages.h` - CAN message structures
  - `softap_config.h` - Configuration portal headers
  - `web_interface.h` - Web interface headers

## Contributing

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request