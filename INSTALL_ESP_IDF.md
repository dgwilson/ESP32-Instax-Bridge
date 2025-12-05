# Installing ESP-IDF on macOS

This guide will help you install ESP-IDF natively on your Mac and build the **ESP32 Instax Printer Emulator** project.

**Note:** This project emulates an Instax printer - it doesn't connect TO printers, it IS the printer (virtually). Your photo apps connect to the ESP32 thinking it's a real Instax printer.

## Prerequisites

First, install the required tools using Homebrew:

```bash
# Install Homebrew if you don't have it
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install required packages
brew install cmake ninja dfu-util python3
```

## Step 1: Install ESP-IDF

```bash
# Create a directory for ESP-IDF
mkdir -p ~/esp
cd ~/esp

# Clone ESP-IDF (this will take a few minutes)
git clone --recursive https://github.com/espressif/esp-idf.git

# Go into the ESP-IDF directory
cd esp-idf

# Checkout a stable version (v5.1.2 is stable and well-tested)
git checkout v5.1.2
git submodule update --init --recursive

# Install ESP-IDF tools (this will take 5-10 minutes)
./install.sh esp32

# This downloads the Xtensa toolchain and other tools
```

## Step 2: Set Up Environment

You need to source the ESP-IDF environment every time you open a new terminal:

```bash
# Source the environment (do this in every new terminal session)
. ~/esp/esp-idf/export.sh
```

**Pro tip:** Add an alias to your `~/.zshrc` or `~/.bash_profile`:

```bash
# Add this line to your shell config file
alias get_idf='. ~/esp/esp-idf/export.sh'

# Then reload your shell
source ~/.zshrc  # or ~/.bash_profile
```

Now you can just type `get_idf` in any terminal to set up the environment.

## Step 3: Build the Project

```bash
# Navigate to the project
cd /Users/dgwilson/Projects/ESP32-Instax-Bridge

# Make sure ESP-IDF environment is loaded
. ~/esp/esp-idf/export.sh

# Configure the project (first time only)
idf.py menuconfig
# Press 'Q' to quit and save if you don't need to change anything

# Build the project
idf.py build
```

The build will take 2-5 minutes the first time as it compiles all components.

## Step 4: Flash to ESP32

Connect your ESP32 to your Mac via USB, then:

```bash
# Find your ESP32's serial port
ls /dev/cu.usb*

# Flash the firmware (replace with your actual port)
idf.py -p /dev/cu.usbserial-* flash

# Or let idf.py detect the port automatically
idf.py flash
```

## Step 5: Monitor Serial Output

```bash
# Open the serial monitor (115200 baud)
idf.py monitor

# Press Ctrl+] to exit the monitor
```

## Quick Reference

### Common Commands

```bash
# Clean build
idf.py fullclean

# Build only (no flash)
idf.py build

# Flash only (no build)
idf.py flash

# Build and flash
idf.py build flash

# Build, flash, and monitor
idf.py build flash monitor

# Just monitor
idf.py monitor

# Erase flash completely
idf.py erase-flash
```

### Project Configuration

```bash
# Open menuconfig (text-based configuration)
idf.py menuconfig

# Set WiFi credentials (for testing)
idf.py menuconfig
# Navigate to: Component config → WiFi
```

## Using the Console

Once flashed, the ESP32 will start and show:

```
ESP32 Instax Bridge Console
Type 'help' for available commands.

instax>
```

### Console Commands

```bash
# Configure WiFi
wifi_set <ssid> <password>
wifi_connect
wifi_status

# Scan for Instax printers
ble_scan
ble_devices
ble_status

# Manage files
files

# System
reboot
help
```

## Web Interface

1. Connect ESP32 to WiFi using console commands
2. Get the IP address: `wifi_status`
3. Open browser to `http://<ip-address>/`
4. Upload JPEG files and print to Instax printer

## Troubleshooting

### "Permission denied" on serial port

```bash
# Add your user to the dialout group (may need to log out/in)
sudo dseditgroup -o edit -a $USER -t user wheel

# Or use sudo
sudo idf.py flash monitor
```

### "Port /dev/cu.usbserial-* not found"

- Check USB cable is connected
- Try a different USB port
- Install USB-to-Serial drivers if needed (CP210x or CH340)
- List all ports: `ls /dev/cu.*`

### Build fails with "command not found"

```bash
# Make sure ESP-IDF environment is loaded
. ~/esp/esp-idf/export.sh

# Or use the alias if you set it up
get_idf
```

### "CMake Error" or "Ninja not found"

```bash
# Reinstall tools
cd ~/esp/esp-idf
./install.sh esp32
```

### Clean build if things get weird

```bash
# Remove build artifacts
idf.py fullclean

# Rebuild from scratch
idf.py build
```

## VS Code Integration (Optional)

If you want to use VS Code with ESP-IDF:

1. Install the "ESP-IDF" extension (NOT PlatformIO)
2. Configure the extension to use your ESP-IDF installation
3. Use Command Palette (Cmd+Shift+P) → "ESP-IDF: Build your project"

**But honestly**, using the command line with `idf.py` is simpler and more reliable.

## Estimated Time

- **Install ESP-IDF**: 10-15 minutes (one-time setup)
- **First build**: 2-5 minutes
- **Subsequent builds**: 10-30 seconds (incremental)
- **Flash**: 10-20 seconds

## Why This Works (vs PlatformIO)

Native ESP-IDF:
- ✅ Official Espressif toolchain
- ✅ No CMake quote escaping bugs
- ✅ Better documentation
- ✅ Faster builds
- ✅ Direct access to all ESP-IDF features

PlatformIO:
- ❌ Wrapper around ESP-IDF with bugs
- ❌ CMake syntax errors
- ❌ Harder to debug
- ✅ Good for Arduino projects

## Next Steps

Once you have it built and flashed:

1. Configure WiFi via serial console
2. Access web interface
3. Scan for Instax printer
4. Upload a JPEG file
5. Print!

---

**Need help?** Check the official ESP-IDF documentation:
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

**Quick start video:**
- https://www.youtube.com/watch?v=byVPAfodTyY
