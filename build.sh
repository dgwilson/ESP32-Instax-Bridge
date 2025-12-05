#!/bin/bash
# Simple ESP-IDF build script that bypasses PlatformIO

# Check if ESP-IDF is installed
if [ -z "$IDF_PATH" ]; then
    echo "ESP-IDF not found. Installing via PlatformIO's ESP-IDF..."
    export IDF_PATH="$HOME/.platformio/packages/framework-espidf"
fi

if [ ! -d "$IDF_PATH" ]; then
    echo "Error: ESP-IDF not found at $IDF_PATH"
    echo "Please install ESP-IDF or run PlatformIO once to download it"
    exit 1
fi

# Set up ESP-IDF environment
export PATH="$HOME/.platformio/packages/toolchain-xtensa-esp32/bin:$PATH"

# Build
echo "Building with ESP-IDF..."
idf.py build

# Flash (optional)
if [ "$1" == "flash" ]; then
    idf.py flash
fi

# Monitor (optional)
if [ "$1" == "monitor" ]; then
    idf.py monitor
fi
