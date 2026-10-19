#pragma once
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GC2Plus", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GC2Plus", __VA_ARGS__)
