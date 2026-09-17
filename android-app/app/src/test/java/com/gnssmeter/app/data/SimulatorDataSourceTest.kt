package com.gnssmeter.app.data

import com.gnssmeter.app.domain.GnssState
import com.gnssmeter.app.domain.SimulationScenario
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class SimulatorDataSourceTest {
    @Test fun streamIsDeterministicAndRunsAtTwentyHertz() = runTest {
        val source = SimulatorDataSource()
        source.start()
        val values = async { source.samples().take(3).toList() }.await()
        assertEquals(listOf(0L, 50L, 100L), values.map { it.sourceTimeMs })
        assertEquals(listOf(0L, 1L, 2L), values.map { it.sequence })
    }

    @Test fun gnssLossKeepsImuDataValid() {
        val value = SimulatorDataSource().sampleFor(SimulationScenario.GNSS_LOSS, 100L, 5f)
        assertEquals(GnssState.STALE, value.gnssState)
        assertTrue(value.imuValid)
    }
}
