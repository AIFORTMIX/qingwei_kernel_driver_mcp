/**
 * 轻微 MCP JNI 原生层
 *
 * 提供 Java/Kotlin 与内核驱动 ioctl 之间的桥接。
 * 通过 /dev/qingwei_mcp 设备节点与内核模块通信。
 *
 * 对应 Kotlin 类: com.kernelai.app.driver.DeviceNode
 */

#include <jni.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "qingwei_mcp_jni"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// JNI 类描述符 - 对应 DeviceNode 类
static const char *kClassName = "com/kernelai/app/driver/DeviceNode";

// ============================================================
// nativeOpen(path: String): Int
// 以 O_RDWR 模式打开设备节点，返回文件描述符
// ============================================================
extern "C"
JNIEXPORT jint JNICALL
Java_com_kernelai_app_driver_DeviceNode_nativeOpen(
        JNIEnv *env,
        jobject /* this */,
        jstring path) {

    const char *pathChars = env->GetStringUTFChars(path, nullptr);
    if (pathChars == nullptr) {
        LOGE("nativeOpen: 无法获取路径字符串 (OutOfMemory)");
        return -1;
    }

    LOGI("nativeOpen: 正在打开设备节点 '%s'", pathChars);

    int fd = ::open(pathChars, O_RDWR);
    if (fd < 0) {
        LOGE("nativeOpen: 打开 '%s' 失败, errno=%d (%s)",
             pathChars, errno, strerror(errno));
    } else {
        LOGI("nativeOpen: 成功打开设备, fd=%d", fd);
    }

    env->ReleaseStringUTFChars(path, pathChars);
    return fd;
}

// ============================================================
// nativeClose(fd: Int): Int
// 关闭文件描述符
// ============================================================
extern "C"
JNIEXPORT jint JNICALL
Java_com_kernelai_app_driver_DeviceNode_nativeClose(
        JNIEnv *env,
        jobject /* this */,
        jint fd) {

    LOGI("nativeClose: 正在关闭 fd=%d", fd);

    int ret = ::close(fd);
    if (ret < 0) {
        LOGE("nativeClose: 关闭 fd=%d 失败, errno=%d (%s)",
             fd, errno, strerror(errno));
    } else {
        LOGI("nativeClose: 成功关闭 fd=%d", fd);
    }

    return ret;
}

// ============================================================
// nativeIoctl(fd: Int, command: Int, arg: ByteArray): Int
//
// 执行 ioctl 系统调用。
// arg 字节数组同时作为输入和输出缓冲区：
//   1. 将 Java 字节数组内容拷贝到本地缓冲区
//   2. 将本地缓冲区指针传递给内核 ioctl
//   3. ioctl 返回后，将修改后的缓冲区内容写回 Java 字节数组
//
// 返回值: ioctl 系统调用的返回值 (0 表示成功, -1 表示失败)
// ============================================================
extern "C"
JNIEXPORT jint JNICALL
Java_com_kernelai_app_driver_DeviceNode_nativeIoctl(
        JNIEnv *env,
        jobject /* this */,
        jint fd,
        jint command,
        jbyteArray arg) {

    // 获取字节数组长度
    jsize len = env->GetArrayLength(arg);
    if (len <= 0) {
        LOGE("nativeIoctl: 无效的 arg 数组长度: %d", len);
        return -1;
    }

    // 分配本地缓冲区并拷贝数据
    // 使用 malloc 分配，因为 ioctl 需要一块可读写内存
    jbyte *buffer = static_cast<jbyte *>(malloc(len));
    if (buffer == nullptr) {
        LOGE("nativeIoctl: 内存分配失败 (size=%d)", len);
        return -1;
    }

    // 从 Java 数组拷贝到本地缓冲区
    env->GetByteArrayRegion(arg, 0, len, reinterpret_cast<jbyte *>(buffer));

    LOGD("nativeIoctl: fd=%d, cmd=0x%X, buf_size=%d", fd, command, len);

    // 执行 ioctl 调用
    // 将缓冲区指针作为 ioctl 的第三个参数 (void* arg)
    int ret = ::ioctl(fd, command, buffer);
    if (ret < 0) {
        LOGE("nativeIoctl: ioctl 失败, cmd=0x%X, errno=%d (%s)",
             command, errno, strerror(errno));
    } else {
        LOGD("nativeIoctl: ioctl 成功, cmd=0x%X, ret=%d", command, ret);
        // 将内核修改后的数据写回 Java 字节数组
        env->SetByteArrayRegion(arg, 0, len, reinterpret_cast<const jbyte *>(buffer));
    }

    free(buffer);
    return ret;
}

// ============================================================
// JNI_OnLoad: 动态注册（备用方案）
//
// 当前使用静态注册（Java_ 前缀命名约定），
// 此处保留 JNI_OnLoad 用于版本检查和未来扩展。
// ============================================================
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void * /* reserved */) {
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("JNI_OnLoad: GetEnv 失败");
        return JNI_ERR;
    }

    LOGI("轻微 MCP JNI 库已加载 (JNI_VERSION_1_6)");
    return JNI_VERSION_1_6;
}
