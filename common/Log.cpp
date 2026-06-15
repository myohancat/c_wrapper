/**
 * C API wrappers and utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#include "Log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>

static LOG_LEVEL_e  gLogLevel = LOG_LEVEL_TRACE;
static bool gLogWithTime = true;

#if 1 /* Enable Mutual logs */
static pthread_mutex_t gLogMutex = PTHREAD_MUTEX_INITIALIZER;

#define LOCK_LOG_OUT()    pthread_mutex_lock(&gLogMutex)
#define UNLOCK_LOG_OUT()  pthread_mutex_unlock(&gLogMutex)
#else
#define LOCK_LOG_OUT()    do { } while(0)
#define UNLOCK_LOG_OUT()  do { } while(0)
#endif

#ifdef __cplusplus
extern "C"
{
#endif

const char* simplify_function(char* buf, const char* func)
{
    if (!func) return "";

    const char* end = strrchr(func, '(');
    if (!end) end = func + strlen(func);

    const char* p = func;
    const char* begin = NULL;
    while ((p = strchr(p, ' ')) && p < end) { begin = p; p++; }
    if (begin) begin++;
    else begin = buf;

    size_t len = end - begin;

    if (len >= MAX_FUNCTION_SIZE)
    {
        size_t copy_len = MAX_FUNCTION_SIZE - 4;
        strncpy(buf, begin, copy_len);
        strcpy(buf + copy_len, "...");
    }
    else
    {
        strncpy(buf, begin, len);
        buf[len] = '\0';
    }

    return buf;
}

static FILE* output_device(void)
{
    // TODO IMPLEMETNS HERE
    return stdout;
}

static const char *cur_time_str(char *buf)
{
    struct timeval tv;
    struct tm t_buf;
    gettimeofday(&tv, 0);
    time_t curtime = tv.tv_sec;

    struct tm *t = localtime_r(&curtime, &t_buf);

    snprintf(buf, 32, "[%02d:%02d:%02d.%03ld] ", t->tm_hour, t->tm_min, t->tm_sec, tv.tv_usec / 1000);
    return buf;
}

void LOG_SetLevel(LOG_LEVEL_e eLevel)
{
    gLogLevel = eLevel;
}

LOG_LEVEL_e LOG_GetLevel(void)
{
    return gLogLevel;
}

void LOG_Print(int priority, const char* color, const char* fmt, ...)
{
    va_list ap;
    FILE* fp;

    if(priority > gLogLevel)
        return;

    LOCK_LOG_OUT();

    fp = output_device();

    if(color && color[0] != '\0')
        fputs(color, fp);

    if(gLogWithTime)
    {
        char timestr[32];
        fputs(cur_time_str(timestr), fp);
    }

    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);

    if(color && color[0] != '\0')
        fputs(ANSI_COLOR_RESET, fp);

    fflush(fp);

    UNLOCK_LOG_OUT();
}

#define IS_PRINTABLE(c)  (((c)>=32 && (c)<=126))

void LOG_Dump(int priority, const void* ptr, size_t len)
{
    FILE* fp;
    size_t offset = 0;
    const unsigned char* data = (const unsigned char*)ptr;

    if(priority > gLogLevel)
        return;

    if (!ptr || len == 0)
        return;

    LOCK_LOG_OUT();

    fp = output_device();

    while (offset < len)
    {
        char buffer[128];
        int pos = 0;
        int remain = len - offset;
        if (remain > 16) remain = 16;

        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "0x%04zx  ", offset);

        for (int i = 0; i < 16; i++)
        {
            if (i == 8)
                buffer[pos++] = ' ';

            if (i < remain)
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%02x ", data[offset + i]);
            else
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, "   ");
        }

        buffer[pos++] = ' ';

        for (int i = 0; i < remain; i++)
        {
            char c = (char)data[offset + i];
            buffer[pos++] = IS_PRINTABLE(c) ? c : '.';
        }

        buffer[pos++] = '\n';
        buffer[pos] = '\0';

        fputs(buffer, fp);
        offset += 16;
    }

    fflush(fp);
    UNLOCK_LOG_OUT();
}

#ifdef __cplusplus
}
#endif
