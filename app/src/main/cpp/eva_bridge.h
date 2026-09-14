#pragma once

#include <jni.h>
#include <string>

void eva_bridge_set_activity(JNIEnv* env, jobject activity);
bool eva_bridge_poll_haptic(double command[4]);
void eva_bridge_send_frame(const std::string& json);
bool eva_bridge_is_connected();
