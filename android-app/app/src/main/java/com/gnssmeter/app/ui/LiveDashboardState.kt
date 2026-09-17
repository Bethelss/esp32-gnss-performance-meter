package com.gnssmeter.app.ui

import androidx.compose.runtime.Immutable
import com.gnssmeter.app.domain.ConnectionState
import com.gnssmeter.app.domain.SimulationScenario
import com.gnssmeter.app.domain.TelemetrySample

@Immutable
data class LiveDashboardState(
    val sample: TelemetrySample? = null,
    val trail: List<TelemetrySample> = emptyList(),
    val connection: ConnectionState = ConnectionState.STOPPED,
    val selectedScenario: SimulationScenario = SimulationScenario.IDLE,
    val recording: Boolean = false,
)
