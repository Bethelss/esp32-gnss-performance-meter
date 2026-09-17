package com.gnssmeter.app.storage

import androidx.room.Room
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TelemetryDatabaseTest {
    private lateinit var db: TelemetryDatabase

    @Before fun createDatabase() {
        db = Room.inMemoryDatabaseBuilder(
            ApplicationProvider.getApplicationContext(),
            TelemetryDatabase::class.java,
        ).build()
    }

    @After fun closeDatabase() = db.close()

    @Test fun sessionAndSamplesRoundTripInTimeOrder() = runBlocking {
        val id = db.telemetryDao().insertSession(SessionEntity(startedAtEpochMs = 123L))
        db.telemetryDao().insertSamples(listOf(stored(id, 100L, 2L), stored(id, 50L, 1L)))
        assertEquals(listOf(1L, 2L), db.telemetryDao().samples(id).map { it.sequence })
    }

    private fun stored(sessionId: Long, time: Long, sequence: Long) = TelemetrySampleEntity(
        sessionId = sessionId,
        sourceTimeMs = time,
        speedMps = 1f,
        longitudinalG = 0f,
        lateralG = 0f,
        verticalG = 0f,
        gnssState = "VALID",
        usedSatellites = 12,
        horizontalAccuracyM = 1f,
        speedAccuracyMps = 0.1f,
        sequence = sequence,
        imuValid = true,
        imuCalibrated = true,
    )
}
