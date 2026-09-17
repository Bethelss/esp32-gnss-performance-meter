package com.gnssmeter.app.data

import com.gnssmeter.app.sample
import org.junit.Assert.assertEquals
import org.junit.Test

class TelemetryMathTest {
    @Test fun accumulatorUsesAbsoluteGExtremaAndSourceDuration() {
        val accumulator = SessionAccumulator()
        accumulator.accept(sample(1_000, speed = 10f, longitudinal = -0.7f, lateral = 0.2f))
        accumulator.accept(sample(1_250, speed = 20f, longitudinal = 0.4f, lateral = -1.1f))
        assertEquals(250L, accumulator.durationMs)
        assertEquals(20f, accumulator.maxSpeedMps)
        assertEquals(0.7f, accumulator.maxLongitudinalG)
        assertEquals(1.1f, accumulator.maxLateralG)
    }

    @Test fun sequenceTrackerCountsOnlyForwardGaps() {
        val tracker = SequenceTracker()
        listOf(10L, 11L, 15L, 0L, 1L).forEach(tracker::accept)
        assertEquals(3L, tracker.lostSamples)
    }
}
