# Code Organization Guide

## Overview

As your codebase grows, organizing it into multiple files makes it more maintainable. In PlatformIO/Arduino, all `.cpp` files in the `src/` directory are automatically compiled and linked together.

## File Structure

Here's the recommended structure:

```
src/
├── main.cpp              # Main setup() and loop() functions
├── config.h              # Configuration constants, pin definitions, macros
├── globals.h             # Global variable declarations (extern)
├── effects.h / effects.cpp        # LED effects (breath, rainbow, chase, etc.)
├── motion_control.h / motion_control.cpp  # IMU, blinkers, park mode, braking
├── web_server.h / web_server.cpp  # Web server, API handlers
├── led_manager.h / led_manager.cpp        # LED initialization, color order
├── storage.h / storage.cpp        # Filesystem, NVS, settings save/load
├── communication.h / communication.cpp    # ESPNow, BLE, WiFi
└── startup.h / startup.cpp        # Startup sequences
```

## How It Works

### Header Files (.h)
- Contains function declarations (prototypes)
- Contains struct/class definitions
- Contains `extern` declarations for global variables
- Includes necessary headers
- Protected by `#ifndef` guards to prevent multiple includes

### Implementation Files (.cpp)
- Contains function implementations
- Includes the corresponding `.h` file
- Includes other necessary headers
- Defines global variables (not `extern`)

### Example Pattern

**effects.h:**
```cpp
#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

// Function declarations
void effectBreath(CRGB* leds, uint8_t numLeds, CRGB color);
void effectRainbow(CRGB* leds, uint8_t numLeds);
void effectKnightRider(CRGB* leds, uint8_t numLeds, CRGB color);
// ... etc

#endif
```

**effects.cpp:**
```cpp
#include "effects.h"
#include "globals.h"  // For access to global variables if needed

void effectBreath(CRGB* leds, uint8_t numLeds, CRGB color) {
    // Implementation here
}

// ... other implementations
```

**main.cpp:**
```cpp
#include "config.h"
#include "globals.h"
#include "effects.h"
#include "motion_control.h"
// ... other includes

void setup() {
    // Your setup code
}
```

## Key Points

1. **Global Variables**: Declare them in `globals.h` with `extern`, define them in `main.cpp` or a dedicated `globals.cpp`
2. **Includes**: Each `.cpp` file should include its own `.h` file first, then other headers
3. **Forward Declarations**: Use forward declarations in headers when possible to reduce compile dependencies
4. **Compilation**: PlatformIO automatically compiles all `.cpp` files in `src/`

## Migration Strategy

1. Start with one module (e.g., effects)
2. Create `effects.h` with function declarations
3. Create `effects.cpp` with implementations
4. Update `main.cpp` to include `effects.h` instead of having the functions inline
5. Test that everything still compiles
6. Repeat for other modules

## Benefits

- **Organization**: Related code grouped together
- **Maintainability**: Easier to find and modify code
- **Readability**: Smaller files are easier to understand
- **Collaboration**: Multiple people can work on different modules
- **Testing**: Easier to test individual modules

