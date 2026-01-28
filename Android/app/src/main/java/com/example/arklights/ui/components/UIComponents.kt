package com.example.arklights.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.selection.selectable
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.arklights.data.*
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.Dp
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt
import android.graphics.Color as AndroidColor

@Composable
fun PresetsSection(
    deviceStatus: LEDStatus?,
    onPresetSelected: (Int) -> Unit,
    onSavePreset: (String) -> Unit,
    onRenamePreset: (Int, String) -> Unit,
    onDeletePreset: (Int) -> Unit
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

            val presets = deviceStatus?.presets?.mapIndexed { index, preset -> index to preset.name }
                ?: Presets.presetNames.toList()

            var saveName by remember { mutableStateOf("") }
            var renameIndex by remember { mutableStateOf<Int?>(null) }
            var renameValue by remember { mutableStateOf("") }

            LazyRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(presets) { (id, name) ->
                    val isActive = deviceStatus?.preset == id
                    Button(
                        onClick = { onPresetSelected(id) },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (isActive) {
                                MaterialTheme.colorScheme.secondaryContainer
                            } else {
                                MaterialTheme.colorScheme.primary
                            },
                            contentColor = if (isActive) {
                                MaterialTheme.colorScheme.onSecondaryContainer
                            } else {
                                MaterialTheme.colorScheme.onPrimary
                            }
                        )
                    ) {
                        Text(name.ifBlank { "Preset ${id + 1}" })
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            Text(
                text = "Manage presets",
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold
            )

            Spacer(modifier = Modifier.height(8.dp))

            OutlinedTextField(
                value = saveName,
                onValueChange = { saveName = it },
                label = { Text("New preset name") },
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(8.dp))

            Button(
                onClick = {
                    if (saveName.isNotBlank()) {
                        onSavePreset(saveName.trim())
                        saveName = ""
                    }
                },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Save Current as Preset")
            }

            Spacer(modifier = Modifier.height(8.dp))

            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                presets.forEach { (id, name) ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(name.ifBlank { "Preset ${id + 1}" })
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            TextButton(onClick = {
                                renameIndex = id
                                renameValue = name
                            }) {
                                Text("Rename")
                            }
                            TextButton(onClick = { onDeletePreset(id) }) {
                                Text("Remove")
                            }
                        }
                    }
                }
            }

            if (renameIndex != null) {
                AlertDialog(
                    onDismissRequest = { renameIndex = null },
                    title = { Text("Rename preset") },
                    text = {
                        OutlinedTextField(
                            value = renameValue,
                            onValueChange = { renameValue = it },
                            label = { Text("Preset name") }
                        )
                    },
                    confirmButton = {
                        TextButton(onClick = {
                            val index = renameIndex
                            if (index != null && renameValue.isNotBlank()) {
                                onRenamePreset(index, renameValue.trim())
                            }
                            renameIndex = null
                        }) {
                            Text("Save")
                        }
                    },
                    dismissButton = {
                        TextButton(onClick = { renameIndex = null }) {
                            Text("Cancel")
                        }
                    }
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HeadlightSection(
    deviceStatus: LEDStatus?,
    onColorChange: (String) -> Unit,
    onEffectChange: (Int) -> Unit,
    onBackgroundEnabledChange: (Boolean) -> Unit,
    onBackgroundColorChange: (String) -> Unit,
    onHeadlightModeChange: (Int) -> Unit
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
            
            var showColorPicker by remember { mutableStateOf(false) }
            val headlightHex = deviceStatus?.headlightColor ?: "ffffff"

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Color:",
                    modifier = Modifier.weight(1f)
                )

                Button(onClick = { showColorPicker = true }) {
                    Text("Pick Color")
                }

                Spacer(modifier = Modifier.width(8.dp))

                ColorSwatch(hex = headlightHex)
            }

            if (showColorPicker) {
                ColorPickerDialog(
                    title = "Headlight Color",
                    initialHex = headlightHex,
                    onDismiss = { showColorPicker = false },
                    onConfirm = { hex ->
                        onColorChange(hex)
                        showColorPicker = false
                    }
                )
            }
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Effect Selector
            Text(
                text = "Effect:",
                style = MaterialTheme.typography.bodyMedium
            )
            
            Spacer(modifier = Modifier.height(4.dp))
            
            var expanded by remember { mutableStateOf(false) }
            var localEffect by remember { mutableStateOf<Int?>(null) }
            val effectValue = localEffect ?: (deviceStatus?.headlightEffect ?: 0)
            LaunchedEffect(deviceStatus?.headlightEffect) {
                localEffect = null
            }
            
            ExposedDropdownMenuBox(
                expanded = expanded,
                onExpandedChange = { expanded = !expanded }
            ) {
                OutlinedTextField(
                    value = LEDEffects.effectNames[effectValue] ?: "Unknown",
                    onValueChange = {},
                    readOnly = true,
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .menuAnchor()
                )
                
                ExposedDropdownMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false }
                ) {
                    LEDEffects.effectNames.forEach { (id, name) ->
                        DropdownMenuItem(
                            text = { Text(name) },
                            onClick = {
                                localEffect = id
                                expanded = false
                                onEffectChange(id)
                            }
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            Text(
                text = "Headlight Mode:",
                style = MaterialTheme.typography.bodyMedium
            )

            Spacer(modifier = Modifier.height(4.dp))

            var headlightModeExpanded by remember { mutableStateOf(false) }
            var selectedMode by remember { mutableStateOf(deviceStatus?.headlight_mode ?: 0) }

            ExposedDropdownMenuBox(
                expanded = headlightModeExpanded,
                onExpandedChange = { headlightModeExpanded = !headlightModeExpanded }
            ) {
                OutlinedTextField(
                    value = if (selectedMode == 0) "Solid White" else "Headlight Effect",
                    onValueChange = {},
                    readOnly = true,
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = headlightModeExpanded) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .menuAnchor()
                )
                ExposedDropdownMenu(
                    expanded = headlightModeExpanded,
                    onDismissRequest = { headlightModeExpanded = false }
                ) {
                    DropdownMenuItem(
                        text = { Text("Solid White") },
                        onClick = {
                            selectedMode = 0
                            headlightModeExpanded = false
                            onHeadlightModeChange(0)
                        }
                    )
                    DropdownMenuItem(
                        text = { Text("Headlight Effect") },
                        onClick = {
                            selectedMode = 1
                            headlightModeExpanded = false
                            onHeadlightModeChange(1)
                        }
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Background Color")
                    Text(
                        text = "Enable background color for effects",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.headlightBackgroundEnabled ?: false,
                    onCheckedChange = onBackgroundEnabledChange
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            var showBackgroundPicker by remember { mutableStateOf(false) }
            val headlightBackgroundHex = deviceStatus?.headlightBackgroundColor ?: "000000"

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Button(onClick = { showBackgroundPicker = true }) {
                    Text("Pick Background")
                }

                Spacer(modifier = Modifier.width(8.dp))

                ColorSwatch(hex = headlightBackgroundHex)
            }

            if (showBackgroundPicker) {
                ColorPickerDialog(
                    title = "Headlight Background",
                    initialHex = headlightBackgroundHex,
                    onDismiss = { showBackgroundPicker = false },
                    onConfirm = { hex ->
                        onBackgroundColorChange(hex)
                        showBackgroundPicker = false
                    }
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TaillightSection(
    deviceStatus: LEDStatus?,
    onColorChange: (String) -> Unit,
    onEffectChange: (Int) -> Unit,
    onBackgroundEnabledChange: (Boolean) -> Unit,
    onBackgroundColorChange: (String) -> Unit
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
            
            var showColorPicker by remember { mutableStateOf(false) }
            val taillightHex = deviceStatus?.taillightColor ?: "ff0000"

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Color:",
                    modifier = Modifier.weight(1f)
                )

                Button(onClick = { showColorPicker = true }) {
                    Text("Pick Color")
                }

                Spacer(modifier = Modifier.width(8.dp))

                ColorSwatch(hex = taillightHex)
            }

            if (showColorPicker) {
                ColorPickerDialog(
                    title = "Taillight Color",
                    initialHex = taillightHex,
                    onDismiss = { showColorPicker = false },
                    onConfirm = { hex ->
                        onColorChange(hex)
                        showColorPicker = false
                    }
                )
            }
            
            Spacer(modifier = Modifier.height(12.dp))
            
            // Effect Selector
            Text(
                text = "Effect:",
                style = MaterialTheme.typography.bodyMedium
            )
            
            Spacer(modifier = Modifier.height(4.dp))
            
            var expanded by remember { mutableStateOf(false) }
            var localEffect by remember { mutableStateOf<Int?>(null) }
            val effectValue = localEffect ?: (deviceStatus?.taillightEffect ?: 0)
            LaunchedEffect(deviceStatus?.taillightEffect) {
                localEffect = null
            }
            
            ExposedDropdownMenuBox(
                expanded = expanded,
                onExpandedChange = { expanded = !expanded }
            ) {
                OutlinedTextField(
                    value = LEDEffects.effectNames[effectValue] ?: "Unknown",
                    onValueChange = {},
                    readOnly = true,
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .menuAnchor()
                )
                
                ExposedDropdownMenu(
                    expanded = expanded,
                    onDismissRequest = { expanded = false }
                ) {
                    LEDEffects.effectNames.forEach { (id, name) ->
                        DropdownMenuItem(
                            text = { Text(name) },
                            onClick = {
                                localEffect = id
                                expanded = false
                                onEffectChange(id)
                            }
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Background Color")
                    Text(
                        text = "Enable background color for effects",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.taillightBackgroundEnabled ?: false,
                    onCheckedChange = onBackgroundEnabledChange
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            var showBackgroundPicker by remember { mutableStateOf(false) }
            val taillightBackgroundHex = deviceStatus?.taillightBackgroundColor ?: "000000"

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Button(onClick = { showBackgroundPicker = true }) {
                    Text("Pick Background")
                }

                Spacer(modifier = Modifier.width(8.dp))

                ColorSwatch(hex = taillightBackgroundHex)
            }

            if (showBackgroundPicker) {
                ColorPickerDialog(
                    title = "Taillight Background",
                    initialHex = taillightBackgroundHex,
                    onDismiss = { showBackgroundPicker = false },
                    onConfirm = { hex ->
                        onBackgroundColorChange(hex)
                        showBackgroundPicker = false
                    }
                )
            }
        }
    }
}

@Composable
private fun ColorSwatch(hex: String) {
    val color = parseHexColor(hex)
    Surface(
        modifier = Modifier.size(32.dp),
        shape = MaterialTheme.shapes.small,
        color = color,
        tonalElevation = 1.dp
    ) {}
}

@Composable
private fun ColorPickerDialog(
    title: String,
    initialHex: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit
) {
    val initialHsv = remember(initialHex) { hexToHsv(initialHex) }
    var hue by remember { mutableStateOf(initialHsv[0]) }
    var saturation by remember { mutableStateOf(initialHsv[1]) }
    var value by remember { mutableStateOf(initialHsv[2]) }
    val previewColor = remember(hue, saturation, value) {
        Color(AndroidColor.HSVToColor(floatArrayOf(hue, saturation, value)))
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(60.dp),
                    shape = MaterialTheme.shapes.small,
                    color = previewColor
                ) {}

                HsvColorWheel(
                    hue = hue,
                    saturation = saturation,
                    value = value,
                    onHueSaturationChange = { newHue, newSaturation ->
                        hue = newHue
                        saturation = newSaturation
                    }
                )

                Text("Brightness: ${(value * 100).toInt()}%")
                Slider(
                    value = value,
                    onValueChange = { value = it },
                    valueRange = 0f..1f
                )
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val colorInt = AndroidColor.HSVToColor(floatArrayOf(hue, saturation, value))
                onConfirm(String.format("%06X", colorInt and 0xFFFFFF))
            }) {
                Text("Apply")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

private fun parseHexColor(hex: String): Color {
    val clean = hex.removePrefix("#").padStart(6, '0')
    val red = clean.substring(0, 2).toIntOrNull(16) ?: 0
    val green = clean.substring(2, 4).toIntOrNull(16) ?: 0
    val blue = clean.substring(4, 6).toIntOrNull(16) ?: 0
    return Color(red, green, blue)
}

private fun hexToHsv(hex: String): FloatArray {
    val clean = hex.removePrefix("#").padStart(6, '0')
    val colorInt = runCatching { AndroidColor.parseColor("#$clean") }
        .getOrDefault(AndroidColor.WHITE)
    val hsv = FloatArray(3)
    AndroidColor.colorToHSV(colorInt, hsv)
    return hsv
}

private fun handleWheelTouch(
    position: Offset,
    wheelSize: IntSize,
    onHueSaturationChange: (Float, Float) -> Unit
) {
    if (wheelSize.width == 0 || wheelSize.height == 0) return
    val center = Offset(wheelSize.width / 2f, wheelSize.height / 2f)
    val dx = position.x - center.x
    val dy = position.y - center.y
    val radius = min(wheelSize.width, wheelSize.height) / 2f
    val distance = min(sqrt(dx * dx + dy * dy), radius)
    val angle = (Math.toDegrees(atan2(dy.toDouble(), dx.toDouble())) + 360.0) % 360.0
    val newHue = angle.toFloat()
    val newSaturation = (distance / radius).coerceIn(0f, 1f)
    onHueSaturationChange(newHue, newSaturation)
}

@Composable
private fun HsvColorWheel(
    hue: Float,
    saturation: Float,
    value: Float,
    onHueSaturationChange: (Float, Float) -> Unit,
    wheelSizeDp: Dp = 220.dp
) {
    var wheelSize by remember { mutableStateOf(IntSize.Zero) }
    val hueColors = remember {
        listOf(
            Color.Red,
            Color.Yellow,
            Color.Green,
            Color.Cyan,
            Color.Blue,
            Color.Magenta,
            Color.Red
        )
    }

    Box(
        modifier = Modifier
            .size(wheelSizeDp)
            .onSizeChanged { wheelSize = it }
            .pointerInput(wheelSize) {
                detectTapGestures { offset ->
                    handleWheelTouch(offset, wheelSize, onHueSaturationChange)
                }
            }
            .pointerInput(wheelSize) {
                detectDragGestures { change, _ ->
                    handleWheelTouch(change.position, wheelSize, onHueSaturationChange)
                }
            }
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val radius = min(this.size.width, this.size.height) / 2f
            val center = Offset(radius, radius)

            drawCircle(
                brush = Brush.sweepGradient(hueColors),
                radius = radius,
                center = center
            )

            drawCircle(
                brush = Brush.radialGradient(
                    colors = listOf(Color.White, Color.Transparent),
                    center = center,
                    radius = radius
                ),
                radius = radius,
                center = center
            )

            if (value < 1f) {
                drawCircle(
                    color = Color.Black.copy(alpha = (1f - value).coerceIn(0f, 1f)),
                    radius = radius,
                    center = center
                )
            }

            val angleRad = Math.toRadians(hue.toDouble())
            val markerRadius = saturation * radius
            val markerX = center.x + (cos(angleRad) * markerRadius).toFloat()
            val markerY = center.y + (sin(angleRad) * markerRadius).toFloat()

            drawCircle(
                color = Color.White,
                radius = 10f,
                center = Offset(markerX, markerY),
                style = Stroke(width = 3f, cap = StrokeCap.Round)
            )
            drawCircle(
                color = Color.Black.copy(alpha = 0.4f),
                radius = 10f,
                center = Offset(markerX, markerY),
                style = Stroke(width = 1.5f, cap = StrokeCap.Round)
            )
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

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MotionControlSection(
    deviceStatus: LEDStatus?,
    onMotionEnabled: (Boolean) -> Unit,
    onBlinkerEnabled: (Boolean) -> Unit,
    onParkModeEnabled: (Boolean) -> Unit,
    onImpactDetectionEnabled: (Boolean) -> Unit,
    onMotionSensitivityChange: (Double) -> Unit,
    onDirectionBasedLighting: (Boolean) -> Unit,
    onForwardAccelThresholdChange: (Double) -> Unit,
    onBrakingEnabled: (Boolean) -> Unit,
    onBrakingEffectChange: (Int) -> Unit,
    onBrakingThresholdChange: (Double) -> Unit,
    onBrakingBrightnessChange: (Int) -> Unit,
    onRgbwWhiteModeChange: (Int) -> Unit,
    onManualBlinker: (String) -> Unit,
    onManualBrake: (Boolean) -> Unit
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

            // Direction-Based Lighting
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Direction-Based Lighting")
                    Text(
                        text = "Switch headlight/taillight based on movement direction",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.direction_based_lighting ?: false,
                    onCheckedChange = onDirectionBasedLighting
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            var forwardThreshold by remember { mutableStateOf(deviceStatus?.forward_accel_threshold ?: 0.3) }
            Text(
                text = "Direction Threshold: ${String.format("%.2f", forwardThreshold)}G",
                style = MaterialTheme.typography.bodyMedium
            )
            Slider(
                value = forwardThreshold.toFloat(),
                onValueChange = { newValue ->
                    forwardThreshold = newValue.toDouble()
                    onForwardAccelThresholdChange(forwardThreshold)
                },
                valueRange = 0.1f..1.0f,
                steps = 8,
                modifier = Modifier.fillMaxWidth()
            )
            
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

            // Braking Detection
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Braking Detection")
                    Text(
                        text = "Detect braking and adjust taillight",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.braking_enabled ?: false,
                    onCheckedChange = onBrakingEnabled
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            var brakingThreshold by remember { mutableStateOf(deviceStatus?.braking_threshold ?: -0.5) }
            Text(
                text = "Braking Threshold: ${String.format("%.2f", brakingThreshold)}G",
                style = MaterialTheme.typography.bodyMedium
            )
            Slider(
                value = brakingThreshold.toFloat(),
                onValueChange = { newValue ->
                    brakingThreshold = newValue.toDouble()
                    onBrakingThresholdChange(brakingThreshold)
                },
                valueRange = -2.0f..-0.1f,
                steps = 18,
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(8.dp))

            Text("Braking Effect")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    selected = (deviceStatus?.braking_effect ?: 0) == 0,
                    onClick = { onBrakingEffectChange(0) },
                    label = { Text("Flash") }
                )
                FilterChip(
                    selected = (deviceStatus?.braking_effect ?: 0) == 1,
                    onClick = { onBrakingEffectChange(1) },
                    label = { Text("Pulse") }
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            var brakingBrightness by remember { mutableStateOf(deviceStatus?.braking_brightness ?: 255) }
            Text("Braking Brightness: $brakingBrightness")
            Slider(
                value = brakingBrightness.toFloat(),
                onValueChange = { newValue ->
                    brakingBrightness = newValue.toInt()
                    onBrakingBrightnessChange(brakingBrightness)
                },
                valueRange = 128f..255f,
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(12.dp))

            // Manual Signals
            Text(
                text = "Manual Signals",
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { onManualBlinker("left") }) { Text("Left") }
                Button(onClick = { onManualBlinker("right") }) { Text("Right") }
                OutlinedButton(onClick = { onManualBlinker("off") }) { Text("Off") }
                Button(onClick = { onManualBrake(true) }) { Text("Brake On") }
                OutlinedButton(onClick = { onManualBrake(false) }) { Text("Brake Off") }
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

            Spacer(modifier = Modifier.height(12.dp))

            Text(
                text = "RGBW White Mode",
                style = MaterialTheme.typography.bodyMedium
            )

            Spacer(modifier = Modifier.height(4.dp))

            var rgbwExpanded by remember { mutableStateOf(false) }
            val rgbwModes = listOf(
                0 to "Off",
                1 to "Exact White",
                2 to "Boosted White",
                3 to "Max Brightness"
            )
            val currentMode = deviceStatus?.rgbw_white_mode
                ?: if (deviceStatus?.white_leds_enabled == true) 1 else 0

            ExposedDropdownMenuBox(
                expanded = rgbwExpanded,
                onExpandedChange = { rgbwExpanded = !rgbwExpanded }
            ) {
                OutlinedTextField(
                    value = rgbwModes.firstOrNull { it.first == currentMode }?.second ?: "Off",
                    onValueChange = {},
                    readOnly = true,
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = rgbwExpanded) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .menuAnchor()
                )

                ExposedDropdownMenu(
                    expanded = rgbwExpanded,
                    onDismissRequest = { rgbwExpanded = false }
                ) {
                    rgbwModes.forEach { (mode, label) ->
                        DropdownMenuItem(
                            text = { Text(label) },
                            onClick = {
                                rgbwExpanded = false
                                onRgbwWhiteModeChange(mode)
                            }
                        )
                    }
                }
            }
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
                val presetName = deviceStatus.presets.getOrNull(deviceStatus.preset)?.name
                    ?: Presets.presetNames[deviceStatus.preset]
                    ?: "Preset ${deviceStatus.preset + 1}"
                Text(
                    text = "Preset: $presetName",
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
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("Calibration", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            Button(onClick = onStartCalibration) { Text("Start Calibration") }
            Button(onClick = onNextStep) { Text("Next Step") }
            Button(onClick = onResetCalibration) { Text("Reset Calibration") }
        }
    }
}

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
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Enable ESPNow")
                    Text(
                        text = "Device-to-device sync for group rides",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.enableESPNow ?: false,
                    onCheckedChange = onESPNowEnabled
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Effect Sync")
                    Text(
                        text = "Sync headlight/taillight effects",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = deviceStatus?.useESPNowSync ?: false,
                    onCheckedChange = onESPNowSync
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            var channel by remember { mutableStateOf(deviceStatus?.espNowChannel ?: 1) }
            Text("ESPNow Channel: $channel")
            Slider(
                value = channel.toFloat(),
                onValueChange = { newValue ->
                    channel = newValue.toInt()
                    onESPNowChannelChange(channel)
                },
                valueRange = 1f..14f,
                steps = 12,
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = "Status: ${deviceStatus?.espNowStatus ?: "Unknown"}",
                style = MaterialTheme.typography.bodyMedium
            )
            Text(
                text = "Peers: ${deviceStatus?.espNowPeerCount ?: 0}",
                style = MaterialTheme.typography.bodyMedium
            )
            Text(
                text = "Last Send: ${deviceStatus?.espNowLastSend ?: "Never"}",
                style = MaterialTheme.typography.bodyMedium
            )
        }
    }
}

@Composable
fun GroupManagementSection(
    deviceStatus: LEDStatus?,
    onDeviceNameChange: (String) -> Unit,
    onCreateGroup: (String?) -> Unit,
    onJoinGroup: (String) -> Unit,
    onScanJoinGroup: () -> Unit,
    onLeaveGroup: () -> Unit,
    onAllowGroupJoin: () -> Unit,
    onBlockGroupJoin: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("Group Management", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            var deviceName by remember { mutableStateOf(deviceStatus?.deviceName ?: "") }
            var groupCode by remember { mutableStateOf("") }

            OutlinedTextField(
                value = deviceName,
                onValueChange = {
                    deviceName = it
                    onDeviceNameChange(it)
                },
                label = { Text("Device Name") },
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(8.dp))

            Text("Group Status: ${if (deviceStatus?.groupCode?.isNotBlank() == true) "In group" else "Not in group"}")
            Text("Group Code: ${deviceStatus?.groupCode ?: ""}")
            Text("Role: ${if (deviceStatus?.isGroupMaster == true) "Master" else "Follower"}")
            Text("Members: ${deviceStatus?.groupMemberCount ?: 0}")

            Spacer(modifier = Modifier.height(8.dp))

            OutlinedTextField(
                value = groupCode,
                onValueChange = { groupCode = it },
                label = { Text("Group Code (optional)") },
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(8.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { onCreateGroup(groupCode.ifBlank { null }) }) {
                    Text("Create Group")
                }
                Button(onClick = onScanJoinGroup) {
                    Text("Scan & Join")
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { onJoinGroup(groupCode) }) {
                    Text("Join by Code")
                }
                OutlinedButton(onClick = onLeaveGroup) {
                    Text("Leave")
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = onAllowGroupJoin) {
                    Text("Allow Joins")
                }
                OutlinedButton(onClick = onBlockGroupJoin) {
                    Text("Block Joins")
                }
            }
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
