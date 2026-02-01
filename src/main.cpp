#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Preferences.h>  // NVS for persistent settings storage
#include <Wire.h>
#include <MPU6050.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include "embedded_ui.h"  // Auto-generated embedded UI files (gzipped)

// CRGBW struct for RGBW LED support
struct CRGBW {
    union {
        uint32_t color32; // Access as a 32-bit value (0xWWRRGGBB)
        struct {
            uint8_t b;
            uint8_t g;
            uint8_t r;
            uint8_t w;
        };
        uint8_t raw[4];   // Access as an array in the order B, G, R, W
    };

    // Default constructor
    inline CRGBW() __attribute__((always_inline)) = default;

    // Constructor from a 32-bit color (0xWWRRGGBB)
    constexpr CRGBW(uint32_t color) __attribute__((always_inline)) : color32(color) {}

    // Constructor with r, g, b, w values
    constexpr CRGBW(uint8_t red, uint8_t green, uint8_t blue, uint8_t white = 0) __attribute__((always_inline)) : b(blue), g(green), r(red), w(white) {}

    // Constructor from CRGB
    constexpr CRGBW(CRGB rgb) __attribute__((always_inline)) : b(rgb.b), g(rgb.g), r(rgb.r), w(0) {}

    // Assignment from CRGB
    inline CRGBW& operator=(const CRGB& rgb) __attribute__((always_inline)) { b = rgb.b; g = rgb.g; r = rgb.r; w = 0; return *this; }

    // Conversion operator to uint32_t
    inline operator uint32_t() const __attribute__((always_inline)) {
      return color32;
    }
};

// ArkLights PEV Lighting System - Modular Version
// This is a clean, focused implementation for PEV devices

// Debug flag - set to false to disable all debug Serial output for release builds
#define DEBUG_ENABLED false

// Configuration for XIAO ESP32S3
#define HEADLIGHT_PIN 2
#define TAILLIGHT_PIN 3
#define HEADLIGHT_CLOCK_PIN 5  // For APA102/LPD8806
#define TAILLIGHT_CLOCK_PIN 4  // For APA102/LPD8806
#define DEFAULT_BRIGHTNESS 128

// LED Configuration (can be changed via web UI)
uint8_t headlightLedCount = 11;
uint8_t taillightLedCount = 11;
uint8_t headlightLedType = 0;  // 0=SK6812 RGBW, 1=SK6812 RGB, 2=WS2812B, 3=APA102, 4=LPD8806
uint8_t taillightLedType = 0;
uint8_t headlightColorOrder = 1;  // 0=RGB, 1=GRB, 2=BGR - Default to GRB for SK6812 RGBW
uint8_t taillightColorOrder = 1;  // 0=RGB, 1=GRB, 2=BGR - Default to GRB for SK6812 RGBW

// LED Type Configuration
// Can be overridden via build flags: -D LED_TYPE=SK6812 -D LED_COLOR_ORDER=GRB
#ifndef LED_TYPE
#define LED_TYPE SK6812  // Default to SK6812 (RGBW capable)
#endif
#ifndef LED_COLOR_ORDER
#define LED_COLOR_ORDER GRB  // Default color order
#endif

#define LED_TYPE_HEADLIGHT LED_TYPE
#define LED_TYPE_TAILLIGHT LED_TYPE
#define LED_COLOR_ORDER_HEADLIGHT LED_COLOR_ORDER
#define LED_COLOR_ORDER_TAILLIGHT LED_COLOR_ORDER

// WiFi AP Configuration
const char* AP_SSID = "ARKLIGHTS-AP";
const char* AP_PASSWORD = "float420";
const int AP_CHANNEL = 1;
const int MAX_CONNECTIONS = 4;

// ESPNow Configuration
bool enableESPNow = true;
bool useESPNowSync = true;
uint8_t espNowChannel = 1;
uint8_t espNowState = 0; // 0=uninit, 1=on, 2=error
int espNowLastError = 0;

// Group Ride Management
bool isGroupMaster = false;
bool allowGroupJoin = false;
String groupCode = ""; // 6-digit group code for authentication
String deviceName = ""; // User-defined device name
uint32_t masterHeartbeat = 0; // Last master heartbeat
const uint32_t MASTER_TIMEOUT = 5000; // 5 seconds without master = become master
const uint32_t HEARTBEAT_INTERVAL = 1000; // Send heartbeat every 1 second
bool autoJoinOnHeartbeat = false; // Auto-join first group heartbeat
bool joinInProgress = false; // Waiting to join a group
uint32_t lastJoinRequest = 0;
const uint32_t JOIN_RETRY_INTERVAL = 1000; // Retry join request every 1 second

// ESPNow Broadcast Address
uint8_t espNowBroadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESPNow Data Structures
struct __attribute__((packed)) ESPNowLEDData {
    uint8_t magic = 'A'; // 'A' for ArkLights
    uint8_t packetNum = 0;
    uint8_t totalPackets = 1;
    uint8_t brightness;
    uint8_t headlightEffect;
    uint8_t taillightEffect;
    uint8_t effectSpeed;
    uint8_t headlightColor[3]; // R, G, B
    uint8_t taillightColor[3]; // R, G, B
    uint8_t headlightBackgroundEnabled;
    uint8_t taillightBackgroundEnabled;
    uint8_t headlightBackgroundColor[3]; // R, G, B
    uint8_t taillightBackgroundColor[3]; // R, G, B
    uint8_t preset;
    uint32_t syncTimestamp;    // Timestamp for timing coordination
    uint16_t masterStep;       // Master step counter for sync
    uint8_t stripLength;       // Length of master strip for speed normalization
    uint8_t checksum;
};

// Group Management Data Structure
struct __attribute__((packed)) ESPNowGroupData {
    uint8_t magic = 'G'; // 'G' for Group
    uint8_t messageType; // 0=heartbeat, 1=join_request, 2=join_accept, 3=join_reject, 4=master_election
    char groupCode[7];   // 6-digit group code + null terminator
    char deviceName[21]; // Device name + null terminator
    uint8_t macAddress[6]; // Device MAC address
    uint32_t timestamp;
    uint8_t checksum;
};

struct ESPNowPeer {
    uint8_t mac[6];
    uint8_t channel;
    bool isActive;
    uint32_t lastSeen;
};

// Forward declarations
bool deviceConnected = false;
bool oldDeviceConnected = false;
void processBLEHTTPRequest(String request);
void appendBleRequestChunk(const String& chunk);
bool consumeBleRequest(String& requestOut);
String getStatusJSON();
void startOTAUpdate(String url);
struct BleFrame;

// BLE framed protocol helpers
uint16_t crc16Ccitt(const uint8_t* data, size_t length);
bool tryExtractBleFrame(std::string& buffer, BleFrame& frame);
void sendBleFrame(uint8_t type, uint8_t seq, uint8_t flags, const uint8_t* payload, uint16_t length);
void sendBleAck(uint8_t seq);
void sendBleError(uint8_t seq, const String& message);
String getOtaStatusJSON();

// BLE request buffering/queueing
String bleRequestBuffer = "";
int bleRequestBodyLength = -1;
String blePendingJson = "";
bool blePendingApply = false;
portMUX_TYPE blePendingMutex = portMUX_INITIALIZER_UNLOCKED;

// BLE deferred responses (to avoid stack overflow in BLE callback)
volatile bool blePendingStatusRequest = false;
volatile uint8_t blePendingStatusSeq = 0;
volatile bool blePendingOtaStatusRequest = false;
volatile uint8_t blePendingOtaStatusSeq = 0;
constexpr uint8_t BLE_REQUEST_QUEUE_SIZE = 4;
String bleRequestQueue[BLE_REQUEST_QUEUE_SIZE];
volatile uint8_t bleRequestQueueHead = 0;
volatile uint8_t bleRequestQueueTail = 0;
volatile uint8_t bleRequestQueueCount = 0;
portMUX_TYPE bleRequestQueueMutex = portMUX_INITIALIZER_UNLOCKED;

// BLE framed protocol
constexpr uint8_t BLE_FRAME_MAGIC0 = 0xA7;
constexpr uint8_t BLE_FRAME_MAGIC1 = 0x1C;
constexpr uint8_t BLE_FRAME_VERSION = 1;
constexpr uint8_t BLE_FRAME_FLAG_ACK_REQUIRED = 0x01;
constexpr uint8_t BLE_FRAME_HEADER_SIZE = 8;
constexpr uint8_t BLE_FRAME_CRC_SIZE = 2;

enum BleFrameType : uint8_t {
    BLE_MSG_SETTINGS_JSON = 0x01,
    BLE_MSG_STATUS_REQUEST = 0x02,
    BLE_MSG_STATUS_RESPONSE = 0x03,
    BLE_MSG_OTA_START = 0x04,
    BLE_MSG_OTA_STATUS = 0x05,
    BLE_MSG_ACK = 0x7E,
    BLE_MSG_ERROR = 0x7F
};

struct BleFrame {
    uint8_t type = 0;
    uint8_t seq = 0;
    uint8_t flags = 0;
    std::string payload;
};

std::string bleRxBuffer;

// BLE Server Callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE: Client connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE: Client disconnected");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();
      
      if (rxValue.length() > 0) {
        bleRxBuffer.append(rxValue);
        BleFrame frame;
        while (tryExtractBleFrame(bleRxBuffer, frame)) {
          if (frame.type == BLE_MSG_SETTINGS_JSON) {
            if (frame.payload.empty()) {
              sendBleError(frame.seq, "Empty settings payload");
              continue;
            }

            bool canApply = false;
            portENTER_CRITICAL(&blePendingMutex);
            if (!blePendingApply) {
              blePendingApply = true;
              blePendingJson = String(
                  reinterpret_cast<const char*>(frame.payload.data()),
                  frame.payload.length()
              );
              canApply = true;
            }
            portEXIT_CRITICAL(&blePendingMutex);

            if (canApply && (frame.flags & BLE_FRAME_FLAG_ACK_REQUIRED)) {
              sendBleAck(frame.seq);
            } else if (!canApply) {
              sendBleError(frame.seq, "Busy");
            }
          } else if (frame.type == BLE_MSG_STATUS_REQUEST) {
            if (frame.flags & BLE_FRAME_FLAG_ACK_REQUIRED) {
              sendBleAck(frame.seq);
            }
            // Defer status response to main loop to avoid stack overflow
            // (getStatusJSON allocates 4KB DynamicJsonDocument)
            blePendingStatusSeq = frame.seq;
            blePendingStatusRequest = true;
          } else if (frame.type == BLE_MSG_OTA_START) {
            if (frame.flags & BLE_FRAME_FLAG_ACK_REQUIRED) {
              sendBleAck(frame.seq);
            }
            if (!frame.payload.empty()) {
              String url = String(
                  reinterpret_cast<const char*>(frame.payload.data()),
                  frame.payload.length()
              );
              startOTAUpdate(url);
            }
            // Defer OTA status response to main loop
            blePendingOtaStatusSeq = frame.seq;
            blePendingOtaStatusRequest = true;
          } else if (frame.type == BLE_MSG_OTA_STATUS) {
            if (frame.flags & BLE_FRAME_FLAG_ACK_REQUIRED) {
              sendBleAck(frame.seq);
            }
            // Defer OTA status response to main loop
            blePendingOtaStatusSeq = frame.seq;
            blePendingOtaStatusRequest = true;
          } else if (frame.type == BLE_MSG_ACK) {
            // No-op for device
          } else {
            sendBleError(frame.seq, "Unknown message");
          }
        }
      }
    }
};

// Web Server
WebServer server(80);

// BLE Server
BLEServer* pBLEServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool bluetoothEnabled = true;
String bluetoothDeviceName = "ARKLIGHTS-AP";

// Effect IDs
#define FX_SOLID 0
#define FX_BREATH 1
#define FX_RAINBOW 2
#define FX_PULSE 3              // Replaced FX_CHASE - smooth rhythmic pulsing
#define FX_BLINK_RAINBOW 4
#define FX_GRADIENT_SHIFT 5     // Replaced FX_TWINKLE - color gradient that moves
#define FX_FIRE 6
#define FX_METEOR 7
#define FX_WAVE 8
#define FX_CENTER_BURST 9       // Replaced FX_COMET - expands from center outward
#define FX_CANDLE 10
#define FX_STATIC_RAINBOW 11
#define FX_KNIGHT_RIDER 12
#define FX_POLICE 13
#define FX_STROBE 14
#define FX_LARSON_SCANNER 15
#define FX_COLOR_WIPE 16
#define FX_RAINBOW_WIPE 23
#define FX_HAZARD 17            // Replaced FX_THEATER_CHASE - alternating halves flash
#define FX_RUNNING_LIGHTS 18
#define FX_COLOR_SWEEP 19
#define FX_RAINBOW_KNIGHT_RIDER 20
#define FX_DUAL_KNIGHT_RIDER 21
#define FX_DUAL_RAINBOW_KNIGHT_RIDER 22

// Preset IDs
#define PRESET_STANDARD 0
#define PRESET_NIGHT 1
#define PRESET_PARTY 2
#define PRESET_STEALTH 3
#define MAX_PRESETS 16

// Startup Sequence IDs
#define STARTUP_NONE 0
#define STARTUP_POWER_ON 1
#define STARTUP_SCAN 2
#define STARTUP_WAVE 3
#define STARTUP_RACE 4
#define STARTUP_CUSTOM 5

// LED strips (dynamic size) - Use CRGB for all LED types
CRGB* headlight;
CRGB* taillight;
CLEDController* headlightController = nullptr;
CLEDController* taillightController = nullptr;

// System state
uint8_t globalBrightness = DEFAULT_BRIGHTNESS;
uint8_t currentPreset = PRESET_STANDARD;
uint8_t headlightEffect = FX_SOLID;
uint8_t taillightEffect = FX_SOLID;
CRGB headlightColor = CRGB::White;
CRGB taillightColor = CRGB::Red;

struct PresetConfig {
    char name[21];
    uint8_t brightness;
    uint8_t effectSpeed;
    uint8_t headlightEffect;
    uint8_t taillightEffect;
    uint8_t headlightColor[3];
    uint8_t taillightColor[3];
    uint8_t headlightBackgroundEnabled;
    uint8_t taillightBackgroundEnabled;
    uint8_t headlightBackgroundColor[3];
    uint8_t taillightBackgroundColor[3];
};

PresetConfig presets[MAX_PRESETS];
uint8_t presetCount = 0;
bool headlightBackgroundEnabled = false;
bool taillightBackgroundEnabled = false;
CRGB headlightBackgroundColor = CRGB::Black;
CRGB taillightBackgroundColor = CRGB::Black;
bool effectBackgroundEnabled = false;
CRGB effectBackgroundColor = CRGB::Black;
uint8_t effectSpeed = 64; // Speed control (0-255, higher = faster) - Default to slower speed

// RGBW white channel control (for SK6812 RGBW strips)
// 0 = Off, 1 = Exact White, 2 = Boosted White, 3 = Max Brightness
uint8_t rgbwWhiteMode = 0;
bool whiteLEDsEnabled = false;  // Compatibility flag (true when mode != 0)

// Startup sequence settings
uint8_t startupSequence = STARTUP_POWER_ON;
bool startupEnabled = true;
uint16_t startupDuration = 3000; // Duration in milliseconds

// MPU6050 Motion Control Settings
#define MPU_SDA_PIN 5
#define MPU_SCL_PIN 6
MPU6050 mpu;

// NVS for persistent settings storage (survives OTA filesystem updates)
// ESP32 NVS has a 508-byte limit per key; we chunk the settings JSON into NVS_CHUNK_SIZE pieces.
constexpr size_t NVS_CHUNK_SIZE = 500;
constexpr char NVS_KEY_CHUNK_COUNT[] = "sc";  // 1 byte: number of chunks
Preferences nvs;
const char* NVS_NAMESPACE = "arklights";
bool nvsMigrationPending = false; // Track if NVS migration needs to happen

// Motion control settings
bool motionEnabled = true;
bool blinkerEnabled = true;
bool parkModeEnabled = true;
bool impactDetectionEnabled = true;
float motionSensitivity = 1.0; // 0.5 to 2.0
uint16_t blinkerDelay = 300; // ms before triggering blinker
uint16_t blinkerTimeout = 2000; // ms before turning off blinker
uint8_t parkDetectionAngle = 15; // degrees of tilt for park mode
uint8_t impactThreshold = 3; // G-force threshold for impact detection

// Park mode noise thresholds
float parkAccelNoiseThreshold = 0.05; // G-force deviation from gravity threshold (0.05G = very small movement)
float parkGyroNoiseThreshold = 2.5;  // deg/s threshold for gyro noise (increased to account for MPU drift)
uint16_t parkStationaryTime = 2000;  // ms of stationary time before park mode activates (back to 2 seconds)

// Park mode effect settings
uint8_t parkEffect = FX_BREATH;        // Effect to use in park mode
uint8_t parkEffectSpeed = 64;          // Speed for park mode effect
CRGB parkHeadlightColor = CRGB::Blue;  // Headlight color in park mode
CRGB parkTaillightColor = CRGB::Blue;  // Taillight color in park mode
uint8_t parkBrightness = 128;          // Brightness in park mode

// OTA Update settings
String otaUpdateURL = "";
bool otaInProgress = false;
uint8_t otaProgress = 0;
String otaStatus = "Ready";
String otaError = "";
unsigned long otaStartTime = 0;
String otaFileName = "";
size_t otaFileSize = 0;

// Motion state
bool blinkerActive = false;
int8_t blinkerDirection = 0; // -1 = left, 1 = right, 0 = none
bool parkModeActive = false;
unsigned long lastMotionUpdate = 0;
unsigned long blinkerStartTime = 0;
unsigned long parkStartTime = 0;
unsigned long lastImpactTime = 0;
bool manualBlinkerActive = false;

// Direction detection state
bool directionBasedLighting = false;
bool isMovingForward = true;  // true = forward, false = backward
bool directionChangePending = false;  // Direction change detected but not yet applied
float forwardAccelThreshold = 0.3;  // G-force threshold for direction change
unsigned long directionChangeDetectedTime = 0;
unsigned long directionFadeStartTime = 0;
const unsigned long DIRECTION_SUSTAIN_TIME = 500;  // ms direction must be sustained before switching
const unsigned long DIRECTION_FADE_DURATION = 1500;  // ms for smooth fade transition (needs to be visible)
uint8_t headlightMode = 0;  // 0 = solid white, 1 = headlight effect
float directionFadeProgress = 0.0;  // 0.0 to 1.0 for fade transition

// Braking detection state
bool brakingEnabled = false;  // Default to disabled - user must enable via UI
bool brakingActive = false;
bool manualBrakeActive = false;
float brakingThreshold = -0.5;  // G-force deceleration threshold (negative = deceleration)
const unsigned long BRAKING_SUSTAIN_TIME = 200;  // ms deceleration must be sustained before triggering
unsigned long brakingDetectedTime = 0;
unsigned long brakingStartTime = 0;
uint8_t brakingEffect = 0;  // 0 = flash, 1 = pulse
uint8_t brakingBrightness = 255;  // Brightness during braking
uint8_t brakingFlashCount = 0;  // Number of flashes completed (0-3)
uint8_t brakingPulseCount = 0;  // Number of pulses completed (0-3)
unsigned long lastBrakingFlash = 0;
unsigned long lastBrakingPulse = 0;
const unsigned long BRAKING_FLASH_INTERVAL = 200;  // ms between flashes
const unsigned long BRAKING_PULSE_DURATION = 300;  // ms per pulse cycle
const uint8_t BRAKING_CYCLE_COUNT = 3;  // Number of flash/pulse cycles before going solid

// Filtering for direction detection (to reduce noise)
float filteredForwardAccel = 0.0;
const float FILTER_ALPHA = 0.7;  // Low-pass filter coefficient (0.0-1.0, higher = less filtering)

// Calibration system
bool calibrationMode = false;
bool calibrationComplete = false;
uint8_t calibrationStep = 0;
unsigned long calibrationStartTime = 0;
const unsigned long calibrationTimeout = 30000; // 30 seconds per step

struct CalibrationData {
    float levelAccelX, levelAccelY, levelAccelZ;
    float forwardAccelX, forwardAccelY, forwardAccelZ;
    float backwardAccelX, backwardAccelY, backwardAccelZ;
    float leftAccelX, leftAccelY, leftAccelZ;
    float rightAccelX, rightAccelY, rightAccelZ;
    char forwardAxis = 'X';
    char leftRightAxis = 'Y';
    int forwardSign = 1;
    int leftRightSign = 1;
    bool valid = false;
} calibration;

// Motion data structure
struct MotionData {
    float pitch, roll, yaw;
    float accelX, accelY, accelZ;
    float gyroX, gyroY, gyroZ;
};

// ESPNow Peer Management
ESPNowPeer espNowPeers[10]; // Max 10 peers
uint8_t espNowPeerCount = 0;
uint32_t lastESPNowSend = 0;
const uint32_t ESPNOW_SEND_INTERVAL = 100; // Legacy interval (unused for group sync)
const uint32_t ESPNOW_SYNC_MIN_INTERVAL = 200; // Min delay between sync bursts
const uint32_t ESPNOW_SYNC_IDLE_INTERVAL = 1000; // Keepalive when no changes

struct ESPNowSyncState {
    uint8_t brightness = 0;
    uint8_t headlightEffect = 0;
    uint8_t taillightEffect = 0;
    uint8_t effectSpeed = 0;
    uint8_t headlightColor[3] = {0, 0, 0};
    uint8_t taillightColor[3] = {0, 0, 0};
    uint8_t headlightBackgroundEnabled = 0;
    uint8_t taillightBackgroundEnabled = 0;
    uint8_t headlightBackgroundColor[3] = {0, 0, 0};
    uint8_t taillightBackgroundColor[3] = {0, 0, 0};
    uint8_t preset = 0;
};

ESPNowSyncState lastSyncState;
bool hasLastSyncState = false;

// Group Management
struct GroupMember {
    uint8_t mac[6];
    char deviceName[21];
    uint32_t lastSeen;
    bool isAuthenticated;
};

GroupMember groupMembers[10]; // Max 10 group members
uint8_t groupMemberCount = 0;
uint32_t lastGroupHeartbeat = 0;
uint8_t groupMasterMac[6] = {0};
bool hasGroupMaster = false;

// ESPNow Callback Functions
void espNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("ESPNow: Data sent successfully");
    } else {
        Serial.println("ESPNow: Data send failed");
    }
}

// Speed normalization for consistent effect timing
#define ARKLIGHTS_FPS 42
#define FRAMETIME_FIXED (1000/ARKLIGHTS_FPS)
#define MIN_FRAME_DELAY 2

// Speed formula for length-normalized effects
#define SPEED_FORMULA_L(speed, length) (5U + (50U*(255U - speed))/length)

// Effect timing system
struct EffectTiming {
    unsigned long lastFrame = 0;
    uint16_t frameTime = FRAMETIME_FIXED;
    uint16_t step = 0;
    uint16_t stepAccumulator = 0; // For fractional step increments
    bool needsUpdate = false;
};

// Global timing variables (declared before ESP-NOW callbacks)
EffectTiming headlightTiming;
EffectTiming taillightTiming;

// Group Management function declarations
void handleGroupMessage(const uint8_t* mac_addr, const uint8_t* data, int len);
bool isGroupMember(const uint8_t* mac_addr);
void sendGroupHeartbeat();
void sendJoinRequest();
void sendJoinResponse(const uint8_t* mac_addr, bool accept);
void addGroupMember(const uint8_t* mac_addr, const char* deviceName);
void removeGroupMember(const uint8_t* mac_addr);
void checkMasterTimeout();
void becomeMaster();
void generateGroupCode();
String getDeviceMAC();
String formatMacAddress(const uint8_t* mac);
bool parseMacAddress(const String& macStr, uint8_t* outMac);
String formatColorHex(const CRGB& color);

void espNowReceiveCallback(const uint8_t *mac_addr, const uint8_t *data, int len) {
    // Only process if ESPNow is enabled
    if (!enableESPNow) return;
    
    // Check if it's a group management packet
    if (data[0] == 'G') {
        if (len != sizeof(ESPNowGroupData)) {
            Serial.println("Group: Invalid packet size");
            return;
        }
        handleGroupMessage(mac_addr, data, len);
        return;
    }
    
    // Check if it's an ArkLights packet
    if (data[0] != 'A') {
        return;
    }
    if (len != sizeof(ESPNowLEDData)) {
        Serial.println("ESPNow: Invalid packet size");
        return;
    }

    // Only process LED sync when enabled
    if (!useESPNowSync) {
        return;
    }
    
    ESPNowLEDData* receivedData = (ESPNowLEDData*)data;
    
    // Verify checksum
    uint8_t calculatedChecksum = 0;
    for (int i = 0; i < (int)sizeof(ESPNowLEDData) - 1; i++) {
        calculatedChecksum ^= data[i];
    }
    
    if (calculatedChecksum != receivedData->checksum) {
        Serial.println("ESPNow: Invalid checksum");
        return;
    }
    
    uint32_t currentTime = millis();
    
    Serial.println("ESPNow: Received LED data from peer");
    
    // Only accept LED data from the group master when in a group
    if (groupCode.length() > 0) {
        if (isGroupMaster) {
            // Leader ignores incoming LED data (local control only)
            return;
        }
        if (!hasGroupMaster || memcmp(mac_addr, groupMasterMac, 6) != 0) {
            Serial.println("ESPNow: Ignored data from non-master device");
            return;
        }
    }
    
    // Apply the received LED settings (but NOT motion-based effects)
    // Only sync main LED effects, not MPU-driven effects like blinkers or park mode
    if (!blinkerActive && !parkModeActive) {
        globalBrightness = receivedData->brightness;
        headlightEffect = receivedData->headlightEffect;
        taillightEffect = receivedData->taillightEffect;
        effectSpeed = receivedData->effectSpeed;
        headlightColor = CRGB(receivedData->headlightColor[0], receivedData->headlightColor[1], receivedData->headlightColor[2]);
        taillightColor = CRGB(receivedData->taillightColor[0], receivedData->taillightColor[1], receivedData->taillightColor[2]);
        headlightBackgroundEnabled = receivedData->headlightBackgroundEnabled;
        taillightBackgroundEnabled = receivedData->taillightBackgroundEnabled;
        headlightBackgroundColor = CRGB(receivedData->headlightBackgroundColor[0], receivedData->headlightBackgroundColor[1], receivedData->headlightBackgroundColor[2]);
        taillightBackgroundColor = CRGB(receivedData->taillightBackgroundColor[0], receivedData->taillightBackgroundColor[1], receivedData->taillightBackgroundColor[2]);
        currentPreset = receivedData->preset;
        
        // Sync timing for coordinated effects (ignore timestamps)
        if (receivedData->masterStep > 0) {
            headlightTiming.step = receivedData->masterStep;
            taillightTiming.step = receivedData->masterStep;
        }
        
        // Adjust speed for different strip lengths
        if (receivedData->stripLength > 0) {
            // Normalize speed based on strip length difference
            uint8_t maxLength = (headlightLedCount > taillightLedCount) ? headlightLedCount : taillightLedCount;
            uint8_t lengthRatio = (maxLength * 100) / receivedData->stripLength;
            effectSpeed = constrain(effectSpeed * lengthRatio / 100, 0, 255);
        }
        
        // Update LED strips
        FastLED.setBrightness(globalBrightness);
        
        Serial.println("ESPNow: Applied LED settings from peer");
    } else {
        Serial.println("ESPNow: Ignored sync due to active motion effects");
    }
}

// Effect state
unsigned long lastUpdate = 0;
uint16_t effectStep = 0;
unsigned long lastEffectUpdate = 0;

// Startup sequence state
bool startupActive = false;
unsigned long startupStartTime = 0;
uint16_t startupStep = 0;

// WiFi AP Configuration
String apName = "ARKLIGHTS-AP";
String apPassword = "float420";

// Filesystem-based persistent storage

// Function declarations
void updateEffects();
bool shouldUpdateEffect(EffectTiming& timing, uint8_t speed, uint8_t length);
void processDirectionDetection(MotionData& data);
void processBrakingDetection(MotionData& data);
void showBrakingEffect();
void blendLEDArrays(CRGB* target, CRGB* source1, CRGB* source2, uint8_t numLeds, float fadeProgress);
void applyEffectToArray(CRGB* leds, uint8_t numLeds, uint8_t effect, CRGB color, EffectTiming& timing, uint8_t ledType, uint8_t colorOrder, CRGB backgroundColor, bool backgroundEnabled);
void updateSoftAPChannel();

// Original effect functions (kept for compatibility)
void effectBreath(CRGB* leds, uint8_t numLeds, CRGB color);
void effectRainbow(CRGB* leds, uint8_t numLeds);
void effectPulse(CRGB* leds, uint8_t numLeds, CRGB color);           // PEV-friendly: smooth rhythmic pulsing
void effectBlinkRainbow(CRGB* leds, uint8_t numLeds);
void effectGradientShift(CRGB* leds, uint8_t numLeds, CRGB color);   // PEV-friendly: moving color gradient
void effectFire(CRGB* leds, uint8_t numLeds);
void effectMeteor(CRGB* leds, uint8_t numLeds, CRGB color);
void effectWave(CRGB* leds, uint8_t numLeds, CRGB color);
void effectCenterBurst(CRGB* leds, uint8_t numLeds, CRGB color);     // PEV-friendly: center expansion
void effectCandle(CRGB* leds, uint8_t numLeds);
void effectStaticRainbow(CRGB* leds, uint8_t numLeds);
void effectKnightRider(CRGB* leds, uint8_t numLeds, CRGB color);
void effectPolice(CRGB* leds, uint8_t numLeds);
void effectStrobe(CRGB* leds, uint8_t numLeds, CRGB color);
void effectLarsonScanner(CRGB* leds, uint8_t numLeds, CRGB color);
void effectColorWipe(CRGB* leds, uint8_t numLeds, CRGB color);
void effectHazard(CRGB* leds, uint8_t numLeds, CRGB color);          // PEV-friendly: alternating halves
void effectRunningLights(CRGB* leds, uint8_t numLeds, CRGB color);
void effectColorSweep(CRGB* leds, uint8_t numLeds, CRGB color);

// Improved effect functions with consistent timing
void effectBreathImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectRainbowImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectPulseImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);           // PEV-friendly
void effectBlinkRainbowImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectGradientShiftImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);   // PEV-friendly
void effectFireImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectMeteorImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectWaveImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectCenterBurstImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);     // PEV-friendly
void effectCandleImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectKnightRiderImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectPoliceImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectStrobeImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectLarsonScannerImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectColorWipeImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectRainbowWipeImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectHazardImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);          // PEV-friendly
void effectRunningLightsImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectColorSweepImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectRainbowKnightRiderImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectDualKnightRiderImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectDualRainbowKnightRiderImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void setPreset(uint8_t preset);
void handleSerialCommands();
void printStatus();
void initDefaultPresets();
void captureCurrentPreset(PresetConfig& preset);
bool addPreset(const String& name);
bool updatePreset(uint8_t index, const String& name);
bool deletePreset(uint8_t index);
void loadPresetsFromDoc(const JsonDocument& doc);
void savePresetsToDoc(JsonDocument& doc);
void printHelp();
void listSPIFFSFiles();
void showSettingsFile();
void cleanDuplicateFiles();

// Startup sequence functions
void startStartupSequence();
void updateStartupSequence();
void startupPowerOn();
void startupScan();
void startupWave();
void startupRace();
void startupCustom();
String getStartupSequenceName(uint8_t sequence);

// Motion control functions
void initMotionControl();
MotionData getMotionData();
void updateMotionControl();
void processBlinkers(MotionData& data);
void processParkMode(MotionData& data);
void processImpactDetection(MotionData& data);
void startCalibration();
void captureCalibrationStep(MotionData& data);
void resetCalibration();
void completeCalibration();
float getCalibratedForwardAccel(MotionData& data);
float getCalibratedLeftRightAccel(MotionData& data);
void showBlinkerEffect(int direction);
void showParkEffect();
void showImpactEffect();
void resetToNormalEffects();

// OTA Update functions
void handleOTAUpdate();
void handleOTAStatus();
void handleOTAUpload();
void startOTAUpdate(String url);
void startOTAUpdateFromFile(String filename);
void updateOTAProgress(unsigned int progress, unsigned int total);
void handleOTAError(int error);

// LED Configuration functions
void initializeLEDs();
void applyRgbwWhiteChannelMode();
void setRgbwWhiteMode(uint8_t mode);
void testLEDConfiguration();
String getLEDTypeName(uint8_t type);
String getColorOrderName(uint8_t order);
CRGB convertColorOrder(CRGB color, uint8_t colorOrder);
void setLEDColor(CRGB* leds, uint8_t index, CRGB color, uint8_t ledType, uint8_t colorOrder);
void fillSolidWithColorOrder(CRGB* leds, uint8_t numLeds, CRGB color, uint8_t ledType, uint8_t colorOrder);
void applyColorOrderToArray(CRGB* leds, uint8_t numLeds, uint8_t ledType, uint8_t colorOrder);
void fillRainbowWithColorOrder(CRGB* leds, uint8_t numLeds, uint8_t initialHue, uint8_t deltaHue, uint8_t ledType, uint8_t colorOrder);
// Filesystem functions
void initFilesystem();
bool saveSettings();
bool loadSettings();
bool saveSettingsToNVS();
bool loadSettingsFromNVS();
bool migrateSettingsFromSPIFFSToNVS();
void testFilesystem();

// ESPNow functions
bool initESPNow();
void sendESPNowData();
void addESPNowPeer(uint8_t* macAddress);
void deinitESPNow();
bool ensureESPNowActive(const char* context);
const char* espNowErrorName(esp_err_t error);

// Group Management functions
void handleGroupMessage(const uint8_t* mac_addr, const uint8_t* data, int len);
bool isGroupMember(const uint8_t* mac_addr);
void sendGroupHeartbeat();
void sendJoinRequest();
void sendJoinResponse(const uint8_t* mac_addr, bool accept);
void addGroupMember(const uint8_t* mac_addr, const char* deviceName);
void removeGroupMember(const uint8_t* mac_addr);
void checkMasterTimeout();
void becomeMaster();
void generateGroupCode();
String getDeviceMAC();

// Web server functions
void setupWiFiAP();
void setupBluetooth();
void processBLEHTTPRequest(String request);
void setupWebServer();
void handleRoot();
void handleUI();
void handleUIUpdate();
bool serveEmbeddedFile(const char* filename);
bool processUIUpdate(const String& updatePath);
bool processUIUpdateStreaming(const String& updatePath);
bool applyApiJson(DynamicJsonDocument& doc, bool allowRestart, bool& shouldRestart);
void appendBleRequestChunk(const String& chunk);
bool consumeBleRequest(String& requestOut);
int parseBleContentLength(const String& headers);
void sendBleResponse(const String& response);
uint16_t crc16Ccitt(const uint8_t* data, size_t length);
bool tryExtractBleFrame(std::string& buffer, BleFrame& frame);
void sendBleFrame(uint8_t type, uint8_t seq, uint8_t flags, const uint8_t* payload, uint16_t length);
void sendBleAck(uint8_t seq);
void sendBleError(uint8_t seq, const String& message);
String getOtaStatusJSON();
bool saveUIFile(const String& filename, const String& content);
void serveEmbeddedUI();
void handleAPI();
void handleStatus();
String getStatusJSON();
void buildStatusDocument(DynamicJsonDocument& doc);
void handleLEDConfig();
void handleLEDTest();
void handleGetSettings();
void sendJSONResponse(DynamicJsonDocument& doc);
void restoreDefaultsToStock();
String getDefaultApName();

void setup() {
    Serial.begin(115200);
    Serial.println("ArkLights PEV Lighting System");
    Serial.println("==============================");
    
    // ⚡ FAST BOOT: Initialize LEDs FIRST for immediate visual feedback
    // Use default values if settings not loaded yet
    initializeLEDs();
    
    // Show a simple "booting" pattern immediately
    fillSolidWithColorOrder(headlight, headlightLedCount, CRGB::Blue, headlightLedType, headlightColorOrder);
    fillSolidWithColorOrder(taillight, taillightLedCount, CRGB::Blue, taillightLedType, taillightColorOrder);
    FastLED.setBrightness(64); // Low brightness for boot indicator
    FastLED.show();
    
    // Initialize filesystem (can be slow)
    initFilesystem();
    
    // Load saved settings
    if (!loadSettings()) {
        // No saved settings - use unique default AP/BLE name from MAC
        apName = getDefaultApName();
        bluetoothDeviceName = apName;
        apPassword = "float420";
        Serial.printf("📡 First boot: using unique AP/BLE name %s\n", apName.c_str());
    }
    if (presetCount == 0) {
        initDefaultPresets();
    }
    
    // Skip testFilesystem() during boot - it's unnecessary and slow
    // Can be called manually via serial command if needed
    
    // Initialize motion control (I2C can be slow, but LEDs already showing)
    initMotionControl();
    
    // Start startup sequence if enabled
    Serial.printf("🔍 Startup check: enabled=%s, sequence=%d (%s)\n", 
                  startupEnabled ? "true" : "false", 
                  startupSequence, 
                  getStartupSequenceName(startupSequence).c_str());
    
    if (startupEnabled && startupSequence != STARTUP_NONE) {
        startStartupSequence();
    } else {
        // Show loaded colors immediately
        Serial.println("⚡ Skipping startup sequence, showing loaded colors");
        fillSolidWithColorOrder(headlight, headlightLedCount, headlightColor, headlightLedType, headlightColorOrder);
        fillSolidWithColorOrder(taillight, taillightLedCount, taillightColor, taillightLedType, taillightColorOrder);
        FastLED.show();
        // Removed delay(1000) - no need to wait
    }
    
    Serial.printf("Headlight: %d LEDs on GPIO %d (Type: %s, Order: %s)\n", 
                  headlightLedCount, HEADLIGHT_PIN, 
                  getLEDTypeName(headlightLedType).c_str(),
                  getColorOrderName(headlightColorOrder).c_str());
    Serial.printf("Taillight: %d LEDs on GPIO %d (Type: %s, Order: %s)\n", 
                  taillightLedCount, TAILLIGHT_PIN,
                  getLEDTypeName(taillightLedType).c_str(),
                  getColorOrderName(taillightColorOrder).c_str());
    
    // LED strips already initialized for visual debugging
    
    // Apply loaded brightness setting
    FastLED.setBrightness(globalBrightness);
    
    // Settings are already loaded from flash, no need to override with presets
    
    // Setup WiFi AP and Web Server
    setupWiFiAP();
    setupBluetooth();
    setupWebServer();
    
    // Initialize ESPNow
    initESPNow();
    
    Serial.println("System initialized successfully!");
    Serial.println("Web UI available at: http://192.168.4.1");
    
    // Debug: Final color check before main loop
    Serial.printf("🔍 Final colors before main loop - Headlight RGB(%d,%d,%d), Taillight RGB(%d,%d,%d)\n",
                  headlightColor.r, headlightColor.g, headlightColor.b,
                  taillightColor.r, taillightColor.g, taillightColor.b);
    
    printHelp();
}

void loop() {
    // Handle deferred NVS migration (non-blocking, happens once after boot)
    if (nvsMigrationPending) {
        Serial.println("🔄 Performing NVS migration in background...");
        if (saveSettingsToNVS()) {
            Serial.println("✅ Settings migrated to NVS (will survive OTA filesystem updates)");
        } else {
            Serial.println("⚠️ Failed to migrate settings to NVS");
        }
        nvsMigrationPending = false;
    }
    
    // Update startup sequence if active
    if (startupActive) {
        updateStartupSequence();
        FastLED.show();
        delay(50); // Slower update for startup sequences
        return;
    }
    
    // Update motion control at 20Hz
    if (motionEnabled && millis() - lastMotionUpdate >= 50) {
        updateMotionControl();
        lastMotionUpdate = millis();
    }
    
    // Update effects at 50 FPS
    if (millis() - lastUpdate >= 20) {
        updateEffects();
        FastLED.show();
        lastUpdate = millis();
    }
    
    // Handle BLE reconnection
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pBLEServer->startAdvertising(); // restart advertising
        Serial.println("BLE: Start advertising");
        oldDeviceConnected = deviceConnected;
    }
    
    // Check if a new client connected
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
    
    // Send ESPNow data
    sendESPNowData();
    
    // Handle group management
    if (enableESPNow && groupCode.length() > 0) {
        checkMasterTimeout();
        if (isGroupMaster) {
            sendGroupHeartbeat();
        } else if (joinInProgress && (millis() - lastJoinRequest >= JOIN_RETRY_INTERVAL)) {
            sendJoinRequest();
        }
    }
    
    // Handle serial commands
    handleSerialCommands();
    
    // Handle web server requests
    server.handleClient();

    // Process queued BLE requests outside BT task
    String pendingBleRequest = "";
    portENTER_CRITICAL(&bleRequestQueueMutex);
    if (bleRequestQueueCount > 0) {
        pendingBleRequest = bleRequestQueue[bleRequestQueueHead];
        bleRequestQueue[bleRequestQueueHead] = "";
        bleRequestQueueHead = (bleRequestQueueHead + 1) % BLE_REQUEST_QUEUE_SIZE;
        bleRequestQueueCount--;
    }
    portEXIT_CRITICAL(&bleRequestQueueMutex);

    if (pendingBleRequest.length() > 0) {
        processBLEHTTPRequest(pendingBleRequest);
    }

    // Apply deferred BLE API updates outside BT task
    String pendingJsonCopy = "";
    bool shouldApplyPending = false;
    portENTER_CRITICAL(&blePendingMutex);
    if (blePendingApply) {
        shouldApplyPending = true;
        blePendingApply = false;
        pendingJsonCopy = blePendingJson;
        blePendingJson = "";
    }
    portEXIT_CRITICAL(&blePendingMutex);

    if (shouldApplyPending && pendingJsonCopy.length() > 0) {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, pendingJsonCopy);
        if (error) {
            Serial.printf("BLE: Deferred JSON parse error: %s\n", error.c_str());
        } else {
            bool shouldRestart = false;
            applyApiJson(doc, true, shouldRestart);
            if (shouldRestart) {
                delay(1000);
                ESP.restart();
            }
        }
    }
    
    // Handle deferred BLE status responses (to avoid stack overflow in BLE callback)
    // These run in main loop which has much more stack space than BTC_TASK
    if (blePendingStatusRequest) {
        blePendingStatusRequest = false;
        uint8_t seq = blePendingStatusSeq;
        String statusJson = getStatusJSON();
        sendBleFrame(
            BLE_MSG_STATUS_RESPONSE,
            seq,
            0,
            reinterpret_cast<const uint8_t*>(statusJson.c_str()),
            statusJson.length()
        );
    }
    
    if (blePendingOtaStatusRequest) {
        blePendingOtaStatusRequest = false;
        uint8_t seq = blePendingOtaStatusSeq;
        String otaStatusJson = getOtaStatusJSON();
        sendBleFrame(
            BLE_MSG_OTA_STATUS,
            seq,
            0,
            reinterpret_cast<const uint8_t*>(otaStatusJson.c_str()),
            otaStatusJson.length()
        );
    }
    
    delay(10);
}

// Helper function to get effect-specific speed multiplier
// With the new consistent frame rate system, multipliers are reduced since speed control works better
uint8_t getEffectSpeedMultiplier(uint8_t effect) {
    switch (effect) {
        case FX_RAINBOW:
        case FX_BLINK_RAINBOW:
            return 2; // Rainbow effects: slight boost for visibility (reduced from 8x)
        case FX_PULSE:
        case FX_METEOR:
        case FX_CENTER_BURST:
            return 1; // Movement effects: normal speed (removed multiplier)
        case FX_WAVE:
        case FX_COLOR_WIPE:
        case FX_RAINBOW_WIPE:
        case FX_HAZARD:
        case FX_RUNNING_LIGHTS:
        case FX_COLOR_SWEEP:
        case FX_RAINBOW_KNIGHT_RIDER:
        case FX_DUAL_KNIGHT_RIDER:
        case FX_DUAL_RAINBOW_KNIGHT_RIDER:
            return 1; // Sweep effects: normal speed (removed multiplier)
        default:
            return 1; // Other effects: normal speed
    }
}

// Improved timing system: Keep frame rate high, control speed via step increment
// This prevents stuttering while still allowing speed control
bool shouldUpdateEffect(EffectTiming& timing, uint8_t speed, uint8_t length) {
    unsigned long now = millis();
    
    // Always use a consistent, high frame rate (~24ms = ~42 FPS)
    // This keeps effects smooth and prevents stuttering
    timing.frameTime = FRAMETIME_FIXED;
    
    if (now - timing.lastFrame >= timing.frameTime) {
        timing.lastFrame = now;
        
        // Control speed by adjusting step increment instead of frame rate
        // Speed 0 = very slow (step increments by 0.1 per frame)
        // Speed 255 = very fast (step increments by 8.0 per frame)
        // Map speed to step increment: 0-255 -> 10 to 800 (scaled by 100 for fractional steps)
        uint16_t stepIncrement = map(speed, 0, 255, 10, 800); // 0.1 to 8.0 steps per frame (scaled by 100)
        
        // Apply step increment with fractional precision
        // This allows smooth speed control without stuttering
        timing.stepAccumulator += stepIncrement;
        if (timing.stepAccumulator >= 100) {
            timing.step += (timing.stepAccumulator / 100);
            timing.stepAccumulator = timing.stepAccumulator % 100;
        }
        
        return true;
    }
    return false;
}

// Helper function to blend two LED arrays with fade progress
void blendLEDArrays(CRGB* target, CRGB* source1, CRGB* source2, uint8_t numLeds, float fadeProgress) {
    for (uint8_t i = 0; i < numLeds; i++) {
        // Blend between source1 (old) and source2 (new) based on fadeProgress
        // fadeProgress 0.0 = all source1, 1.0 = all source2
        float r = source1[i].r + (source2[i].r - source1[i].r) * fadeProgress;
        float g = source1[i].g + (source2[i].g - source1[i].g) * fadeProgress;
        float b = source1[i].b + (source2[i].b - source1[i].b) * fadeProgress;
        target[i].r = (uint8_t)constrain(r, 0, 255);
        target[i].g = (uint8_t)constrain(g, 0, 255);
        target[i].b = (uint8_t)constrain(b, 0, 255);
    }
}

// Helper function to apply effect to LED array
void applyEffectToArray(CRGB* leds, uint8_t numLeds, uint8_t effect, CRGB color, EffectTiming& timing, uint8_t ledType, uint8_t colorOrder, CRGB backgroundColor, bool backgroundEnabled) {
    effectBackgroundEnabled = backgroundEnabled;
    effectBackgroundColor = backgroundColor;
    switch (effect) {
        case FX_SOLID:
            fillSolidWithColorOrder(leds, numLeds, color, ledType, colorOrder);
            break;
        case FX_BREATH:
            effectBreathImproved(leds, numLeds, color, timing.step);
            break;
        case FX_RAINBOW:
            effectRainbowImproved(leds, numLeds, timing.step);
            break;
        case FX_PULSE:
            effectPulseImproved(leds, numLeds, color, timing.step);
            break;
        case FX_BLINK_RAINBOW:
            effectBlinkRainbowImproved(leds, numLeds, timing.step);
            break;
        case FX_GRADIENT_SHIFT:
            effectGradientShiftImproved(leds, numLeds, color, timing.step);
            break;
        case FX_FIRE:
            effectFireImproved(leds, numLeds, timing.step);
            break;
        case FX_METEOR:
            effectMeteorImproved(leds, numLeds, color, timing.step);
            break;
        case FX_WAVE:
            effectWaveImproved(leds, numLeds, color, timing.step);
            break;
        case FX_CENTER_BURST:
            effectCenterBurstImproved(leds, numLeds, color, timing.step);
            break;
        case FX_CANDLE:
            effectCandleImproved(leds, numLeds, timing.step);
            break;
        case FX_STATIC_RAINBOW:
            effectStaticRainbow(leds, numLeds);
            break;
        case FX_KNIGHT_RIDER:
            effectKnightRiderImproved(leds, numLeds, color, timing.step);
            break;
        case FX_POLICE:
            effectPoliceImproved(leds, numLeds, timing.step);
            break;
        case FX_STROBE:
            effectStrobeImproved(leds, numLeds, color, timing.step);
            break;
        case FX_LARSON_SCANNER:
            effectLarsonScannerImproved(leds, numLeds, color, timing.step);
            break;
        case FX_COLOR_WIPE:
            effectColorWipeImproved(leds, numLeds, color, timing.step);
            break;
        case FX_RAINBOW_WIPE:
            effectRainbowWipeImproved(leds, numLeds, timing.step);
            break;
        case FX_HAZARD:
            effectHazardImproved(leds, numLeds, color, timing.step);
            break;
        case FX_RUNNING_LIGHTS:
            effectRunningLightsImproved(leds, numLeds, color, timing.step);
            break;
        case FX_COLOR_SWEEP:
            effectColorSweepImproved(leds, numLeds, color, timing.step);
            break;
        case FX_RAINBOW_KNIGHT_RIDER:
            effectRainbowKnightRiderImproved(leds, numLeds, timing.step);
            break;
        case FX_DUAL_KNIGHT_RIDER:
            effectDualKnightRiderImproved(leds, numLeds, color, timing.step);
            break;
        case FX_DUAL_RAINBOW_KNIGHT_RIDER:
            effectDualRainbowKnightRiderImproved(leds, numLeds, timing.step);
            break;
    }
    // Apply color order conversion for RGBW LEDs (FX_SOLID already handles this)
    if (effect != FX_SOLID) {
        applyColorOrderToArray(leds, numLeds, ledType, colorOrder);
    }
}

void updateEffects() {
    // Use timing system for consistent effect speeds
    bool headlightUpdate = shouldUpdateEffect(headlightTiming, effectSpeed, headlightLedCount);
    bool taillightUpdate = shouldUpdateEffect(taillightTiming, effectSpeed, taillightLedCount);
    
    // Only update if timing allows it, UNLESS we're in a fade or manual effects are active
    if (!headlightUpdate && !taillightUpdate && !directionChangePending && !blinkerActive && !brakingActive) {
        return;
    }
    
    // Priority 1: Park mode (highest priority - overrides everything)
    if (parkModeActive) {
        showParkEffect();
        return; // Skip normal effects when in park mode
    }
    
    // Priority 3: Normal effects (lowest priority - default behavior)
    // Restore normal brightness
    FastLED.setBrightness(globalBrightness);
    
    // Direction-based lighting mode
    if (directionBasedLighting) {
        // Determine which lights are "front" (headlight) and "back" (taillight) based on direction
        CRGB* frontLights = isMovingForward ? headlight : taillight;
        CRGB* backLights = isMovingForward ? taillight : headlight;
        uint8_t frontCount = isMovingForward ? headlightLedCount : taillightLedCount;
        uint8_t backCount = isMovingForward ? taillightLedCount : headlightLedCount;
        uint8_t frontLedType = isMovingForward ? headlightLedType : taillightLedType;
        uint8_t backLedType = isMovingForward ? taillightLedType : headlightLedType;
        uint8_t frontColorOrder = isMovingForward ? headlightColorOrder : taillightColorOrder;
        uint8_t backColorOrder = isMovingForward ? taillightColorOrder : headlightColorOrder;
        EffectTiming& frontTiming = isMovingForward ? headlightTiming : taillightTiming;
        EffectTiming& backTiming = isMovingForward ? taillightTiming : headlightTiming;
        bool frontUpdate = isMovingForward ? headlightUpdate : taillightUpdate;
        bool backUpdate = isMovingForward ? taillightUpdate : headlightUpdate;
        
        // Static arrays for fade blending (sized to maximum possible)
        static CRGB* headlightOld = nullptr;
        static CRGB* taillightOld = nullptr;
        static bool arraysInitialized = false;
        static bool fadeStateSaved = false;  // Track if we've saved state for current fade
        
        if (!arraysInitialized) {
            uint8_t maxCount = (headlightLedCount > taillightLedCount) ? headlightLedCount : taillightLedCount;
            headlightOld = new CRGB[maxCount];
            taillightOld = new CRGB[maxCount];
            arraysInitialized = true;
        }
        
        // Handle fade transition (including 100% completion frame)
        if (directionChangePending && directionFadeProgress >= 0.0 && directionFadeProgress <= 1.0) {
            // During fade: blend between old and new directions
            // Note: isMovingForward is still the OLD direction during fade
            bool newDirection = !isMovingForward;
            
            // Save current state on FIRST frame of fade (when fadeProgress is exactly 0.0)
            // This captures what's currently displayed BEFORE we start blending
            if (directionFadeProgress == 0.0 && !fadeStateSaved) {
                // Save from the arrays that are currently being displayed (based on OLD direction)
                CRGB* currentFrontLights = isMovingForward ? headlight : taillight;
                CRGB* currentBackLights = isMovingForward ? taillight : headlight;
                uint8_t currentFrontCount = isMovingForward ? headlightLedCount : taillightLedCount;
                uint8_t currentBackCount = isMovingForward ? taillightLedCount : headlightLedCount;
                
                // Save to the appropriate old arrays
                memcpy(isMovingForward ? headlightOld : taillightOld, currentFrontLights, currentFrontCount * sizeof(CRGB));
                memcpy(isMovingForward ? taillightOld : headlightOld, currentBackLights, currentBackCount * sizeof(CRGB));
                fadeStateSaved = true;
            }
            
            // Reset flag when fade completes
            if (directionFadeProgress >= 1.0 && !directionChangePending) {
                fadeStateSaved = false;
            }
            CRGB* newFrontLights = newDirection ? headlight : taillight;
            CRGB* newBackLights = newDirection ? taillight : headlight;
            uint8_t newFrontCount = newDirection ? headlightLedCount : taillightLedCount;
            uint8_t newBackCount = newDirection ? taillightLedCount : headlightLedCount;
            uint8_t newFrontLedType = newDirection ? headlightLedType : taillightLedType;
            uint8_t newBackLedType = newDirection ? taillightLedType : headlightLedType;
            uint8_t newFrontColorOrder = newDirection ? headlightColorOrder : taillightColorOrder;
            uint8_t newBackColorOrder = newDirection ? taillightColorOrder : headlightColorOrder;
            EffectTiming& newFrontTiming = newDirection ? headlightTiming : taillightTiming;
            EffectTiming& newBackTiming = newDirection ? taillightTiming : headlightTiming;
            
            // Render new direction effects to temporary arrays
            CRGB* newFrontTemp = new CRGB[newFrontCount];
            CRGB* newBackTemp = new CRGB[newBackCount];
            
            // Apply front light effect (headlight mode: solid white or effect)
            if (frontUpdate || backUpdate) {
                if (headlightMode == 0) {
                    // Solid white
                    fillSolidWithColorOrder(newFrontTemp, newFrontCount, CRGB::White, newFrontLedType, newFrontColorOrder);
                } else {
                    // Headlight effect
                    applyEffectToArray(newFrontTemp, newFrontCount, headlightEffect, headlightColor, newFrontTiming, newFrontLedType, newFrontColorOrder, headlightBackgroundColor, headlightBackgroundEnabled);
                }
                
                // Apply back light effect (always taillight effect)
                applyEffectToArray(newBackTemp, newBackCount, taillightEffect, taillightColor, newBackTiming, newBackLedType, newBackColorOrder, taillightBackgroundColor, taillightBackgroundEnabled);
            }
            
            // Blend old and new for both headlight and taillight
            // IMPORTANT: The physical arrays (headlight/taillight) don't change, only their roles do
            // When fading forward->backward:
            //   - Old front (headlight) becomes new back (headlight) - same physical array!
            //   - Old back (taillight) becomes new front (taillight) - same physical array!
            // So we always blend: headlightOld -> headlight, taillightOld -> taillight
            
            // Determine which new effect goes to which physical array:
            // - headlight array gets: newDirection's front effect (if forward) or back effect (if backward)
            // - taillight array gets: newDirection's back effect (if forward) or front effect (if backward)
            CRGB* newHeadlightEffect = newDirection ? newFrontTemp : newBackTemp;  // Forward: front, Backward: back
            CRGB* newTaillightEffect = newDirection ? newBackTemp : newFrontTemp;  // Forward: back, Backward: front
            uint8_t newHeadlightCount = newDirection ? newFrontCount : newBackCount;
            uint8_t newTaillightCount = newDirection ? newBackCount : newFrontCount;
            
            // Always blend headlightOld -> headlight, taillightOld -> taillight
            blendLEDArrays(headlight, headlightOld, newHeadlightEffect, newHeadlightCount, directionFadeProgress);
            blendLEDArrays(taillight, taillightOld, newTaillightEffect, newTaillightCount, directionFadeProgress);
            
            delete[] newFrontTemp;
            delete[] newBackTemp;
        } else {
            // Normal operation: apply effects based on current direction
            // Reset fade state saved flag when not in fade (so it's ready for next fade)
            fadeStateSaved = false;
            if (frontUpdate && frontCount > 0) {
                if (headlightMode == 0) {
                    // Solid white for headlight
                    fillSolidWithColorOrder(frontLights, frontCount, CRGB::White, frontLedType, frontColorOrder);
                } else {
                    // Headlight effect
                    applyEffectToArray(frontLights, frontCount, headlightEffect, headlightColor, frontTiming, frontLedType, frontColorOrder, headlightBackgroundColor, headlightBackgroundEnabled);
                }
            }
            
            if (backUpdate && backCount > 0) {
                // Taillight always gets taillight effect
                applyEffectToArray(backLights, backCount, taillightEffect, taillightColor, backTiming, backLedType, backColorOrder, taillightBackgroundColor, taillightBackgroundEnabled);
            }
        }
    } else {
        // Normal mode: effects apply to fixed headlight/taillight
        // Update headlight effect (only if timing allows)
        if (headlightUpdate) {
            applyEffectToArray(headlight, headlightLedCount, headlightEffect, headlightColor, headlightTiming, headlightLedType, headlightColorOrder, headlightBackgroundColor, headlightBackgroundEnabled);
        }
        
        // Update taillight effect (only if timing allows)
        if (taillightUpdate) {
            applyEffectToArray(taillight, taillightLedCount, taillightEffect, taillightColor, taillightTiming, taillightLedType, taillightColorOrder, taillightBackgroundColor, taillightBackgroundEnabled);
        }
    }

    // Priority 4: Braking effects (override taillight after base effects)
    if (brakingActive) {
        showBrakingEffect();
    }

    // Priority 5: Blinker effects (override base colors while active)
    if (blinkerActive) {
        fillSolidWithColorOrder(headlight, headlightLedCount, CRGB::White, headlightLedType, headlightColorOrder);
        fillSolidWithColorOrder(taillight, taillightLedCount, CRGB::Red, taillightLedType, taillightColorOrder);
        showBlinkerEffect(blinkerDirection);
    }
    
    FastLED.show();
}

CRGB getEffectBackgroundColor() {
    return effectBackgroundEnabled ? effectBackgroundColor : CRGB::Black;
}

CRGB mixColors(const CRGB& base, const CRGB& added) {
    if (base.r == 0 && base.g == 0 && base.b == 0) {
        return added;
    }
    if (added.r == 0 && added.g == 0 && added.b == 0) {
        return base;
    }

    CHSV baseHsv = rgb2hsv_approximate(base);
    CHSV addedHsv = rgb2hsv_approximate(added);

    uint16_t valueSum = baseHsv.value + addedHsv.value;
    uint8_t weight = valueSum > 0 ? static_cast<uint8_t>((addedHsv.value * 255) / valueSum) : 128;
    uint8_t blendedHue = blend8(baseHsv.hue, addedHsv.hue, weight);
    uint8_t blendedSat = max(baseHsv.sat, addedHsv.sat);
    uint8_t blendedVal = max(baseHsv.val, addedHsv.val);

    return CHSV(blendedHue, blendedSat, blendedVal);
}

void effectBreath(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate breathing speed based on effectSpeed (0-255)
    // Higher speed = faster breathing
    uint16_t breathSpeed = map(effectSpeed, 0, 255, 15000, 1000); // 1s to 15s cycle (much slower)
    uint8_t breathe = (sin(millis() / (breathSpeed / 1000.0)) + 1) * 127;
    CRGB breatheColor = color;
    breatheColor.nscale8(breathe);
    fill_solid(leds, numLeds, breatheColor);
}

void effectRainbow(CRGB* leds, uint8_t numLeds) {
    // Calculate rainbow speed based on effectSpeed (0-255)
    uint16_t rainbowSpeed = map(effectSpeed, 0, 255, 1000, 50); // 50ms to 1000ms per step (much slower)
    uint16_t hue = (effectStep * 65536L / numLeds) + (millis() / rainbowSpeed);
    for (uint8_t i = 0; i < numLeds; i++) {
        uint16_t pixelHue = hue + (i * 65536L / numLeds);
        leds[i] = CHSV(pixelHue >> 8, 255, 255);
    }
    effectStep += 2;
}

// PEV-Friendly: Smooth Pulse Effect - rhythmic brightness pulsing like a heartbeat
void effectPulse(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate pulse timing based on effectSpeed (slower = longer pulse cycle)
    uint16_t pulseSpeed = map(effectSpeed, 0, 255, 4000, 500); // 500ms to 4s per cycle
    
    // Use sine wave for smooth pulsing
    uint16_t phase = (millis() % pulseSpeed) * 256 / pulseSpeed;
    uint8_t brightness = sin8(phase); // 0-255 sine wave
    
    // Ensure minimum visibility
    brightness = map(brightness, 0, 255, 40, 255);
    
    CRGB pulseColor = color;
    pulseColor.nscale8(brightness);
    fill_solid(leds, numLeds, pulseColor);
}

void effectBlinkRainbow(CRGB* leds, uint8_t numLeds) {
    // Calculate blink speed based on effectSpeed (0-255)
    uint16_t blinkSpeed = map(effectSpeed, 0, 255, 10000, 800); // 800ms to 10s (much slower)l
    bool blinkState = (millis() / blinkSpeed) % 2;
    if (blinkState) {
        effectRainbow(leds, numLeds);
    } else {
        fill_solid(leds, numLeds, getEffectBackgroundColor());
    }
}

// PEV-Friendly: Gradient Shift Effect - smooth color gradient that moves along the strip
void effectGradientShift(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate shift speed based on effectSpeed
    uint16_t shiftSpeed = map(effectSpeed, 0, 255, 8000, 1000); // 1s to 8s per cycle
    uint16_t phase = (millis() % shiftSpeed) * 256 / shiftSpeed;
    
    // Get hue from color for gradient variation
    CHSV hsv = rgb2hsv_approximate(color);
    
    for (uint8_t i = 0; i < numLeds; i++) {
        // Create smooth gradient across the strip
        uint8_t position = (i * 256 / numLeds + phase) % 256;
        
        // Vary brightness smoothly across the strip
        uint8_t brightness = sin8(position);
        brightness = map(brightness, 0, 255, 60, 255); // Minimum 60 brightness
        
        CRGB gradientColor = color;
        gradientColor.nscale8(brightness);
        leds[i] = gradientColor;
    }
}

// Fire Effect
void effectFire(CRGB* leds, uint8_t numLeds) {
    // Fire simulation with heat and cooling
    static uint8_t heat[200]; // Max LEDs supported
    
    // Calculate fire speed based on effectSpeed
    uint8_t cooling = map(effectSpeed, 0, 255, 50, 200);
    uint8_t sparking = map(effectSpeed, 0, 255, 50, 120);
    
    // Cool down every cell a little
    for (uint8_t i = 0; i < numLeds; i++) {
        heat[i] = qsub8(heat[i], random(0, ((cooling * 10) / numLeds) + 2));
    }
    
    // Heat from each cell drifts 'up' and diffuses a little
    for (uint8_t k = numLeds - 1; k >= 2; k--) {
        heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    }
    
    // Randomly ignite new 'sparks' near the bottom
    if (random(255) < sparking) {
        uint8_t y = random(7);
        heat[y] = qadd8(heat[y], random(160, 255));
    }
    
    // Convert heat to LED colors
    for (uint8_t j = 0; j < numLeds; j++) {
        CRGB color = HeatColor(heat[j]);
        leds[j] = color;
    }
}

// Meteor Effect
void effectMeteor(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Fade all LEDs
    for (uint8_t i = 0; i < numLeds; i++) {
        leds[i].nscale8(192); // Fade by 25%
    }
    
    // Calculate meteor speed
    uint8_t meteorSize = map(effectSpeed, 0, 255, 1, 5);
    uint8_t meteorPos = (effectStep / 2) % (numLeds + meteorSize);
    
    // Draw meteor
    for (uint8_t i = 0; i < meteorSize; i++) {
        if (meteorPos - i >= 0 && meteorPos - i < numLeds) {
            uint8_t brightness = 255 - (i * 255 / meteorSize);
            CRGB meteorColor = color;
            meteorColor.nscale8(brightness);
            leds[meteorPos - i] = meteorColor;
        }
    }
    
    effectStep++;
}

// Wave Effect
void effectWave(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate wave speed
    uint16_t waveSpeed = map(effectSpeed, 0, 255, 1000, 100);
    uint8_t wavePos = (millis() / waveSpeed) % (numLeds * 2);
    
    // Create wave pattern
    for (uint8_t i = 0; i < numLeds; i++) {
        uint8_t distance = abs(i - wavePos);
        if (distance > numLeds) distance = (numLeds * 2) - distance;
        
        uint8_t brightness = 255 - (distance * 255 / numLeds);
        if (brightness > 0) {
            CRGB waveColor = color;
            waveColor.nscale8(brightness);
            leds[i] = waveColor;
        } else {
            leds[i] = getEffectBackgroundColor();
        }
    }
}

// PEV-Friendly: Center Burst Effect - LEDs expand outward from center
void effectCenterBurst(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate burst speed based on effectSpeed
    uint16_t burstSpeed = map(effectSpeed, 0, 255, 3000, 500); // 500ms to 3s per cycle
    uint16_t phase = (millis() % burstSpeed) * 256 / burstSpeed;
    
    // Use sine wave for smooth expansion/contraction
    uint8_t expansion = sin8(phase); // 0-255
    uint8_t maxRadius = numLeds / 2;
    uint8_t radius = map(expansion, 0, 255, 0, maxRadius);
    
    uint8_t center = numLeds / 2;
    
    for (uint8_t i = 0; i < numLeds; i++) {
        uint8_t distance = abs((int)i - (int)center);
        
        if (distance <= radius) {
            // Inside the burst - calculate brightness based on distance from edge
            uint8_t edgeDistance = radius - distance;
            uint8_t brightness = map(edgeDistance, 0, radius > 0 ? radius : 1, 100, 255);
            CRGB burstColor = color;
            burstColor.nscale8(brightness);
            leds[i] = burstColor;
        } else {
            // Outside the burst
            leds[i] = getEffectBackgroundColor();
        }
    }
}

// Candle Effect
void effectCandle(CRGB* leds, uint8_t numLeds) {
    // Calculate candle flicker speed
    uint8_t flickerSpeed = map(effectSpeed, 0, 255, 50, 200);
    
    for (uint8_t i = 0; i < numLeds; i++) {
        // Base candle color (warm white/orange)
        CRGB baseColor = CRGB(255, 147, 41); // Warm orange
        
        // Add random flicker
        uint8_t flicker = random(0, flickerSpeed);
        uint8_t brightness = 200 + flicker;
        
        CRGB candleColor = baseColor;
        candleColor.nscale8(brightness);
        leds[i] = candleColor;
    }
}

// Static Rainbow Effect
void effectStaticRainbow(CRGB* leds, uint8_t numLeds) {
    // Static rainbow - no movement, just rainbow colors across the strip
    for (uint8_t i = 0; i < numLeds; i++) {
        uint8_t hue = (i * 255) / numLeds;
        leds[i] = CHSV(hue, 255, 255);
    }
}

// Electrolyte-style Knight Rider Effect (KITT scanner)
void effectKnightRider(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Fade all LEDs
    for (uint8_t i = 0; i < numLeds; i++) {
        leds[i].nscale8(200); // Fade by 22%
    }
    
    // Calculate scanner speed and size
    uint8_t scannerSize = map(effectSpeed / 2, 0, 255, 2, 6);
    uint8_t scannerPos = (effectStep / 2) % ((numLeds + scannerSize) * 2);
    
    // Determine direction (forward or backward)
    bool forward = (scannerPos < (numLeds + scannerSize));
    uint8_t actualPos = forward ? scannerPos : ((numLeds + scannerSize) * 2) - scannerPos - 1;
    
    // Draw scanner
    for (uint8_t i = 0; i < scannerSize; i++) {
        if (actualPos - i >= 0 && actualPos - i < numLeds) {
            uint8_t brightness = 255 - (i * 255 / scannerSize);
            CRGB scannerColor = color;
            scannerColor.nscale8(brightness);
            leds[actualPos - i] = scannerColor;
        }
    }
    
    effectStep++;
}

// Electrolyte-style Police Effect (red/blue alternating)
void effectPolice(CRGB* leds, uint8_t numLeds) {
    // Calculate police flash speed
    uint16_t flashSpeed = map(effectSpeed, 0, 255, 1000, 100);
    bool flashState = (millis() / flashSpeed) % 2;
    
    for (uint8_t i = 0; i < numLeds; i++) {
        if (flashState) {
            // Red on odd positions, blue on even
            leds[i] = (i % 2) ? CRGB::Red : CRGB::Blue;
        } else {
            // Blue on odd positions, red on even
            leds[i] = (i % 2) ? CRGB::Blue : CRGB::Red;
        }
    }
}

// Electrolyte-style Strobe Effect
void effectStrobe(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate strobe speed
    uint16_t strobeSpeed = map(effectSpeed, 0, 255, 2000, 50);
    bool strobeState = (millis() / strobeSpeed) % 2;
    
    if (strobeState) {
        fill_solid(leds, numLeds, color);
    } else {
        fill_solid(leds, numLeds, getEffectBackgroundColor());
    }
}

// Electrolyte-style Larson Scanner Effect
void effectLarsonScanner(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Fade all LEDs
    for (uint8_t i = 0; i < numLeds; i++) {
        leds[i].nscale8(220); // Fade by 14%
    }
    
    // Calculate scanner speed and size
    uint8_t scannerSize = map(effectSpeed, 0, 255, 1, 4);
    uint8_t scannerPos = (effectStep / 3) % ((numLeds + scannerSize) * 2);
    
    // Determine direction
    bool forward = (scannerPos < (numLeds + scannerSize));
    uint8_t actualPos = forward ? scannerPos : ((numLeds + scannerSize) * 2) - scannerPos - 1;
    
    // Draw scanner with fade
    for (uint8_t i = 0; i < scannerSize; i++) {
        if (actualPos - i >= 0 && actualPos - i < numLeds) {
            uint8_t brightness = 255 - (i * 200 / scannerSize);
            CRGB scannerColor = color;
            scannerColor.nscale8(brightness);
            leds[actualPos - i] = scannerColor;
        }
    }
    
    effectStep++;
}

// Electrolyte-style Color Wipe Effect
void effectColorWipe(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate wipe speed
    uint16_t wipeSpeed = map(effectSpeed, 0, 255, 2000, 200);
    uint8_t wipePos = (millis() / wipeSpeed) % (numLeds * 2);
    
    // Determine direction
    bool forward = (wipePos < numLeds);
    uint8_t actualPos = forward ? wipePos : (numLeds * 2) - wipePos - 1;
    
    // Clear all LEDs
    fill_solid(leds, numLeds, getEffectBackgroundColor());
    
    // Fill up to position
    for (uint8_t i = 0; i <= actualPos && i < numLeds; i++) {
        leds[i] = color;
    }
}

// Electrolyte-style Theater Chase Effect
// PEV-Friendly: Hazard Effect - alternating halves for high visibility
void effectHazard(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate flash rate based on effectSpeed (not too fast to avoid strobe)
    uint16_t flashRate = map(effectSpeed, 0, 255, 1500, 400); // 400ms to 1.5s per cycle
    bool firstHalf = ((millis() / flashRate) % 2) == 0;
    
    uint8_t midPoint = numLeds / 2;
    
    for (uint8_t i = 0; i < numLeds; i++) {
        bool isFirstHalf = (i < midPoint);
        
        if ((isFirstHalf && firstHalf) || (!isFirstHalf && !firstHalf)) {
            leds[i] = color;
        } else {
            // Dim the other half instead of turning off completely
            CRGB dimColor = color;
            dimColor.nscale8(40); // 15% brightness
            leds[i] = dimColor;
        }
    }
}

// Electrolyte-style Running Lights Effect
void effectRunningLights(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate running speed
    uint16_t runSpeed = map(effectSpeed, 0, 255, 2000, 200);
    uint8_t runPos = (millis() / runSpeed) % numLeds;
    
    // Clear all LEDs
    fill_solid(leds, numLeds, getEffectBackgroundColor());
    
    // Create running light pattern
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t pos = (runPos + i) % numLeds;
        uint8_t brightness = 255 - (i * 85); // Fade each light
        CRGB runColor = color;
        runColor.nscale8(brightness);
        leds[pos] = runColor;
    }
}

// Electrolyte-style Color Sweep Effect
void effectColorSweep(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Calculate sweep speed
    uint16_t sweepSpeed = map(effectSpeed, 0, 255, 3000, 300);
    uint8_t sweepPos = (millis() / sweepSpeed) % (numLeds * 2);
    
    // Determine direction
    bool forward = (sweepPos < numLeds);
    uint8_t actualPos = forward ? sweepPos : (numLeds * 2) - sweepPos - 1;
    
    // Create sweep pattern
    for (uint8_t i = 0; i < numLeds; i++) {
        uint8_t distance = abs(i - actualPos);
        if (distance < 5) {
            uint8_t brightness = 255 - (distance * 50);
            CRGB sweepColor = color;
            sweepColor.nscale8(brightness);
            leds[i] = sweepColor;
        } else {
            leds[i] = getEffectBackgroundColor();
        }
    }
}

// ============================================================================
// IMPROVED EFFECT FUNCTIONS WITH CONSISTENT TIMING
// ============================================================================

// Improved Breath Effect with consistent timing
void effectBreathImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Use step-based timing for consistent speed across different strip lengths
    uint8_t breathPhase = (step * 2) % 256; // 0-255 cycle
    uint8_t brightness = sin8(breathPhase);
    
    CRGB breathColor = color;
    breathColor.nscale8(brightness);
    fill_solid(leds, numLeds, breathColor);
}

// Improved Rainbow Effect with consistent timing
void effectRainbowImproved(CRGB* leds, uint8_t numLeds, uint16_t step) {
    // Use step-based timing for consistent rainbow speed
    // Apply speed multiplier: faster base speed for rainbow (multiply step by multiplier)
    uint8_t multiplier = getEffectSpeedMultiplier(FX_RAINBOW);
    uint16_t hueOffset = (step * multiplier) % 256; // Faster rainbow movement
    
    for (uint8_t i = 0; i < numLeds; i++) {
        uint8_t hue = (hueOffset + (i * 256 / numLeds)) % 256;
        leds[i] = CHSV(hue, 255, 255);
    }
}

// Improved Chase Effect with consistent timing
// PEV-Friendly: Improved Pulse Effect with consistent timing
void effectPulseImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Use step for consistent timing across synced devices
    uint8_t phase = (step * 4) % 256; // Smooth cycle
    uint8_t brightness = sin8(phase);
    
    // Ensure minimum visibility
    brightness = map(brightness, 0, 255, 40, 255);
    
    CRGB pulseColor = color;
    pulseColor.nscale8(brightness);
    fill_solid(leds, numLeds, pulseColor);
}

// Improved Blink Rainbow Effect with consistent timing
void effectBlinkRainbowImproved(CRGB* leds, uint8_t numLeds, uint16_t step) {
    // Blink every 20 steps (adjustable)
    bool blinkState = (step / 20) % 2;
    if (blinkState) {
        effectRainbowImproved(leds, numLeds, step);
    } else {
        fill_solid(leds, numLeds, getEffectBackgroundColor());
    }
}

// Improved Twinkle Effect with consistent timing
// PEV-Friendly: Improved Gradient Shift Effect with consistent timing
void effectGradientShiftImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Use step for consistent timing across synced devices
    uint8_t phase = (step * 2) % 256;
    
    for (uint8_t i = 0; i < numLeds; i++) {
        // Create smooth gradient across the strip
        uint8_t position = (i * 256 / numLeds + phase) % 256;
        
        // Vary brightness smoothly across the strip
        uint8_t brightness = sin8(position);
        brightness = map(brightness, 0, 255, 60, 255); // Minimum 60 brightness
        
        CRGB gradientColor = color;
        gradientColor.nscale8(brightness);
        leds[i] = gradientColor;
    }
}

// Improved Fire Effect with consistent timing
void effectFireImproved(CRGB* leds, uint8_t numLeds, uint16_t step) {
    static uint8_t heat[200]; // Max LEDs supported
    
    // Use step for consistent fire behavior
    uint8_t cooling = 50 + (step % 50);
    uint8_t sparking = 50 + (step % 70);
    
    // Cool down every cell a little
    for (uint8_t i = 0; i < numLeds; i++) {
        heat[i] = qsub8(heat[i], random(0, ((cooling * 10) / numLeds) + 2));
    }
    
    // Heat from each cell drifts 'up' and diffuses a little
    for (uint8_t k = numLeds - 1; k >= 2; k--) {
        heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    }
    
    // Randomly ignite new 'sparks' near the bottom
    if (random(255) < sparking) {
        uint8_t y = random(7);
        heat[y] = qadd8(heat[y], random(160, 255));
    }
    
    // Convert heat to LED colors
    for (uint8_t j = 0; j < numLeds; j++) {
        CRGB color = HeatColor(heat[j]);
        leds[j] = color;
    }
}

// Improved Meteor Effect with consistent timing
void effectMeteorImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Fade all LEDs
    for (uint8_t i = 0; i < numLeds; i++) {
        leds[i].nscale8(192); // Fade by 25%
    }
    
    // Apply speed multiplier for faster movement
    uint8_t multiplier = getEffectSpeedMultiplier(FX_METEOR);
    // Use step for consistent meteor movement
    uint8_t meteorSize = 3 + (step % 3);
    uint8_t meteorPos = ((step * multiplier) / 2) % (numLeds + meteorSize);
    
    // Draw meteor
    for (uint8_t i = 0; i < meteorSize; i++) {
        if (meteorPos - i >= 0 && meteorPos - i < numLeds) {
            uint8_t brightness = 255 - (i * 255 / meteorSize);
            CRGB meteorColor = color;
            meteorColor.nscale8(brightness);
            leds[meteorPos - i] = meteorColor;
        }
    }
}

// Improved Wave Effect with consistent timing
void effectWaveImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Apply speed multiplier for faster movement
    uint8_t multiplier = getEffectSpeedMultiplier(FX_WAVE);
    // Use step for consistent wave movement
    uint8_t wavePos = ((step * multiplier) / 2) % (numLeds * 2);
    
    // Create wave pattern
    for (uint8_t i = 0; i < numLeds; i++) {
        uint8_t distance = abs(i - wavePos);
        if (distance > numLeds) distance = (numLeds * 2) - distance;
        
        uint8_t brightness = 255 - (distance * 255 / numLeds);
        if (brightness > 0) {
            CRGB waveColor = color;
            waveColor.nscale8(brightness);
            leds[i] = waveColor;
        } else {
            leds[i] = getEffectBackgroundColor();
        }
    }
}

// Improved Comet Effect with consistent timing
// PEV-Friendly: Improved Center Burst Effect with consistent timing
void effectCenterBurstImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Use step for consistent timing across synced devices
    uint8_t phase = (step * 3) % 256;
    
    // Use sine wave for smooth expansion/contraction
    uint8_t expansion = sin8(phase);
    uint8_t maxRadius = numLeds / 2;
    uint8_t radius = map(expansion, 0, 255, 0, maxRadius);
    
    uint8_t center = numLeds / 2;
    
    for (uint8_t i = 0; i < numLeds; i++) {
        uint8_t distance = abs((int)i - (int)center);
        
        if (distance <= radius) {
            // Inside the burst - calculate brightness based on distance from edge
            uint8_t edgeDistance = radius - distance;
            uint8_t brightness = map(edgeDistance, 0, radius > 0 ? radius : 1, 100, 255);
            CRGB burstColor = color;
            burstColor.nscale8(brightness);
            leds[i] = burstColor;
        } else {
            // Outside the burst
            leds[i] = getEffectBackgroundColor();
        }
    }
}

// Improved Candle Effect with consistent timing
void effectCandleImproved(CRGB* leds, uint8_t numLeds, uint16_t step) {
    for (uint8_t i = 0; i < numLeds; i++) {
        // Base candle color (warm white/orange)
        CRGB baseColor = CRGB(255, 147, 41); // Warm orange
        
        // Use step for consistent flicker pattern
        uint8_t flicker = (step * 3 + i * 7) % 100;
        uint8_t brightness = 150 + flicker;
        
        CRGB candleColor = baseColor;
        candleColor.nscale8(brightness);
        leds[i] = candleColor;
    }
}

// Improved Knight Rider Effect with consistent timing (KITT scanner)
void effectKnightRiderImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Fade all LEDs to black (preserves color better than nscale8)
    // Use a slower fade to black to preserve color hue
    for (uint8_t i = 0; i < numLeds; i++) {
        // Fade each channel independently toward black (preserves color better)
        // Fade by ~25% per frame for smoother trail
        leds[i].r = (leds[i].r * 192) >> 8; // ~25% fade
        leds[i].g = (leds[i].g * 192) >> 8;
        leds[i].b = (leds[i].b * 192) >> 8;
    }
    
    // Calculate scanner position (smooth back-and-forth motion)
    // Slow down the scanner by dividing step (Knight Rider should be slower)
    // Allow scanner to go "off-screen" past the edges for authentic KITT effect
    // The scanner appears to leave the LED bar before turning around
    uint16_t trailLength = numLeds / 3;
    if (trailLength < 3) trailLength = 3;
    if (trailLength > 8) trailLength = 8;
    
    // Extend the cycle to allow scanner to go past edges
    // Forward: -trailLength to numLeds + trailLength
    // Backward: numLeds + trailLength to -trailLength
    uint16_t cycleLength = (numLeds + trailLength * 2) * 2;
    uint16_t position = (step / 4) % cycleLength; // Divide by 4 to slow down scanner
    
    // Determine direction and actual position (can be negative or beyond numLeds)
    int16_t scannerPos;
    bool forward;
    if (position < (numLeds + trailLength * 2)) {
        // Forward direction: goes from -trailLength to numLeds + trailLength
        scannerPos = (int16_t)position - trailLength;
        forward = true;
    } else {
        // Backward direction: goes from numLeds + trailLength to -trailLength
        scannerPos = (int16_t)(cycleLength - position) - trailLength;
        forward = false;
    }
    
    // Draw trail first (behind scanner) - only draw visible parts
    for (uint8_t i = 1; i <= trailLength; i++) {
        int16_t trailPos;
        
        if (forward) {
            // Trail behind (to the left when going forward)
            trailPos = scannerPos - i;
        } else {
            // Trail behind (to the right when going backward)
            trailPos = scannerPos + i;
        }
        
        // Only draw if position is visible (within LED bounds)
        if (trailPos >= 0 && trailPos < numLeds) {
            // Exponential fade for smooth trail (brighter closer to main light)
            // Use a curve: brightness = 255 * (1 - (i/trailLength)^2)
            float fadeRatio = (float)i / trailLength;
            uint8_t brightness = 255 * (1.0 - fadeRatio * fadeRatio);
            
            // Scale color channels independently to preserve hue
            CRGB trailColor;
            trailColor.r = (color.r * brightness) >> 8;
            trailColor.g = (color.g * brightness) >> 8;
            trailColor.b = (color.b * brightness) >> 8;
            leds[trailPos] = trailColor; // Overwrite (not blend) to prevent color mixing
        }
    }
    
    // Main scanner light (bright center) - only draw if visible
    if (scannerPos >= 0 && scannerPos < numLeds) {
        leds[scannerPos] = color; // Full brightness main light
    }
}

// Improved Police Effect with consistent timing
void effectPoliceImproved(CRGB* leds, uint8_t numLeds, uint16_t step) {
    // Use step for consistent flash pattern
    bool flashState = (step / 10) % 2;
    
    for (uint8_t i = 0; i < numLeds; i++) {
        if (flashState) {
            // Red on odd positions, blue on even
            leds[i] = (i % 2) ? CRGB::Red : CRGB::Blue;
        } else {
            // Blue on odd positions, red on even
            leds[i] = (i % 2) ? CRGB::Blue : CRGB::Red;
        }
    }
}

// Improved Strobe Effect with consistent timing
void effectStrobeImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Use step for consistent strobe pattern
    bool strobeState = (step / 5) % 2;
    
    if (strobeState) {
        fill_solid(leds, numLeds, color);
    } else {
        fill_solid(leds, numLeds, getEffectBackgroundColor());
    }
}

// Improved Larson Scanner Effect with consistent timing
void effectLarsonScannerImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Fade all LEDs
    for (uint8_t i = 0; i < numLeds; i++) {
        leds[i].nscale8(220); // Fade by 14%
    }
    
    // Use step for consistent scanner movement
    uint8_t scannerSize = 2 + (step % 2);
    uint8_t scannerPos = (step / 3) % ((numLeds + scannerSize) * 2);
    
    // Determine direction
    bool forward = (scannerPos < (numLeds + scannerSize));
    uint8_t actualPos = forward ? scannerPos : ((numLeds + scannerSize) * 2) - scannerPos - 1;
    
    // Draw scanner with fade
    for (uint8_t i = 0; i < scannerSize; i++) {
        if (actualPos - i >= 0 && actualPos - i < numLeds) {
            uint8_t brightness = 255 - (i * 200 / scannerSize);
            CRGB scannerColor = color;
            scannerColor.nscale8(brightness);
            leds[actualPos - i] = scannerColor;
        }
    }
}

// Improved Color Wipe Effect with consistent timing
void effectColorWipeImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Apply speed multiplier for faster movement
    uint8_t multiplier = getEffectSpeedMultiplier(FX_COLOR_WIPE);
    // Use step for consistent wipe movement
    uint8_t wipePos = ((step * multiplier) / 3) % (numLeds * 2);
    
    // Determine direction
    bool forward = (wipePos < numLeds);
    uint8_t actualPos = forward ? wipePos : (numLeds * 2) - wipePos - 1;
    
    // Clear all LEDs
    fill_solid(leds, numLeds, getEffectBackgroundColor());
    
    // Fill up to position
    for (uint8_t i = 0; i <= actualPos && i < numLeds; i++) {
        leds[i] = color;
    }
}

// Rainbow Color Wipe Effect (single-direction sweep, alternating direction)
void effectRainbowWipeImproved(CRGB* leds, uint8_t numLeds, uint16_t step) {
    uint8_t multiplier = getEffectSpeedMultiplier(FX_RAINBOW_WIPE);
    uint16_t sweepStep = (step * multiplier) / 3;
    uint16_t sweepIndex = sweepStep / numLeds;
    uint8_t pos = sweepStep % numLeds;
    bool forward = (sweepIndex % 2 == 0);

    uint8_t prevHue = ((sweepIndex - 1) * 57 + 23) & 0xFF;
    uint8_t currHue = (sweepIndex * 57 + 23) & 0xFF;
    if (abs((int)prevHue - (int)currHue) < 32) {
        currHue = (currHue + 64) & 0xFF;
    }

    CRGB backgroundColor = CHSV(prevHue, 255, 255);
    CRGB wipeColor = CHSV(currHue, 255, 255);
    fill_solid(leds, numLeds, backgroundColor);

    if (forward) {
        for (uint8_t i = 0; i <= pos && i < numLeds; i++) {
            leds[i] = wipeColor;
        }
    } else {
        uint8_t actualPos = (numLeds - 1) - pos;
        for (uint8_t i = actualPos; i < numLeds; i++) {
            leds[i] = wipeColor;
        }
    }
}

// Improved Theater Chase Effect with consistent timing
// PEV-Friendly: Improved Hazard Effect with consistent timing
void effectHazardImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Use step for consistent timing across synced devices
    bool firstHalf = ((step / 15) % 2) == 0; // Toggle every ~15 steps
    
    uint8_t midPoint = numLeds / 2;
    
    for (uint8_t i = 0; i < numLeds; i++) {
        bool isFirstHalf = (i < midPoint);
        
        if ((isFirstHalf && firstHalf) || (!isFirstHalf && !firstHalf)) {
            leds[i] = color;
        } else {
            // Dim the other half instead of turning off completely
            CRGB dimColor = color;
            dimColor.nscale8(40); // 15% brightness
            leds[i] = dimColor;
        }
    }
}

// Improved Running Lights Effect with consistent timing
void effectRunningLightsImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Apply speed multiplier for faster movement
    uint8_t multiplier = getEffectSpeedMultiplier(FX_RUNNING_LIGHTS);
    // Use step for consistent running movement
    uint8_t runPos = ((step * multiplier) / 2) % numLeds;
    
    // Clear all LEDs
    fill_solid(leds, numLeds, getEffectBackgroundColor());
    
    // Create running light pattern
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t pos = (runPos + i) % numLeds;
        uint8_t brightness = 255 - (i * 85); // Fade each light
        CRGB runColor = color;
        runColor.nscale8(brightness);
        leds[pos] = runColor;
    }
}

// Improved Color Sweep Effect with consistent timing
void effectColorSweepImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    // Apply speed multiplier for faster movement
    uint8_t multiplier = getEffectSpeedMultiplier(FX_COLOR_SWEEP);
    // Use step for consistent sweep movement
    uint8_t sweepPos = ((step * multiplier) / 2) % (numLeds * 2);
    
    // Determine direction
    bool forward = (sweepPos < numLeds);
    uint8_t actualPos = forward ? sweepPos : (numLeds * 2) - sweepPos - 1;
    
    // Create sweep pattern
    for (uint8_t i = 0; i < numLeds; i++) {
        uint8_t distance = abs(i - actualPos);
        if (distance < 5) {
            uint8_t brightness = 255 - (distance * 50);
            CRGB sweepColor = color;
            sweepColor.nscale8(brightness);
            leds[i] = sweepColor;
        } else {
            leds[i] = getEffectBackgroundColor();
        }
    }
}

void effectRainbowKnightRiderImproved(CRGB* leds, uint8_t numLeds, uint16_t step) {
    static bool lastForward = true;
    static CRGB currentColor = CRGB::Red;

    uint16_t trailLength = numLeds / 3;
    if (trailLength < 3) trailLength = 3;
    if (trailLength > 8) trailLength = 8;

    uint16_t cycleLength = (numLeds + trailLength * 2) * 2;
    uint16_t position = (step / 4) % cycleLength;

    bool forward = (position < (numLeds + trailLength * 2));
    int16_t scannerPos;
    if (forward) {
        scannerPos = (int16_t)position - trailLength;
    } else {
        scannerPos = (int16_t)(cycleLength - position) - trailLength;
    }

    if (forward != lastForward) {
        currentColor = CHSV(random8(), 255, 255);
        lastForward = forward;
    }

    fill_solid(leds, numLeds, getEffectBackgroundColor());

    for (uint8_t i = 1; i <= trailLength; i++) {
        int16_t trailPos = forward ? (scannerPos - i) : (scannerPos + i);
        if (trailPos >= 0 && trailPos < numLeds) {
            float fadeRatio = (float)i / trailLength;
            uint8_t brightness = 255 * (1.0 - fadeRatio * fadeRatio);
            CRGB trailColor;
            trailColor.r = (currentColor.r * brightness) >> 8;
            trailColor.g = (currentColor.g * brightness) >> 8;
            trailColor.b = (currentColor.b * brightness) >> 8;
            leds[trailPos] = trailColor;
        }
    }

    if (scannerPos >= 0 && scannerPos < numLeds) {
        leds[scannerPos] = currentColor;
    }
}

void effectDualKnightRiderImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step) {
    CRGB secondaryColor = getEffectBackgroundColor();

    uint16_t trailLength = numLeds;
    if (trailLength < 4) trailLength = 4;
    if (trailLength > 16) trailLength = 16;

    uint16_t cycleLength = (numLeds + trailLength * 2) * 2;
    uint16_t position = (step / 4) % cycleLength;

    bool forward = (position < (numLeds + trailLength * 2));
    bool oppositeForward = !forward;

    int16_t primaryPos = forward
        ? (int16_t)position - trailLength
        : (int16_t)(cycleLength - position) - trailLength;

    int16_t posMin = -static_cast<int16_t>(trailLength);
    int16_t posMax = static_cast<int16_t>(numLeds - 1 + trailLength);
    int16_t secondaryPos = posMin + posMax - primaryPos;

    fill_solid(leds, numLeds, CRGB::Black);

    for (uint8_t i = 1; i <= trailLength; i++) {
        float fadeRatio = (float)i / trailLength;
        uint8_t brightness = 255 * (1.0 - sqrtf(fadeRatio));

        int16_t primaryTrail = forward ? (primaryPos - i) : (primaryPos + i);
        if (primaryTrail >= 0 && primaryTrail < numLeds) {
            CRGB trailColor;
            trailColor.r = (color.r * brightness) >> 8;
            trailColor.g = (color.g * brightness) >> 8;
            trailColor.b = (color.b * brightness) >> 8;
            leds[primaryTrail] = mixColors(leds[primaryTrail], trailColor);
        }

        int16_t secondaryTrail = oppositeForward ? (secondaryPos - i) : (secondaryPos + i);
        if (secondaryTrail >= 0 && secondaryTrail < numLeds) {
            CRGB trailColor;
            trailColor.r = (secondaryColor.r * brightness) >> 8;
            trailColor.g = (secondaryColor.g * brightness) >> 8;
            trailColor.b = (secondaryColor.b * brightness) >> 8;
            leds[secondaryTrail] = mixColors(leds[secondaryTrail], trailColor);
        }
    }

    if (primaryPos >= 0 && primaryPos < numLeds) {
        leds[primaryPos] = mixColors(leds[primaryPos], color);
    }
    if (secondaryPos >= 0 && secondaryPos < numLeds) {
        leds[secondaryPos] = mixColors(leds[secondaryPos], secondaryColor);
    }
}

void effectDualRainbowKnightRiderImproved(CRGB* leds, uint8_t numLeds, uint16_t step) {
    static bool lastForward = true;
    static bool lastOppositeForward = false;
    static CRGB primaryColor = CHSV(0, 255, 255);
    static CRGB secondaryColor = CHSV(160, 255, 255);

    uint16_t trailLength = numLeds;
    if (trailLength < 4) trailLength = 4;
    if (trailLength > 16) trailLength = 16;

    uint16_t cycleLength = (numLeds + trailLength * 2) * 2;
    uint16_t position = (step / 4) % cycleLength;

    bool forward = (position < (numLeds + trailLength * 2));
    bool oppositeForward = !forward;

    int16_t primaryPos = forward
        ? (int16_t)position - trailLength
        : (int16_t)(cycleLength - position) - trailLength;

    int16_t posMin = -static_cast<int16_t>(trailLength);
    int16_t posMax = static_cast<int16_t>(numLeds - 1 + trailLength);
    int16_t secondaryPos = posMin + posMax - primaryPos;

    if (forward != lastForward) {
        primaryColor = CHSV(random8(), 255, 255);
        lastForward = forward;
    }
    if (oppositeForward != lastOppositeForward) {
        secondaryColor = CHSV(random8(), 255, 255);
        lastOppositeForward = oppositeForward;
    }

    fill_solid(leds, numLeds, CRGB::Black);

    for (uint8_t i = 1; i <= trailLength; i++) {
        float fadeRatio = (float)i / trailLength;
        uint8_t brightness = 255 * (1.0 - sqrtf(fadeRatio));

        int16_t primaryTrail = forward ? (primaryPos - i) : (primaryPos + i);
        if (primaryTrail >= 0 && primaryTrail < numLeds) {
            CRGB trailColor;
            trailColor.r = (primaryColor.r * brightness) >> 8;
            trailColor.g = (primaryColor.g * brightness) >> 8;
            trailColor.b = (primaryColor.b * brightness) >> 8;
            leds[primaryTrail] = mixColors(leds[primaryTrail], trailColor);
        }

        int16_t secondaryTrail = oppositeForward ? (secondaryPos - i) : (secondaryPos + i);
        if (secondaryTrail >= 0 && secondaryTrail < numLeds) {
            CRGB trailColor;
            trailColor.r = (secondaryColor.r * brightness) >> 8;
            trailColor.g = (secondaryColor.g * brightness) >> 8;
            trailColor.b = (secondaryColor.b * brightness) >> 8;
            leds[secondaryTrail] = mixColors(leds[secondaryTrail], trailColor);
        }
    }

    if (primaryPos >= 0 && primaryPos < numLeds) {
        leds[primaryPos] = mixColors(leds[primaryPos], primaryColor);
    }
    if (secondaryPos >= 0 && secondaryPos < numLeds) {
        leds[secondaryPos] = mixColors(leds[secondaryPos], secondaryColor);
    }
}

void setPreset(uint8_t preset) {
    if (preset >= presetCount) return;
    currentPreset = preset;

    PresetConfig& config = presets[preset];
    globalBrightness = config.brightness;
    effectSpeed = config.effectSpeed;
    headlightEffect = config.headlightEffect;
    taillightEffect = config.taillightEffect;
    headlightColor = CRGB(config.headlightColor[0], config.headlightColor[1], config.headlightColor[2]);
    taillightColor = CRGB(config.taillightColor[0], config.taillightColor[1], config.taillightColor[2]);
    headlightBackgroundEnabled = config.headlightBackgroundEnabled;
    taillightBackgroundEnabled = config.taillightBackgroundEnabled;
    headlightBackgroundColor = CRGB(config.headlightBackgroundColor[0], config.headlightBackgroundColor[1], config.headlightBackgroundColor[2]);
    taillightBackgroundColor = CRGB(config.taillightBackgroundColor[0], config.taillightBackgroundColor[1], config.taillightBackgroundColor[2]);

    Serial.printf("Preset applied: %s (index %d)\n", config.name, preset);
    FastLED.setBrightness(globalBrightness);
}

void captureCurrentPreset(PresetConfig& preset) {
    preset.brightness = globalBrightness;
    preset.effectSpeed = effectSpeed;
    preset.headlightEffect = headlightEffect;
    preset.taillightEffect = taillightEffect;
    preset.headlightColor[0] = headlightColor.r;
    preset.headlightColor[1] = headlightColor.g;
    preset.headlightColor[2] = headlightColor.b;
    preset.taillightColor[0] = taillightColor.r;
    preset.taillightColor[1] = taillightColor.g;
    preset.taillightColor[2] = taillightColor.b;
    preset.headlightBackgroundEnabled = headlightBackgroundEnabled;
    preset.taillightBackgroundEnabled = taillightBackgroundEnabled;
    preset.headlightBackgroundColor[0] = headlightBackgroundColor.r;
    preset.headlightBackgroundColor[1] = headlightBackgroundColor.g;
    preset.headlightBackgroundColor[2] = headlightBackgroundColor.b;
    preset.taillightBackgroundColor[0] = taillightBackgroundColor.r;
    preset.taillightBackgroundColor[1] = taillightBackgroundColor.g;
    preset.taillightBackgroundColor[2] = taillightBackgroundColor.b;
}

void initDefaultPresets() {
    presetCount = 0;
    auto addDefault = [&](const char* name, uint8_t brightness, uint8_t effectSpeedValue,
                          uint8_t headEffect, uint8_t tailEffect,
                          const CRGB& headColor, const CRGB& tailColor) {
        if (presetCount >= MAX_PRESETS) return;
        PresetConfig& preset = presets[presetCount];
        strncpy(preset.name, name, sizeof(preset.name) - 1);
        preset.name[sizeof(preset.name) - 1] = '\0';
        preset.brightness = brightness;
        preset.effectSpeed = effectSpeedValue;
        preset.headlightEffect = headEffect;
        preset.taillightEffect = tailEffect;
        preset.headlightColor[0] = headColor.r;
        preset.headlightColor[1] = headColor.g;
        preset.headlightColor[2] = headColor.b;
        preset.taillightColor[0] = tailColor.r;
        preset.taillightColor[1] = tailColor.g;
        preset.taillightColor[2] = tailColor.b;
        preset.headlightBackgroundEnabled = headlightBackgroundEnabled;
        preset.taillightBackgroundEnabled = taillightBackgroundEnabled;
        preset.headlightBackgroundColor[0] = headlightBackgroundColor.r;
        preset.headlightBackgroundColor[1] = headlightBackgroundColor.g;
        preset.headlightBackgroundColor[2] = headlightBackgroundColor.b;
        preset.taillightBackgroundColor[0] = taillightBackgroundColor.r;
        preset.taillightBackgroundColor[1] = taillightBackgroundColor.g;
        preset.taillightBackgroundColor[2] = taillightBackgroundColor.b;
        presetCount++;
    };

    addDefault("Standard", 200, 64, FX_SOLID, FX_SOLID, CRGB::White, CRGB::Red);
    addDefault("Night", 255, 64, FX_SOLID, FX_BREATH, CRGB::White, CRGB::Red);
    addDefault("Party", 180, 64, FX_SOLID, FX_RAINBOW, CRGB::White, CRGB::Black);
    addDefault("Stealth", 50, 64, FX_SOLID, FX_SOLID, CRGB(50, 50, 50), CRGB(20, 0, 0));
}

void restoreDefaultsToStock() {
    Serial.println("🔄 Restoring all settings to stock defaults...");

    // Lights
    currentPreset = PRESET_STANDARD;
    globalBrightness = DEFAULT_BRIGHTNESS;
    effectSpeed = 64;
    headlightColor = CRGB::White;
    taillightColor = CRGB::Red;
    headlightEffect = FX_SOLID;
    taillightEffect = FX_SOLID;
    headlightBackgroundEnabled = false;
    taillightBackgroundEnabled = false;
    headlightBackgroundColor = CRGB::Black;
    taillightBackgroundColor = CRGB::Black;
    headlightMode = 0;

    // Startup
    startupSequence = STARTUP_POWER_ON;
    startupEnabled = true;
    startupDuration = 3000;

    // Motion
    motionEnabled = true;
    blinkerEnabled = true;
    parkModeEnabled = true;
    impactDetectionEnabled = true;
    motionSensitivity = 1.0f;
    blinkerDelay = 300;
    blinkerTimeout = 2000;
    parkAccelNoiseThreshold = 0.05f;
    parkGyroNoiseThreshold = 2.5f;
    parkStationaryTime = 2000;
    directionBasedLighting = false;
    forwardAccelThreshold = 0.3f;
    brakingEnabled = false;
    brakingThreshold = -0.5f;
    brakingEffect = 0;
    brakingBrightness = 255;

    // Park mode
    parkEffect = FX_BREATH;
    parkEffectSpeed = 64;
    parkHeadlightColor = CRGB::Blue;
    parkTaillightColor = CRGB::Blue;
    parkBrightness = 128;

    // RGBW
    whiteLEDsEnabled = false;
    rgbwWhiteMode = 0;

    // LED config
    headlightLedCount = 11;
    taillightLedCount = 11;
    headlightLedType = 0;
    taillightLedType = 0;
    headlightColorOrder = 1;
    taillightColorOrder = 1;

    // WiFi - unique name from MAC, password stays default
    apName = getDefaultApName();
    bluetoothDeviceName = apName;
    apPassword = "float420";

    // ESPNow
    enableESPNow = true;
    useESPNowSync = true;
    espNowChannel = 1;

    // Group
    groupCode = "";
    isGroupMaster = false;
    allowGroupJoin = false;
    deviceName = "";

    // Calibration
    resetCalibration();

    // Presets
    initDefaultPresets();

    // Re-initialize LEDs with new counts
    initializeLEDs();
    applyRgbwWhiteChannelMode();

    saveSettings();
    Serial.printf("✅ Stock defaults restored. AP/BLE: %s (restart required)\n", apName.c_str());
}

bool addPreset(const String& name) {
    if (presetCount >= MAX_PRESETS) return false;
    PresetConfig& preset = presets[presetCount];
    captureCurrentPreset(preset);
    strncpy(preset.name, name.c_str(), sizeof(preset.name) - 1);
    preset.name[sizeof(preset.name) - 1] = '\0';
    presetCount++;
    return true;
}

bool updatePreset(uint8_t index, const String& name) {
    if (index >= presetCount) return false;
    PresetConfig& preset = presets[index];
    captureCurrentPreset(preset);
    if (name.length() > 0) {
        strncpy(preset.name, name.c_str(), sizeof(preset.name) - 1);
        preset.name[sizeof(preset.name) - 1] = '\0';
    }
    return true;
}

bool deletePreset(uint8_t index) {
    if (index >= presetCount) return false;
    if (presetCount <= 1) return false;
    for (uint8_t i = index; i + 1 < presetCount; i++) {
        presets[i] = presets[i + 1];
    }
    presetCount--;
    if (currentPreset >= presetCount) {
        currentPreset = presetCount > 0 ? presetCount - 1 : 0;
    }
    return true;
}

void loadPresetsFromDoc(const JsonDocument& doc) {
    presetCount = 0;
    if (doc.containsKey("presets")) {
        JsonArrayConst presetsArray = doc["presets"].as<JsonArrayConst>();
        for (JsonVariantConst presetVar : presetsArray) {
            if (presetCount >= MAX_PRESETS) break;
            JsonObjectConst presetObj = presetVar.as<JsonObjectConst>();
            PresetConfig& preset = presets[presetCount];
            const char* nameValue = presetObj["name"] | "";
            if (nameValue[0] == '\0') {
                String defaultName = String("Preset ") + String(presetCount + 1);
                strncpy(preset.name, defaultName.c_str(), sizeof(preset.name) - 1);
            } else {
                strncpy(preset.name, nameValue, sizeof(preset.name) - 1);
            }
            preset.name[sizeof(preset.name) - 1] = '\0';
            preset.brightness = presetObj["brightness"] | DEFAULT_BRIGHTNESS;
            preset.effectSpeed = presetObj["effectSpeed"] | effectSpeed;
            preset.headlightEffect = presetObj["headlightEffect"] | FX_SOLID;
            preset.taillightEffect = presetObj["taillightEffect"] | FX_SOLID;
            preset.headlightColor[0] = presetObj["headlightColor_r"] | headlightColor.r;
            preset.headlightColor[1] = presetObj["headlightColor_g"] | headlightColor.g;
            preset.headlightColor[2] = presetObj["headlightColor_b"] | headlightColor.b;
            preset.taillightColor[0] = presetObj["taillightColor_r"] | taillightColor.r;
            preset.taillightColor[1] = presetObj["taillightColor_g"] | taillightColor.g;
            preset.taillightColor[2] = presetObj["taillightColor_b"] | taillightColor.b;
            preset.headlightBackgroundEnabled = presetObj["headlightBackgroundEnabled"] | headlightBackgroundEnabled;
            preset.taillightBackgroundEnabled = presetObj["taillightBackgroundEnabled"] | taillightBackgroundEnabled;
            preset.headlightBackgroundColor[0] = presetObj["headlightBackgroundColor_r"] | headlightBackgroundColor.r;
            preset.headlightBackgroundColor[1] = presetObj["headlightBackgroundColor_g"] | headlightBackgroundColor.g;
            preset.headlightBackgroundColor[2] = presetObj["headlightBackgroundColor_b"] | headlightBackgroundColor.b;
            preset.taillightBackgroundColor[0] = presetObj["taillightBackgroundColor_r"] | taillightBackgroundColor.r;
            preset.taillightBackgroundColor[1] = presetObj["taillightBackgroundColor_g"] | taillightBackgroundColor.g;
            preset.taillightBackgroundColor[2] = presetObj["taillightBackgroundColor_b"] | taillightBackgroundColor.b;
            presetCount++;
        }
    }

    if (presetCount == 0) {
        initDefaultPresets();
    }

    if (currentPreset >= presetCount) {
        currentPreset = 0;
    }
}

void savePresetsToDoc(JsonDocument& doc) {
    JsonArray presetsArray = doc.createNestedArray("presets");
    for (uint8_t i = 0; i < presetCount; i++) {
        JsonObject presetObj = presetsArray.createNestedObject();
        PresetConfig& preset = presets[i];
        presetObj["name"] = preset.name;
        presetObj["brightness"] = preset.brightness;
        presetObj["effectSpeed"] = preset.effectSpeed;
        presetObj["headlightEffect"] = preset.headlightEffect;
        presetObj["taillightEffect"] = preset.taillightEffect;
        presetObj["headlightColor_r"] = preset.headlightColor[0];
        presetObj["headlightColor_g"] = preset.headlightColor[1];
        presetObj["headlightColor_b"] = preset.headlightColor[2];
        presetObj["taillightColor_r"] = preset.taillightColor[0];
        presetObj["taillightColor_g"] = preset.taillightColor[1];
        presetObj["taillightColor_b"] = preset.taillightColor[2];
        presetObj["headlightBackgroundEnabled"] = preset.headlightBackgroundEnabled;
        presetObj["taillightBackgroundEnabled"] = preset.taillightBackgroundEnabled;
        presetObj["headlightBackgroundColor_r"] = preset.headlightBackgroundColor[0];
        presetObj["headlightBackgroundColor_g"] = preset.headlightBackgroundColor[1];
        presetObj["headlightBackgroundColor_b"] = preset.headlightBackgroundColor[2];
        presetObj["taillightBackgroundColor_r"] = preset.taillightBackgroundColor[0];
        presetObj["taillightBackgroundColor_g"] = preset.taillightBackgroundColor[1];
        presetObj["taillightBackgroundColor_b"] = preset.taillightBackgroundColor[2];
    }
}

// Startup Sequence Implementation
void startStartupSequence() {
    startupActive = true;
    startupStartTime = millis();
    startupStep = 0;
    Serial.printf("🎬 Starting %s sequence...\n", getStartupSequenceName(startupSequence).c_str());
}

void updateStartupSequence() {
    if (!startupActive) return;
    
    unsigned long elapsed = millis() - startupStartTime;
    
    // Check if sequence should end
    if (elapsed >= startupDuration) {
        startupActive = false;
        // Set final colors
        fill_solid(headlight, headlightLedCount, headlightColor);
        fill_solid(taillight, taillightLedCount, taillightColor);
        Serial.println("✅ Startup sequence complete!");
        return;
    }
    
    // Update sequence based on type
    switch (startupSequence) {
        case STARTUP_POWER_ON:
            startupPowerOn();
            break;
        case STARTUP_SCAN:
            startupScan();
            break;
        case STARTUP_WAVE:
            startupWave();
            break;
        case STARTUP_RACE:
            startupRace();
            break;
        case STARTUP_CUSTOM:
            startupCustom();
            break;
    }
    
    startupStep++;
}

void startupPowerOn() {
    // Progressive power-on effect - LEDs turn on from center outward
    uint8_t progress = map(millis() - startupStartTime, 0, startupDuration, 0, 255);
    
    // Headlight - center outward
    uint8_t headlightCenter = headlightLedCount / 2;
    uint8_t headlightRadius = map(progress, 0, 255, 0, headlightCenter);
    
    fill_solid(headlight, headlightLedCount, CRGB::Black);
    for (uint8_t i = 0; i < headlightLedCount; i++) {
        uint8_t distance = abs(i - headlightCenter);
        if (distance <= headlightRadius) {
            uint8_t brightness = map(distance, 0, headlightRadius, 255, 100);
            CRGB color = headlightColor;
            color.nscale8(brightness);
            headlight[i] = color;
        }
    }
    
    // Taillight - center outward
    uint8_t taillightCenter = taillightLedCount / 2;
    uint8_t taillightRadius = map(progress, 0, 255, 0, taillightCenter);
    
    fill_solid(taillight, taillightLedCount, CRGB::Black);
    for (uint8_t i = 0; i < taillightLedCount; i++) {
        uint8_t distance = abs(i - taillightCenter);
        if (distance <= taillightRadius) {
            uint8_t brightness = map(distance, 0, taillightRadius, 255, 100);
            CRGB color = taillightColor;
            color.nscale8(brightness);
            taillight[i] = color;
        }
    }
}

void startupScan() {
    // KITT-style scanner effect
    uint16_t scanSpeed = startupDuration / 4; // 4 scans total
    uint8_t scanPhase = (millis() - startupStartTime) / scanSpeed;
    uint8_t scanPos = (millis() - startupStartTime) % scanSpeed;
    
    // Headlight scanner
    fill_solid(headlight, headlightLedCount, CRGB::Black);
    uint8_t headlightPos = map(scanPos, 0, scanSpeed, 0, headlightLedCount * 2);
    if (headlightPos >= headlightLedCount) headlightPos = (headlightLedCount * 2) - headlightPos - 1;
    
    for (uint8_t i = 0; i < 3; i++) {
        if (headlightPos - i >= 0 && headlightPos - i < headlightLedCount) {
            uint8_t brightness = 255 - (i * 85);
            CRGB color = headlightColor;
            color.nscale8(brightness);
            headlight[headlightPos - i] = color;
        }
    }
    
    // Taillight scanner
    fill_solid(taillight, taillightLedCount, CRGB::Black);
    uint8_t taillightPos = map(scanPos, 0, scanSpeed, 0, taillightLedCount * 2);
    if (taillightPos >= taillightLedCount) taillightPos = (taillightLedCount * 2) - taillightPos - 1;
    
    for (uint8_t i = 0; i < 3; i++) {
        if (taillightPos - i >= 0 && taillightPos - i < taillightLedCount) {
            uint8_t brightness = 255 - (i * 85);
            CRGB color = taillightColor;
            color.nscale8(brightness);
            taillight[taillightPos - i] = color;
        }
    }
}

void startupWave() {
    // Wave effect that builds up
    uint8_t progress = map(millis() - startupStartTime, 0, startupDuration, 0, 255);
    uint8_t waveCount = map(progress, 0, 255, 1, 4);
    
    // Headlight wave
    fill_solid(headlight, headlightLedCount, CRGB::Black);
    for (uint8_t wave = 0; wave < waveCount; wave++) {
        uint16_t wavePos = (startupStep * 2 + wave * (headlightLedCount / waveCount)) % (headlightLedCount * 2);
        if (wavePos >= headlightLedCount) wavePos = (headlightLedCount * 2) - wavePos - 1;
        
        for (uint8_t i = 0; i < 5; i++) {
            if (wavePos - i >= 0 && wavePos - i < headlightLedCount) {
                uint8_t brightness = 255 - (i * 50);
                CRGB color = headlightColor;
                color.nscale8(brightness);
                headlight[wavePos - i] = color;
            }
        }
    }
    
    // Taillight wave
    fill_solid(taillight, taillightLedCount, CRGB::Black);
    for (uint8_t wave = 0; wave < waveCount; wave++) {
        uint16_t wavePos = (startupStep * 2 + wave * (taillightLedCount / waveCount)) % (taillightLedCount * 2);
        if (wavePos >= taillightLedCount) wavePos = (taillightLedCount * 2) - wavePos - 1;
        
        for (uint8_t i = 0; i < 5; i++) {
            if (wavePos - i >= 0 && wavePos - i < taillightLedCount) {
                uint8_t brightness = 255 - (i * 50);
                CRGB color = taillightColor;
                color.nscale8(brightness);
                taillight[wavePos - i] = color;
            }
        }
    }
}

void startupRace() {
    // Racing lights effect - LEDs chase around the strip
    uint16_t raceSpeed = startupDuration / 6; // 6 laps total
    uint8_t racePos = (millis() - startupStartTime) % raceSpeed;
    
    // Headlight race
    fill_solid(headlight, headlightLedCount, CRGB::Black);
    uint8_t headlightPos = map(racePos, 0, raceSpeed, 0, headlightLedCount);
    
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t pos = (headlightPos + i) % headlightLedCount;
        uint8_t brightness = 255 - (i * 60);
        CRGB color = headlightColor;
        color.nscale8(brightness);
        headlight[pos] = color;
    }
    
    // Taillight race
    fill_solid(taillight, taillightLedCount, CRGB::Black);
    uint8_t taillightPos = map(racePos, 0, raceSpeed, 0, taillightLedCount);
    
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t pos = (taillightPos + i) % taillightLedCount;
        uint8_t brightness = 255 - (i * 60);
        CRGB color = taillightColor;
        color.nscale8(brightness);
        taillight[pos] = color;
    }
}

void startupCustom() {
    // Custom sequence - rainbow fade-in with breathing effect
    uint8_t progress = map(millis() - startupStartTime, 0, startupDuration, 0, 255);
    
    // Breathing effect
    uint8_t breathe = (sin(millis() / 200.0) + 1) * 127;
    
    // Headlight - rainbow fade-in
    for (uint8_t i = 0; i < headlightLedCount; i++) {
        uint8_t hue = (i * 255 / headlightLedCount) + (startupStep * 2);
        CRGB color = CHSV(hue, 255, breathe);
        headlight[i] = color;
    }
    
    // Taillight - rainbow fade-in
    for (uint8_t i = 0; i < taillightLedCount; i++) {
        uint8_t hue = (i * 255 / taillightLedCount) + (startupStep * 2);
        CRGB color = CHSV(hue, 255, breathe);
        taillight[i] = color;
    }
}

String getStartupSequenceName(uint8_t sequence) {
    switch (sequence) {
        case STARTUP_NONE: return "None";
        case STARTUP_POWER_ON: return "Power On";
        case STARTUP_SCAN: return "Scanner";
        case STARTUP_WAVE: return "Wave";
        case STARTUP_RACE: return "Race";
        case STARTUP_CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

// Motion Control Implementation
void initMotionControl() {
    Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
    
    mpu.initialize();
    
    if (!mpu.testConnection()) {
        Serial.println("❌ MPU6050 not found! Motion control disabled.");
        motionEnabled = false;
        return;
    }
    
    Serial.println("✅ MPU6050 initialized successfully!");
    
    // Configure MPU6050
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_500);
    mpu.setDLPFMode(MPU6050_DLPF_BW_20);
    
    Serial.println("🎯 Motion control features:");
    Serial.println("  - Auto blinkers based on lean angle");
    Serial.println("  - Park mode when stationary and tilted");
    Serial.println("  - Impact detection for crashes");
    Serial.println("  - Calibration system for orientation independence");
}

MotionData getMotionData() {
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    
    // Convert raw values to meaningful units
    float accelX = ax / 16384.0; // 2G range
    float accelY = ay / 16384.0;
    float accelZ = az / 16384.0;
    float gyroX = gx / 65.5; // 500 deg/s range
    float gyroY = gy / 65.5;
    float gyroZ = gz / 65.5;
    
    // Calculate pitch and roll from accelerometer
    float pitch = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0 / PI;
    float roll = atan2(accelY, accelZ) * 180.0 / PI;
    float yaw = gyroZ;
    
    return {
        pitch,
        roll,
        yaw,
        accelX,
        accelY,
        accelZ,
        gyroX,
        gyroY,
        gyroZ
    };
}

void updateMotionControl() {
    // Handle calibration mode - only show debug info, don't auto-capture
    if (calibrationMode) {
        MotionData data = getMotionData();
        // Show current motion data for debugging during calibration
        static unsigned long lastCalibrationDebug = 0;
        if (millis() - lastCalibrationDebug >= 1000) { // Update every second
            Serial.printf("Calibration Step %d - Accel: X=%.2f, Y=%.2f, Z=%.2f\n", 
                         calibrationStep + 1, data.accelX, data.accelY, data.accelZ);
            lastCalibrationDebug = millis();
        }
        return;
    }
    
    if (!motionEnabled) return;
    
    MotionData data = getMotionData();
    
    // Process motion features
    if (directionBasedLighting) {
        processDirectionDetection(data);
    }
    
    if (brakingEnabled) {
        processBrakingDetection(data);
    }
    
    if (blinkerEnabled) {
        processBlinkers(data);
    }
    
    if (parkModeEnabled) {
        processParkMode(data);
    }
    
    if (impactDetectionEnabled) {
        processImpactDetection(data);
    }
}

void processBrakingDetection(MotionData& data) {
    if (manualBrakeActive) return;
    if (!brakingEnabled || !motionEnabled) return;
    
    unsigned long currentTime = millis();
    float forwardAccel = calibration.valid ? getCalibratedForwardAccel(data) : data.accelX;
    
    // Only detect braking when moving forward (negative acceleration = deceleration)
    // Also check if we're not in park mode (stationary)
    bool isDecelerating = isMovingForward && forwardAccel < brakingThreshold && !parkModeActive;
    
    if (isDecelerating) {
        if (brakingDetectedTime == 0) {
            // Start tracking sustained deceleration
            brakingDetectedTime = currentTime;
        } else {
            unsigned long elapsed = currentTime - brakingDetectedTime;
            if (elapsed >= BRAKING_SUSTAIN_TIME && !brakingActive) {
                // Deceleration sustained long enough - activate braking
                brakingActive = true;
                brakingStartTime = currentTime;
                brakingFlashCount = 0;
                brakingPulseCount = 0;
                lastBrakingFlash = currentTime;
                lastBrakingPulse = currentTime;
                #if DEBUG_ENABLED
                Serial.printf("🛑 Braking detected! Deceleration: %.2fG (sustained for %dms)\n", 
                             forwardAccel, elapsed);
                #endif
            }
        }
    } else {
        // Not decelerating - reset detection timer
        if (brakingDetectedTime > 0) {
            brakingDetectedTime = 0;
        }
        
        // Stop braking if:
        // 1. Acceleration becomes positive (starting to go forward again)
        // 2. Or we're in park mode (stopped)
        if (brakingActive && (forwardAccel >= 0 || parkModeActive)) {
            brakingActive = false;
            brakingFlashCount = 0;
            brakingPulseCount = 0;
            #if DEBUG_ENABLED
            Serial.println("🛑 Braking ended");
            #endif
        }
    }
}

void showBrakingEffect() {
    if (!brakingActive) return;
    
    unsigned long currentTime = millis();
    unsigned long brakingElapsed = currentTime - brakingStartTime;
    
    // Determine which lights are taillight (based on direction)
    CRGB* targetLights = isMovingForward ? taillight : headlight;
    uint8_t targetCount = isMovingForward ? taillightLedCount : headlightLedCount;
    uint8_t targetLedType = isMovingForward ? taillightLedType : headlightLedType;
    uint8_t targetColorOrder = isMovingForward ? taillightColorOrder : headlightColorOrder;
    
    if (brakingEffect == 0) {
        // Flash mode: Flash 3 times then stay solid red
        if (brakingFlashCount < BRAKING_CYCLE_COUNT) {
            // Still in flash phase
            unsigned long flashElapsed = currentTime - lastBrakingFlash;
            bool flashOn = (flashElapsed % (BRAKING_FLASH_INTERVAL * 2)) < BRAKING_FLASH_INTERVAL;
            
            if (flashOn) {
                FastLED.setBrightness(brakingBrightness);
                fillSolidWithColorOrder(targetLights, targetCount, CRGB::Red, targetLedType, targetColorOrder);
            } else {
                // Off phase
                fillSolidWithColorOrder(targetLights, targetCount, CRGB::Black, targetLedType, targetColorOrder);
            }
            
            // Check if we've completed a flash cycle
            if (flashElapsed >= BRAKING_FLASH_INTERVAL * 2) {
                brakingFlashCount++;
                lastBrakingFlash = currentTime;
            }
        } else {
            // Flash phase complete - stay solid red
            FastLED.setBrightness(brakingBrightness);
            fillSolidWithColorOrder(targetLights, targetCount, CRGB::Red, targetLedType, targetColorOrder);
        }
    } else {
        // Pulse mode: Pulse from center 3 times then stay solid red
        if (brakingPulseCount < BRAKING_CYCLE_COUNT) {
            // Still in pulse phase
            unsigned long pulseElapsed = (currentTime - lastBrakingPulse) % BRAKING_PULSE_DURATION;
            float pulseProgress = (float)pulseElapsed / BRAKING_PULSE_DURATION;
            
            // Clear taillight first
            fillSolidWithColorOrder(targetLights, targetCount, CRGB::Black, targetLedType, targetColorOrder);
            
            // Pulse from center outward
            uint8_t center = targetCount / 2;
            float pulseWidth = sin(pulseProgress * PI) * (targetCount / 2.0);
            uint8_t pulseStart = center - (uint8_t)pulseWidth;
            uint8_t pulseEnd = center + (uint8_t)pulseWidth;
            
            // Clamp to array bounds
            if (pulseStart < 0) pulseStart = 0;
            if (pulseEnd > targetCount) pulseEnd = targetCount;
            
            // Fill pulse region with red
            for (uint8_t i = pulseStart; i < pulseEnd; i++) {
                float distanceFromCenter = abs((float)(i - center));
                float normalizedDistance = pulseWidth > 0 ? distanceFromCenter / pulseWidth : 0;
                uint8_t brightness = brakingBrightness * (1.0 - normalizedDistance);
                CRGB red = CRGB::Red;
                red.nscale8(brightness);
                targetLights[i] = red;
            }
            
            // Apply color order
            applyColorOrderToArray(targetLights, targetCount, targetLedType, targetColorOrder);
            
            // Check if we've completed a pulse cycle (when pulseProgress wraps back to 0)
            if (pulseElapsed < 50 && currentTime - lastBrakingPulse >= BRAKING_PULSE_DURATION) {
                brakingPulseCount++;
                lastBrakingPulse = currentTime;
            }
        } else {
            // Pulse phase complete - stay solid red
            FastLED.setBrightness(brakingBrightness);
            fillSolidWithColorOrder(targetLights, targetCount, CRGB::Red, targetLedType, targetColorOrder);
        }
    }
}

void processBlinkers(MotionData& data) {
    unsigned long currentTime = millis();
    if (manualBlinkerActive) return;
    
    // Use calibrated values if available
    float leftRightAccel = calibration.valid ? getCalibratedLeftRightAccel(data) : data.accelY;
    
    // Detect turn intent based on lateral acceleration
    float turnThreshold = 1.5 * motionSensitivity;
    bool turnIntent = abs(leftRightAccel) > turnThreshold;
    
    if (turnIntent) {
        int direction = (leftRightAccel > 0) ? 1 : -1; // 1 = right, -1 = left
        
        if (!blinkerActive) {
            blinkerActive = true;
            blinkerDirection = direction;
            blinkerStartTime = currentTime;
            #if DEBUG_ENABLED
            Serial.printf("🔄 Blinker activated: %s\n", direction > 0 ? "Right" : "Left");
            #endif
        }
    } else if (blinkerActive && currentTime - blinkerStartTime > blinkerTimeout) {
        blinkerActive = false;
        blinkerDirection = 0;
        Serial.println("🔄 Blinker deactivated");
    }
    
    // Blinker effect is now handled in updateEffects() with proper priority
}

void processParkMode(MotionData& data) {
    unsigned long currentTime = millis();
    
    // Calculate motion magnitude from accelerometer and gyroscope
    // For acceleration, we want to detect changes from the baseline (gravity)
    // So we calculate the deviation from 1G (gravity)
    float accelMagnitude = sqrt(data.accelX * data.accelX + data.accelY * data.accelY + data.accelZ * data.accelZ);
    float accelDeviation = abs(accelMagnitude - 1.0); // Deviation from 1G (gravity)
    float gyroMagnitude = sqrt(data.gyroX * data.gyroX + data.gyroY * data.gyroY + data.gyroZ * data.gyroZ);
    
    // Convert gyro to deg/s for easier threshold setting
    float gyroDegPerSec = gyroMagnitude;
    
    // Define noise thresholds (adjustable via settings)
    float accelNoiseThreshold = parkAccelNoiseThreshold; // G-force threshold for acceleration noise
    float gyroNoiseThreshold = parkGyroNoiseThreshold;  // deg/s threshold for gyro noise
    
    // Debug output every 2 seconds
    #if DEBUG_ENABLED
    static unsigned long lastDebugTime = 0;
    if (currentTime - lastDebugTime > 2000) {
        Serial.printf("🔍 Park Debug - Accel: %.3fG (dev: %.3f), Gyro: %.1f°/s, Thresholds: %.3fG, %.1f°/s\n", 
                     accelMagnitude, accelDeviation, gyroDegPerSec, accelNoiseThreshold, gyroNoiseThreshold);
        lastDebugTime = currentTime;
    }
    #endif
    
    // Check if device is stationary (below noise thresholds)
    // Use acceleration deviation from gravity instead of total magnitude
    bool isStationary = (accelDeviation < accelNoiseThreshold) && (gyroDegPerSec < gyroNoiseThreshold);
    
    if (isStationary) {
        if (!parkModeActive) {
            if (parkStartTime == 0) {
                // Start park mode timer
                parkStartTime = currentTime;
                #if DEBUG_ENABLED
                Serial.printf("🅿️ Starting park timer (stationary detected)\n");
                #endif
            } else {
                // Check if we've been stationary long enough
                if (currentTime - parkStartTime > parkStationaryTime) {
                    parkModeActive = true;
                    #if DEBUG_ENABLED
                    Serial.printf("🅿️ Park mode activated (stationary for %dms)\n", parkStationaryTime);
                    #endif
                    showParkEffect();
                }
            }
        }
    } else {
        // Motion detected - deactivate park mode
        if (parkModeActive) {
            parkModeActive = false;
            parkStartTime = 0;
            #if DEBUG_ENABLED
            Serial.println("🅿️ Park mode deactivated (motion detected)");
            #endif
        } else if (parkStartTime > 0) {
            // Reset timer if we were counting down but motion detected
            parkStartTime = 0;
            #if DEBUG_ENABLED
            Serial.println("🅿️ Park timer reset (motion detected)");
            #endif
        }
    }
}

void processImpactDetection(MotionData& data) {
    unsigned long currentTime = millis();
    
    // Calculate total acceleration magnitude
    float accelMagnitude = sqrt(data.accelX * data.accelX + data.accelY * data.accelY + data.accelZ * data.accelZ);
    
    // Convert to G-force (assuming 9.8 m/s² = 1G)
    float gForce = accelMagnitude / 9.8;
    
    // Detect impact (sudden high acceleration)
    if (gForce > impactThreshold && currentTime - lastImpactTime > 1000) {
        lastImpactTime = currentTime;
        Serial.printf("💥 Impact detected! G-force: %.1f\n", gForce);
        showImpactEffect();
    }
}

void processDirectionDetection(MotionData& data) {
    if (!directionBasedLighting || !motionEnabled) return;
    
    unsigned long currentTime = millis();
    float rawForwardAccel = calibration.valid ? getCalibratedForwardAccel(data) : data.accelX;
    
    // Apply low-pass filter to reduce noise
    filteredForwardAccel = FILTER_ALPHA * filteredForwardAccel + (1.0 - FILTER_ALPHA) * rawForwardAccel;
    float forwardAccel = filteredForwardAccel;
    
    // Debug output only when direction changes or every 5 seconds
    #if DEBUG_ENABLED
    static unsigned long lastDebugTime = 0;
    static bool lastDirectionState = isMovingForward;
    if (currentTime - lastDebugTime > 5000 || lastDirectionState != isMovingForward) {
        Serial.printf("🔄 Direction Debug - Raw: %.3fG, Filtered: %.3fG, Threshold: ±%.3fG, Current: %s\n", 
                     rawForwardAccel, filteredForwardAccel, forwardAccelThreshold, isMovingForward ? "Forward" : "Backward");
        lastDebugTime = currentTime;
        lastDirectionState = isMovingForward;
    }
    #endif
    
    // Determine desired direction based on filtered forward acceleration
    // Use hysteresis to prevent rapid switching (different thresholds for forward vs backward)
    // Positive = forward, negative = backward
    float forwardThreshold = forwardAccelThreshold;
    float backwardThreshold = -forwardAccelThreshold;
    
    // Add hysteresis: once moving in a direction, require more force to change
    if (isMovingForward) {
        backwardThreshold = -forwardAccelThreshold * 0.7;  // Easier to detect backward when currently forward
    } else {
        forwardThreshold = forwardAccelThreshold * 0.7;  // Easier to detect forward when currently backward
    }
    
    bool desiredForward = forwardAccel > forwardThreshold;
    bool desiredBackward = forwardAccel < backwardThreshold;
    
    // If we're in a fade transition, continue it (don't check for new changes)
    if (directionChangePending) {
        unsigned long fadeElapsed = currentTime - directionFadeStartTime;
        
        if (fadeElapsed < DIRECTION_FADE_DURATION) {
            // Still fading - update progress
            directionFadeProgress = (float)fadeElapsed / DIRECTION_FADE_DURATION;
            if (directionFadeProgress > 1.0) directionFadeProgress = 1.0;
            
            // Debug fade progress every 100ms
            #if DEBUG_ENABLED
            static unsigned long lastFadeDebug = 0;
            if (currentTime - lastFadeDebug > 100) {
                Serial.printf("🔄 Fade progress: %.1f%% (%d/%dms)\n", 
                             directionFadeProgress * 100.0, fadeElapsed, DIRECTION_FADE_DURATION);
                lastFadeDebug = currentTime;
            }
            #endif
            // Continue to updateEffects() to render the fade
        } else {
            // Fade duration reached - set to 100% and keep fading for one more frame
            if (directionFadeProgress < 1.0) {
                directionFadeProgress = 1.0;
                #if DEBUG_ENABLED
                Serial.printf("🔄 Fade reached 100%% - rendering final blend frame\n");
                #endif
                // Continue to updateEffects() to render the 100% blend
            } else {
                // Fade complete (100% rendered for at least one frame) - now apply direction change
                isMovingForward = !isMovingForward;
                directionChangePending = false;
                directionFadeProgress = 0.0;
                directionChangeDetectedTime = 0;
                #if DEBUG_ENABLED
                Serial.printf("🔄 Direction switched: %s (fade complete after %dms)\n", 
                             isMovingForward ? "Forward" : "Backward", fadeElapsed);
                #endif
            }
        }
        // During fade, don't process new direction changes
        return;
    }
    
    // Check if direction change is needed
    bool needsChange = false;
    if (isMovingForward && desiredBackward) {
        needsChange = true;
    } else if (!isMovingForward && desiredForward) {
        needsChange = true;
    }
    
    if (needsChange) {
        if (directionChangeDetectedTime == 0) {
            // Start tracking sustained direction change
            directionChangeDetectedTime = currentTime;
            #if DEBUG_ENABLED
            Serial.printf("🔄 Direction change detected! Desired: %s, Current: %s, Accel: %.3fG\n", 
                         desiredForward ? "Forward" : "Backward", isMovingForward ? "Forward" : "Backward", forwardAccel);
            #endif
        } else {
            unsigned long elapsed = currentTime - directionChangeDetectedTime;
            if (elapsed >= DIRECTION_SUSTAIN_TIME) {
                // Direction has been sustained long enough - start fade transition
                // Only start if not already fading
                if (!directionChangePending) {
                    directionChangePending = true;
                    directionFadeStartTime = currentTime;
                    directionFadeProgress = 0.0;
                    #if DEBUG_ENABLED
                    Serial.printf("🔄 Direction change confirmed (sustained for %dms) - starting fade to %s\n", 
                                 DIRECTION_SUSTAIN_TIME, desiredForward ? "Forward" : "Backward");
                    #endif
                }
            }
        }
    } else {
        // Direction matches current state - reset detection timer
        if (directionChangeDetectedTime > 0) {
            unsigned long elapsed = currentTime - directionChangeDetectedTime;
            #if DEBUG_ENABLED
            Serial.printf("🔄 Direction change cancelled after %dms - direction reverted (accel: %.3fG)\n", 
                         elapsed, forwardAccel);
            #endif
            directionChangeDetectedTime = 0;
        }
    }
}

void showBlinkerEffect(int direction) {
    static bool blinkState = false;
    static unsigned long lastBlinkTime = 0;
    
    if (millis() - lastBlinkTime > 500) { // 500ms blink interval
        blinkState = !blinkState;
        lastBlinkTime = millis();
    }
    
    if (!blinkState) return;
    
    // Calculate which half of each strip to blink
    uint8_t headlightHalf = headlightLedCount / 2;
    uint8_t taillightHalf = taillightLedCount / 2;
    
    // Blink the appropriate half based on direction
    if (direction > 0) { // Right turn
        // Blink right half of headlight, left half of taillight
        for (uint8_t i = headlightHalf; i < headlightLedCount; i++) {
            headlight[i] = CRGB::Yellow;
        }
        for (uint8_t i = 0; i < taillightHalf; i++) {
            taillight[i] = CRGB::Yellow;
        }
    } else { // Left turn
        // Blink left half of headlight, right half of taillight
        for (uint8_t i = 0; i < headlightHalf; i++) {
            headlight[i] = CRGB::Yellow;
        }
        for (uint8_t i = taillightHalf; i < taillightLedCount; i++) {
            taillight[i] = CRGB::Yellow;
        }
    }
}

void showParkEffect() {
    // Apply park mode brightness
    FastLED.setBrightness(parkBrightness);
    
    // Temporarily store original effect speed and set park speed
    uint8_t originalSpeed = effectSpeed;
    effectSpeed = parkEffectSpeed;
    
    // Show configurable park effect
    switch (parkEffect) {
        case FX_SOLID:
            fillSolidWithColorOrder(headlight, headlightLedCount, parkHeadlightColor, headlightLedType, headlightColorOrder);
            fillSolidWithColorOrder(taillight, taillightLedCount, parkTaillightColor, taillightLedType, taillightColorOrder);
            break;
        case FX_BREATH:
            effectBreathImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectBreathImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_RAINBOW:
            effectRainbowImproved(headlight, headlightLedCount, headlightTiming.step);
            effectRainbowImproved(taillight, taillightLedCount, taillightTiming.step);
            break;
        case FX_PULSE:
            effectPulseImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectPulseImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_BLINK_RAINBOW:
            effectBlinkRainbowImproved(headlight, headlightLedCount, headlightTiming.step);
            effectBlinkRainbowImproved(taillight, taillightLedCount, taillightTiming.step);
            break;
        case FX_GRADIENT_SHIFT:
            effectGradientShiftImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectGradientShiftImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_FIRE:
            effectFireImproved(headlight, headlightLedCount, headlightTiming.step);
            effectFireImproved(taillight, taillightLedCount, taillightTiming.step);
            break;
        case FX_METEOR:
            effectMeteorImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectMeteorImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_WAVE:
            effectWaveImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectWaveImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_CENTER_BURST:
            effectCenterBurstImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectCenterBurstImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_CANDLE:
            effectCandleImproved(headlight, headlightLedCount, headlightTiming.step);
            effectCandleImproved(taillight, taillightLedCount, taillightTiming.step);
            break;
        case FX_STATIC_RAINBOW:
            effectStaticRainbow(headlight, headlightLedCount);
            effectStaticRainbow(taillight, taillightLedCount);
            break;
        case FX_KNIGHT_RIDER:
            effectKnightRiderImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectKnightRiderImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_POLICE:
            effectPoliceImproved(headlight, headlightLedCount, headlightTiming.step);
            effectPoliceImproved(taillight, taillightLedCount, taillightTiming.step);
            break;
        case FX_STROBE:
            effectStrobeImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectStrobeImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_LARSON_SCANNER:
            effectLarsonScannerImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectLarsonScannerImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_COLOR_WIPE:
            effectColorWipeImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectColorWipeImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_HAZARD:
            effectHazardImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectHazardImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_RUNNING_LIGHTS:
            effectRunningLightsImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectRunningLightsImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_COLOR_SWEEP:
            effectColorSweepImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectColorSweepImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_RAINBOW_KNIGHT_RIDER:
            effectRainbowKnightRiderImproved(headlight, headlightLedCount, headlightTiming.step);
            effectRainbowKnightRiderImproved(taillight, taillightLedCount, taillightTiming.step);
            break;
        case FX_DUAL_KNIGHT_RIDER:
            effectDualKnightRiderImproved(headlight, headlightLedCount, parkHeadlightColor, headlightTiming.step);
            effectDualKnightRiderImproved(taillight, taillightLedCount, parkTaillightColor, taillightTiming.step);
            break;
        case FX_DUAL_RAINBOW_KNIGHT_RIDER:
            effectDualRainbowKnightRiderImproved(headlight, headlightLedCount, headlightTiming.step);
            effectDualRainbowKnightRiderImproved(taillight, taillightLedCount, taillightTiming.step);
            break;
    }
    
    // Apply color order conversion for RGBW LEDs in park mode (FX_SOLID already handles this)
    if (parkEffect != FX_SOLID) {
        applyColorOrderToArray(headlight, headlightLedCount, headlightLedType, headlightColorOrder);
        applyColorOrderToArray(taillight, taillightLedCount, taillightLedType, taillightColorOrder);
    }
    
    // Restore original effect speed
    effectSpeed = originalSpeed;
}

void showImpactEffect() {
    // Flash all lights white briefly
    fill_solid(headlight, headlightLedCount, CRGB::White);
    fill_solid(taillight, taillightLedCount, CRGB::White);
    FastLED.show();
    delay(200);
    
    // Restore normal colors
    fill_solid(headlight, headlightLedCount, headlightColor);
    fill_solid(taillight, taillightLedCount, taillightColor);
}

void resetToNormalEffects() {
    // Reset brightness to normal level
    FastLED.setBrightness(globalBrightness);
    
    // Reset effect speed to normal
    effectSpeed = effectSpeed; // Keep current effect speed
    
    // Apply normal headlight and taillight effects immediately
    updateEffects();
    FastLED.show();
    
    Serial.println("🔄 Reset to normal effects");
}

void startCalibration() {
    calibrationMode = true;
    calibrationStep = 0;
    calibrationStartTime = millis();
    calibration.valid = false;
    calibrationComplete = false;
    
    Serial.println("=== MPU6050 CALIBRATION STARTED ===");
    Serial.println("Step 1: Hold device LEVEL and click 'Next Step' button in UI...");
    Serial.println("(Calibration will wait for your input - no automatic progression)");
}

void captureCalibrationStep(MotionData& data) {
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - calibrationStartTime;
    
    // Check for timeout
    if (elapsed > calibrationTimeout) {
        Serial.println("Calibration timeout! Restarting...");
        startCalibration();
        return;
    }
    
    // Debug output
    Serial.printf("Capturing Step %d: Accel X=%.2f, Y=%.2f, Z=%.2f\n", 
                  calibrationStep + 1, data.accelX, data.accelY, data.accelZ);
    
    switch(calibrationStep) {
        case 0: // Level
            calibration.levelAccelX = data.accelX;
            calibration.levelAccelY = data.accelY;
            calibration.levelAccelZ = data.accelZ;
            Serial.println("✅ Level captured. Step 2: Tilt FORWARD and click Next Step...");
            break;
            
        case 1: // Forward
            calibration.forwardAccelX = data.accelX;
            calibration.forwardAccelY = data.accelY;
            calibration.forwardAccelZ = data.accelZ;
            Serial.println("✅ Forward captured. Step 3: Tilt BACKWARD and click Next Step...");
            break;
            
        case 2: // Backward
            calibration.backwardAccelX = data.accelX;
            calibration.backwardAccelY = data.accelY;
            calibration.backwardAccelZ = data.accelZ;
            Serial.println("✅ Backward captured. Step 4: Tilt LEFT and click Next Step...");
            break;
            
        case 3: // Left
            calibration.leftAccelX = data.accelX;
            calibration.leftAccelY = data.accelY;
            calibration.leftAccelZ = data.accelZ;
            Serial.println("✅ Left captured. Step 5: Tilt RIGHT and click Next Step...");
            break;
            
        case 4: // Right
            calibration.rightAccelX = data.accelX;
            calibration.rightAccelY = data.accelY;
            calibration.rightAccelZ = data.accelZ;
            Serial.println("✅ Right captured. Completing calibration...");
            completeCalibration();
            return;
    }
    
    calibrationStep++;
    calibrationStartTime = currentTime; // Reset timer for next step
}

void completeCalibration() {
    // Find the axis with maximum change for forward/backward
    float forwardX = abs(calibration.forwardAccelX - calibration.backwardAccelX);
    float forwardY = abs(calibration.forwardAccelY - calibration.backwardAccelY);
    float forwardZ = abs(calibration.forwardAccelZ - calibration.backwardAccelZ);
    
    if (forwardX > forwardY && forwardX > forwardZ) {
        calibration.forwardAxis = 'X';
        calibration.forwardSign = (calibration.forwardAccelX > calibration.backwardAccelX) ? 1 : -1;
    } else if (forwardY > forwardZ) {
        calibration.forwardAxis = 'Y';
        calibration.forwardSign = (calibration.forwardAccelY > calibration.backwardAccelY) ? 1 : -1;
    } else {
        calibration.forwardAxis = 'Z';
        calibration.forwardSign = (calibration.forwardAccelZ > calibration.backwardAccelZ) ? 1 : -1;
    }
    
    // Find the axis with maximum change for left/right
    float leftRightX = abs(calibration.leftAccelX - calibration.rightAccelX);
    float leftRightY = abs(calibration.leftAccelY - calibration.rightAccelY);
    float leftRightZ = abs(calibration.leftAccelZ - calibration.rightAccelZ);
    
    if (leftRightX > leftRightY && leftRightX > leftRightZ) {
        calibration.leftRightAxis = 'X';
        calibration.leftRightSign = (calibration.leftAccelX > calibration.rightAccelX) ? 1 : -1;
    } else if (leftRightY > leftRightZ) {
        calibration.leftRightAxis = 'Y';
        calibration.leftRightSign = (calibration.leftAccelY > calibration.rightAccelY) ? 1 : -1;
    } else {
        calibration.leftRightAxis = 'Z';
        calibration.leftRightSign = (calibration.leftAccelZ > calibration.rightAccelZ) ? 1 : -1;
    }
    
    calibration.valid = true;
    calibrationMode = false;
    calibrationComplete = true;
    
    Serial.println("=== CALIBRATION COMPLETE ===");
    Serial.printf("Forward axis: %c (sign: %d)\n", calibration.forwardAxis, calibration.forwardSign);
    Serial.printf("Left/Right axis: %c (sign: %d)\n", calibration.leftRightAxis, calibration.leftRightSign);
    
    // Save calibration data to persistent storage
    saveSettings();
    Serial.println("Calibration data saved to filesystem!");
}

void resetCalibration() {
    calibrationComplete = false;
    calibration.valid = false;
    calibrationMode = false;
    
    // Save the reset state to persistent storage
    saveSettings();
    Serial.println("Motion calibration reset and saved to filesystem.");
}

float getCalibratedForwardAccel(MotionData& data) {
    if (!calibration.valid) return data.accelX;
    
    switch(calibration.forwardAxis) {
        case 'X': return data.accelX * calibration.forwardSign;
        case 'Y': return data.accelY * calibration.forwardSign;
        case 'Z': return data.accelZ * calibration.forwardSign;
        default: return data.accelX;
    }
}

float getCalibratedLeftRightAccel(MotionData& data) {
    if (!calibration.valid) return data.accelY;
    
    switch(calibration.leftRightAxis) {
        case 'X': return data.accelX * calibration.leftRightSign;
        case 'Y': return data.accelY * calibration.leftRightSign;
        case 'Z': return data.accelZ * calibration.leftRightSign;
        default: return data.accelY;
    }
}

// OTA Update Implementation
void startOTAUpdate(String url) {
    if (url.isEmpty()) {
        Serial.println("❌ No update URL provided");
        otaStatus = "No URL";
        otaError = "No update URL provided";
        return;
    }
    
    Serial.printf("🔄 Starting OTA update from: %s\n", url.c_str());
    
    otaInProgress = true;
    otaProgress = 0;
    otaStatus = "Downloading";
    otaError = "";
    otaStartTime = millis();
    
    // Show downloading effect on LEDs
    fill_solid(headlight, headlightLedCount, CRGB::Blue);
    fill_solid(taillight, taillightLedCount, CRGB::Blue);
    FastLED.show();
    
    // Start the update process
    httpUpdate.setLedPin(-1); // Disable built-in LED
    httpUpdate.onProgress(updateOTAProgress);
    httpUpdate.onError(handleOTAError);
    
    WiFiClient client;
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("❌ OTA update failed: %s\n", httpUpdate.getLastErrorString().c_str());
            otaStatus = "Failed";
            otaError = httpUpdate.getLastErrorString();
            otaInProgress = false;
            break;
            
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("ℹ️ No updates available");
            otaStatus = "No Updates";
            otaInProgress = false;
            break;
            
        case HTTP_UPDATE_OK:
            Serial.println("✅ OTA update completed, restarting...");
            otaStatus = "Complete";
            otaProgress = 100;
            // Device will restart automatically
            break;
    }
}

void updateOTAProgress(unsigned int progress, unsigned int total) {
    otaProgress = (progress * 100) / total;
    Serial.printf("📥 OTA Progress: %d%% (%d/%d bytes)\n", otaProgress, progress, total);
    
    // Update LED progress indicator
    uint8_t ledProgress = (progress * headlightLedCount) / total;
    for (uint8_t i = 0; i < headlightLedCount; i++) {
        headlight[i] = (i < ledProgress) ? CRGB::Green : CRGB::Blue;
    }
    for (uint8_t i = 0; i < taillightLedCount; i++) {
        taillight[i] = (i < ledProgress) ? CRGB::Green : CRGB::Blue;
    }
    FastLED.show();
}

void handleOTAError(int error) {
    String errorMsg = "";
    switch (error) {
        case HTTP_UE_TOO_LESS_SPACE:
            errorMsg = "Not enough space";
            break;
        case HTTP_UE_SERVER_NOT_REPORT_SIZE:
            errorMsg = "Server did not report size";
            break;
        case HTTP_UE_SERVER_FILE_NOT_FOUND:
            errorMsg = "File not found on server";
            break;
        case HTTP_UE_SERVER_FORBIDDEN:
            errorMsg = "Server forbidden";
            break;
        case HTTP_UE_SERVER_WRONG_HTTP_CODE:
            errorMsg = "Wrong HTTP code";
            break;
        case HTTP_UE_SERVER_FAULTY_MD5:
            errorMsg = "Faulty MD5";
            break;
        case HTTP_UE_BIN_VERIFY_HEADER_FAILED:
            errorMsg = "Verify header failed";
            break;
        case HTTP_UE_BIN_FOR_WRONG_FLASH:
            errorMsg = "Wrong flash size";
            break;
        default:
            errorMsg = "Unknown error";
            break;
    }
    
    Serial.printf("❌ OTA Error: %s\n", errorMsg.c_str());
    otaStatus = "Error";
    otaError = errorMsg;
    otaInProgress = false;
    
    // Show error on LEDs
    fill_solid(headlight, headlightLedCount, CRGB::Red);
    fill_solid(taillight, taillightLedCount, CRGB::Red);
    FastLED.show();
}

// File Upload Handler for OTA
void handleOTAUpload() {
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("📁 Starting firmware upload: %s\n", upload.filename.c_str());
        
        // Validate file extension
        if (!upload.filename.endsWith(".bin")) {
            Serial.println("❌ Invalid file type. Only .bin files are allowed.");
            return;
        }
        
        otaFileName = upload.filename;
        otaInProgress = true;
        otaProgress = 0;
        otaStatus = "Uploading";
        otaError = "";
        otaStartTime = millis();
        
        // Start OTA update
        Serial.println("🔄 Starting OTA update");
        size_t freeSpace = ESP.getFreeSketchSpace();
        Serial.printf("💾 Free sketch space: %d bytes\n", freeSpace);
        
        if (!Update.begin((freeSpace - 0x1000) & 0xFFFFF000)) {
            String errorMsg = Update.errorString();
            Serial.printf("❌ OTA begin failed: %s\n", errorMsg.c_str());
            otaStatus = "Begin Failed";
            otaError = errorMsg;
            otaInProgress = false;
            return;
        }
        
        Serial.println("✅ OTA update started successfully");
        
        // Show uploading effect on LEDs
        fill_solid(headlight, headlightLedCount, CRGB::Blue);
        fill_solid(taillight, taillightLedCount, CRGB::Blue);
        FastLED.show();
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Write data (no return value check)
        if (!Update.hasError()) {
            Update.write(upload.buf, upload.currentSize);
            
            // Update progress occasionally
            static size_t lastProgressUpdate = 0;
            if (upload.currentSize - lastProgressUpdate > 50000) { // Every 50KB
                otaProgress = (upload.currentSize * 100) / upload.totalSize;
                Serial.printf("📥 Upload Progress: %d%% (%d/%d bytes)\n", otaProgress, upload.currentSize, upload.totalSize);
                lastProgressUpdate = upload.currentSize;
                
                // Update LED progress indicator
                uint8_t ledProgress = (upload.currentSize * headlightLedCount) / upload.totalSize;
                for (uint8_t i = 0; i < headlightLedCount; i++) {
                    headlight[i] = (i < ledProgress) ? CRGB::Green : CRGB::Blue;
                }
                for (uint8_t i = 0; i < taillightLedCount; i++) {
                    taillight[i] = (i < ledProgress) ? CRGB::Green : CRGB::Blue;
                }
                FastLED.show();
            }
        }
        
    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("✅ Upload complete: %s (%d bytes)\n", upload.filename.c_str(), upload.totalSize);
        
        if (Update.end(true)) {
            Serial.println("✅ OTA update completed, restarting...");
            otaStatus = "Complete";
            otaProgress = 100;
            
            // Show success on LEDs
            fill_solid(headlight, headlightLedCount, CRGB::Green);
            fill_solid(taillight, taillightLedCount, CRGB::Green);
            FastLED.show();
            
            // Send success response to client before restart
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Update complete, restarting...\"}");
            
            // Give the client time to receive the response
            delay(1500);
            ESP.restart();
        } else {
            String errorMsg = Update.errorString();
            Serial.printf("❌ OTA end failed: %s\n", errorMsg.c_str());
            otaStatus = "End Failed";
            otaError = errorMsg;
            otaInProgress = false;
            
            // Show error on LEDs
            fill_solid(headlight, headlightLedCount, CRGB::Red);
            fill_solid(taillight, taillightLedCount, CRGB::Red);
            FastLED.show();
        }
    }
}

// Start OTA update from uploaded file
void startOTAUpdateFromFile(String filename) {
    Serial.printf("🔄 Starting OTA update from file: %s\n", filename.c_str());
    
    otaStatus = "Installing";
    otaProgress = 0;
    otaError = "";
    
    // Show installing effect on LEDs
    fill_solid(headlight, headlightLedCount, CRGB::Yellow);
    fill_solid(taillight, taillightLedCount, CRGB::Yellow);
    FastLED.show();
    
    // Start the update process from file
    Update.onProgress(updateOTAProgress);
    
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.printf("❌ Failed to open file: %s\n", filename.c_str());
        otaStatus = "File Error";
        otaError = "Failed to open uploaded file";
        otaInProgress = false;
        return;
    }
    
    size_t fileSize = file.size();
    Serial.printf("📁 File size: %d bytes\n", fileSize);
    
    // Check if file size is valid
    if (fileSize == 0) {
        Serial.println("❌ File is empty");
        otaStatus = "File Error";
        otaError = "File is empty";
        otaInProgress = false;
        file.close();
        return;
    }
    
    // Check if file size is too large for available space
    size_t freeSpace = ESP.getFreeSketchSpace();
    Serial.printf("💾 Free sketch space: %d bytes\n", freeSpace);
    
    if (fileSize > freeSpace) {
        Serial.printf("❌ File too large: %d > %d\n", fileSize, freeSpace);
        otaStatus = "File Error";
        otaError = "File too large for available space";
        otaInProgress = false;
        file.close();
        return;
    }
    
    if (!Update.begin(fileSize, U_FLASH)) {
        Serial.printf("❌ OTA begin failed: %s\n", Update.errorString());
        otaStatus = "Begin Failed";
        otaError = Update.errorString();
        otaInProgress = false;
        file.close();
        return;
    }
    
    size_t written = Update.writeStream(file);
    file.close();
    
    if (written != fileSize) {
        Serial.printf("❌ OTA write failed: %s\n", Update.errorString());
        otaStatus = "Write Failed";
        otaError = Update.errorString();
        otaInProgress = false;
        return;
    }
    
    if (!Update.end()) {
        Serial.printf("❌ OTA end failed: %s\n", Update.errorString());
        otaStatus = "End Failed";
        otaError = Update.errorString();
        otaInProgress = false;
        return;
    }
    
    Serial.println("✅ OTA update completed, restarting...");
    otaStatus = "Complete";
    otaProgress = 100;
    
    // Show success on LEDs
    fill_solid(headlight, headlightLedCount, CRGB::Green);
    fill_solid(taillight, taillightLedCount, CRGB::Green);
    FastLED.show();
    
    delay(2000);
    ESP.restart();
}

void handleSerialCommands() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        command.toLowerCase();
        
        if (command.startsWith("p")) {
            uint8_t preset = command.substring(1).toInt();
            if (preset < presetCount) {
                setPreset(preset);
            }
        }
        else if (command.startsWith("b")) {
            uint8_t brightness = command.substring(1).toInt();
            globalBrightness = brightness;
            FastLED.setBrightness(brightness);
            Serial.printf("Brightness set to %d\n", brightness);
        }
        else if (command.startsWith("h")) {
            uint32_t colorHex = strtol(command.substring(1).c_str(), NULL, 16);
            headlightColor = CRGB((colorHex >> 16) & 0xFF, (colorHex >> 8) & 0xFF, colorHex & 0xFF);
            Serial.printf("Headlight color set to 0x%06X\n", colorHex);
        }
        else if (command.startsWith("t")) {
            uint32_t colorHex = strtol(command.substring(1).c_str(), NULL, 16);
            taillightColor = CRGB((colorHex >> 16) & 0xFF, (colorHex >> 8) & 0xFF, colorHex & 0xFF);
            Serial.printf("Taillight color set to 0x%06X\n", colorHex);
        }
        else if (command.startsWith("eh")) {
            uint8_t effect = command.substring(2).toInt();
            if (effect <= 5) {
                headlightEffect = effect;
                Serial.printf("Headlight effect set to %d\n", effect);
            }
        }
        else if (command.startsWith("et")) {
            uint8_t effect = command.substring(2).toInt();
            if (effect <= 5) {
                taillightEffect = effect;
                Serial.printf("Taillight effect set to %d\n", effect);
            }
        }
        else if (command.startsWith("startup")) {
            uint8_t sequence = command.substring(7).toInt();
            if (sequence <= 5) {
                startupSequence = sequence;
                startupEnabled = (sequence != STARTUP_NONE);
                Serial.printf("Startup sequence set to %d (%s)\n", sequence, getStartupSequenceName(sequence).c_str());
            }
        }
        else if (command == "test_startup") {
            startStartupSequence();
            Serial.println("Testing startup sequence...");
        }
        else if (command == "calibrate" || command == "cal") {
            startCalibration();
            Serial.println("Starting motion calibration...");
        }
        else if (command == "reset_cal") {
            resetCalibration();
            Serial.println("Motion calibration reset");
        }
        else if (command == "motion_on") {
            motionEnabled = true;
            Serial.println("Motion control enabled");
        }
        else if (command == "motion_off") {
            motionEnabled = false;
            Serial.println("Motion control disabled");
        }
        else if (command == "blinker_on") {
            blinkerEnabled = true;
            Serial.println("Auto blinkers enabled");
        }
        else if (command == "blinker_off") {
            blinkerEnabled = false;
            Serial.println("Auto blinkers disabled");
        }
        else if (command == "park_on") {
            parkModeEnabled = true;
            Serial.println("Park mode enabled");
        }
        else if (command == "park_off") {
            parkModeEnabled = false;
            Serial.println("Park mode disabled");
        }
        else if (command == "group_create" || command.startsWith("group_create ")) {
            String code = "";
            if (command.startsWith("group_create ")) {
                code = command.substring(13);
            }
            groupCode = code;
            if (groupCode.length() != 6) {
                groupCode = "";
                generateGroupCode();
            }
            isGroupMaster = true;
            allowGroupJoin = true;
            hasGroupMaster = true;
            autoJoinOnHeartbeat = false;
            joinInProgress = false;
            groupMemberCount = 0;
            esp_wifi_get_mac(WIFI_IF_STA, groupMasterMac);
            // Add self as a group member when creating a group
            uint8_t mac[6];
            esp_wifi_get_mac(WIFI_IF_STA, mac);
            addGroupMember(mac, deviceName.c_str());
            Serial.printf("Group: Created with code %s\n", groupCode.c_str());
        }
        else if (command.startsWith("group_join ")) {
            groupCode = command.substring(11);
            if (groupCode.length() == 6) {
                isGroupMaster = false;
                hasGroupMaster = false;
                autoJoinOnHeartbeat = false;
                joinInProgress = true;
                memset(groupMasterMac, 0, sizeof(groupMasterMac));
                groupMemberCount = 0;
                sendJoinRequest();
                Serial.printf("Group: Attempting to join with code %s\n", groupCode.c_str());
            } else {
                Serial.println("Group: Code must be 6 digits");
            }
        }
        else if (command == "group_scan_join") {
            groupCode = "";
            isGroupMaster = false;
            hasGroupMaster = false;
            allowGroupJoin = false;
            autoJoinOnHeartbeat = true;
            joinInProgress = false;
            groupMemberCount = 0;
            memset(groupMasterMac, 0, sizeof(groupMasterMac));
            Serial.println("Group: Scanning for group heartbeat to join");
        }
        else if (command == "group_leave") {
            groupCode = "";
            isGroupMaster = false;
            allowGroupJoin = false;
            groupMemberCount = 0;
            hasGroupMaster = false;
            autoJoinOnHeartbeat = false;
            joinInProgress = false;
            memset(groupMasterMac, 0, sizeof(groupMasterMac));
            Serial.println("Group: Left group");
        }
        else if (command == "group_allow_join") {
            allowGroupJoin = true;
            Serial.println("Group: Join requests enabled");
        }
        else if (command == "group_block_join") {
            allowGroupJoin = false;
            Serial.println("Group: Join requests disabled");
        }
        else if (command == "group_status") {
            Serial.printf("Group: Code=%s, Master=%s, Members=%d, AllowJoin=%s, AutoJoin=%s\n", 
                         groupCode.c_str(), isGroupMaster ? "Yes" : "No", 
                         groupMemberCount, allowGroupJoin ? "Yes" : "No",
                         autoJoinOnHeartbeat ? "Yes" : "No");
        }
        else if (command.startsWith("park_effect ")) {
            int effect = command.substring(12).toInt();
            if (effect >= 0 && effect <= 22) {
                parkEffect = effect;
                saveSettings();
                Serial.printf("Park effect set to %d\n", effect);
            } else {
                Serial.println("Invalid effect (0-22)");
            }
        }
        else if (command.startsWith("park_speed ")) {
            int speed = command.substring(11).toInt();
            if (speed >= 0 && speed <= 255) {
                parkEffectSpeed = speed;
                saveSettings();
                Serial.printf("Park effect speed set to %d\n", speed);
            } else {
                Serial.println("Invalid speed (0-255)");
            }
        }
        else if (command.startsWith("park_brightness ")) {
            int brightness = command.substring(16).toInt();
            if (brightness >= 0 && brightness <= 255) {
                parkBrightness = brightness;
                saveSettings();
                Serial.printf("Park brightness set to %d\n", brightness);
            } else {
                Serial.println("Invalid brightness (0-255)");
            }
        }
        else if (command.startsWith("park_color ")) {
            String colorStr = command.substring(11);
            if (colorStr.startsWith("headlight ")) {
                String rgbStr = colorStr.substring(10);
                int firstComma = rgbStr.indexOf(',');
                int secondComma = rgbStr.indexOf(',', firstComma + 1);
                if (firstComma > 0 && secondComma > firstComma) {
                    int r = rgbStr.substring(0, firstComma).toInt();
                    int g = rgbStr.substring(firstComma + 1, secondComma).toInt();
                    int b = rgbStr.substring(secondComma + 1).toInt();
                    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                        parkHeadlightColor = CRGB(r, g, b);
                        saveSettings();
                        Serial.printf("Park headlight color set to RGB(%d,%d,%d)\n", r, g, b);
                    } else {
                        Serial.println("Invalid RGB values (0-255)");
                    }
                } else {
                    Serial.println("Invalid format. Use: park_color headlight r,g,b");
                }
            } else if (colorStr.startsWith("taillight ")) {
                String rgbStr = colorStr.substring(10);
                int firstComma = rgbStr.indexOf(',');
                int secondComma = rgbStr.indexOf(',', firstComma + 1);
                if (firstComma > 0 && secondComma > firstComma) {
                    int r = rgbStr.substring(0, firstComma).toInt();
                    int g = rgbStr.substring(firstComma + 1, secondComma).toInt();
                    int b = rgbStr.substring(secondComma + 1).toInt();
                    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                        parkTaillightColor = CRGB(r, g, b);
                        saveSettings();
                        Serial.printf("Park taillight color set to RGB(%d,%d,%d)\n", r, g, b);
                    } else {
                        Serial.println("Invalid RGB values (0-255)");
                    }
                } else {
                    Serial.println("Invalid format. Use: park_color taillight r,g,b");
                }
            } else {
                Serial.println("Usage: park_color headlight r,g,b or park_color taillight r,g,b");
            }
        }
        else if (command == "ota_status") {
            Serial.printf("OTA Status: %s, Progress: %d%%, Error: %s\n", 
                         otaStatus.c_str(), otaProgress, otaError.c_str());
        }
        else if (command == "status") {
            printStatus();
        }
        else if (command == "list_files" || command == "ls") {
            Serial.println("📁 SPIFFS File Listing:");
            listSPIFFSFiles();
        }
        else if (command == "show_settings" || command == "cat_settings") {
            showSettingsFile();
        }
        else if (command == "clean_duplicates") {
            Serial.println("🧹 Cleaning duplicate files...");
            cleanDuplicateFiles();
        }
        else if (command == "help") {
            printHelp();
        }
        else {
            Serial.println("Unknown command. Type 'help' for available commands.");
        }
    }
}

void printStatus() {
    Serial.println("=== ArkLights Status ===");
    Serial.printf("Preset: %d\n", currentPreset);
    Serial.printf("Brightness: %d\n", globalBrightness);
    Serial.printf("Headlight: Effect %d, Color 0x%06X\n", headlightEffect, headlightColor);
    Serial.printf("Taillight: Effect %d, Color 0x%06X\n", taillightEffect, taillightColor);
    Serial.printf("Startup: %s (%d), Duration: %dms\n", getStartupSequenceName(startupSequence).c_str(), startupSequence, startupDuration);
}

void printHelp() {
    Serial.println("Available commands:");
    Serial.printf("  p0-p%d: Set preset by index\n", presetCount > 0 ? (presetCount - 1) : 0);
    Serial.println("  b<0-255>: Set brightness");
    Serial.println("  h<hex>: Set headlight color (e.g., hFF0000)");
    Serial.println("  t<hex>: Set taillight color (e.g., t00FF00)");
    Serial.println("  eh<0-22>: Set headlight effect");
    Serial.println("  et<0-22>: Set taillight effect");
    Serial.println("  startup<0-5>: Set startup sequence");
    Serial.println("  test_startup: Test current startup sequence");
    Serial.println("");
    Serial.println("Motion Control:");
    Serial.println("  calibrate/cal: Start motion calibration");
    Serial.println("  reset_cal: Reset motion calibration");
    Serial.println("  motion_on/off: Enable/disable motion control");
    Serial.println("  blinker_on/off: Enable/disable auto blinkers");
    Serial.println("  park_on/off: Enable/disable park mode");
    Serial.println("  park_effect <0-22>: Set park mode effect");
    Serial.println("  park_speed <0-255>: Set park mode effect speed");
    Serial.println("  park_brightness <0-255>: Set park mode brightness");
    Serial.println("  park_color headlight r,g,b: Set park headlight color");
    Serial.println("  park_color taillight r,g,b: Set park taillight color");
    Serial.println("");
    Serial.println("Group Ride Commands:");
    Serial.println("  group_create [6-digit-code]: Create a group ride");
    Serial.println("  group_join <6-digit-code>: Join a group ride");
    Serial.println("  group_scan_join: Scan and join the first group found");
    Serial.println("  group_leave: Leave current group");
    Serial.println("  group_allow_join: Allow new members to join");
    Serial.println("  group_block_join: Block new members from joining");
    Serial.println("  group_status: Show group status");
    Serial.println("");
    Serial.println("OTA Updates:");
    Serial.println("  ota_status: Show OTA update status");
    Serial.println("");
    Serial.println("System:");
    Serial.println("  status: Show current status");
    Serial.println("  list_files/ls: List SPIFFS files");
    Serial.println("  show_settings/cat_settings: Display settings.json contents");
    Serial.println("  clean_duplicates: Remove duplicate UI files");
    Serial.println("  help: Show this help");
    Serial.println("");
    Serial.println("Startup Sequences:");
    Serial.println("  0=None, 1=Power On, 2=Scanner, 3=Wave, 4=Race, 5=Custom");
    Serial.println("");
    Serial.println("Effects: 0=Solid, 1=Breath, 2=Rainbow, 3=Chase, 4=Blink Rainbow, 5=Twinkle");
    Serial.println("         6=Fire, 7=Meteor, 8=Wave, 9=Comet, 10=Candle, 11=Static Rainbow");
    Serial.println("         12=Knight Rider, 13=Police, 14=Strobe, 15=Larson Scanner");
    Serial.println("         16=Color Wipe, 17=Theater Chase, 18=Running Lights, 19=Color Sweep");
}

void listSPIFFSFiles() {
    Serial.println("📁 SPIFFS File Listing:");
    Serial.println("========================");
    
    File root = SPIFFS.open("/");
    if (!root) {
        Serial.println("❌ Failed to open SPIFFS root directory");
        return;
    }
    
    if (!root.isDirectory()) {
        Serial.println("❌ Root is not a directory");
        root.close();
        return;
    }
    
    File file = root.openNextFile();
    int fileCount = 0;
    size_t totalSize = 0;
    
    while (file) {
        fileCount++;
        totalSize += file.size();
        
        Serial.printf("📄 %-20s %8d bytes", file.name(), file.size());
        
        // Add file type indicator
        String filename = String(file.name());
        if (filename.endsWith(".html") || filename.endsWith(".htm")) {
            Serial.print(" [HTML]");
        } else if (filename.endsWith(".css")) {
            Serial.print(" [CSS]");
        } else if (filename.endsWith(".js")) {
            Serial.print(" [JS]");
        } else if (filename.endsWith(".json")) {
            Serial.print(" [JSON]");
        } else if (filename.endsWith(".txt")) {
            Serial.print(" [TXT]");
        } else if (filename.endsWith(".zip")) {
            Serial.print(" [ZIP]");
        }
        
        Serial.println();
        file = root.openNextFile();
    }
    
    root.close();
    
    Serial.println("========================");
    Serial.printf("📊 Total: %d files, %d bytes\n", fileCount, totalSize);
    
    // Check for UI files specifically
    Serial.println("\n🎨 UI Files Check:");
    String uiFiles[] = {"/ui/index.html", "/ui/styles.css", "/ui/script.js"};
    String rootFiles[] = {"/index.html", "/styles.css", "/script.js"};
    bool allUIFilesExist = true;
    bool hasValidUIFiles = false;
    
    // Check /ui/ directory first
    Serial.println("📁 /ui/ directory:");
    for (int i = 0; i < 3; i++) {
        File uiFile = SPIFFS.open(uiFiles[i], "r");
        if (uiFile) {
            if (uiFile.size() > 0) {
                Serial.printf("✅ %s (%d bytes)\n", uiFiles[i].c_str(), uiFile.size());
                hasValidUIFiles = true;
            } else {
                Serial.printf("⚠️ %s (0 bytes - empty)\n", uiFiles[i].c_str());
            }
            uiFile.close();
        } else {
            Serial.printf("❌ %s (not found)\n", uiFiles[i].c_str());
            allUIFilesExist = false;
        }
    }
    
    // Check root directory as fallback
    Serial.println("📁 Root directory:");
    for (int i = 0; i < 3; i++) {
        File rootFile = SPIFFS.open(rootFiles[i], "r");
        if (rootFile) {
            if (rootFile.size() > 0) {
                Serial.printf("✅ %s (%d bytes)\n", rootFiles[i].c_str(), rootFile.size());
                hasValidUIFiles = true;
            } else {
                Serial.printf("⚠️ %s (0 bytes - empty)\n", rootFiles[i].c_str());
            }
            rootFile.close();
        } else {
            Serial.printf("❌ %s (not found)\n", rootFiles[i].c_str());
        }
    }
    
    if (hasValidUIFiles) {
        Serial.println("🎉 Valid UI files found - external UI should work");
    } else {
        Serial.println("⚠️ No valid UI files found - will use embedded UI fallback");
    }
}

void showSettingsFile() {
    Serial.println("⚙️ Settings.json Contents:");
    Serial.println("===========================");
    
    File file = SPIFFS.open("/settings.json", "r");
    if (!file) {
        Serial.println("❌ settings.json not found");
        return;
    }
    
    // Read and display the entire file
    while (file.available()) {
        String line = file.readStringUntil('\n');
        Serial.println(line);
    }
    
    file.close();
    Serial.println("===========================");
}

void cleanDuplicateFiles() {
    Serial.println("🧹 Cleaning duplicate files...");
    
    // List of files to clean from root directory if they exist in /ui/
    String filesToClean[] = {"index.html", "styles.css", "script.js"};
    int cleanedCount = 0;
    
    for (int i = 0; i < 3; i++) {
        String rootFile = "/" + filesToClean[i];
        String uiFile = "/ui/" + filesToClean[i];
        
        // Check if both files exist
        File rootFileHandle = SPIFFS.open(rootFile, "r");
        File uiFileHandle = SPIFFS.open(uiFile, "r");
        
        if (rootFileHandle && uiFileHandle) {
            Serial.printf("🗑️ Removing duplicate: %s (keeping %s)\n", rootFile.c_str(), uiFile.c_str());
            rootFileHandle.close();
            uiFileHandle.close();
            
            if (SPIFFS.remove(rootFile)) {
                cleanedCount++;
                Serial.printf("✅ Removed: %s\n", rootFile.c_str());
            } else {
                Serial.printf("❌ Failed to remove: %s\n", rootFile.c_str());
            }
        } else {
            if (rootFileHandle) rootFileHandle.close();
            if (uiFileHandle) uiFileHandle.close();
        }
    }
    
    Serial.printf("🧹 Cleanup complete: %d duplicate files removed\n", cleanedCount);
    Serial.println("💡 Use 'ls' command to verify cleanup");
}

// Web Server Implementation
void setupWiFiAP() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apName.c_str(), apPassword.c_str(), espNowChannel, false, MAX_CONNECTIONS);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.printf("AP IP address: %s\n", IP.toString().c_str());
    Serial.printf("Connect to WiFi: %s\n", apName.c_str());
    Serial.printf("Password: %s\n", apPassword.c_str());
}

void updateSoftAPChannel() {
    WiFi.softAP(apName.c_str(), apPassword.c_str(), espNowChannel, false, MAX_CONNECTIONS);
}

void setupBluetooth() {
    if (!bluetoothEnabled) {
        Serial.println("BLE: Disabled");
        return;
    }
    
    // Initialize BLE
    BLEDevice::setMTU(185);
    BLEDevice::init(bluetoothDeviceName.c_str());
    pBLEServer = BLEDevice::createServer();
    pBLEServer->setCallbacks(new MyServerCallbacks());

    // Create BLE Service
    BLEService *pService = pBLEServer->createService("12345678-1234-1234-1234-123456789abc");

    // Create BLE Characteristic
    pCharacteristic = pService->createCharacteristic(
                        "87654321-4321-4321-4321-cba987654321",
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_NOTIFY |
                        BLECharacteristic::PROPERTY_INDICATE
                      );

    BLE2902 *ble2902 = new BLE2902();
    ble2902->setNotifications(true);
    ble2902->setIndications(true);
    pCharacteristic->addDescriptor(ble2902);

    pCharacteristic->setCallbacks(new MyCallbacks());
    pCharacteristic->setValue("ArkLights BLE Service");

    // Start the service
    pService->start();

    // Start advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("12345678-1234-1234-1234-123456789abc");
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE: Initialized successfully");
    Serial.printf("BLE Device Name: %s\n", bluetoothDeviceName.c_str());
    Serial.println("BLE: Ready to accept connections");
}

void processBLEHTTPRequest(String request) {
    Serial.printf("Processing BLE HTTP request: %s\n", request.c_str());
    
    if (request.startsWith("GET /api/status")) {
        // Send status response
        String body = getStatusJSON();
        String statusResponse = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";
        statusResponse += String(body.length());
        statusResponse += "\r\n\r\n";
        statusResponse += body;
        sendBleResponse(statusResponse);
        Serial.println("BLE: Sent status response");
    }
    else if (request.startsWith("POST /api")) {
        // Process API command - this is where most LED control commands come
        Serial.println("BLE: Processing API command");
        
        // Extract JSON body from HTTP request
        int bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart != -1) {
            String jsonBody = request.substring(bodyStart + 4);
            Serial.printf("BLE: JSON body: %s\n", jsonBody.c_str());
            
            // Parse JSON using heap allocation to avoid stack overflow
            DynamicJsonDocument* doc = new DynamicJsonDocument(2048);
            DeserializationError error = deserializeJson(*doc, jsonBody);
            
            if (error) {
                Serial.printf("BLE: JSON parse error: %s\n", error.c_str());
                String body = "{\"error\":\"Invalid JSON\"}";
                String response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: ";
                response += String(body.length());
                response += "\r\n\r\n";
                response += body;
                sendBleResponse(response);
                delete doc;
                return;
            }
            
            if (blePendingApply) {
                String body = "{\"error\":\"Busy\"}";
                String response = "HTTP/1.1 429 Too Many Requests\r\nContent-Type: application/json\r\nContent-Length: ";
                response += String(body.length());
                response += "\r\n\r\n";
                response += body;
                sendBleResponse(response);
                delete doc;
                return;
            }

            portENTER_CRITICAL(&blePendingMutex);
            blePendingJson = jsonBody;
            blePendingApply = true;
            portEXIT_CRITICAL(&blePendingMutex);
            String body = "{\"queued\":true}";
            String response = "HTTP/1.1 202 Accepted\r\nContent-Type: application/json\r\nContent-Length: ";
            response += String(body.length());
            response += "\r\n\r\n";
            response += body;
            sendBleResponse(response);
            Serial.println("BLE: Queued API request");

            delete doc;
        } else {
            String body = "{\"error\":\"No JSON body\"}";
            String response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: ";
            response += String(body.length());
            response += "\r\n\r\n";
            response += body;
            sendBleResponse(response);
        }
    }
    else if (request.startsWith("POST /api/led-config")) {
        // Process LED config
        Serial.println("BLE: Processing LED config");
        String body = "{\"status\":\"ok\"}";
        String response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";
        response += String(body.length());
        response += "\r\n\r\n";
        response += body;
        sendBleResponse(response);
    }
    else if (request.startsWith("POST /api/led-test")) {
        // Process LED test
        Serial.println("BLE: Processing LED test");
        String body = "{\"status\":\"ok\"}";
        String response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";
        response += String(body.length());
        response += "\r\n\r\n";
        response += body;
        sendBleResponse(response);
    }
    else {
        // Default response
        Serial.printf("BLE: Unknown request: %s\n", request.c_str());
        String body = "Not Found";
        String response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: ";
        response += String(body.length());
        response += "\r\n\r\n";
        response += body;
        sendBleResponse(response);
    }
}

void setupWebServer() {
    // Serve the main web page
    server.on("/", handleRoot);
    server.on("/ui/styles.css", handleUI);
    server.on("/ui/script.js", handleUI);
    server.on("/styles.css", handleUI);
    server.on("/script.js", handleUI);
    server.on("/updateui", HTTP_GET, handleUIUpdate);
    server.on("/updateui", HTTP_POST, []() {
        server.send(200, "text/plain", "UI update endpoint ready");
    }, handleUIUpdate);
    
    // API endpoints
    server.on("/api", HTTP_POST, handleAPI);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/led-config", HTTP_POST, handleLEDConfig);
    server.on("/api/led-test", HTTP_POST, handleLEDTest);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/ota-upload", HTTP_POST, []() {
        // This handler is called after handleOTAUpload completes
        // Check actual OTA status to return appropriate response
        // Note: If update was successful, the device restarts in handleOTAUpload
        // so this code only runs if there was an error
        if (otaError.length() > 0) {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            String response = "{\"success\":false,\"error\":\"" + otaError + "\"}";
            server.send(500, "application/json", response);
        } else if (!otaInProgress && otaStatus == "Complete") {
            // Update completed but restart didn't happen yet
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Update complete, restarting...\"}");
        } else {
            // Unknown state - likely still processing
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Upload received\"}");
        }
    }, handleOTAUpload);
    
    // Handle CORS preflight requests
    server.on("/api", HTTP_OPTIONS, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
        server.send(200, "text/plain", "");
    });
    
    server.on("/api/ota-upload", HTTP_OPTIONS, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
        server.send(200, "text/plain", "");
    });
    
    server.begin();
    Serial.println("Web server started");
}

void handleUI() {
    String uri = server.uri();
    
    Serial.printf("🎨 handleUI: Requesting file: %s\n", uri.c_str());
    
    // Extract just the filename from the URI
    String filename = uri;
    if (filename.startsWith("/ui/")) {
        filename = filename.substring(4);  // Remove "/ui/" prefix
    } else if (filename.startsWith("/")) {
        filename = filename.substring(1);  // Remove leading "/"
    }
    
    // Priority 1: Check for SPIFFS override (allows custom UI)
    String filePath = "/ui/" + filename;
    File file = SPIFFS.open(filePath, "r");
    if (file && file.size() > 0) {
        String contentType = "text/plain";
        if (filename.endsWith(".css")) contentType = "text/css";
        else if (filename.endsWith(".js")) contentType = "application/javascript";
        
        Serial.printf("✅ Serving custom file from SPIFFS: %s (%d bytes)\n", filePath.c_str(), file.size());
        server.streamFile(file, contentType);
        file.close();
        return;
    }
    if (file) file.close();
    
    // Try root directory
    filePath = "/" + filename;
    file = SPIFFS.open(filePath, "r");
    if (file && file.size() > 0) {
        String contentType = "text/plain";
        if (filename.endsWith(".css")) contentType = "text/css";
        else if (filename.endsWith(".js")) contentType = "application/javascript";
        
        Serial.printf("✅ Serving custom file from SPIFFS root: %s (%d bytes)\n", filePath.c_str(), file.size());
        server.streamFile(file, contentType);
        file.close();
        return;
    }
    if (file) file.close();
    
    // Priority 2: Serve embedded gzipped file
    if (serveEmbeddedFile(filename.c_str())) {
        return;
    }
    
    // Not found
    Serial.printf("❌ handleUI: File not found: %s\n", uri.c_str());
    server.send(404, "text/plain", "File not found: " + uri);
}

// Serve embedded gzipped file with proper headers
bool serveEmbeddedFile(const char* filename) {
    const EmbeddedFile* file = findEmbeddedFile(filename);
    if (file == nullptr) {
        return false;
    }
    
    Serial.printf("📦 Serving embedded file: %s (%d bytes gzipped)\n", filename, file->length);
    
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "max-age=86400");  // Cache for 1 day
    server.send_P(200, file->contentType, (const char*)file->data, file->length);
    return true;
}

void handleRoot() {
    // Priority 1: Check for SPIFFS override (allows custom UI without reflashing)
    File file = SPIFFS.open("/ui/index.html", "r");
    if (file && file.size() > 0) {
        Serial.printf("✅ Serving custom UI from SPIFFS (%d bytes)\n", file.size());
        server.streamFile(file, "text/html");
        file.close();
        return;
    }
    if (file) file.close();
    
    // Try root directory
    file = SPIFFS.open("/index.html", "r");
    if (file && file.size() > 0) {
        Serial.printf("✅ Serving custom UI from SPIFFS root (%d bytes)\n", file.size());
        server.streamFile(file, "text/html");
        file.close();
        return;
    }
    if (file) file.close();
    
    // Priority 2: Serve embedded gzipped file (bundled with firmware)
    if (serveEmbeddedFile("index.html")) {
        return;
    }
    
    // Priority 3: Fallback to embedded HTML string (minimal UI)
    Serial.println("⚠️ Serving minimal embedded UI fallback");
    serveEmbeddedUI();
}

void serveEmbeddedUI() {
    Serial.println("🎨 serveEmbeddedUI: Serving embedded UI fallback");
    // Embedded HTML as fallback - this will be updated with OTA
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ArkLights PEV Control v8.0 OTA</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 600px; margin: 0 auto; }
        .section { background: #2a2a2a; padding: 20px; margin: 10px 0; border-radius: 8px; }
        .preset-btn { background: #4CAF50; color: white; padding: 10px 20px; margin: 5px; border: none; border-radius: 4px; cursor: pointer; }
        .preset-btn:hover { background: #45a049; }
        .control-group { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; }
        input[type="range"] { width: 100%; }
        input[type="color"] { width: 50px; height: 30px; }
        select { padding: 8px; border-radius: 4px; background: #333; color: #fff; border: 1px solid #555; }
        .status { background: #333; padding: 10px; border-radius: 4px; margin: 10px 0; }
        h1 { color: #4CAF50; text-align: center; }
        h2 { color: #81C784; }
        .warning { background: #ff9800; color: #000; padding: 10px; border-radius: 4px; margin: 10px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ArkLights PEV Control v8.0 OTA</h1>
        <div class="warning">
            ⚠️ Using embedded UI (fallback mode). Upload UI files to SPIFFS for better performance.
        </div>
        <div style="text-align: center; margin: 10px 0; padding: 10px; background: rgba(255,255,255,0.1); border-radius: 8px;">
            <strong>Firmware Version:</strong> v8.0 OTA | <strong>Build Date:</strong> <span id="buildDate">Loading...</span>
        </div>
        
        <!-- Simplified UI for embedded mode -->
        <div class="section">
            <h2>Presets</h2>
            <button class="preset-btn" onclick="setPreset(0)">Standard</button>
            <button class="preset-btn" onclick="setPreset(1)">Night</button>
            <button class="preset-btn" onclick="setPreset(2)">Party</button>
            <button class="preset-btn" onclick="setPreset(3)">Stealth</button>
        </div>
        
        <div class="section">
            <h2>Brightness</h2>
            <div class="control-group">
                <label>Global Brightness: <span id="brightnessValue">128</span></label>
                <input type="range" id="brightness" min="0" max="255" value="128" onchange="setBrightness(this.value)">
            </div>
        </div>
        
        <div class="section">
            <h2>OTA Updates</h2>
            <div class="control-group">
                <label>Firmware File:</label>
                <input type="file" id="otaFileInput" accept=".bin" onchange="handleFileSelect(this)">
                <small>Select firmware binary file (.bin)</small>
            </div>
            <div class="control-group">
                <button onclick="startOTAUpdate()" id="startOTAButton" style="background: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 5px;" disabled>
                    Upload & Install
                </button>
                <small>Upload and install firmware file</small>
            </div>
        </div>
        
        <div class="section">
            <h2>📐 Calibration</h2>
            <div id="calibrationStatus" class="status">
                Status: <span id="calibrationStatusText">Not calibrated</span>
            </div>
            
            <div id="calibrationProgress" style="display: none; margin: 15px 0;">
                <div style="width: 100%; height: 20px; background: #ddd; border-radius: 10px; overflow: hidden;">
                    <div id="calibrationProgressBar" style="height: 100%; background: #4CAF50; width: 0%; transition: width 0.3s;"></div>
                </div>
                <div id="calibrationStepText" style="margin-top: 10px; font-weight: bold;">Step 1: Hold device LEVEL</div>
            </div>
            
            <div style="margin-top: 15px;">
                <button onclick="startCalibration()" id="startCalibrationBtn" style="background: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 5px; margin-right: 10px;">Start Calibration</button>
                <button onclick="nextCalibrationStep()" id="nextCalibrationBtn" style="background: #2196F3; color: white; padding: 10px 20px; border: none; border-radius: 5px; margin-right: 10px; display: none;">Next Step</button>
                <button onclick="resetCalibration()" id="resetCalibrationBtn" style="background: #f44336; color: white; padding: 10px 20px; border: none; border-radius: 5px;">Reset Calibration</button>
            </div>
        </div>
        
        <div class="section">
            <h2>🎯 Motion Control</h2>
            <div class="control-group">
                <label>
                    <input type="checkbox" id="motionEnabled" onchange="updateMotionSettings()">
                    Enable Motion Control
                </label>
            </div>
            <div class="control-group">
                <label>
                    <input type="checkbox" id="blinkerEnabled" onchange="updateMotionSettings()">
                    Enable Auto Blinkers
                </label>
            </div>
            <div class="control-group">
                <label>
                    <input type="checkbox" id="parkModeEnabled" onchange="updateMotionSettings()">
                    Enable Park Mode
                </label>
            </div>
            <div class="control-group">
                <label>
                    <input type="checkbox" id="impactDetectionEnabled" onchange="updateMotionSettings()">
                    Enable Impact Detection
                </label>
            </div>
        </div>
        
        <div class="section">
            <h2>🅿️ Park Mode Settings</h2>
            <div class="control-group">
                <label>Park Effect: <span id="parkEffectValue">1</span></label>
                <select id="parkEffect" onchange="updateParkSettings()">
                    <option value="0">Solid</option>
                    <option value="1">Breath</option>
                    <option value="2">Rainbow</option>
                    <option value="3">Chase</option>
                    <option value="4">Blink Rainbow</option>
                    <option value="5">Twinkle</option>
                    <option value="6">Fire</option>
                    <option value="7">Meteor</option>
                    <option value="8">Wave</option>
                    <option value="9">Comet</option>
                    <option value="10">Candle</option>
                    <option value="11">Static Rainbow</option>
                    <option value="12">Knight Rider</option>
                    <option value="13">Police</option>
                    <option value="14">Strobe</option>
                    <option value="15">Larson Scanner</option>
                    <option value="16">Color Wipe</option>
                    <option value="17">Theater Chase</option>
                    <option value="18">Running Lights</option>
                    <option value="19">Color Sweep</option>
                </select>
            </div>
            <div class="control-group">
                <label>Park Effect Speed: <span id="parkSpeedValue">64</span></label>
                <input type="range" id="parkSpeed" min="0" max="255" value="64" onchange="updateParkSettings()">
            </div>
            <div class="control-group">
                <label>Park Brightness: <span id="parkBrightnessValue">128</span></label>
                <input type="range" id="parkBrightness" min="0" max="255" value="128" onchange="updateParkSettings()">
            </div>
            <div class="control-group">
                <label>Park Headlight Color:</label>
                <input type="color" id="parkHeadlightColor" value="#0000ff" onchange="updateParkSettings()">
            </div>
            <div class="control-group">
                <label>Park Taillight Color:</label>
                <input type="color" id="parkTaillightColor" value="#0000ff" onchange="updateParkSettings()">
            </div>
        </div>
        
        <div class="section">
            <h2>Status</h2>
            <div class="status" id="status">Loading...</div>
            <button onclick="updateStatus()">Refresh Status</button>
        </div>
    </div>
    
    <script>
        function setPreset(preset) {
            fetch('/api', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ preset: preset })
            }).then(() => updateStatus());
        }
        
        function setBrightness(value) {
            document.getElementById('brightnessValue').textContent = value;
            fetch('/api', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ brightness: parseInt(value) })
            });
        }
        
        function handleFileSelect(input) {
            const file = input.files[0];
            const button = document.getElementById('startOTAButton');
            
            if (file) {
                if (file.name.endsWith('.bin')) {
                    button.disabled = false;
                    button.textContent = `Upload & Install (${(file.size / 1024 / 1024).toFixed(1)}MB)`;
                } else {
                    alert('Please select a .bin file');
                    input.value = '';
                    button.disabled = true;
                    button.textContent = 'Upload & Install';
                }
            } else {
                button.disabled = true;
                button.textContent = 'Upload & Install';
            }
        }
        
        function startOTAUpdate() {
            const fileInput = document.getElementById('otaFileInput');
            const file = fileInput.files[0];
            
            if (!file) {
                alert('Please select a firmware file first');
                return;
            }
            
            if (!confirm('This will restart the device. Continue?')) {
                return;
            }
            
            const formData = new FormData();
            formData.append('firmware', file);
            
            fetch('/api/ota-upload', {
                method: 'POST',
                body: formData
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    if (data.message && data.message.includes('restarting')) {
                        alert('✅ Firmware update completed successfully! The device is restarting with the new firmware.');
                        setTimeout(() => {
                            window.location.reload();
                        }, 5000);
                    }
                } else {
                    alert('Upload failed: ' + (data.error || 'Unknown error'));
                }
            })
            .catch(error => {
                alert('Upload error: ' + error);
            });
        }
        
        function startCalibration() {
            fetch('/api', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ startCalibration: true })
            })
            .then(response => response.json())
            .then(data => {
                console.log('Calibration started:', data);
                document.getElementById('calibrationProgress').style.display = 'block';
                document.getElementById('startCalibrationBtn').style.display = 'none';
                document.getElementById('nextCalibrationBtn').style.display = 'inline-block';
                document.getElementById('calibrationStatusText').textContent = 'In Progress';
                updateStatus();
            })
            .catch(error => {
                console.error('Error starting calibration:', error);
            });
        }
        
        function nextCalibrationStep() {
            const nextBtn = document.getElementById('nextCalibrationBtn');
            nextBtn.disabled = true;
            nextBtn.textContent = 'Capturing...';
            
            fetch('/api', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ nextCalibrationStep: true })
            })
            .then(response => response.json())
            .then(data => {
                console.log('Next calibration step sent:', data);
                nextBtn.disabled = false;
                nextBtn.textContent = 'Next Step';
                updateStatus();
            })
            .catch(error => {
                console.error('Error sending next calibration step:', error);
                nextBtn.disabled = false;
                nextBtn.textContent = 'Next Step';
            });
        }
        
        function resetCalibration() {
            fetch('/api', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ resetCalibration: true })
            })
            .then(response => response.json())
            .then(data => {
                console.log('Calibration reset:', data);
                document.getElementById('calibrationProgress').style.display = 'none';
                document.getElementById('startCalibrationBtn').style.display = 'inline-block';
                document.getElementById('nextCalibrationBtn').style.display = 'none';
                document.getElementById('calibrationStatusText').textContent = 'Not calibrated';
                document.getElementById('calibrationProgressBar').style.width = '0%';
                updateStatus();
            })
            .catch(error => {
                console.error('Error resetting calibration:', error);
            });
        }
        
        function updateStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('status').innerHTML = 
                        `Preset: ${data.preset}<br>` +
                        `Brightness: ${data.brightness}<br>` +
                        `Firmware: ${data.firmware_version}<br>` +
                        `Build Date: ${data.build_date}<br>` +
                        `Calibration: ${data.calibration_complete ? 'Complete' : 'Not calibrated'}`;
                    
                    document.getElementById('brightness').value = data.brightness;
                    document.getElementById('brightnessValue').textContent = data.brightness;
                    document.getElementById('buildDate').textContent = data.build_date || 'Unknown';
                    
                    // Update motion control settings
                    document.getElementById('motionEnabled').checked = data.motion_enabled;
                    document.getElementById('blinkerEnabled').checked = data.blinker_enabled;
                    document.getElementById('parkModeEnabled').checked = data.park_mode_enabled;
                    document.getElementById('impactDetectionEnabled').checked = data.impact_detection_enabled;
                    
                    // Update park mode settings
                    document.getElementById('parkEffect').value = data.park_effect;
                    document.getElementById('parkEffectValue').textContent = data.park_effect;
                    document.getElementById('parkSpeed').value = data.park_effect_speed;
                    document.getElementById('parkSpeedValue').textContent = data.park_effect_speed;
                    document.getElementById('parkBrightness').value = data.park_brightness;
                    document.getElementById('parkBrightnessValue').textContent = data.park_brightness;
                    document.getElementById('parkHeadlightColor').value = rgbToHex(data.park_headlight_color_r, data.park_headlight_color_g, data.park_headlight_color_b);
                    document.getElementById('parkTaillightColor').value = rgbToHex(data.park_taillight_color_r, data.park_taillight_color_g, data.park_taillight_color_b);
                    
                    // Update calibration UI
                    if (data.calibration_mode) {
                        console.log('Calibration mode active, step:', data.calibration_step);
                        document.getElementById('calibrationProgress').style.display = 'block';
                        document.getElementById('startCalibrationBtn').style.display = 'none';
                        document.getElementById('nextCalibrationBtn').style.display = 'inline-block';
                        document.getElementById('calibrationStatusText').textContent = 'In Progress';
                        
                        const currentStep = data.calibration_step;
                        const progress = ((currentStep + 1) / 5) * 100;
                        document.getElementById('calibrationProgressBar').style.width = progress + '%';
                        
                        const stepTexts = [
                            'Hold device LEVEL',
                            'Tilt FORWARD', 
                            'Tilt BACKWARD',
                            'Tilt LEFT',
                            'Tilt RIGHT'
                        ];
                        const currentStepNumber = data.calibration_step + 1;
                        const stepIndex = Math.min(data.calibration_step, 4);
                        const stepDescription = stepTexts[stepIndex] || 'Calibrating...';
                        
                        document.getElementById('calibrationStepText').textContent = `Step ${currentStepNumber}/5: ${stepDescription}`;
                        console.log('Updated UI - Step:', currentStepNumber, 'Progress:', progress + '%');
                    } else {
                        document.getElementById('calibrationProgress').style.display = 'none';
                        document.getElementById('startCalibrationBtn').style.display = 'inline-block';
                        document.getElementById('nextCalibrationBtn').style.display = 'none';
                        document.getElementById('calibrationStatusText').textContent = data.calibration_complete ? 'Complete' : 'Not calibrated';
                    }
                });
        }
        
        function updateMotionSettings() {
            const motionEnabled = document.getElementById('motionEnabled').checked;
            const blinkerEnabled = document.getElementById('blinkerEnabled').checked;
            const parkModeEnabled = document.getElementById('parkModeEnabled').checked;
            const impactDetectionEnabled = document.getElementById('impactDetectionEnabled').checked;
            
            fetch('/api', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    motion_enabled: motionEnabled,
                    blinker_enabled: blinkerEnabled,
                    park_mode_enabled: parkModeEnabled,
                    impact_detection_enabled: impactDetectionEnabled
                })
            });
        }
        
        function updateParkSettings() {
            const parkEffect = document.getElementById('parkEffect').value;
            const parkSpeed = document.getElementById('parkSpeed').value;
            const parkBrightness = document.getElementById('parkBrightness').value;
            const parkHeadlightColor = document.getElementById('parkHeadlightColor').value;
            const parkTaillightColor = document.getElementById('parkTaillightColor').value;
            
            // Update display values
            document.getElementById('parkEffectValue').textContent = parkEffect;
            document.getElementById('parkSpeedValue').textContent = parkSpeed;
            document.getElementById('parkBrightnessValue').textContent = parkBrightness;
            
            // Convert hex colors to RGB
            const headlightRGB = hexToRgb(parkHeadlightColor);
            const taillightRGB = hexToRgb(parkTaillightColor);
            
            fetch('/api', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    park_effect: parseInt(parkEffect),
                    park_effect_speed: parseInt(parkSpeed),
                    park_brightness: parseInt(parkBrightness),
                    park_headlight_color_r: headlightRGB.r,
                    park_headlight_color_g: headlightRGB.g,
                    park_headlight_color_b: headlightRGB.b,
                    park_taillight_color_r: taillightRGB.r,
                    park_taillight_color_g: taillightRGB.g,
                    park_taillight_color_b: taillightRGB.b
                })
            });
        }
        
        function hexToRgb(hex) {
            const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
            return result ? {
                r: parseInt(result[1], 16),
                g: parseInt(result[2], 16),
                b: parseInt(result[3], 16)
            } : {r: 0, g: 0, b: 0};
        }
        
        function rgbToHex(r, g, b) {
            return "#" + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
        }
        
        updateStatus();
        setInterval(updateStatus, 5000);
    </script>
</body>
</html>
)rawliteral";
    
    Serial.printf("📤 serveEmbeddedUI: Sending HTML response (%d bytes)\n", html.length());
    server.send(200, "text/html", html);
    Serial.println("✅ serveEmbeddedUI: Response sent successfully");
}

void handleUIUpdate() {
    if (server.method() == HTTP_GET) {
        // Serve UI update page
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ArkLights UI Update</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a2e; color: white; }
        .container { max-width: 600px; margin: 0 auto; }
        .control { margin: 20px 0; padding: 20px; background: rgba(255,255,255,0.1); border-radius: 8px; }
        button { padding: 12px 24px; margin: 8px; background: #667eea; color: white; border: none; border-radius: 5px; cursor: pointer; }
        button:hover { background: #764ba2; }
        .status { padding: 15px; margin: 15px 0; border-radius: 8px; }
        .success { background: rgba(76,175,80,0.2); border: 1px solid rgba(76,175,80,0.5); }
        .error { background: rgba(244,67,54,0.2); border: 1px solid rgba(244,67,54,0.5); }
        .info { background: rgba(33,150,243,0.2); border: 1px solid rgba(33,150,243,0.5); }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎨 ArkLights UI Update</h1>
        <div class="control">
            <h3>Update Interface Files</h3>
            <p>Upload a ZIP file containing updated UI files. This will update the web interface without requiring a full firmware update.</p>
            
            <form id="updateForm" enctype="multipart/form-data">
                <input type="file" id="uiFile" accept=".zip,.txt" required>
                <button type="submit">Update UI</button>
            </form>
            
            <div id="status" style="display: none;"></div>
        </div>
        
        <div class="control">
            <h3>Current UI Files</h3>
            <p>Files that can be updated:</p>
            <ul>
                <li><strong>Main Interface:</strong></li>
                <li>ui/index.html - Main ArkLights interface</li>
                <li>ui/styles.css - ArkLights stylesheet</li>
                <li>ui/script.js - ArkLights JavaScript</li>
                <li><strong>Custom Files:</strong></li>
                <li>Any custom CSS/JS/HTML files</li>
            </ul>
            <p><em>Note: Filesystem versions override embedded versions. If a file doesn't exist in filesystem, the embedded version is used.</em></p>
        </div>
        
        <div class="control">
            <button onclick="window.location.href='/'">Back to Main Interface</button>
        </div>
    </div>

    <script>
        document.getElementById('updateForm').addEventListener('submit', function(e) {
            e.preventDefault();
            
            const fileInput = document.getElementById('uiFile');
            const statusDiv = document.getElementById('status');
            
            if (!fileInput.files[0]) {
                showStatus('Please select a ZIP or TXT file', 'error');
                return;
            }
            
            const formData = new FormData();
            formData.append('uiupdate', fileInput.files[0]);
            
            showStatus('Uploading and updating UI files...', 'info');
            
            fetch('/updateui', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(data => {
                if (data.includes('success')) {
                    showStatus('UI update successful! The interface has been updated.', 'success');
                } else {
                    showStatus('Update failed: ' + data, 'error');
                }
            })
            .catch(error => {
                showStatus('Upload failed: ' + error.message, 'error');
            });
        });
        
        function showStatus(message, type) {
            const statusDiv = document.getElementById('status');
            statusDiv.innerHTML = message;
            statusDiv.className = 'status ' + type;
            statusDiv.style.display = 'block';
        }
    </script>
</body>
</html>
        )rawliteral";
        
        server.send(200, "text/html", html);
    } else if (server.method() == HTTP_POST) {
        // Handle file upload
        HTTPUpload& upload = server.upload();
        
        static File updateFile;
        static String updatePath;
        
        if (upload.status == UPLOAD_FILE_START) {
            updatePath = "/ui_update_" + String(millis()) + ".zip";
            updateFile = SPIFFS.open(updatePath, "w");
            Serial.printf("Starting UI update: %s\n", updatePath.c_str());
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (updateFile) {
                updateFile.write(upload.buf, upload.currentSize);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (updateFile) {
                updateFile.close();
                Serial.println("UI update file received, processing...");
                
                if (processUIUpdate(updatePath)) {
                    server.send(200, "text/plain", "UI update successful!");
                    SPIFFS.remove(updatePath);
                } else {
                    server.send(500, "text/plain", "UI update failed - could not process files");
                    SPIFFS.remove(updatePath);
                }
            }
        }
    }
}

bool processUIUpdate(const String& updatePath) {
    Serial.printf("Processing UI update: %s\n", updatePath.c_str());
    
    File updateFile = SPIFFS.open(updatePath, "r");
    if (!updateFile) {
        Serial.println("Failed to open update file");
        return false;
    }
    
    // Check file size first
    size_t fileSize = updateFile.size();
    Serial.printf("Update file size: %d bytes\n", fileSize);
    
    updateFile.close();
    
    // If file is large (>30KB), use streaming mode
    if (fileSize > 30000) {
        Serial.println("Large file detected, using streaming mode");
        return processUIUpdateStreaming(updatePath);
    }
    
    // For smaller files, use the original method
    Serial.println("Small file, using original method");
    
    updateFile = SPIFFS.open(updatePath, "r");
    String content = updateFile.readString();
    updateFile.close();
    
    // Process text-based update format: FILENAME:CONTENT:ENDFILE
    if (content.indexOf("FILENAME:") == 0) {
        Serial.println("Processing text-based UI update");
        
        int pos = 0;
        int filesProcessed = 0;
        
        while (pos < content.length()) {
            int filenameStart = content.indexOf("FILENAME:", pos);
            if (filenameStart == -1) break;
            
            int filenameEnd = content.indexOf(":", filenameStart + 9);
            if (filenameEnd == -1) break;
            
            String filename = content.substring(filenameStart + 9, filenameEnd);
            
            int contentStart = filenameEnd + 1;
            int contentEnd = content.indexOf(":ENDFILE", contentStart);
            if (contentEnd == -1) break;
            
            String fileContent = content.substring(contentStart, contentEnd);
            
            if (saveUIFile(filename, fileContent)) {
                filesProcessed++;
            }
            
            pos = contentEnd + 8; // Move past :ENDFILE
        }
        
        Serial.printf("UI update completed successfully - %d files processed\n", filesProcessed);
        return filesProcessed > 0;
    }
    
    // For ZIP files, we'd need a ZIP library - for now just return success
    Serial.println("ZIP update format not yet implemented - use text format");
    return false;
}

bool applyApiJson(DynamicJsonDocument& doc, bool allowRestart, bool& shouldRestart) {
    shouldRestart = false;

    if (doc.containsKey("preset")) {
        setPreset(doc["preset"]);
        saveSettings(); // Auto-save
    }

    if (doc.containsKey("presetAction")) {
        String action = doc["presetAction"].as<String>();
        if (action == "save") {
            String name = doc["presetName"] | "Custom Preset";
            if (addPreset(name)) {
                saveSettings();
            }
        } else if (action == "update") {
            int index = doc["presetIndex"] | -1;
            String name = doc["presetName"] | "";
            if (index >= 0 && updatePreset((uint8_t)index, name)) {
                saveSettings();
            }
        } else if (action == "delete") {
            int index = doc["presetIndex"] | -1;
            if (index >= 0 && deletePreset((uint8_t)index)) {
                saveSettings();
            }
        }
    }
    if (doc.containsKey("brightness")) {
        globalBrightness = doc["brightness"];
        FastLED.setBrightness(globalBrightness);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("effectSpeed")) {
        effectSpeed = doc["effectSpeed"];
        saveSettings(); // Auto-save
    }

    bool configChanged = false;
    if (doc.containsKey("headlightLedCount")) {
        uint8_t newHeadlightCount = doc["headlightLedCount"];
        if (newHeadlightCount != headlightLedCount) {
            Serial.printf("LED Config: Headlight count changed from %d to %d\n", headlightLedCount, newHeadlightCount);
            headlightLedCount = newHeadlightCount;
            configChanged = true;
        }
    }
    if (doc.containsKey("taillightLedCount")) {
        uint8_t newTaillightCount = doc["taillightLedCount"];
        if (newTaillightCount != taillightLedCount) {
            Serial.printf("LED Config: Taillight count changed from %d to %d\n", taillightLedCount, newTaillightCount);
            taillightLedCount = newTaillightCount;
            configChanged = true;
        }
    }
    if (doc.containsKey("headlightLedType")) {
        headlightLedType = doc["headlightLedType"];
        configChanged = true;
    }
    if (doc.containsKey("taillightLedType")) {
        taillightLedType = doc["taillightLedType"];
        configChanged = true;
    }
    if (doc.containsKey("headlightColorOrder")) {
        headlightColorOrder = doc["headlightColorOrder"];
        configChanged = true;
    }
    if (doc.containsKey("taillightColorOrder")) {
        taillightColorOrder = doc["taillightColorOrder"];
        configChanged = true;
    }
    if (configChanged) {
        saveSettings();
        initializeLEDs();
        Serial.println("LED configuration updated and applied!");
    }
    if (doc.containsKey("startup_sequence")) {
        startupSequence = doc["startup_sequence"];
        startupEnabled = (startupSequence != STARTUP_NONE);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("startup_duration")) {
        startupDuration = doc["startup_duration"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("testStartup") && doc["testStartup"]) {
        startStartupSequence();
    }
    if (doc.containsKey("testParkMode") && doc["testParkMode"]) {
        // Temporarily activate park mode for testing
        parkModeActive = true;
        parkStartTime = millis(); // Reset timer
        Serial.println("🅿️ Test park mode activated");
    }
    if (doc.containsKey("testLEDs") && doc["testLEDs"]) {
        testLEDConfiguration();
    }
    
    // Motion control API
    if (doc.containsKey("motion_enabled")) {
        bool newMotionEnabled = doc["motion_enabled"];
        
        // If motion control is being disabled, deactivate all motion features
        if (motionEnabled && !newMotionEnabled) {
            if (parkModeActive) {
                parkModeActive = false;
                parkStartTime = 0;
                resetToNormalEffects(); // Reset LEDs to normal state
                Serial.println("🅿️ Motion control disabled - deactivating park mode");
            }
            if (blinkerActive) {
                blinkerActive = false;
                blinkerStartTime = 0;
                manualBlinkerActive = false;
                resetToNormalEffects(); // Reset LEDs to normal state
                Serial.println("🚦 Motion control disabled - deactivating blinkers");
            }
        }
        
        motionEnabled = newMotionEnabled;
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("blinker_enabled")) {
        bool newBlinkerEnabled = doc["blinker_enabled"];
        
        // If blinkers are being disabled and they're currently active, deactivate them
        if (blinkerEnabled && !newBlinkerEnabled && blinkerActive) {
            blinkerActive = false;
            blinkerStartTime = 0;
            manualBlinkerActive = false;
            resetToNormalEffects(); // Reset LEDs to normal state
            Serial.println("🚦 Blinkers disabled - deactivating current blinker");
        }
        
        blinkerEnabled = newBlinkerEnabled;
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_mode_enabled")) {
        bool newParkModeEnabled = doc["park_mode_enabled"];
        
        // If park mode is being disabled and it's currently active, deactivate it
        if (parkModeEnabled && !newParkModeEnabled && parkModeActive) {
            parkModeActive = false;
            parkStartTime = 0;
            resetToNormalEffects(); // Reset LEDs to normal state
            Serial.println("🅿️ Park mode disabled - deactivating current park mode");
        }
        
        parkModeEnabled = newParkModeEnabled;
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("impact_detection_enabled")) {
        impactDetectionEnabled = doc["impact_detection_enabled"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("motion_sensitivity")) {
        motionSensitivity = doc["motion_sensitivity"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("blinker_delay")) {
        blinkerDelay = doc["blinker_delay"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("blinker_timeout")) {
        blinkerTimeout = doc["blinker_timeout"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("manualBlinker")) {
        String manual = doc["manualBlinker"].as<String>();
        if (manual == "left" || manual == "right") {
            manualBlinkerActive = true;
            blinkerActive = true;
            blinkerDirection = (manual == "right") ? 1 : -1;
            blinkerStartTime = millis();
        } else if (manual == "off") {
            manualBlinkerActive = false;
            blinkerActive = false;
            blinkerDirection = 0;
            blinkerStartTime = 0;
            resetToNormalEffects();
        }
    }
    if (doc.containsKey("park_detection_angle")) {
        parkDetectionAngle = doc["park_detection_angle"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_stationary_time")) {
        parkStationaryTime = doc["park_stationary_time"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_accel_noise_threshold")) {
        parkAccelNoiseThreshold = doc["park_accel_noise_threshold"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_gyro_noise_threshold")) {
        parkGyroNoiseThreshold = doc["park_gyro_noise_threshold"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_effect")) {
        parkEffect = doc["park_effect"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_effect_speed")) {
        parkEffectSpeed = doc["park_effect_speed"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_headlight_color_r")) {
        parkHeadlightColor.r = doc["park_headlight_color_r"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_headlight_color_g")) {
        parkHeadlightColor.g = doc["park_headlight_color_g"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_headlight_color_b")) {
        parkHeadlightColor.b = doc["park_headlight_color_b"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_taillight_color_r")) {
        parkTaillightColor.r = doc["park_taillight_color_r"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_taillight_color_g")) {
        parkTaillightColor.g = doc["park_taillight_color_g"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_taillight_color_b")) {
        parkTaillightColor.b = doc["park_taillight_color_b"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("park_brightness")) {
        parkBrightness = doc["park_brightness"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("braking_enabled")) {
        brakingEnabled = doc["braking_enabled"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("manualBrake")) {
        bool manual = doc["manualBrake"];
        manualBrakeActive = manual;
        brakingActive = manual;
        brakingStartTime = millis();
        brakingFlashCount = 0;
        brakingPulseCount = 0;
        if (!manual) {
            resetToNormalEffects();
        }
    }
    if (doc.containsKey("braking_threshold")) {
        brakingThreshold = doc["braking_threshold"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("braking_effect")) {
        brakingEffect = doc["braking_effect"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("braking_brightness")) {
        brakingBrightness = doc["braking_brightness"] | 255;
        Serial.printf("🛑 Braking brightness: %d\n", brakingBrightness);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("direction_based_lighting")) {
        directionBasedLighting = doc["direction_based_lighting"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("headlight_mode")) {
        headlightMode = doc["headlight_mode"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("forward_accel_threshold")) {
        forwardAccelThreshold = doc["forward_accel_threshold"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("rgbw_white_mode")) {
        setRgbwWhiteMode(doc["rgbw_white_mode"] | 0);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("white_leds_enabled") && !doc.containsKey("rgbw_white_mode")) {
        setRgbwWhiteMode(doc["white_leds_enabled"] ? 1 : 0);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("headlightColor")) {
        String colorHex = doc["headlightColor"];
        uint32_t color = strtol(colorHex.c_str(), NULL, 16);
        headlightColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("taillightColor")) {
        String colorHex = doc["taillightColor"];
        uint32_t color = strtol(colorHex.c_str(), NULL, 16);
        taillightColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("headlightBackgroundColor")) {
        String colorHex = doc["headlightBackgroundColor"];
        uint32_t color = strtol(colorHex.c_str(), NULL, 16);
        headlightBackgroundColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("taillightBackgroundColor")) {
        String colorHex = doc["taillightBackgroundColor"];
        uint32_t color = strtol(colorHex.c_str(), NULL, 16);
        taillightBackgroundColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("headlightBackgroundEnabled")) {
        headlightBackgroundEnabled = doc["headlightBackgroundEnabled"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("taillightBackgroundEnabled")) {
        taillightBackgroundEnabled = doc["taillightBackgroundEnabled"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("headlightEffect")) {
        headlightEffect = doc["headlightEffect"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("taillightEffect")) {
        taillightEffect = doc["taillightEffect"];
        saveSettings(); // Auto-save
    }
    
    // ESPNow API
    if (doc.containsKey("enableESPNow")) {
        enableESPNow = doc["enableESPNow"];
        saveSettings(); // Auto-save
        if (enableESPNow) {
            initESPNow();
        } else {
            deinitESPNow();
        }
    }
    if (doc.containsKey("useESPNowSync")) {
        useESPNowSync = doc["useESPNowSync"];
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("espNowChannel")) {
        uint8_t newChannel = doc["espNowChannel"];
        if (newChannel != espNowChannel) {
            espNowChannel = newChannel;
            saveSettings(); // Auto-save
            updateSoftAPChannel();
            if (enableESPNow) {
                initESPNow();
            }
        }
    }
    
    // Group Management API
    if (doc.containsKey("deviceName")) {
        deviceName = doc["deviceName"].as<String>();
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("groupAction")) {
        String action = doc["groupAction"].as<String>();
        if (action == "create") {
            String code = doc["groupCode"] | "";
            groupCode = code;
            if (groupCode.length() != 6) {
                groupCode = "";
                generateGroupCode();
            }
            isGroupMaster = true;
            allowGroupJoin = true;
            hasGroupMaster = true;
            autoJoinOnHeartbeat = false;
            joinInProgress = false;
            groupMemberCount = 0;
            esp_wifi_get_mac(WIFI_IF_STA, groupMasterMac);
            // Add self as a group member when creating a group
            uint8_t mac[6];
            esp_wifi_get_mac(WIFI_IF_STA, mac);
            addGroupMember(mac, deviceName.c_str());
            Serial.printf("Group: Created with code %s and joined as master\n", groupCode.c_str());
        } else if (action == "join" && doc.containsKey("groupCode")) {
            String code = doc["groupCode"].as<String>();
            if (code.length() == 6) {
                groupCode = code;
                isGroupMaster = false;
                hasGroupMaster = false;
                autoJoinOnHeartbeat = false;
                joinInProgress = true;
                memset(groupMasterMac, 0, sizeof(groupMasterMac));
                groupMemberCount = 0;
                sendJoinRequest();
                Serial.printf("Group: Attempting to join with code %s\n", groupCode.c_str());
            }
        } else if (action == "scan_join") {
            groupCode = "";
            isGroupMaster = false;
            hasGroupMaster = false;
            allowGroupJoin = false;
            autoJoinOnHeartbeat = true;
            joinInProgress = false;
            memset(groupMasterMac, 0, sizeof(groupMasterMac));
            groupMemberCount = 0;
            Serial.println("Group: Scanning for group heartbeat to join");
        } else if (action == "leave") {
            groupCode = "";
            isGroupMaster = false;
            allowGroupJoin = false;
            groupMemberCount = 0;
            hasGroupMaster = false;
            autoJoinOnHeartbeat = false;
            joinInProgress = false;
            memset(groupMasterMac, 0, sizeof(groupMasterMac));
            Serial.println("Group: Left group");
        } else if (action == "allow_join") {
            allowGroupJoin = true;
            Serial.println("Group: Join requests enabled");
        } else if (action == "block_join") {
            allowGroupJoin = false;
            Serial.println("Group: Join requests disabled");
        }
        saveSettings(); // Auto-save
    }

    // Calibration API (required for BLE/Android app)
    if (doc.containsKey("startCalibration") && doc["startCalibration"]) {
        startCalibration();
        Serial.println("BLE: Starting motion calibration...");
    }
    if (doc.containsKey("resetCalibration") && doc["resetCalibration"]) {
        resetCalibration();
        Serial.println("BLE: Motion calibration reset");
    }
    if (doc.containsKey("nextCalibrationStep") && doc["nextCalibrationStep"]) {
        if (calibrationMode) {
            MotionData data = getMotionData();
            captureCalibrationStep(data);
        }
    }

    // WiFi settings
    if (doc.containsKey("apName")) {
        apName = doc["apName"].as<String>();
        bluetoothDeviceName = apName;  // Keep BLE name in sync with AP name
        Serial.printf("🔧 WiFi AP Name updated to: %s (BLE will use on restart)\n", apName.c_str());
        saveSettings(); // Auto-save
    }
    if (doc.containsKey("apPassword")) {
        apPassword = doc["apPassword"].as<String>();
        Serial.printf("🔧 WiFi AP Password updated to: %s\n", apPassword.c_str());
        saveSettings(); // Auto-save
    }

    if (doc.containsKey("restoreDefaults") && doc["restoreDefaults"]) {
        restoreDefaultsToStock();
        delay(500);  // Allow response to be sent
        ESP.restart();
        return true;
    }

    if (doc.containsKey("restart") && doc["restart"]) {
        shouldRestart = allowRestart;
    }

    return true;
}

int parseBleContentLength(const String& headers) {
    String lowerHeaders = headers;
    lowerHeaders.toLowerCase();
    int index = lowerHeaders.indexOf("content-length:");
    if (index == -1) {
        return -1;
    }
    int valueStart = index + 15;
    while (valueStart < lowerHeaders.length() && lowerHeaders.charAt(valueStart) == ' ') {
        valueStart++;
    }
    int valueEnd = lowerHeaders.indexOf("\r\n", valueStart);
    if (valueEnd == -1) {
        valueEnd = lowerHeaders.length();
    }
    String value = lowerHeaders.substring(valueStart, valueEnd);
    return value.toInt();
}

void appendBleRequestChunk(const String& chunk) {
    bleRequestBuffer += chunk;
    if (bleRequestBuffer.length() > 8192) {
        Serial.println("BLE: Request buffer overflow, resetting");
        bleRequestBuffer = "";
        bleRequestBodyLength = -1;
    }
}

bool consumeBleRequest(String& requestOut) {
    int headerEnd = bleRequestBuffer.indexOf("\r\n\r\n");
    if (headerEnd == -1) {
        return false;
    }

    String headers = bleRequestBuffer.substring(0, headerEnd);
    if (bleRequestBodyLength < 0) {
        bleRequestBodyLength = parseBleContentLength(headers);
    }

    bool isGet = headers.startsWith("GET ");
    if (isGet) {
        requestOut = bleRequestBuffer.substring(0, headerEnd + 4);
        bleRequestBuffer = bleRequestBuffer.substring(headerEnd + 4);
        bleRequestBodyLength = -1;
        return true;
    }

    if (bleRequestBodyLength < 0) {
        return false;
    }

    int totalLength = headerEnd + 4 + bleRequestBodyLength;
    if (bleRequestBuffer.length() < totalLength) {
        return false;
    }

    requestOut = bleRequestBuffer.substring(0, totalLength);
    bleRequestBuffer = bleRequestBuffer.substring(totalLength);
    bleRequestBodyLength = -1;
    return true;
}

void sendBleResponse(const String& response) {
    if (!pCharacteristic) {
        return;
    }

    // Use conservative chunk size to fit default BLE MTU (23 bytes -> 20 payload).
    const size_t chunkSize = 20;
    for (size_t offset = 0; offset < response.length(); offset += chunkSize) {
        String chunk = response.substring(offset, offset + chunkSize);
        pCharacteristic->setValue(chunk.c_str());
        pCharacteristic->notify();
        delay(10);
    }
}

uint16_t crc16Ccitt(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

bool tryExtractBleFrame(std::string& buffer, BleFrame& frame) {
    if (buffer.size() < BLE_FRAME_HEADER_SIZE + BLE_FRAME_CRC_SIZE) {
        return false;
    }

    size_t start = std::string::npos;
    for (size_t i = 0; i + 1 < buffer.size(); i++) {
        if ((uint8_t)buffer[i] == BLE_FRAME_MAGIC0 && (uint8_t)buffer[i + 1] == BLE_FRAME_MAGIC1) {
            start = i;
            break;
        }
    }

    if (start == std::string::npos) {
        buffer.clear();
        return false;
    }

    if (start > 0) {
        buffer.erase(0, start);
    }

    if (buffer.size() < BLE_FRAME_HEADER_SIZE + BLE_FRAME_CRC_SIZE) {
        return false;
    }

    if ((uint8_t)buffer[2] != BLE_FRAME_VERSION) {
        buffer.erase(0, 2);
        return false;
    }

    uint16_t payloadLength = (uint8_t)buffer[6] | ((uint16_t)(uint8_t)buffer[7] << 8);
    size_t frameSize = BLE_FRAME_HEADER_SIZE + payloadLength + BLE_FRAME_CRC_SIZE;
    if (buffer.size() < frameSize) {
        return false;
    }

    const uint8_t* frameBytes = reinterpret_cast<const uint8_t*>(buffer.data());
    uint16_t expectedCrc = (uint8_t)buffer[frameSize - 2] | ((uint16_t)(uint8_t)buffer[frameSize - 1] << 8);
    uint16_t actualCrc = crc16Ccitt(frameBytes, frameSize - BLE_FRAME_CRC_SIZE);
    if (expectedCrc != actualCrc) {
        buffer.erase(0, 2);
        return false;
    }

    frame.type = (uint8_t)buffer[3];
    frame.seq = (uint8_t)buffer[4];
    frame.flags = (uint8_t)buffer[5];
    frame.payload.assign(buffer.data() + BLE_FRAME_HEADER_SIZE, buffer.data() + BLE_FRAME_HEADER_SIZE + payloadLength);
    buffer.erase(0, frameSize);
    return true;
}

void sendBleFrame(uint8_t type, uint8_t seq, uint8_t flags, const uint8_t* payload, uint16_t length) {
    if (!pCharacteristic) {
        return;
    }

    std::string frame;
    frame.reserve(BLE_FRAME_HEADER_SIZE + length + BLE_FRAME_CRC_SIZE);
    frame.push_back((char)BLE_FRAME_MAGIC0);
    frame.push_back((char)BLE_FRAME_MAGIC1);
    frame.push_back((char)BLE_FRAME_VERSION);
    frame.push_back((char)type);
    frame.push_back((char)seq);
    frame.push_back((char)flags);
    frame.push_back((char)(length & 0xFF));
    frame.push_back((char)((length >> 8) & 0xFF));
    if (payload && length > 0) {
        frame.append(reinterpret_cast<const char*>(payload), length);
    }

    uint16_t crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
    frame.push_back((char)(crc & 0xFF));
    frame.push_back((char)((crc >> 8) & 0xFF));

    const size_t chunkSize = 180; // Fits within MTU 185 (payload <= 182)
    for (size_t offset = 0; offset < frame.size(); offset += chunkSize) {
        size_t chunkLen = min(chunkSize, frame.size() - offset);
        pCharacteristic->setValue((uint8_t*)frame.data() + offset, chunkLen);
        pCharacteristic->notify();
        delay(10);
    }
}

void sendBleAck(uint8_t seq) {
    sendBleFrame(BLE_MSG_ACK, seq, 0, nullptr, 0);
}

void sendBleError(uint8_t seq, const String& message) {
    String payload = "{\"error\":\"" + message + "\"}";
    sendBleFrame(
        BLE_MSG_ERROR,
        seq,
        0,
        reinterpret_cast<const uint8_t*>(payload.c_str()),
        payload.length()
    );
}

String getOtaStatusJSON() {
    DynamicJsonDocument doc(256);
    doc["ota_update_url"] = otaUpdateURL;
    doc["ota_in_progress"] = otaInProgress;
    doc["ota_progress"] = otaProgress;
    doc["ota_status"] = otaStatus;
    doc["ota_error"] = otaError;
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

// Old handleRoot function removed - now using external UI files
void handleAPI() {
    if (server.hasArg("plain")) {
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, server.arg("plain"));
        
        if (doc.containsKey("preset")) {
            setPreset(doc["preset"]);
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("presetAction")) {
            String action = doc["presetAction"].as<String>();
            if (action == "save") {
                String name = doc["presetName"] | "Custom Preset";
                if (addPreset(name)) {
                    saveSettings();
                }
            } else if (action == "update") {
                int index = doc["presetIndex"] | -1;
                String name = doc["presetName"] | "";
                if (index >= 0 && updatePreset((uint8_t)index, name)) {
                    saveSettings();
                }
            } else if (action == "delete") {
                int index = doc["presetIndex"] | -1;
                if (index >= 0 && deletePreset((uint8_t)index)) {
                    saveSettings();
                }
            }
        }
        if (doc.containsKey("brightness")) {
            globalBrightness = doc["brightness"];
            FastLED.setBrightness(globalBrightness);
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("effectSpeed")) {
            effectSpeed = doc["effectSpeed"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("startup_sequence")) {
            startupSequence = doc["startup_sequence"];
            startupEnabled = (startupSequence != STARTUP_NONE);
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("startup_duration")) {
            startupDuration = doc["startup_duration"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("testStartup") && doc["testStartup"]) {
            startStartupSequence();
        }
        if (doc.containsKey("testParkMode") && doc["testParkMode"]) {
            // Temporarily activate park mode for testing
            parkModeActive = true;
            parkStartTime = millis(); // Reset timer
            Serial.println("🅿️ Test park mode activated");
        }
        
        // Motion control API
        if (doc.containsKey("motion_enabled")) {
            bool newMotionEnabled = doc["motion_enabled"];
            
            // If motion control is being disabled, deactivate all motion features
            if (motionEnabled && !newMotionEnabled) {
                if (parkModeActive) {
                    parkModeActive = false;
                    parkStartTime = 0;
                    resetToNormalEffects(); // Reset LEDs to normal state
                    Serial.println("🅿️ Motion control disabled - deactivating park mode");
                }
                if (blinkerActive) {
                    blinkerActive = false;
                    blinkerStartTime = 0;
                    resetToNormalEffects(); // Reset LEDs to normal state
                    Serial.println("🚦 Motion control disabled - deactivating blinkers");
                }
            }
            
            motionEnabled = newMotionEnabled;
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("blinker_enabled")) {
            bool newBlinkerEnabled = doc["blinker_enabled"];
            
            // If blinkers are being disabled and they're currently active, deactivate them
            if (blinkerEnabled && !newBlinkerEnabled && blinkerActive) {
                blinkerActive = false;
                blinkerStartTime = 0;
                resetToNormalEffects(); // Reset LEDs to normal state
                Serial.println("🚦 Blinkers disabled - deactivating current blinker");
            }
            
            blinkerEnabled = newBlinkerEnabled;
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_mode_enabled")) {
            bool newParkModeEnabled = doc["park_mode_enabled"];
            
            // If park mode is being disabled and it's currently active, deactivate it
            if (parkModeEnabled && !newParkModeEnabled && parkModeActive) {
                parkModeActive = false;
                parkStartTime = 0;
                resetToNormalEffects(); // Reset LEDs to normal state
                Serial.println("🅿️ Park mode disabled - deactivating current park mode");
            }
            
            parkModeEnabled = newParkModeEnabled;
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("impact_detection_enabled")) {
            impactDetectionEnabled = doc["impact_detection_enabled"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("motion_sensitivity")) {
            motionSensitivity = doc["motion_sensitivity"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("blinker_delay")) {
            blinkerDelay = doc["blinker_delay"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("blinker_timeout")) {
            blinkerTimeout = doc["blinker_timeout"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_detection_angle")) {
            parkDetectionAngle = doc["park_detection_angle"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_stationary_time")) {
            parkStationaryTime = doc["park_stationary_time"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_accel_noise_threshold")) {
            parkAccelNoiseThreshold = doc["park_accel_noise_threshold"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_gyro_noise_threshold")) {
            parkGyroNoiseThreshold = doc["park_gyro_noise_threshold"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_effect")) {
            parkEffect = doc["park_effect"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_effect_speed")) {
            parkEffectSpeed = doc["park_effect_speed"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_headlight_color_r")) {
            parkHeadlightColor.r = doc["park_headlight_color_r"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_headlight_color_g")) {
            parkHeadlightColor.g = doc["park_headlight_color_g"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_headlight_color_b")) {
            parkHeadlightColor.b = doc["park_headlight_color_b"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_taillight_color_r")) {
            parkTaillightColor.r = doc["park_taillight_color_r"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_taillight_color_g")) {
            parkTaillightColor.g = doc["park_taillight_color_g"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_taillight_color_b")) {
            parkTaillightColor.b = doc["park_taillight_color_b"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("park_brightness")) {
            parkBrightness = doc["park_brightness"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("impact_threshold")) {
            impactThreshold = doc["impact_threshold"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("startCalibration") && doc["startCalibration"]) {
            startCalibration();
        }
        if (doc.containsKey("resetCalibration") && doc["resetCalibration"]) {
            resetCalibration();
        }
        if (doc.containsKey("nextCalibrationStep") && doc["nextCalibrationStep"]) {
            if (calibrationMode) {
                MotionData data = getMotionData();
                captureCalibrationStep(data);
            }
        }
        
        // Direction-based lighting API
        if (doc.containsKey("direction_based_lighting")) {
            directionBasedLighting = doc["direction_based_lighting"] | false;
            saveSettings(); // Auto-save
            #if DEBUG_ENABLED
            Serial.printf("🔄 Direction-based lighting: %s\n", directionBasedLighting ? "enabled" : "disabled");
            #endif
        }
        if (doc.containsKey("headlight_mode")) {
            headlightMode = doc["headlight_mode"] | 0;
            saveSettings(); // Auto-save
            Serial.printf("💡 Headlight mode: %s\n", headlightMode == 0 ? "Solid White" : "Effect");
        }
        if (doc.containsKey("forward_accel_threshold")) {
            forwardAccelThreshold = doc["forward_accel_threshold"] | 0.3;
            saveSettings(); // Auto-save
            Serial.printf("🔄 Forward acceleration threshold: %.2fG\n", forwardAccelThreshold);
        }
        
        // Braking detection API
        if (doc.containsKey("braking_enabled")) {
            brakingEnabled = doc["braking_enabled"] | false;
            saveSettings(); // Auto-save
            #if DEBUG_ENABLED
            Serial.printf("🛑 Braking detection: %s\n", brakingEnabled ? "enabled" : "disabled");
            #endif
        }
        if (doc.containsKey("braking_threshold")) {
            brakingThreshold = doc["braking_threshold"] | -0.5;
            saveSettings(); // Auto-save
            #if DEBUG_ENABLED
            Serial.printf("🛑 Braking threshold: %.2fG\n", brakingThreshold);
            #endif
        }
        if (doc.containsKey("braking_effect")) {
            brakingEffect = doc["braking_effect"] | 0;
            saveSettings(); // Auto-save
            #if DEBUG_ENABLED
            Serial.printf("🛑 Braking effect: %s\n", brakingEffect == 0 ? "Flash" : "Pulse");
            #endif
        }
        if (doc.containsKey("braking_brightness")) {
            brakingBrightness = doc["braking_brightness"] | 255;
            saveSettings(); // Auto-save
            #if DEBUG_ENABLED
            Serial.printf("🛑 Braking brightness: %d\n", brakingBrightness);
            #endif
        }
        if (doc.containsKey("manualBlinker")) {
            String manual = doc["manualBlinker"].as<String>();
            if (manual == "left" || manual == "right") {
                manualBlinkerActive = true;
                blinkerActive = true;
                blinkerDirection = (manual == "right") ? 1 : -1;
                blinkerStartTime = millis();
            } else if (manual == "off") {
                manualBlinkerActive = false;
                blinkerActive = false;
                blinkerDirection = 0;
                blinkerStartTime = 0;
                resetToNormalEffects();
            }
        }
        if (doc.containsKey("manualBrake")) {
            bool manual = doc["manualBrake"];
            manualBrakeActive = manual;
            brakingActive = manual;
            brakingStartTime = millis();
            brakingFlashCount = 0;
            brakingPulseCount = 0;
            if (!manual) {
                resetToNormalEffects();
            }
        }
        
        // RGBW white channel control
        if (doc.containsKey("rgbw_white_mode")) {
            setRgbwWhiteMode(doc["rgbw_white_mode"] | 0);
            saveSettings(); // Auto-save
            Serial.printf("💡 RGBW white mode: %d\n", rgbwWhiteMode);
        }
        if (doc.containsKey("white_leds_enabled") && !doc.containsKey("rgbw_white_mode")) {
            setRgbwWhiteMode((doc["white_leds_enabled"] | false) ? 1 : 0);
            saveSettings(); // Auto-save
            Serial.printf("💡 RGBW white channel: %s\n", whiteLEDsEnabled ? "enabled" : "disabled");
        }
        
        // OTA Update API
        if (doc.containsKey("otaUpdateURL")) {
            otaUpdateURL = doc["otaUpdateURL"].as<String>();
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("startOTAUpdate") && doc["startOTAUpdate"]) {
            if (!otaUpdateURL.isEmpty()) {
                startOTAUpdate(otaUpdateURL);
            }
        }
        if (doc.containsKey("apName")) {
            apName = doc["apName"].as<String>();
            bluetoothDeviceName = apName;  // Keep BLE name in sync with AP name
            Serial.printf("🔧 WiFi AP Name updated to: %s (BLE will use on restart)\n", apName.c_str());
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("apPassword")) {
            apPassword = doc["apPassword"].as<String>();
            Serial.printf("🔧 WiFi AP Password updated to: %s\n", apPassword.c_str());
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("restoreDefaults") && doc["restoreDefaults"]) {
            restoreDefaultsToStock();
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Defaults restored, restarting...\"}");
            delay(1000);
            ESP.restart();
        }
        if (doc.containsKey("restart") && doc["restart"]) {
            // Restart the device after a delay
            server.send(200, "application/json", "{\"status\":\"restarting\"}");
            delay(1000);
            ESP.restart();
        }
                if (doc.containsKey("headlightColor")) {
                    String colorHex = doc["headlightColor"];
                    uint32_t color = strtol(colorHex.c_str(), NULL, 16);
                    headlightColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
                    saveSettings(); // Auto-save
                }
                if (doc.containsKey("taillightColor")) {
                    String colorHex = doc["taillightColor"];
                    uint32_t color = strtol(colorHex.c_str(), NULL, 16);
                    taillightColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
                    saveSettings(); // Auto-save
                }
        if (doc.containsKey("headlightBackgroundColor")) {
            String colorHex = doc["headlightBackgroundColor"];
            uint32_t color = strtol(colorHex.c_str(), NULL, 16);
            headlightBackgroundColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("taillightBackgroundColor")) {
            String colorHex = doc["taillightBackgroundColor"];
            uint32_t color = strtol(colorHex.c_str(), NULL, 16);
            taillightBackgroundColor = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("headlightBackgroundEnabled")) {
            headlightBackgroundEnabled = doc["headlightBackgroundEnabled"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("taillightBackgroundEnabled")) {
            taillightBackgroundEnabled = doc["taillightBackgroundEnabled"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("headlightEffect")) {
            headlightEffect = doc["headlightEffect"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("taillightEffect")) {
            taillightEffect = doc["taillightEffect"];
            saveSettings(); // Auto-save
        }
        
        // ESPNow API
        if (doc.containsKey("enableESPNow")) {
            enableESPNow = doc["enableESPNow"];
            saveSettings(); // Auto-save
            if (enableESPNow) {
                initESPNow();
            } else {
                deinitESPNow();
            }
        }
        if (doc.containsKey("useESPNowSync")) {
            useESPNowSync = doc["useESPNowSync"];
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("espNowChannel")) {
            uint8_t newChannel = doc["espNowChannel"];
            if (newChannel != espNowChannel) {
                espNowChannel = newChannel;
                saveSettings(); // Auto-save
                updateSoftAPChannel();
                if (enableESPNow) {
                    initESPNow();
                }
            }
        }
        
        // Group Management API
        if (doc.containsKey("deviceName")) {
            deviceName = doc["deviceName"].as<String>();
            saveSettings(); // Auto-save
        }
        if (doc.containsKey("groupAction")) {
            String action = doc["groupAction"].as<String>();
            if (action == "create") {
                String code = doc["groupCode"] | "";
                groupCode = code;
                if (groupCode.length() != 6) {
                    groupCode = "";
                    generateGroupCode();
                }
                isGroupMaster = true;
                allowGroupJoin = true;
                hasGroupMaster = true;
                autoJoinOnHeartbeat = false;
                joinInProgress = false;
                groupMemberCount = 0;
                esp_wifi_get_mac(WIFI_IF_STA, groupMasterMac);
                // Add self as a group member when creating a group
                uint8_t mac[6];
                esp_wifi_get_mac(WIFI_IF_STA, mac);
                addGroupMember(mac, deviceName.c_str());
                Serial.printf("Group: Created with code %s and joined as master\n", groupCode.c_str());
            } else if (action == "join" && doc.containsKey("groupCode")) {
                String code = doc["groupCode"].as<String>();
                if (code.length() == 6) {
                    groupCode = code;
                    isGroupMaster = false;
                    hasGroupMaster = false;
                    autoJoinOnHeartbeat = false;
                    joinInProgress = true;
                    memset(groupMasterMac, 0, sizeof(groupMasterMac));
                    groupMemberCount = 0;
                    sendJoinRequest();
                    Serial.printf("Group: Attempting to join with code %s\n", groupCode.c_str());
                }
            } else if (action == "scan_join") {
                groupCode = "";
                isGroupMaster = false;
                hasGroupMaster = false;
                allowGroupJoin = false;
                autoJoinOnHeartbeat = true;
                joinInProgress = false;
                memset(groupMasterMac, 0, sizeof(groupMasterMac));
                groupMemberCount = 0;
                Serial.println("Group: Scanning for group heartbeat to join");
            } else if (action == "leave") {
                groupCode = "";
                isGroupMaster = false;
                allowGroupJoin = false;
                groupMemberCount = 0;
                hasGroupMaster = false;
                autoJoinOnHeartbeat = false;
                joinInProgress = false;
                memset(groupMasterMac, 0, sizeof(groupMasterMac));
                Serial.println("Group: Left group");
            } else if (action == "allow_join") {
                allowGroupJoin = true;
                Serial.println("Group: Join requests enabled");
            } else if (action == "block_join") {
                allowGroupJoin = false;
                Serial.println("Group: Join requests disabled");
            }
            saveSettings(); // Auto-save
        }
        
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        server.send(400, "application/json", "{\"error\":\"No data\"}");
    }
}

void buildStatusDocument(DynamicJsonDocument& doc) {
    doc["preset"] = currentPreset;
    doc["brightness"] = globalBrightness;
    doc["effectSpeed"] = effectSpeed;
    doc["startup_sequence"] = startupSequence;
    doc["startup_sequence_name"] = getStartupSequenceName(startupSequence);
    doc["startup_duration"] = startupDuration;

    // Motion control status
    doc["motion_enabled"] = motionEnabled;
    doc["blinker_enabled"] = blinkerEnabled;
    doc["park_mode_enabled"] = parkModeEnabled;
    doc["impact_detection_enabled"] = impactDetectionEnabled;
    doc["motion_sensitivity"] = motionSensitivity;

    // Direction-based lighting status
    doc["direction_based_lighting"] = directionBasedLighting;
    doc["headlight_mode"] = headlightMode;
    doc["is_moving_forward"] = isMovingForward;
    doc["forward_accel_threshold"] = forwardAccelThreshold;

    // Braking detection status
    doc["braking_enabled"] = brakingEnabled;
    doc["braking_active"] = brakingActive;
    doc["braking_threshold"] = brakingThreshold;
    doc["braking_effect"] = brakingEffect;
    doc["braking_brightness"] = brakingBrightness;
    doc["manual_brake_active"] = manualBrakeActive;

    doc["blinker_delay"] = blinkerDelay;
    doc["blinker_timeout"] = blinkerTimeout;
    doc["park_detection_angle"] = parkDetectionAngle;
    doc["impact_threshold"] = impactThreshold;
    doc["park_accel_noise_threshold"] = parkAccelNoiseThreshold;
    doc["park_gyro_noise_threshold"] = parkGyroNoiseThreshold;
    doc["park_stationary_time"] = parkStationaryTime;
    doc["park_effect"] = parkEffect;
    doc["park_effect_speed"] = parkEffectSpeed;
    doc["park_headlight_color_r"] = parkHeadlightColor.r;
    doc["park_headlight_color_g"] = parkHeadlightColor.g;
    doc["park_headlight_color_b"] = parkHeadlightColor.b;
    doc["park_taillight_color_r"] = parkTaillightColor.r;
    doc["park_taillight_color_g"] = parkTaillightColor.g;
    doc["park_taillight_color_b"] = parkTaillightColor.b;
    doc["park_brightness"] = parkBrightness;
    doc["blinker_active"] = blinkerActive;
    doc["blinker_direction"] = blinkerDirection;
    doc["manual_blinker_active"] = manualBlinkerActive;
    doc["park_mode_active"] = parkModeActive;
    doc["calibration_complete"] = calibrationComplete;
    doc["calibration_mode"] = calibrationMode;
    doc["calibration_step"] = calibrationStep;

    // OTA Update status
    doc["ota_update_url"] = otaUpdateURL;
    doc["ota_in_progress"] = otaInProgress;
    doc["ota_progress"] = otaProgress;
    doc["ota_status"] = otaStatus;
    doc["ota_error"] = otaError;
    doc["ota_file_name"] = otaFileName;
    doc["ota_file_size"] = otaFileSize;
    doc["firmware_version"] = "v8.0 OTA";
    doc["build_date"] = __DATE__ " " __TIME__;
    doc["apName"] = apName;
    doc["apPassword"] = apPassword;
    doc["headlightColor"] = formatColorHex(headlightColor);
    doc["taillightColor"] = formatColorHex(taillightColor);
    doc["headlightBackgroundEnabled"] = headlightBackgroundEnabled;
    doc["taillightBackgroundEnabled"] = taillightBackgroundEnabled;
    doc["headlightBackgroundColor"] = formatColorHex(headlightBackgroundColor);
    doc["taillightBackgroundColor"] = formatColorHex(taillightBackgroundColor);
    doc["headlightEffect"] = headlightEffect;
    doc["taillightEffect"] = taillightEffect;

    // LED configuration
    doc["headlightLedCount"] = headlightLedCount;
    doc["taillightLedCount"] = taillightLedCount;
    doc["headlightLedType"] = headlightLedType;
    doc["taillightLedType"] = taillightLedType;
    doc["headlightColorOrder"] = headlightColorOrder;
    doc["taillightColorOrder"] = taillightColorOrder;

    // ESPNow status
    doc["enableESPNow"] = enableESPNow;
    doc["useESPNowSync"] = useESPNowSync;
    doc["espNowChannel"] = espNowChannel;
    doc["espNowStatus"] = (espNowState == 1)
        ? "Active"
        : (espNowState == 2)
            ? ("Error (" + String(espNowLastError) + ")")
            : "Inactive";
    doc["espNowPeerCount"] = espNowPeerCount;
    doc["espNowLastSend"] = (lastESPNowSend > 0) ? String((millis() - lastESPNowSend) / 1000) + "s ago" : "Never";

    // Preset list
    doc["presetCount"] = presetCount;
    JsonArray presetArray = doc.createNestedArray("presets");
    for (uint8_t i = 0; i < presetCount; i++) {
        JsonObject presetObj = presetArray.createNestedObject();
        presetObj["name"] = presets[i].name;
    }

    // Group status
    doc["groupCode"] = groupCode;
    doc["isGroupMaster"] = isGroupMaster;
    doc["groupMemberCount"] = groupMemberCount;
    doc["deviceName"] = deviceName;
    doc["hasGroupMaster"] = hasGroupMaster;
    doc["groupMasterMac"] = hasGroupMaster ? formatMacAddress(groupMasterMac) : "";

    // Bluetooth status
    doc["bluetoothEnabled"] = bluetoothEnabled;
    doc["bluetoothDeviceName"] = bluetoothDeviceName;
    doc["bluetoothConnected"] = deviceConnected;

    // RGBW white channel status
    doc["rgbw_white_mode"] = rgbwWhiteMode;
    doc["white_leds_enabled"] = whiteLEDsEnabled;
}

void handleStatus() {
    DynamicJsonDocument doc(4096);
    buildStatusDocument(doc);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    sendJSONResponse(doc);
}

String getStatusJSON() {
    // Full status for both HTTP and BLE - single source of truth
    DynamicJsonDocument doc(4096);
    buildStatusDocument(doc);
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

void handleLEDConfig() {
    if (server.hasArg("plain")) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, server.arg("plain"));
        
        bool configChanged = false;
        
        if (doc.containsKey("headlightLedCount")) {
            uint8_t newHeadlightCount = doc["headlightLedCount"];
            if (newHeadlightCount != headlightLedCount) {
                Serial.printf("LED Config: Headlight count changed from %d to %d\n", headlightLedCount, newHeadlightCount);
                headlightLedCount = newHeadlightCount;
                configChanged = true;
            }
        }
        if (doc.containsKey("taillightLedCount")) {
            uint8_t newTaillightCount = doc["taillightLedCount"];
            if (newTaillightCount != taillightLedCount) {
                Serial.printf("LED Config: Taillight count changed from %d to %d\n", taillightLedCount, newTaillightCount);
                taillightLedCount = newTaillightCount;
                configChanged = true;
            }
        }
        if (doc.containsKey("headlightLedType")) {
            headlightLedType = doc["headlightLedType"];
            configChanged = true;
        }
        if (doc.containsKey("taillightLedType")) {
            taillightLedType = doc["taillightLedType"];
            configChanged = true;
        }
        if (doc.containsKey("headlightColorOrder")) {
            headlightColorOrder = doc["headlightColorOrder"];
            configChanged = true;
        }
        if (doc.containsKey("taillightColorOrder")) {
            taillightColorOrder = doc["taillightColorOrder"];
            configChanged = true;
        }
        
        if (configChanged) {
            saveSettings();
            initializeLEDs();
            Serial.println("LED configuration updated and applied!");
        }
        
        // Return updated configuration in response so UI can verify
        DynamicJsonDocument responseDoc(512);
        responseDoc["status"] = "ok";
        responseDoc["headlightLedCount"] = headlightLedCount;
        responseDoc["taillightLedCount"] = taillightLedCount;
        responseDoc["headlightLedType"] = headlightLedType;
        responseDoc["taillightLedType"] = taillightLedType;
        responseDoc["headlightColorOrder"] = headlightColorOrder;
        responseDoc["taillightColorOrder"] = taillightColorOrder;
        
        server.sendHeader("Access-Control-Allow-Origin", "*");
        String response;
        serializeJson(responseDoc, response);
        server.send(200, "application/json", response);
    } else {
        server.send(400, "application/json", "{\"error\":\"No data\"}");
    }
}

void handleLEDTest() {
    testLEDConfiguration();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"status\":\"test_complete\"}");
}

void handleGetSettings() {
    // Try to get settings from NVS first (primary storage; chunked or legacy)
    String settingsJson;
    nvs.begin(NVS_NAMESPACE, true); // Open read-only
    if (nvs.isKey(NVS_KEY_CHUNK_COUNT)) {
        uint8_t numChunks = nvs.getUChar(NVS_KEY_CHUNK_COUNT, 0);
        settingsJson.reserve(numChunks * NVS_CHUNK_SIZE);
        for (uint8_t i = 0; i < numChunks; i++) {
            String key = "s" + String(i);
            settingsJson += nvs.getString(key.c_str(), "");
        }
        nvs.end();
        Serial.println("📄 Returning settings from NVS (chunked)");
    } else if (nvs.isKey("settings")) {
        settingsJson = nvs.getString("settings", "");
        nvs.end();
        Serial.println("📄 Returning settings from NVS (legacy)");
    } else {
        nvs.end();
        // Fallback to SPIFFS
        File file = SPIFFS.open("/settings.json", "r");
        if (file) {
            settingsJson = file.readString();
            file.close();
            Serial.println("📄 Returning settings from SPIFFS");
        } else {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(404, "application/json", "{\"error\":\"No settings found\"}");
            return;
        }
    }
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", settingsJson);
}

void sendJSONResponse(DynamicJsonDocument& doc) {
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// LED Configuration Implementation
// RGBW controllers for SK6812 RGBW LEDs - dynamically created based on color order

// Function to convert CRGB color based on color order for RGBW LEDs
CRGB convertColorOrder(CRGB color, uint8_t colorOrder) {
    switch (colorOrder) {
        case 0: // RGB - no conversion needed
            return color;
        case 1: // GRB - swap R and G
            return CRGB(color.g, color.r, color.b);
        case 2: // BGR - swap R and B
            return CRGB(color.b, color.g, color.r);
        default:
            return color;
    }
}

// Helper function to set LED color with proper color order conversion
void setLEDColor(CRGB* leds, uint8_t index, CRGB color, uint8_t ledType, uint8_t colorOrder) {
    if (ledType == 0) { // RGBW LEDs need color order conversion
        leds[index] = convertColorOrder(color, colorOrder);
    } else {
        leds[index] = color;
    }
}

// Wrapper functions for RGBW color order handling
void fillSolidWithColorOrder(CRGB* leds, uint8_t numLeds, CRGB color, uint8_t ledType, uint8_t colorOrder) {
    if (ledType == 0) { // RGBW LEDs - convert color order in software
        // RGBWEmulatedController uses RGB internally, so we need to convert
        // the color to match what the physical LEDs expect
        CRGB convertedColor = convertColorOrder(color, colorOrder);
        fill_solid(leds, numLeds, convertedColor);
    } else {
        // For RGB-only LEDs, FastLED handles color order via template parameter
        fill_solid(leds, numLeds, color);
    }
}

// Apply color order conversion to entire LED array (for effects that write directly)
void applyColorOrderToArray(CRGB* leds, uint8_t numLeds, uint8_t ledType, uint8_t colorOrder) {
    if (ledType == 0) { // RGBW LEDs need color order conversion
        for (uint8_t i = 0; i < numLeds; i++) {
            leds[i] = convertColorOrder(leds[i], colorOrder);
        }
    }
    // For RGB-only LEDs, color order is handled by FastLED template parameter
}

void fillRainbowWithColorOrder(CRGB* leds, uint8_t numLeds, uint8_t initialHue, uint8_t deltaHue, uint8_t ledType, uint8_t colorOrder) {
    if (ledType == 0) { // RGBW LEDs need color order conversion
        fill_rainbow(leds, numLeds, initialHue, deltaHue);
        for (uint8_t i = 0; i < numLeds; i++) {
            leds[i] = convertColorOrder(leds[i], colorOrder);
        }
    } else {
        fill_rainbow(leds, numLeds, initialHue, deltaHue);
    }
}

void initializeLEDs() {
    // Clean up existing memory if arrays exist
    if (headlight != nullptr) {
        delete[] headlight;
        headlight = nullptr;
    }
    if (taillight != nullptr) {
        delete[] taillight;
        taillight = nullptr;
    }
    
    // Allocate memory for LED arrays
    headlight = new CRGB[headlightLedCount];
    taillight = new CRGB[taillightLedCount];
    
    Serial.printf("LED Init: Allocated %d headlight LEDs and %d taillight LEDs\n", headlightLedCount, taillightLedCount);
    
    // Clear FastLED
    FastLED.clear();
    
    // Add LED strips based on configuration
    switch (headlightLedType) {
        case 0: // SK6812 RGBW - Use RGBW emulation
            // RGBWEmulatedController requires underlying controller to use RGB order
            // Color order conversion is handled in software via fillSolidWithColorOrder
            typedef SK6812<HEADLIGHT_PIN, RGB> HeadlightControllerT;
            static RGBWEmulatedController<HeadlightControllerT, RGB> headlightRGBWController(
                Rgbw(kRGBWDefaultColorTemp, kRGBWNullWhitePixel, W3)
            );
            headlightController = &FastLED.addLeds(&headlightRGBWController, headlight, headlightLedCount);
            break;
        case 1: // SK6812 RGB - Use RGB only
            if (headlightColorOrder == 0) headlightController = &FastLED.addLeds<SK6812, HEADLIGHT_PIN, RGB>(headlight, headlightLedCount);
            else if (headlightColorOrder == 1) headlightController = &FastLED.addLeds<SK6812, HEADLIGHT_PIN, GRB>(headlight, headlightLedCount);
            else if (headlightColorOrder == 2) headlightController = &FastLED.addLeds<SK6812, HEADLIGHT_PIN, BGR>(headlight, headlightLedCount);
            break;
        case 2: // WS2812B
            if (headlightColorOrder == 0) headlightController = &FastLED.addLeds<WS2812B, HEADLIGHT_PIN, RGB>(headlight, headlightLedCount);
            else if (headlightColorOrder == 1) headlightController = &FastLED.addLeds<WS2812B, HEADLIGHT_PIN, GRB>(headlight, headlightLedCount);
            else if (headlightColorOrder == 2) headlightController = &FastLED.addLeds<WS2812B, HEADLIGHT_PIN, BGR>(headlight, headlightLedCount);
            break;
        case 3: // APA102
            if (headlightColorOrder == 0) headlightController = &FastLED.addLeds<APA102, HEADLIGHT_PIN, HEADLIGHT_CLOCK_PIN, RGB>(headlight, headlightLedCount);
            else if (headlightColorOrder == 1) headlightController = &FastLED.addLeds<APA102, HEADLIGHT_PIN, HEADLIGHT_CLOCK_PIN, GRB>(headlight, headlightLedCount);
            else headlightController = &FastLED.addLeds<APA102, HEADLIGHT_PIN, HEADLIGHT_CLOCK_PIN, BGR>(headlight, headlightLedCount);
            break;
        case 4: // LPD8806
            if (headlightColorOrder == 0) headlightController = &FastLED.addLeds<LPD8806, HEADLIGHT_PIN, HEADLIGHT_CLOCK_PIN, RGB>(headlight, headlightLedCount);
            else if (headlightColorOrder == 1) headlightController = &FastLED.addLeds<LPD8806, HEADLIGHT_PIN, HEADLIGHT_CLOCK_PIN, GRB>(headlight, headlightLedCount);
            else headlightController = &FastLED.addLeds<LPD8806, HEADLIGHT_PIN, HEADLIGHT_CLOCK_PIN, BGR>(headlight, headlightLedCount);
            break;
    }
    
    switch (taillightLedType) {
        case 0: // SK6812 RGBW - Use RGBW emulation
            // RGBWEmulatedController requires underlying controller to use RGB order
            // Color order conversion is handled in software via fillSolidWithColorOrder
            typedef SK6812<TAILLIGHT_PIN, RGB> TaillightControllerT;
            static RGBWEmulatedController<TaillightControllerT, RGB> taillightRGBWController(
                Rgbw(kRGBWDefaultColorTemp, kRGBWNullWhitePixel, W3)
            );
            taillightController = &FastLED.addLeds(&taillightRGBWController, taillight, taillightLedCount);
            break;
        case 1: // SK6812 RGB - Use RGB only
            if (taillightColorOrder == 0) taillightController = &FastLED.addLeds<SK6812, TAILLIGHT_PIN, RGB>(taillight, taillightLedCount);
            else if (taillightColorOrder == 1) taillightController = &FastLED.addLeds<SK6812, TAILLIGHT_PIN, GRB>(taillight, taillightLedCount);
            else if (taillightColorOrder == 2) taillightController = &FastLED.addLeds<SK6812, TAILLIGHT_PIN, BGR>(taillight, taillightLedCount);
            break;
        case 2: // WS2812B
            if (taillightColorOrder == 0) taillightController = &FastLED.addLeds<WS2812B, TAILLIGHT_PIN, RGB>(taillight, taillightLedCount);
            else if (taillightColorOrder == 1) taillightController = &FastLED.addLeds<WS2812B, TAILLIGHT_PIN, GRB>(taillight, taillightLedCount);
            else if (taillightColorOrder == 2) taillightController = &FastLED.addLeds<WS2812B, TAILLIGHT_PIN, BGR>(taillight, taillightLedCount);
            break;
        case 3: // APA102
            if (taillightColorOrder == 0) taillightController = &FastLED.addLeds<APA102, TAILLIGHT_PIN, TAILLIGHT_CLOCK_PIN, RGB>(taillight, taillightLedCount);
            else if (taillightColorOrder == 1) taillightController = &FastLED.addLeds<APA102, TAILLIGHT_PIN, TAILLIGHT_CLOCK_PIN, GRB>(taillight, taillightLedCount);
            else taillightController = &FastLED.addLeds<APA102, TAILLIGHT_PIN, TAILLIGHT_CLOCK_PIN, BGR>(taillight, taillightLedCount);
            break;
        case 4: // LPD8806
            if (taillightColorOrder == 0) taillightController = &FastLED.addLeds<LPD8806, TAILLIGHT_PIN, TAILLIGHT_CLOCK_PIN, RGB>(taillight, taillightLedCount);
            else if (taillightColorOrder == 1) taillightController = &FastLED.addLeds<LPD8806, TAILLIGHT_PIN, TAILLIGHT_CLOCK_PIN, GRB>(taillight, taillightLedCount);
            else taillightController = &FastLED.addLeds<LPD8806, TAILLIGHT_PIN, TAILLIGHT_CLOCK_PIN, BGR>(taillight, taillightLedCount);
            break;
    }

    applyRgbwWhiteChannelMode();
    
    FastLED.setBrightness(globalBrightness);
    Serial.printf("LED strips initialized successfully! Headlight: %d LEDs, Taillight: %d LEDs\n", headlightLedCount, taillightLedCount);
}

void applyRgbwWhiteChannelMode() {
    Rgbw rgbwMode = Rgbw(kRGBWDefaultColorTemp, kRGBWNullWhitePixel, W3);

    switch (rgbwWhiteMode) {
        case 1:
            rgbwMode = Rgbw(kRGBWDefaultColorTemp, kRGBWExactColors, W3);
            break;
        case 2:
            rgbwMode = Rgbw(kRGBWDefaultColorTemp, kRGBWBoostedWhite, W3);
            break;
        case 3:
            rgbwMode = Rgbw(kRGBWDefaultColorTemp, kRGBWMaxBrightness, W3);
            break;
        default:
            rgbwMode = Rgbw(kRGBWDefaultColorTemp, kRGBWNullWhitePixel, W3);
            break;
    }

    if (headlightController) {
        headlightController->setRgbw(rgbwMode);
    }
    if (taillightController) {
        taillightController->setRgbw(rgbwMode);
    }
}

void setRgbwWhiteMode(uint8_t mode) {
    rgbwWhiteMode = min(mode, (uint8_t)3);
    whiteLEDsEnabled = rgbwWhiteMode != 0;
    applyRgbwWhiteChannelMode();
}

void testLEDConfiguration() {
    Serial.println("Testing LED configuration...");
    
    // Test red - use fillSolidWithColorOrder to respect color order settings
    fillSolidWithColorOrder(headlight, headlightLedCount, CRGB::Red, headlightLedType, headlightColorOrder);
    fillSolidWithColorOrder(taillight, taillightLedCount, CRGB::Red, taillightLedType, taillightColorOrder);
    FastLED.show();
    delay(1000);
    
    // Test green
    fillSolidWithColorOrder(headlight, headlightLedCount, CRGB::Green, headlightLedType, headlightColorOrder);
    fillSolidWithColorOrder(taillight, taillightLedCount, CRGB::Green, taillightLedType, taillightColorOrder);
    FastLED.show();
    delay(1000);
    
    // Test blue
    fillSolidWithColorOrder(headlight, headlightLedCount, CRGB::Blue, headlightLedType, headlightColorOrder);
    fillSolidWithColorOrder(taillight, taillightLedCount, CRGB::Blue, taillightLedType, taillightColorOrder);
    FastLED.show();
    delay(1000);
    
    // Test white (using white channel for RGBW LEDs)
    fillSolidWithColorOrder(headlight, headlightLedCount, CRGB::White, headlightLedType, headlightColorOrder);
    fillSolidWithColorOrder(taillight, taillightLedCount, CRGB::White, taillightLedType, taillightColorOrder);
    FastLED.show();
    delay(1000);
    
    Serial.println("LED test complete!");
}

String getLEDTypeName(uint8_t type) {
    switch (type) {
        case 0: return "SK6812 RGBW";
        case 1: return "SK6812 RGB";
        case 2: return "WS2812B";
        case 3: return "APA102";
        case 4: return "LPD8806";
        default: return "Unknown";
    }
}

String getColorOrderName(uint8_t order) {
    switch (order) {
        case 0: return "RGB";
        case 1: return "GRB";
        case 2: return "BGR";
        default: return "Unknown";
    }
}

// Save all settings to persistent storage
// Initialize SPIFFS filesystem
void initFilesystem() {
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS Mount Failed");
        return;
    }
    Serial.println("✅ SPIFFS Mount Success");
    
    // Skip file listing during boot for faster startup
    // Can be enabled for debugging by uncommenting:
    /*
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    Serial.println("📁 Existing files:");
    while (file) {
        Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
        file = root.openNextFile();
    }
    */
}

// Save all settings to filesystem
bool saveSettings() {
    DynamicJsonDocument doc(8192);
    
    // Light settings
    doc["headlight_effect"] = headlightEffect;
    doc["taillight_effect"] = taillightEffect;
    doc["headlight_color_r"] = headlightColor.r;
    doc["headlight_color_g"] = headlightColor.g;
    doc["headlight_color_b"] = headlightColor.b;
    doc["taillight_color_r"] = taillightColor.r;
    doc["taillight_color_g"] = taillightColor.g;
    doc["taillight_color_b"] = taillightColor.b;
    doc["headlight_background_enabled"] = headlightBackgroundEnabled;
    doc["taillight_background_enabled"] = taillightBackgroundEnabled;
    doc["headlight_background_r"] = headlightBackgroundColor.r;
    doc["headlight_background_g"] = headlightBackgroundColor.g;
    doc["headlight_background_b"] = headlightBackgroundColor.b;
    doc["taillight_background_r"] = taillightBackgroundColor.r;
    doc["taillight_background_g"] = taillightBackgroundColor.g;
    doc["taillight_background_b"] = taillightBackgroundColor.b;
    doc["global_brightness"] = globalBrightness;
    doc["effect_speed"] = effectSpeed;
    doc["current_preset"] = currentPreset;
    
    // Startup sequence settings
    doc["startup_sequence"] = startupSequence;
    doc["startup_enabled"] = startupEnabled;
    doc["startup_duration"] = startupDuration;
    
    // Motion control settings
    doc["motion_enabled"] = motionEnabled;
    doc["blinker_enabled"] = blinkerEnabled;
    doc["park_mode_enabled"] = parkModeEnabled;
    doc["impact_detection_enabled"] = impactDetectionEnabled;
    doc["motion_sensitivity"] = motionSensitivity;
    doc["blinker_delay"] = blinkerDelay;
    doc["blinker_timeout"] = blinkerTimeout;
    doc["park_detection_angle"] = parkDetectionAngle;
    doc["impact_threshold"] = impactThreshold;
    doc["park_accel_noise_threshold"] = parkAccelNoiseThreshold;
    doc["park_gyro_noise_threshold"] = parkGyroNoiseThreshold;
    doc["park_stationary_time"] = parkStationaryTime;
    
    // Direction-based lighting settings
    doc["direction_based_lighting"] = directionBasedLighting;
    doc["headlight_mode"] = headlightMode;
    doc["forward_accel_threshold"] = forwardAccelThreshold;
    
    // Braking detection settings
    doc["braking_enabled"] = brakingEnabled;
    doc["braking_threshold"] = brakingThreshold;
    doc["braking_effect"] = brakingEffect;
    doc["braking_brightness"] = brakingBrightness;
    
    // RGBW white channel setting
    doc["rgbw_white_mode"] = rgbwWhiteMode;
    doc["white_leds_enabled"] = whiteLEDsEnabled;
    
    // Park mode effect settings
    doc["park_effect"] = parkEffect;
    doc["park_effect_speed"] = parkEffectSpeed;
    doc["park_headlight_color_r"] = parkHeadlightColor.r;
    doc["park_headlight_color_g"] = parkHeadlightColor.g;
    doc["park_headlight_color_b"] = parkHeadlightColor.b;
    doc["park_taillight_color_r"] = parkTaillightColor.r;
    doc["park_taillight_color_g"] = parkTaillightColor.g;
    doc["park_taillight_color_b"] = parkTaillightColor.b;
    doc["park_brightness"] = parkBrightness;
    
    // OTA Update settings
    doc["ota_update_url"] = otaUpdateURL;
    
    // LED configuration
    doc["headlight_count"] = headlightLedCount;
    doc["taillight_count"] = taillightLedCount;
    doc["headlight_type"] = headlightLedType;
    doc["taillight_type"] = taillightLedType;
    doc["headlight_order"] = headlightColorOrder;
    doc["taillight_order"] = taillightColorOrder;
    
    // WiFi settings
    doc["apName"] = apName;
    doc["apPassword"] = apPassword;
    
    // ESPNow settings
    doc["enableESPNow"] = enableESPNow;
    doc["useESPNowSync"] = useESPNowSync;
    doc["espNowChannel"] = espNowChannel;

    // Presets
    savePresetsToDoc(doc);
    
    // Group settings
    doc["groupCode"] = groupCode;
    doc["isGroupMaster"] = isGroupMaster;
    doc["allowGroupJoin"] = allowGroupJoin;
    doc["deviceName"] = deviceName;
    doc["hasGroupMaster"] = hasGroupMaster;
    doc["groupMasterMac"] = hasGroupMaster ? formatMacAddress(groupMasterMac) : "";
    doc["hasGroupMaster"] = hasGroupMaster;
    doc["groupMasterMac"] = hasGroupMaster ? formatMacAddress(groupMasterMac) : "";
    
    // Calibration data
    doc["calibration_complete"] = calibrationComplete;
    doc["calibration_valid"] = calibration.valid;
    doc["calibration_forward_axis"] = String(calibration.forwardAxis);
    doc["calibration_forward_sign"] = calibration.forwardSign;
    doc["calibration_leftright_axis"] = String(calibration.leftRightAxis);
    doc["calibration_leftright_sign"] = calibration.leftRightSign;
    doc["calibration_level_x"] = calibration.levelAccelX;
    doc["calibration_level_y"] = calibration.levelAccelY;
    doc["calibration_level_z"] = calibration.levelAccelZ;
    doc["calibration_forward_x"] = calibration.forwardAccelX;
    doc["calibration_forward_y"] = calibration.forwardAccelY;
    doc["calibration_forward_z"] = calibration.forwardAccelZ;
    doc["calibration_backward_x"] = calibration.backwardAccelX;
    doc["calibration_backward_y"] = calibration.backwardAccelY;
    doc["calibration_backward_z"] = calibration.backwardAccelZ;
    doc["calibration_left_x"] = calibration.leftAccelX;
    doc["calibration_left_y"] = calibration.leftAccelY;
    doc["calibration_left_z"] = calibration.leftAccelZ;
    doc["calibration_right_x"] = calibration.rightAccelX;
    doc["calibration_right_y"] = calibration.rightAccelY;
    doc["calibration_right_z"] = calibration.rightAccelZ;
    
    // Save to both NVS and SPIFFS (same data):
    // - NVS: primary; survives OTA/filesystem wipes (chunked to fit 508-byte-per-key limit).
    // - SPIFFS: backward compatibility, migration, and GET /api/settings fallback.
    bool nvsSuccess = saveSettingsToNVS();
    
    // Also save to SPIFFS for backward compatibility and migration
    File file = SPIFFS.open("/settings.json", "w");
    if (!file) {
        Serial.println("⚠️ Failed to open settings.json for writing (SPIFFS)");
        // Still return success if NVS saved
        return nvsSuccess;
    }
    
    size_t bytesWritten = serializeJson(doc, file);
    file.close();
    
    if (bytesWritten > 0) {
        Serial.printf("✅ Settings saved to SPIFFS (%d bytes)\n", bytesWritten);
    } else {
        Serial.println("⚠️ Failed to write settings to SPIFFS");
    }
    
    if (nvsSuccess) {
        Serial.printf("✅ Settings saved to NVS (survives OTA filesystem updates)\n");
        Serial.printf("Headlight: RGB(%d,%d,%d), Taillight: RGB(%d,%d,%d)\n", 
                      headlightColor.r, headlightColor.g, headlightColor.b,
                      taillightColor.r, taillightColor.g, taillightColor.b);
        return true;
    } else {
        // If NVS failed but SPIFFS succeeded, still return true (backward compatibility)
        return (bytesWritten > 0);
    }
}

// Save all settings to NVS (survives OTA filesystem updates)
bool saveSettingsToNVS() {
    if (!nvs.begin(NVS_NAMESPACE, false)) {
        Serial.println("❌ Failed to open NVS namespace");
        return false;
    }
    
    DynamicJsonDocument doc(8192);
    
    // Light settings
    doc["headlight_effect"] = headlightEffect;
    doc["taillight_effect"] = taillightEffect;
    doc["headlight_color_r"] = headlightColor.r;
    doc["headlight_color_g"] = headlightColor.g;
    doc["headlight_color_b"] = headlightColor.b;
    doc["taillight_color_r"] = taillightColor.r;
    doc["taillight_color_g"] = taillightColor.g;
    doc["taillight_color_b"] = taillightColor.b;
    doc["headlight_background_enabled"] = headlightBackgroundEnabled;
    doc["taillight_background_enabled"] = taillightBackgroundEnabled;
    doc["headlight_background_r"] = headlightBackgroundColor.r;
    doc["headlight_background_g"] = headlightBackgroundColor.g;
    doc["headlight_background_b"] = headlightBackgroundColor.b;
    doc["taillight_background_r"] = taillightBackgroundColor.r;
    doc["taillight_background_g"] = taillightBackgroundColor.g;
    doc["taillight_background_b"] = taillightBackgroundColor.b;
    doc["global_brightness"] = globalBrightness;
    doc["effect_speed"] = effectSpeed;
    doc["current_preset"] = currentPreset;
    
    // Startup sequence settings
    doc["startup_sequence"] = startupSequence;
    doc["startup_enabled"] = startupEnabled;
    doc["startup_duration"] = startupDuration;
    
    // Motion control settings
    doc["motion_enabled"] = motionEnabled;
    doc["blinker_enabled"] = blinkerEnabled;
    doc["park_mode_enabled"] = parkModeEnabled;
    doc["impact_detection_enabled"] = impactDetectionEnabled;
    doc["motion_sensitivity"] = motionSensitivity;
    doc["blinker_delay"] = blinkerDelay;
    doc["blinker_timeout"] = blinkerTimeout;
    doc["park_detection_angle"] = parkDetectionAngle;
    doc["impact_threshold"] = impactThreshold;
    doc["park_accel_noise_threshold"] = parkAccelNoiseThreshold;
    doc["park_gyro_noise_threshold"] = parkGyroNoiseThreshold;
    doc["park_stationary_time"] = parkStationaryTime;
    
    // Direction-based lighting settings
    doc["direction_based_lighting"] = directionBasedLighting;
    doc["headlight_mode"] = headlightMode;
    doc["forward_accel_threshold"] = forwardAccelThreshold;
    
    // Braking detection settings
    doc["braking_enabled"] = brakingEnabled;
    doc["braking_threshold"] = brakingThreshold;
    doc["braking_effect"] = brakingEffect;
    doc["braking_brightness"] = brakingBrightness;
    
    // RGBW white channel setting
    doc["rgbw_white_mode"] = rgbwWhiteMode;
    doc["white_leds_enabled"] = whiteLEDsEnabled;
    
    // Park mode effect settings
    doc["park_effect"] = parkEffect;
    doc["park_effect_speed"] = parkEffectSpeed;
    doc["park_headlight_color_r"] = parkHeadlightColor.r;
    doc["park_headlight_color_g"] = parkHeadlightColor.g;
    doc["park_headlight_color_b"] = parkHeadlightColor.b;
    doc["park_taillight_color_r"] = parkTaillightColor.r;
    doc["park_taillight_color_g"] = parkTaillightColor.g;
    doc["park_taillight_color_b"] = parkTaillightColor.b;
    doc["park_brightness"] = parkBrightness;
    
    // OTA Update settings
    doc["ota_update_url"] = otaUpdateURL;
    
    // LED configuration
    doc["headlight_count"] = headlightLedCount;
    doc["taillight_count"] = taillightLedCount;
    doc["headlight_type"] = headlightLedType;
    doc["taillight_type"] = taillightLedType;
    doc["headlight_order"] = headlightColorOrder;
    doc["taillight_order"] = taillightColorOrder;
    
    // WiFi settings
    doc["apName"] = apName;
    doc["apPassword"] = apPassword;
    
    // ESPNow settings
    doc["enableESPNow"] = enableESPNow;
    doc["useESPNowSync"] = useESPNowSync;
    doc["espNowChannel"] = espNowChannel;

    // Presets
    savePresetsToDoc(doc);
    
    // Group settings
    doc["groupCode"] = groupCode;
    doc["isGroupMaster"] = isGroupMaster;
    doc["allowGroupJoin"] = allowGroupJoin;
    doc["deviceName"] = deviceName;
    
    // Calibration data (CRITICAL - must survive OTA filesystem updates)
    doc["calibration_complete"] = calibrationComplete;
    doc["calibration_valid"] = calibration.valid;
    doc["calibration_forward_axis"] = String(calibration.forwardAxis);
    doc["calibration_forward_sign"] = calibration.forwardSign;
    doc["calibration_leftright_axis"] = String(calibration.leftRightAxis);
    doc["calibration_leftright_sign"] = calibration.leftRightSign;
    doc["calibration_level_x"] = calibration.levelAccelX;
    doc["calibration_level_y"] = calibration.levelAccelY;
    doc["calibration_level_z"] = calibration.levelAccelZ;
    doc["calibration_forward_x"] = calibration.forwardAccelX;
    doc["calibration_forward_y"] = calibration.forwardAccelY;
    doc["calibration_forward_z"] = calibration.forwardAccelZ;
    doc["calibration_backward_x"] = calibration.backwardAccelX;
    doc["calibration_backward_y"] = calibration.backwardAccelY;
    doc["calibration_backward_z"] = calibration.backwardAccelZ;
    doc["calibration_left_x"] = calibration.leftAccelX;
    doc["calibration_left_y"] = calibration.leftAccelY;
    doc["calibration_left_z"] = calibration.leftAccelZ;
    doc["calibration_right_x"] = calibration.rightAccelX;
    doc["calibration_right_y"] = calibration.rightAccelY;
    doc["calibration_right_z"] = calibration.rightAccelZ;
    
    // Serialize to string
    String jsonString;
    serializeJson(doc, jsonString);
    
    // NVS allows only ~508 bytes per key; chunk the JSON into NVS_CHUNK_SIZE pieces
    size_t len = jsonString.length();
    uint8_t numChunks = (len + NVS_CHUNK_SIZE - 1) / NVS_CHUNK_SIZE;
    if (numChunks > 200) {
        Serial.println("❌ Settings too large for NVS (chunk limit)");
        nvs.end();
        return false;
    }
    bool ok = true;
    for (uint8_t i = 0; i < numChunks && ok; i++) {
        size_t start = i * NVS_CHUNK_SIZE;
        size_t chunkLen = (start + NVS_CHUNK_SIZE <= len) ? NVS_CHUNK_SIZE : (len - start);
        String key = "s" + String(i);
        if (nvs.putString(key.c_str(), jsonString.substring(start, start + chunkLen)) == 0) {
            ok = false;
        }
    }
    if (ok) {
        nvs.putUChar(NVS_KEY_CHUNK_COUNT, numChunks);
        nvs.remove("settings");  // remove legacy single-key if present
    }
    nvs.end();
    
    if (ok) {
        Serial.printf("✅ Settings saved to NVS (%d bytes, %d chunks)\n", (int)len, numChunks);
        return true;
    } else {
        Serial.println("❌ Failed to write settings to NVS");
        return false;
    }
}

// Load all settings from filesystem
bool loadSettings() {
    // Try NVS first (survives OTA filesystem updates)
    if (loadSettingsFromNVS()) {
        Serial.println("✅ Settings loaded from NVS");
        // Migrate from SPIFFS to NVS if SPIFFS has newer data (one-time migration)
        migrateSettingsFromSPIFFSToNVS();
        return true;
    }
    
    // Fallback to SPIFFS (for backward compatibility)
    Serial.println("⚠️ No settings in NVS, trying SPIFFS...");
    File file = SPIFFS.open("/settings.json", "r");
    if (!file) {
        Serial.println("⚠️ No settings file found, using defaults");
        return false;
    }
    
    size_t fileSize = file.size();
    Serial.printf("📄 Loading settings.json (%d bytes)\n", fileSize);
    
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.printf("❌ Failed to parse settings.json: %s\n", error.c_str());
        Serial.println("🔄 Attempting to read WiFi settings directly from file...");
        
        // Try to read the file as a string and extract WiFi settings manually
        file = SPIFFS.open("/settings.json", "r");
        if (file) {
            String content = file.readString();
            file.close();
            
            // Simple string search for WiFi settings
            int apNameStart = content.indexOf("\"apName\":\"");
            int apPasswordStart = content.indexOf("\"apPassword\":\"");
            
            if (apNameStart != -1) {
                apNameStart += 10; // Skip "apName":"
                int apNameEnd = content.indexOf("\"", apNameStart);
                if (apNameEnd != -1) {
                    apName = content.substring(apNameStart, apNameEnd);
                    Serial.printf("🔧 Recovered AP Name: %s\n", apName.c_str());
                }
            }
            
            if (apPasswordStart != -1) {
                apPasswordStart += 13; // Skip "apPassword":"
                int apPasswordEnd = content.indexOf("\"", apPasswordStart);
                if (apPasswordEnd != -1) {
                    apPassword = content.substring(apPasswordStart, apPasswordEnd);
                    Serial.printf("🔧 Recovered AP Password: %s\n", apPassword.c_str());
                }
            }
        }
        return false;
    }
    
    // Load light settings
    headlightEffect = doc["headlight_effect"] | FX_SOLID;
    taillightEffect = doc["taillight_effect"] | FX_SOLID;
    headlightColor.r = doc["headlight_color_r"] | 0;   // Default: black
    headlightColor.g = doc["headlight_color_g"] | 0;   // Default: black
    headlightColor.b = doc["headlight_color_b"] | 0;   // Default: black
    taillightColor.r = doc["taillight_color_r"] | 0;   // Default: black
    taillightColor.g = doc["taillight_color_g"] | 0;   // Default: black
    taillightColor.b = doc["taillight_color_b"] | 0;   // Default: black
    headlightBackgroundEnabled = doc["headlight_background_enabled"] | false;
    taillightBackgroundEnabled = doc["taillight_background_enabled"] | false;
    headlightBackgroundColor.r = doc["headlight_background_r"] | 0;
    headlightBackgroundColor.g = doc["headlight_background_g"] | 0;
    headlightBackgroundColor.b = doc["headlight_background_b"] | 0;
    taillightBackgroundColor.r = doc["taillight_background_r"] | 0;
    taillightBackgroundColor.g = doc["taillight_background_g"] | 0;
    taillightBackgroundColor.b = doc["taillight_background_b"] | 0;
    globalBrightness = doc["global_brightness"] | DEFAULT_BRIGHTNESS;
    effectSpeed = doc["effect_speed"] | 64;
    currentPreset = doc["current_preset"] | PRESET_STANDARD;
    
    // Load startup sequence settings
    startupSequence = doc["startup_sequence"] | STARTUP_POWER_ON;
    startupEnabled = doc["startup_enabled"] | true;
    startupDuration = doc["startup_duration"] | 3000;
    
    // Load motion control settings
    motionEnabled = doc["motion_enabled"] | true;
    blinkerEnabled = doc["blinker_enabled"] | true;
    parkModeEnabled = doc["park_mode_enabled"] | true;
    impactDetectionEnabled = doc["impact_detection_enabled"] | true;
    motionSensitivity = doc["motion_sensitivity"] | 1.0;
    blinkerDelay = doc["blinker_delay"] | 300;
    blinkerTimeout = doc["blinker_timeout"] | 2000;
    parkDetectionAngle = doc["park_detection_angle"] | 15;
    impactThreshold = doc["impact_threshold"] | 3;
    parkAccelNoiseThreshold = doc["park_accel_noise_threshold"] | 0.05;
    parkGyroNoiseThreshold = doc["park_gyro_noise_threshold"] | 2.5;
    parkStationaryTime = doc["park_stationary_time"] | 2000;
    
    // Load direction-based lighting settings
    directionBasedLighting = doc["direction_based_lighting"] | false;
    headlightMode = doc["headlight_mode"] | 0;
    forwardAccelThreshold = doc["forward_accel_threshold"] | 0.3;
    
    // Load braking detection settings
    brakingEnabled = doc["braking_enabled"] | false;  // Default to disabled
    brakingThreshold = doc["braking_threshold"] | -0.5;
    brakingEffect = doc["braking_effect"] | 0;
    brakingBrightness = doc["braking_brightness"] | 255;
    
    // Load RGBW white channel setting
    rgbwWhiteMode = doc["rgbw_white_mode"] | (doc["white_leds_enabled"] | false ? 1 : 0);
    whiteLEDsEnabled = rgbwWhiteMode != 0;
    
    // Load park mode effect settings
    parkEffect = doc["park_effect"] | FX_BREATH;
    parkEffectSpeed = doc["park_effect_speed"] | 64;
    parkHeadlightColor.r = doc["park_headlight_color_r"] | 0;
    parkHeadlightColor.g = doc["park_headlight_color_g"] | 0;
    parkHeadlightColor.b = doc["park_headlight_color_b"] | 255;
    parkTaillightColor.r = doc["park_taillight_color_r"] | 0;
    parkTaillightColor.g = doc["park_taillight_color_g"] | 0;
    parkTaillightColor.b = doc["park_taillight_color_b"] | 255;
    parkBrightness = doc["park_brightness"] | 128;
    
    // Load OTA Update settings
    otaUpdateURL = doc["ota_update_url"] | "";
    
    // Load LED configuration
    headlightLedCount = doc["headlight_count"] | 11;
    taillightLedCount = doc["taillight_count"] | 11;
    headlightLedType = doc["headlight_type"] | 0;  // SK6812
    taillightLedType = doc["taillight_type"] | 0;  // SK6812
    headlightColorOrder = doc["headlight_order"] | 1;  // GRB - Default for SK6812 RGBW
    taillightColorOrder = doc["taillight_order"] | 1;  // GRB - Default for SK6812 RGBW
    
    // Load WiFi settings (unique default AP name from MAC to avoid conflicts when devices are near)
    apName = doc["apName"] | getDefaultApName();
    bluetoothDeviceName = apName;  // Keep BLE name in sync with AP name
    apPassword = doc["apPassword"] | "float420";
    Serial.printf("📡 Loaded WiFi settings: AP=%s, BLE=%s, Password=%s\n", apName.c_str(), bluetoothDeviceName.c_str(), apPassword.c_str());
    
    // Load ESPNow settings
    enableESPNow = doc["enableESPNow"] | true;
    useESPNowSync = doc["useESPNowSync"] | true;
    espNowChannel = doc["espNowChannel"] | 1;

    // Load presets
    loadPresetsFromDoc(doc);
    Serial.printf("📡 Loaded ESPNow settings: Enabled=%s, Sync=%s, Channel=%d\n", 
                  enableESPNow ? "Yes" : "No", useESPNowSync ? "Yes" : "No", espNowChannel);

    // Load presets
    loadPresetsFromDoc(doc);
    
    // Load group settings
    groupCode = doc["groupCode"] | "";
    isGroupMaster = doc["isGroupMaster"] | false;
    allowGroupJoin = doc["allowGroupJoin"] | false;
    deviceName = doc["deviceName"] | "";
    String storedMasterMac = doc["groupMasterMac"] | "";
    hasGroupMaster = doc["hasGroupMaster"] | false;
    if (storedMasterMac.length() > 0 && parseMacAddress(storedMasterMac, groupMasterMac)) {
        hasGroupMaster = true;
    }
    if (isGroupMaster) {
        hasGroupMaster = true;
        esp_wifi_get_mac(WIFI_IF_STA, groupMasterMac);
    }
    Serial.printf("🚴 Loaded group settings: Code=%s, Master=%s, DeviceName=%s\n", 
                  groupCode.c_str(), isGroupMaster ? "Yes" : "No", deviceName.c_str());
    
    // Load calibration data
    calibrationComplete = doc["calibration_complete"] | false;
    calibration.valid = doc["calibration_valid"] | false;
    if (calibration.valid) {
        String forwardAxisStr = doc["calibration_forward_axis"] | "X";
        calibration.forwardAxis = forwardAxisStr.charAt(0);
        calibration.forwardSign = doc["calibration_forward_sign"] | 1;
        String leftRightAxisStr = doc["calibration_leftright_axis"] | "Y";
        calibration.leftRightAxis = leftRightAxisStr.charAt(0);
        calibration.leftRightSign = doc["calibration_leftright_sign"] | 1;
        calibration.levelAccelX = doc["calibration_level_x"] | 0.0;
        calibration.levelAccelY = doc["calibration_level_y"] | 0.0;
        calibration.levelAccelZ = doc["calibration_level_z"] | 1.0;
        calibration.forwardAccelX = doc["calibration_forward_x"] | 0.0;
        calibration.forwardAccelY = doc["calibration_forward_y"] | 0.0;
        calibration.forwardAccelZ = doc["calibration_forward_z"] | 1.0;
        calibration.backwardAccelX = doc["calibration_backward_x"] | 0.0;
        calibration.backwardAccelY = doc["calibration_backward_y"] | 0.0;
        calibration.backwardAccelZ = doc["calibration_backward_z"] | 1.0;
        calibration.leftAccelX = doc["calibration_left_x"] | 0.0;
        calibration.leftAccelY = doc["calibration_left_y"] | 0.0;
        calibration.leftAccelZ = doc["calibration_left_z"] | 1.0;
        calibration.rightAccelX = doc["calibration_right_x"] | 0.0;
        calibration.rightAccelY = doc["calibration_right_y"] | 0.0;
        calibration.rightAccelZ = doc["calibration_right_z"] | 1.0;
        
        Serial.println("✅ Calibration data loaded from filesystem:");
        Serial.printf("Forward axis: %c (sign: %d)\n", calibration.forwardAxis, calibration.forwardSign);
        Serial.printf("Left/Right axis: %c (sign: %d)\n", calibration.leftRightAxis, calibration.leftRightSign);
    }
    
    Serial.println("✅ Settings loaded from SPIFFS");
    Serial.printf("Headlight: RGB(%d,%d,%d), Taillight: RGB(%d,%d,%d)\n", 
                  headlightColor.r, headlightColor.g, headlightColor.b,
                  taillightColor.r, taillightColor.g, taillightColor.b);
    Serial.printf("Brightness: %d, Speed: %d, Preset: %d\n", 
                  globalBrightness, effectSpeed, currentPreset);
    Serial.printf("Startup: %s (%dms), Enabled: %s\n", 
                  getStartupSequenceName(startupSequence).c_str(), startupDuration, startupEnabled ? "Yes" : "No");
    
    // Defer NVS migration to after LEDs are shown (non-blocking)
    // Check if migration is needed (chunked "sc" or legacy "settings")
    nvs.begin(NVS_NAMESPACE, true); // Open read-only to check
    bool nvsHasData = nvs.isKey(NVS_KEY_CHUNK_COUNT) || nvs.isKey("settings");
    nvs.end();
    
    if (!nvsHasData) {
        nvsMigrationPending = true;
        Serial.println("🔄 NVS migration needed (will happen in background)");
    } else {
        Serial.println("✅ NVS already has settings");
    }
    
    // Apply RGBW white channel setting with loaded settings
    applyRgbwWhiteChannelMode();
    
    return true;
}

// Load all settings from NVS (survives OTA filesystem updates)
bool loadSettingsFromNVS() {
    if (!nvs.begin(NVS_NAMESPACE, true)) {  // Read-only mode
        Serial.println("⚠️ Failed to open NVS namespace (read-only)");
        return false;
    }
    
    String jsonString;
    if (nvs.isKey(NVS_KEY_CHUNK_COUNT)) {
        // Chunked format (current)
        uint8_t numChunks = nvs.getUChar(NVS_KEY_CHUNK_COUNT, 0);
        if (numChunks == 0) {
            Serial.println("⚠️ No NVS chunk count");
            nvs.end();
            return false;
        }
        jsonString.reserve(numChunks * NVS_CHUNK_SIZE);
        for (uint8_t i = 0; i < numChunks; i++) {
            String key = "s" + String(i);
            if (!nvs.isKey(key.c_str())) {
                Serial.printf("⚠️ NVS chunk %s missing\n", key.c_str());
                nvs.end();
                return false;
            }
            jsonString += nvs.getString(key.c_str(), "");
        }
    } else if (nvs.isKey("settings")) {
        // Legacy single-key (only works if blob was < 508 bytes)
        jsonString = nvs.getString("settings", "");
    } else {
        Serial.println("⚠️ No settings found in NVS");
        nvs.end();
        return false;
    }
    nvs.end();
    
    if (jsonString.length() == 0) {
        Serial.println("⚠️ Settings string is empty in NVS");
        return false;
    }
    
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, jsonString);
    
    if (error) {
        Serial.printf("❌ Failed to parse NVS settings: %s\n", error.c_str());
        return false;
    }
    
    // Load all settings (same structure as SPIFFS version)
    // Light settings
    headlightEffect = doc["headlight_effect"] | 0;
    taillightEffect = doc["taillight_effect"] | 0;
    headlightColor.r = doc["headlight_color_r"] | 255;
    headlightColor.g = doc["headlight_color_g"] | 255;
    headlightColor.b = doc["headlight_color_b"] | 255;
    taillightColor.r = doc["taillight_color_r"] | 255;
    taillightColor.g = doc["taillight_color_g"] | 0;
    taillightColor.b = doc["taillight_color_b"] | 0;
    headlightBackgroundEnabled = doc["headlight_background_enabled"] | false;
    taillightBackgroundEnabled = doc["taillight_background_enabled"] | false;
    headlightBackgroundColor.r = doc["headlight_background_r"] | 0;
    headlightBackgroundColor.g = doc["headlight_background_g"] | 0;
    headlightBackgroundColor.b = doc["headlight_background_b"] | 0;
    taillightBackgroundColor.r = doc["taillight_background_r"] | 0;
    taillightBackgroundColor.g = doc["taillight_background_g"] | 0;
    taillightBackgroundColor.b = doc["taillight_background_b"] | 0;
    globalBrightness = doc["global_brightness"] | 128;
    effectSpeed = doc["effect_speed"] | 128;
    currentPreset = doc["current_preset"] | 0;
    
    // Startup sequence settings
    startupSequence = doc["startup_sequence"] | STARTUP_NONE;
    startupEnabled = doc["startup_enabled"] | false;
    startupDuration = doc["startup_duration"] | 3000;
    
    // Motion control settings
    motionEnabled = doc["motion_enabled"] | true;
    blinkerEnabled = doc["blinker_enabled"] | true;
    parkModeEnabled = doc["park_mode_enabled"] | true;
    impactDetectionEnabled = doc["impact_detection_enabled"] | true;
    motionSensitivity = doc["motion_sensitivity"] | 1.0;
    blinkerDelay = doc["blinker_delay"] | 300;
    blinkerTimeout = doc["blinker_timeout"] | 2000;
    parkDetectionAngle = doc["park_detection_angle"] | 15;
    impactThreshold = doc["impact_threshold"] | 3;
    parkAccelNoiseThreshold = doc["park_accel_noise_threshold"] | 0.05;
    parkGyroNoiseThreshold = doc["park_gyro_noise_threshold"] | 2.5;
    parkStationaryTime = doc["park_stationary_time"] | 2000;
    
    // Direction-based lighting settings
    directionBasedLighting = doc["direction_based_lighting"] | false;
    headlightMode = doc["headlight_mode"] | 0;
    forwardAccelThreshold = doc["forward_accel_threshold"] | 0.3;
    
    // Braking detection settings
    brakingEnabled = doc["braking_enabled"] | false;
    brakingThreshold = doc["braking_threshold"] | -0.5;
    brakingEffect = doc["braking_effect"] | 0;
    brakingBrightness = doc["braking_brightness"] | 255;
    
    // Load park mode effect settings
    parkEffect = doc["park_effect"] | FX_BREATH;
    parkEffectSpeed = doc["park_effect_speed"] | 64;
    parkHeadlightColor.r = doc["park_headlight_color_r"] | 0;
    parkHeadlightColor.g = doc["park_headlight_color_g"] | 0;
    parkHeadlightColor.b = doc["park_headlight_color_b"] | 255;
    parkTaillightColor.r = doc["park_taillight_color_r"] | 0;
    parkTaillightColor.g = doc["park_taillight_color_g"] | 0;
    parkTaillightColor.b = doc["park_taillight_color_b"] | 255;
    parkBrightness = doc["park_brightness"] | 128;
    
    // Load OTA Update settings
    otaUpdateURL = doc["ota_update_url"] | "";
    
    // Load LED configuration
    headlightLedCount = doc["headlight_count"] | 11;
    taillightLedCount = doc["taillight_count"] | 11;
    headlightLedType = doc["headlight_type"] | 0;
    taillightLedType = doc["taillight_type"] | 0;
    headlightColorOrder = doc["headlight_order"] | 1;
    taillightColorOrder = doc["taillight_order"] | 1;
    
    // Load WiFi settings (unique default AP name from MAC)
    apName = doc["apName"] | getDefaultApName();
    bluetoothDeviceName = apName;  // Keep BLE name in sync with AP name
    apPassword = doc["apPassword"] | "float420";
    
    // Load ESPNow settings
    enableESPNow = doc["enableESPNow"] | true;
    useESPNowSync = doc["useESPNowSync"] | true;
    espNowChannel = doc["espNowChannel"] | 1;
    
    // Load group settings
    groupCode = doc["groupCode"] | "";
    isGroupMaster = doc["isGroupMaster"] | false;
    allowGroupJoin = doc["allowGroupJoin"] | false;
    deviceName = doc["deviceName"] | "";
    
    // Load calibration data (CRITICAL - must survive OTA filesystem updates)
    calibrationComplete = doc["calibration_complete"] | false;
    calibration.valid = doc["calibration_valid"] | false;
    if (calibration.valid) {
        String forwardAxisStr = doc["calibration_forward_axis"] | "X";
        calibration.forwardAxis = forwardAxisStr.charAt(0);
        calibration.forwardSign = doc["calibration_forward_sign"] | 1;
        String leftRightAxisStr = doc["calibration_leftright_axis"] | "Y";
        calibration.leftRightAxis = leftRightAxisStr.charAt(0);
        calibration.leftRightSign = doc["calibration_leftright_sign"] | 1;
        calibration.levelAccelX = doc["calibration_level_x"] | 0.0;
        calibration.levelAccelY = doc["calibration_level_y"] | 0.0;
        calibration.levelAccelZ = doc["calibration_level_z"] | 1.0;
        calibration.forwardAccelX = doc["calibration_forward_x"] | 0.0;
        calibration.forwardAccelY = doc["calibration_forward_y"] | 0.0;
        calibration.forwardAccelZ = doc["calibration_forward_z"] | 1.0;
        calibration.backwardAccelX = doc["calibration_backward_x"] | 0.0;
        calibration.backwardAccelY = doc["calibration_backward_y"] | 0.0;
        calibration.backwardAccelZ = doc["calibration_backward_z"] | 1.0;
        calibration.leftAccelX = doc["calibration_left_x"] | 0.0;
        calibration.leftAccelY = doc["calibration_left_y"] | 0.0;
        calibration.leftAccelZ = doc["calibration_left_z"] | 1.0;
        calibration.rightAccelX = doc["calibration_right_x"] | 0.0;
        calibration.rightAccelY = doc["calibration_right_y"] | 0.0;
        calibration.rightAccelZ = doc["calibration_right_z"] | 1.0;
        
        Serial.println("✅ Calibration data loaded from NVS:");
        Serial.printf("Forward axis: %c (sign: %d)\n", calibration.forwardAxis, calibration.forwardSign);
        Serial.printf("Left/Right axis: %c (sign: %d)\n", calibration.leftRightAxis, calibration.leftRightSign);
    }
    
    return true;
}

// Migrate settings from SPIFFS to NVS (one-time migration)
bool migrateSettingsFromSPIFFSToNVS() {
    // Check if SPIFFS has settings.json
    if (!SPIFFS.exists("/settings.json")) {
        return false;  // Nothing to migrate
    }
    
    File file = SPIFFS.open("/settings.json", "r");
    if (!file) {
        return false;
    }
    
    // Check if NVS already has settings (chunked or legacy)
    if (!nvs.begin(NVS_NAMESPACE, true)) {
        file.close();
        return false;
    }
    
    bool nvsHasCalibration = nvs.isKey(NVS_KEY_CHUNK_COUNT) || nvs.isKey("settings");
    nvs.end();
    
    if (nvsHasCalibration) {
        // NVS already has settings, no need to migrate
        file.close();
        return false;
    }
    
    // Read SPIFFS settings
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        return false;
    }
    
    // Check if SPIFFS has calibration data
    if (doc.containsKey("calibration_valid") && doc["calibration_valid"]) {
        Serial.println("🔄 Migrating calibration from SPIFFS to NVS...");
        // Save current settings (which includes SPIFFS-loaded values) to NVS
        return saveSettingsToNVS();
    }
    
    return false;
}

// Test filesystem functionality
void testFilesystem() {
    Serial.println("🧪 Testing Filesystem...");
    
    // List all files in SPIFFS
    Serial.println("📁 SPIFFS file listing:");
    File root = SPIFFS.open("/");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            Serial.printf("  📄 %s (%d bytes)\n", file.name(), file.size());
            file = root.openNextFile();
        }
        root.close();
    }
    
    // Test write
    DynamicJsonDocument testDoc(128);
    testDoc["test_value"] = 123;
    testDoc["timestamp"] = millis();
    
    File file = SPIFFS.open("/test.json", "w");
    if (file) {
        serializeJson(testDoc, file);
        file.close();
        Serial.println("✅ Test file written");
        
        // Test read
        file = SPIFFS.open("/test.json", "r");
        if (file) {
            DynamicJsonDocument readDoc(128);
            DeserializationError error = deserializeJson(readDoc, file);
            file.close();
            
            if (!error) {
                int testValue = readDoc["test_value"];
                Serial.printf("✅ Test file read: %d\n", testValue);
                if (testValue == 123) {
                    Serial.println("✅ Filesystem working correctly!");
                } else {
                    Serial.println("❌ Data corruption detected!");
                }
            } else {
                Serial.println("❌ Failed to parse test file");
            }
        } else {
            Serial.println("❌ Failed to read test file");
        }
    } else {
        Serial.println("❌ Failed to write test file");
    }
}

// ESPNow Functions
bool initESPNow() {
    if (!enableESPNow) {
        Serial.println("ESPNow: Disabled");
        espNowState = 0;
        espNowLastError = 0;
        return false;
    }
    
    // Reset any previous ESP-NOW state
    esp_now_deinit();
    espNowPeerCount = 0;
    espNowState = 0;
    espNowLastError = 0;

    // Initialize ESPNow
    esp_err_t initResult = esp_now_init();
    if (initResult != ESP_OK) {
        espNowLastError = initResult;
        Serial.printf("ESPNow: Failed to initialize (%d:%s)\n",
                      initResult, espNowErrorName(initResult));
        espNowState = 2; // Error
        return false;
    }
    
    // Register callbacks
    esp_now_register_send_cb(espNowSendCallback);
    esp_now_register_recv_cb(espNowReceiveCallback);
    
    // Add broadcast peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, espNowBroadcastAddress, 6);
    peerInfo.channel = 0; // Use current channel
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_AP;
    
    esp_err_t addResult = esp_now_add_peer(&peerInfo);
    if (addResult != ESP_OK && addResult != ESP_ERR_ESPNOW_EXIST) {
        espNowLastError = addResult;
        Serial.printf("ESPNow: Failed to add broadcast peer (%d:%s)\n",
                      addResult, espNowErrorName(addResult));
        espNowState = 2; // Error
        return false;
    }
    
    espNowState = 1; // On
    espNowLastError = 0;
    Serial.println("ESPNow: Initialized successfully");
    return true;
}

void deinitESPNow() {
    esp_now_deinit();
    espNowState = 0;
    espNowLastError = 0;
    espNowPeerCount = 0;
    Serial.println("ESPNow: Deinitialized");
}

bool ensureESPNowActive(const char* context) {
    if (!enableESPNow) {
        return false;
    }
    if (espNowState == 1) {
        return true;
    }
    Serial.printf("ESPNow: Reinit requested (%s), state=%d, err=%d\n",
                  context, espNowState, espNowLastError);
    return initESPNow();
}

const char* espNowErrorName(esp_err_t error) {
    const char* name = esp_err_to_name(error);
    return name ? name : "UNKNOWN";
}

void sendESPNowData() {
    if (!enableESPNow || !useESPNowSync || espNowState != 1) {
        return;
    }
    // Only broadcast LED sync when leading a group
    if (groupCode.length() == 0 || !isGroupMaster) {
        return;
    }
    
    // Only send if not in motion-based effects (blinkers, park mode)
    if (blinkerActive || parkModeActive) {
        return;
    }
    
    uint32_t currentTime = millis();
    bool hasChange = false;
    if (!hasLastSyncState) {
        hasChange = true;
    } else {
        hasChange |= lastSyncState.brightness != globalBrightness;
        hasChange |= lastSyncState.headlightEffect != headlightEffect;
        hasChange |= lastSyncState.taillightEffect != taillightEffect;
        hasChange |= lastSyncState.effectSpeed != effectSpeed;
        hasChange |= lastSyncState.headlightColor[0] != headlightColor.r;
        hasChange |= lastSyncState.headlightColor[1] != headlightColor.g;
        hasChange |= lastSyncState.headlightColor[2] != headlightColor.b;
        hasChange |= lastSyncState.taillightColor[0] != taillightColor.r;
        hasChange |= lastSyncState.taillightColor[1] != taillightColor.g;
        hasChange |= lastSyncState.taillightColor[2] != taillightColor.b;
        hasChange |= lastSyncState.headlightBackgroundEnabled != headlightBackgroundEnabled;
        hasChange |= lastSyncState.taillightBackgroundEnabled != taillightBackgroundEnabled;
        hasChange |= lastSyncState.headlightBackgroundColor[0] != headlightBackgroundColor.r;
        hasChange |= lastSyncState.headlightBackgroundColor[1] != headlightBackgroundColor.g;
        hasChange |= lastSyncState.headlightBackgroundColor[2] != headlightBackgroundColor.b;
        hasChange |= lastSyncState.taillightBackgroundColor[0] != taillightBackgroundColor.r;
        hasChange |= lastSyncState.taillightBackgroundColor[1] != taillightBackgroundColor.g;
        hasChange |= lastSyncState.taillightBackgroundColor[2] != taillightBackgroundColor.b;
        hasChange |= lastSyncState.preset != currentPreset;
    }

    uint32_t interval = hasChange ? ESPNOW_SYNC_MIN_INTERVAL : ESPNOW_SYNC_IDLE_INTERVAL;
    if (currentTime - lastESPNowSend < interval) {
        return;
    }
    
    ESPNowLEDData data;
    data.magic = 'A';
    data.packetNum = 0;
    data.totalPackets = 1;
    data.brightness = globalBrightness;
    data.headlightEffect = headlightEffect;
    data.taillightEffect = taillightEffect;
    data.effectSpeed = effectSpeed;
    data.headlightColor[0] = headlightColor.r;
    data.headlightColor[1] = headlightColor.g;
    data.headlightColor[2] = headlightColor.b;
    data.taillightColor[0] = taillightColor.r;
    data.taillightColor[1] = taillightColor.g;
    data.taillightColor[2] = taillightColor.b;
    data.headlightBackgroundEnabled = headlightBackgroundEnabled;
    data.taillightBackgroundEnabled = taillightBackgroundEnabled;
    data.headlightBackgroundColor[0] = headlightBackgroundColor.r;
    data.headlightBackgroundColor[1] = headlightBackgroundColor.g;
    data.headlightBackgroundColor[2] = headlightBackgroundColor.b;
    data.taillightBackgroundColor[0] = taillightBackgroundColor.r;
    data.taillightBackgroundColor[1] = taillightBackgroundColor.g;
    data.taillightBackgroundColor[2] = taillightBackgroundColor.b;
    data.preset = currentPreset;
    
    // Add timing coordination data
    data.syncTimestamp = currentTime;
    data.masterStep = (headlightTiming.step > taillightTiming.step) ? headlightTiming.step : taillightTiming.step;
    data.stripLength = (headlightLedCount > taillightLedCount) ? headlightLedCount : taillightLedCount;
    
    // Calculate checksum
    data.checksum = 0;
    uint8_t* dataPtr = (uint8_t*)&data;
    for (int i = 0; i < sizeof(data) - 1; i++) {
        data.checksum ^= dataPtr[i];
    }
    
    esp_err_t result = esp_now_send(espNowBroadcastAddress, (uint8_t*)&data, sizeof(data));
    if (result == ESP_OK) {
        lastESPNowSend = currentTime;
        lastSyncState.brightness = globalBrightness;
        lastSyncState.headlightEffect = headlightEffect;
        lastSyncState.taillightEffect = taillightEffect;
        lastSyncState.effectSpeed = effectSpeed;
        lastSyncState.headlightColor[0] = headlightColor.r;
        lastSyncState.headlightColor[1] = headlightColor.g;
        lastSyncState.headlightColor[2] = headlightColor.b;
        lastSyncState.taillightColor[0] = taillightColor.r;
        lastSyncState.taillightColor[1] = taillightColor.g;
        lastSyncState.taillightColor[2] = taillightColor.b;
        lastSyncState.headlightBackgroundEnabled = headlightBackgroundEnabled;
        lastSyncState.taillightBackgroundEnabled = taillightBackgroundEnabled;
        lastSyncState.headlightBackgroundColor[0] = headlightBackgroundColor.r;
        lastSyncState.headlightBackgroundColor[1] = headlightBackgroundColor.g;
        lastSyncState.headlightBackgroundColor[2] = headlightBackgroundColor.b;
        lastSyncState.taillightBackgroundColor[0] = taillightBackgroundColor.r;
        lastSyncState.taillightBackgroundColor[1] = taillightBackgroundColor.g;
        lastSyncState.taillightBackgroundColor[2] = taillightBackgroundColor.b;
        lastSyncState.preset = currentPreset;
        hasLastSyncState = true;
    } else {
        //Serial.printf("ESPNow: Send failed with error %d\n", result);
    }
}

void addESPNowPeer(uint8_t* macAddress) {
    if (espNowPeerCount >= 10) {
        Serial.println("ESPNow: Maximum peer count reached");
        return;
    }
    
    // Check if peer already exists
    for (int i = 0; i < espNowPeerCount; i++) {
        if (memcmp(espNowPeers[i].mac, macAddress, 6) == 0) {
            espNowPeers[i].lastSeen = millis();
            espNowPeers[i].isActive = true;
            return;
        }
    }
    
    // Add new peer
    memcpy(espNowPeers[espNowPeerCount].mac, macAddress, 6);
    espNowPeers[espNowPeerCount].channel = espNowChannel;
    espNowPeers[espNowPeerCount].isActive = true;
    espNowPeers[espNowPeerCount].lastSeen = millis();
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macAddress, 6);
    peerInfo.channel = 0; // Use current channel
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_AP;
    
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        espNowPeerCount++;
        Serial.printf("ESPNow: Added peer %02x:%02x:%02x:%02x:%02x:%02x\n", 
                     macAddress[0], macAddress[1], macAddress[2], 
                     macAddress[3], macAddress[4], macAddress[5]);
    }
}

// Group Management Functions
void handleGroupMessage(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (len != sizeof(ESPNowGroupData)) return;
    
    ESPNowGroupData* groupData = (ESPNowGroupData*)data;
    
    // Verify checksum
    uint8_t calculatedChecksum = 0;
    for (int i = 0; i < (int)sizeof(ESPNowGroupData) - 1; i++) {
        calculatedChecksum ^= data[i];
    }
    if (calculatedChecksum != groupData->checksum) {
        Serial.println("Group: Invalid checksum");
        return;
    }
    
    // Auto-join discovery: accept first heartbeat when scanning
    if (groupCode.length() == 0 && autoJoinOnHeartbeat && groupData->messageType == 0) {
        groupCode = String(groupData->groupCode);
        isGroupMaster = false;
        hasGroupMaster = false;
        autoJoinOnHeartbeat = false;
        joinInProgress = true;
        memset(groupMasterMac, 0, sizeof(groupMasterMac));
        groupMemberCount = 0;
        sendJoinRequest();
        Serial.printf("Group: Discovered and joining code %s\n", groupCode.c_str());
        return;
    }

    // Check if group code matches
    if (strcmp(groupData->groupCode, groupCode.c_str()) != 0) {
        return; // Not for our group
    }
    
    switch (groupData->messageType) {
        case 0: // Heartbeat
            if (!isGroupMaster) {
                if (!hasGroupMaster || memcmp(mac_addr, groupMasterMac, 6) == 0) {
                    if (!hasGroupMaster) {
                        memcpy(groupMasterMac, mac_addr, 6);
                        hasGroupMaster = true;
                    }
                    masterHeartbeat = millis();
                    joinInProgress = false;
                    Serial.printf("Group: Received heartbeat from %s\n", groupData->deviceName);
                }
            }
            break;
            
        case 1: // Join request
            if (isGroupMaster && allowGroupJoin) {
                Serial.printf("Group: Join request from %s (%02x:%02x:%02x:%02x:%02x:%02x)\n", 
                             groupData->deviceName, mac_addr[0], mac_addr[1], mac_addr[2], 
                             mac_addr[3], mac_addr[4], mac_addr[5]);
                addESPNowPeer((uint8_t*)mac_addr);
                addGroupMember(mac_addr, groupData->deviceName);
                sendJoinResponse(mac_addr, true);
            } else if (isGroupMaster && !allowGroupJoin) {
                Serial.println("Group: Join request ignored (joining disabled)");
            }
            break;
            
        case 2: // Join accept
            if (!isGroupMaster) {
                Serial.printf("Group: Join accepted by %s\n", groupData->deviceName);
                isGroupMaster = false; // We're now a follower
                if (!hasGroupMaster) {
                    memcpy(groupMasterMac, mac_addr, 6);
                    hasGroupMaster = true;
                }
                masterHeartbeat = millis();
                joinInProgress = false;
                addGroupMember(mac_addr, groupData->deviceName);
            }
            break;
            
        case 3: // Join reject
            Serial.printf("Group: Join rejected by %s\n", groupData->deviceName);
            break;
            
        case 4: // Master election
            if (!isGroupMaster) {
                Serial.println("Group: Master election received");
                // Could implement master election logic here
            }
            break;
    }
}

bool isGroupMember(const uint8_t* mac_addr) {
    for (int i = 0; i < groupMemberCount; i++) {
        if (memcmp(groupMembers[i].mac, mac_addr, 6) == 0) {
            return groupMembers[i].isAuthenticated;
        }
    }
    return false;
}

void sendGroupHeartbeat() {
    if (millis() - lastGroupHeartbeat < HEARTBEAT_INTERVAL) return;
    if (!ensureESPNowActive("heartbeat")) {
        return;
    }
    
    ESPNowGroupData data;
    data.magic = 'G';
    data.messageType = 0; // Heartbeat
    strncpy(data.groupCode, groupCode.c_str(), 6);
    data.groupCode[6] = '\0';
    strncpy(data.deviceName, deviceName.c_str(), 20);
    data.deviceName[20] = '\0';
    
    // Get device MAC
    String macStr = getDeviceMAC();
    sscanf(macStr.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", 
           &data.macAddress[0], &data.macAddress[1], &data.macAddress[2],
           &data.macAddress[3], &data.macAddress[4], &data.macAddress[5]);
    
    data.timestamp = millis();
    
    // Calculate checksum
    data.checksum = 0;
    uint8_t* dataPtr = (uint8_t*)&data;
    for (int i = 0; i < sizeof(data) - 1; i++) {
        data.checksum ^= dataPtr[i];
    }
    
    esp_err_t result = esp_now_send(espNowBroadcastAddress, (uint8_t*)&data, sizeof(data));
    if (result != ESP_OK) {
        espNowLastError = result;
        if (result == ESP_ERR_ESPNOW_NOT_INIT || result == ESP_ERR_INVALID_STATE) {
            espNowState = 2;
        }
        Serial.printf("Group: Heartbeat send failed (%d:%s)\n",
                      result, espNowErrorName(result));
        return;
    }
    lastGroupHeartbeat = millis();
}

void sendJoinRequest() {
    if (groupCode.length() != 6) return;
    if (!ensureESPNowActive("join")) {
        Serial.println("Group: Join request skipped (ESPNow not active)");
        return;
    }
    ESPNowGroupData data;
    data.magic = 'G';
    data.messageType = 1; // Join request
    strncpy(data.groupCode, groupCode.c_str(), 6);
    data.groupCode[6] = '\0';
    strncpy(data.deviceName, deviceName.c_str(), 20);
    data.deviceName[20] = '\0';
    
    // Get device MAC
    String macStr = getDeviceMAC();
    sscanf(macStr.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", 
           &data.macAddress[0], &data.macAddress[1], &data.macAddress[2],
           &data.macAddress[3], &data.macAddress[4], &data.macAddress[5]);
    
    data.timestamp = millis();
    
    // Calculate checksum
    data.checksum = 0;
    uint8_t* dataPtr = (uint8_t*)&data;
    for (int i = 0; i < sizeof(data) - 1; i++) {
        data.checksum ^= dataPtr[i];
    }
    
    esp_err_t result = esp_now_send(espNowBroadcastAddress, (uint8_t*)&data, sizeof(data));
    if (result != ESP_OK) {
        espNowLastError = result;
        if (result == ESP_ERR_ESPNOW_NOT_INIT || result == ESP_ERR_INVALID_STATE) {
            espNowState = 2;
        }
        Serial.printf("Group: Join request send failed (%d:%s)\n",
                      result, espNowErrorName(result));
        return;
    }
    lastJoinRequest = millis();
    Serial.println("Group: Sent join request");
}

void sendJoinResponse(const uint8_t* mac_addr, bool accept) {
    ESPNowGroupData data;
    data.magic = 'G';
    data.messageType = accept ? 2 : 3; // Join accept/reject
    strncpy(data.groupCode, groupCode.c_str(), 6);
    data.groupCode[6] = '\0';
    strncpy(data.deviceName, deviceName.c_str(), 20);
    data.deviceName[20] = '\0';
    
    // Get device MAC
    String macStr = getDeviceMAC();
    sscanf(macStr.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", 
           &data.macAddress[0], &data.macAddress[1], &data.macAddress[2],
           &data.macAddress[3], &data.macAddress[4], &data.macAddress[5]);
    
    data.timestamp = millis();
    
    // Calculate checksum
    data.checksum = 0;
    uint8_t* dataPtr = (uint8_t*)&data;
    for (int i = 0; i < sizeof(data) - 1; i++) {
        data.checksum ^= dataPtr[i];
    }
    
    esp_now_send(mac_addr, (uint8_t*)&data, sizeof(data));
    Serial.printf("Group: Sent join %s to %02x:%02x:%02x:%02x:%02x:%02x\n", 
                  accept ? "accept" : "reject", mac_addr[0], mac_addr[1], mac_addr[2], 
                  mac_addr[3], mac_addr[4], mac_addr[5]);
}

void addGroupMember(const uint8_t* mac_addr, const char* deviceName) {
    if (groupMemberCount >= 10) return;
    
    for (int i = 0; i < groupMemberCount; i++) {
        if (memcmp(groupMembers[i].mac, mac_addr, 6) == 0) {
            strncpy(groupMembers[i].deviceName, deviceName, 20);
            groupMembers[i].deviceName[20] = '\0';
            groupMembers[i].lastSeen = millis();
            groupMembers[i].isAuthenticated = true;
            return;
        }
    }

    memcpy(groupMembers[groupMemberCount].mac, mac_addr, 6);
    strncpy(groupMembers[groupMemberCount].deviceName, deviceName, 20);
    groupMembers[groupMemberCount].deviceName[20] = '\0';
    groupMembers[groupMemberCount].lastSeen = millis();
    groupMembers[groupMemberCount].isAuthenticated = true;
    groupMemberCount++;
    
    Serial.printf("Group: Added member %s (%02x:%02x:%02x:%02x:%02x:%02x)\n", 
                  deviceName, mac_addr[0], mac_addr[1], mac_addr[2], 
                  mac_addr[3], mac_addr[4], mac_addr[5]);
}

void removeGroupMember(const uint8_t* mac_addr) {
    for (int i = 0; i < groupMemberCount; i++) {
        if (memcmp(groupMembers[i].mac, mac_addr, 6) == 0) {
            // Shift remaining members
            for (int j = i; j < groupMemberCount - 1; j++) {
                groupMembers[j] = groupMembers[j + 1];
            }
            groupMemberCount--;
            Serial.printf("Group: Removed member %02x:%02x:%02x:%02x:%02x:%02x\n", 
                          mac_addr[0], mac_addr[1], mac_addr[2], 
                          mac_addr[3], mac_addr[4], mac_addr[5]);
            break;
        }
    }
}

void checkMasterTimeout() {
    if (isGroupMaster) return; // We are the master

    if (joinInProgress) return; // Avoid self-election while joining
    
    if (millis() - masterHeartbeat > MASTER_TIMEOUT) {
        Serial.println("Group: Master timeout - becoming master");
        becomeMaster();
    }
}

void becomeMaster() {
    isGroupMaster = true;
    masterHeartbeat = millis();
    hasGroupMaster = true;
    esp_wifi_get_mac(WIFI_IF_STA, groupMasterMac);
    Serial.println("Group: Now acting as master");
}

void generateGroupCode() {
    if (groupCode.length() == 0) {
        groupCode = String(random(100000, 999999));
        Serial.printf("Group: Generated code %s\n", groupCode.c_str());
    }
}

String getDeviceMAC() {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    return String(mac[0], HEX) + ":" + String(mac[1], HEX) + ":" + 
           String(mac[2], HEX) + ":" + String(mac[3], HEX) + ":" + 
           String(mac[4], HEX) + ":" + String(mac[5], HEX);
}

String formatColorHex(const CRGB& color) {
    char buffer[7];
    snprintf(buffer, sizeof(buffer), "%02x%02x%02x", color.r, color.g, color.b);
    return String(buffer);
}

// Generate unique default AP/BLE name: ARKLIGHTS-XXXXXX (last 3 bytes of MAC as hex)
String getDefaultApName() {
    uint64_t chipId = ESP.getEfuseMac();
    uint8_t macBytes[6];
    for (int i = 0; i < 6; i++) {
        macBytes[i] = (chipId >> (40 - i * 8)) & 0xFF;
    }
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02X%02X%02X", macBytes[3], macBytes[4], macBytes[5]);
    return String("ARKLIGHTS-") + suffix;
}

String formatMacAddress(const uint8_t* mac) {
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buffer);
}

bool parseMacAddress(const String& macStr, uint8_t* outMac) {
    if (macStr.length() != 17) {
        return false;
    }
    int values[6];
    int parsed = sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
                        &values[0], &values[1], &values[2],
                        &values[3], &values[4], &values[5]);
    if (parsed != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        outMac[i] = static_cast<uint8_t>(values[i]);
    }
    return true;
}

// Helper function for saving UI files
bool saveUIFile(const String& filename, const String& content) {
    // Ensure filename starts with / (avoid double slashes)
    String cleanFilename = filename;
    if (cleanFilename.charAt(0) != '/') {
        cleanFilename = "/" + cleanFilename;
    }
    
    Serial.printf("Saving file: %s (%d bytes)\n", cleanFilename.c_str(), content.length());
    
    // Write the file
    File targetFile = SPIFFS.open(cleanFilename, "w");
    if (targetFile) {
        size_t bytesWritten = targetFile.print(content);
        targetFile.close();
        Serial.printf("Successfully saved: %s (%d bytes written)\n", cleanFilename.c_str(), bytesWritten);
        
        // Verify the file was written correctly
        File verifyFile = SPIFFS.open(cleanFilename, "r");
        if (verifyFile) {
            Serial.printf("✅ Verification: %s exists (%d bytes)\n", cleanFilename.c_str(), verifyFile.size());
            verifyFile.close();
            return true;
        } else {
            Serial.printf("❌ Verification failed: %s not found after write\n", cleanFilename.c_str());
            return false;
        }
    } else {
        Serial.printf("Failed to write file: %s\n", cleanFilename.c_str());
        return false;
    }
}

// Streaming UI update function for large files
bool processUIUpdateStreaming(const String& updatePath) {
    Serial.printf("Processing UI update (streaming): %s\n", updatePath.c_str());
    
    File updateFile = SPIFFS.open(updatePath, "r");
    if (!updateFile) {
        Serial.println("Failed to open update file");
        return false;
    }
    
    // Process text-based update format: FILENAME:CONTENT:ENDFILE
    String line;
    String currentFilename = "";
    String currentContent = "";
    bool inFileContent = false;
    int filesProcessed = 0;
    
    Serial.println("Processing text-based UI update (streaming mode)");
    
    while (updateFile.available()) {
        line = updateFile.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("FILENAME:")) {
            // Save previous file if we have one
            if (currentFilename.length() > 0 && currentContent.length() > 0) {
                if (saveUIFile(currentFilename, currentContent)) {
                    filesProcessed++;
                }
            }
            
            // Start new file
            int colonPos = line.indexOf(':', 9); // Find second colon
            if (colonPos > 0) {
                currentFilename = line.substring(9, colonPos);
                currentContent = line.substring(colonPos + 1);
                inFileContent = true;
                Serial.printf("Starting file: %s\n", currentFilename.c_str());
            }
        }
        else if (line == ":ENDFILE") {
            // End of current file
            if (currentFilename.length() > 0) {
                if (saveUIFile(currentFilename, currentContent)) {
                    filesProcessed++;
                }
                currentFilename = "";
                currentContent = "";
                inFileContent = false;
            }
        }
        else if (inFileContent && line.length() > 0) {
            // Add line to current file content
            if (currentContent.length() > 0) {
                currentContent += "\n";
            }
            currentContent += line;
        }
    }
    
    // Save last file if we have one
    if (currentFilename.length() > 0 && currentContent.length() > 0) {
        if (saveUIFile(currentFilename, currentContent)) {
            filesProcessed++;
        }
    }
    
    updateFile.close();
    
    Serial.printf("UI update completed successfully - %d files processed\n", filesProcessed);
    return filesProcessed > 0;
}
