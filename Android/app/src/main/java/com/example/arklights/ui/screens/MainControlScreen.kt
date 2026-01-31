package com.example.arklights.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
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
                "main" -> MainControlsPage(viewModel = viewModel)
                "settings" -> SettingsPage(viewModel = viewModel)
            }
        }
    }
}

@Composable
fun MainControlsPage(
    viewModel: ArkLightsViewModel
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
        
        // Quick Presets - Prominent at the top
        QuickPresetsSection(
            currentPreset = deviceStatus?.preset,
            onPresetSelected = { preset ->
                scope.launch {
                    viewModel.setPreset(preset)
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
        
        // Device Status Debug Section - Shows raw data from controller
        DeviceStatusDebugSection(deviceStatus = deviceStatus)
    }
}
