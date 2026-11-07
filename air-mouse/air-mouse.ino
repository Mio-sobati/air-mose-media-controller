/*
 * Project: ESP32-C3 Air Mouse & Media Controller
 * Hardware: ESP32-C3, GY-25 Gyroscope, Rotary Encoder, Push Buttons, WS2812 NeoPixel
 * Libraries Required: 
 *   - ESP32-BLE-Mouse (T-vK)
 *   - ESP32-BLE-Keyboard (T-vK)
 *   - Adafruit NeoPixel
 * 
 * Description: 
 * A two-mode Bluetooth HID controller. 
 * Mode 1 (Green): Air Mouse with adjustable sensitivity via Rotary Encoder.
 * Mode 2 (Purple): Media Controller (Volume, Next/Prev, Play/Pause).
 * Uses Hardware Serial for GY-25 module and an analog voltage divider for buttons.
 */

#include <Preferences.h>
#include <HardwareSerial.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_mac.h>
#include <Adafruit_NeoPixel.h>
#include <BleMouse.h>
#include <BleKeyboard.h>

// --- Configuration ---
#define RX_PIN          2
#define TX_PIN          3
#define BUTTON_ADC_PIN  0 
#define ENCODER_CLK     6
#define ENCODER_DT      7
#define ENCODER_SW      10
#define MODE_SW_PIN     4
#define NEOPIXEL_PIN    5
#define NUM_PIXELS      1

enum SystemMode {
  MODE_MOUSE = 0,
  MODE_MEDIA = 1
};

SystemMode currentMode = MODE_MOUSE;
Preferences preferences;

// BLE Instances
BleMouse bleMouse("Air Mouse ESP", "ESP32", 100);
BleKeyboard bleKeyboard("Air Media ESP", "ESP32", 100);

// Hardware Instances
Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
HardwareSerial sensorSerial(1);

// --- State Variables ---
unsigned char re_buf[8];
unsigned char counter = 0;
float yaw = 0, pitch = 0, lastYaw = 0, lastPitch = 0;
float smoothX = 0, smoothY = 0;
bool firstRead = true;

// Sensitivity & Smoothing
float sensitivity = 15.0;       
const float MIN_SENS = 10.0;    
const float MAX_SENS = 30.0;    
const float ACCEL_FACTOR = 2.0; 
const float SMOOTHING = 0.40; 
bool isSensitivityMode = false; 

// Button & Encoder States
int lastClkState;
bool lastEncoderBtnState = HIGH;
bool lastLeftState = false;
bool lastRightState = false;
unsigned long lastActionTime = 0;
const int ACTION_DELAY = 250; 
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// --- Helper Functions ---
void updateSensitivityColor() {
  float ratio = (sensitivity - MIN_SENS) / (MAX_SENS - MIN_SENS);
  uint8_t red   = (uint8_t)(ratio * 255);
  uint8_t blue  = (uint8_t)((1.0 - ratio) * 255);
  pixels.setPixelColor(0, pixels.Color(red, 0, blue));
  pixels.show();
}

void setup() {
  Serial.begin(115200);

  // Load saved mode
  preferences.begin("system_cfg", false);
  currentMode = (SystemMode)preferences.getUInt("mode", MODE_MOUSE);

  // Initialize NeoPixel
  pixels.begin();
  pixels.setBrightness(40);
  pixels.clear();
  pixels.show();

  analogReadResolution(12);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(MODE_SW_PIN, INPUT_PULLUP);
  
  lastClkState = digitalRead(ENCODER_CLK);

  // Initialize GY-25 Gyro Sensor
  sensorSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  sensorSerial.write(0xA5);
  sensorSerial.write(0x52); // Request continuous data

  // Identity fix for Android (Distinct MAC address for Keyboard mode)
  if (currentMode == MODE_MEDIA) {
    uint8_t newMac[6];
    esp_read_mac(newMac, ESP_MAC_BT);
    newMac[5] += 1; 
    esp_base_mac_addr_set(newMac);
  }

  // Start BLE Service
  if (currentMode == MODE_MOUSE) {
    bleMouse.begin();
  } else {
    bleKeyboard.begin();
  }

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
}

void loop() {
  // 1. Mode Switch Logic (Requires Restart to switch BLE profile)
  if (digitalRead(MODE_SW_PIN) == LOW) {
    delay(50);
    if (digitalRead(MODE_SW_PIN) == LOW) {
      SystemMode nextMode = (currentMode == MODE_MOUSE) ? MODE_MEDIA : MODE_MOUSE;
      preferences.putUInt("mode", (uint32_t)nextMode);
      preferences.end();
      pixels.clear();
      pixels.show();
      delay(100);
      ESP.restart(); 
    }
  }

  bool isConnected = (currentMode == MODE_MOUSE) ? bleMouse.isConnected() : bleKeyboard.isConnected();

  if (isConnected) {

    // ==========================================
    //            MODE: MOUSE
    // ==========================================
    if (currentMode == MODE_MOUSE) {
      
      // Rotary Encoder Push Button (Toggle Sensitivity Mode)
      bool encoderBtnState = digitalRead(ENCODER_SW);
      if (encoderBtnState == LOW && lastEncoderBtnState == HIGH) {
        isSensitivityMode = !isSensitivityMode;
        if (isSensitivityMode) updateSensitivityColor();
      }
      lastEncoderBtnState = encoderBtnState;

      // Rotary Encoder Rotation (Adjust Sensitivity)
      int currentClkState = digitalRead(ENCODER_CLK);
      if (currentClkState != lastClkState && currentClkState == LOW) {
        bool isCW = (digitalRead(ENCODER_DT) == currentClkState);
        if (isSensitivityMode) {
          if (isCW) sensitivity += 1.5;
          else sensitivity -= 1.5;
          sensitivity = constrain(sensitivity, MIN_SENS, MAX_SENS);
          updateSensitivityColor();
        }
      }
      lastClkState = currentClkState;

      if (!isSensitivityMode) {
        pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Green for Mouse Mode
        pixels.show();
      }

      // Read GY-25 Gyroscope Data
      while (sensorSerial.available()) {
        re_buf[counter] = (unsigned char)sensorSerial.read();
        if (counter == 0 && re_buf[0] != 0xAA) return;
        counter++;

        if (counter == 8) {
          counter = 0;
          if (re_buf[7] == 0x55) {
            yaw   = (int16_t)(re_buf[1] << 8 | re_buf[2]) / 100.0;
            pitch = (int16_t)(re_buf[3] << 8 | re_buf[4]) / 100.0;

            if (firstRead) {
              lastYaw = yaw;
              lastPitch = pitch;
              firstRead = false;
            }

            float deltaYaw   = yaw - lastYaw;
            float deltaPitch = pitch - lastPitch;

            // Handle wrap-around
            if (deltaYaw > 180)   deltaYaw -= 360;
            if (deltaYaw < -180)  deltaYaw += 360;
            if (deltaPitch > 180)  deltaPitch -= 360;
            if (deltaPitch < -180) deltaPitch += 360;

            float magX = abs(deltaYaw);
            float magY = abs(deltaPitch);

            // Apply sensitivity and acceleration
            float targetX = -deltaYaw * sensitivity * (1.0 + magX * ACCEL_FACTOR);
            float targetY = -deltaPitch * sensitivity * (1.0 + magY * ACCEL_FACTOR);

            // Apply smoothing filter
            smoothX = (targetX * SMOOTHING) + (smoothX * (1.0 - SMOOTHING));
            smoothY = (targetY * SMOOTHING) + (smoothY * (1.0 - SMOOTHING));

            int8_t moveX = (int8_t)constrain(smoothX, -127, 127);
            int8_t moveY = (int8_t)constrain(smoothY, -127, 127);

            if (moveX != 0 || moveY != 0) {
              bleMouse.move(moveX, moveY, 0);
            }

            lastYaw = yaw;
            lastPitch = pitch;
          }
        }
      }

      // Read Analog Buttons (Resistor Network)
      int adcVal = analogRead(BUTTON_ADC_PIN);
      bool currentLeft  = (adcVal > 3700);
      bool currentRight = (adcVal >= 3200 && adcVal <= 3699);
      bool currentUp    = (adcVal >= 2300 && adcVal <= 3199);
      bool currentDown  = (adcVal >= 1000 && adcVal <= 2299);

      if (currentLeft != lastLeftState) {
        if (currentLeft) bleMouse.press(MOUSE_LEFT);
        else bleMouse.release(MOUSE_LEFT);
        lastLeftState = currentLeft;
      }

      if (currentRight != lastRightState) {
        if (currentRight) bleMouse.press(MOUSE_RIGHT);
        else bleMouse.release(MOUSE_RIGHT);
        lastRightState = currentRight;
      }

      if (millis() - lastActionTime > 150) {
        if (currentUp) {
          bleMouse.move(0, 0, 1); // Scroll Up
          lastActionTime = millis();
        } else if (currentDown) {
          bleMouse.move(0, 0, (int8_t)-1); // Scroll Down
          lastActionTime = millis();
        }
      }
    } 

    // ==========================================
    //            MODE: MEDIA
    // ==========================================
    else if (currentMode == MODE_MEDIA) {
      
      pixels.setPixelColor(0, pixels.Color(180, 0, 255)); // Purple for Media Mode
      pixels.show();

      // Rotary Encoder Rotation (Volume Control)
      int currentClkState = digitalRead(ENCODER_CLK);
      if (currentClkState != lastClkState && currentClkState == LOW) {
        bool isCW = (digitalRead(ENCODER_DT) == currentClkState);
        if (isCW) {
          bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
        } else {
          bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
        }
      }
      lastClkState = currentClkState;

      // Read Analog Buttons (Resistor Network)
      int adcVal = analogRead(BUTTON_ADC_PIN);
      bool currentLeft  = (adcVal > 3700);
      bool currentRight = (adcVal >= 3200 && adcVal <= 3699);
      bool currentUp    = (adcVal >= 2300 && adcVal <= 3199);
      bool currentDown  = (adcVal >= 1000 && adcVal <= 2299);

      if (millis() - lastActionTime > ACTION_DELAY) {
        if (currentLeft) {
          bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
          lastActionTime = millis();
        } else if (currentRight) {
          bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
          lastActionTime = millis();
        } else if (currentUp || currentDown) {
          bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
          lastActionTime = millis();
        }
      }
    }

    delay(4);

  } else {
    // Waiting for BLE connection: Blink LED
    firstRead = true;
    if (millis() - lastBlinkTime > 500) {
      lastBlinkTime = millis();
      blinkState = !blinkState;
      
      if (blinkState) {
        if (currentMode == MODE_MOUSE) pixels.setPixelColor(0, pixels.Color(0, 255, 0));
        else pixels.setPixelColor(0, pixels.Color(180, 0, 255));
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
      pixels.show();
    }
  }
}