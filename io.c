/*
 * io.c
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "rtos.h"
#include "fifo.h"

#define IO_MAX_NUM 100

#define IO_DEB 1

typedef struct
{
    int size;
    int cnt;
    short mode;
    short id;

    void (*handler)(short);

    Fifo *fifo;
    SEM_ID sem_op;
    SEM_ID sem;
    SEM_ID *sem_select;
} IO_STREAM_REC;

typedef struct
{
    IO_STREAM_REC stream;
} IO_REC;

typedef struct
{
    char *mem_ptr;
    int mem_size;
    IO_REC io_descr[IO_MAX_NUM];
    char *io_buf;
} IO_DATA;

static IO_DATA io_data;

#define GET_IO_DATA_PTR() ((IO_DATA *)&io_data)
#define GET_IO_REC_PTR(X) (&iptr->io_descr[X].stream)
#define IS_OPENED(X) ((X)->cnt)

static void *io_allocate_mem(int size)
{
    IO_DATA *iptr;
    char *ret;
    iptr = GET_IO_DATA_PTR();
    if(iptr->mem_ptr + size > iptr->io_buf + iptr->mem_size)
    {
        return 0;
    }
    ret = iptr->mem_ptr;
    iptr->mem_ptr += size;
    if((unsigned int) iptr->mem_ptr & 3)
    {
        iptr->mem_ptr = (char *) ((unsigned int) iptr->mem_ptr & ~3) + 4;
    }
    return ret;
}

int io_init(void *buf, int len)
{
    IO_DATA *iptr;

    iptr = GET_IO_DATA_PTR();
    if(iptr == NULL)
        return IO_ERR;
    iptr->mem_size = (len & ~3) + 4;
    if(iptr->mem_size <= 0)
        return IO_ERR;

    iptr->io_buf = buf;
    iptr->mem_ptr = iptr->io_buf;
    memset(iptr->io_descr, 0, sizeof(iptr->io_descr));
    return IO_OK;
}

int io_open(short id, int cnt, int size, unsigned int mode)
{
    IO_DATA *iptr = GET_IO_DATA_PTR();
    IO_STREAM_REC *pr;
    char *p_buf;
    if(id < 0 || id >= IO_MAX_NUM)
    {
        return IO_ERR;
    }
    pr = GET_IO_REC_PTR(id);

    if(size == 0)
    {
        if(IS_OPENED(pr))
            return IO_OK;
        else
            return IO_ERR;
    }
    if(IS_OPENED(pr))
    {
        return IO_ERR;
    }
    if((pr->fifo = io_allocate_mem(sizeof(Fifo))) && (p_buf = io_allocate_mem(size * cnt)))
    {
        fifo_InitFifo(pr->fifo, (unsigned char *) p_buf, size * cnt);

        pr->fifo->id = id;
        pr->cnt = cnt;
        pr->size = size;
        *(unsigned int *) &pr->mode = mode;
        SemaphoreInit(&pr->sem_op);
        SemaphoreInit(&pr->sem);
        pr->sem_select = NULL;
        pr->id = id;

        if(!(pr->mode & O_NONBLOCK))
        {
            SemaphoreLock(&pr->sem, 0);
        }
        return IO_OK;
    }
    else
    {
        memset(pr, 0, sizeof(IO_STREAM_REC));
        return IO_ERR;
    }
}

int io_read(short id, void *buf, int len)
{
    int ret;
    IO_STREAM_REC *pr;
    IO_DATA *iptr = GET_IO_DATA_PTR();
    if(id < 0 || id >= IO_MAX_NUM)
    {
        return IO_ERR;
    }
    pr = GET_IO_REC_PTR(id);
    if(IS_OPENED(pr))
    {
        if(pr->mode & O_NONBLOCK)
        {
            ret = fifo_ExtrBlock(pr->fifo, (unsigned char *) buf, len);
        }
        else
        {
            int real_len;
            SEM_ID *s;
            s = pr->sem_select != NULL ? pr->sem_select : &pr->sem;
            while((real_len = fifo_GetDataLen(pr->fifo)) < len)
            {
                if(pr->size == 1 && real_len > 0)
                    break;
                SemaphoreLock(s, 0);
            }
            ret = fifo_ExtrBlock(pr->fifo, (unsigned char *) buf, len);
        }
        return (ret);
    }
    return IO_ERR;
}

int io_write(short id, const void *buf, int len)
{
    IO_STREAM_REC *pr;
    int ret;
    IO_DATA *iptr = GET_IO_DATA_PTR();
    if(id < 0 || id >= IO_MAX_NUM)
    {
        return IO_ERR;
    }
    if(len <= 0)
        return (0);

    pr = GET_IO_REC_PTR(id);
    if(IS_OPENED(pr))
    {
        if(pr->mode & O_OVERWRITE)
        {
            // Overwrite element if pipe is full
            ret = fifo_InsBlock_Overwrite(pr->fifo, (unsigned char *) buf, len, 0);
        }
        else
        {
            // Don't add element if pipe is full
            ret = fifo_InsBlock(pr->fifo, (unsigned char *) buf, len);
        }
        if(!(pr->mode & O_NONBLOCK) || pr->sem_select != NULL)
        {
            SEM_ID *sem;
            sem = pr->sem_select != NULL ? pr->sem_select : &pr->sem;
            SemaphoreUnlock(sem);
        }

        if(pr->handler)
            pr->handler(id);
        return (ret);
    }
    return IO_ERR;
}

int io_select(int rds_count, short rds_arr[], short *rds_res, int timeout)
{
    IO_STREAM_REC *pr;
    IO_DATA *iptr = GET_IO_DATA_PTR();
    int i;
    SEM_ID *s = 0;
    int res = IO_UNDEF;

    if(rds_res)
        *rds_res = IO_MAX_NUM;
    for(i = 0; i < rds_count; ++i)
    {
        if(rds_arr[i] < 0 || rds_arr[i] >= IO_MAX_NUM)
        {
            continue;
        }
        pr = GET_IO_REC_PTR(rds_arr[i]);
        if(!IS_OPENED(pr))
        {
            continue;
        }
        if(pr->sem_select != NULL)
        {
            continue;
        }

        if(s == 0)
            s = &pr->sem;

        pr->sem_select = s;
        if(fifo_GetDataLen(pr->fifo) >= pr->size)
        {
            if(rds_res)
                *rds_res = i;
            res = IO_OK;
            break;
        }
    }

    if(res == IO_UNDEF)
        if(SemaphoreLock(s, timeout) == 0)
            res = IO_TIMEOUT;

    for(i = 0; i < rds_count; ++i)
    {
        if(rds_arr[i] < 0 || rds_arr[i] >= IO_MAX_NUM)
        {
            continue;
        }
        pr = GET_IO_REC_PTR(rds_arr[i]);
        if(!IS_OPENED(pr))
        {
            continue;
        }
        if(pr->sem_select != s)
        {
            continue;
        }

        if(res == IO_UNDEF && fifo_GetDataLen(pr->fifo) >= pr->size)
        {
            res = IO_OK;
            if(rds_res && *rds_res == IO_MAX_NUM)
                *rds_res = i;
        }

        pr->sem_select = NULL;
    }

    return (res);
}

int io_ioctl(short id, int cmd, ...)
{
    int res = IO_ERR;
    IO_STREAM_REC *pr;
    IO_DATA *iptr = GET_IO_DATA_PTR();
    va_list arg;

    if(id < 0 || id >= IO_MAX_NUM)
    {
        return IO_ERR;
    }
    pr = GET_IO_REC_PTR(id);

    va_start(arg, cmd);

    if(IS_OPENED(pr))
    {
        switch (cmd)
        {
            case IO_CMD_GET_DATA_COUNT:
            {
                unsigned int *res = va_arg(arg, unsigned int *);
                *res = fifo_GetDataLen(pr->fifo);
                break;
            }

            case IO_CMD_GET_FREE_SIZE:
            {
                unsigned int *res = va_arg(arg, unsigned int *);
                *res = (pr->size * pr->cnt) - fifo_GetDataLen(pr->fifo);
                break;
            }

            case IO_CMD_GET_ELEMSIZE:
            {
                unsigned int *res = va_arg(arg, unsigned int *);
                *res = pr->size;
                break;
            }
        }

        res = IO_OK;
    }

    switch (cmd)
    {
        case IO_CMD_SET_HANDLER:
        {
            unsigned int handler;
            handler = va_arg(arg, unsigned int);
            pr->handler = (void (*)(short)) handler;
            res = IO_OK;
            break;
        }
    }

    va_end(arg);
    return (res);
}
