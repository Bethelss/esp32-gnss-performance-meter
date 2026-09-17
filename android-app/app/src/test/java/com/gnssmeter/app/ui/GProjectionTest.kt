package com.gnssmeter.app.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GProjectionTest {
    @Test
    fun fourDirectionsKeepTheirOrientation() {
        assertEquals(1f, projectG(1f, 0f).x, 0.0001f)
        assertEquals(-1f, projectG(-1f, 0f).x, 0.0001f)
        assertEquals(1f, projectG(0f, 1f).y, 0.0001f)
        assertEquals(-1f, projectG(0f, -1f).y, 0.0001f)
        assertFalse(projectG(0.5f, 0.5f).outside)
    }

    @Test
    fun valuesAboveOneGAreMarkedAndClippedToTheRing() {
        val projection = projectG(1.2f, 1.6f)

        assertTrue(projection.outside)
        assertEquals(2f, projection.magnitude, 0.0001f)
        assertEquals(1f, kotlin.math.sqrt(projection.x * projection.x + projection.y * projection.y), 0.0001f)
    }
}
