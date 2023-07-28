/*
 * task.c
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/prctl.h>

#include "rtos.h"

#define STACK_LEN 1000000
#define MAX_THREAD_NUM 100

static __thread struct os_task *cur_task_ptr;
static struct os_task threads[MAX_THREAD_NUM];
static int thread_num = 0;

static sem_t sem_create;
static sem_t sem_start;

struct os_task *os_get_cur_task(void)
{
    return cur_task_ptr;
}

const char *os_get_cur_task_name(void)
{
    if (cur_task_ptr)
        return (const char *)cur_task_ptr->name;
    return "";
}

static void *start_thread_context(void *p)
{
    struct os_task *ppar = (struct os_task *)p;
    if (thread_num < MAX_THREAD_NUM - 1)
    {
        cur_task_ptr = &threads[thread_num++];
        memset(cur_task_ptr, 0, sizeof(*cur_task_ptr));
        cur_task_ptr->priority = ppar->priority;
        cur_task_ptr->entry_func = ppar->entry_func;
        cur_task_ptr->data = ppar->data;
        memcpy(cur_task_ptr->name, ppar->name, sizeof(cur_task_ptr->name));
        sem_init(&cur_task_ptr->sem, 0, 1);
        if (cur_task_ptr->entry_func)
        {
            sem_post(&sem_create);
            sem_wait(&sem_start);
            sem_post(&sem_start);
            os_printf("start thread #%s %p %x\n", cur_task_ptr->name, cur_task_ptr->entry_func,
                      (unsigned int)cur_task_ptr->data);
            cur_task_ptr->entry_func(cur_task_ptr->data);
            os_printf("stop thread #%s\n", cur_task_ptr->name);
        }
    }
    return 0;
}

int os_create_task(const char *name, void (*entry_func)(void *), int priority, void *data)
{
    int res;
    unsigned int sh_policy;
    struct os_task par;
    struct sched_param param;
    int max_task_prior;
    pthread_t thread;
    pthread_t *pthread = &thread;

    pthread_attr_t tattr;

    os_printf("os_create_task %s %p %x\n", name, entry_func, (unsigned int)data);
    pthread_attr_init(&tattr);

    pthread_attr_setstacksize(&tattr, STACK_LEN);
    pthread_attr_getschedparam(&tattr, &param);

    sh_policy = SCHED_OTHER;

    res = pthread_attr_setschedpolicy(&tattr, sh_policy);
    if (res)
    {
        os_printf("pthread_attr_setschedpolicy err %s,ret:%d\n", name, res);
        return -1;
    }
    max_task_prior = sched_get_priority_max(sh_policy);

    param.sched_priority = (max_task_prior - priority);

    if (sh_policy == SCHED_OTHER)
    {
        param.sched_priority = 0;
    }
    res = pthread_attr_setschedparam(&tattr, &param);
    if (res)
    {
        os_printf("pthread_attr_setschedparam err %s,ret:%d\n", name, res);
        return -1;
    }
    prctl(PR_SET_NAME, name, 0, 0, 0);
    par.entry_func = entry_func;
    strncpy(par.name, name, sizeof(par.name) - 1);
    par.name[sizeof(par.name) - 1] = 0;
    par.priority = priority;
    par.data = data;

    res = pthread_create(pthread, &tattr, start_thread_context, (void *)&par);
    if (res)
    {
        os_printf("pthread_create err %s,ret:%d\n", name, res);
        return -1;
    }
    res = pthread_detach(*pthread);
    if (res)
    {
        os_printf("pthread_detach err %s,ret:%d\n", name, res);
        return -1;
    }
    if (sh_policy != SCHED_OTHER)
    {
        res = pthread_setschedparam(*pthread, sh_policy, &param);
        if (res != 0)
        {
            os_printf("pthread_setschedparam err %s\n", strerror(errno));
            return -1;
        }
    }
    sem_wait(&sem_create);
    return 0;
}

void os_init_task(void)
{
    thread_num = 0;
    memset(threads, 0, sizeof(threads));
    sem_init(&sem_create, 0, 0);
    sem_init(&sem_start, 0, 0);
}

void os_start(void)
{
    os_printf("os_start %d\n", thread_num);
    sem_post(&sem_start);
}
