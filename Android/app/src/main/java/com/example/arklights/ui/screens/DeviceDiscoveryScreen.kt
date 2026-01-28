package com.example.arklights.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import android.bluetooth.BluetoothDevice
import com.example.arklights.viewmodel.ArkLightsViewModel
import kotlinx.coroutines.launch

@Composable
fun DeviceDiscoveryScreen(
    viewModel: ArkLightsViewModel
) {
    val scope = rememberCoroutineScope()
    val discoveredDevices by viewModel.discoveredDevices.collectAsState()
    val savedDevices by viewModel.savedDevices.collectAsState()
    val lastDeviceAddress by viewModel.lastDeviceAddress.collectAsState()
    var pairedDevices by remember { mutableStateOf<List<BluetoothDevice>>(emptyList()) }
    var isScanning by remember { mutableStateOf(false) }
    
    LaunchedEffect(Unit) {
        // Load paired devices on first launch
        pairedDevices = viewModel.getPairedDevices()
    }
    
    Column(
        modifier = Modifier.fillMaxSize(),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "Connect to ArkLights Device",
            style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.Bold
        )
        
        Spacer(modifier = Modifier.height(16.dp))

        if (savedDevices.isNotEmpty()) {
            Text(
                text = "Saved Devices",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )

            Spacer(modifier = Modifier.height(8.dp))

            LazyColumn(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(savedDevices) { device ->
                    SavedDeviceItem(
                        device = device,
                        isLastConnected = device.address == lastDeviceAddress,
                        onConnect = {
                            scope.launch {
                                viewModel.connectToSavedDevice(device.address)
                            }
                        },
                        onForget = {
                            scope.launch {
                                viewModel.removeSavedDevice(device.address)
                            }
                        }
                    )
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
        }
        
        // Scan Controls
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceEvenly
        ) {
            Button(
                onClick = {
                    scope.launch {
                        isScanning = true
                        viewModel.startDiscovery()
                    }
                },
                enabled = !isScanning
            ) {
                Text("Start Scan")
            }
            
            Button(
                onClick = {
                    scope.launch {
                        isScanning = false
                        viewModel.stopDiscovery()
                    }
                },
                enabled = isScanning
            ) {
                Text("Stop Scan")
            }
        }
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // Paired Devices Section
        if (pairedDevices.isNotEmpty()) {
            Text(
                text = "Paired Devices",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(8.dp))
            
            LazyColumn(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(pairedDevices) { device ->
                    DeviceItem(
                        device = device,
                        onClick = {
                            scope.launch {
                                viewModel.connectToDevice(device)
                            }
                        }
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
        }
        
        // Discovered Devices Section
        if (discoveredDevices.isNotEmpty()) {
            Text(
                text = "Discovered Devices",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            
            Spacer(modifier = Modifier.height(8.dp))
            
            LazyColumn(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(discoveredDevices) { device ->
                    DeviceItem(
                        device = device,
                        onClick = {
                            scope.launch {
                                viewModel.connectToDevice(device)
                            }
                        }
                    )
                }
            }
        } else if (isScanning) {
            Text(
                text = "Scanning for devices...",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            Text(
                text = "No devices found. Make sure your ArkLights device is powered on and in pairing mode.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
fun SavedDeviceItem(
    device: com.example.arklights.data.SavedDevice,
    isLastConnected: Boolean,
    onConnect: () -> Unit,
    onForget: () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(
                modifier = Modifier.weight(1f)
            ) {
                Text(
                    text = device.name,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = device.address,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                if (isLastConnected) {
                    Text(
                        text = "Last connected",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = onConnect) {
                    Text("Connect")
                }
                OutlinedButton(onClick = onForget) {
                    Text("Forget")
                }
            }
        }
    }
}

@Composable
fun DeviceItem(
    device: BluetoothDevice,
    onClick: () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(
                modifier = Modifier.weight(1f)
            ) {
                Text(
                    text = try {
                        device.name ?: "Unknown Device"
                    } catch (e: SecurityException) {
                        "Unknown Device"
                    },
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                
                Text(
                    text = device.address,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            
            Text(
                text = "📱",
                style = MaterialTheme.typography.titleLarge
            )
        }
    }
}
