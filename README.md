# ESP32-C3 Air Mouse & Media Controller

A comprehensive, hardware-based Bluetooth Air Mouse and Media Controller. Built around the **ESP32-C3** RISC-V microcontroller, it utilizes a **GY-25 Gyroscope** for motion tracking, a rotary encoder for dynamic adjustments, and an analog button array for inputs.

This device functions as a standard Bluetooth HID (Human Interface Device). It does not require any custom applications or drivers on the host device—it works out of the box with Android, iOS, Windows, Mac, and Smart TVs.

## 🚀 Key Features & Logic

- **Mode 1 (Green LED): Air Mouse**
  - **Motion Tracking:** Reads raw Yaw and Pitch angles from the GY-25 sensor via Hardware Serial.
  - **Smoothing & Acceleration:** Applies a software-based exponential smoothing filter and dynamic acceleration factor. The faster you move your hand, the further the cursor travels, mimicking high-end PC mice.
  - **Click & Scroll:** Left/Right click and Scroll Up/Down are mapped to the analog push buttons.
  - **On-the-fly Sensitivity:** Clicking the rotary encoder toggles sensitivity mode. Rotating it adjusts the base cursor speed in real-time.
  
- **Mode 2 (Purple LED): Media Remote**
  - **Volume Control:** The rotary encoder maps directly to Volume Up and Volume Down.
  - **Media Playback:** Buttons control Play/Pause, Next Track, and Previous Track.

- **The "Dual-Profile" Fix:** 
  Connecting a single Bluetooth device attempting to present itself as both a Mouse and a Media Keyboard simultaneously can cause identity caching issues. This project solves this by slightly modifying the ESP32's internal MAC address when entering Media Mode. The host device perceives two entirely separate physical devices (`Air Mouse ESP` and `Air Media ESP`), ensuring stable connections.

## 🐛 Known Bugs & Issues

- **Android Media Controller Issue:** Currently, the **Media Controller mode** (Purple LED) is **not working on Android devices**. While it connects, the media commands (Volume, Play/Pause, Track skipping) may not register correctly on the Android OS. Mouse mode works perfectly on Android, and Media mode functions normally on Windows/iOS. We are actively investigating a fix for the Android HID profile compatibility.

## 🔌 Hardware Connections (ESP32-C3 Wiring)

| Component | Component Pin | ESP32-C3 Pin | Note / Function |
| :--- | :--- | :--- | :--- |
| **GY-25 Gyroscope** | `TX` | `GPIO 2` | Serial RX for ESP32 |
| | `RX` | `GPIO 3` | Serial TX for ESP32 |
| | `VCC` / `GND` | `3.3V` / `GND` | Power (GY-25 requires UART 115200 baud) |
| **Rotary Encoder** | `CLK` | `GPIO 6` | Sensitivity / Volume adjustment |
| | `DT` | `GPIO 7` | Direction tracking |
| | `SW` | `GPIO 10` | Push button (Sensitivity toggle) |
| | `+` / `GND` | `3.3V` / `GND` | Power |
| **Mode Switch** | `Button Pin` | `GPIO 4` | Switches between Mouse & Media modes (Pull to GND) |
| **Analog Buttons** | `Output` | `GPIO 0` | Resistor ladder voltage divider (ADC) |
| **WS2812 NeoPixel** | `DIN` | `GPIO 5` | Status Indicator (Green/Purple) |

*(Note: Avoid using GPIO 8 and 9 for critical inputs on the ESP32-C3 as they are strapping pins that can affect the boot process).*

## 🎛️ How the Analog Button Array Works
To save GPIO pins on the ESP32-C3, the four main action buttons (Left, Right, Up, Down) are wired into a **Resistor Ladder (Voltage Divider)** connected to a single Analog-to-Digital Converter (ADC) pin (`GPIO 0`). 

When a button is pressed, it creates a specific voltage drop. The ESP32 reads this as a value between 0 and 4095 (12-bit resolution). Depending on your exact resistor values (e.g., 1kΩ, 2kΩ, 4.7kΩ, 10kΩ), you may need to calibrate the thresholds in the code.

Current ADC thresholds in `loop()`:
- Left Click / Prev: `> 3700`
- Right Click / Next: `3200 - 3699`
- Scroll Up / Play: `2300 - 3199`
- Scroll Down: `1000 - 2299`

## 📚 Required Libraries

Install the following via the Arduino IDE Library Manager:
1. **ESP32 BLE Mouse** by T-vK
2. **ESP32 BLE Keyboard** by T-vK
3. **Adafruit NeoPixel** by Adafruit

*(Ensure your ESP32 board package is updated in the Boards Manager to avoid RISC-V compilation errors).*

## ⚙️ Setup & Usage

1. **Flash the Code:** Upload the provided `.ino` file to your ESP32-C3.
2. **Mouse Mode:** Power on the device. The NeoPixel will blink Green indicating it's waiting for a connection. Pair your host device to **Air Mouse ESP**.
3. **Switch Mode:** Press the Mode switch button (GPIO 4). The ESP32 saves the state in its non-volatile memory (NVS) and restarts. The LED will blink Purple.
4. **Media Mode:** Pair your host device to the new Bluetooth entry named **Air Media ESP**.
5. **Ready:** You can now switch freely between Mouse and Media remote modes using the hardware button. Reconnections are fast and automatic.
