package com.example.arklights.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.arklights.api.ArkLightsApiService
import com.example.arklights.bluetooth.BluetoothService
import com.example.arklights.data.*
import com.example.arklights.data.ConnectionState
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import android.bluetooth.BluetoothDevice

class ArkLightsViewModel(
    private val bluetoothService: BluetoothService,
    private val apiService: ArkLightsApiService,
    private val deviceStore: DeviceStore
) : ViewModel() {
    
    // Connection state
    val connectionState = bluetoothService.connectionState
    val discoveredDevices = bluetoothService.discoveredDevices
    val errorMessage = bluetoothService.errorMessage

    val savedDevices: StateFlow<List<SavedDevice>> = deviceStore.savedDevices
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())
    val lastDeviceAddress: StateFlow<String?> = deviceStore.lastDeviceAddress
        .stateIn(viewModelScope, SharingStarted.Eagerly, null)
    
    // API state
    val isLoading = apiService.isLoading
    val lastError = apiService.lastError
    
    // Device status
    private val _deviceStatus = MutableStateFlow<LEDStatus?>(null)
    val deviceStatus: StateFlow<LEDStatus?> = _deviceStatus.asStateFlow()
    private var cachedPresets: List<PresetInfo> = emptyList()
    private var calibrationRefreshJob: Job? = null
    private var calibrationForcedUntilMs: Long = 0
    
    // UI state
    private val _currentPage = MutableStateFlow("main")
    val currentPage: StateFlow<String> = _currentPage.asStateFlow()
    
    private val _selectedDevice = MutableStateFlow<BluetoothDevice?>(null)
    val selectedDevice: StateFlow<BluetoothDevice?> = _selectedDevice.asStateFlow()
    
    init {
        // Auto-refresh status when connected
        viewModelScope.launch {
            connectionState.collect { state ->
                if (state == ConnectionState.CONNECTED) {
                    val connectedDevice = bluetoothService.getCurrentDevice()
                    if (connectedDevice != null) {
                        val name = try {
                            connectedDevice.name ?: "ArkLights Device"
                        } catch (e: SecurityException) {
                            "ArkLights Device"
                        }
                        deviceStore.saveDevice(name, connectedDevice.address)
                    }
                    refreshStatus()
                    // Start periodic status updates
                    startStatusUpdates()
                }
            }
        }
    }
    
    fun setCurrentPage(page: String) {
        _currentPage.value = page
    }
    
    fun selectDevice(device: BluetoothDevice) {
        _selectedDevice.value = device
    }
    
    suspend fun startDiscovery(): Boolean {
        return bluetoothService.startDiscovery()
    }
    
    suspend fun stopDiscovery(): Boolean {
        return bluetoothService.stopDiscovery()
    }
    
    suspend fun getPairedDevices(): List<BluetoothDevice> {
        return bluetoothService.getPairedDevices()
    }
    
    suspend fun connectToDevice(device: BluetoothDevice): Boolean {
        _selectedDevice.value = device
        return bluetoothService.connectToDevice(device)
    }

    suspend fun connectToSavedDevice(address: String): Boolean {
        val device = bluetoothService.getRemoteDevice(address) ?: return false
        return connectToDevice(device)
    }

    suspend fun removeSavedDevice(address: String) {
        deviceStore.removeDevice(address)
    }
    
    suspend fun disconnect(): Boolean {
        return bluetoothService.disconnect()
    }
    
    suspend fun refreshStatus() {
        val status = apiService.getStatus()
        if (status != null) {
            val resolvedPresets = if (status.presets.isNotEmpty()) {
                cachedPresets = status.presets
                status.presets
            } else {
                cachedPresets
            }
            val resolvedPresetCount = if (status.presetCount > 0) {
                status.presetCount
            } else {
                resolvedPresets.size
            }
            val now = System.currentTimeMillis()
            val shouldForceCalibration = now < calibrationForcedUntilMs && !status.calibration_mode
            val forcedStatus = if (shouldForceCalibration) {
                status.copy(
                    calibration_mode = true,
                    calibration_step = 0,
                    calibration_complete = false
                )
            } else {
                status
            }

            _deviceStatus.value = forcedStatus.copy(
                presets = resolvedPresets,
                presetCount = resolvedPresetCount
            )
            if (forcedStatus.calibration_mode) {
                startCalibrationRefreshIfNeeded()
            } else {
                stopCalibrationRefresh()
            }
        }
    }

    private fun startCalibrationRefreshIfNeeded() {
        if (calibrationRefreshJob?.isActive == true) return
        calibrationRefreshJob = viewModelScope.launch {
            while (true) {
                refreshStatus()
                kotlinx.coroutines.delay(2000)
                val status = _deviceStatus.value
                if (status == null || !status.calibration_mode) break
            }
        }
    }

    private fun stopCalibrationRefresh() {
        calibrationRefreshJob?.cancel()
        calibrationRefreshJob = null
    }
    
    private fun startStatusUpdates() {
        viewModelScope.launch {
            while (connectionState.value == ConnectionState.CONNECTED) {
                refreshStatus()
                kotlinx.coroutines.delay(5000) // Update every 5 seconds
            }
        }
    }
    
    // LED Control Methods
    suspend fun setPreset(preset: Int) {
        apiService.setPreset(preset)
        refreshStatus()
    }
    
    suspend fun setBrightness(brightness: Int) {
        apiService.setBrightness(brightness)
        refreshStatus()
    }
    
    suspend fun setEffectSpeed(speed: Int) {
        apiService.setEffectSpeed(speed)
        refreshStatus()
    }
    
    suspend fun setHeadlightColor(color: String) {
        apiService.setHeadlightColor(color)
        refreshStatus()
    }

    suspend fun setHeadlightBackgroundEnabled(enabled: Boolean) {
        apiService.setHeadlightBackgroundEnabled(enabled)
        refreshStatus()
    }

    suspend fun setHeadlightBackgroundColor(color: String) {
        apiService.setHeadlightBackgroundColor(color)
        refreshStatus()
    }
    
    suspend fun setTaillightColor(color: String) {
        apiService.setTaillightColor(color)
        refreshStatus()
    }

    suspend fun setTaillightBackgroundEnabled(enabled: Boolean) {
        apiService.setTaillightBackgroundEnabled(enabled)
        refreshStatus()
    }

    suspend fun setTaillightBackgroundColor(color: String) {
        apiService.setTaillightBackgroundColor(color)
        refreshStatus()
    }
    
    suspend fun setHeadlightEffect(effect: Int) {
        apiService.setHeadlightEffect(effect)
        refreshStatus()
    }
    
    suspend fun setTaillightEffect(effect: Int) {
        apiService.setTaillightEffect(effect)
        refreshStatus()
    }

    suspend fun setHeadlightMode(mode: Int) {
        apiService.setHeadlightMode(mode)
        refreshStatus()
    }
    
    // Motion Control Methods
    suspend fun setMotionEnabled(enabled: Boolean) {
        apiService.setMotionEnabled(enabled)
        refreshStatus()
    }

    suspend fun setDirectionBasedLighting(enabled: Boolean) {
        apiService.setDirectionBasedLighting(enabled)
        refreshStatus()
    }

    suspend fun setForwardAccelThreshold(threshold: Double) {
        apiService.setForwardAccelThreshold(threshold)
        refreshStatus()
    }
    
    suspend fun setBlinkerEnabled(enabled: Boolean) {
        apiService.setBlinkerEnabled(enabled)
        refreshStatus()
    }
    
    suspend fun setParkModeEnabled(enabled: Boolean) {
        apiService.setParkModeEnabled(enabled)
        refreshStatus()
    }
    
    suspend fun setImpactDetectionEnabled(enabled: Boolean) {
        apiService.setImpactDetectionEnabled(enabled)
        refreshStatus()
    }

    suspend fun setBrakingEnabled(enabled: Boolean) {
        apiService.setBrakingEnabled(enabled)
        refreshStatus()
    }

    suspend fun setBrakingThreshold(threshold: Double) {
        apiService.setBrakingThreshold(threshold)
        refreshStatus()
    }

    suspend fun setBrakingEffect(effect: Int) {
        apiService.setBrakingEffect(effect)
        refreshStatus()
    }

    suspend fun setBrakingBrightness(brightness: Int) {
        apiService.setBrakingBrightness(brightness)
        refreshStatus()
    }

    suspend fun setRgbwWhiteMode(mode: Int) {
        apiService.setRgbwWhiteMode(mode)
        refreshStatus()
    }

    suspend fun startOtaViaBle(url: String): Boolean {
        return apiService.startOtaViaBle(url)
    }

    suspend fun getOtaStatus(): OtaStatus? {
        return apiService.getOtaStatus()
    }

    suspend fun setWhiteLEDsEnabled(enabled: Boolean) {
        apiService.setWhiteLEDsEnabled(enabled)
        refreshStatus()
    }

    suspend fun setManualBlinker(direction: String) {
        apiService.setManualBlinker(direction)
        refreshStatus()
    }

    suspend fun setManualBrake(enabled: Boolean) {
        apiService.setManualBrake(enabled)
        refreshStatus()
    }
    
    suspend fun setMotionSensitivity(sensitivity: Double) {
        apiService.setMotionSensitivity(sensitivity)
        refreshStatus()
    }
    
    suspend fun setBlinkerDelay(delay: Int) {
        apiService.setBlinkerDelay(delay)
        refreshStatus()
    }
    
    suspend fun setBlinkerTimeout(timeout: Int) {
        apiService.setBlinkerTimeout(timeout)
        refreshStatus()
    }
    
    suspend fun setParkStationaryTime(time: Int) {
        apiService.setParkStationaryTime(time)
        refreshStatus()
    }
    
    suspend fun setParkAccelNoiseThreshold(threshold: Double) {
        apiService.setParkAccelNoiseThreshold(threshold)
        refreshStatus()
    }
    
    suspend fun setParkGyroNoiseThreshold(threshold: Double) {
        apiService.setParkGyroNoiseThreshold(threshold)
        refreshStatus()
    }
    
    suspend fun setImpactThreshold(threshold: Double) {
        apiService.setImpactThreshold(threshold)
        refreshStatus()
    }
    
    // Park Mode Settings
    suspend fun setParkEffect(effect: Int) {
        apiService.setParkEffect(effect)
        refreshStatus()
    }
    
    suspend fun setParkEffectSpeed(speed: Int) {
        apiService.setParkEffectSpeed(speed)
        refreshStatus()
    }
    
    suspend fun setParkBrightness(brightness: Int) {
        apiService.setParkBrightness(brightness)
        refreshStatus()
    }
    
    suspend fun setParkHeadlightColor(r: Int, g: Int, b: Int) {
        apiService.setParkHeadlightColor(r, g, b)
        refreshStatus()
    }
    
    suspend fun setParkTaillightColor(r: Int, g: Int, b: Int) {
        apiService.setParkTaillightColor(r, g, b)
        refreshStatus()
    }
    
    // Calibration Methods
    suspend fun startCalibration() {
        apiService.startCalibration()
        val current = _deviceStatus.value
        if (current != null) {
            _deviceStatus.value = current.copy(
                calibration_mode = true,
                calibration_step = 0,
                calibration_complete = false
            )
        }
        calibrationForcedUntilMs = System.currentTimeMillis() + 6000
        startCalibrationRefreshIfNeeded()
        refreshStatus()
    }
    
    suspend fun nextCalibrationStep() {
        apiService.nextCalibrationStep()
        startCalibrationRefreshIfNeeded()
        refreshStatus()
    }
    
    suspend fun resetCalibration() {
        apiService.resetCalibration()
        stopCalibrationRefresh()
        refreshStatus()
    }
    
    // Test Methods
    suspend fun testStartupSequence() {
        apiService.testStartupSequence()
    }
    
    suspend fun testParkMode() {
        apiService.testParkMode()
    }
    
    suspend fun testLEDs() {
        apiService.testLEDs()
    }
    
    // Startup Sequence Settings
    suspend fun setStartupSequence(sequence: Int) {
        apiService.setStartupSequence(sequence)
        refreshStatus()
    }
    
    suspend fun setStartupDuration(duration: Int) {
        apiService.setStartupDuration(duration)
        refreshStatus()
    }
    
    // WiFi Configuration
    suspend fun setAPName(name: String) {
        apiService.setAPName(name)
        refreshStatus()
    }
    
    suspend fun setAPPassword(password: String) {
        apiService.setAPPassword(password)
        refreshStatus()
    }
    
    suspend fun applyWiFiConfig(name: String, password: String) {
        apiService.applyWiFiConfig(name, password)
        refreshStatus()
    }

    suspend fun restoreDefaults() {
        apiService.restoreDefaults()
        // Device will restart and disconnect - no need to refresh
    }
    
    // ESPNow Configuration
    suspend fun setESPNowEnabled(enabled: Boolean) {
        apiService.setESPNowEnabled(enabled)
        refreshStatus()
    }
    
    suspend fun setESPNowSync(enabled: Boolean) {
        apiService.setESPNowSync(enabled)
        refreshStatus()
    }
    
    suspend fun setESPNowChannel(channel: Int) {
        apiService.setESPNowChannel(channel)
        refreshStatus()
    }
    
    // Group Management
    suspend fun setDeviceName(name: String) {
        apiService.setDeviceName(name)
        refreshStatus()
    }
    
    suspend fun createGroup(code: String?) {
        apiService.createGroup(code)
        refreshStatus()
    }
    
    suspend fun joinGroup(code: String) {
        apiService.joinGroup(code)
        refreshStatus()
    }

    suspend fun scanJoinGroup() {
        apiService.scanJoinGroup()
        refreshStatus()
    }
    
    suspend fun leaveGroup() {
        apiService.leaveGroup()
        refreshStatus()
    }
    
    suspend fun allowGroupJoin() {
        apiService.allowGroupJoin()
        refreshStatus()
    }
    
    suspend fun blockGroupJoin() {
        apiService.blockGroupJoin()
        refreshStatus()
    }

    suspend fun savePreset(name: String) {
        apiService.savePreset(name)
        refreshStatus()
    }

    suspend fun updatePreset(index: Int, name: String) {
        apiService.updatePreset(index, name)
        refreshStatus()
    }

    suspend fun deletePreset(index: Int) {
        apiService.deletePreset(index)
        refreshStatus()
    }
    
    // LED Configuration
    suspend fun updateLEDConfig(config: LEDConfigRequest) {
        apiService.updateLEDConfig(config)
        refreshStatus()
    }
    
    // Utility methods
    fun hexToRgb(hex: String): Triple<Int, Int, Int> {
        val cleanHex = hex.replace("#", "")
        val r = cleanHex.substring(0, 2).toInt(16)
        val g = cleanHex.substring(2, 4).toInt(16)
        val b = cleanHex.substring(4, 6).toInt(16)
        return Triple(r, g, b)
    }
    
    fun rgbToHex(r: Int, g: Int, b: Int): String {
        return String.format("#%02X%02X%02X", r, g, b)
    }
}
