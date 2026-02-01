package com.example.arklights.data

import android.content.Context
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import kotlinx.serialization.Serializable
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

private val Context.deviceDataStore by preferencesDataStore(name = "arklights_devices")

@Serializable
data class SavedDevice(
    val name: String,
    val address: String,
    val lastConnected: Long = 0
)

class DeviceStore(private val context: Context) {
    private val json = Json { ignoreUnknownKeys = true }

    private object Keys {
        val SAVED_DEVICES = stringPreferencesKey("saved_devices_json")
        val LAST_DEVICE = stringPreferencesKey("last_device_address")
    }

    val savedDevices: Flow<List<SavedDevice>> = context.deviceDataStore.data.map { prefs ->
        val raw = prefs[Keys.SAVED_DEVICES].orEmpty()
        if (raw.isBlank()) {
            emptyList()
        } else {
            runCatching { json.decodeFromString<List<SavedDevice>>(raw) }
                .getOrDefault(emptyList())
        }
    }

    val lastDeviceAddress: Flow<String?> = context.deviceDataStore.data.map { prefs ->
        prefs[Keys.LAST_DEVICE]
    }

    suspend fun saveDevice(name: String, address: String) {
        context.deviceDataStore.edit { prefs ->
            val current = decodeDevices(prefs[Keys.SAVED_DEVICES])
            val updated = current.filterNot { it.address == address } +
                SavedDevice(name = name, address = address, lastConnected = System.currentTimeMillis())
            prefs[Keys.SAVED_DEVICES] = encodeDevices(updated)
            prefs[Keys.LAST_DEVICE] = address
        }
    }

    suspend fun removeDevice(address: String) {
        context.deviceDataStore.edit { prefs ->
            val current = decodeDevices(prefs[Keys.SAVED_DEVICES])
            val updated = current.filterNot { it.address == address }
            prefs[Keys.SAVED_DEVICES] = encodeDevices(updated)
            if (prefs[Keys.LAST_DEVICE] == address) {
                prefs.remove(Keys.LAST_DEVICE)
            }
        }
    }

    private fun decodeDevices(raw: String?): List<SavedDevice> {
        val value = raw.orEmpty()
        if (value.isBlank()) return emptyList()
        return runCatching { json.decodeFromString<List<SavedDevice>>(value) }
            .getOrDefault(emptyList())
    }

    private fun encodeDevices(devices: List<SavedDevice>): String {
        val sorted = devices.sortedByDescending { it.lastConnected }
        return json.encodeToString(sorted)
    }
}
