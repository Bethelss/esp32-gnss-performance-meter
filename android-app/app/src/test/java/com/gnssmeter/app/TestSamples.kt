package com.gnssmeter.app

import com.gnssmeter.app.domain.GnssState
import com.gnssmeter.app.domain.TelemetrySample

fun sample(
    time: Long,
    sequence: Long = time,
    speed: Float = 0f,
    longitudinal: Float = 0f,
    lateral: Float = 0f,
) = TelemetrySample(
    sourceTimeMs = time,
    speedMps = speed,
    longitudinalG = longitudinal,
    lateralG = lateral,
    verticalG = 0f,
    gnssState = GnssState.VALID,
    usedSatellites = 15,
    horizontalAccuracyM = 0.8f,
    speedAccuracyMps = 0.08f,
    sequence = sequence,
    imuValid = true,
    imuCalibrated = true,
)
