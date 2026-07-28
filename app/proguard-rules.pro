# ============================================================
# KernelAI ProGuard 混淆规则
# ============================================================

# ----- 通用 Android 规则 -----
-keepattributes *Annotation*
-keepattributes Signature
-keepattributes InnerClasses
-keepattributes EnclosingMethod

# ----- Kotlinx Serialization -----
# 保留 @Serializable 类及其生成的 Companion 对象
-keepattributes RuntimeVisibleAnnotations
-keepclassmembers class **.$serializer {
    <init>(...);
    <fields>;
}
-keepclassmembers class ** {
    *** Companion;
}
-keepclasseswithmembers class ** : kotlinx.serialization.KSerializer {
    ** INSTANCE(...);
}
# 保留 kotlinx.serialization 内部类
-keep,includedescriptorclasses class com.kernelai.app.**$$serializer { *; }
-keepclassmembers class com.kernelai.app.** {
    *** Companion;
}
-keepclasseswithmembers class com.kernelai.app.** {
    kotlinx.serialization.KSerializer serializer(...);
}

# ----- OkHttp -----
# OkHttp 内部使用反射，需要保留关键类
-dontwarn okhttp3.**
-dontwarn okio.**
-keep class okhttp3.** { *; }
-keep interface okhttp3.** { *; }
-keep class okio.** { *; }

# OkHttp SSE
-keep class okhttp3.sse.** { *; }
-keep interface okhttp3.sse.** { *; }

# ----- Kotlin Coroutines -----
-keepnames class kotlinx.coroutines.internal.MainDispatcherFactory {}
-keepnames class kotlinx.coroutines.CoroutineExceptionHandler {}
-keepclassmembers class kotlinx.coroutines.** {
    volatile <fields>;
}

# ----- JNI 原生方法 -----
# 保留 JNI 调用的类和方法，防止被混淆后 JNI 找不到
-keep class com.kernelai.app.driver.DeviceNode {
    native <methods>;
    private native <methods>;
}
-keepclasseswithmembernames class com.kernelai.app.driver.** {
    native <methods>;
}

# ----- Miuix UI -----
-keep class top.yukonga.miuix.kmp.** { *; }
-dontwarn top.yukonga.miuix.kmp.**

# ----- Commonmark -----
-keep class org.commonmark.** { *; }
-dontwarn org.commonmark.**

# ----- Hidden API Bypass -----
-keep class org.lsposed.hiddenapibypass.** { *; }
-dontwarn org.lsposed.hiddenapibypass.**

# ----- Material Kolor -----
-keep class com.materialkolor.** { *; }
-dontwarn com.materialkolor.**

# ----- Compose -----
-keep class androidx.compose.** { *; }

# ----- 数据类保留 (用于序列化/反序列化) -----
-keep class com.kernelai.app.data.model.** { *; }
-keep class com.kernelai.app.driver.IoctlStructs$* { *; }

# ----- MCP 协议相关 -----
-keep class com.kernelai.app.mcp.** { *; }
