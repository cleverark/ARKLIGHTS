package com.example.arklights.viewmodel

import android.bluetooth.BluetoothDevice
import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.arklights.api.ArkLightsApiService
import com.example.arklights.bluetooth.BluetoothService
import com.example.arklights.data.*
import com.example.arklights.data.ConnectionState
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

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
    
    // OTA Update state
    private val _otaFileName = MutableStateFlow<String?>(null)
    val otaFileName: StateFlow<String?> = _otaFileName.asStateFlow()
    
    private val _otaFileBytes = MutableStateFlow<ByteArray?>(null)
    
    private val _otaUploading = MutableStateFlow(false)
    val otaUploading: StateFlow<Boolean> = _otaUploading.asStateFlow()
    
    private val _otaProgress = MutableStateFlow(0)
    val otaProgress: StateFlow<Int> = _otaProgress.asStateFlow()
    
    private val _otaStatus = MutableStateFlow("Ready")
    val otaStatus: StateFlow<String> = _otaStatus.asStateFlow()
    
    // Firmware update availability
    private val _updateAvailable = MutableStateFlow<FirmwareManifest?>(null)
    val updateAvailable: StateFlow<FirmwareManifest?> = _updateAvailable.asStateFlow()
    
    private val _isCheckingForUpdates = MutableStateFlow(false)
    val isCheckingForUpdates: StateFlow<Boolean> = _isCheckingForUpdates.asStateFlow()
    
    private val _isDownloadingFirmware = MutableStateFlow(false)
    val isDownloadingFirmware: StateFlow<Boolean> = _isDownloadingFirmware.asStateFlow()
    
    private val _downloadProgress = MutableStateFlow(0)
    val downloadProgress: StateFlow<Int> = _downloadProgress.asStateFlow()
    
    private var firmwareUpdateManager: FirmwareUpdateManager? = null
    
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
            
            // Check if current firmware version matches the available update and clear banner
            val currentFirmwareVersion = forcedStatus.firmware_version
            val availableUpdate = _updateAvailable.value
            if (availableUpdate != null && currentFirmwareVersion.isNotEmpty()) {
                val manager = firmwareUpdateManager
                if (manager != null && !manager.isUpdateAvailable(currentFirmwareVersion, availableUpdate.latest_version)) {
                    // Current version is same or newer, clear the update banner
                    _updateAvailable.value = null
                }
            }
            
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
        // Strip # prefix if present - device expects color without #
        val cleanColor = color.removePrefix("#")
        apiService.setHeadlightColor(cleanColor)
        refreshStatus()
    }

    suspend fun setHeadlightBackgroundEnabled(enabled: Boolean) {
        apiService.setHeadlightBackgroundEnabled(enabled)
        refreshStatus()
    }

    suspend fun setHeadlightBackgroundColor(color: String) {
        val cleanColor = color.removePrefix("#")
        apiService.setHeadlightBackgroundColor(cleanColor)
        refreshStatus()
    }
    
    suspend fun setTaillightColor(color: String) {
        // Strip # prefix if present - device expects color without #
        val cleanColor = color.removePrefix("#")
        apiService.setTaillightColor(cleanColor)
        refreshStatus()
    }

    suspend fun setTaillightBackgroundEnabled(enabled: Boolean) {
        apiService.setTaillightBackgroundEnabled(enabled)
        refreshStatus()
    }

    suspend fun setTaillightBackgroundColor(color: String) {
        val cleanColor = color.removePrefix("#")
        apiService.setTaillightBackgroundColor(cleanColor)
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
    
    // ============================================
    // OTA UPDATE METHODS
    // ============================================
    
    fun setOtaFile(fileName: String, fileBytes: ByteArray) {
        _otaFileName.value = fileName
        _otaFileBytes.value = fileBytes
        _otaStatus.value = "Ready"
        _otaProgress.value = 0
    }
    
    fun clearOtaFile() {
        _otaFileName.value = null
        _otaFileBytes.value = null
        _otaStatus.value = "Ready"
        _otaProgress.value = 0
    }
    
    fun cancelOtaUpload() {
        _otaUploading.value = false
        _otaStatus.value = "Cancelled"
        _otaProgress.value = 0
    }
    
    // Store context for network binding
    private var appContext: android.content.Context? = null
    
    /**
     * Set the application context for network operations
     */
    fun setAppContext(context: android.content.Context) {
        appContext = context.applicationContext
    }
    
    /**
     * Get WiFi network for binding connections
     */
    private fun getWifiNetwork(): android.net.Network? {
        val ctx = appContext ?: return null
        val connectivityManager = ctx.getSystemService(android.content.Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
        return connectivityManager.allNetworks.find { network ->
            val capabilities = connectivityManager.getNetworkCapabilities(network)
            capabilities?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) == true
        }
    }
    
    /**
     * Check if the device appears to be on the ArkLights WiFi network.
     * This is a heuristic check - we try to reach the device's status endpoint.
     */
    suspend fun checkDeviceWifiConnection(): Boolean = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
        try {
            val url = java.net.URL("http://192.168.4.1/api/status")
            val wifiNetwork = getWifiNetwork()
            
            android.util.Log.d("OTA", "Checking device connection, WiFi network: ${wifiNetwork != null}")
            
            // Use WiFi network if available to avoid routing through mobile data
            val connection = if (wifiNetwork != null) {
                android.util.Log.d("OTA", "Opening connection via WiFi network binding")
                wifiNetwork.openConnection(url) as java.net.HttpURLConnection
            } else {
                android.util.Log.d("OTA", "Opening connection via default network (no WiFi binding)")
                url.openConnection() as java.net.HttpURLConnection
            }
            
            connection.connectTimeout = 5000
            connection.readTimeout = 5000
            connection.requestMethod = "GET"
            
            android.util.Log.d("OTA", "Connecting to device...")
            val responseCode = connection.responseCode
            connection.disconnect()
            android.util.Log.d("OTA", "Device check response: $responseCode")
            responseCode == 200
        } catch (e: java.net.SocketTimeoutException) {
            android.util.Log.e("OTA", "Device check failed: Connection timeout")
            false
        } catch (e: java.net.ConnectException) {
            android.util.Log.e("OTA", "Device check failed: Connection refused - ${e.message}")
            false
        } catch (e: java.io.IOException) {
            android.util.Log.e("OTA", "Device check failed: IO error - ${e.javaClass.simpleName}: ${e.message}")
            e.printStackTrace()
            false
        } catch (e: Exception) {
            android.util.Log.e("OTA", "Device check failed: ${e.javaClass.simpleName}: ${e.message}")
            e.printStackTrace()
            false
        }
    }
    
    suspend fun uploadFirmware(): Boolean {
        val fileBytes = _otaFileBytes.value
        val fileName = _otaFileName.value
        
        if (fileBytes == null || fileName == null) {
            _otaStatus.value = "No firmware file selected"
            return false
        }
        
        _otaUploading.value = true
        _otaStatus.value = "Checking device connection..."
        _otaProgress.value = 0
        
        // Check if we can reach the device first
        val canReachDevice = checkDeviceWifiConnection()
        if (!canReachDevice) {
            _otaStatus.value = "Cannot reach device. Connect to device WiFi first."
            _otaUploading.value = false
            return false
        }
        
        _otaStatus.value = "Uploading..."
        
        return try {
            val result = apiService.uploadFirmwareViaHttp(
                fileBytes = fileBytes,
                fileName = fileName,
                onProgress = { progress ->
                    _otaProgress.value = progress
                    _otaStatus.value = when {
                        progress < 100 -> "Uploading... ${progress}%"
                        else -> "Installing..."
                    }
                },
                context = appContext
            )
            
            if (result) {
                _otaStatus.value = "Complete! Device restarting..."
                _otaProgress.value = 100
                // Clear file after successful upload
                clearOtaFile()
                // Clear update available banner since we just updated
                _updateAvailable.value = null
            } else {
                // Get error from API service
                val error = apiService.lastError.value ?: "Upload failed"
                _otaStatus.value = error
            }
            result
        } catch (e: Exception) {
            _otaStatus.value = "Error: ${e.message}"
            false
        } finally {
            _otaUploading.value = false
        }
    }
    
    // ============================================
    // AUTOMATIC FIRMWARE UPDATE CHECKING
    // ============================================
    
    /**
     * Initialize the firmware update manager with context
     */
    fun initFirmwareUpdateManager(context: Context) {
        if (firmwareUpdateManager == null) {
            firmwareUpdateManager = FirmwareUpdateManager(context)
        }
    }
    
    /**
     * Check if we should auto-check for updates (once per week)
     */
    fun shouldAutoCheckForUpdates(): Boolean {
        return firmwareUpdateManager?.shouldAutoCheck() ?: false
    }
    
    /**
     * Check for firmware updates from remote server
     */
    suspend fun checkForUpdates() {
        val manager = firmwareUpdateManager ?: return
        val currentVersion = _deviceStatus.value?.let { 
            // Use firmware_version from device, with fallback
            it.firmware_version.takeIf { v -> v.isNotEmpty() } 
                ?: "v0.0.0"  // Default fallback if not set
        } ?: "v0.0.0"
        
        _isCheckingForUpdates.value = true
        
        try {
            val manifest = manager.checkForUpdates()
            
            if (manifest != null && manager.isUpdateAvailable(currentVersion, manifest.latest_version)) {
                // Check if user dismissed this version
                if (!manager.isVersionDismissed(manifest.latest_version)) {
                    _updateAvailable.value = manifest
                }
            } else {
                _updateAvailable.value = null
            }
        } catch (e: Exception) {
            // Silent fail - don't bother user with update check errors
        } finally {
            _isCheckingForUpdates.value = false
        }
    }
    
    /**
     * Dismiss the update notification for current version
     */
    fun dismissUpdate() {
        val version = _updateAvailable.value?.latest_version ?: return
        firmwareUpdateManager?.dismissVersion(version)
        _updateAvailable.value = null
    }
    
    /**
     * Download firmware from remote server and prepare for upload
     */
    suspend fun downloadAndPrepareUpdate(): Boolean {
        val manager = firmwareUpdateManager ?: return false
        val manifest = _updateAvailable.value ?: return false
        
        _isDownloadingFirmware.value = true
        _downloadProgress.value = 0
        _otaStatus.value = "Downloading update..."
        
        return try {
            val firmwareFile = manager.downloadFirmware(
                url = manifest.download_url,
                onProgress = { progress ->
                    _downloadProgress.value = progress
                }
            )
            
            if (firmwareFile != null && firmwareFile.exists()) {
                // Read file bytes and set as OTA file
                val bytes = firmwareFile.readBytes()
                val fileName = "arklights-${manifest.latest_version}.bin"
                setOtaFile(fileName, bytes)
                _otaStatus.value = "Download complete. Connect to device WiFi to install."
                true
            } else {
                _otaStatus.value = "Download failed"
                false
            }
        } catch (e: Exception) {
            _otaStatus.value = "Download error: ${e.message}"
            false
        } finally {
            _isDownloadingFirmware.value = false
        }
    }
    
    /**
     * One-click update: download and upload in sequence
     */
    suspend fun performAutomaticUpdate(): Boolean {
        // Step 1: Download firmware
        if (!downloadAndPrepareUpdate()) {
            return false
        }
        
        // Step 2: Upload to device (user must be on device WiFi)
        return uploadFirmware()
    }
}
