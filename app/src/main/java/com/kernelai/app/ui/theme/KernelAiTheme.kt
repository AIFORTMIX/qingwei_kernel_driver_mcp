package com.kernelai.app.ui.theme

import android.app.Activity
import android.content.Context
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.core.view.WindowInsetsControllerCompat
import com.kernelai.app.data.model.ThemeMode
import com.kernelai.app.ui.LocalUiMode
import com.kernelai.app.ui.UiMode
import top.yukonga.miuix.kmp.theme.ColorSchemeMode
import top.yukonga.miuix.kmp.theme.LocalContentColor
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.ThemeController
import top.yukonga.miuix.kmp.theme.ThemePaletteStyle
import top.yukonga.miuix.kmp.theme.ThemeColorSpec

/**
 * 轻微 MCP 颜色模式
 */
enum class KernelAiColorMode(val value: Int) {
    SYSTEM(0),
    LIGHT(1),
    DARK(2);

    companion object {
        fun fromValue(value: Int) = entries.find { it.value == value } ?: SYSTEM
        fun fromThemeMode(mode: ThemeMode) = when (mode) {
            ThemeMode.SYSTEM -> SYSTEM
            ThemeMode.LIGHT -> LIGHT
            ThemeMode.DARK -> DARK
        }
    }

    val isSystem: Boolean get() = value == 0
    val isDark: Boolean get() = value == 2
}

/**
 * 轻微 MCP 主题设置
 */
data class KernelAiAppSettings(
    val colorMode: KernelAiColorMode = KernelAiColorMode.SYSTEM,
    val keyColor: Int = 0,
)

/**
 * 主题控制器 - 从 SharedPreferences 读取/保存设置
 */
object KernelAiThemeController {
    fun getAppSettings(context: Context): KernelAiAppSettings {
        val prefs = context.getSharedPreferences("kernel_ai_settings", Context.MODE_PRIVATE)
        val colorModeValue = prefs.getInt("color_mode", KernelAiColorMode.SYSTEM.value)
        val keyColor = prefs.getInt("key_color", 0)
        return KernelAiAppSettings(
            colorMode = KernelAiColorMode.fromValue(colorModeValue),
            keyColor = keyColor
        )
    }

    fun saveColorMode(context: Context, mode: KernelAiColorMode) {
        val prefs = context.getSharedPreferences("kernel_ai_settings", Context.MODE_PRIVATE)
        prefs.edit().putInt("color_mode", mode.value).apply()
    }

    fun saveUiMode(context: Context, uiMode: UiMode) {
        val prefs = context.getSharedPreferences("kernel_ai_settings", Context.MODE_PRIVATE)
        prefs.edit().putString("ui_mode", uiMode.value).apply()
    }

    fun getUiMode(context: Context): UiMode {
        val prefs = context.getSharedPreferences("kernel_ai_settings", Context.MODE_PRIVATE)
        val value = prefs.getString("ui_mode", UiMode.DEFAULT_VALUE) ?: UiMode.DEFAULT_VALUE
        return UiMode.fromValue(value)
    }
}

/**
 * 轻微 MCP 统一主题入口 - 根据 UiMode 分发到 Miuix 或 Material3 主题
 */
@Composable
fun KernelAiTheme(
    appSettings: KernelAiAppSettings? = null,
    uiMode: UiMode = LocalUiMode.current,
    content: @Composable () -> Unit
) {
    val context = LocalContext.current
    val currentAppSettings = appSettings ?: KernelAiThemeController.getAppSettings(context)

    when (uiMode) {
        UiMode.Miuix -> MiuixKernelAiTheme(currentAppSettings, content)
        UiMode.Material -> MaterialKernelAiTheme(currentAppSettings, content)
    }
}

/**
 * Miuix 风格主题
 */
@Composable
fun MiuixKernelAiTheme(
    appSettings: KernelAiAppSettings,
    content: @Composable () -> Unit
) {
    val context = LocalContext.current
    val systemDarkTheme = isSystemInDarkTheme()
    val darkTheme = appSettings.colorMode.isDark || (appSettings.colorMode.isSystem && systemDarkTheme)

    val colorSchemeMode = when (appSettings.colorMode) {
        KernelAiColorMode.SYSTEM -> ColorSchemeMode.System
        KernelAiColorMode.LIGHT -> ColorSchemeMode.Light
        KernelAiColorMode.DARK -> ColorSchemeMode.Dark
    }

    val controller = ThemeController(
        colorSchemeMode = colorSchemeMode,
        keyColor = if (appSettings.keyColor == 0) null else Color(appSettings.keyColor),
        isDark = darkTheme,
        paletteStyle = ThemePaletteStyle.TonalSpot,
        colorSpec = ThemeColorSpec.Spec2021,
    )

    MiuixTheme(
        controller = controller,
        content = {
            LaunchedEffect(darkTheme) {
                val window = (context as? Activity)?.window ?: return@LaunchedEffect
                WindowInsetsControllerCompat(window, window.decorView).apply {
                    isAppearanceLightStatusBars = !darkTheme
                    isAppearanceLightNavigationBars = !darkTheme
                }
            }
            CompositionLocalProvider(
                LocalContentColor provides MiuixTheme.colorScheme.onBackground,
            ) {
                content()
            }
        }
    )
}

/**
 * Material3 风格主题
 */
@Composable
fun MaterialKernelAiTheme(
    appSettings: KernelAiAppSettings,
    content: @Composable () -> Unit
) {
    val context = LocalContext.current
    val systemDarkTheme = isSystemInDarkTheme()
    val darkTheme = appSettings.colorMode.isDark || (appSettings.colorMode.isSystem && systemDarkTheme)

    val colorScheme = if (appSettings.keyColor == 0) {
        if (darkTheme) {
            androidx.compose.material3.dynamicDarkColorScheme(context)
        } else {
            androidx.compose.material3.dynamicLightColorScheme(context)
        }
    } else {
        com.materialkolor.rememberDynamicColorScheme(
            seedColor = Color(appSettings.keyColor),
            isDark = darkTheme,
            isAmoled = false,
            style = com.materialkolor.PaletteStyle.TonalSpot,
            specVersion = com.materialkolor.dynamiccolor.ColorSpec.SpecVersion.Default,
        )
    }

    LaunchedEffect(darkTheme) {
        val window = (context as? Activity)?.window ?: return@LaunchedEffect
        WindowInsetsControllerCompat(window, window.decorView).apply {
            isAppearanceLightStatusBars = !darkTheme
            isAppearanceLightNavigationBars = !darkTheme
        }
    }

    androidx.compose.material3.MaterialExpressiveTheme(
        colorScheme = colorScheme,
        content = content
    )
}

/**
 * 判断当前是否为深色主题（在 Composable 中使用）
 */
@Composable
@ReadOnlyComposable
fun isInDarkTheme(): Boolean {
    return when (LocalColorMode.current) {
        1 -> false
        2 -> true
        else -> isSystemInDarkTheme()
    }
}

val LocalColorMode = staticCompositionLocalOf { 0 }
val LocalEnableBlur = staticCompositionLocalOf { false }
