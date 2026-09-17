package com.gnssmeter.app

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MainFlowTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()

    @Test fun recordingControlUpdatesUi() {
        compose.onNodeWithContentDescription("Начать запись").performClick()
        compose.waitUntil(5_000) {
            compose.onAllNodesWithContentDescription("Остановить запись")
                .fetchSemanticsNodes().isNotEmpty()
        }
        compose.onNodeWithContentDescription("Остановить запись").performClick()
    }

    @Test fun primaryScreensAreReachable() {
        compose.onNodeWithText("Диагностика").performClick()
        compose.onNodeWithText("Источник данных").assertIsDisplayed()
        compose.onNodeWithText("Прибор").performClick()
        compose.onNodeWithContentDescription("Круг перегрузок").assertIsDisplayed()
    }
}
