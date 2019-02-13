//
// Created by esalman17 on 5.10.2018.
//

#include <android/log.h>

#define DEBUG

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "Native", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "Native", __VA_ARGS__))

#ifdef DEBUG
#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, "Native", __VA_ARGS__))
#else
#define LOGD(...)
#endif
