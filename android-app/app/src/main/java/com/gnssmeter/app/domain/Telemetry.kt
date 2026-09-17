package com.gnssmeter.app.domain

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow

enum class ConnectionState { STOPPED, CONNECTING, CONNECTED, INTERRUPTED }
enum class GnssState { UNAVAILABLE, INVALID, STALE, VALID }

enum class SimulationScenario {
    IDLE,
    ACCELERATION,
    BRAKING,
    GENTLE_CORNER,
    SPORT_MANEUVER,
    GNSS_LOSS,
    STREAM_GAP,
}

data class TelemetrySample(
    val sourceTimeMs: Long,
    val speedMps: Float,
    val longitudinalG: Float,
    val lateralG: Float,
    val verticalG: Float,
    val gnssState: GnssState,
    val usedSatellites: Int?,
    val horizontalAccuracyM: Float?,
    val speedAccuracyMps: Float?,
    val sequence: Long,
    val imuValid: Boolean,
    val imuCalibrated: Boolean,
)

interface TelemetryDataSource {
    val connectionState: StateFlow<ConnectionState>
    fun samples(): Flow<TelemetrySample>
    suspend fun start()
    suspend fun stop()
}

data class StreamDiagnostics(
    val measuredHz: Float = 0f,
    val lastSampleAgeMs: Long? = null,
    val lostSamples: Long = 0,
    val receivedSamples: Long = 0,
    val sourceName: String = "Симулятор",
)
