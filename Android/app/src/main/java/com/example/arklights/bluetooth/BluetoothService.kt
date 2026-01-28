package com.example.arklights.bluetooth

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.ActivityCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.UUID
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import com.example.arklights.data.ConnectionState
import java.io.ByteArrayOutputStream
import java.util.concurrent.ConcurrentHashMap

class BluetoothService(private val context: Context) {

    data class BleFrame(
        val type: Byte,
        val seq: Int,
        val flags: Byte,
        val payload: ByteArray
    )
    
    companion object {
        // BLE Service and Characteristic UUIDs (matching ESP32)
        private val SERVICE_UUID = UUID.fromString("12345678-1234-1234-1234-123456789abc")
        private val CHARACTERISTIC_UUID = UUID.fromString("87654321-4321-4321-4321-cba987654321")
        private const val ARKLIGHTS_DEVICE_NAME_PREFIX = "ARKLIGHTS"

        private const val FRAME_MAGIC0: Byte = 0xA7.toByte()
        private const val FRAME_MAGIC1: Byte = 0x1C.toByte()
        private const val FRAME_VERSION: Byte = 0x01
        private const val FRAME_FLAG_ACK_REQUIRED: Byte = 0x01
        private const val FRAME_HEADER_SIZE = 8
        private const val FRAME_CRC_SIZE = 2

        private const val MSG_SETTINGS_JSON: Byte = 0x01
        private const val MSG_STATUS_REQUEST: Byte = 0x02
        private const val MSG_STATUS_RESPONSE: Byte = 0x03
        private const val MSG_OTA_START: Byte = 0x04
        private const val MSG_OTA_STATUS: Byte = 0x05
        private const val MSG_ACK: Byte = 0x7E.toByte()
        private const val MSG_ERROR: Byte = 0x7F.toByte()
    }

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter
    private val bleScanner: BluetoothLeScanner? = bluetoothAdapter?.bluetoothLeScanner

    private var bluetoothGatt: BluetoothGatt? = null
    private var characteristic: BluetoothGattCharacteristic? = null
    private var currentMtu: Int = 23
    private var awaitingServiceDiscovery = false
    private var notificationsEnabled = false

    private val sendMutex = Mutex()
    private var nextSeq: Int = 0
    private val pendingAcks = ConcurrentHashMap<Int, CompletableDeferred<Boolean>>()
    private val pendingResponses = ConcurrentHashMap<Int, CompletableDeferred<BleFrame>>()
    private val rxBuffer = ByteArrayOutputStream()

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()
    private val _isReady = MutableStateFlow(false)
    val isReady: StateFlow<Boolean> = _isReady.asStateFlow()

    private val _discoveredDevices = MutableStateFlow<List<BluetoothDevice>>(emptyList())
    val discoveredDevices: StateFlow<List<BluetoothDevice>> = _discoveredDevices.asStateFlow()

    private val _messages = MutableSharedFlow<String>()
    val messages: SharedFlow<String> = _messages.asSharedFlow()

    private val _errorMessage = MutableSharedFlow<String>()
    val errorMessage: SharedFlow<String> = _errorMessage.asSharedFlow()


    private fun parseContentLength(headers: String): Int? {
        val regex = Regex("Content-Length:\\s*(\\d+)", RegexOption.IGNORE_CASE)
        val match = regex.find(headers) ?: return null
        return match.groupValues[1].toIntOrNull()
    }

    private fun isCompleteHttpResponse(response: String): Boolean {
        val headerEnd = response.indexOf("\r\n\r\n")
        if (headerEnd < 0) return false

        val headers = response.substring(0, headerEnd)
        val body = response.substring(headerEnd + 4)

        val contentLength = parseContentLength(headers)
        if (contentLength != null) {
            return body.length >= contentLength
        }

        val trimmedBody = body.trimEnd()
        if (trimmedBody.isEmpty()) {
            return false
        }
        if (trimmedBody.startsWith("{") && trimmedBody.endsWith("}")) return true
        if (trimmedBody.startsWith("[") && trimmedBody.endsWith("]")) return true
        return false
    }

    private fun extractNextHttpResponse(buffer: StringBuilder): String? {
        var data = buffer.toString()
        val startIndex = data.indexOf("HTTP/")
        if (startIndex < 0) {
            if (buffer.length > 4096) {
                buffer.clear()
            }
            return null
        }
        if (startIndex > 0) {
            buffer.delete(0, startIndex)
            data = buffer.toString()
        }

        val headerEnd = data.indexOf("\r\n\r\n")
        if (headerEnd < 0) return null

        val headers = data.substring(0, headerEnd)
        val contentLength = parseContentLength(headers)
        val bodyStart = headerEnd + 4
        if (contentLength != null) {
            val bodyEnd = bodyStart + contentLength
            if (data.length < bodyEnd) return null
            val response = data.substring(0, bodyEnd)
            buffer.delete(0, bodyEnd)
            return response
        }

        if (isCompleteHttpResponse(data)) {
            buffer.clear()
            return data
        }

        return null
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt?, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    _connectionState.value = ConnectionState.CONNECTED
                    _isReady.value = false
                    notificationsEnabled = false
                    try {
                        if (hasBluetoothPermissions()) {
                            val mtuRequested = gatt?.requestMtu(185) == true
                            if (mtuRequested) {
                                awaitingServiceDiscovery = true
                            } else {
                                gatt?.discoverServices()
                            }
                        }
                    } catch (e: SecurityException) {
                        _errorMessage.tryEmit("Bluetooth permission denied")
                    }
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    _connectionState.value = ConnectionState.DISCONNECTED
                    awaitingServiceDiscovery = false
                    currentMtu = 23
                    notificationsEnabled = false
                    _isReady.value = false
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt?.getService(SERVICE_UUID)
                characteristic = service?.getCharacteristic(CHARACTERISTIC_UUID)
                if (characteristic != null) {
                    characteristic?.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                    try {
                        if (hasBluetoothPermissions()) {
                            gatt?.setCharacteristicNotification(characteristic, true)
                            
                            // Enable notifications by setting the descriptor
                            val descriptor = characteristic?.getDescriptor(
                                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
                            )
                            if (descriptor != null) {
                                // Enable notifications to receive status/settings responses.
                                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                                val writeOk = gatt?.writeDescriptor(descriptor) == true
                                if (!writeOk) {
                                    notificationsEnabled = true
                                    _isReady.value = true
                                    println("Android: Descriptor write failed, enabling notifications fallback")
                                } else {
                                    _isReady.value = false
                                    notificationsEnabled = false
                                }
                            } else {
                                notificationsEnabled = true
                                _isReady.value = true
                                println("Android: Notification descriptor missing, enabling fallback")
                            }
                        }
                    } catch (e: SecurityException) {
                        _errorMessage.tryEmit("Bluetooth permission denied")
                    }
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt?, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                println("Android: MTU changed to $mtu")
                currentMtu = mtu
            } else {
                println("Android: MTU change failed: $status")
            }
            if (awaitingServiceDiscovery) {
                awaitingServiceDiscovery = false
                try {
                    if (hasBluetoothPermissions()) {
                        gatt?.discoverServices()
                    }
                } catch (e: SecurityException) {
                    _errorMessage.tryEmit("Bluetooth permission denied")
                }
            }
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt?, characteristic: BluetoothGattCharacteristic?) {
            val data = characteristic?.value
            if (data != null && data.isNotEmpty()) {
                println("Android: Received BLE data: ${data.size} bytes")
                val frames = appendAndExtractFrames(data)
                frames.forEach { frame ->
                    if (frame.type == MSG_ACK) {
                        pendingAcks.remove(frame.seq)?.complete(true)
                        return@forEach
                    }

                    if (frame.flags.toInt() and FRAME_FLAG_ACK_REQUIRED.toInt() != 0) {
                        sendAck(frame.seq)
                    }

                    when (frame.type) {
                        MSG_STATUS_RESPONSE, MSG_OTA_STATUS -> {
                            pendingResponses.remove(frame.seq)?.complete(frame)
                        }
                        MSG_ERROR -> {
                            val message = frame.payload.toString(Charsets.UTF_8)
                            _errorMessage.tryEmit("BLE error: $message")
                            pendingResponses.remove(frame.seq)?.complete(frame)
                        }
                        else -> {
                            // Ignore other message types for now
                        }
                    }
                }
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt?, descriptor: BluetoothGattDescriptor?, status: Int) {
            if (descriptor?.uuid == UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    notificationsEnabled = true
                    _isReady.value = true
                    println("Android: BLE notifications enabled successfully")
                } else {
                    notificationsEnabled = false
                    _isReady.value = false
                    println("Android: Failed to enable BLE notifications: $status")
                }
            }
        }
    }

    fun isBluetoothEnabled(): Boolean {
        return bluetoothAdapter?.isEnabled == true
    }

    fun hasBluetoothPermissions(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ActivityCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED &&
            ActivityCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
        } else {
            ActivityCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH) == PackageManager.PERMISSION_GRANTED &&
            ActivityCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_ADMIN) == PackageManager.PERMISSION_GRANTED
        }
    }

    suspend fun startDiscovery(): Boolean = withContext(Dispatchers.IO) {
        if (!isBluetoothEnabled() || !hasBluetoothPermissions()) {
            _errorMessage.tryEmit("Bluetooth not enabled or permissions not granted")
            return@withContext false
        }

        val devices = mutableListOf<BluetoothDevice>()
        
        try {
            bleScanner?.startScan(object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    val device = result.device
                    val deviceName = try {
                        device.name ?: "Unknown Device"
                    } catch (e: SecurityException) {
                        "Unknown Device"
                    }
                    
                    if (deviceName.startsWith(ARKLIGHTS_DEVICE_NAME_PREFIX)) {
                        if (!devices.any { it.address == device.address }) {
                            devices.add(device)
                            _discoveredDevices.value = devices.toList()
                        }
                    }
                }

                override fun onScanFailed(errorCode: Int) {
                    _errorMessage.tryEmit("BLE scan failed with error: $errorCode")
                }
            })
        } catch (e: SecurityException) {
            _errorMessage.tryEmit("Bluetooth permission denied")
            return@withContext false
        }

        // Stop scanning after 10 seconds
        kotlinx.coroutines.delay(10000)
        try {
            bleScanner?.stopScan(object : ScanCallback() {})
        } catch (e: SecurityException) {
            _errorMessage.tryEmit("Bluetooth permission denied")
        }
        true
    }

    suspend fun stopDiscovery(): Boolean = withContext(Dispatchers.IO) {
        try {
            bleScanner?.stopScan(object : ScanCallback() {})
            true
        } catch (e: SecurityException) {
            _errorMessage.tryEmit("Bluetooth permission denied")
            false
        } catch (e: Exception) {
            _errorMessage.tryEmit("Failed to stop discovery: ${e.message}")
            false
        }
    }

    suspend fun getPairedDevices(): List<BluetoothDevice> = withContext(Dispatchers.IO) {
        if (!hasBluetoothPermissions()) {
            return@withContext emptyList()
        }
        
        try {
            bluetoothAdapter?.bondedDevices?.filter { device ->
                device.name?.startsWith(ARKLIGHTS_DEVICE_NAME_PREFIX) == true
            } ?: emptyList()
        } catch (e: SecurityException) {
            _errorMessage.tryEmit("Bluetooth permission denied")
            emptyList()
        } catch (e: Exception) {
            _errorMessage.tryEmit("Failed to get paired devices: ${e.message}")
            emptyList()
        }
    }

    suspend fun connectToDevice(device: BluetoothDevice): Boolean = withContext(Dispatchers.IO) {
        if (!hasBluetoothPermissions()) {
            _errorMessage.tryEmit("Bluetooth permission denied")
            return@withContext false
        }

        _connectionState.value = ConnectionState.CONNECTING
        try {
            bluetoothGatt = device.connectGatt(context, false, gattCallback)
        } catch (e: SecurityException) {
            _errorMessage.tryEmit("Bluetooth permission denied")
            return@withContext false
        }
        true
    }

    suspend fun disconnect(): Boolean = withContext(Dispatchers.IO) {
        try {
            try {
                bluetoothGatt?.disconnect()
            } catch (e: SecurityException) {
                _errorMessage.tryEmit("Bluetooth permission denied")
            }
            try {
                bluetoothGatt?.close()
            } catch (e: SecurityException) {
                _errorMessage.tryEmit("Bluetooth permission denied")
            }
            bluetoothGatt = null
            characteristic = null
            _connectionState.value = ConnectionState.DISCONNECTED
            true
        } catch (e: Exception) {
            _errorMessage.tryEmit("Disconnect error: ${e.message}")
            false
        }
    }

    suspend fun sendData(data: String): Boolean = withContext(Dispatchers.IO) {
        if (_connectionState.value != ConnectionState.CONNECTED || characteristic == null) {
            _errorMessage.tryEmit("Not connected to device")
            return@withContext false
        }

        try {
            writeInChunks(data.toByteArray())
        } catch (e: Exception) {
            _errorMessage.tryEmit("Failed to send data: ${e.message}")
            false
        }
    }

    suspend fun sendHttpRequest(endpoint: String, method: String = "GET", body: String? = null): String? = withContext(Dispatchers.IO) {
        if (_connectionState.value != ConnectionState.CONNECTED || characteristic == null) {
            _errorMessage.tryEmit("Not connected to device")
            return@withContext null
        }

        try {
            return@withContext when {
                method == "GET" && endpoint == "/api/status" -> {
                    val statusJson = requestStatus()
                    if (statusJson != null) {
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ${statusJson.length}\r\n\r\n$statusJson"
                    } else {
                        null
                    }
                }
                method == "POST" && endpoint == "/api" && body != null -> {
                    val ok = sendSettingsJson(body)
                    if (ok) {
                        "HTTP/1.1 202 Accepted\r\nContent-Length: 15\r\n\r\n{\"queued\":true}"
                    } else {
                        null
                    }
                }
                else -> {
                    _errorMessage.tryEmit("Unsupported BLE HTTP request: $method $endpoint")
                    null
                }
            }
        } catch (e: Exception) {
            _errorMessage.tryEmit("HTTP request failed: ${e.message}")
            null
        }
    }

    private suspend fun writeInChunks(data: ByteArray): Boolean {
        if (!hasBluetoothPermissions()) {
            _errorMessage.tryEmit("Bluetooth permission denied")
            return false
        }

        val gatt = bluetoothGatt ?: return false
        val targetCharacteristic = characteristic ?: return false
        val maxPayload = (currentMtu - 3).coerceAtLeast(20)

        var offset = 0
        while (offset < data.size) {
            val end = (offset + maxPayload).coerceAtMost(data.size)
            val chunk = data.copyOfRange(offset, end)
            targetCharacteristic.value = chunk

            val writeOk = try {
                gatt.writeCharacteristic(targetCharacteristic)
            } catch (e: SecurityException) {
                _errorMessage.tryEmit("Bluetooth permission denied")
                return false
            }

            if (!writeOk) {
                _errorMessage.tryEmit("Failed to write BLE chunk")
                return false
            }

            offset = end
            delay(5)
        }

        return true
    }

    suspend fun sendSettingsJson(jsonBody: String): Boolean = withContext(Dispatchers.IO) {
        sendFrameAwaitAck(MSG_SETTINGS_JSON, jsonBody.toByteArray())
    }

    suspend fun requestStatus(): String? = withContext(Dispatchers.IO) {
        if (!_isReady.value || !notificationsEnabled) {
            println("Android: Status request skipped (BLE not ready)")
            return@withContext null
        }
        val frame = sendFrameAwaitResponse(
            MSG_STATUS_REQUEST,
            ByteArray(0),
            MSG_STATUS_RESPONSE,
            timeoutMs = 15000
        )
        if (frame == null) {
            println("Android: Status response timed out")
            return@withContext null
        }
        val payload = frame.payload.toString(Charsets.UTF_8)
        val preview = payload.take(200).replace("\n", " ").replace("\r", " ")
        println("Android: Status payload length=${payload.length} preview=$preview")
        payload
    }

    suspend fun startOta(url: String): Boolean = withContext(Dispatchers.IO) {
        sendFrameAwaitAck(MSG_OTA_START, url.toByteArray())
    }

    suspend fun requestOtaStatus(): String? = withContext(Dispatchers.IO) {
        val frame = sendFrameAwaitResponse(MSG_OTA_STATUS, ByteArray(0), MSG_OTA_STATUS)
        frame?.payload?.toString(Charsets.UTF_8)
    }

    private fun buildHttpRequest(endpoint: String, method: String, body: String?): String {
        val request = StringBuilder()
        request.append("$method $endpoint HTTP/1.1\r\n")
        request.append("Host: localhost\r\n")
        request.append("Content-Type: application/json\r\n")
        
        if (body != null) {
            request.append("Content-Length: ${body.length}\r\n")
        }
        
        request.append("\r\n")
        
        if (body != null) {
            request.append(body)
        }
        
        return request.toString()
    }

    private fun nextSequence(): Int {
        val seq = nextSeq
        nextSeq = (nextSeq + 1) and 0xFF
        return seq
    }

    private fun crc16Ccitt(data: ByteArray): Int {
        var crc = 0xFFFF
        for (byte in data) {
            crc = crc xor ((byte.toInt() and 0xFF) shl 8)
            repeat(8) {
                crc = if (crc and 0x8000 != 0) {
                    (crc shl 1) xor 0x1021
                } else {
                    crc shl 1
                }
                crc = crc and 0xFFFF
            }
        }
        return crc and 0xFFFF
    }

    private fun buildFrame(type: Byte, seq: Int, flags: Byte, payload: ByteArray): ByteArray {
        val length = payload.size
        val frame = ByteArray(FRAME_HEADER_SIZE + length + FRAME_CRC_SIZE)
        frame[0] = FRAME_MAGIC0
        frame[1] = FRAME_MAGIC1
        frame[2] = FRAME_VERSION
        frame[3] = type
        frame[4] = seq.toByte()
        frame[5] = flags
        frame[6] = (length and 0xFF).toByte()
        frame[7] = ((length shr 8) and 0xFF).toByte()
        if (length > 0) {
            payload.copyInto(frame, FRAME_HEADER_SIZE)
        }
        val crc = crc16Ccitt(frame.copyOfRange(0, FRAME_HEADER_SIZE + length))
        frame[FRAME_HEADER_SIZE + length] = (crc and 0xFF).toByte()
        frame[FRAME_HEADER_SIZE + length + 1] = ((crc shr 8) and 0xFF).toByte()
        return frame
    }

    private fun appendAndExtractFrames(data: ByteArray): List<BleFrame> {
        rxBuffer.write(data)
        val bytes = rxBuffer.toByteArray()
        val frames = mutableListOf<BleFrame>()
        var index = 0

        while (index + FRAME_HEADER_SIZE + FRAME_CRC_SIZE <= bytes.size) {
            while (index + 1 < bytes.size && (bytes[index] != FRAME_MAGIC0 || bytes[index + 1] != FRAME_MAGIC1)) {
                index++
            }
            if (index + FRAME_HEADER_SIZE + FRAME_CRC_SIZE > bytes.size) {
                break
            }
            if (bytes[index + 2] != FRAME_VERSION) {
                index += 2
                continue
            }

            val length = (bytes[index + 6].toInt() and 0xFF) or ((bytes[index + 7].toInt() and 0xFF) shl 8)
            val frameSize = FRAME_HEADER_SIZE + length + FRAME_CRC_SIZE
            if (index + frameSize > bytes.size) {
                break
            }

            val frameBytes = bytes.copyOfRange(index, index + frameSize)
            val expectedCrc = (frameBytes[frameSize - 2].toInt() and 0xFF) or ((frameBytes[frameSize - 1].toInt() and 0xFF) shl 8)
            val actualCrc = crc16Ccitt(frameBytes.copyOfRange(0, frameSize - FRAME_CRC_SIZE))
            if (expectedCrc != actualCrc) {
                println(
                    "Android: BLE frame CRC mismatch expected=$expectedCrc actual=$actualCrc length=$length buffer=${bytes.size}"
                )
                index += 2
                continue
            }

            val payload = if (length > 0) frameBytes.copyOfRange(FRAME_HEADER_SIZE, FRAME_HEADER_SIZE + length) else ByteArray(0)
            frames.add(
                BleFrame(
                    type = frameBytes[3],
                    seq = frameBytes[4].toInt() and 0xFF,
                    flags = frameBytes[5],
                    payload = payload
                )
            )
            println(
                "Android: BLE frame parsed type=${frameBytes[3]} seq=${frameBytes[4].toInt() and 0xFF} length=$length"
            )
            index += frameSize
        }

        val remaining = if (index < bytes.size) bytes.copyOfRange(index, bytes.size) else ByteArray(0)
        rxBuffer.reset()
        rxBuffer.write(remaining)
        return frames
    }

    private suspend fun sendFrameAwaitAck(type: Byte, payload: ByteArray, timeoutMs: Long = 5000): Boolean {
        return sendMutex.withLock {
            val seq = nextSequence()
            val deferred = CompletableDeferred<Boolean>()
            pendingAcks[seq] = deferred

            val frame = buildFrame(type, seq, FRAME_FLAG_ACK_REQUIRED, payload)
            val writeOk = writeInChunks(frame)
            if (!writeOk) {
                pendingAcks.remove(seq)
                return@withLock false
            }

            val acked = withTimeoutOrNull(timeoutMs) { deferred.await() } ?: false
            pendingAcks.remove(seq)
            acked
        }
    }

    private suspend fun sendFrameAwaitResponse(type: Byte, payload: ByteArray, expectedType: Byte, timeoutMs: Long = 5000): BleFrame? {
        return sendMutex.withLock {
            val seq = nextSequence()
            val ackDeferred = CompletableDeferred<Boolean>()
            val responseDeferred = CompletableDeferred<BleFrame>()
            pendingAcks[seq] = ackDeferred
            pendingResponses[seq] = responseDeferred

            val frame = buildFrame(type, seq, FRAME_FLAG_ACK_REQUIRED, payload)
            val writeOk = writeInChunks(frame)
            if (!writeOk) {
                pendingAcks.remove(seq)
                pendingResponses.remove(seq)
                return@withLock null
            }

            val acked = withTimeoutOrNull(timeoutMs) { ackDeferred.await() } ?: false
            if (!acked) {
                pendingAcks.remove(seq)
                pendingResponses.remove(seq)
                return@withLock null
            }

            val response = withTimeoutOrNull(timeoutMs) { responseDeferred.await() }
            pendingResponses.remove(seq)
            response?.takeIf { it.type == expectedType }
        }
    }

    private fun sendAck(seq: Int) {
        val frame = buildFrame(MSG_ACK, seq, 0, ByteArray(0))
        val gatt = bluetoothGatt ?: return
        val target = characteristic ?: return
        if (!hasBluetoothPermissions()) {
            _errorMessage.tryEmit("Bluetooth permission denied")
            return
        }
        try {
            target.value = frame
            gatt.writeCharacteristic(target)
        } catch (e: SecurityException) {
            _errorMessage.tryEmit("Bluetooth permission denied")
        }
    }

    fun isConnected(): Boolean {
        return _connectionState.value == ConnectionState.CONNECTED
    }

    fun getCurrentDevice(): BluetoothDevice? {
        return bluetoothGatt?.device
    }

    fun getRemoteDevice(address: String): BluetoothDevice? {
        return try {
            bluetoothAdapter?.getRemoteDevice(address)
        } catch (e: IllegalArgumentException) {
            null
        } catch (e: SecurityException) {
            _errorMessage.tryEmit("Bluetooth permission denied")
            null
        }
    }
}