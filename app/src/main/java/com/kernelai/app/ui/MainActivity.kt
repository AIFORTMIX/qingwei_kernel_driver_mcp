package com.kernelai.app.ui

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.BugReport
import androidx.compose.material.icons.rounded.Chat
import androidx.compose.material.icons.rounded.Code
import androidx.compose.material.icons.rounded.ListAlt
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationDrawerItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.PermanentDrawerSheet
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.runtime.DisposableEffect
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation3.runtime.entryProvider
import androidx.navigation3.runtime.rememberSaveableStateHolderNavEntryDecorator
import androidx.navigation3.runtime.rememberViewModelStoreNavEntryDecorator
import androidx.navigation3.ui.NavDisplay
import android.content.res.Configuration
import com.kernelai.app.ui.navigation.LocalAppNavigator
import com.kernelai.app.ui.navigation.AppNavigator
import com.kernelai.app.ui.navigation.Route
import com.kernelai.app.ui.navigation.rememberAppNavigator
import com.kernelai.app.ui.screen.breakpoint.BreakpointScreen
import com.kernelai.app.ui.screen.chat.ChatScreen
import com.kernelai.app.ui.screen.disasm.DisasmScreen
import com.kernelai.app.ui.screen.log.LogScreen
import com.kernelai.app.ui.screen.memory.MemoryScreen
import com.kernelai.app.ui.screen.process.ProcessScreen
import com.kernelai.app.ui.screen.settings.SettingsScreen
import com.kernelai.app.ui.theme.KernelAiTheme
import com.kernelai.app.ui.theme.KernelAiThemeController
import com.kernelai.app.ui.theme.KernelAiAppSettings
import com.kernelai.app.ui.viewmodel.DriverViewModel
import top.yukonga.miuix.kmp.basic.NavigationBar as MiuixNavigationBar
import top.yukonga.miuix.kmp.basic.NavigationBarItem as MiuixNavigationBarItem
import top.yukonga.miuix.kmp.basic.NavigationItem
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * 轻微 MCP 主 Activity - 基于 KernelSU-Style-UI-Kit 模板结构
 * 支持 Miuix/Material 双主题、底部导航 + 侧边导航
 */
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            val driverViewModel = viewModel<DriverViewModel>()
            val connectionStatus by driverViewModel.connectionStatus.collectAsStateWithLifecycle()
            val appSettings by driverViewModel.appSettings.collectAsStateWithLifecycle()
            val logEntries by driverViewModel.logEntries.collectAsStateWithLifecycle()

            val uiMode = appSettings.uiStyle.let {
                when (it) {
                    com.kernelai.app.data.model.UiStyle.MIUIX -> UiMode.Miuix
                    com.kernelai.app.data.model.UiStyle.MATERIAL -> UiMode.Material
                }
            }

            val darkMode = when (appSettings.themeMode) {
                com.kernelai.app.data.model.ThemeMode.DARK -> true
                com.kernelai.app.data.model.ThemeMode.LIGHT -> false
                com.kernelai.app.data.model.ThemeMode.SYSTEM -> isSystemInDarkTheme()
            }

            // 初始化驱动连接
            LaunchedEffect(Unit) {
                driverViewModel.initialize(this@MainActivity)
            }

            // 设置系统栏样式
            DisposableEffect(darkMode) {
                enableEdgeToEdge(
                    statusBarStyle = SystemBarStyle.auto(
                        android.graphics.Color.TRANSPARENT,
                        android.graphics.Color.TRANSPARENT
                    ) { darkMode },
                    navigationBarStyle = SystemBarStyle.auto(
                        android.graphics.Color.TRANSPARENT,
                        android.graphics.Color.TRANSPARENT
                    ) { darkMode },
                )
                window.isNavigationBarContrastEnforced = false
                onDispose { }
            }

            val navigator = rememberAppNavigator(Route.Main)
            val themeSettings = KernelAiAppSettings(
                colorMode = com.kernelai.app.ui.theme.KernelAiColorMode.fromThemeMode(appSettings.themeMode)
            )

            CompositionLocalProvider(
                LocalAppNavigator provides navigator,
                LocalUiMode provides uiMode,
            ) {
                KernelAiTheme(appSettings = themeSettings, uiMode = uiMode) {
                    val mainScreenEntry = @Composable {
                        MainScreen(
                            connectionStatus = connectionStatus,
                            appSettings = appSettings,
                            logEntries = logEntries,
                            driverViewModel = driverViewModel,
                        )
                    }

                    val navDisplay = @Composable {
                        NavDisplay(
                            backStack = navigator.backStack,
                            entryDecorators = listOf(
                                rememberSaveableStateHolderNavEntryDecorator(),
                                rememberViewModelStoreNavEntryDecorator()
                            ),
                            onBack = { navigator.pop() },
                            entryProvider = entryProvider {
                                entry<Route.Main> { mainScreenEntry() }
                                entry<Route.Chat> { mainScreenEntry() }
                                entry<Route.Settings> { mainScreenEntry() }
                                entry<Route.ProcessBrowser> { mainScreenEntry() }
                                entry<Route.BreakpointManager> { mainScreenEntry() }
                                entry<Route.MemoryViewer> { MemoryScreen() }
                                entry<Route.DisasmViewer> { DisasmScreen() }
                                entry<Route.LogViewer> {
                                    LogScreen(
                                        logEntries = logEntries,
                                        onClear = { driverViewModel.clearLogs() }
                                    )
                                }
                            }
                        )
                    }

                    when (uiMode) {
                        UiMode.Material -> androidx.compose.material3.Scaffold { navDisplay() }
                        UiMode.Miuix -> Scaffold { navDisplay() }
                    }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
    }
}

/**
 * 底部导航页面配置
 */
object MainPagerConfig {
    const val PAGE_COUNT = 4
    const val PAGE_CHAT = 0
    const val PAGE_PROCESS = 1
    const val PAGE_BREAKPOINT = 2
    const val PAGE_SETTINGS = 3
}

/**
 * 底部导航目标
 */
enum class BottomNavDestination(
    val label: String,
    val icon: ImageVector
) {
    Chat("聊天", Icons.Rounded.Chat),
    Process("进程", Icons.Rounded.Memory),
    Breakpoint("断点", Icons.Rounded.BugReport),
    Settings("设置", Icons.Rounded.Settings)
}

/**
 * 侧边导航目标
 */
enum class SideNavDestination(
    val label: String,
    val icon: ImageVector,
    val route: Route
) {
    Memory("内存查看", Icons.Rounded.ListAlt, Route.MemoryViewer),
    Disasm("反汇编", Icons.Rounded.Code, Route.DisasmViewer),
    Log("日志", Icons.Rounded.ListAlt, Route.LogViewer)
}

/**
 * 主屏幕 - 包含底部导航和侧边导航
 */
@Composable
fun MainScreen(
    connectionStatus: com.kernelai.app.data.model.ConnectionStatus,
    appSettings: com.kernelai.app.data.model.AppSettings,
    logEntries: List<DriverViewModel.LogEntry>,
    driverViewModel: DriverViewModel,
) {
    val navigator = LocalAppNavigator.current
    val uiMode = LocalUiMode.current
    var selectedPage by remember { mutableIntStateOf(0) }
    val pagerState = rememberPagerState(initialPage = 0, pageCount = { MainPagerConfig.PAGE_COUNT })

    // 同步 pager 和 selectedPage
    val settledPage = pagerState.settledPage
    LaunchedEffect(settledPage) {
        selectedPage = settledPage
    }

    val isLandscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE

    val pagerContent = @Composable {
        HorizontalPager(
            state = pagerState,
            modifier = Modifier.fillMaxSize(),
            beyondViewportPageCount = 1,
        ) { page ->
            when (page) {
                MainPagerConfig.PAGE_CHAT -> ChatScreen()
                MainPagerConfig.PAGE_PROCESS -> ProcessScreen()
                MainPagerConfig.PAGE_BREAKPOINT -> BreakpointScreen()
                MainPagerConfig.PAGE_SETTINGS -> SettingsScreen(
                    connectionStatus = connectionStatus,
                    appSettings = appSettings,
                    onThemeModeChange = { driverViewModel.updateThemeMode(it) },
                    onUiStyleChange = { driverViewModel.updateUiStyle(it) },
                    onReconnectDriver = { driverViewModel.reconnectDriver() },
                    onToggleMcp = { driverViewModel.toggleMcpServer() },
                )
            }
        }
    }

    if (isLandscape) {
        // 横屏：侧边导航栏
        Row(modifier = Modifier.fillMaxSize()) {
            when (uiMode) {
                UiMode.Material -> {
                    NavigationRail {
                        BottomNavDestination.entries.forEachIndexed { index, dest ->
                            NavigationRailItem(
                                selected = selectedPage == index,
                                onClick = { selectedPage = index },
                                icon = { Icon(dest.icon, contentDescription = dest.label) },
                                label = { Text(dest.label, fontSize = 11.sp) }
                            )
                        }
                        // 侧边导航项
                        SideNavDestination.entries.forEach { dest ->
                            NavigationRailItem(
                                selected = false,
                                onClick = { navigator.push(dest.route) },
                                icon = { Icon(dest.icon, contentDescription = dest.label) },
                                label = { Text(dest.label, fontSize = 11.sp) }
                            )
                        }
                    }
                }
                UiMode.Miuix -> {
                    // Miuix 侧边导航
                    top.yukonga.miuix.kmp.basic.Column(
                        modifier = Modifier.padding(8.dp)
                    ) {
                        BottomNavDestination.entries.forEachIndexed { index, dest ->
                            top.yukonga.miuix.kmp.basic.Text(
                                text = dest.label,
                                modifier = Modifier.padding(8.dp)
                            )
                        }
                    }
                }
            }

            Box(modifier = Modifier.weight(1f)) {
                pagerContent()
            }
        }
    } else {
        // 竖屏：底部导航栏
        val bottomBar = @Composable {
            Box(modifier = Modifier.fillMaxWidth()) {
                when (uiMode) {
                    UiMode.Material -> {
                        NavigationBar {
                            BottomNavDestination.entries.forEachIndexed { index, dest ->
                                NavigationBarItem(
                                    selected = selectedPage == index,
                                    onClick = { selectedPage = index },
                                    icon = { Icon(dest.icon, contentDescription = dest.label) },
                                    label = { Text(dest.label, fontSize = 11.sp) }
                                )
                            }
                        }
                    }
                    UiMode.Miuix -> {
                        val items = BottomNavDestination.entries.map { dest ->
                            NavigationItem(
                                label = dest.label,
                                icon = dest.icon,
                            )
                        }
                        MiuixNavigationBar(
                            color = MiuixTheme.colorScheme.surface,
                            content = {
                                items.forEachIndexed { index, item ->
                                    MiuixNavigationBarItem(
                                        modifier = Modifier.weight(1f),
                                        icon = item.icon,
                                        label = item.label,
                                        selected = selectedPage == index,
                                        onClick = { selectedPage = index }
                                    )
                                }
                            }
                        )
                    }
                }
            }
        }

        when (uiMode) {
            UiMode.Material -> androidx.compose.material3.Scaffold(
                bottomBar = bottomBar
            ) { innerPadding ->
                Box(modifier = Modifier.padding(innerPadding)) {
                    pagerContent()
                }
            }
            UiMode.Miuix -> Scaffold(
                bottomBar = bottomBar
            ) { innerPadding ->
                Box(modifier = Modifier.padding(innerPadding)) {
                    pagerContent()
                }
            }
        }
    }
}
