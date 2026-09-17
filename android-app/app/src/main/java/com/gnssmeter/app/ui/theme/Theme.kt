package com.gnssmeter.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

private val DarkColors = darkColorScheme(
    primary = Color(0xFFFFA21A),
    onPrimary = Color(0xFF211300),
    secondary = Color(0xFF29B6D8),
    tertiary = Color(0xFF54E58B),
    background = Color(0xFF101113),
    surface = Color(0xFF1B1D20),
    surfaceVariant = Color(0xFF292C30),
    outline = Color(0xFF5A5F65),
)

private val LightColors = lightColorScheme(
    primary = Color(0xFF9A5000),
    secondary = Color(0xFF00677D),
    tertiary = Color(0xFF006C46),
    surfaceVariant = Color(0xFFE7E9EC),
)

private val GnssTypography = Typography(
    displayLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = (-1.5).sp,
    ),
    headlineMedium = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = (-0.4).sp,
    ),
    titleLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.SemiBold,
    ),
    bodyLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Normal,
    ),
    labelLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Bold,
        letterSpacing = 0.2.sp,
    ),
)

@Composable
fun GnssMeterTheme(
    darkTheme: Boolean = true,
    content: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (darkTheme) DarkColors else LightColors,
        typography = GnssTypography,
        content = content,
    )
}
