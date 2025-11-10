# AI Assistant Instructions for VC-Master Project

## Project Overview
This is an ESP32-C3 based project using the PlatformIO development environment with Arduino framework. The project appears to be focused on CAN bus communication, as indicated by the CAN library dependency.

## Project Structure
- `src/`: Main application code
- `platformio.ini`: Project configuration and dependencies
- `lib/`: Custom libraries
- `include/`: Header files
- `test/`: Test files

## Development Environment
- Platform: ESP32 (specifically esp32-c3-devkitc-02)
- Framework: Arduino
- Build System: PlatformIO
- Key Dependency: sandeepmistry/CAN library v0.3.1

## Build and Development Workflow
1. The project uses PlatformIO for build management. Common commands:
   ```
   pio run           # Build the project
   pio run --target upload  # Build and upload to the device
   pio test         # Run tests
   ```

2. Code organization follows standard Arduino patterns:
   - `setup()`: One-time initialization code
   - `loop()`: Main program loop that runs continuously
   - Custom functions should be declared at the top and defined at the bottom of the file

## Project-Specific Conventions
1. Hardware Configuration:
   - Target Board: esp32-c3-devkitm-1
   - Uses CAN bus for communication

2. Code Structure:
   - Use Allman style braces when generating code (each brace on its own line)
   - create new source and header files as needed to maintain modularity
   - Standard Arduino lifecycle methods (`setup()`/`loop()`) in the middle

## Areas for AI Focus
When assisting with this project:
1. Consider CAN bus timing and protocol requirements when modifying communication code
2. Respect ESP32-C3 hardware limitations and capabilities
3. Follow Arduino framework conventions for hardware initialization and loop structure
4. Use PlatformIO-specific commands for build and deployment tasks

## Key Integration Points
1. CAN Bus Interface: Uses esp32 twai driver for communication
2. Arduino Framework: Standard hardware abstraction layer
3. ESP32 SDK: Underlying platform-specific functionality

Remember to handle hardware-specific initialization and error conditions appropriately when modifying code.