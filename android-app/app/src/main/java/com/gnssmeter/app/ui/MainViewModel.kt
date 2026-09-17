package com.gnssmeter.app.ui

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.gnssmeter.app.TelemetryApplication
import com.gnssmeter.app.data.ReplayDataSource
import com.gnssmeter.app.domain.SimulationScenario
import com.gnssmeter.app.domain.TelemetrySample
import com.gnssmeter.app.recording.RecordingService
import com.gnssmeter.app.storage.SessionEntity
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

class MainViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = (application as TelemetryApplication).repository
    val latest = repository.latest
    val connectionState = repository.connectionState
    val diagnostics = repository.diagnostics
    val sessions = repository.sessions
    val recordingSessionId = repository.recordingSessionId
    val scenario: StateFlow<SimulationScenario> = repository.simulator.scenario

    private val _trail = MutableStateFlow<List<TelemetrySample>>(emptyList())
    val trail = _trail.asStateFlow()
    private val _selectedSession = MutableStateFlow<SessionEntity?>(null)
    val selectedSession = _selectedSession.asStateFlow()
    private val _sessionSamples = MutableStateFlow<List<TelemetrySample>>(emptyList())
    val sessionSamples = _sessionSamples.asStateFlow()
    private val _replaySample = MutableStateFlow<TelemetrySample?>(null)
    val replaySample = _replaySample.asStateFlow()
    private val _replayFraction = MutableStateFlow(0f)
    val replayFraction = _replayFraction.asStateFlow()
    private val _replayPlaying = MutableStateFlow(false)
    val replayPlaying = _replayPlaying.asStateFlow()
    private val _replaySpeed = MutableStateFlow(1f)
    val replaySpeed = _replaySpeed.asStateFlow()

    private var replay: ReplayDataSource? = null
    private var replayJob: Job? = null

    init {
        viewModelScope.launch {
            latest.collectLatest { sample ->
                if (sample != null) _trail.value = (_trail.value + sample).takeLast(24)
            }
        }
    }

    fun selectScenario(value: SimulationScenario) {
        _trail.value = emptyList()
        repository.selectScenario(value)
    }

    fun toggleRecording(context: Context) {
        if (recordingSessionId.value == null) RecordingService.start(context)
        else RecordingService.stop(context)
    }

    fun openSession(id: Long) {
        replayJob?.cancel()
        viewModelScope.launch {
            _selectedSession.value = repository.session(id)
            val values = repository.samples(id)
            _sessionSamples.value = values
            _replaySample.value = values.firstOrNull()
            _replayFraction.value = 0f
            val source = ReplayDataSource(values)
            replay = source
            replayJob = viewModelScope.launch {
                launch { source.currentIndex.collect { updateReplayPosition(it) } }
                launch { source.playing.collect { _replayPlaying.value = it } }
                source.start()
                source.samples().collect { _replaySample.value = it }
            }
        }
    }

    fun toggleReplay() {
        replay?.let { if (it.playing.value) it.pause() else it.resume() }
    }

    fun seekReplay(fraction: Float) {
        replay?.seekToFraction(fraction)
        val values = _sessionSamples.value
        if (values.isNotEmpty()) {
            val index = (values.lastIndex * fraction.coerceIn(0f, 1f)).toInt()
            _replaySample.value = values[index]
            updateReplayPosition(index)
        }
    }

    fun setReplaySpeed(value: Float) {
        replay?.setSpeed(value)
        _replaySpeed.value = value
    }

    fun exportCsv(id: Long, deliver: (String) -> Unit) {
        viewModelScope.launch { deliver(repository.exportCsv(id)) }
    }

    fun deleteSession(id: Long, after: () -> Unit) {
        viewModelScope.launch {
            repository.deleteSession(id)
            after()
        }
    }

    private fun updateReplayPosition(index: Int) {
        val last = _sessionSamples.value.lastIndex
        _replayFraction.value = if (last > 0) index.toFloat() / last else 0f
    }
}
