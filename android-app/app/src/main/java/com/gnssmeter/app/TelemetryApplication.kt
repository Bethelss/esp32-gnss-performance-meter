package com.gnssmeter.app

import android.app.Application
import androidx.room.Room
import com.gnssmeter.app.data.SimulatorDataSource
import com.gnssmeter.app.data.TelemetryRepository
import com.gnssmeter.app.storage.TelemetryDatabase

class TelemetryApplication : Application() {
    val database by lazy {
        Room.databaseBuilder(this, TelemetryDatabase::class.java, "telemetry.db").build()
    }
    val repository by lazy {
        TelemetryRepository(database.telemetryDao(), SimulatorDataSource())
    }
}
