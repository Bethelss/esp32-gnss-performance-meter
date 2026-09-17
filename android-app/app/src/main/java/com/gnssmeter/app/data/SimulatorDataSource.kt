package com.gnssmeter.app.data

import com.gnssmeter.app.domain.ConnectionState
import com.gnssmeter.app.domain.GnssState
import com.gnssmeter.app.domain.SimulationScenario
import com.gnssmeter.app.domain.TelemetryDataSource
import com.gnssmeter.app.domain.TelemetrySample
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.flow
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

class SimulatorDataSource : TelemetryDataSource {
    private val _connectionState = MutableStateFlow(ConnectionState.STOPPED)
    override val connectionState: StateFlow<ConnectionState> = _connectionState

    val scenario = MutableStateFlow(SimulationScenario.ACCELERATION)
    private var running = false
    private var sequence = 0L

    override suspend fun start() {
        running = true
        _connectionState.value = ConnectionState.CONNECTED
    }

    override suspend fun stop() {
        running = false
        _connectionState.value = ConnectionState.STOPPED
    }

    override fun samples(): Flow<TelemetrySample> = flow {
        while (running) {
            val currentSequence = sequence++
            val cycleSeconds = (currentSequence % 240L) * SAMPLE_PERIOD_MS / 1000f
            val selected = scenario.value

            if (selected == SimulationScenario.STREAM_GAP && cycleSeconds in 4f..6f) {
                _connectionState.value = ConnectionState.INTERRUPTED
                delay(SAMPLE_PERIOD_MS)
                continue
            }

            _connectionState.value = ConnectionState.CONNECTED
            emit(sampleFor(selected, currentSequence, cycleSeconds))
            delay(SAMPLE_PERIOD_MS)
        }
    }

    fun select(value: SimulationScenario) {
        scenario.value = value
        sequence = 0L
    }

    internal fun sampleFor(
        selected: SimulationScenario,
        sequence: Long,
        t: Float,
    ): TelemetrySample {
        val phase = (t / 12f).coerceIn(0f, 1f)
        val wave = sin(t * PI.toFloat() / 3f)
        val (speed, longG, lateralG) = when (selected) {
            SimulationScenario.IDLE -> Triple(0f, 0.006f * wave, 0.004f * cos(t))
            SimulationScenario.ACCELERATION -> {
                val ramp = (phase * 32f).coerceAtMost(30f)
                Triple(ramp, if (ramp < 30f) 0.42f + 0.03f * wave else 0f, 0.02f * wave)
            }
            SimulationScenario.BRAKING -> {
                val value = (31f - phase * 34f).coerceAtLeast(0f)
                Triple(value, if (value > 0f) -0.58f + 0.04f * wave else 0f, 0.015f * wave)
            }
            SimulationScenario.GENTLE_CORNER -> Triple(18f, 0.03f * wave, 0.38f * sin(t / 2f))
            SimulationScenario.SPORT_MANEUVER -> Triple(
                12f + abs(wave) * 18f,
                0.72f * sin(t * 1.4f),
                1.16f * sin(t * 0.85f),
            )
            SimulationScenario.GNSS_LOSS,
            SimulationScenario.STREAM_GAP -> Triple(16f + 4f * wave, 0.18f * cos(t), 0.3f * wave)
        }

        val gnssState = when {
            selected == SimulationScenario.GNSS_LOSS && cycleSecondsInLoss(t) -> GnssState.STALE
            selected == SimulationScenario.IDLE && (sequence / 80L) % 3L == 2L -> GnssState.INVALID
            else -> GnssState.VALID
        }

        return TelemetrySample(
            sourceTimeMs = sequence * SAMPLE_PERIOD_MS,
            speedMps = speed,
            longitudinalG = longG,
            lateralG = lateralG,
            verticalG = 0.015f * sin(t * 2.2f),
            gnssState = gnssState,
            usedSatellites = if (gnssState == GnssState.VALID) 15 + (sequence % 4).toInt() else null,
            horizontalAccuracyM = if (gnssState == GnssState.VALID) 0.8f else null,
            speedAccuracyMps = if (gnssState == GnssState.VALID) 0.08f else null,
            sequence = sequence,
            imuValid = true,
            imuCalibrated = true,
        )
    }

    private fun cycleSecondsInLoss(t: Float): Boolean = t in 4f..9f

    companion object {
        const val SAMPLE_PERIOD_MS = 50L
    }
}
