package com.gnssmeter.app.ui

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.gnssmeter.app.domain.ConnectionState
import com.gnssmeter.app.domain.GnssState
import com.gnssmeter.app.domain.SimulationScenario
import com.gnssmeter.app.domain.StreamDiagnostics
import com.gnssmeter.app.domain.TelemetrySample
import com.gnssmeter.app.storage.SessionEntity
import java.text.DateFormat
import java.util.Date
import java.util.Locale

@Composable
fun LiveScreen(
    state: LiveDashboardState,
    onScenario: (SimulationScenario) -> Unit,
    onRecord: () -> Unit,
) {
    val sample = state.sample
    val trail = state.trail
    val connection = state.connection
    val selectedScenario = state.selectedScenario
    val recording = state.recording
    LazyColumn(
        modifier = Modifier.fillMaxSize().padding(horizontal = 16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(14.dp),
        contentPadding = PaddingValues(top = 10.dp, bottom = 24.dp),
    ) {
        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(28.dp),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            ) {
                Column(
                    Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 18.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                        Column {
                            Text("LIVE TELEMETRY", fontSize = 11.sp, letterSpacing = 1.5.sp, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            Text("СИМУЛЯЦИЯ", fontSize = 13.sp, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.tertiary)
                        }
                        Text("50 Hz", fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    Spacer(Modifier.height(6.dp))
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            text = String.format(Locale.US, "%.1f", (sample?.speedMps ?: 0f) * 3.6f),
                            fontSize = 70.sp,
                            lineHeight = 72.sp,
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.SemiBold,
                            letterSpacing = (-3).sp,
                        )
                        Text(" km/h", Modifier.padding(bottom = 12.dp), fontSize = 15.sp, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        StatusPill(gnssLabel(sample?.gnssState), gnssColor(sample?.gnssState), Modifier.weight(1f))
                        StatusPill("SV ${sample?.usedSatellites ?: "—"}", MaterialTheme.colorScheme.secondary, Modifier.weight(0.68f))
                        StatusPill(connectionLabel(connection), connectionColor(connection), Modifier.weight(1f))
                    }
                }
            }
        }
        item {
            GForceCircle(sample, trail, Modifier.size(300.dp).semantics { contentDescription = "Круг перегрузок" })
        }
        item {
            Button(
                onClick = onRecord,
                modifier = Modifier.fillMaxWidth().semantics {
                    contentDescription = if (recording) "Остановить запись" else "Начать запись"
                },
            ) { Text(if (recording) "Остановить запись" else "Начать запись", fontWeight = FontWeight.Bold) }
        }
        item {
            Text("Сценарий", modifier = Modifier.fillMaxWidth(), fontSize = 18.sp, fontWeight = FontWeight.Bold)
            LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(SimulationScenario.entries) { scenario ->
                    FilterChip(
                        selected = scenario == selectedScenario,
                        onClick = { onScenario(scenario) },
                        label = { Text(scenarioLabel(scenario)) },
                    )
                }
            }
        }
    }
}

@Composable
fun HistoryScreen(sessions: List<SessionEntity>, onOpen: (Long) -> Unit) {
    if (sessions.isEmpty()) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("Записей пока нет\nЗапустите симуляцию на главном экране", textAlign = TextAlign.Center)
        }
        return
    }
    LazyColumn(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        items(sessions, key = { it.id }) { session ->
            Card(Modifier.fillMaxWidth().clickable { onOpen(session.id) }) {
                Column(Modifier.padding(16.dp)) {
                    Text(DateFormat.getDateTimeInstance().format(Date(session.startedAtEpochMs)), fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(8.dp))
                    Text("${formatDuration(session.durationMs)}  •  ${(session.maxSpeedMps * 3.6f).oneDecimal()} км/ч")
                    Text("G: +/− ${session.maxLongitudinalG.oneDecimal()} прод.  •  ${session.maxLateralG.oneDecimal()} бок.")
                    Text(session.sourceName, color = MaterialTheme.colorScheme.tertiary)
                }
            }
        }
    }
}

@Composable
fun SessionScreen(
    session: SessionEntity?,
    samples: List<TelemetrySample>,
    sample: TelemetrySample?,
    fraction: Float,
    playing: Boolean,
    speed: Float,
    onSeek: (Float) -> Unit,
    onTogglePlay: () -> Unit,
    onSpeed: (Float) -> Unit,
    onExport: ((String) -> Unit) -> Unit,
    onDelete: () -> Unit,
) {
    val context = LocalContext.current
    var pendingCsv by remember { mutableStateOf<String?>(null) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/csv")) { uri ->
        val csv = pendingCsv
        if (uri != null && csv != null) writeText(context, uri, csv)
        pendingCsv = null
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
        Text(session?.let { DateFormat.getDateTimeInstance().format(Date(it.startedAtEpochMs)) } ?: "Сессия", fontWeight = FontWeight.Bold)
        GForceCircle(sample, emptyList(), Modifier.size(220.dp).align(Alignment.CenterHorizontally))
        Text("Скорость", fontWeight = FontWeight.Bold)
        TelemetryChart(samples, fraction, { it.speedMps * 3.6f }, MaterialTheme.colorScheme.secondary)
        Text("Продольная G", fontWeight = FontWeight.Bold)
        TelemetryChart(samples, fraction, { it.longitudinalG }, MaterialTheme.colorScheme.primary)
        Text("Боковая G", fontWeight = FontWeight.Bold)
        TelemetryChart(samples, fraction, { it.lateralG }, MaterialTheme.colorScheme.tertiary)
        Slider(value = fraction, onValueChange = onSeek, modifier = Modifier.semantics { contentDescription = "Позиция воспроизведения" })
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = onTogglePlay) { Text(if (playing) "Пауза" else "Играть") }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                listOf(0.5f, 1f, 2f, 4f).forEach { value ->
                    FilterChip(selected = speed == value, onClick = { onSpeed(value) }, label = { Text("${value}×") })
                }
            }
        }
        Spacer(Modifier.height(16.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(
                modifier = Modifier.weight(1f),
                onClick = { onExport { csv -> pendingCsv = csv; launcher.launch("gnss-session-${session?.id ?: 0}.csv") } },
            ) { Text("Экспорт CSV") }
            OutlinedButton(modifier = Modifier.weight(1f), onClick = onDelete) { Text("Удалить") }
        }
    }
}

@Composable
fun DiagnosticsScreen(
    diagnostics: StreamDiagnostics,
    connection: ConnectionState,
    sample: TelemetrySample?,
) {
    Column(Modifier.fillMaxSize().padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Источник данных", style = MaterialTheme.typography.titleLarge)
        DiagnosticRow("Источник", diagnostics.sourceName)
        DiagnosticRow("Соединение", connectionLabel(connection))
        DiagnosticRow("Частота", "${diagnostics.measuredHz.oneDecimal()} Гц")
        DiagnosticRow("Возраст образца", diagnostics.lastSampleAgeMs?.let { "$it мс" } ?: "N/A")
        DiagnosticRow("Получено", diagnostics.receivedSamples.toString())
        DiagnosticRow("Пропущено sequence", diagnostics.lostSamples.toString())
        Spacer(Modifier.height(12.dp))
        Text("Качество", style = MaterialTheme.typography.titleLarge)
        DiagnosticRow("GNSS", gnssLabel(sample?.gnssState))
        DiagnosticRow("IMU", if (sample?.imuValid == true) "VALID" else "N/A")
        DiagnosticRow("Калибровка", if (sample?.imuCalibrated == true) "OK" else "N/A")
        Text("Сейчас используются только синтетические данные.", color = MaterialTheme.colorScheme.tertiary)
    }
}

@Composable private fun StatusPill(text: String, color: Color, modifier: Modifier = Modifier) {
    Row(
        modifier.background(color.copy(alpha = 0.16f), CircleShape).padding(horizontal = 8.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(7.dp).background(color, CircleShape))
        Text("  $text", fontSize = 10.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

@Composable private fun DiagnosticRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, fontWeight = FontWeight.Bold)
    }
}

private fun writeText(context: Context, uri: Uri, text: String) {
    context.contentResolver.openOutputStream(uri)?.bufferedWriter()?.use { it.write(text) }
}

private fun gnssLabel(state: GnssState?) = when (state) {
    GnssState.VALID -> "GNSS READY"
    GnssState.STALE -> "GNSS STALE"
    GnssState.INVALID -> "GNSS INVALID"
    else -> "GNSS N/A"
}

private fun gnssColor(state: GnssState?) = when (state) {
    GnssState.VALID -> Color(0xFF57E389)
    GnssState.STALE -> Color(0xFFFFB86B)
    GnssState.INVALID -> Color(0xFFFF6B6B)
    else -> Color.Gray
}

private fun connectionLabel(state: ConnectionState) = when (state) {
    ConnectionState.CONNECTED -> "ПОТОК OK"
    ConnectionState.INTERRUPTED -> "РАЗРЫВ"
    ConnectionState.CONNECTING -> "ПОДКЛЮЧЕНИЕ"
    ConnectionState.STOPPED -> "ОСТАНОВЛЕН"
}

private fun connectionColor(state: ConnectionState) = when (state) {
    ConnectionState.CONNECTED -> Color(0xFF64D8FF)
    ConnectionState.INTERRUPTED -> Color(0xFFFF6B6B)
    else -> Color.Gray
}

private fun scenarioLabel(scenario: SimulationScenario) = when (scenario) {
    SimulationScenario.IDLE -> "Покой"
    SimulationScenario.ACCELERATION -> "Разгон"
    SimulationScenario.BRAKING -> "Торможение"
    SimulationScenario.GENTLE_CORNER -> "Поворот"
    SimulationScenario.SPORT_MANEUVER -> "Манёвр"
    SimulationScenario.GNSS_LOSS -> "Потеря GNSS"
    SimulationScenario.STREAM_GAP -> "Разрыв потока"
}

private fun Float.oneDecimal() = String.format(Locale.getDefault(), "%.1f", this)
private fun formatDuration(ms: Long): String = "%d:%02d".format(ms / 60_000, (ms / 1_000) % 60)
