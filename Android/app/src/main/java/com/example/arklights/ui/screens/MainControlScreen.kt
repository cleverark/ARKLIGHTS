package com.example.arklights.ui.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.BorderStroke
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.arklights.viewmodel.ArkLightsViewModel
import com.example.arklights.ui.components.*
import kotlinx.coroutines.launch

@Composable
fun MainControlScreen(
    currentPage: String,
    onPageChange: (String) -> Unit,
    viewModel: ArkLightsViewModel
) {
    Column(
        modifier = Modifier.fillMaxSize()
    ) {
        // Navigation Tabs - cleaner styling
        TabRow(
            selectedTabIndex = when (currentPage) {
                "main" -> 0
                "settings" -> 1
                else -> 0
            },
            containerColor = MaterialTheme.colorScheme.surface,
            contentColor = MaterialTheme.colorScheme.primary
        ) {
            Tab(
                selected = currentPage == "main",
                onClick = { onPageChange("main") },
                text = { Text("Lights") }
            )
            Tab(
                selected = currentPage == "settings",
                onClick = { onPageChange("settings") },
                icon = { Icon(Icons.Filled.Settings, contentDescription = null, modifier = Modifier.size(18.dp)) },
                text = { Text("Settings") }
            )
        }
        
        // Page Content
        Box(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
        ) {
            when (currentPage) {
                "main" -> MainControlsPage(viewModel = viewModel, onPageChange = onPageChange)
                "settings" -> SettingsPage(viewModel = viewModel)
            }
        }
    }
}

@Composable
fun MainControlsPage(
    viewModel: ArkLightsViewModel,
    onPageChange: (String) -> Unit
) {
    val deviceStatus by viewModel.deviceStatus.collectAsState()
    val isLoading by viewModel.isLoading.collectAsState()
    val scope = rememberCoroutineScope()
    var advancedExpanded by remember { mutableStateOf(false) }
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Sync Status Row
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Show last sync info
            if (deviceStatus != null) {
                Text(
                    text = "Device synced",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary
                )
            } else {
                Text(
                    text = "Waiting for device...",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            
            // Manual refresh button
            IconButton(
                onClick = {
                    scope.launch {
                        viewModel.refreshStatus()
                    }
                },
                enabled = !isLoading
            ) {
                if (isLoading) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(20.dp),
                        strokeWidth = 2.dp
                    )
                } else {
                    Icon(
                        Icons.Default.Refresh,
                        contentDescription = "Refresh",
                        tint = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }
        
        // Calibration Warning Banner - Show if device not calibrated
        val status = deviceStatus
        if (status != null && !status.calibration_complete && !status.calibration_mode) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.errorContainer
                ),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.5f))
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(12.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.Default.Warning,
                        contentDescription = "Warning",
                        tint = MaterialTheme.colorScheme.error,
                        modifier = Modifier.size(24.dp)
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "IMU Not Calibrated",
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onErrorContainer
                        )
                        Text(
                            text = "Direction and motion features require calibration. Go to Settings to calibrate.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f)
                        )
                    }
                }
            }
        }
        
        // Firmware Update Available Banner
        val updateAvailable by viewModel.updateAvailable.collectAsState()
        val mainContext = LocalContext.current
        
        // Initialize app context, firmware manager and auto-check on main page
        LaunchedEffect(Unit) {
            viewModel.setAppContext(mainContext)
            viewModel.initFirmwareUpdateManager(mainContext)
            if (viewModel.shouldAutoCheckForUpdates()) {
                viewModel.checkForUpdates()
            }
        }
        
        if (updateAvailable != null) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                ),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.5f)),
                onClick = { onPageChange("settings") }
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(12.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "🎉",
                        style = MaterialTheme.typography.titleLarge
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "Firmware Update Available",
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                        Text(
                            text = "Version ${updateAvailable!!.latest_version} - Tap to update",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
                        )
                    }
                    Icon(
                        Icons.Default.KeyboardArrowUp,
                        contentDescription = "Go to Settings",
                        tint = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }
        
        // Quick Presets - Prominent at the top
        QuickPresetsSection(
            currentPreset = deviceStatus?.preset,
            presets = deviceStatus?.presets ?: emptyList(),
            onPresetSelected = { preset ->
                scope.launch {
                    viewModel.setPreset(preset)
                }
            },
            onSavePreset = { name ->
                scope.launch {
                    viewModel.savePreset(name)
                }
            },
            onUpdatePreset = { index, name ->
                scope.launch {
                    viewModel.updatePreset(index, name)
                }
            },
            onDeletePreset = { index ->
                scope.launch {
                    viewModel.deletePreset(index)
                }
            }
        )
        
        // Global Brightness - Always visible since it's commonly used
        BrightnessSliderCompact(
            brightness = deviceStatus?.brightness ?: 128,
            onBrightnessChange = { brightness ->
                scope.launch {
                    viewModel.setBrightness(brightness)
                }
            }
        )
        
        // Headlight Card - Clean compact design
        LightControlCard(
            title = "Headlight",
            currentColor = deviceStatus?.headlightColor,
            currentEffect = deviceStatus?.headlightEffect,
            backgroundEnabled = deviceStatus?.headlightBackgroundEnabled ?: false,
            backgroundColor = deviceStatus?.headlightBackgroundColor,
            onColorChange = { color ->
                scope.launch {
                    viewModel.setHeadlightColor(color)
                }
            },
            onEffectChange = { effect ->
                scope.launch {
                    viewModel.setHeadlightEffect(effect)
                }
            },
            onBackgroundEnabledChange = { enabled ->
                scope.launch {
                    viewModel.setHeadlightBackgroundEnabled(enabled)
                }
            },
            onBackgroundColorChange = { color ->
                scope.launch {
                    viewModel.setHeadlightBackgroundColor(color)
                }
            }
        )
        
        // Taillight Card - Clean compact design
        LightControlCard(
            title = "Taillight",
            currentColor = deviceStatus?.taillightColor,
            currentEffect = deviceStatus?.taillightEffect,
            backgroundEnabled = deviceStatus?.taillightBackgroundEnabled ?: false,
            backgroundColor = deviceStatus?.taillightBackgroundColor,
            onColorChange = { color ->
                scope.launch {
                    viewModel.setTaillightColor(color)
                }
            },
            onEffectChange = { effect ->
                scope.launch {
                    viewModel.setTaillightEffect(effect)
                }
            },
            onBackgroundEnabledChange = { enabled ->
                scope.launch {
                    viewModel.setTaillightBackgroundEnabled(enabled)
                }
            },
            onBackgroundColorChange = { color ->
                scope.launch {
                    viewModel.setTaillightBackgroundColor(color)
                }
            }
        )
        
        // Advanced LED Settings - Collapsible (everything else goes here)
        AdvancedSettingsCard(
            expanded = advancedExpanded,
            onExpandedChange = { advancedExpanded = it },
            deviceStatus = deviceStatus,
            onSpeedChange = { speed ->
                scope.launch {
                    viewModel.setEffectSpeed(speed)
                }
            },
            onMotionEnabled = { enabled ->
                scope.launch {
                    viewModel.setMotionEnabled(enabled)
                }
            },
            onBlinkerEnabled = { enabled ->
                scope.launch {
                    viewModel.setBlinkerEnabled(enabled)
                }
            },
            onParkModeEnabled = { enabled ->
                scope.launch {
                    viewModel.setParkModeEnabled(enabled)
                }
            },
            onImpactDetectionEnabled = { enabled ->
                scope.launch {
                    viewModel.setImpactDetectionEnabled(enabled)
                }
            },
            onMotionSensitivityChange = { sensitivity ->
                scope.launch {
                    viewModel.setMotionSensitivity(sensitivity)
                }
            },
            // Direction-based lighting
            onDirectionBasedLightingEnabled = { enabled ->
                scope.launch {
                    viewModel.setDirectionBasedLighting(enabled)
                }
            },
            onForwardAccelThresholdChange = { threshold ->
                scope.launch {
                    viewModel.setForwardAccelThreshold(threshold)
                }
            },
            // Braking
            onBrakingEnabled = { enabled ->
                scope.launch {
                    viewModel.setBrakingEnabled(enabled)
                }
            },
            onBrakingThresholdChange = { threshold ->
                scope.launch {
                    viewModel.setBrakingThreshold(threshold)
                }
            },
            onBrakingBrightnessChange = { brightness ->
                scope.launch {
                    viewModel.setBrakingBrightness(brightness)
                }
            },
            // Manual controls
            onManualBlinkerChange = { direction ->
                scope.launch {
                    viewModel.setManualBlinker(direction)
                }
            },
            onManualBrakeChange = { enabled ->
                scope.launch {
                    viewModel.setManualBrake(enabled)
                }
            }
        )
    }
}

@Composable
fun SettingsPage(
    viewModel: ArkLightsViewModel
) {
    val deviceStatus by viewModel.deviceStatus.collectAsState()
    val scope = rememberCoroutineScope()
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Connection Status Header
        ConnectionStatusCard(
            deviceStatus = deviceStatus,
            onDisconnect = {
                scope.launch {
                    viewModel.disconnect()
                }
            }
        )
        
        // Calibration Section
        CalibrationSection(
            deviceStatus = deviceStatus,
            onStartCalibration = {
                scope.launch {
                    viewModel.startCalibration()
                }
            },
            onNextStep = {
                scope.launch {
                    viewModel.nextCalibrationStep()
                }
            },
            onResetCalibration = {
                scope.launch {
                    viewModel.resetCalibration()
                }
            }
        )
        
        // Motion Status
        MotionStatusSection(deviceStatus = deviceStatus)
        
        // LED Hardware Configuration
        LEDHardwareConfigSection(
            deviceStatus = deviceStatus,
            onHeadlightConfigChange = { ledCount, ledType, colorOrder ->
                scope.launch {
                    viewModel.setHeadlightLedConfig(ledCount, ledType, colorOrder)
                }
            },
            onTaillightConfigChange = { ledCount, ledType, colorOrder ->
                scope.launch {
                    viewModel.setTaillightLedConfig(ledCount, ledType, colorOrder)
                }
            }
        )
        
        // Startup Sequence Section
        StartupSequenceSection(
            deviceStatus = deviceStatus,
            onSequenceChange = { sequence ->
                scope.launch {
                    viewModel.setStartupSequence(sequence)
                }
            },
            onDurationChange = { duration ->
                scope.launch {
                    viewModel.setStartupDuration(duration)
                }
            },
            onTestStartup = {
                scope.launch {
                    viewModel.testStartupSequence()
                }
            }
        )
        
        // Advanced Motion Control Section
        AdvancedMotionControlSection(
            deviceStatus = deviceStatus,
            onBlinkerDelayChange = { delay ->
                scope.launch {
                    viewModel.setBlinkerDelay(delay)
                }
            },
            onBlinkerTimeoutChange = { timeout ->
                scope.launch {
                    viewModel.setBlinkerTimeout(timeout)
                }
            },
            onParkStationaryTimeChange = { time ->
                scope.launch {
                    viewModel.setParkStationaryTime(time)
                }
            },
            onParkAccelNoiseThresholdChange = { threshold ->
                scope.launch {
                    viewModel.setParkAccelNoiseThreshold(threshold)
                }
            },
            onParkGyroNoiseThresholdChange = { threshold ->
                scope.launch {
                    viewModel.setParkGyroNoiseThreshold(threshold)
                }
            },
            onImpactThresholdChange = { threshold ->
                scope.launch {
                    viewModel.setImpactThreshold(threshold)
                }
            }
        )
        
        // Park Mode Settings Section
        ParkModeSettingsSection(
            deviceStatus = deviceStatus,
            onParkEffectChange = { effect ->
                scope.launch {
                    viewModel.setParkEffect(effect)
                }
            },
            onParkEffectSpeedChange = { speed ->
                scope.launch {
                    viewModel.setParkEffectSpeed(speed)
                }
            },
            onParkBrightnessChange = { brightness ->
                scope.launch {
                    viewModel.setParkBrightness(brightness)
                }
            },
            onParkHeadlightColorChange = { r, g, b ->
                scope.launch {
                    viewModel.setParkHeadlightColor(r, g, b)
                }
            },
            onParkTaillightColorChange = { r, g, b ->
                scope.launch {
                    viewModel.setParkTaillightColor(r, g, b)
                }
            },
            onTestParkMode = {
                scope.launch {
                    viewModel.testParkMode()
                }
            }
        )
        
        // WiFi Configuration Section
        WiFiConfigurationSection(
            deviceStatus = deviceStatus,
            onAPNameChange = { name ->
                scope.launch {
                    viewModel.setAPName(name)
                }
            },
            onAPPasswordChange = { password ->
                scope.launch {
                    viewModel.setAPPassword(password)
                }
            },
            onApplyWiFiConfig = { name, password ->
                scope.launch {
                    viewModel.applyWiFiConfig(name, password)
                }
            }
        )
        
        // ESPNow Configuration Section
        ESPNowConfigurationSection(
            deviceStatus = deviceStatus,
            onESPNowEnabled = { enabled ->
                scope.launch {
                    viewModel.setESPNowEnabled(enabled)
                }
            },
            onESPNowSync = { enabled ->
                scope.launch {
                    viewModel.setESPNowSync(enabled)
                }
            },
            onESPNowChannelChange = { channel ->
                scope.launch {
                    viewModel.setESPNowChannel(channel)
                }
            }
        )
        
        // Group Management Section
        GroupManagementSection(
            deviceStatus = deviceStatus,
            onDeviceNameChange = { name ->
                scope.launch {
                    viewModel.setDeviceName(name)
                }
            },
            onCreateGroup = { code ->
                scope.launch {
                    viewModel.createGroup(code)
                }
            },
            onJoinGroup = { code ->
                scope.launch {
                    viewModel.joinGroup(code)
                }
            },
            onLeaveGroup = {
                scope.launch {
                    viewModel.leaveGroup()
                }
            },
            onAllowGroupJoin = {
                scope.launch {
                    viewModel.allowGroupJoin()
                }
            },
            onBlockGroupJoin = {
                scope.launch {
                    viewModel.blockGroupJoin()
                }
            }
        )
        
        // LED Configuration Section
        LEDConfigurationSection(
            deviceStatus = deviceStatus,
            onLEDConfigUpdate = { config ->
                scope.launch {
                    viewModel.updateLEDConfig(config)
                }
            },
            onTestLEDs = {
                scope.launch {
                    viewModel.testLEDs()
                }
            }
        )
        
        // OTA Firmware Update Section
        val context = LocalContext.current
        val otaFileName by viewModel.otaFileName.collectAsState()
        val otaUploading by viewModel.otaUploading.collectAsState()
        val otaProgress by viewModel.otaProgress.collectAsState()
        val otaStatus by viewModel.otaStatus.collectAsState()
        
        // Automatic update state
        val updateAvailable by viewModel.updateAvailable.collectAsState()
        val isCheckingForUpdates by viewModel.isCheckingForUpdates.collectAsState()
        val isDownloadingFirmware by viewModel.isDownloadingFirmware.collectAsState()
        val downloadProgress by viewModel.downloadProgress.collectAsState()
        
        // Initialize firmware update manager, set app context for OTA, and auto-check
        LaunchedEffect(Unit) {
            viewModel.setAppContext(context)
            viewModel.initFirmwareUpdateManager(context)
            if (viewModel.shouldAutoCheckForUpdates()) {
                viewModel.checkForUpdates()
            }
        }
        
        val filePicker = rememberLauncherForActivityResult(
            contract = ActivityResultContracts.GetContent()
        ) { uri: Uri? ->
            uri?.let {
                try {
                    val inputStream = context.contentResolver.openInputStream(uri)
                    val bytes = inputStream?.readBytes()
                    inputStream?.close()
                    
                    if (bytes != null) {
                        // Get filename from URI
                        val cursor = context.contentResolver.query(uri, null, null, null, null)
                        var fileName = "firmware.bin"
                        cursor?.use {
                            if (it.moveToFirst()) {
                                val nameIndex = it.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                                if (nameIndex >= 0) {
                                    fileName = it.getString(nameIndex)
                                }
                            }
                        }
                        
                        viewModel.setOtaFile(fileName, bytes)
                    }
                } catch (e: Exception) {
                    // Handle error
                }
            }
        }
        
        OTAUpdateSection(
            deviceStatus = deviceStatus,
            selectedFileName = otaFileName,
            isUploading = otaUploading,
            uploadProgress = otaProgress,
            uploadStatus = otaStatus,
            updateAvailable = updateAvailable,
            isCheckingForUpdates = isCheckingForUpdates,
            isDownloadingFirmware = isDownloadingFirmware,
            downloadProgress = downloadProgress,
            onSelectFile = {
                filePicker.launch("application/octet-stream")
            },
            onStartUpload = {
                scope.launch {
                    viewModel.uploadFirmware()
                }
            },
            onCancelUpload = {
                viewModel.cancelOtaUpload()
            },
            onCheckForUpdates = {
                scope.launch {
                    viewModel.checkForUpdates()
                }
            },
            onDownloadAndInstall = {
                scope.launch {
                    viewModel.downloadAndPrepareUpdate()
                }
            },
            onDismissUpdate = {
                viewModel.dismissUpdate()
            }
        )
        
        // Factory Reset Section
        FactoryResetSection(
            onFactoryReset = {
                scope.launch {
                    viewModel.restoreDefaults()
                }
            }
        )
        
        // Device Status Debug Section - Shows raw data from controller
        DeviceStatusDebugSection(deviceStatus = deviceStatus)
    }
}
