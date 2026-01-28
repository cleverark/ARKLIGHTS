package com.example.arklights.data

import kotlinx.serialization.Serializable

// LED Effect IDs (matching the C++ definitions)
object LEDEffects {
    const val SOLID = 0
    const val BREATH = 1
    const val RAINBOW = 2
    const val CHASE = 3
    const val BLINK_RAINBOW = 4
    const val TWINKLE = 5
    const val FIRE = 6
    const val METEOR = 7
    const val WAVE = 8
    const val COMET = 9
    const val CANDLE = 10
    const val STATIC_RAINBOW = 11
    const val KNIGHT_RIDER = 12
    const val POLICE = 13
    const val STROBE = 14
    const val LARSON_SCANNER = 15
    const val COLOR_WIPE = 16
    const val RAINBOW_WIPE = 23
    const val THEATER_CHASE = 17
    const val RUNNING_LIGHTS = 18
    const val COLOR_SWEEP = 19
    const val RAINBOW_KNIGHT_RIDER = 20
    const val DUAL_KNIGHT_RIDER = 21
    const val DUAL_RAINBOW_KNIGHT_RIDER = 22
    
    val effectNames = mapOf(
        SOLID to "Solid",
        BREATH to "Breath",
        RAINBOW to "Rainbow",
        CHASE to "Chase",
        BLINK_RAINBOW to "Blink Rainbow",
        TWINKLE to "Twinkle",
        FIRE to "Fire",
        METEOR to "Meteor",
        WAVE to "Wave",
        COMET to "Comet",
        CANDLE to "Candle",
        STATIC_RAINBOW to "Static Rainbow",
        KNIGHT_RIDER to "Knight Rider",
        POLICE to "Police",
        STROBE to "Strobe",
        LARSON_SCANNER to "Larson Scanner",
        COLOR_WIPE to "Color Wipe",
        RAINBOW_WIPE to "Rainbow Wipe",
        THEATER_CHASE to "Theater Chase",
        RUNNING_LIGHTS to "Running Lights",
        COLOR_SWEEP to "Color Sweep",
        RAINBOW_KNIGHT_RIDER to "Rainbow Knight Rider",
        DUAL_KNIGHT_RIDER to "Dual Knight Rider",
        DUAL_RAINBOW_KNIGHT_RIDER to "Dual Rainbow Knight Rider"
    )
}

// Preset IDs
object Presets {
    const val STANDARD = 0
    const val NIGHT = 1
    const val PARTY = 2
    const val STEALTH = 3
    
    val presetNames = mapOf(
        STANDARD to "Standard",
        NIGHT to "Night",
        PARTY to "Party",
        STEALTH to "Stealth"
    )
}

// Startup Sequence IDs
object StartupSequences {
    const val NONE = 0
    const val POWER_ON = 1
    const val SCAN = 2
    const val WAVE = 3
    const val RACE = 4
    const val CUSTOM = 5
    
    val sequenceNames = mapOf(
        NONE to "None",
        POWER_ON to "Power On",
        SCAN to "Scanner",
        WAVE to "Wave",
        RACE to "Race",
        CUSTOM to "Custom"
    )
}

// LED Types
object LEDTypes {
    const val SK6812_RGBW = 0
    const val SK6812_RGB = 1
    const val WS2812B_RGB = 2
    const val APA102_RGB = 3
    const val LPD8806_RGB = 4
    
    val typeNames = mapOf(
        SK6812_RGBW to "SK6812 (RGBW)",
        SK6812_RGB to "SK6812 (RGB)",
        WS2812B_RGB to "WS2812B (RGB)",
        APA102_RGB to "APA102 (RGB)",
        LPD8806_RGB to "LPD8806 (RGB)"
    )
}

// Color Orders
object ColorOrders {
    const val RGB = 0
    const val GRB = 1
    const val BGR = 2
    
    val orderNames = mapOf(
        RGB to "RGB",
        GRB to "GRB",
        BGR to "BGR"
    )
}

// API Request Models
@Serializable
data class LEDControlRequest(
    val preset: Int? = null,
    val brightness: Int? = null,
    val effectSpeed: Int? = null,
    val headlightColor: String? = null,
    val taillightColor: String? = null,
    val headlightBackgroundEnabled: Boolean? = null,
    val taillightBackgroundEnabled: Boolean? = null,
    val headlightBackgroundColor: String? = null,
    val taillightBackgroundColor: String? = null,
    val headlight_mode: Int? = null,
    val headlightEffect: Int? = null,
    val taillightEffect: Int? = null,
    val direction_based_lighting: Boolean? = null,
    val forward_accel_threshold: Double? = null,
    val startup_sequence: Int? = null,
    val startup_duration: Int? = null,
    val testStartup: Boolean? = null,
    val testParkMode: Boolean? = null,
    val testLEDs: Boolean? = null,
    
    // Motion Control
    val motion_enabled: Boolean? = null,
    val blinker_enabled: Boolean? = null,
    val park_mode_enabled: Boolean? = null,
    val impact_detection_enabled: Boolean? = null,
    val motion_sensitivity: Double? = null,
    val blinker_delay: Int? = null,
    val blinker_timeout: Int? = null,
    val park_stationary_time: Int? = null,
    val park_accel_noise_threshold: Double? = null,
    val park_gyro_noise_threshold: Double? = null,
    val impact_threshold: Double? = null,
    val braking_enabled: Boolean? = null,
    val braking_threshold: Double? = null,
    val braking_effect: Int? = null,
    val braking_brightness: Int? = null,
    val white_leds_enabled: Boolean? = null,
    val rgbw_white_mode: Int? = null,
    val manualBlinker: String? = null,
    val manualBrake: Boolean? = null,
    
    // Park Mode Settings
    val park_effect: Int? = null,
    val park_effect_speed: Int? = null,
    val park_brightness: Int? = null,
    val park_headlight_color_r: Int? = null,
    val park_headlight_color_g: Int? = null,
    val park_headlight_color_b: Int? = null,
    val park_taillight_color_r: Int? = null,
    val park_taillight_color_g: Int? = null,
    val park_taillight_color_b: Int? = null,
    
    // Calibration
    val startCalibration: Boolean? = null,
    val nextCalibrationStep: Boolean? = null,
    val resetCalibration: Boolean? = null,
    
    // WiFi Configuration
    val apName: String? = null,
    val apPassword: String? = null,
    val restart: Boolean? = null,
    
    // ESPNow Configuration
    val enableESPNow: Boolean? = null,
    val useESPNowSync: Boolean? = null,
    val espNowChannel: Int? = null,
    
    // Group Management
    val deviceName: String? = null,
    val groupAction: String? = null,
    val groupCode: String? = null,
    val presetAction: String? = null,
    val presetName: String? = null,
    val presetIndex: Int? = null
)

@Serializable
data class LEDConfigRequest(
    val headlightLedCount: Int,
    val taillightLedCount: Int,
    val headlightLedType: Int,
    val taillightLedType: Int,
    val headlightColorOrder: Int,
    val taillightColorOrder: Int
)

@Serializable
data class OtaStatus(
    val ota_update_url: String = "",
    val ota_in_progress: Boolean = false,
    val ota_progress: Int = 0,
    val ota_status: String = "Ready",
    val ota_error: String? = null
)

// API Response Models
@Serializable
data class LEDStatus(
    val preset: Int = 0,
    val presetCount: Int = 0,
    val presets: List<PresetInfo> = emptyList(),
    val brightness: Int = 128,
    val effectSpeed: Int = 64,
    val startup_sequence: Int = 0,
    val startup_sequence_name: String = "None",
    val startup_duration: Int = 3000,
    val motion_enabled: Boolean = false,
    val blinker_enabled: Boolean = false,
    val park_mode_enabled: Boolean = false,
    val impact_detection_enabled: Boolean = false,
    val direction_based_lighting: Boolean = false,
    val headlight_mode: Int = 0,
    val forward_accel_threshold: Double = 0.3,
    val motion_sensitivity: Double = 1.0,
    val blinker_delay: Int = 300,
    val blinker_timeout: Int = 2000,
    val park_stationary_time: Int = 2000,
    val park_accel_noise_threshold: Double = 0.1,
    val park_gyro_noise_threshold: Double = 0.5,
    val impact_threshold: Double = 3.0,
    val braking_enabled: Boolean = false,
    val braking_active: Boolean = false,
    val braking_threshold: Double = -0.5,
    val braking_effect: Int = 0,
    val braking_brightness: Int = 255,
    val white_leds_enabled: Boolean = false,
    val rgbw_white_mode: Int = 0,
    val park_effect: Int = 0,
    val park_effect_speed: Int = 64,
    val park_brightness: Int = 128,
    val park_headlight_color_r: Int = 0,
    val park_headlight_color_g: Int = 0,
    val park_headlight_color_b: Int = 255,
    val park_taillight_color_r: Int = 0,
    val park_taillight_color_g: Int = 0,
    val park_taillight_color_b: Int = 255,
    val blinker_active: Boolean = false,
    val blinker_direction: Int = 0,
    val manual_blinker_active: Boolean = false,
    val manual_brake_active: Boolean = false,
    val park_mode_active: Boolean = false,
    val calibration_complete: Boolean = false,
    val calibration_mode: Boolean = false,
    val calibration_step: Int = 0,
    val apName: String = "ARKLIGHTS-AP",
    val apPassword: String = "float420",
    val headlightColor: String = "ffffff",
    val taillightColor: String = "ff0000",
    val headlightBackgroundEnabled: Boolean = false,
    val taillightBackgroundEnabled: Boolean = false,
    val headlightBackgroundColor: String = "000000",
    val taillightBackgroundColor: String = "000000",
    val headlightEffect: Int = 0,
    val taillightEffect: Int = 0,
    val headlightLedCount: Int = 20,
    val taillightLedCount: Int = 20,
    val headlightLedType: Int = 0,
    val taillightLedType: Int = 0,
    val headlightColorOrder: Int = 0,
    val taillightColorOrder: Int = 0,
    val enableESPNow: Boolean = false,
    val useESPNowSync: Boolean = false,
    val espNowChannel: Int = 1,
    val espNowStatus: String = "Initializing",
    val espNowPeerCount: Int = 0,
    val espNowLastSend: String = "Never",
    val groupCode: String = "",
    val isGroupMaster: Boolean = false,
    val hasGroupMaster: Boolean = false,
    val groupMasterMac: String = "",
    val groupMemberCount: Int = 0,
    val deviceName: String = "ArkLights Device",
    val ota_status: String = "Ready",
    val ota_progress: Int = 0,
    val ota_error: String? = null,
    val ota_in_progress: Boolean = false,
    val build_date: String = ""
)

@Serializable
data class PresetInfo(
    val name: String = ""
)

@Serializable
data class ApiResponse(
    val success: Boolean,
    val message: String? = null,
    val error: String? = null
)

// Bluetooth Device Model
data class BluetoothDevice(
    val name: String,
    val address: String,
    val isConnected: Boolean = false
)

// Connection State
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
}
