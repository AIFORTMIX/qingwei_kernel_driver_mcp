plugins {
    alias(libs.plugins.agp.app) apply false
    alias(libs.plugins.kotlin) apply false
    alias(libs.plugins.compose.compiler) apply false
    alias(libs.plugins.kotlin.serialization) apply false
}

// 全局构建属性 - 供子模块引用
val androidMinSdkVersion by extra(28)
val androidTargetSdkVersion by extra(35)
val androidCompileSdkVersion by extra(35)
val androidBuildToolsVersion by extra("35.0.0")
val androidSourceCompatibility by extra(JavaVersion.VERSION_17)
val androidTargetCompatibility by extra(JavaVersion.VERSION_17)
val managerVersionCode by extra(100)
val managerVersionName by extra("0.1.0")
