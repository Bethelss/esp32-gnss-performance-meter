package com.gnssmeter.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.gnssmeter.app.ui.DiagnosticsScreen
import com.gnssmeter.app.ui.HistoryScreen
import com.gnssmeter.app.ui.LiveScreen
import com.gnssmeter.app.ui.LiveDashboardState
import com.gnssmeter.app.ui.MainViewModel
import com.gnssmeter.app.ui.SessionScreen
import com.gnssmeter.app.ui.theme.GnssMeterTheme

class MainActivity : ComponentActivity() {
    private val viewModel: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            GnssMeterTheme { App(viewModel) }
        }
    }
}

private data class Destination(val route: String, val label: String, val glyph: String)

private val destinations = listOf(
    Destination("live", "Прибор", "●"),
    Destination("history", "Поездки", "▥"),
    Destination("diagnostics", "Диагностика", "i"),
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun App(viewModel: MainViewModel) {
    val nav = rememberNavController()
    val context = LocalContext.current
    val backStack by nav.currentBackStackEntryAsState()
    val route = backStack?.destination?.route.orEmpty()
    val isDetail = route.startsWith("session/")

    Scaffold(
        topBar = {
            TopAppBar(
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.background,
                ),
                title = {
                    Text(
                        if (isDetail) "Запись поездки" else "GNSS PERFORMANCE METER",
                        fontSize = 17.sp,
                        fontWeight = FontWeight.Bold,
                        letterSpacing = 0.8.sp,
                    )
                },
                navigationIcon = {
                    if (isDetail) TextButton(onClick = { nav.popBackStack() }) { Text("‹ Назад") }
                },
            )
        },
        bottomBar = {
            if (!isDetail) {
                NavigationBar(
                    containerColor = MaterialTheme.colorScheme.surface,
                    tonalElevation = 0.dp,
                ) {
                    destinations.forEach { destination ->
                        NavigationBarItem(
                            selected = route == destination.route,
                            onClick = {
                                nav.navigate(destination.route) {
                                    popUpTo("live") { saveState = true }
                                    launchSingleTop = true
                                    restoreState = true
                                }
                            },
                            icon = { Text(destination.glyph) },
                            label = { Text(destination.label) },
                            colors = NavigationBarItemDefaults.colors(
                                selectedIconColor = MaterialTheme.colorScheme.primary,
                                selectedTextColor = MaterialTheme.colorScheme.primary,
                                indicatorColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f),
                                unselectedIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                                unselectedTextColor = MaterialTheme.colorScheme.onSurfaceVariant,
                            ),
                        )
                    }
                }
            }
        },
    ) { padding ->
        NavHost(nav, startDestination = "live", modifier = Modifier.padding(padding)) {
            composable("live") {
                val sample by viewModel.latest.collectAsStateWithLifecycle()
                val trail by viewModel.trail.collectAsStateWithLifecycle()
                val connection by viewModel.connectionState.collectAsStateWithLifecycle()
                val scenario by viewModel.scenario.collectAsStateWithLifecycle()
                val recordingId by viewModel.recordingSessionId.collectAsStateWithLifecycle()
                LiveScreen(
                    state = LiveDashboardState(
                        sample = sample,
                        trail = trail,
                        connection = connection,
                        selectedScenario = scenario,
                        recording = recordingId != null,
                    ),
                    onScenario = viewModel::selectScenario,
                    onRecord = { viewModel.toggleRecording(context) },
                )
            }
            composable("history") {
                val sessions by viewModel.sessions.collectAsStateWithLifecycle()
                HistoryScreen(sessions) { id ->
                    viewModel.openSession(id)
                    nav.navigate("session/$id")
                }
            }
            composable("diagnostics") {
                val diagnostics by viewModel.diagnostics.collectAsStateWithLifecycle()
                val connection by viewModel.connectionState.collectAsStateWithLifecycle()
                val sample by viewModel.latest.collectAsStateWithLifecycle()
                DiagnosticsScreen(diagnostics, connection, sample)
            }
            composable(
                route = "session/{id}",
                arguments = listOf(navArgument("id") { type = NavType.LongType }),
            ) {
                val session by viewModel.selectedSession.collectAsStateWithLifecycle()
                val samples by viewModel.sessionSamples.collectAsStateWithLifecycle()
                val sample by viewModel.replaySample.collectAsStateWithLifecycle()
                val fraction by viewModel.replayFraction.collectAsStateWithLifecycle()
                val playing by viewModel.replayPlaying.collectAsStateWithLifecycle()
                val speed by viewModel.replaySpeed.collectAsStateWithLifecycle()
                SessionScreen(
                    session = session,
                    samples = samples,
                    sample = sample,
                    fraction = fraction,
                    playing = playing,
                    speed = speed,
                    onSeek = viewModel::seekReplay,
                    onTogglePlay = viewModel::toggleReplay,
                    onSpeed = viewModel::setReplaySpeed,
                    onExport = { deliver -> session?.let { viewModel.exportCsv(it.id, deliver) } },
                    onDelete = {
                        session?.let { viewModel.deleteSession(it.id) { nav.popBackStack() } }
                    },
                )
            }
        }
    }
}
