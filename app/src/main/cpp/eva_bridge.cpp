#include "eva_bridge.h"

#include <android/log.h>
#include <mutex>

namespace {
JavaVM* g_vm = nullptr;
jobject g_activity = nullptr;
jmethodID g_poll_haptic = nullptr;
jmethodID g_send_frame = nullptr;
jmethodID g_is_connected = nullptr;
std::mutex g_mutex;
}

void eva_bridge_set_activity(JNIEnv* env, jobject activity) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_activity != nullptr) {
        env->DeleteGlobalRef(g_activity);
        g_activity = nullptr;
    }
    g_vm = nullptr;
    if (env == nullptr || activity == nullptr) return;
    env->GetJavaVM(&g_vm);
    g_activity = env->NewGlobalRef(activity);
    jclass cls = env->GetObjectClass(activity);
    g_poll_haptic = env->GetMethodID(cls, "pollHaptic", "()[D");
    g_send_frame = env->GetMethodID(cls, "sendFrame", "(Ljava/lang/String;)V");
    g_is_connected = env->GetMethodID(cls, "isConnected", "()Z");
    env->DeleteLocalRef(cls);
}

bool eva_bridge_poll_haptic(double command[4]) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_vm == nullptr || g_activity == nullptr || g_poll_haptic == nullptr) return false;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    }
    auto* values = static_cast<jdoubleArray>(env->CallObjectMethod(g_activity, g_poll_haptic));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        values = nullptr;
    }
    bool ok = false;
    if (values != nullptr && env->GetArrayLength(values) >= 4) {
        env->GetDoubleArrayRegion(values, 0, 4, command);
        ok = true;
        env->DeleteLocalRef(values);
    }
    if (attached) g_vm->DetachCurrentThread();
    return ok;
}

void eva_bridge_send_frame(const std::string& json) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_vm == nullptr || g_activity == nullptr || g_send_frame == nullptr) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    jstring message = env->NewStringUTF(json.c_str());
    env->CallVoidMethod(g_activity, g_send_frame, message);
    env->DeleteLocalRef(message);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_vm->DetachCurrentThread();
}


bool eva_bridge_is_connected() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_vm == nullptr || g_activity == nullptr || g_is_connected == nullptr) return false;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    }
    const jboolean connected = env->CallBooleanMethod(g_activity, g_is_connected);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_vm->DetachCurrentThread();
    return connected == JNI_TRUE;
}
