/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LOG_EOL                 "\n"

#define BASENAME(str)       (strrchr(str, '/') ? strrchr(str, '/') + 1 : str)
#define __BASE_FILE_NAME__   BASENAME(__FILE__)

#define ANSI_COLOR_BOLD      "\x1b[1m"
#define ANSI_COLOR_UNBOLD    "\x1b[22m"
#define ANSI_COLOR_ITALIC    "\x1b[3m"
#define ANSI_COLOR_UNDERLINE "\x1b[4m"

#define ANSI_COLOR_NONE      ""
#define ANSI_COLOR_RED       "\x1b[31m"
#define ANSI_COLOR_GREEN     "\x1b[32m"
#define ANSI_COLOR_YELLOW    "\x1b[33m"
#define ANSI_COLOR_BLUE      "\x1b[34m"
#define ANSI_COLOR_MAGENTA   "\x1b[35m"
#define ANSI_COLOR_CYAN      "\x1b[36m"
#define ANSI_COLOR_GRAY      "\x1b[37m"
#define ANSI_COLOR_DARKGRAY  "\x1b[30;1m"
#define ANSI_COLOR_RESET     "\x1b[0m"

#define ANSI_COLOR_BRIGHT_YELLOW   "\x1b[93m"
#define ANSI_COLOR_BRIGHT_MAGENTA  "\x1b[95m"
#define ANSI_COLOR_BRIGHT_CYAN     "\x1b[96m"

typedef enum
{
    LOG_LEVEL_NONE,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE,

    MAX_LOG_LEVEL
}LOG_LEVEL_e;

#ifdef __cplusplus
extern "C"
{
#endif

void         LOG_SetLevel(LOG_LEVEL_e eLevel);
LOG_LEVEL_e  LOG_GetLevel(void);

void LOG_Print(int priority, const char* color, const char* fmt, ...);
void LOG_Dump(int priority, const void* ptr, size_t len);

#define MAX_FUNCTION_SIZE    (128)
const char* simplify_function(char* buf, const char* func);

#define PRINT(fmt, ...)    do { \
                                   LOG_Print(LOG_LEVEL_NONE, ANSI_COLOR_NONE, fmt LOG_EOL, ##__VA_ARGS__); \
                           } while(0)

#define LOGT(fmt, ...)     do { \
                                   LOG_Print(LOG_LEVEL_TRACE, ANSI_COLOR_GRAY, fmt LOG_EOL, ##__VA_ARGS__); \
                           } while(0)

#define LOGD(fmt, ...)     do { \
                                   LOG_Print(LOG_LEVEL_DEBUG, ANSI_COLOR_NONE, fmt LOG_EOL, ##__VA_ARGS__); \
                           } while(0)

#define LOGI(fmt, ...)     do { \
                                   char tmp[MAX_FUNCTION_SIZE]; \
                                   const char* func = simplify_function(tmp, __PRETTY_FUNCTION__); \
                                   LOG_Print(LOG_LEVEL_INFO, ANSI_COLOR_YELLOW, "[%s:%d] %s() " fmt LOG_EOL, __BASE_FILE_NAME__, __LINE__, func, ##__VA_ARGS__); \
                           } while(0)

#define LOGW(fmt, ...)     do { \
                                   char tmp[MAX_FUNCTION_SIZE]; \
                                   const char* func = simplify_function(tmp, __PRETTY_FUNCTION__); \
                                   LOG_Print(LOG_LEVEL_WARN, ANSI_COLOR_BOLD, "[%s:%d] %s() " fmt LOG_EOL, __BASE_FILE_NAME__, __LINE__, func, ##__VA_ARGS__); \
                           } while(0)

#define LOGE(fmt, ...)     do { \
                                   char tmp[MAX_FUNCTION_SIZE]; \
                                   const char* func = simplify_function(tmp, __PRETTY_FUNCTION__); \
                                   LOG_Print(LOG_LEVEL_ERROR, ANSI_COLOR_BOLD ANSI_COLOR_RED, "[%s:%d] %s() " fmt LOG_EOL, __BASE_FILE_NAME__, __LINE__, func, ##__VA_ARGS__); \
                           } while(0)


#define CHECK(expr)        do { \
                                  if (!(expr)) { \
                                      LOGE("CHECK FAILED: " #expr); \
                                      abort(); \
                                  } \
                           } while(0)

#define ABORT_IF(expr)     do { \
                               if ((expr)) { \
                                   LOGE("ABORT_IF: " #expr); \
                                   abort(); \
                               } \
                           } while(0)

#ifndef NDEBUG
#define ASSERT_IF(expr, fmt, ...) do { \
    if ((expr)) { \
        LOGE("ASSERT_IF: " #expr " | " fmt, ##__VA_ARGS__); \
        abort(); \
    } \
} while (0)
#else
#define ASSERT_IF(expr, fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef __cplusplus
class AutoFunctionTrace
{
public:
    AutoFunctionTrace(const char* filename, int line, const char* func)
    {
        char tmp[MAX_FUNCTION_SIZE];

        snprintf(mStackTrace, MAX_FUNCTION_SIZE-1, "[%s:%d] %s", filename, line, simplify_function(tmp, func));
        mStackTrace[MAX_FUNCTION_SIZE - 1] = '\0';

        LOGT("------ %s() Enter ------", mStackTrace);
    }

    ~AutoFunctionTrace()
    {
        LOGT("------ %s() Leave ------", mStackTrace);
    }

private:
    char mStackTrace[MAX_FUNCTION_SIZE];
};

#define __TRACE__   AutoFunctionTrace __function_trace__(__BASE_FILE_NAME__, __LINE__, __PRETTY_FUNCTION__);
#else
#define __TRACE__   LOGT("----- [%s:%d] %s() -----", __BASE_FILE_NAME__, __LINE__, __FUNCTION__);
#endif
