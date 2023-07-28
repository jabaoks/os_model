/*
 * rtos.h
 */

#ifndef _RTOS_H_
#define _RTOS_H_

#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define FIFO_SEM_SUPPORT 1

#ifdef __cplusplus
extern "C"
{
#endif

/* Common */
void os_init(void);
void os_start(void);

int os_printf(const char *fmt, ...);
int os_terminate(const char *fmt, ...);

void os_sleep_ms(int);

unsigned int os_get_msec_clock(void);
unsigned int os_get_usec_clock(void);

/* Task */
struct os_task
{
    void (*entry_func)(void *);
    char name[16];
    sem_t sem;
    int priority;
    void *data;
};

int os_create_task(const char *name, void (*entry_func)(void *), int prio, void *data);
const char *os_get_cur_task_name(void);

/* Mutex */
#define SYS_PMUTEX void *;
#define MUTEX_ID pthread_mutex_t
#define MutexLock pthread_mutex_lock
#define MutexUnlock pthread_mutex_unlock
#define MutexAdd(m) pthread_mutex_init((m), 0)

/* Semahore */
#define SEM_ID sem_t
void SemaphoreInit(SEM_ID *sem);
int SemaphoreLock(SEM_ID *sem, int timeout_ms);
void SemaphoreUnlock(SEM_ID *sem);

/* IO */
typedef enum
{
    IO_OK = 1,
    IO_ERR = -1,
    IO_TIMEOUT = -2,
    IO_UNDEF = -3
} IO_RET;

typedef enum
{
    O_RDONLY = 0,
    O_WRONLY = 1,

    O_NOCOPY = 0x40,
    O_NONBLOCK = 0x80,
    O_READ_BLOCK = 0,  // must be zero
    O_BOX = 0x100,
    O_OVERWRITE = 0x200  // Overwrite element if pipe is full
} IO_MODE_FLAGS;

enum IO_CMD
{
    IO_CMD_GET_DATA_COUNT,
    IO_CMD_GET_ELEMSIZE,
    IO_CMD_SET_HANDLER,
    IO_CMD_GET_FREE_SIZE,
    IO_CMD_RESET
};

int io_ioctl(short id, int cmd, ...);
int io_init(void *buf, int len);
int io_open(short id, int cnt, int size, unsigned int mode);
int io_read(short id, void *buf, int len);
int io_write(short id, const void *buf, int len);
int io_select(int rds_count, short rds_arr[], short *rds_res, int timeout);

#ifdef __cplusplus
}
#endif

#endif  // _RTOS_H_
