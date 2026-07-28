package com.kernelai.app.ui

import androidx.compose.runtime.staticCompositionLocalOf

/**
 * UI 风格枚举 - Miuix / Material 双主题支持
 */
enum class UiMode(val value: String) {
    Miuix("miuix"),
    Material("material");

    companion object {
        fun fromValue(value: String): UiMode = when (value) {
            Material.value -> Material
            else -> Miuix
        }

        val DEFAULT_VALUE = Miuix.value
    }
}

val LocalUiMode = staticCompositionLocalOf { UiMode.Miuix }
