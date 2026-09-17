package com.gnssmeter.app.data

import android.os.SystemClock
import com.gnssmeter.app.domain.ConnectionState
import com.gnssmeter.app.domain.GnssState
import com.gnssmeter.app.domain.SimulationScenario
import com.gnssmeter.app.domain.StreamDiagnostics
import com.gnssmeter.app.domain.TelemetrySample
import com.gnssmeter.app.storage.SessionEntity
import com.gnssmeter.app.storage.TelemetryDao
import com.gnssmeter.app.storage.TelemetrySampleEntity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.Locale

class TelemetryRepository(
    private val dao: TelemetryDao,
    val simulator: SimulatorDataSource,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val recordMutex = Mutex()
    private val _latest = MutableStateFlow<TelemetrySample?>(null)
    val latest: StateFlow<TelemetrySample?> = _latest.asStateFlow()
    val connectionState: StateFlow<ConnectionState> = simulator.connectionState
    val sessions = dao.observeSessions().stateIn(scope, SharingStarted.Eagerly, emptyList())

    private val _recordingSessionId = MutableStateFlow<Long?>(null)
    val recordingSessionId: StateFlow<Long?> = _recordingSessionId.asStateFlow()
    private val _diagnostics = MutableStateFlow(StreamDiagnostics())
    val diagnostics: StateFlow<StreamDiagnostics> = _diagnostics.asStateFlow()

    private val sequenceTracker = SequenceTracker()
    private var lastArrivalElapsedMs: Long? = null
    private val arrivals = ArrayDeque<Long>()
    private var received = 0L
    private var lost = 0L
    private var recording: ActiveRecording? = null

    init {
        scope.launch {
            simulator.start()
            simulator.samples().collect(::accept)
        }
        scope.launch {
            while (true) {
                updateDiagnostics()
                delay(250)
            }
        }
    }

    fun selectScenario(scenario: SimulationScenario) = simulator.select(scenario)

    suspend fun startRecording(): Long = recordMutex.withLock {
        recording?.sessionId ?: dao.insertSession(
            SessionEntity(startedAtEpochMs = System.currentTimeMillis())
        ).also { id ->
            recording = ActiveRecording(sessionId = id)
            _recordingSessionId.value = id
        }
    }

    suspend fun stopRecording(): Long? = recordMutex.withLock {
        val active = recording ?: return@withLock null
        flush(active)
        val original = dao.session(active.sessionId) ?: return@withLock null
        dao.updateSession(
            original.copy(
                endedAtEpochMs = System.currentTimeMillis(),
                durationMs = active.accumulator.durationMs,
                maxSpeedMps = active.accumulator.maxSpeedMps,
                maxLongitudinalG = active.accumulator.maxLongitudinalG,
                maxLateralG = active.accumulator.maxLateralG,
            )
        )
        recording = null
        _recordingSessionId.value = null
        active.sessionId
    }

    suspend fun session(id: Long): SessionEntity? = dao.session(id)
    suspend fun samples(id: Long): List<TelemetrySample> = dao.samples(id).map { it.toDomain() }
    suspend fun deleteSession(id: Long) = dao.deleteSession(id)

    suspend fun exportCsv(id: Long): String {
        val rows = samples(id)
        return buildString {
            appendLine("source_time_ms,speed_mps,longitudinal_g,lateral_g,vertical_g,gnss_state,used_satellites,sequence,imu_valid,imu_calibrated")
            rows.forEach { s ->
                append(s.sourceTimeMs).append(',')
                append(format(s.speedMps)).append(',')
                append(format(s.longitudinalG)).append(',')
                append(format(s.lateralG)).append(',')
                append(format(s.verticalG)).append(',')
                append(s.gnssState.name.lowercase()).append(',')
                append(s.usedSatellites ?: "").append(',')
                append(s.sequence).append(',')
                append(s.imuValid).append(',').appendLine(s.imuCalibrated)
            }
        }
    }

    private suspend fun accept(sample: TelemetrySample) {
        val now = SystemClock.elapsedRealtime()
        sequenceTracker.accept(sample.sequence)
        lost = sequenceTracker.lostSamples
        lastArrivalElapsedMs = now
        received++
        arrivals.addLast(now)
        while (arrivals.isNotEmpty() && now - arrivals.first() > 2_000L) arrivals.removeFirst()
        _latest.value = sample

        recordMutex.withLock {
            recording?.let { active ->
                active.accumulator.accept(sample)
                active.buffer += sample.toEntity(active.sessionId)
                if (active.buffer.size >= BATCH_SIZE) flush(active)
            }
        }
        updateDiagnostics()
    }

    private fun updateDiagnostics() {
        val now = SystemClock.elapsedRealtime()
        while (arrivals.isNotEmpty() && now - arrivals.first() > 2_000L) arrivals.removeFirst()
        val hz = if (arrivals.size > 1) {
            (arrivals.size - 1) * 1000f / (arrivals.last() - arrivals.first()).coerceAtLeast(1L)
        } else 0f
        _diagnostics.value = StreamDiagnostics(
            measuredHz = hz,
            lastSampleAgeMs = lastArrivalElapsedMs?.let { now - it },
            lostSamples = lost,
            receivedSamples = received,
        )
    }

    private suspend fun flush(active: ActiveRecording) {
        if (active.buffer.isEmpty()) return
        val batch = active.buffer.toList()
        active.buffer.clear()
        dao.insertSamples(batch)
    }

    private fun format(value: Float): String = String.format(Locale.US, "%.5f", value)

    private data class ActiveRecording(
        val sessionId: Long,
        val accumulator: SessionAccumulator = SessionAccumulator(),
        val buffer: MutableList<TelemetrySampleEntity> = mutableListOf(),
    )

    companion object { private const val BATCH_SIZE = 50 }
}

private fun TelemetrySample.toEntity(sessionId: Long) = TelemetrySampleEntity(
    sessionId = sessionId,
    sourceTimeMs = sourceTimeMs,
    speedMps = speedMps,
    longitudinalG = longitudinalG,
    lateralG = lateralG,
    verticalG = verticalG,
    gnssState = gnssState.name,
    usedSatellites = usedSatellites,
    horizontalAccuracyM = horizontalAccuracyM,
    speedAccuracyMps = speedAccuracyMps,
    sequence = sequence,
    imuValid = imuValid,
    imuCalibrated = imuCalibrated,
)

private fun TelemetrySampleEntity.toDomain() = TelemetrySample(
    sourceTimeMs = sourceTimeMs,
    speedMps = speedMps,
    longitudinalG = longitudinalG,
    lateralG = lateralG,
    verticalG = verticalG,
    gnssState = runCatching { GnssState.valueOf(gnssState) }.getOrDefault(GnssState.UNAVAILABLE),
    usedSatellites = usedSatellites,
    horizontalAccuracyM = horizontalAccuracyM,
    speedAccuracyMps = speedAccuracyMps,
    sequence = sequence,
    imuValid = imuValid,
    imuCalibrated = imuCalibrated,
)
