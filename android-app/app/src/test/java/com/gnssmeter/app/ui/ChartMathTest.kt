package com.gnssmeter.app.ui

import com.gnssmeter.app.sample
import org.junit.Assert.assertTrue
import org.junit.Test

class ChartMathTest {
    @Test fun decimationPreservesLocalExtremaAndOrder() {
        val values = (0 until 1_000).map { index ->
            sample(index.toLong(), longitudinal = when (index) { 410 -> -2f; 411 -> 3f; else -> 0.1f })
        }
        val reduced = minMaxDecimate(values, 100) { it.longitudinalG }
        assertTrue(reduced.any { it.longitudinalG == -2f })
        assertTrue(reduced.any { it.longitudinalG == 3f })
        assertTrue(reduced.zipWithNext().all { (a, b) -> a.sourceTimeMs <= b.sourceTimeMs })
    }
}
