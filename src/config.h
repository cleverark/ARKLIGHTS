#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <FastLED.h>

// Debug flag - set to false to disable all debug Serial output for release builds
#define DEBUG_ENABLED false

// Configuration for XIAO ESP32S3
#define HEADLIGHT_PIN 2
#define TAILLIGHT_PIN 3
#define HEADLIGHT_CLOCK_PIN 5  // For APA102/LPD8806
#define TAILLIGHT_CLOCK_PIN 4  // For APA102/LPD8806
#define DEFAULT_BRIGHTNESS 128

// MPU6050 Motion Control Settings
#define MPU_SDA_PIN 5
#define MPU_SCL_PIN 6

// WiFi AP Configuration
#define AP_CHANNEL 1
#define MAX_CONNECTIONS 4

// Effect IDs
#define FX_SOLID 0
#define FX_BREATH 1
#define FX_RAINBOW 2
#define FX_CHASE 3
#define FX_BLINK_RAINBOW 4
#define FX_TWINKLE 5
#define FX_FIRE 6
#define FX_METEOR 7
#define FX_WAVE 8
#define FX_COMET 9
#define FX_CANDLE 10
#define FX_STATIC_RAINBOW 11
#define FX_KNIGHT_RIDER 12
#define FX_POLICE 13
#define FX_STROBE 14
#define FX_LARSON_SCANNER 15
#define FX_COLOR_WIPE 16
#define FX_THEATER_CHASE 17
#define FX_RUNNING_LIGHTS 18
#define FX_COLOR_SWEEP 19

// Preset IDs
#define PRESET_STANDARD 0
#define PRESET_NIGHT 1
#define PRESET_PARTY 2
#define PRESET_STEALTH 3

// Startup Sequence IDs
#define STARTUP_NONE 0
#define STARTUP_POWER_ON 1
#define STARTUP_SCAN 2
#define STARTUP_WAVE 3
#define STARTUP_RACE 4
#define STARTUP_CUSTOM 5

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

#endif // CONFIG_H

