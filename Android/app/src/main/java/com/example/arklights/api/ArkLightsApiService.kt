package com.example.arklights.api

import com.example.arklights.bluetooth.BluetoothService
import com.example.arklights.data.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import kotlinx.serialization.encodeToString
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.json.Json

class ArkLightsApiService(private val bluetoothService: BluetoothService) {
    
    private val _isLoading = MutableStateFlow(false)
    val isLoading: StateFlow<Boolean> = _isLoading.asStateFlow()
    
    private val _lastError = MutableStateFlow<String?>(null)
    val lastError: StateFlow<String?> = _lastError.asStateFlow()

    private val json = Json {
        ignoreUnknownKeys = true
        explicitNulls = false
    }

    private fun extractHttpBody(response: String): String {
        val separator = "\r\n\r\n"
        val index = response.indexOf(separator)
        return if (index >= 0) {
            response.substring(index + separator.length)
        } else {
            response
        }
    }

    private fun isHttpSuccess(response: String?): Boolean {
        return response?.contains(" 200 ") == true || response?.startsWith("HTTP/1.1 200") == true
    }
    
    suspend fun getStatus(): LEDStatus? = withContext(Dispatchers.IO) {
        if (!bluetoothService.isConnected()) {
            _lastError.value = "Not connected to device"
            return@withContext null
        }
        
        _isLoading.value = true
        _lastError.value = null
        
        try {
            val statusJson = bluetoothService.requestStatus()
            if (statusJson != null) {
                json.decodeFromString<LEDStatus>(statusJson)
            } else {
                _lastError.value = "Failed to get response"
                null
            }
        } catch (e: Exception) {
            _lastError.value = "Error parsing response: ${e.message}"
            null
        } finally {
            _isLoading.value = false
        }
    }
    
    suspend fun sendControlRequest(request: LEDControlRequest): Boolean = withContext(Dispatchers.IO) {
        if (!bluetoothService.isConnected()) {
            _lastError.value = "Not connected to device"
            return@withContext false
        }
        
        _isLoading.value = true
        _lastError.value = null
        
        try {
            // Serialize the LED control request to JSON
            val jsonBody = json.encodeToString(request)
            println("Android: Sending LED control request: $jsonBody")
            
            if (bluetoothService.sendSettingsJson(jsonBody)) {
                true
            } else {
                _lastError.value = "Request failed"
                false
            }
        } catch (e: Exception) {
            _lastError.value = "Error sending request: ${e.message}"
            false
        } finally {
            _isLoading.value = false
        }
    }
    
    suspend fun updateLEDConfig(config: LEDConfigRequest): Boolean = withContext(Dispatchers.IO) {
        if (!bluetoothService.isConnected()) {
            _lastError.value = "Not connected to device"
            return@withContext false
        }
        
        _isLoading.value = true
        _lastError.value = null
        
        try {
            // Serialize the LED config request to JSON
            val jsonBody = json.encodeToString(config)
            println("Android: Sending LED config request: $jsonBody")
            
            if (bluetoothService.sendSettingsJson(jsonBody)) {
                true
            } else {
                _lastError.value = "LED config update failed"
                false
            }
        } catch (e: Exception) {
            _lastError.value = "Error updating LED config: ${e.message}"
            false
        } finally {
            _isLoading.value = false
        }
    }
    
    suspend fun testLEDs(): Boolean = withContext(Dispatchers.IO) {
        if (!bluetoothService.isConnected()) {
            _lastError.value = "Not connected to device"
            return@withContext false
        }
        
        _isLoading.value = true
        _lastError.value = null
        
        try {
            if (bluetoothService.sendSettingsJson("{\"testLEDs\":true}")) {
                true
            } else {
                _lastError.value = "LED test failed"
                false
            }
        } catch (e: Exception) {
            _lastError.value = "Error testing LEDs: ${e.message}"
            false
        } finally {
            _isLoading.value = false
        }
    }

    suspend fun startOtaViaBle(url: String): Boolean = withContext(Dispatchers.IO) {
        if (!bluetoothService.isConnected()) {
            _lastError.value = "Not connected to device"
            return@withContext false
        }

        _isLoading.value = true
        _lastError.value = null

        try {
            bluetoothService.startOta(url)
        } catch (e: Exception) {
            _lastError.value = "Error starting OTA: ${e.message}"
            false
        } finally {
            _isLoading.value = false
        }
    }

    suspend fun getOtaStatus(): OtaStatus? = withContext(Dispatchers.IO) {
        if (!bluetoothService.isConnected()) {
            _lastError.value = "Not connected to device"
            return@withContext null
        }

        _isLoading.value = true
        _lastError.value = null

        try {
            val statusJson = bluetoothService.requestOtaStatus()
            if (statusJson != null) {
                json.decodeFromString<OtaStatus>(statusJson)
            } else {
                _lastError.value = "Failed to get OTA status"
                null
            }
        } catch (e: Exception) {
            _lastError.value = "Error parsing OTA status: ${e.message}"
            null
        } finally {
            _isLoading.value = false
        }
    }
    
    // Convenience methods for common operations
    suspend fun setPreset(preset: Int): Boolean {
        return sendControlRequest(LEDControlRequest(preset = preset))
    }
    
    suspend fun setBrightness(brightness: Int): Boolean {
        return sendControlRequest(LEDControlRequest(brightness = brightness))
    }
    
    suspend fun setEffectSpeed(speed: Int): Boolean {
        return sendControlRequest(LEDControlRequest(effectSpeed = speed))
    }
    
    suspend fun setHeadlightColor(color: String): Boolean {
        return sendControlRequest(LEDControlRequest(headlightColor = color))
    }

    suspend fun setHeadlightBackgroundEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(headlightBackgroundEnabled = enabled))
    }

    suspend fun setHeadlightBackgroundColor(color: String): Boolean {
        return sendControlRequest(LEDControlRequest(headlightBackgroundColor = color))
    }
    
    suspend fun setTaillightColor(color: String): Boolean {
        return sendControlRequest(LEDControlRequest(taillightColor = color))
    }

    suspend fun setTaillightBackgroundEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(taillightBackgroundEnabled = enabled))
    }

    suspend fun setTaillightBackgroundColor(color: String): Boolean {
        return sendControlRequest(LEDControlRequest(taillightBackgroundColor = color))
    }
    
    suspend fun setHeadlightEffect(effect: Int): Boolean {
        return sendControlRequest(LEDControlRequest(headlightEffect = effect))
    }
    
    suspend fun setTaillightEffect(effect: Int): Boolean {
        return sendControlRequest(LEDControlRequest(taillightEffect = effect))
    }

    suspend fun setHeadlightMode(mode: Int): Boolean {
        return sendControlRequest(LEDControlRequest(headlight_mode = mode))
    }
    
    suspend fun setMotionEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(motion_enabled = enabled))
    }

    suspend fun setDirectionBasedLighting(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(direction_based_lighting = enabled))
    }

    suspend fun setForwardAccelThreshold(threshold: Double): Boolean {
        return sendControlRequest(LEDControlRequest(forward_accel_threshold = threshold))
    }
    
    suspend fun setBlinkerEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(blinker_enabled = enabled))
    }
    
    suspend fun setParkModeEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(park_mode_enabled = enabled))
    }
    
    suspend fun setImpactDetectionEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(impact_detection_enabled = enabled))
    }

    suspend fun setBrakingEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(braking_enabled = enabled))
    }

    suspend fun setBrakingThreshold(threshold: Double): Boolean {
        return sendControlRequest(LEDControlRequest(braking_threshold = threshold))
    }

    suspend fun setBrakingEffect(effect: Int): Boolean {
        return sendControlRequest(LEDControlRequest(braking_effect = effect))
    }

    suspend fun setBrakingBrightness(brightness: Int): Boolean {
        return sendControlRequest(LEDControlRequest(braking_brightness = brightness))
    }

    suspend fun setWhiteLEDsEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(white_leds_enabled = enabled))
    }

    suspend fun setRgbwWhiteMode(mode: Int): Boolean {
        return sendControlRequest(LEDControlRequest(rgbw_white_mode = mode))
    }

    suspend fun setManualBlinker(direction: String): Boolean {
        return sendControlRequest(LEDControlRequest(manualBlinker = direction))
    }

    suspend fun setManualBrake(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(manualBrake = enabled))
    }
    
    suspend fun setMotionSensitivity(sensitivity: Double): Boolean {
        return sendControlRequest(LEDControlRequest(motion_sensitivity = sensitivity))
    }
    
    suspend fun setBlinkerDelay(delay: Int): Boolean {
        return sendControlRequest(LEDControlRequest(blinker_delay = delay))
    }
    
    suspend fun setBlinkerTimeout(timeout: Int): Boolean {
        return sendControlRequest(LEDControlRequest(blinker_timeout = timeout))
    }
    
    suspend fun setParkStationaryTime(time: Int): Boolean {
        return sendControlRequest(LEDControlRequest(park_stationary_time = time))
    }
    
    suspend fun setParkAccelNoiseThreshold(threshold: Double): Boolean {
        return sendControlRequest(LEDControlRequest(park_accel_noise_threshold = threshold))
    }
    
    suspend fun setParkGyroNoiseThreshold(threshold: Double): Boolean {
        return sendControlRequest(LEDControlRequest(park_gyro_noise_threshold = threshold))
    }
    
    suspend fun setImpactThreshold(threshold: Double): Boolean {
        return sendControlRequest(LEDControlRequest(impact_threshold = threshold))
    }
    
    suspend fun setParkEffect(effect: Int): Boolean {
        return sendControlRequest(LEDControlRequest(park_effect = effect))
    }
    
    suspend fun setParkEffectSpeed(speed: Int): Boolean {
        return sendControlRequest(LEDControlRequest(park_effect_speed = speed))
    }
    
    suspend fun setParkBrightness(brightness: Int): Boolean {
        return sendControlRequest(LEDControlRequest(park_brightness = brightness))
    }
    
    suspend fun setParkHeadlightColor(r: Int, g: Int, b: Int): Boolean {
        return sendControlRequest(LEDControlRequest(
            park_headlight_color_r = r,
            park_headlight_color_g = g,
            park_headlight_color_b = b
        ))
    }
    
    suspend fun setParkTaillightColor(r: Int, g: Int, b: Int): Boolean {
        return sendControlRequest(LEDControlRequest(
            park_taillight_color_r = r,
            park_taillight_color_g = g,
            park_taillight_color_b = b
        ))
    }
    
    suspend fun startCalibration(): Boolean {
        return sendControlRequest(LEDControlRequest(startCalibration = true))
    }
    
    suspend fun nextCalibrationStep(): Boolean {
        return sendControlRequest(LEDControlRequest(nextCalibrationStep = true))
    }
    
    suspend fun resetCalibration(): Boolean {
        return sendControlRequest(LEDControlRequest(resetCalibration = true))
    }
    
    suspend fun testStartupSequence(): Boolean {
        return sendControlRequest(LEDControlRequest(testStartup = true))
    }
    
    suspend fun testParkMode(): Boolean {
        return sendControlRequest(LEDControlRequest(testParkMode = true))
    }
    
    suspend fun setStartupSequence(sequence: Int): Boolean {
        return sendControlRequest(LEDControlRequest(startup_sequence = sequence))
    }
    
    suspend fun setStartupDuration(duration: Int): Boolean {
        return sendControlRequest(LEDControlRequest(startup_duration = duration))
    }
    
    suspend fun setAPName(name: String): Boolean {
        return sendControlRequest(LEDControlRequest(apName = name))
    }
    
    suspend fun setAPPassword(password: String): Boolean {
        return sendControlRequest(LEDControlRequest(apPassword = password))
    }
    
    suspend fun applyWiFiConfig(name: String, password: String): Boolean {
        return sendControlRequest(LEDControlRequest(
            apName = name,
            apPassword = password,
            restart = true
        ))
    }

    suspend fun restoreDefaults(): Boolean {
        return sendControlRequest(LEDControlRequest(restoreDefaults = true))
    }
    
    suspend fun setESPNowEnabled(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(enableESPNow = enabled))
    }
    
    suspend fun setESPNowSync(enabled: Boolean): Boolean {
        return sendControlRequest(LEDControlRequest(useESPNowSync = enabled))
    }
    
    suspend fun setESPNowChannel(channel: Int): Boolean {
        return sendControlRequest(LEDControlRequest(espNowChannel = channel))
    }
    
    suspend fun setDeviceName(name: String): Boolean {
        return sendControlRequest(LEDControlRequest(deviceName = name))
    }
    
    suspend fun createGroup(code: String?): Boolean {
        return sendControlRequest(LEDControlRequest(
            groupAction = "create",
            groupCode = code
        ))
    }
    
    suspend fun joinGroup(code: String): Boolean {
        return sendControlRequest(LEDControlRequest(
            groupAction = "join",
            groupCode = code
        ))
    }

    suspend fun scanJoinGroup(): Boolean {
        return sendControlRequest(LEDControlRequest(groupAction = "scan_join"))
    }
    
    suspend fun leaveGroup(): Boolean {
        return sendControlRequest(LEDControlRequest(groupAction = "leave"))
    }
    
    suspend fun allowGroupJoin(): Boolean {
        return sendControlRequest(LEDControlRequest(groupAction = "allow_join"))
    }
    
    suspend fun blockGroupJoin(): Boolean {
        return sendControlRequest(LEDControlRequest(groupAction = "block_join"))
    }

    suspend fun savePreset(name: String): Boolean {
        return sendControlRequest(LEDControlRequest(presetAction = "save", presetName = name))
    }

    suspend fun updatePreset(index: Int, name: String): Boolean {
        return sendControlRequest(LEDControlRequest(presetAction = "update", presetIndex = index, presetName = name))
    }

    suspend fun deletePreset(index: Int): Boolean {
        return sendControlRequest(LEDControlRequest(presetAction = "delete", presetIndex = index))
    }
    
    // ============================================
    // OTA FIRMWARE UPLOAD VIA HTTP
    // ============================================
    
    /**
     * Upload firmware directly to the device via HTTP.
     * Requires the phone to be connected to the device's WiFi AP.
     * 
     * @param fileBytes The firmware binary data
     * @param fileName The name of the firmware file
     * @param onProgress Callback for upload progress (0-100)
     * @param context Android context for network binding
     * @return true if upload was successful
     */
    suspend fun uploadFirmwareViaHttp(
        fileBytes: ByteArray,
        fileName: String,
        onProgress: (Int) -> Unit,
        context: android.content.Context? = null
    ): Boolean = withContext(Dispatchers.IO) {
        val deviceIP = "192.168.4.1"  // Default ESP32 AP IP
        val url = java.net.URL("http://$deviceIP/api/ota-upload")
        // Boundary should NOT have leading dashes - they are added in the body
        val boundary = "FirmwareBoundary${System.currentTimeMillis()}"
        
        _isLoading.value = true
        _lastError.value = null
        
        // Build multipart body parts
        val lineEnd = "\r\n"
        
        // Pre-calculate content to get exact length
        // Body boundaries have -- prefix, end boundary has -- suffix too
        val headerPart = ("--$boundary$lineEnd" +
                "Content-Disposition: form-data; name=\"firmware\"; filename=\"$fileName\"$lineEnd" +
                "Content-Type: application/octet-stream$lineEnd" +
                lineEnd).toByteArray(Charsets.UTF_8)
        
        val footerPart = ("$lineEnd--$boundary--$lineEnd").toByteArray(Charsets.UTF_8)
        
        val totalContentLength = headerPart.size + fileBytes.size + footerPart.size
        
        android.util.Log.d("OTA", "Starting upload: $fileName, size: ${fileBytes.size}, total: $totalContentLength")
        
        try {
            // Get WiFi network and bind connection to it (prevents routing through mobile data)
            val wifiNetwork = context?.let { ctx ->
                val connectivityManager = ctx.getSystemService(android.content.Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
                connectivityManager.allNetworks.find { network ->
                    val capabilities = connectivityManager.getNetworkCapabilities(network)
                    capabilities?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) == true
                }
            }
            
            android.util.Log.d("OTA", "WiFi network found: ${wifiNetwork != null}")
            
            // Open connection through WiFi network if available, otherwise use default
            val connection = if (wifiNetwork != null) {
                android.util.Log.d("OTA", "Binding connection to WiFi network")
                wifiNetwork.openConnection(url) as java.net.HttpURLConnection
            } else {
                android.util.Log.d("OTA", "Using default network (no WiFi binding)")
                url.openConnection() as java.net.HttpURLConnection
            }
            
            connection.apply {
                requestMethod = "POST"
                doOutput = true
                doInput = true
                useCaches = false
                connectTimeout = 30000
                readTimeout = 180000  // 3 minutes for large files
                setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
                setRequestProperty("Content-Length", totalContentLength.toString())
                setRequestProperty("Connection", "Keep-Alive")
                // Disable chunked streaming by setting fixed length
                setFixedLengthStreamingMode(totalContentLength)
            }
            
            android.util.Log.d("OTA", "Connection configured, starting upload...")
            
            connection.outputStream.use { os ->
                // Write header part
                os.write(headerPart)
                android.util.Log.d("OTA", "Header written")
                
                // Write file bytes with progress tracking
                val totalBytes = fileBytes.size
                var bytesWritten = 0
                val chunkSize = 4096
                
                var offset = 0
                while (offset < totalBytes) {
                    val remaining = totalBytes - offset
                    val toWrite = minOf(chunkSize, remaining)
                    os.write(fileBytes, offset, toWrite)
                    offset += toWrite
                    bytesWritten += toWrite
                    
                    val progress = (bytesWritten * 100) / totalBytes
                    onProgress(progress)
                    
                    // Log progress every 10%
                    if (progress % 10 == 0 && progress > 0) {
                        android.util.Log.d("OTA", "Upload progress: $progress%")
                    }
                }
                
                // Write footer part
                os.write(footerPart)
                os.flush()
                android.util.Log.d("OTA", "Upload complete, waiting for response...")
            }
            
            android.util.Log.d("OTA", "Upload complete, waiting for response...")
            
            // Read response
            val responseCode = connection.responseCode
            val responseBody = try {
                if (responseCode in 200..299) {
                    connection.inputStream.bufferedReader().readText()
                } else {
                    connection.errorStream?.bufferedReader()?.readText() ?: "Unknown error"
                }
            } catch (e: Exception) {
                "Response read error: ${e.message}"
            }
            
            connection.disconnect()
            
            android.util.Log.d("OTA", "Response code: $responseCode, body: $responseBody")
            
            if (responseCode in 200..299) {
                // Parse response to check success
                try {
                    val response = json.decodeFromString<ApiResponse>(responseBody)
                    if (response.success) {
                        onProgress(100)
                        true
                    } else {
                        _lastError.value = response.error ?: "Upload failed"
                        false
                    }
                } catch (e: Exception) {
                    // If response parsing fails but status was OK, assume success
                    android.util.Log.d("OTA", "Response parse error (assuming success): ${e.message}")
                    onProgress(100)
                    true
                }
            } else {
                _lastError.value = "HTTP Error: $responseCode - $responseBody"
                false
            }
        } catch (e: java.net.ConnectException) {
            android.util.Log.e("OTA", "ConnectException: ${e.message}")
            _lastError.value = "Cannot connect to device. Make sure you're connected to the device's WiFi network."
            false
        } catch (e: java.net.SocketTimeoutException) {
            android.util.Log.d("OTA", "SocketTimeoutException - device may be restarting")
            _lastError.value = "Connection timed out. The device may be restarting."
            // Timeout during upload might mean success (device is restarting)
            true
        } catch (e: Exception) {
            android.util.Log.e("OTA", "Upload error: ${e.message}", e)
            _lastError.value = "Upload error: ${e.message}"
            false
        } finally {
            _isLoading.value = false
        }
    }
}
