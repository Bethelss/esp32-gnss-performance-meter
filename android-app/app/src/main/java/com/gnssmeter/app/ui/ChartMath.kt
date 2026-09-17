package com.gnssmeter.app.ui

import com.gnssmeter.app.domain.TelemetrySample

fun minMaxDecimate(
    samples: List<TelemetrySample>,
    maxPoints: Int,
    value: (TelemetrySample) -> Float,
): List<TelemetrySample> {
    if (maxPoints < 4 || samples.size <= maxPoints) return samples
    val bucketSize = (samples.size.toFloat() / (maxPoints / 2)).toInt().coerceAtLeast(1)
    val result = ArrayList<TelemetrySample>(maxPoints + 2)
    var start = 0
    while (start < samples.size) {
        val end = (start + bucketSize).coerceAtMost(samples.size)
        val bucket = samples.subList(start, end)
        val min = bucket.minBy(value)
        val max = bucket.maxBy(value)
        if (min.sourceTimeMs <= max.sourceTimeMs) {
            result += min
            if (max !== min) result += max
        } else {
            result += max
            if (max !== min) result += min
        }
        start = end
    }
    return result
}
