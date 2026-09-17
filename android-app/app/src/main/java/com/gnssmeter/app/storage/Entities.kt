package com.gnssmeter.app.storage

import androidx.room.Entity
import androidx.room.ForeignKey
import androidx.room.Index
import androidx.room.PrimaryKey

@Entity(tableName = "sessions")
data class SessionEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val startedAtEpochMs: Long,
    val endedAtEpochMs: Long? = null,
    val durationMs: Long = 0,
    val maxSpeedMps: Float = 0f,
    val maxLongitudinalG: Float = 0f,
    val maxLateralG: Float = 0f,
    val sourceName: String = "Симулятор",
)

@Entity(
    tableName = "samples",
    foreignKeys = [ForeignKey(
        entity = SessionEntity::class,
        parentColumns = ["id"],
        childColumns = ["sessionId"],
        onDelete = ForeignKey.CASCADE,
    )],
    indices = [Index("sessionId"), Index(value = ["sessionId", "sourceTimeMs"])],
)
data class TelemetrySampleEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val sessionId: Long,
    val sourceTimeMs: Long,
    val speedMps: Float,
    val longitudinalG: Float,
    val lateralG: Float,
    val verticalG: Float,
    val gnssState: String,
    val usedSatellites: Int?,
    val horizontalAccuracyM: Float?,
    val speedAccuracyMps: Float?,
    val sequence: Long,
    val imuValid: Boolean,
    val imuCalibrated: Boolean,
)
