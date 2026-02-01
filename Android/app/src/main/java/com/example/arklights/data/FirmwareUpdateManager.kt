package com.example.arklights.data

import android.content.Context
import android.content.SharedPreferences
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.File
import java.net.URL

/**
 * Manages firmware update checking and downloading.
 * 
 * Hosted manifest URL structure:
 * https://your-host.com/firmware/manifest.json
 * 
 * Manifest format:
 * {
 *   "latest_version": "8.1.0",
 *   "download_url": "https://your-host.com/firmware/arklights-v8.1.0.bin",
 *   "file_size": 1572864,
 *   "release_notes": "- Bug fixes\n- New features"
 * }
 */
class FirmwareUpdateManager(private val context: Context) {
    
    companion object {
        // Firmware manifest hosted on GitHub
        // This file is automatically updated by the release workflow
        const val MANIFEST_URL = "https://raw.githubusercontent.com/cleverark/ARKLIGHTS/main/firmware/manifest.json"
        
        private const val PREFS_NAME = "firmware_update_prefs"
        private const val KEY_LAST_CHECK = "last_update_check"
        private const val KEY_CACHED_MANIFEST = "cached_manifest"
        private const val KEY_DISMISSED_VERSION = "dismissed_version"
        
        // Check interval: 7 days in milliseconds
        private const val CHECK_INTERVAL_MS = 7L * 24 * 60 * 60 * 1000
    }
    
    private val prefs: SharedPreferences = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    
    private val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
    }
    
    /**
     * Check if we should auto-check for updates (once per week)
     */
    fun shouldAutoCheck(): Boolean {
        val lastCheck = prefs.getLong(KEY_LAST_CHECK, 0)
        val now = System.currentTimeMillis()
        return (now - lastCheck) > CHECK_INTERVAL_MS
    }
    
    /**
     * Get cached manifest without network request
     */
    fun getCachedManifest(): FirmwareManifest? {
        val cachedJson = prefs.getString(KEY_CACHED_MANIFEST, null) ?: return null
        return try {
            json.decodeFromString<FirmwareManifest>(cachedJson)
        } catch (e: Exception) {
            null
        }
    }
    
    /**
     * Check for updates from remote server
     */
    suspend fun checkForUpdates(): FirmwareManifest? = withContext(Dispatchers.IO) {
        try {
            val connection = URL(MANIFEST_URL).openConnection()
            connection.connectTimeout = 10000
            connection.readTimeout = 10000
            
            val manifestJson = connection.getInputStream().bufferedReader().readText()
            val manifest = json.decodeFromString<FirmwareManifest>(manifestJson)
            
            // Cache the manifest
            prefs.edit()
                .putString(KEY_CACHED_MANIFEST, manifestJson)
                .putLong(KEY_LAST_CHECK, System.currentTimeMillis())
                .apply()
            
            manifest
        } catch (e: Exception) {
            // Return cached manifest on error
            getCachedManifest()
        }
    }
    
    /**
     * Compare versions to determine if update is available
     * Supports formats: "8.0", "v8.0", "8.0.0", "v8.0 OTA"
     */
    fun isUpdateAvailable(currentVersion: String?, latestVersion: String?): Boolean {
        if (currentVersion == null || latestVersion == null) return false
        
        val current = parseVersion(currentVersion)
        val latest = parseVersion(latestVersion)
        
        // Compare major.minor.patch
        for (i in 0 until maxOf(current.size, latest.size)) {
            val c = current.getOrElse(i) { 0 }
            val l = latest.getOrElse(i) { 0 }
            if (l > c) return true
            if (l < c) return false
        }
        return false
    }
    
    private fun parseVersion(version: String): List<Int> {
        // Remove common prefixes and suffixes
        val cleaned = version
            .replace(Regex("^v", RegexOption.IGNORE_CASE), "")
            .replace(Regex("\\s*(OTA|alpha|beta|rc).*$", RegexOption.IGNORE_CASE), "")
            .trim()
        
        // Split by dots and parse as integers
        return cleaned.split(".")
            .mapNotNull { it.toIntOrNull() }
    }
    
    /**
     * Check if user dismissed this version's update notification
     */
    fun isVersionDismissed(version: String): Boolean {
        return prefs.getString(KEY_DISMISSED_VERSION, null) == version
    }
    
    /**
     * Dismiss update notification for a specific version
     */
    fun dismissVersion(version: String) {
        prefs.edit().putString(KEY_DISMISSED_VERSION, version).apply()
    }
    
    /**
     * Clear dismissed version (show notification again)
     */
    fun clearDismissedVersion() {
        prefs.edit().remove(KEY_DISMISSED_VERSION).apply()
    }
    
    /**
     * Download firmware file to cache directory
     */
    suspend fun downloadFirmware(
        url: String,
        onProgress: (Int) -> Unit
    ): File? = withContext(Dispatchers.IO) {
        try {
            val connection = URL(url).openConnection()
            connection.connectTimeout = 30000
            connection.readTimeout = 120000
            
            val contentLength = connection.contentLength
            val inputStream = connection.getInputStream()
            
            // Create cache file
            val cacheDir = context.cacheDir
            val firmwareFile = File(cacheDir, "firmware_update.bin")
            
            firmwareFile.outputStream().use { output ->
                val buffer = ByteArray(8192)
                var bytesRead: Int
                var totalBytesRead = 0L
                
                while (inputStream.read(buffer).also { bytesRead = it } != -1) {
                    output.write(buffer, 0, bytesRead)
                    totalBytesRead += bytesRead
                    
                    if (contentLength > 0) {
                        val progress = ((totalBytesRead * 100) / contentLength).toInt()
                        onProgress(progress)
                    }
                }
            }
            
            inputStream.close()
            onProgress(100)
            firmwareFile
        } catch (e: Exception) {
            null
        }
    }
    
    /**
     * Get the cached firmware file if it exists
     */
    fun getCachedFirmwareFile(): File? {
        val file = File(context.cacheDir, "firmware_update.bin")
        return if (file.exists()) file else null
    }
    
    /**
     * Delete cached firmware file
     */
    fun clearCachedFirmware() {
        File(context.cacheDir, "firmware_update.bin").delete()
    }
}

@Serializable
data class FirmwareManifest(
    val latest_version: String,
    val download_url: String,
    val file_size: Long = 0,
    val release_notes: String = "",
    val min_app_version: String = "1.0.0"
)
