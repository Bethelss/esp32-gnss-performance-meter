package com.gnssmeter.app.storage

import androidx.room.Database
import androidx.room.RoomDatabase

@Database(
    entities = [SessionEntity::class, TelemetrySampleEntity::class],
    version = 1,
    exportSchema = true,
)
abstract class TelemetryDatabase : RoomDatabase() {
    abstract fun telemetryDao(): TelemetryDao
}
