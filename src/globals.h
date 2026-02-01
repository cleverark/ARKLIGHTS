#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <FastLED.h>
#include <WebServer.h>
#include <MPU6050.h>
#include <Preferences.h>
#include "config.h"

// Forward declarations
struct MotionData;
struct EffectTiming;
struct CalibrationData;

// LED Configuration (can be changed via web UI)
extern uint8_t headlightLedCount;
extern uint8_t taillightLedCount;
extern uint8_t headlightLedType;
extern uint8_t taillightLedType;
extern uint8_t headlightColorOrder;
extern uint8_t taillightColorOrder;

// LED strips (dynamic size)
extern CRGB* headlight;
extern CRGB* taillight;

// System state
extern uint8_t globalBrightness;
extern uint8_t currentPreset;
extern uint8_t headlightEffect;
extern uint8_t taillightEffect;
extern CRGB headlightColor;
extern CRGB taillightColor;
extern uint8_t effectSpeed;

// Motion control
extern MPU6050 mpu;
extern bool motionEnabled;
extern bool blinkerEnabled;
extern bool parkModeEnabled;
extern bool impactDetectionEnabled;
extern bool directionBasedLighting;
extern bool brakingEnabled;

// NVS for persistent settings storage
extern Preferences nvs;
extern const char* NVS_NAMESPACE;
extern bool nvsMigrationPending;

// Web Server
extern WebServer server;

// BLE Server
extern BLEServer* pBLEServer;
extern BLECharacteristic* pCharacteristic;
extern bool bluetoothEnabled;
extern String bluetoothDeviceName;

// Effect state
extern unsigned long lastUpdate;
extern uint16_t effectStep;

// Startup sequence state
extern bool startupActive;
extern unsigned long startupStartTime;
extern uint16_t startupStep;

// WiFi AP Configuration
extern String apName;
extern String apPassword;

#endif // GLOBALS_H

