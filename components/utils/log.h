#ifndef LOG_H
#define LOG_H
#include "esp_log.h"
#ifdef LOG_ENABLE
#define SET_TAG(x)          const char *LOG_TAG = (x)
#define LOG(fmt, ...) ESP_LOGI(LOG_TAG, fmt, ##__VA_ARGS__)
#define WARNING(fmt, ...) ESP_LOGW(LOG_TAG, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...) ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
#elifdef LOG_ENABLE_WARNING
#define SET_TAG(x)          const char *LOG_TAG = (x)
#define LOG(fmt, ...) ((void) 0)
#define WARNING(fmt, ...) ESP_LOGW(LOG_TAG, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...) ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
#elifdef LOG_ENABLE_ERROR
#define SET_TAG(x)          const char *LOG_TAG = (x)
#define LOG(fmt, ...) ((void) 0)
#define WARNING(fmt, ...) ((void) 0)
#define ERROR(fmt, ...) ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
#else
#define SET_TAG(x)          ((void) 0)
#define LOG(fmt, ...) ((void) 0)
#define WARNING(fmt, ...) ((void) 0)
#define ERROR(fmt, ...) ((void) 0)
#endif
#endif

