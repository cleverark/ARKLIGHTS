#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

// Effect function declarations
// Original effect functions
void effectBreath(CRGB* leds, uint8_t numLeds, CRGB color);
void effectRainbow(CRGB* leds, uint8_t numLeds);
void effectChase(CRGB* leds, uint8_t numLeds, CRGB color);
void effectBlinkRainbow(CRGB* leds, uint8_t numLeds);
void effectTwinkle(CRGB* leds, uint8_t numLeds, CRGB color);
void effectFire(CRGB* leds, uint8_t numLeds);
void effectMeteor(CRGB* leds, uint8_t numLeds, CRGB color);
void effectWave(CRGB* leds, uint8_t numLeds, CRGB color);
void effectComet(CRGB* leds, uint8_t numLeds, CRGB color);
void effectCandle(CRGB* leds, uint8_t numLeds);
void effectStaticRainbow(CRGB* leds, uint8_t numLeds);
void effectKnightRider(CRGB* leds, uint8_t numLeds, CRGB color);
void effectPolice(CRGB* leds, uint8_t numLeds);
void effectStrobe(CRGB* leds, uint8_t numLeds, CRGB color);
void effectLarsonScanner(CRGB* leds, uint8_t numLeds, CRGB color);
void effectColorWipe(CRGB* leds, uint8_t numLeds, CRGB color);
void effectTheaterChase(CRGB* leds, uint8_t numLeds, CRGB color);
void effectRunningLights(CRGB* leds, uint8_t numLeds, CRGB color);
void effectColorSweep(CRGB* leds, uint8_t numLeds, CRGB color);

// Improved effect functions with consistent timing (step-based)
void effectBreathImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectRainbowImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectChaseImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectBlinkRainbowImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectTwinkleImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectFireImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectMeteorImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectWaveImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectCometImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectCandleImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectKnightRiderImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectPoliceImproved(CRGB* leds, uint8_t numLeds, uint16_t step);
void effectStrobeImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectLarsonScannerImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectColorWipeImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectTheaterChaseImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectRunningLightsImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);
void effectColorSweepImproved(CRGB* leds, uint8_t numLeds, CRGB color, uint16_t step);

#endif // EFFECTS_H

