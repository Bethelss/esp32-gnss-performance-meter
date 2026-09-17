package com.gnssmeter.app.data

import com.gnssmeter.app.domain.TelemetrySample
import kotlin.math.abs
import kotlin.math.max

class SessionAccumulator {
    var firstTimeMs: Long? = null
        private set
    var lastTimeMs: Long? = null
        private set
    var maxSpeedMps: Float = 0f
        private set
    var maxLongitudinalG: Float = 0f
        private set
    var maxLateralG: Float = 0f
        private set

    val durationMs: Long
        get() = ((lastTimeMs ?: firstTimeMs ?: 0L) - (firstTimeMs ?: 0L)).coerceAtLeast(0L)

    fun accept(sample: TelemetrySample) {
        if (firstTimeMs == null) firstTimeMs = sample.sourceTimeMs
        lastTimeMs = sample.sourceTimeMs
        maxSpeedMps = max(maxSpeedMps, sample.speedMps)
        maxLongitudinalG = max(maxLongitudinalG, abs(sample.longitudinalG))
        maxLateralG = max(maxLateralG, abs(sample.lateralG))
    }
}

class SequenceTracker {
    var lostSamples: Long = 0
        private set
    private var previous: Long? = null

    fun accept(sequence: Long) {
        val before = previous
        if (before != null && sequence > before + 1) lostSamples += sequence - before - 1
        previous = sequence
    }
}
