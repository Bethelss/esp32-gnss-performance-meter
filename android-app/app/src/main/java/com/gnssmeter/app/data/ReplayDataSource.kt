package com.gnssmeter.app.data

import com.gnssmeter.app.domain.ConnectionState
import com.gnssmeter.app.domain.TelemetryDataSource
import com.gnssmeter.app.domain.TelemetrySample
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow

class ReplayDataSource(
    private val recorded: List<TelemetrySample>,
) : TelemetryDataSource {
    private val _connectionState = MutableStateFlow(ConnectionState.STOPPED)
    override val connectionState: StateFlow<ConnectionState> = _connectionState
    val currentIndex = MutableStateFlow(0)
    val playing = MutableStateFlow(false)
    val speed = MutableStateFlow(1f)
    private var running = false

    override suspend fun start() {
        running = true
        playing.value = true
        _connectionState.value = ConnectionState.CONNECTED
    }

    override suspend fun stop() {
        running = false
        playing.value = false
        _connectionState.value = ConnectionState.STOPPED
    }

    fun pause() { playing.value = false }
    fun resume() { playing.value = true }
    fun setSpeed(value: Float) { speed.value = value.coerceIn(0.5f, 4f) }
    fun seekToFraction(fraction: Float) {
        currentIndex.value = ((recorded.lastIndex.coerceAtLeast(0)) * fraction.coerceIn(0f, 1f)).toInt()
    }

    override fun samples(): Flow<TelemetrySample> = flow {
        if (recorded.isEmpty()) return@flow
        while (running) {
            playing.first { it }
            val index = currentIndex.value.coerceIn(0, recorded.lastIndex)
            emit(recorded[index])
            if (index == recorded.lastIndex) {
                playing.value = false
                continue
            }
            val delta = (recorded[index + 1].sourceTimeMs - recorded[index].sourceTimeMs)
                .coerceIn(1L, 2_000L)
            delay((delta / speed.value).toLong().coerceAtLeast(1L))
            currentIndex.value = index + 1
        }
    }
}

/** Reserved integration seam. BLE permissions and scanning are intentionally absent in v0.1. */
interface BleTelemetryDataSource : TelemetryDataSource
