# 轻微 MCP JNI 原生库构建配置
# 生成 libqingwei_mcp_jni.so，供 DeviceNode.kt 通过 System.loadLibrary("qingwei_mcp_jni") 加载

cmake_minimum_required(VERSION 3.22.1)

project("qingwei_mcp_jni")

# 构建共享库
add_library(${PROJECT_NAME} SHARED
    kernelai_jni.cpp
)

# 链接 Android 日志库 (liblog)
# 用于 __android_log_print 输出
find_library(log-lib log)

target_link_libraries(${PROJECT_NAME}
    ${log-lib}
)

# 编译选项
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -fvisibility=hidden
)
