/*
 * rtos.c
 */

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "rtos.h"

static pthread_mutex_t mutex_printf = PTHREAD_MUTEX_INITIALIZER;

unsigned int os_get_msec_clock(void)
{
    static unsigned int start_sec = 0;
    struct timespec tm;

    clock_gettime(CLOCK_MONOTONIC, &tm);

    unsigned int t = (tm.tv_sec - start_sec) * 1000 + tm.tv_nsec / 1000000;
    return t;
}

unsigned int os_get_usec_clock(void)
{
    static unsigned int start_sec = 0;
    struct timespec tm;

    clock_gettime(CLOCK_MONOTONIC, &tm);

    unsigned int t = (tm.tv_sec - start_sec) * 1000000 + tm.tv_nsec / 1000;
    return t;
}

int os_printf(const char *fmt, ...)
{
    int ret = 0, ret2;
    char buf[0x1000];
    char *ps = buf;
    int max_len = sizeof(buf);
    va_list arg;
#if 1
    double cur_time = os_get_msec_clock() / 1000.;
    ret = snprintf(buf, max_len, "[S%.3f %s] ", cur_time, os_get_cur_task_name());
    ps = buf + ret;
    max_len -= ret;
#endif
    va_start(arg, fmt);
    ret2 = vsnprintf(ps, max_len, fmt, arg);
    va_end(arg);
    if (ret2 > 0)
    {
        pthread_mutex_lock(&mutex_printf);
        write(2, buf, ret + ret2);
        pthread_mutex_unlock(&mutex_printf);
    }
    return ret;
}

int os_terminate(const char *fmt, ...)
{
    char buf[0x200];
    int len;
    va_list arg;
    va_start(arg, fmt);
    len = vsnprintf(buf, sizeof(buf) - 1, fmt, arg);
    va_end(arg);
    write(2, buf, len);
    exit(-1);
}

void SemaphoreInit(SEM_ID *sem)
{
    sem_init(sem, 0, 1);
}

int SemaphoreLock(SEM_ID *sem, int timeout_ms)
{
    if (timeout_ms <= 0)
    {
        return sem_wait(sem);
    }
    else
    {
        int ret;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += timeout_ms * 1000000;
        while (ts.tv_nsec > 1000000000)
        {
            ts.tv_nsec -= 1000000000;
            ts.tv_sec++;
        }
        while ((ret = sem_timedwait(sem, &ts)) == -1 && errno == EINTR)
            continue; /* Restart if interrupted by handler */
        if (ret == -1)
        {
            if (errno != ETIMEDOUT)
                return -1;
        }
        else
        {
            return 1;
        }
        return 0;  // timeout
    }
}

void SemaphoreUnlock(SEM_ID *sem)
{
    sem_post(sem);
}

void os_sleep_ms(int ms)
{
    usleep(ms * 1000);
}

void os_init_task(void);
void os_init(void)
{
    os_init_task();
    pthread_mutex_init(&mutex_printf, 0);
}
