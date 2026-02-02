package com.example.arklights.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.arklights.data.*
import androidx.compose.material3.ExperimentalMaterial3Api
import kotlin.math.*

// ============================================
// NEW CLEANER COMPONENTS
// ============================================

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun QuickPresetsSection(
    currentPreset: Int?,
    presets: List<PresetInfo>,
    onPresetSelected: (Int) -> Unit,
    onSavePreset: (String) -> Unit,
    onUpdatePreset: (Int, String) -> Unit,
    onDeletePreset: (Int) -> Unit
) {
    var showSaveDialog by remember { mutableStateOf(false) }
    var showEditDialog by remember { mutableStateOf(false) }
    var editingPresetIndex by remember { mutableStateOf(-1) }
    var editingPresetName by remember { mutableStateOf("") }
    var isEditMode by remember { mutableStateOf(false) }
    
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
        )
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            // Header with title and edit toggle
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Presets",
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                
                Row(
                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    // Edit mode toggle
                    if (presets.isNotEmpty()) {
                        IconButton(
                            onClick = { isEditMode = !isEditMode },
                            modifier = Modifier.size(32.dp)
                        ) {
                            Icon(
                                if (isEditMode) Icons.Default.Close else Icons.Default.Edit,
                                contentDescription = if (isEditMode) "Done editing" else "Edit presets",
                                tint = if (isEditMode) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.size(18.dp)
                            )
                        }
                    }
                    
                    // Save new preset button
                    IconButton(
                        onClick = { showSaveDialog = true },
                        modifier = Modifier.size(32.dp)
                    ) {
                        Icon(
                            Icons.Default.Add,
                            contentDescription = "Save current as preset",
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(20.dp)
                        )
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Presets grid
            if (presets.isEmpty()) {
                // Show message when no presets
                Text(
                    text = "No presets saved. Tap + to save current settings.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                    modifier = Modifier.padding(vertical = 8.dp)
                )
            } else {
                // Show presets in a flowing layout
                Column(
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    // Show presets in rows of 3
                    presets.chunked(3).forEach { rowPresets ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            rowPresets.forEachIndexed { indexInRow, preset ->
                                val actualIndex = presets.indexOf(preset)
                                val isSelected = currentPreset == actualIndex
                                
                                Box(
                                    modifier = Modifier.weight(1f)
                                ) {
                                    if (isEditMode) {
                                        // Edit mode: show edit/delete buttons
                                        Card(
                                            modifier = Modifier.fillMaxWidth(),
                                            colors = CardDefaults.cardColors(
                                                containerColor = MaterialTheme.colorScheme.surfaceVariant
                                            )
                                        ) {
                                            Row(
                                                modifier = Modifier
                                                    .fillMaxWidth()
                                                    .padding(4.dp),
                                                horizontalArrangement = Arrangement.SpaceBetween,
                                                verticalAlignment = Alignment.CenterVertically
                                            ) {
                                                Text(
                                                    text = preset.name.ifEmpty { "Preset ${actualIndex + 1}" },
                                                    style = MaterialTheme.typography.labelSmall,
                                                    maxLines = 1,
                                                    modifier = Modifier
                                                        .weight(1f)
                                                        .padding(start = 8.dp)
                                                )
                                                
                                                Row {
                                                    IconButton(
                                                        onClick = {
                                                            editingPresetIndex = actualIndex
                                                            editingPresetName = preset.name
                                                            showEditDialog = true
                                                        },
                                                        modifier = Modifier.size(28.dp)
                                                    ) {
                                                        Icon(
                                                            Icons.Default.Edit,
                                                            contentDescription = "Edit",
                                                            modifier = Modifier.size(16.dp),
                                                            tint = MaterialTheme.colorScheme.primary
                                                        )
                                                    }
                                                    IconButton(
                                                        onClick = { onDeletePreset(actualIndex) },
                                                        modifier = Modifier.size(28.dp)
                                                    ) {
                                                        Icon(
                                                            Icons.Default.Delete,
                                                            contentDescription = "Delete",
                                                            modifier = Modifier.size(16.dp),
                                                            tint = MaterialTheme.colorScheme.error
                                                        )
                                                    }
                                                }
                                            }
                                        }
                                    } else {
                                        // Normal mode: selectable chips
                                        FilterChip(
                                            onClick = { onPresetSelected(actualIndex) },
                                            label = { 
                                                Text(
                                                    text = preset.name.ifEmpty { "Preset ${actualIndex + 1}" },
                                                    style = MaterialTheme.typography.labelMedium,
                                                    maxLines = 1
                                                ) 
                                            },
                                            selected = isSelected,
                                            colors = FilterChipDefaults.filterChipColors(
                                                selectedContainerColor = MaterialTheme.colorScheme.primary,
                                                selectedLabelColor = MaterialTheme.colorScheme.onPrimary,
                                                containerColor = MaterialTheme.colorScheme.surface
                                            ),
                                            modifier = Modifier.fillMaxWidth()
                                        )
                                    }
                                }
                            }
                            
                            // Fill empty slots in row
                            repeat(3 - rowPresets.size) {
                                Spacer(modifier = Modifier.weight(1f))
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Save Preset Dialog
    if (showSaveDialog) {
        PresetNameDialog(
            title = "Save Preset",
            initialName = "",
            onDismiss = { showSaveDialog = false },
            onConfirm = { name ->
                onSavePreset(name)
                showSaveDialog = false
            }
        )
    }
    
    // Edit Preset Dialog
    if (showEditDialog) {
        PresetNameDialog(
            title = "Rename Preset",
            initialName = editingPresetName,
            onDismiss = { showEditDialog = false },
            onConfirm = { name ->
                onUpdatePreset(editingPresetIndex, name)
                showEditDialog = false
                isEditMode = false
            }
        )
    }
}

@Composable
private fun PresetNameDialog(
    title: String,
    initialName: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit
) {
    var name by remember { mutableStateOf(initialName) }
    
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Preset name") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
        },
        confirmButton = {
            TextButton(
                onClick = { onConfirm(name) },
                enabled = name.isNotBlank()
            ) {
                Text("Save")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

@Composable
fun BrightnessSliderCompact(
    brightness: Int,
    onBrightnessChange: (Int) -> Unit
) {
    var sliderValue by remember { mutableStateOf(brightness) }
    var isUserDragging by remember { mutableStateOf(false) }
    
    // Sync with device value when not actively dragging
    LaunchedEffect(brightness) {
        if (!isUserDragging) {
            sliderValue = brightness
        }
    }
    
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Brightness indicator dot
            Box(
                modifier = Modifier
                    .size(20.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.2f)),
                contentAlignment = Alignment.Center
            ) {
                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .clip(CircleShape)
                        .background(MaterialTheme.colorScheme.primary)
                )
            }
            
            Text(
                text = "Brightness",
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.width(80.dp)
            )
            
            Slider(
                value = sliderValue.toFloat(),
                onValueChange = { newValue ->
                    isUserDragging = true
                    sliderValue = newValue.toInt()
                },
                onValueChangeFinished = {
                    onBrightnessChange(sliderValue)
                    isUserDragging = false
                },
                valueRange = 0f..255f,
                modifier = Modifier.weight(1f)
            )
            
            Text(
                text = "${(sliderValue * 100 / 255)}%",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.primary,
                fontWeight = FontWeight.Medium,
                modifier = Modifier.width(40.dp)
            )
        }
    }
}

@Composable
fun ConnectionStatusCard(
    deviceStatus: LEDStatus?,
    onDisconnect: () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = deviceStatus?.deviceName ?: "ArkLights Device",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = "Connected",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary
                )
            }
            
            OutlinedButton(
                onClick = onDisconnect,
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = MaterialTheme.colorScheme.error
                ),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.5f))
            ) {
                Text("Disconnect")
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LightControlCard(
    title: String,
    currentColor: String?,
    currentEffect: Int?,
    backgroundEnabled: Boolean = false,
    backgroundColor: String? = null,
    onColorChange: (String) -> Unit,
    onEffectChange: (Int) -> Unit,
    onBackgroundEnabledChange: (Boolean) -> Unit = {},
    onBackgroundColorChange: (String) -> Unit = {}
) {
    var expanded by remember { mutableStateOf(false) }
    var selectedEffect by remember { mutableStateOf(currentEffect ?: 0) }
    var showBackgroundPicker by remember { mutableStateOf(false) }
    
    // Sync with device value when it changes
    LaunchedEffect(currentEffect) {
        if (currentEffect != null) {
            selectedEffect = currentEffect
        }
    }
    
    // Determine icon tint based on light type
    val iconTint = if (title == "Headlight") {
        Color(0xFFFFD700) // Gold/yellow for headlight
    } else {
        Color(0xFFFF4444) // Red for taillight
    }
    
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
        )
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            // Title row with colored icon
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                // Light type indicator
                Box(
                    modifier = Modifier
                        .size(32.dp)
                        .clip(CircleShape)
                        .background(iconTint.copy(alpha = 0.15f)),
                    contentAlignment = Alignment.Center
                ) {
                    Box(
                        modifier = Modifier
                            .size(14.dp)
                            .clip(CircleShape)
                            .background(iconTint)
                    )
                }
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
            }
            
            // Main Color picker
            Text(
                text = "Main Color",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            ColorWheelPicker(
                currentColor = currentColor,
                onColorSelected = onColorChange
            )
            
            // Background color section
            HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
            
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(
                        text = "Background Color",
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = "Color shown behind the effect",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = backgroundEnabled,
                    onCheckedChange = onBackgroundEnabledChange
                )
            }
            
            // Background color picker (only show when enabled)
            AnimatedVisibility(
                visible = backgroundEnabled,
                enter = expandVertically(),
                exit = shrinkVertically()
            ) {
                Column(
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ColorWheelPicker(
                        currentColor = backgroundColor,
                        onColorSelected = onBackgroundColorChange
                    )
                }
            }
            
            // Effect dropdown - full width for easier selection
            Column(
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = "Effect",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                
                ExposedDropdownMenuBox(
                    expanded = expanded,
                    onExpandedChange = { expanded = !expanded },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    OutlinedTextField(
                        value = LEDEffects.effectNames[selectedEffect] ?: "Solid",
                        onValueChange = {},
                        readOnly = true,
                        singleLine = true,
                        trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
                        modifier = Modifier
                            .menuAnchor()
                            .fillMaxWidth(),
                        textStyle = MaterialTheme.typography.bodyMedium,
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = MaterialTheme.colorScheme.primary,
                            unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.5f)
                        )
                    )
                    
                    ExposedDropdownMenu(
                        expanded = expanded,
                        onDismissRequest = { expanded = false }
                    ) {
                        LEDEffects.effectNames.forEach { (id, name) ->
                            DropdownMenuItem(
                                text = { Text(name) },
                                onClick = {
                                    selectedEffect = id
                                    expanded = false
                                    onEffectChange(id)
                                }
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun AdvancedSettingsCard(
    expanded: Boolean,
    onExpandedChange: (Boolean) -> Unit,
    deviceStatus: LEDStatus?,
    onSpeedChange: (Int) -> Unit,
    onMotionEnabled: (Boolean) -> Unit,
    onBlinkerEnabled: (Boolean) -> Unit,
    onParkModeEnabled: (Boolean) -> Unit,
    onImpactDetectionEnabled: (Boolean) -> Unit,
    onMotionSensitivityChange: (Double) -> Unit,
    // Direction-based lighting
    onDirectionBasedLightingEnabled: (Boolean) -> Unit = {},
    onForwardAccelThresholdChange: (Double) -> Unit = {},
    // Braking
    onBrakingEnabled: (Boolean) -> Unit = {},
    onBrakingThresholdChange: (Double) -> Unit = {},
    onBrakingBrightnessChange: (Int) -> Unit = {},
    // Manual controls
    onManualBlinkerChange: (String) -> Unit = {},
    onManualBrakeChange: (Boolean) -> Unit = {}
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surface
        ),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
    ) {
        Column {
            // Header - clickable to expand/collapse
            Surface(
                onClick = { onExpandedChange(!expanded) },
                color = Color.Transparent
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "Advanced LED Settings",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold
                        )
                        Text(
                            text = "Speed, motion, braking, direction",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    
                    Icon(
                        imageVector = if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                        contentDescription = if (expanded) "Collapse" else "Expand",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            
            // Expandable content
            AnimatedVisibility(
                visible = expanded,
                enter = expandVertically(),
                exit = shrinkVertically()
            ) {
                Column(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                    
                    // ========================
                    // DIRECTION-BASED LIGHTING (Top priority feature)
                    // ========================
                    Text(
                        text = "Direction Detection",
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.primary
                    )
                    
                    // Direction-based lighting toggle
                    CompactToggleRow(
                        label = "Direction-Based Lighting",
                        subtitle = "Change headlight based on travel direction",
                        checked = deviceStatus?.direction_based_lighting ?: false,
                        onCheckedChange = onDirectionBasedLightingEnabled
                    )
                    
                    // Forward acceleration threshold
                    CompactSliderControlDouble(
                        label = "Forward Accel Threshold",
                        value = deviceStatus?.forward_accel_threshold ?: 0.3,
                        valueRange = 0.1f..1.0f,
                        steps = 8,
                        onValueChange = onForwardAccelThresholdChange
                    )
                    
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                    
                    // Effect Speed Slider
                    CompactSliderControl(
                        label = "Effect Speed",
                        value = deviceStatus?.effectSpeed ?: 64,
                        valueRange = 0f..255f,
                        onValueChange = onSpeedChange
                    )
                    
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                    
                    // ========================
                    // MOTION FEATURES
                    // ========================
                    Text(
                        text = "Motion Features",
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.primary
                    )
                    
                    // Motion Control Toggle (Master switch)
                    CompactToggleRow(
                        label = "Motion Control",
                        subtitle = "Master switch for motion features",
                        checked = deviceStatus?.motion_enabled ?: false,
                        onCheckedChange = onMotionEnabled
                    )
                    
                    // Auto Blinkers Toggle
                    CompactToggleRow(
                        label = "Auto Blinkers",
                        subtitle = "Turn signals based on lean angle",
                        checked = deviceStatus?.blinker_enabled ?: false,
                        onCheckedChange = onBlinkerEnabled
                    )
                    
                    // Park Mode Toggle
                    CompactToggleRow(
                        label = "Park Mode",
                        subtitle = "Effects when stationary",
                        checked = deviceStatus?.park_mode_enabled ?: false,
                        onCheckedChange = onParkModeEnabled
                    )
                    
                    // Impact Detection Toggle
                    CompactToggleRow(
                        label = "Impact Detection",
                        subtitle = "Flash on sudden impacts",
                        checked = deviceStatus?.impact_detection_enabled ?: false,
                        onCheckedChange = onImpactDetectionEnabled
                    )
                    
                    // Motion Sensitivity Slider
                    CompactSliderControlDouble(
                        label = "Motion Sensitivity",
                        value = deviceStatus?.motion_sensitivity ?: 1.0,
                        valueRange = 0.5f..2.0f,
                        steps = 14,
                        onValueChange = onMotionSensitivityChange
                    )
                    
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                    
                    // ========================
                    // BRAKING DETECTION
                    // ========================
                    Text(
                        text = "Braking Detection",
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.primary
                    )
                    
                    // Braking enabled toggle
                    CompactToggleRow(
                        label = "Braking Detection",
                        subtitle = "Brighten taillight when braking",
                        checked = deviceStatus?.braking_enabled ?: false,
                        onCheckedChange = onBrakingEnabled
                    )
                    
                    // Braking threshold
                    CompactSliderControlDouble(
                        label = "Braking Threshold",
                        value = deviceStatus?.braking_threshold ?: -0.5,
                        valueRange = -2.0f..0f,
                        steps = 19,
                        onValueChange = onBrakingThresholdChange
                    )
                    
                    // Braking brightness
                    CompactSliderControl(
                        label = "Braking Brightness",
                        value = deviceStatus?.braking_brightness ?: 255,
                        valueRange = 0f..255f,
                        onValueChange = onBrakingBrightnessChange
                    )
                    
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                    
                    // ========================
                    // MANUAL CONTROLS
                    // ========================
                    Text(
                        text = "Manual Controls",
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.primary
                    )
                    
                    // Manual Blinker buttons
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "Manual Blinker",
                            style = MaterialTheme.typography.bodyMedium
                        )
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            val currentBlinker = when {
                                deviceStatus?.manual_blinker_active == true && deviceStatus.blinker_direction < 0 -> "left"
                                deviceStatus?.manual_blinker_active == true && deviceStatus.blinker_direction > 0 -> "right"
                                else -> "off"
                            }
                            
                            FilterChip(
                                onClick = { onManualBlinkerChange(if (currentBlinker == "left") "off" else "left") },
                                label = { Text("L") },
                                selected = currentBlinker == "left"
                            )
                            FilterChip(
                                onClick = { onManualBlinkerChange("off") },
                                label = { Text("Off") },
                                selected = currentBlinker == "off"
                            )
                            FilterChip(
                                onClick = { onManualBlinkerChange(if (currentBlinker == "right") "off" else "right") },
                                label = { Text("R") },
                                selected = currentBlinker == "right"
                            )
                        }
                    }
                    
                    // Manual Brake toggle
                    CompactToggleRow(
                        label = "Manual Brake",
                        subtitle = "Activate brake light manually",
                        checked = deviceStatus?.manual_brake_active ?: false,
                        onCheckedChange = onManualBrakeChange
                    )
                    
                    // Status display
                    if (deviceStatus != null) {
                        HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                        
                        Text(
                            text = "Current Status",
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.primary
                        )
                        
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            StatusIndicator("Blinker", deviceStatus.blinker_active, 
                                if (deviceStatus.blinker_direction > 0) "Right" else if (deviceStatus.blinker_direction < 0) "Left" else "Off")
                            StatusIndicator("Braking", deviceStatus.braking_active, if (deviceStatus.braking_active) "Active" else "Off")
                            StatusIndicator("Park", deviceStatus.park_mode_active, if (deviceStatus.park_mode_active) "Active" else "Off")
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(8.dp))
                }
            }
        }
    }
}

@Composable
private fun StatusIndicator(label: String, active: Boolean, status: String) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Box(
            modifier = Modifier
                .size(12.dp)
                .clip(CircleShape)
                .background(if (active) Color(0xFF4CAF50) else MaterialTheme.colorScheme.outline)
        )
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = status,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Medium
        )
    }
}

@Composable
private fun CompactSliderControl(
    label: String,
    value: Int,
    valueRange: ClosedFloatingPointRange<Float>,
    onValueChange: (Int) -> Unit
) {
    var sliderValue by remember { mutableStateOf(value) }
    var isUserDragging by remember { mutableStateOf(false) }
    
    // Sync with device value when not actively dragging
    LaunchedEffect(value) {
        if (!isUserDragging) {
            sliderValue = value
        }
    }
    
    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium
            )
            Text(
                text = "$sliderValue",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.primary,
                fontWeight = FontWeight.Medium
            )
        }
        
        Slider(
            value = sliderValue.toFloat(),
            onValueChange = { newValue ->
                isUserDragging = true
                sliderValue = newValue.toInt()
            },
            onValueChangeFinished = {
                onValueChange(sliderValue)
                isUserDragging = false
            },
            valueRange = valueRange,
            modifier = Modifier.fillMaxWidth()
        )
    }
}

@Composable
private fun CompactSliderControlDouble(
    label: String,
    value: Double,
    valueRange: ClosedFloatingPointRange<Float>,
    steps: Int,
    onValueChange: (Double) -> Unit
) {
    var sliderValue by remember { mutableStateOf(value) }
    var isUserDragging by remember { mutableStateOf(false) }
    
    // Sync with device value when not actively dragging
    LaunchedEffect(value) {
        if (!isUserDragging) {
            sliderValue = value
        }
    }
    
    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium
            )
            Text(
                text = String.format("%.1f", sliderValue),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.primary,
                fontWeight = FontWeight.Medium
            )
        }
        
        Slider(
            value = sliderValue.toFloat(),
            onValueChange = { newValue ->
                isUserDragging = true
                sliderValue = newValue.toDouble()
            },
            onValueChangeFinished = {
                onValueChange(sliderValue)
                isUserDragging = false
            },
            valueRange = valueRange,
            steps = steps,
            modifier = Modifier.fillMaxWidth()
        )
    }
}

@Composable
private fun CompactToggleRow(
    label: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium
            )
            Text(
                text = subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange
        )
    }
}

// ============================================
// LEGACY COMPONENTS (kept for Settings page)
// ============================================

@Composable
fun PresetsSection(
    onPresetSelected: (Int) -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = "Presets",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(8.dp))
            
            LazyRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(Presets.presetNames.toList()) { (id, name) ->
                    Button(
                        onClick = { onPresetSelected(id) },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = MaterialTheme.colorScheme.primary
                        )
                    ) {
                        Text(name)
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HeadlightSection(
    deviceStatus: LEDStatus?,
    onColorChange: (String) -> Unit,
    onEffectChange: (Int) -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = "Headlight",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Color Picker
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Color:",
                    modifier = Modifier.weight(1f)
                )
                
                // Simple color picker using buttons
                LazyRow(
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    items(listOf("#FFFFFF", "#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF")) { color ->
                        Button(
                            onClick = { onColorChange(color) },
                            modifier = Modifier.size(40.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = androidx.compose.ui.graphics.Color(
                                    android.graphics.Color.parseColor(color)
                                )
                            )
                        ) {}
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Effect Selector
            Text(
                text = "Effect:",
                style = MaterialTheme.typography.bodyMedium
            )
            
            Spacer(modifier = Modifier.height(4.dp))
            
            var expanded by remember { mutableStateOf(false) }
            var selectedEffect by remember { mutableStateOf(deviceStatus?.headlightEffect ?: 0) }
            
            ExposedDropdownMenuBox(
                expanded = expanded,
                onExpandedChange = { expanded = !expanded }
            ) {
                OutlinedTextField(
                    value = LEDEffects.effectNames[selectedEffect] ?: "Unknown",
                    onValueChange = {},
                    readOnly = true,
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
                    modifier = Modifier.fillMaxWidth()
                )
                
                ExposedDropdownMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false }
                ) {
                    LEDEffects.effectNames.forEach { (id, name) ->
                        DropdownMenuItem(
                            text = { Text(name) },
                            onClick = {
                                selectedEffect = id
                                expanded = false
                                onEffectChange(id)
                            }
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TaillightSection(
    deviceStatus: LEDStatus?,
    onColorChange: (String) -> Unit,
    onEffectChange: (Int) -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = "Taillight",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Color Picker
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Color:",
                    modifier = Modifier.weight(1f)
                )
                
                // Simple color picker using buttons
                LazyRow(
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    items(listOf("#FF0000", "#FFFFFF", "#00FF00", "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF")) { color ->
                        Button(
                            onClick = { onColorChange(color) },
                            modifier = Modifier.size(40.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = androidx.compose.ui.graphics.Color(
                                    android.graphics.Color.parseColor(color)
                                )
                            )
                        ) {}
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Effect Selector
            Text(
                text = "Effect:",
                style = MaterialTheme.typography.bodyMedium
            )
            
            Spacer(modifier = Modifier.height(4.dp))
            
            var expanded by remember { mutableStateOf(false) }
            var selectedEffect by remember { mutableStateOf(deviceStatus?.taillightEffect ?: 0) }
            
            ExposedDropdownMenuBox(
                expanded = expanded,
                onExpandedChange = { expanded = !expanded }
            ) {
                OutlinedTextField(
                    value = LEDEffects.effectNames[selectedEffect] ?: "Unknown",
                    onValueChange = {},
                    readOnly = true,
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
                    modifier = Modifier.fillMaxWidth()
                )
                
                ExposedDropdownMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false }
                ) {
                    LEDEffects.effectNames.forEach { (id, name) ->
                        DropdownMenuItem(
                            text = { Text(name) },
                            onClick = {
                                selectedEffect = id
                                expanded = false
                                onEffectChange(id)
                            }
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun BrightnessSection(
    deviceStatus: LEDStatus?,
    onBrightnessChange: (Int) -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = "Brightness",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(8.dp))
            
            var brightness by remember { mutableStateOf(deviceStatus?.brightness ?: 128) }
            
            Text(
                text = "Global Brightness: $brightness",
                style = MaterialTheme.typography.bodyMedium
            )
            
            Slider(
                value = brightness.toFloat(),
                onValueChange = { newValue ->
                    brightness = newValue.toInt()
                    onBrightnessChange(brightness)
                },
                valueRange = 0f..255f,
                modifier = Modifier.fillMaxWidth()
            )
        }
    }
}

@Composable
fun EffectSpeedSection(
    deviceStatus: LEDStatus?,
    onSpeedChange: (Int) -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = "Effect Speed",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(8.dp))
            
            var speed by remember { mutableStateOf(deviceStatus?.effectSpeed ?: 64) }
            
            Text(
                text = "Effect Speed: $speed",
                style = MaterialTheme.typography.bodyMedium
            )
            
            Text(
                text = "Higher values = faster effects",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            
            Slider(
                value = speed.toFloat(),
                onValueChange = { newValue ->
                    speed = newValue.toInt()
                    onSpeedChange(speed)
                },
                valueRange = 0f..255f,
                modifier = Modifier.fillMaxWidth()
            )
        }
    }
}

@Composable
fun MotionControlSection(
    deviceStatus: LEDStatus?,
    onMotionEnabled: (Boolean) -> Unit,
    onBlinkerEnabled: (Boolean) -> Unit,
    onParkModeEnabled: (Boolean) -> Unit,
    onImpactDetectionEnabled: (Boolean) -> Unit,
    onMotionSensitivityChange: (Double) -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = "Motion Control",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Motion Enabled
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Enable Motion Control")
                    Text(
                        text = "Master switch for all motion features",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.motion_enabled ?: false,
                    onCheckedChange = onMotionEnabled
                )
            }
            
            Spacer(modifier = Modifier.height(8.dp))
            
            // Blinker Enabled
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Auto Blinkers")
                    Text(
                        text = "Automatic turn signals based on lean angle",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.blinker_enabled ?: false,
                    onCheckedChange = onBlinkerEnabled
                )
            }
            
            Spacer(modifier = Modifier.height(8.dp))
            
            // Park Mode Enabled
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Park Mode")
                    Text(
                        text = "Special effects when stationary and tilted",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.park_mode_enabled ?: false,
                    onCheckedChange = onParkModeEnabled
                )
            }
            
            Spacer(modifier = Modifier.height(8.dp))
            
            // Impact Detection Enabled
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Impact Detection")
                    Text(
                        text = "Flash lights on sudden acceleration changes",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.impact_detection_enabled ?: false,
                    onCheckedChange = onImpactDetectionEnabled
                )
            }
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Motion Sensitivity
            var sensitivity by remember { mutableStateOf(deviceStatus?.motion_sensitivity ?: 1.0) }
            
            Text(
                text = "Motion Sensitivity: ${String.format("%.1f", sensitivity)}",
                style = MaterialTheme.typography.bodyMedium
            )
            
            Text(
                text = "Higher values = more sensitive to motion",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            
            Slider(
                value = sensitivity.toFloat(),
                onValueChange = { newValue ->
                    sensitivity = newValue.toDouble()
                    onMotionSensitivityChange(sensitivity)
                },
                valueRange = 0.5f..2.0f,
                steps = 14, // 0.1 increments
                modifier = Modifier.fillMaxWidth()
            )
        }
    }
}

@Composable
fun StatusSection(
    deviceStatus: LEDStatus?
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = "Status",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(8.dp))
            
            if (deviceStatus != null) {
                Text(
                    text = "Preset: ${Presets.presetNames[deviceStatus.preset] ?: "Unknown"}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Brightness: ${deviceStatus.brightness}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Effect Speed: ${deviceStatus.effectSpeed}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Startup: ${deviceStatus.startup_sequence_name} (${deviceStatus.startup_duration}ms)",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Motion: ${if (deviceStatus.motion_enabled) "Enabled" else "Disabled"}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Blinker: ${if (deviceStatus.blinker_active) {
                        if (deviceStatus.blinker_direction > 0) "Right" else "Left"
                    } else "Inactive"}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Park Mode: ${if (deviceStatus.park_mode_active) "Active" else "Inactive"}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Calibration: ${if (deviceStatus.calibration_complete) "Complete" else "Not calibrated"}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "WiFi AP: ${deviceStatus.apName}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Headlight: Effect ${deviceStatus.headlightEffect}, Color #${deviceStatus.headlightColor}",
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "Taillight: Effect ${deviceStatus.taillightEffect}, Color #${deviceStatus.taillightColor}",
                    style = MaterialTheme.typography.bodyMedium
                )
            } else {
                Text(
                    text = "Loading...",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

// Placeholder components for settings page
@Composable
fun CalibrationSection(
    deviceStatus: LEDStatus?,
    onStartCalibration: () -> Unit,
    onNextStep: () -> Unit,
    onResetCalibration: () -> Unit
) {
    val isCalibrating = deviceStatus?.calibration_mode == true
    val isComplete = deviceStatus?.calibration_complete == true
    val currentStep = deviceStatus?.calibration_step ?: 0
    
    // Define calibration steps with instructions
    val calibrationSteps = listOf(
        CalibrationStepInfo(
            step = 0,
            title = "Step 1: Level Position",
            instruction = "Place your board on a flat, level surface. The board should be in its normal riding position (not tilted).",
            icon = "⬛" // Flat
        ),
        CalibrationStepInfo(
            step = 1,
            title = "Step 2: Nose Up",
            instruction = "Tilt the board so the NOSE is pointing UP at about 45 degrees. Hold steady.",
            icon = "⬆️" // Up
        ),
        CalibrationStepInfo(
            step = 2,
            title = "Step 3: Nose Down",
            instruction = "Tilt the board so the NOSE is pointing DOWN at about 45 degrees. Hold steady.",
            icon = "⬇️" // Down
        ),
        CalibrationStepInfo(
            step = 3,
            title = "Step 4: Left Lean",
            instruction = "Lean the board to the LEFT side at about 45 degrees. Hold steady.",
            icon = "⬅️" // Left
        ),
        CalibrationStepInfo(
            step = 4,
            title = "Step 5: Right Lean",
            instruction = "Lean the board to the RIGHT side at about 45 degrees. Hold steady.",
            icon = "➡️" // Right
        ),
        CalibrationStepInfo(
            step = 5,
            title = "Complete!",
            instruction = "Calibration complete! Your IMU is now calibrated for accurate motion detection.",
            icon = "✅" // Complete
        )
    )
    
    val totalSteps = calibrationSteps.size - 1 // Exclude "Complete" from count
    val currentStepInfo = calibrationSteps.getOrNull(currentStep) ?: calibrationSteps.first()
    
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = if (isCalibrating) 
                MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
            else 
                MaterialTheme.colorScheme.surface
        )
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Header
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "IMU Calibration",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                
                // Status badge
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = when {
                        isCalibrating -> MaterialTheme.colorScheme.primary
                        isComplete -> Color(0xFF4CAF50)
                        else -> MaterialTheme.colorScheme.error.copy(alpha = 0.7f)
                    }
                ) {
                    Text(
                        text = when {
                            isCalibrating -> "Calibrating..."
                            isComplete -> "Calibrated"
                            else -> "Not Calibrated"
                        },
                        style = MaterialTheme.typography.labelSmall,
                        color = Color.White,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                    )
                }
            }
            
            // If calibrating, show step-by-step guide
            if (isCalibrating) {
                HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                
                // Progress indicator
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    for (i in 0 until totalSteps) {
                        Box(
                            modifier = Modifier
                                .size(24.dp)
                                .clip(CircleShape)
                                .background(
                                    when {
                                        i < currentStep -> Color(0xFF4CAF50) // Completed
                                        i == currentStep -> MaterialTheme.colorScheme.primary // Current
                                        else -> MaterialTheme.colorScheme.outline.copy(alpha = 0.3f) // Future
                                    }
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = "${i + 1}",
                                style = MaterialTheme.typography.labelSmall,
                                color = if (i <= currentStep) Color.White else MaterialTheme.colorScheme.onSurface
                            )
                        }
                        
                        if (i < totalSteps - 1) {
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .height(2.dp)
                                    .padding(horizontal = 4.dp)
                                    .align(Alignment.CenterVertically)
                                    .background(
                                        if (i < currentStep) Color(0xFF4CAF50) 
                                        else MaterialTheme.colorScheme.outline.copy(alpha = 0.3f)
                                    )
                            )
                        }
                    }
                }
                
                Spacer(modifier = Modifier.height(8.dp))
                
                // Current step card
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surface
                    )
                ) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        // Step icon
                        Text(
                            text = currentStepInfo.icon,
                            style = MaterialTheme.typography.displayMedium
                        )
                        
                        // Step title
                        Text(
                            text = currentStepInfo.title,
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.primary
                        )
                        
                        // Instructions
                        Text(
                            text = currentStepInfo.instruction,
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(horizontal = 8.dp)
                        )
                        
                        // Next step button (only if not on final step)
                        if (currentStep < totalSteps) {
                            Button(
                                onClick = onNextStep,
                                modifier = Modifier.fillMaxWidth(),
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = MaterialTheme.colorScheme.primary
                                )
                            ) {
                                Text(
                                    text = if (currentStep == totalSteps - 1) "Complete Calibration" else "Position Set - Next Step",
                                    fontWeight = FontWeight.SemiBold
                                )
                            }
                        }
                    }
                }
                
                // Cancel button
                OutlinedButton(
                    onClick = onResetCalibration,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.outlinedButtonColors(
                        contentColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("Cancel Calibration")
                }
                
            } else {
                // Not calibrating - show start/reset options
                Text(
                    text = if (isComplete) 
                        "Your IMU is calibrated and ready for accurate motion detection."
                    else 
                        "Calibrate the IMU sensor for accurate blinker, braking, and park mode detection.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = onStartCalibration,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (isComplete) 
                                MaterialTheme.colorScheme.secondary 
                            else 
                                MaterialTheme.colorScheme.primary
                        )
                    ) {
                        Text(if (isComplete) "Recalibrate" else "Start Calibration")
                    }
                    
                    if (isComplete) {
                        OutlinedButton(
                            onClick = onResetCalibration,
                            colors = ButtonDefaults.outlinedButtonColors(
                                contentColor = MaterialTheme.colorScheme.error
                            )
                        ) {
                            Text("Reset")
                        }
                    }
                }
            }
        }
    }
}

private data class CalibrationStepInfo(
    val step: Int,
    val title: String,
    val instruction: String,
    val icon: String
)

@Composable
fun MotionStatusSection(deviceStatus: LEDStatus?) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("Motion Status", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Text("Blinker: ${if (deviceStatus?.blinker_active == true) "Active" else "Inactive"}")
            Text("Park Mode: ${if (deviceStatus?.park_mode_active == true) "Active" else "Inactive"}")
            Text("Calibration: ${if (deviceStatus?.calibration_complete == true) "Complete" else "Not calibrated"}")
        }
    }
}

@Composable
fun StartupSequenceSection(
    deviceStatus: LEDStatus?,
    onSequenceChange: (Int) -> Unit,
    onDurationChange: (Int) -> Unit,
    onTestStartup: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("Startup Sequence", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Button(onClick = onTestStartup) { Text("Test Startup Sequence") }
        }
    }
}

@Composable
fun AdvancedMotionControlSection(
    deviceStatus: LEDStatus?,
    onBlinkerDelayChange: (Int) -> Unit,
    onBlinkerTimeoutChange: (Int) -> Unit,
    onParkStationaryTimeChange: (Int) -> Unit,
    onParkAccelNoiseThresholdChange: (Double) -> Unit,
    onParkGyroNoiseThresholdChange: (Double) -> Unit,
    onImpactThresholdChange: (Double) -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("Advanced Motion Control", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Text("Advanced motion control settings...")
        }
    }
}

@Composable
fun ParkModeSettingsSection(
    deviceStatus: LEDStatus?,
    onParkEffectChange: (Int) -> Unit,
    onParkEffectSpeedChange: (Int) -> Unit,
    onParkBrightnessChange: (Int) -> Unit,
    onParkHeadlightColorChange: (Int, Int, Int) -> Unit,
    onParkTaillightColorChange: (Int, Int, Int) -> Unit,
    onTestParkMode: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("Park Mode Settings", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Button(onClick = onTestParkMode) { Text("Test Park Mode") }
        }
    }
}

@Composable
fun WiFiConfigurationSection(
    deviceStatus: LEDStatus?,
    onAPNameChange: (String) -> Unit,
    onAPPasswordChange: (String) -> Unit,
    onApplyWiFiConfig: (String, String) -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("WiFi Configuration", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Text("WiFi configuration settings...")
        }
    }
}

@Composable
fun ESPNowConfigurationSection(
    deviceStatus: LEDStatus?,
    onESPNowEnabled: (Boolean) -> Unit,
    onESPNowSync: (Boolean) -> Unit,
    onESPNowChannelChange: (Int) -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("ESPNow Configuration", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Text("ESPNow configuration settings...")
        }
    }
}

@Composable
fun GroupManagementSection(
    deviceStatus: LEDStatus?,
    onDeviceNameChange: (String) -> Unit,
    onCreateGroup: (String) -> Unit,
    onJoinGroup: (String) -> Unit,
    onLeaveGroup: () -> Unit,
    onAllowGroupJoin: () -> Unit,
    onBlockGroupJoin: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("Group Management", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Text("Group management settings...")
        }
    }
}

@Composable
fun LEDConfigurationSection(
    deviceStatus: LEDStatus?,
    onLEDConfigUpdate: (LEDConfigRequest) -> Unit,
    onTestLEDs: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("LED Configuration", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Button(onClick = onTestLEDs) { Text("Test LEDs") }
        }
    }
}

// ============================================
// DEVICE STATUS DEBUG SECTION
// ============================================

@Composable
fun DeviceStatusDebugSection(deviceStatus: LEDStatus?) {
    var expanded by remember { mutableStateOf(false) }
    
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Column {
            // Header - clickable to expand/collapse
            Surface(
                onClick = { expanded = !expanded },
                color = Color.Transparent
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "Device Status (Debug)",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            text = "Raw data from controller",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    
                    Icon(
                        imageVector = if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                        contentDescription = if (expanded) "Collapse" else "Expand",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            
            // Expandable content
            AnimatedVisibility(
                visible = expanded,
                enter = expandVertically(),
                exit = shrinkVertically()
            ) {
                if (deviceStatus != null) {
                    Column(
                        modifier = Modifier
                            .padding(horizontal = 16.dp, vertical = 8.dp)
                            .heightIn(max = 400.dp)
                            .verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // Basic Info
                        StatusCategory("Basic Settings")
                        StatusRow("Preset", "${deviceStatus.preset} (${Presets.presetNames[deviceStatus.preset] ?: "Unknown"})")
                        StatusRow("Brightness", "${deviceStatus.brightness}")
                        StatusRow("Effect Speed", "${deviceStatus.effectSpeed}")
                        StatusRow("Device Name", deviceStatus.deviceName)
                        StatusRow("Firmware Version", deviceStatus.firmware_version.ifEmpty { "Unknown" })
                        StatusRow("Build Date", deviceStatus.build_date)
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // Headlight Settings
                        StatusCategory("Headlight")
                        StatusRow("Color", "#${deviceStatus.headlightColor}")
                        StatusRow("Effect", "${deviceStatus.headlightEffect} (${LEDEffects.effectNames[deviceStatus.headlightEffect] ?: "Unknown"})")
                        StatusRow("LED Count", "${deviceStatus.headlightLedCount}")
                        StatusRow("LED Type", "${deviceStatus.headlightLedType}")
                        StatusRow("Background Enabled", "${deviceStatus.headlightBackgroundEnabled}")
                        StatusRow("Background Color", "#${deviceStatus.headlightBackgroundColor}")
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // Taillight Settings
                        StatusCategory("Taillight")
                        StatusRow("Color", "#${deviceStatus.taillightColor}")
                        StatusRow("Effect", "${deviceStatus.taillightEffect} (${LEDEffects.effectNames[deviceStatus.taillightEffect] ?: "Unknown"})")
                        StatusRow("LED Count", "${deviceStatus.taillightLedCount}")
                        StatusRow("LED Type", "${deviceStatus.taillightLedType}")
                        StatusRow("Background Enabled", "${deviceStatus.taillightBackgroundEnabled}")
                        StatusRow("Background Color", "#${deviceStatus.taillightBackgroundColor}")
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // Motion Settings
                        StatusCategory("Motion Control")
                        StatusRow("Motion Enabled", "${deviceStatus.motion_enabled}")
                        StatusRow("Blinker Enabled", "${deviceStatus.blinker_enabled}")
                        StatusRow("Blinker Active", "${deviceStatus.blinker_active}")
                        StatusRow("Blinker Direction", "${deviceStatus.blinker_direction}")
                        StatusRow("Park Mode Enabled", "${deviceStatus.park_mode_enabled}")
                        StatusRow("Park Mode Active", "${deviceStatus.park_mode_active}")
                        StatusRow("Impact Detection", "${deviceStatus.impact_detection_enabled}")
                        StatusRow("Motion Sensitivity", "${deviceStatus.motion_sensitivity}")
                        StatusRow("Braking Enabled", "${deviceStatus.braking_enabled}")
                        StatusRow("Braking Active", "${deviceStatus.braking_active}")
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // Calibration
                        StatusCategory("Calibration")
                        StatusRow("Complete", "${deviceStatus.calibration_complete}")
                        StatusRow("Mode", "${deviceStatus.calibration_mode}")
                        StatusRow("Step", "${deviceStatus.calibration_step}")
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // Park Mode Settings
                        StatusCategory("Park Mode Settings")
                        StatusRow("Park Effect", "${deviceStatus.park_effect}")
                        StatusRow("Park Effect Speed", "${deviceStatus.park_effect_speed}")
                        StatusRow("Park Brightness", "${deviceStatus.park_brightness}")
                        StatusRow("Park Headlight", "R:${deviceStatus.park_headlight_color_r} G:${deviceStatus.park_headlight_color_g} B:${deviceStatus.park_headlight_color_b}")
                        StatusRow("Park Taillight", "R:${deviceStatus.park_taillight_color_r} G:${deviceStatus.park_taillight_color_g} B:${deviceStatus.park_taillight_color_b}")
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // Startup
                        StatusCategory("Startup")
                        StatusRow("Sequence", "${deviceStatus.startup_sequence} (${deviceStatus.startup_sequence_name})")
                        StatusRow("Duration", "${deviceStatus.startup_duration}ms")
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // WiFi/Network
                        StatusCategory("Network")
                        StatusRow("AP Name", deviceStatus.apName)
                        StatusRow("ESPNow Enabled", "${deviceStatus.enableESPNow}")
                        StatusRow("ESPNow Sync", "${deviceStatus.useESPNowSync}")
                        StatusRow("ESPNow Channel", "${deviceStatus.espNowChannel}")
                        StatusRow("ESPNow Status", deviceStatus.espNowStatus)
                        StatusRow("ESPNow Peers", "${deviceStatus.espNowPeerCount}")
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // Group
                        StatusCategory("Group")
                        StatusRow("Group Code", deviceStatus.groupCode.ifEmpty { "(none)" })
                        StatusRow("Is Master", "${deviceStatus.isGroupMaster}")
                        StatusRow("Has Master", "${deviceStatus.hasGroupMaster}")
                        StatusRow("Member Count", "${deviceStatus.groupMemberCount}")
                        
                        Spacer(modifier = Modifier.height(8.dp))
                        
                        // OTA
                        StatusCategory("OTA")
                        StatusRow("Status", deviceStatus.ota_status)
                        StatusRow("Progress", "${deviceStatus.ota_progress}%")
                        StatusRow("In Progress", "${deviceStatus.ota_in_progress}")
                        if (deviceStatus.ota_error != null) {
                            StatusRow("Error", deviceStatus.ota_error)
                        }
                        
                        Spacer(modifier = Modifier.height(8.dp))
                    }
                } else {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = "No device status available",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun StatusCategory(title: String) {
    Text(
        text = title,
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.primary,
        fontWeight = FontWeight.Bold,
        modifier = Modifier.padding(top = 4.dp)
    )
}

@Composable
private fun StatusRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodySmall,
            fontWeight = FontWeight.Medium
        )
    }
}

// ============================================
// COLOR WHEEL PICKER
// ============================================

@Composable
fun ColorWheelPicker(
    currentColor: String?,
    onColorSelected: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    var hue by remember { mutableStateOf(0f) }
    var saturation by remember { mutableStateOf(1f) }
    var brightness by remember { mutableStateOf(1f) }
    
    // Parse current color to initialize HSV values
    LaunchedEffect(currentColor) {
        if (currentColor != null) {
            try {
                val color = android.graphics.Color.parseColor("#$currentColor")
                val hsv = FloatArray(3)
                android.graphics.Color.colorToHSV(color, hsv)
                hue = hsv[0]
                saturation = hsv[1]
                brightness = hsv[2]
            } catch (e: Exception) {
                // Keep default values
            }
        }
    }
    
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // Color preview
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "Color",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            
            // Current color preview
            val currentHsvColor = Color.hsv(hue, saturation, brightness)
            Box(
                modifier = Modifier
                    .size(32.dp)
                    .clip(CircleShape)
                    .background(currentHsvColor)
                    .border(2.dp, MaterialTheme.colorScheme.outline, CircleShape)
            )
        }
        
        // Hue slider (rainbow gradient)
        HueSlider(
            hue = hue,
            onHueChange = { newHue ->
                hue = newHue
                val hexColor = hsvToHex(hue, saturation, brightness)
                onColorSelected("#$hexColor")
            }
        )
        
        // Saturation slider
        SaturationSlider(
            hue = hue,
            saturation = saturation,
            onSaturationChange = { newSat ->
                saturation = newSat
                val hexColor = hsvToHex(hue, saturation, brightness)
                onColorSelected("#$hexColor")
            }
        )
        
        // Brightness slider  
        BrightnessSlider(
            hue = hue,
            saturation = saturation,
            brightness = brightness,
            onBrightnessChange = { newBright ->
                brightness = newBright
                val hexColor = hsvToHex(hue, saturation, brightness)
                onColorSelected("#$hexColor")
            }
        )
        
        // Quick color presets row
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceEvenly
        ) {
            listOf(
                "#FFFFFF", "#FF0000", "#00FF00", "#0000FF", 
                "#FFFF00", "#FF00FF", "#00FFFF", "#FF6600"
            ).forEach { color ->
                Box(
                    modifier = Modifier
                        .size(28.dp)
                        .clip(CircleShape)
                        .background(Color(android.graphics.Color.parseColor(color)))
                        .border(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.3f), CircleShape)
                        .clickable { 
                            onColorSelected(color)
                            // Update HSV values
                            try {
                                val parsedColor = android.graphics.Color.parseColor(color)
                                val hsv = FloatArray(3)
                                android.graphics.Color.colorToHSV(parsedColor, hsv)
                                hue = hsv[0]
                                saturation = hsv[1]
                                brightness = hsv[2]
                            } catch (e: Exception) { }
                        }
                )
            }
        }
    }
}

@Composable
private fun HueSlider(
    hue: Float,
    onHueChange: (Float) -> Unit
) {
    val rainbowColors = listOf(
        Color.Red,
        Color.Yellow,
        Color.Green,
        Color.Cyan,
        Color.Blue,
        Color.Magenta,
        Color.Red
    )
    
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(24.dp)
            .clip(RoundedCornerShape(12.dp))
    ) {
        // Rainbow gradient background
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    detectTapGestures { offset ->
                        val newHue = (offset.x / size.width) * 360f
                        onHueChange(newHue.coerceIn(0f, 360f))
                    }
                }
                .pointerInput(Unit) {
                    detectDragGestures { change, _ ->
                        val newHue = (change.position.x / size.width) * 360f
                        onHueChange(newHue.coerceIn(0f, 360f))
                    }
                }
        ) {
            drawRect(
                brush = Brush.horizontalGradient(rainbowColors)
            )
            
            // Draw indicator
            val indicatorX = (hue / 360f) * size.width
            drawCircle(
                color = Color.White,
                radius = 10f,
                center = Offset(indicatorX, size.height / 2),
                style = Stroke(width = 3f)
            )
            drawCircle(
                color = Color.Black,
                radius = 10f,
                center = Offset(indicatorX, size.height / 2),
                style = Stroke(width = 1f)
            )
        }
    }
}

@Composable
private fun SaturationSlider(
    hue: Float,
    saturation: Float,
    onSaturationChange: (Float) -> Unit
) {
    val startColor = Color.hsv(hue, 0f, 1f)
    val endColor = Color.hsv(hue, 1f, 1f)
    
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(24.dp)
            .clip(RoundedCornerShape(12.dp))
    ) {
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    detectTapGestures { offset ->
                        val newSat = offset.x / size.width
                        onSaturationChange(newSat.coerceIn(0f, 1f))
                    }
                }
                .pointerInput(Unit) {
                    detectDragGestures { change, _ ->
                        val newSat = change.position.x / size.width
                        onSaturationChange(newSat.coerceIn(0f, 1f))
                    }
                }
        ) {
            drawRect(
                brush = Brush.horizontalGradient(listOf(startColor, endColor))
            )
            
            // Draw indicator
            val indicatorX = saturation * size.width
            drawCircle(
                color = Color.White,
                radius = 10f,
                center = Offset(indicatorX, size.height / 2),
                style = Stroke(width = 3f)
            )
            drawCircle(
                color = Color.Black,
                radius = 10f,
                center = Offset(indicatorX, size.height / 2),
                style = Stroke(width = 1f)
            )
        }
    }
}

@Composable
private fun BrightnessSlider(
    hue: Float,
    saturation: Float,
    brightness: Float,
    onBrightnessChange: (Float) -> Unit
) {
    val startColor = Color.hsv(hue, saturation, 0f)
    val endColor = Color.hsv(hue, saturation, 1f)
    
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(24.dp)
            .clip(RoundedCornerShape(12.dp))
    ) {
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    detectTapGestures { offset ->
                        val newBright = offset.x / size.width
                        onBrightnessChange(newBright.coerceIn(0f, 1f))
                    }
                }
                .pointerInput(Unit) {
                    detectDragGestures { change, _ ->
                        val newBright = change.position.x / size.width
                        onBrightnessChange(newBright.coerceIn(0f, 1f))
                    }
                }
        ) {
            drawRect(
                brush = Brush.horizontalGradient(listOf(startColor, endColor))
            )
            
            // Draw indicator
            val indicatorX = brightness * size.width
            drawCircle(
                color = Color.White,
                radius = 10f,
                center = Offset(indicatorX, size.height / 2),
                style = Stroke(width = 3f)
            )
            drawCircle(
                color = Color.Black,
                radius = 10f,
                center = Offset(indicatorX, size.height / 2),
                style = Stroke(width = 1f)
            )
        }
    }
}

private fun hsvToHex(hue: Float, saturation: Float, brightness: Float): String {
    val color = android.graphics.Color.HSVToColor(floatArrayOf(hue, saturation, brightness))
    return String.format("%06X", color and 0xFFFFFF)
}

// ============================================
// OTA UPDATE SECTION
// ============================================

@Composable
fun OTAUpdateSection(
    deviceStatus: LEDStatus?,
    selectedFileName: String?,
    isUploading: Boolean,
    uploadProgress: Int,
    uploadStatus: String,
    // Automatic update props
    updateAvailable: FirmwareManifest? = null,
    isCheckingForUpdates: Boolean = false,
    isDownloadingFirmware: Boolean = false,
    downloadProgress: Int = 0,
    onSelectFile: () -> Unit,
    onStartUpload: () -> Unit,
    onCancelUpload: () -> Unit,
    onCheckForUpdates: () -> Unit = {},
    onDownloadAndInstall: () -> Unit = {},
    onDismissUpdate: () -> Unit = {}
) {
    var expanded by remember { mutableStateOf(updateAvailable != null) }
    
    val apName = deviceStatus?.apName ?: "ARKLIGHTS-AP"
    val apPassword = deviceStatus?.apPassword ?: "float420"
    val deviceIP = "192.168.4.1" // Default AP IP for ESP32
    
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surface
        ),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
    ) {
        Column {
            // Header
            Surface(
                onClick = { expanded = !expanded },
                color = Color.Transparent
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "Firmware Update",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold
                        )
                        Text(
                            text = "Update device firmware via WiFi",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    
                    Icon(
                        imageVector = if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                        contentDescription = if (expanded) "Collapse" else "Expand",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            
            AnimatedVisibility(
                visible = expanded,
                enter = expandVertically(),
                exit = shrinkVertically()
            ) {
                Column(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                    
                    // ========================
                    // AUTOMATIC UPDATE SECTION
                    // ========================
                    
                    // Check for Updates Button
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = "Current Version",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Text(
                                text = deviceStatus?.build_date ?: "Unknown",
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                        
                        Button(
                            onClick = onCheckForUpdates,
                            enabled = !isCheckingForUpdates && !isDownloadingFirmware && !isUploading
                        ) {
                            if (isCheckingForUpdates) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(16.dp),
                                    strokeWidth = 2.dp,
                                    color = MaterialTheme.colorScheme.onPrimary
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                            }
                            Text("Check for Updates")
                        }
                    }
                    
                    // Update Available Card
                    if (updateAvailable != null) {
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            colors = CardDefaults.cardColors(
                                containerColor = MaterialTheme.colorScheme.primaryContainer
                            )
                        ) {
                            Column(
                                modifier = Modifier.padding(16.dp),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Column {
                                        Text(
                                            text = "Update Available!",
                                            style = MaterialTheme.typography.titleMedium,
                                            fontWeight = FontWeight.Bold,
                                            color = MaterialTheme.colorScheme.onPrimaryContainer
                                        )
                                        Text(
                                            text = "Version ${updateAvailable.latest_version}",
                                            style = MaterialTheme.typography.bodyMedium,
                                            color = MaterialTheme.colorScheme.onPrimaryContainer
                                        )
                                    }
                                    
                                    TextButton(onClick = onDismissUpdate) {
                                        Text("Dismiss")
                                    }
                                }
                                
                                if (updateAvailable.release_notes.isNotEmpty()) {
                                    Text(
                                        text = updateAvailable.release_notes,
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
                                    )
                                }
                                
                                if (updateAvailable.file_size > 0) {
                                    Text(
                                        text = "Size: ${updateAvailable.file_size / 1024}KB",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.7f)
                                    )
                                }
                                
                                // Download progress
                                if (isDownloadingFirmware) {
                                    Column {
                                        LinearProgressIndicator(
                                            progress = { downloadProgress / 100f },
                                            modifier = Modifier.fillMaxWidth(),
                                        )
                                        Text(
                                            text = "Downloading... $downloadProgress%",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onPrimaryContainer
                                        )
                                    }
                                } else if (selectedFileName != null) {
                                    Text(
                                        text = "✓ Downloaded: $selectedFileName",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.primary
                                    )
                                }
                                
                                // Action buttons
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                                ) {
                                    if (selectedFileName == null && !isDownloadingFirmware) {
                                        Button(
                                            onClick = onDownloadAndInstall,
                                            modifier = Modifier.weight(1f),
                                            colors = ButtonDefaults.buttonColors(
                                                containerColor = MaterialTheme.colorScheme.primary
                                            )
                                        ) {
                                            Text("Download Update")
                                        }
                                    } else if (selectedFileName != null && !isUploading) {
                                        Button(
                                            onClick = onStartUpload,
                                            modifier = Modifier.weight(1f),
                                            colors = ButtonDefaults.buttonColors(
                                                containerColor = MaterialTheme.colorScheme.primary
                                            )
                                        ) {
                                            Text("Install Update")
                                        }
                                    }
                                }
                                
                                Text(
                                    text = "After downloading, connect to device WiFi to install.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.7f)
                                )
                            }
                        }
                    }
                    
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                    
                    Text(
                        text = "Manual Update",
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    
                    // Step 1: WiFi Connection Info
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
                        )
                    ) {
                        Column(
                            modifier = Modifier.padding(12.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Text(
                                text = "Step 1: Connect to Device WiFi",
                                style = MaterialTheme.typography.labelLarge,
                                color = MaterialTheme.colorScheme.primary
                            )
                            
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text(
                                    text = "Network:",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Text(
                                    text = apName,
                                    style = MaterialTheme.typography.bodyMedium,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                            
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text(
                                    text = "Password:",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Text(
                                    text = apPassword,
                                    style = MaterialTheme.typography.bodyMedium,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                            
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text(
                                    text = "Device IP:",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Text(
                                    text = deviceIP,
                                    style = MaterialTheme.typography.bodyMedium,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                            
                            Text(
                                text = "Open your phone's WiFi settings and connect to this network before uploading.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                            )
                        }
                    }
                    
                    // Step 2: Select Firmware File
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.3f)
                        )
                    ) {
                        Column(
                            modifier = Modifier.padding(12.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Text(
                                text = "Step 2: Select Firmware File",
                                style = MaterialTheme.typography.labelLarge,
                                color = MaterialTheme.colorScheme.secondary
                            )
                            
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Button(
                                    onClick = onSelectFile,
                                    enabled = !isUploading,
                                    modifier = Modifier.weight(1f)
                                ) {
                                    Text(if (selectedFileName != null) "Change File" else "Select .bin File")
                                }
                            }
                            
                            if (selectedFileName != null) {
                                Text(
                                    text = "Selected: $selectedFileName",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.primary
                                )
                            }
                        }
                    }
                    
                    // Step 3: Upload
                    if (selectedFileName != null) {
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            colors = CardDefaults.cardColors(
                                containerColor = if (isUploading) 
                                    MaterialTheme.colorScheme.tertiaryContainer.copy(alpha = 0.3f)
                                else 
                                    MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
                            )
                        ) {
                            Column(
                                modifier = Modifier.padding(12.dp),
                                verticalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Text(
                                    text = "Step 3: Upload & Install",
                                    style = MaterialTheme.typography.labelLarge,
                                    color = MaterialTheme.colorScheme.tertiary
                                )
                                
                                // Show status message (always visible when not "Ready")
                                if (uploadStatus != "Ready") {
                                    val statusColor = when {
                                        uploadStatus.contains("error", ignoreCase = true) || 
                                        uploadStatus.contains("failed", ignoreCase = true) ||
                                        uploadStatus.contains("Cannot", ignoreCase = true) -> MaterialTheme.colorScheme.error
                                        uploadStatus.contains("Complete", ignoreCase = true) -> MaterialTheme.colorScheme.primary
                                        else -> MaterialTheme.colorScheme.onSurfaceVariant
                                    }
                                    Text(
                                        text = uploadStatus,
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = statusColor,
                                        fontWeight = FontWeight.Medium
                                    )
                                }
                                
                                if (isUploading) {
                                    // Progress indicator
                                    LinearProgressIndicator(
                                        progress = { uploadProgress / 100f },
                                        modifier = Modifier.fillMaxWidth(),
                                    )
                                    
                                    Text(
                                        text = "Progress: $uploadProgress%",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                    
                                    OutlinedButton(
                                        onClick = onCancelUpload,
                                        modifier = Modifier.fillMaxWidth()
                                    ) {
                                        Text("Cancel")
                                    }
                                } else {
                                    Text(
                                        text = "Make sure you're connected to the device WiFi before uploading. The device will restart after the update.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                                    )
                                    
                                    Button(
                                        onClick = onStartUpload,
                                        modifier = Modifier.fillMaxWidth(),
                                        colors = ButtonDefaults.buttonColors(
                                            containerColor = MaterialTheme.colorScheme.primary
                                        )
                                    ) {
                                        Text("Upload & Install Firmware")
                                    }
                                }
                            }
                        }
                    }
                    
                    // Warning
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.3f)
                        )
                    ) {
                        Row(
                            modifier = Modifier.padding(12.dp),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Icon(
                                Icons.Default.Warning,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.error,
                                modifier = Modifier.size(20.dp)
                            )
                            Text(
                                text = "Do not disconnect power or close the app during firmware update. The device will restart automatically when complete.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onErrorContainer
                            )
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(8.dp))
                }
            }
        }
    }
}

// ============================================
// FACTORY RESET SECTION
// ============================================

@Composable
fun FactoryResetSection(
    onFactoryReset: () -> Unit
) {
    var showConfirmDialog by remember { mutableStateOf(false) }
    
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.2f)
        ),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.3f))
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(
                    Icons.Default.Warning,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.error,
                    modifier = Modifier.size(24.dp)
                )
                Text(
                    text = "Factory Reset",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onErrorContainer
                )
            }
            
            Text(
                text = "Reset the device to factory defaults. This will erase all custom settings including:",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f)
            )
            
            Column(
                modifier = Modifier.padding(start = 16.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text("• WiFi AP name and password", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.7f))
                Text("• All saved presets", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.7f))
                Text("• LED configuration", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.7f))
                Text("• Motion calibration", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.7f))
                Text("• All other custom settings", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.7f))
            }
            
            OutlinedButton(
                onClick = { showConfirmDialog = true },
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = MaterialTheme.colorScheme.error
                ),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.error)
            ) {
                Text("Reset to Factory Defaults")
            }
        }
    }
    
    // Confirmation Dialog
    if (showConfirmDialog) {
        AlertDialog(
            onDismissRequest = { showConfirmDialog = false },
            icon = {
                Icon(
                    Icons.Default.Warning,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.error
                )
            },
            title = { Text("Factory Reset?") },
            text = {
                Text(
                    "This will erase ALL settings and restore the device to factory defaults. " +
                    "The device will restart and you'll need to reconnect.\n\n" +
                    "This action cannot be undone."
                )
            },
            confirmButton = {
                Button(
                    onClick = {
                        showConfirmDialog = false
                        onFactoryReset()
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("Reset Device")
                }
            },
            dismissButton = {
                TextButton(onClick = { showConfirmDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}
