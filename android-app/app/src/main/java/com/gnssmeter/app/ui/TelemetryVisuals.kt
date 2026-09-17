package com.gnssmeter.app.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.gnssmeter.app.domain.TelemetrySample
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.sqrt

private val BeamPanel = Color(0xF21C1E21)
private val BeamPanelBorder = Color(0xFF494D52)
private val BeamGrid = Color(0xFF9DA5AD)
private val BeamBlue = Color(0xFF29B6D8)
private val BeamOrange = Color(0xFFFFA21A)
private val BeamGreen = Color(0xFF54E58B)
private val BeamRed = Color(0xFFFF5B3F)
private val TelemetryFont = FontFamily.Monospace

@Composable
fun GForceCircle(
    sample: TelemetrySample?,
    trail: List<TelemetrySample>,
    modifier: Modifier = Modifier,
) {
    val targetX = sample?.lateralG ?: 0f
    val targetY = sample?.longitudinalG ?: 0f
    val x by animateFloatAsState(targetX, label = "lateralG")
    val y by animateFloatAsState(targetY, label = "longitudinalG")
    val magnitude = sqrt(targetX * targetX + targetY * targetY)
    val outside = magnitude > 1f

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(20.dp))
            .background(
                Brush.verticalGradient(
                    listOf(Color(0xFF25282C), BeamPanel, Color(0xF218191B)),
                ),
            ),
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val inset = 13.dp.toPx()
            val headerSpace = 42.dp.toPx()
            val footerSpace = 26.dp.toPx()
            val radius = minOf(
                (size.width - inset * 2f) / 2f,
                (size.height - headerSpace - footerSpace) / 2f,
            )
            val center = Offset(size.width / 2f, headerSpace + radius)
            val gridStroke = 1.dp.toPx()

            drawRoundRect(
                color = BeamPanelBorder.copy(alpha = 0.82f),
                cornerRadius = CornerRadius(20.dp.toPx()),
                style = Stroke(1.dp.toPx()),
            )
            drawLine(
                color = BeamOrange,
                start = Offset(18.dp.toPx(), 1.5.dp.toPx()),
                end = Offset(size.width - 18.dp.toPx(), 1.5.dp.toPx()),
                strokeWidth = 3.dp.toPx(),
                cap = StrokeCap.Round,
            )
            drawLine(
                color = Color.White.copy(alpha = 0.08f),
                start = Offset(16.dp.toPx(), headerSpace - 4.dp.toPx()),
                end = Offset(size.width - 16.dp.toPx(), headerSpace - 4.dp.toPx()),
                strokeWidth = 1.dp.toPx(),
            )

            drawCircle(
                color = BeamGrid.copy(alpha = 0.78f),
                radius = radius,
                center = center,
                style = Stroke(2.dp.toPx()),
            )
            drawCircle(
                color = BeamGrid.copy(alpha = 0.32f),
                radius = radius * 0.5f,
                center = center,
                style = Stroke(
                    width = gridStroke,
                    pathEffect = PathEffect.dashPathEffect(floatArrayOf(6.dp.toPx(), 5.dp.toPx())),
                ),
            )
            drawLine(
                BeamGrid.copy(alpha = 0.42f),
                Offset(center.x - radius, center.y),
                Offset(center.x + radius, center.y),
                gridStroke,
            )
            drawLine(
                BeamGrid.copy(alpha = 0.42f),
                Offset(center.x, center.y - radius),
                Offset(center.x, center.y + radius),
                gridStroke,
            )

            listOf(-0.5f, 0.5f).forEach { fraction ->
                val tick = 5.dp.toPx()
                val horizontalX = center.x + radius * fraction
                val verticalY = center.y + radius * fraction
                drawLine(
                    BeamGrid.copy(alpha = 0.7f),
                    Offset(horizontalX, center.y - tick),
                    Offset(horizontalX, center.y + tick),
                    gridStroke,
                )
                drawLine(
                    BeamGrid.copy(alpha = 0.7f),
                    Offset(center.x - tick, verticalY),
                    Offset(center.x + tick, verticalY),
                    gridStroke,
                )
            }

            val visibleTrail = trail.takeLast(24)
            visibleTrail.zipWithNext().forEachIndexed { index, (from, to) ->
                if (to.sourceTimeMs - from.sourceTimeMs <= 250L) {
                    val alpha = 0.12f + 0.68f * (index + 1f) / visibleTrail.size.coerceAtLeast(2)
                    drawLine(
                        color = BeamOrange.copy(alpha = alpha),
                        start = gPoint(center, radius, from.lateralG, from.longitudinalG),
                        end = gPoint(center, radius, to.lateralG, to.longitudinalG),
                        strokeWidth = 2.5.dp.toPx(),
                        cap = StrokeCap.Round,
                    )
                }
            }

            val point = gPoint(center, radius, x, y)
            val verticalComponent = Offset(center.x, point.y)
            drawLine(
                color = BeamBlue.copy(alpha = 0.82f),
                start = center,
                end = verticalComponent,
                strokeWidth = 3.dp.toPx(),
                cap = StrokeCap.Round,
            )
            drawLine(
                color = BeamBlue,
                start = verticalComponent,
                end = point,
                strokeWidth = 3.dp.toPx(),
                cap = StrokeCap.Round,
            )

            val activeColor = if (outside) BeamRed else BeamGreen
            drawCircle(activeColor.copy(alpha = 0.13f), 18.dp.toPx(), point)
            drawCircle(activeColor.copy(alpha = 0.28f), 12.dp.toPx(), point)
            drawCircle(activeColor, 7.dp.toPx(), point)
            drawCircle(Color.White.copy(alpha = 0.88f), 2.dp.toPx(), point)
            if (outside) {
                drawCircle(
                    color = BeamOrange,
                    radius = 14.dp.toPx(),
                    center = point,
                    style = Stroke(2.dp.toPx()),
                )
            }

            drawCircle(BeamGrid.copy(alpha = 0.9f), 3.dp.toPx(), center, style = Stroke(1.dp.toPx()))
        }

        Row(
            Modifier.align(Alignment.TopCenter).fillMaxWidth().padding(horizontal = 14.dp, vertical = 9.dp),
            horizontalArrangement = androidx.compose.foundation.layout.Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("G-FORCE", color = Color.White, fontSize = 12.sp, fontWeight = FontWeight.Black)
            Text(
                text = String.format(java.util.Locale.US, "%.2f g", magnitude),
                color = if (outside) BeamOrange else Color.White,
                fontSize = 13.sp,
                fontWeight = FontWeight.Bold,
            )
        }
        Text("0.5", Modifier.align(Alignment.Center).padding(start = 76.dp, bottom = 13.dp), color = BeamGrid.copy(alpha = 0.7f), fontSize = 8.sp, fontFamily = TelemetryFont)
        Text(
            text = formatG(sample?.lateralG),
            modifier = Modifier.align(Alignment.CenterEnd).padding(end = 11.dp, bottom = 31.dp),
            color = BeamBlue,
            fontSize = 11.sp,
            fontFamily = TelemetryFont,
            fontWeight = FontWeight.Bold,
        )
        Text(
            text = formatG(sample?.longitudinalG),
            modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 22.dp),
            color = BeamBlue,
            fontSize = 11.sp,
            fontFamily = TelemetryFont,
            fontWeight = FontWeight.Bold,
        )
    }
}

internal data class GProjection(
    val x: Float,
    val y: Float,
    val magnitude: Float,
    val outside: Boolean,
)

internal fun projectG(lateral: Float, longitudinal: Float, limitG: Float = 1f): GProjection {
    val magnitude = sqrt(lateral * lateral + longitudinal * longitudinal)
    val safeLimit = limitG.coerceAtLeast(0.001f)
    val scale = if (magnitude > safeLimit) safeLimit / magnitude else 1f
    return GProjection(
        x = lateral * scale / safeLimit,
        y = longitudinal * scale / safeLimit,
        magnitude = magnitude,
        outside = magnitude > safeLimit,
    )
}

private fun gPoint(center: Offset, radius: Float, lateral: Float, longitudinal: Float): Offset {
    val projection = projectG(lateral, longitudinal)
    return Offset(
        // Mirror the raw sensor axes into the vehicle-facing HUD orientation.
        x = center.x - projection.x * radius,
        y = center.y + projection.y * radius,
    )
}

private fun formatG(value: Float?): String = value?.let {
    String.format(java.util.Locale.US, "%+.2f g", it)
} ?: "—"

@Composable
fun TelemetryChart(
    samples: List<TelemetrySample>,
    cursorFraction: Float,
    value: (TelemetrySample) -> Float,
    color: Color,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier.fillMaxWidth().height(132.dp).padding(vertical = 8.dp)) {
        if (samples.size < 2) return@Canvas
        val reduced = minMaxDecimate(samples, size.width.toInt().coerceAtLeast(40), value)
        val firstTime = samples.first().sourceTimeMs
        val duration = (samples.last().sourceTimeMs - firstTime).coerceAtLeast(1L)
        val maxAbs = reduced.maxOf { abs(value(it)) }.coerceAtLeast(0.1f)
        val centerY = size.height / 2f
        drawLine(Color.White.copy(alpha = 0.15f), Offset(0f, centerY), Offset(size.width, centerY))

        var path = Path()
        var previous: TelemetrySample? = null
        reduced.forEach { sample ->
            val px = (sample.sourceTimeMs - firstTime).toFloat() / duration * size.width
            val py = centerY - value(sample) / maxAbs * centerY * 0.88f
            val gap = previous?.let { sample.sourceTimeMs - it.sourceTimeMs > 250L } ?: true
            if (gap) {
                if (!path.isEmpty) drawPath(path, color, style = Stroke(2.dp.toPx()))
                path = Path().apply { moveTo(px, py) }
            } else path.lineTo(px, py)
            previous = sample
        }
        if (!path.isEmpty) drawPath(path, color, style = Stroke(2.dp.toPx()))

        val cursorX = cursorFraction.coerceIn(0f, 1f) * size.width
        drawLine(Color.White.copy(alpha = 0.7f), Offset(cursorX, 0f), Offset(cursorX, size.height), 1.dp.toPx())
    }
}
