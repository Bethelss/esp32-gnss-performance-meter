package com.gnssmeter.app.ui

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import com.gnssmeter.app.domain.ConnectionState
import com.gnssmeter.app.domain.GnssState
import com.gnssmeter.app.domain.SimulationScenario
import com.gnssmeter.app.domain.TelemetrySample
import com.gnssmeter.app.ui.theme.GnssMeterTheme

private const val PHONE_360 = "spec:width=360dp,height=800dp,dpi=420"
private const val PHONE_430 = "spec:width=430dp,height=932dp,dpi=440"

private fun previewSample(
    speedKph: Float = 0f,
    longitudinalG: Float = 0f,
    lateralG: Float = 0f,
    gnssState: GnssState = GnssState.VALID,
    satellites: Int? = 18,
    sequence: Long = 100,
) = TelemetrySample(
    sourceTimeMs = sequence * 20L,
    speedMps = speedKph / 3.6f,
    longitudinalG = longitudinalG,
    lateralG = lateralG,
    verticalG = 1f,
    gnssState = gnssState,
    usedSatellites = satellites,
    horizontalAccuracyM = 0.7f,
    speedAccuracyMps = 0.05f,
    sequence = sequence,
    imuValid = true,
    imuCalibrated = true,
)

private fun previewState(
    scenario: SimulationScenario,
    recording: Boolean = false,
): LiveDashboardState {
    val sample = when (scenario) {
        SimulationScenario.IDLE -> previewSample()
        SimulationScenario.ACCELERATION -> previewSample(83.6f, longitudinalG = -0.62f)
        SimulationScenario.BRAKING -> previewSample(56.4f, longitudinalG = 0.78f)
        SimulationScenario.GENTLE_CORNER -> previewSample(71.2f, lateralG = 0.54f)
        SimulationScenario.SPORT_MANEUVER -> previewSample(104.8f, longitudinalG = -0.88f, lateralG = 0.91f)
        SimulationScenario.GNSS_LOSS -> previewSample(88.1f, lateralG = -0.42f, gnssState = GnssState.STALE, satellites = null)
        SimulationScenario.STREAM_GAP -> null
    }
    val trail = sample?.let { current ->
        (1L..12L).map { index ->
            current.copy(
                sourceTimeMs = current.sourceTimeMs - (12L - index) * 20L,
                lateralG = current.lateralG * index / 12f,
                longitudinalG = current.longitudinalG * index / 12f,
                sequence = current.sequence - (12L - index),
            )
        }
    }.orEmpty()
    return LiveDashboardState(
        sample = sample,
        trail = trail,
        connection = if (scenario == SimulationScenario.STREAM_GAP) ConnectionState.INTERRUPTED else ConnectionState.CONNECTED,
        selectedScenario = scenario,
        recording = recording,
    )
}

@Composable
private fun PreviewDashboard(
    initialScenario: SimulationScenario,
    initialRecording: Boolean = false,
    darkTheme: Boolean = true,
) {
    var state by remember(initialScenario, initialRecording) {
        mutableStateOf(previewState(initialScenario, initialRecording))
    }
    GnssMeterTheme(darkTheme = darkTheme) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
            LiveScreen(
                state = state,
                onScenario = { scenario -> state = previewState(scenario, state.recording) },
                onRecord = { state = state.copy(recording = !state.recording) },
            )
        }
    }
}

@Preview(name = "Interactive — 360 dp", device = PHONE_360, showBackground = true)
@Composable
private fun InteractivePreview() = PreviewDashboard(SimulationScenario.ACCELERATION)

@Preview(name = "Idle", device = PHONE_360, showBackground = true)
@Composable
private fun IdlePreview() = PreviewDashboard(SimulationScenario.IDLE)

@Preview(name = "Braking", device = PHONE_360, showBackground = true)
@Composable
private fun BrakingPreview() = PreviewDashboard(SimulationScenario.BRAKING)

@Preview(name = "Corner", device = PHONE_430, showBackground = true)
@Composable
private fun CornerPreview() = PreviewDashboard(SimulationScenario.GENTLE_CORNER)

@Preview(name = "Over 1 g + recording", device = PHONE_430, showBackground = true)
@Composable
private fun OverOneGPreview() = PreviewDashboard(SimulationScenario.SPORT_MANEUVER, initialRecording = true)

@Preview(name = "GNSS stale", device = PHONE_360, showBackground = true)
@Composable
private fun GnssStalePreview() = PreviewDashboard(SimulationScenario.GNSS_LOSS)

@Preview(name = "Stream gap", device = PHONE_360, showBackground = true)
@Composable
private fun StreamGapPreview() = PreviewDashboard(SimulationScenario.STREAM_GAP)

@Preview(name = "Light", device = PHONE_430, showBackground = true)
@Composable
private fun LightPreview() = PreviewDashboard(SimulationScenario.GENTLE_CORNER, darkTheme = false)

@Preview(name = "Large font", device = PHONE_430, fontScale = 1.3f, showBackground = true)
@Composable
private fun LargeFontPreview() = PreviewDashboard(SimulationScenario.ACCELERATION)
